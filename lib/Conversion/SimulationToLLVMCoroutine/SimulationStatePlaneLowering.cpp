//===- SimulationStatePlaneLowering.cpp - Native state planes --------===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/Support/MathExtras.h"

using namespace mlir;

namespace obelisk {

namespace detail {

void notifySignal(ConversionPatternRewriter &builder, Location location,
                  Value handle, uint64_t width, Value oldValue,
                  Value oldUnknown, Value newValue, Value newUnknown,
                  std::optional<DirectStaticStateRange> directRange,
                  schedule::SourceOwnerAttr sourceOwner) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Value address = LLVM::AddressOfOp::create(builder, location, pointer,
                                            "__obelisk_current_context");
  Value context = LLVM::LoadOp::create(builder, location, pointer, address, 8);
  if (directRange && width <= 64) {
    auto scalar = [&](Value value) -> Value {
      if (!value)
        return llvmConstant(builder, location, i64, uint64_t{0});
      if (value.getType() == i64)
        return value;
      return LLVM::ZExtOp::create(builder, location, i64, value);
    };
    Value oldValueScalar = scalar(oldValue);
    Value oldUnknownScalar = scalar(oldUnknown);
    Value newValueScalar = scalar(newValue);
    Value newUnknownScalar = scalar(newUnknown);
    Value changed = arith::OrIOp::create(
        builder, location,
        arith::XOrIOp::create(builder, location, oldValueScalar,
                              newValueScalar),
        arith::XOrIOp::create(builder, location, oldUnknownScalar,
                              newUnknownScalar));
    Value unchanged = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, changed,
        llvmConstant(builder, location, i64, uint64_t{0}));
    Block *head = builder.getInsertionBlock();
    Block *continuation = builder.splitBlock(head, builder.getInsertionPoint());
    Region *region = head->getParent();
    Block *publish = builder.createBlock(region, continuation->getIterator());
    recordStaticSpecializationCFGBlocks(builder, head, 2);
    builder.setInsertionPointToEnd(head);
    cf::CondBranchOp::create(builder, location, unchanged, continuation,
                             ValueRange{}, publish, ValueRange{});
    builder.setInsertionPointToEnd(publish);
    LLVM::CallOp transition = LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(builder.getContext(),
                           "obelisk_rt_v1_scheduler_static_transition"),
        ValueRange{
            context,
            llvmConstant(builder, location, i32, directRange->staticID),
            llvmConstant(builder, location, i64, directRange->localOffset),
            llvmConstant(builder, location, i64, width), oldValueScalar,
            oldUnknownScalar, newValueScalar, newUnknownScalar});
    if (sourceOwner)
      ::obelisk::schedule::set<schedule::metadata::evalSourceOwner>(
          transition, sourceOwner);
    cf::BranchOp::create(builder, location, continuation);
    builder.setInsertionPointToStart(continuation);
    return;
  }
  auto save = [&](Value value) {
    if (!value)
      return LLVM::ZeroOp::create(builder, location, pointer).getResult();
    Value storage = entryAlloca(builder, location, value.getType(), 1, 1);
    LLVM::StoreOp::create(builder, location, value, storage, 1);
    return storage;
  };
  auto transition = LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(builder.getContext(),
                         "obelisk_rt_v1_scheduler_signal_transition"),
      ValueRange{context, handle, llvmConstant(builder, location, i64, width),
                 save(oldValue), save(oldUnknown), save(newValue),
                 save(newUnknown)});
  if (sourceOwner)
    ::obelisk::schedule::set<schedule::metadata::evalSourceOwner>(transition,
                                                                  sourceOwner);
}
std::optional<DirectStaticStateRange>
resolveDirectStaticStateRange(Value handle, unsigned width,
                              const NativeStateLayout *layout) {
  if (!layout || width == 0)
    return std::nullopt;
  std::optional<uint64_t> value = resolveCFGConstantInteger(handle);
  if (!value)
    return std::nullopt;
  obelisk_rt_stable_handle_v1 decoded{};
  if (!obelisk_rt_stable_handle_decode(*value, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset < 0)
    return std::nullopt;
  bool direct = layout->directHandles.contains(decoded.id);
  bool guarded = layout->guardedHandles.contains(decoded.id);
  if (!direct && !guarded)
    return std::nullopt;
  auto bound = llvm::find_if(layout->bounds, [&](const auto &candidate) {
    return candidate.handleID == decoded.id;
  });
  // Direct accesses are authorized either by the specialization policy or by
  // a fully static VPI-off wide-NBA plan, which records its roots in the same
  // direct-handle inventory before conversion.
  if (bound == layout->bounds.end() ||
      static_cast<uint64_t>(decoded.offset) > bound->width ||
      width > bound->width - static_cast<uint64_t>(decoded.offset))
    return std::nullopt;
  return DirectStaticStateRange{
      bound->offset + static_cast<uint64_t>(decoded.offset),
      static_cast<uint64_t>(decoded.offset), decoded.id, guarded};
}

struct DirectPackedPlane {
  Value address;
  IntegerType spanType;
  Value span;
  unsigned bitOffset;
};

struct DirectDynamicStateRange {
  Value valid;
  Value bitOffset;
  uint64_t rootOffset;
  uint64_t localOffset;
  uint64_t rootWidth;
  uint32_t staticID;
  bool guarded;
};

std::optional<DirectDynamicStateRange>
resolveDirectDynamicStateRange(Value handle, unsigned width,
                               const NativeStateLayout *layout) {
  if (!layout || width == 0)
    return std::nullopt;
  Operation *producer = handle.getDefiningOp();
  if (!producer)
    return std::nullopt;
  // IEEE 1800-2023 7.4.5, 11.5.1: any invalid enclosing index denotes no
  // storage. Retain every guard and handle-offset validity check when
  // resolving nested aggregate fields to one fixed physical root.
  SmallVector<Value> guards, amounts;
  Value root = handle;
  while (true) {
    if (auto selected = root.getDefiningOp<arith::SelectOp>()) {
      if (resolveCFGConstantInteger(selected.getFalseValue()) !=
          std::optional<uint64_t>{UINT64_MAX})
        return std::nullopt;
      guards.push_back(selected.getCondition());
      root = selected.getTrueValue();
      continue;
    }
    auto offset = root.getDefiningOp<LLVM::CallOp>();
    if (!offset || !offset.getCallee() ||
        *offset.getCallee() != "obelisk_rt_v1_native_handle_offset" ||
        offset.getArgOperands().size() != 2)
      break;
    amounts.push_back(offset.getArgOperands()[1]);
    root = offset.getArgOperands()[0];
  }
  if (guards.empty() || amounts.empty())
    return std::nullopt;
  std::optional<uint64_t> base = resolveCFGConstantInteger(root);
  obelisk_rt_stable_handle_v1 decoded{};
  if (!base || !obelisk_rt_stable_handle_decode(*base, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset < 0)
    return std::nullopt;
  bool direct = layout->directHandles.contains(decoded.id);
  bool guarded = layout->guardedHandles.contains(decoded.id);
  if (!direct && !guarded)
    return std::nullopt;
  auto bound = llvm::find_if(layout->bounds, [&](const auto &candidate) {
    return candidate.handleID == decoded.id;
  });
  uint64_t localOffset = static_cast<uint64_t>(decoded.offset);
  if (bound == layout->bounds.end() || localOffset > bound->width ||
      width > bound->width - localOffset)
    return std::nullopt;
  OpBuilder builder(producer);
  Location location = producer->getLoc();
  Value valid = guards.front();
  for (Value next : ArrayRef<Value>(guards).drop_front())
    valid = arith::AndIOp::create(builder, location, valid, next);
  auto i64 = builder.getI64Type();
  Value amount = llvmConstant(builder, location, i64, localOffset);
  for (Value next : llvm::reverse(amounts)) {
    Value representable = arith::AndIOp::create(
        builder, location,
        arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::sge, next,
            arith::SubIOp::create(
                builder, location,
                llvmConstant(builder, location, i64, INT32_MIN), amount)),
        arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::sle, next,
            arith::SubIOp::create(
                builder, location,
                llvmConstant(builder, location, i64, INT32_MAX), amount)));
    valid = arith::AndIOp::create(builder, location, valid, representable);
    next = arith::SelectOp::create(builder, location, representable, next,
                                   llvmConstant(builder, location, i64, 0));
    amount = arith::AddIOp::create(builder, location, amount, next);
  }
  return DirectDynamicStateRange{valid,        amount,     bound->offset, 0,
                                 bound->width, decoded.id, guarded};
}

Value loadDirectDynamicPackedPlane(ConversionPatternRewriter &rewriter,
                                   Location location, StringRef globalName,
                                   IntegerType resultType,
                                   const DirectDynamicStateRange &range,
                                   bool unknownFallback) {
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  IntegerType i8 = rewriter.getI8Type();
  IntegerType i64 = rewriter.getI64Type();
  Value zero = llvmConstant(rewriter, location, i64, 0);
  Value minimum = llvmConstant(
      rewriter, location, i64,
      static_cast<uint64_t>(-static_cast<int64_t>(resultType.getWidth() - 1)));
  Value maximum = llvmConstant(rewriter, location, i64, range.rootWidth - 1);
  Value overlapsLow = arith::CmpIOp::create(
      rewriter, location, arith::CmpIPredicate::sge, range.bitOffset, minimum);
  Value overlapsHigh = arith::CmpIOp::create(
      rewriter, location, arith::CmpIPredicate::sle, range.bitOffset, maximum);
  Value encodesLow = arith::CmpIOp::create(
      rewriter, location, arith::CmpIPredicate::sge, range.bitOffset,
      llvmConstant(rewriter, location, i64, static_cast<uint64_t>(INT32_MIN)));
  Value encodesHigh = arith::CmpIOp::create(
      rewriter, location, arith::CmpIPredicate::sle, range.bitOffset,
      llvmConstant(rewriter, location, i64, INT32_MAX));
  Value valid = arith::AndIOp::create(
      rewriter, location, range.valid,
      arith::AndIOp::create(
          rewriter, location,
          arith::AndIOp::create(rewriter, location, overlapsLow, overlapsHigh),
          arith::AndIOp::create(rewriter, location, encodesLow, encodesHigh)));

  Block *head = rewriter.getInsertionBlock();
  Block *continuation = rewriter.splitBlock(head, rewriter.getInsertionPoint());
  BlockArgument result = continuation->addArgument(resultType, location);
  Region *region = head->getParent();
  Block *load = rewriter.createBlock(region, continuation->getIterator());
  Block *fallback = rewriter.createBlock(region, continuation->getIterator());
  unsigned minimumSpanWidth = ((resultType.getWidth() + 7) / 8) * 8;
  unsigned maximumSpanWidth = ((7 + resultType.getWidth() + 7) / 8) * 8;
  recordStaticSpecializationCFGBlocks(
      rewriter, head, minimumSpanWidth == maximumSpanWidth ? 3 : 6);
  rewriter.setInsertionPointToEnd(head);
  cf::CondBranchOp::create(rewriter, location, valid, load, ValueRange{},
                           fallback, ValueRange{});

  rewriter.setInsertionPointToEnd(load);
  Value negative = arith::CmpIOp::create(
      rewriter, location, arith::CmpIPredicate::slt, range.bitOffset, zero);
  Value nonnegative = arith::SelectOp::create(rewriter, location, negative,
                                              zero, range.bitOffset);
  Value lastFullStart = llvmConstant(rewriter, location, i64,
                                     range.rootWidth - resultType.getWidth());
  Value aboveLastFullStart =
      arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::ugt,
                            nonnegative, lastFullStart);
  Value clamped = arith::SelectOp::create(
      rewriter, location, aboveLastFullStart, lastFullStart, nonnegative);
  Value delta =
      arith::SubIOp::create(rewriter, location, range.bitOffset, clamped);
  Value deltaNegative = arith::CmpIOp::create(
      rewriter, location, arith::CmpIPredicate::slt, delta, zero);
  Value negatedDelta = arith::SubIOp::create(rewriter, location, zero, delta);
  Value absoluteDelta = arith::SelectOp::create(
      rewriter, location, deltaNegative, negatedDelta, delta);
  Value absolute =
      arith::AddIOp::create(rewriter, location, clamped,
                            llvmConstant(rewriter, location, i64,
                                         range.rootOffset + range.localOffset));
  Value byteOffset = arith::ShRUIOp::create(
      rewriter, location, absolute,
      llvmConstant(rewriter, location, i64, uint64_t{3}));
  Value firstBit =
      arith::AndIOp::create(rewriter, location, absolute,
                            llvmConstant(rewriter, location, i64, uint64_t{7}));
  Value base =
      LLVM::AddressOfOp::create(rewriter, location, pointer, globalName);
  Value address = LLVM::GEPOp::create(rewriter, location, pointer, i8, base,
                                      ValueRange{byteOffset});
  auto loadSpan = [&](unsigned spanWidth) {
    IntegerType spanType = rewriter.getIntegerType(spanWidth);
    Value span = LLVM::LoadOp::create(rewriter, location, spanType, address, 1);
    Value firstBitShift =
        spanWidth == 64 ? firstBit
        : spanWidth < 64
            ? LLVM::TruncOp::create(rewriter, location, spanType, firstBit)
                  .getResult()
            : LLVM::ZExtOp::create(rewriter, location, spanType, firstBit)
                  .getResult();
    Value shifted =
        arith::ShRUIOp::create(rewriter, location, span, firstBitShift);
    return spanType == resultType
               ? shifted
               : LLVM::TruncOp::create(rewriter, location, resultType, shifted)
                     .getResult();
  };
  Value loaded;
  if (minimumSpanWidth == maximumSpanWidth) {
    loaded = loadSpan(minimumSpanWidth);
  } else {
    Block *narrow = rewriter.createBlock(region, continuation->getIterator());
    Block *wide = rewriter.createBlock(region, continuation->getIterator());
    Block *loadedJoin =
        rewriter.createBlock(region, continuation->getIterator());
    BlockArgument joined = loadedJoin->addArgument(resultType, location);
    rewriter.setInsertionPointToEnd(load);
    Value useNarrow = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::ule, firstBit,
        llvmConstant(rewriter, location, i64,
                     minimumSpanWidth - resultType.getWidth()));
    cf::CondBranchOp::create(rewriter, location, useNarrow, narrow,
                             ValueRange{}, wide, ValueRange{});
    rewriter.setInsertionPointToEnd(narrow);
    Value narrowValue = loadSpan(minimumSpanWidth);
    cf::BranchOp::create(rewriter, location, loadedJoin,
                         ValueRange{narrowValue});
    rewriter.setInsertionPointToEnd(wide);
    Value wideValue = loadSpan(maximumSpanWidth);
    cf::BranchOp::create(rewriter, location, loadedJoin, ValueRange{wideValue});
    rewriter.setInsertionPointToStart(loadedJoin);
    loaded = joined;
  }
  Value resultShift =
      resultType.getWidth() == 64 ? absoluteDelta
      : resultType.getWidth() > 64
          ? LLVM::ZExtOp::create(rewriter, location, resultType, absoluteDelta)
                .getResult()
          : LLVM::TruncOp::create(rewriter, location, resultType, absoluteDelta)
                .getResult();
  Value alignedLeft =
      arith::ShLIOp::create(rewriter, location, loaded, resultShift);
  Value alignedRight =
      arith::ShRUIOp::create(rewriter, location, loaded, resultShift);
  Value aligned = arith::SelectOp::create(rewriter, location, deltaNegative,
                                          alignedLeft, alignedRight);

  IntegerType maskType = rewriter.getIntegerType(resultType.getWidth() + 1);
  Value maskWidth =
      maskType.getWidth() == 64 ? absoluteDelta
      : maskType.getWidth() < 64
          ? LLVM::TruncOp::create(rewriter, location, maskType, absoluteDelta)
                .getResult()
          : LLVM::ZExtOp::create(rewriter, location, maskType, absoluteDelta)
                .getResult();
  Value overlapWidth = arith::SubIOp::create(
      rewriter, location,
      llvmConstant(rewriter, location, maskType, resultType.getWidth()),
      maskWidth);
  Value lowMask = arith::SubIOp::create(
      rewriter, location,
      arith::ShLIOp::create(rewriter, location,
                            llvmConstant(rewriter, location, maskType, 1),
                            overlapWidth),
      llvmConstant(rewriter, location, maskType, 1));
  Value outputStart =
      arith::SelectOp::create(rewriter, location, deltaNegative, maskWidth,
                              llvmConstant(rewriter, location, maskType, 0));
  Value mask = LLVM::TruncOp::create(
      rewriter, location, resultType,
      arith::ShLIOp::create(rewriter, location, lowMask, outputStart));
  Value masked = arith::AndIOp::create(rewriter, location, aligned, mask);
  if (unknownFallback)
    masked = arith::OrIOp::create(
        rewriter, location, masked,
        arith::XOrIOp::create(
            rewriter, location, mask,
            arith::ConstantOp::create(
                rewriter, location, resultType,
                rewriter.getIntegerAttr(
                    resultType, APInt::getAllOnes(resultType.getWidth())))));
  cf::BranchOp::create(rewriter, location, continuation, ValueRange{masked});

  rewriter.setInsertionPointToEnd(fallback);
  APInt fallbackValue = unknownFallback
                            ? APInt::getAllOnes(resultType.getWidth())
                            : APInt(resultType.getWidth(), 0);
  Value invalid = arith::ConstantOp::create(
      rewriter, location, resultType,
      rewriter.getIntegerAttr(resultType, fallbackValue));
  cf::BranchOp::create(rewriter, location, continuation, ValueRange{invalid});
  rewriter.setInsertionPointToStart(continuation);
  return result;
}

DirectPackedPlane loadDirectPackedPlane(OpBuilder &builder, Location location,
                                        StringRef globalName,
                                        uint64_t bitOffset, unsigned width) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  IntegerType i8 = builder.getI8Type();
  IntegerType i64 = builder.getI64Type();
  uint64_t firstByte = bitOffset / 8;
  unsigned firstBit = static_cast<unsigned>(bitOffset % 8);
  unsigned byteCount = (firstBit + width + 7) / 8;
  IntegerType spanType = builder.getIntegerType(byteCount * 8);
  Value base =
      LLVM::AddressOfOp::create(builder, location, pointer, globalName);
  if (globalName.starts_with("__obelisk_continuous_"))
    base = LLVM::LoadOp::create(builder, location, pointer, base, 8);
  Value address = LLVM::GEPOp::create(
      builder, location, pointer, i8, base,
      ValueRange{llvmConstant(builder, location, i64, firstByte)});
  Value span = LLVM::LoadOp::create(builder, location, spanType, address, 1);
  return {address, spanType, span, firstBit};
}

Value extractDirectPackedPlane(OpBuilder &builder, Location location,
                               const DirectPackedPlane &plane,
                               IntegerType resultType) {
  Value shifted = plane.span;
  if (plane.bitOffset != 0)
    shifted = arith::ShRUIOp::create(
        builder, location, shifted,
        llvmConstant(builder, location, plane.spanType, plane.bitOffset));
  if (shifted.getType() == resultType)
    return shifted;
  return LLVM::TruncOp::create(builder, location, resultType, shifted);
}

Value storeDirectPackedPlane(OpBuilder &builder, Location location, Value input,
                             StringRef globalName, uint64_t bitOffset,
                             bool trackChange) {
  IntegerType inputType = cast<IntegerType>(input.getType());
  DirectPackedPlane plane = loadDirectPackedPlane(
      builder, location, globalName, bitOffset, inputType.getWidth());
  APInt fieldMask =
      APInt::getBitsSet(plane.spanType.getWidth(), plane.bitOffset,
                        plane.bitOffset + inputType.getWidth());
  Value mask = arith::ConstantOp::create(
      builder, location, plane.spanType,
      builder.getIntegerAttr(plane.spanType, fieldMask));
  Value preserved = arith::AndIOp::create(
      builder, location, plane.span,
      arith::XOrIOp::create(
          builder, location, mask,
          arith::ConstantOp::create(
              builder, location, plane.spanType,
              builder.getIntegerAttr(
                  plane.spanType,
                  APInt::getAllOnes(plane.spanType.getWidth())))));
  Value extended =
      inputType == plane.spanType
          ? input
          : LLVM::ZExtOp::create(builder, location, plane.spanType, input);
  if (plane.bitOffset != 0)
    extended = arith::ShLIOp::create(
        builder, location, extended,
        llvmConstant(builder, location, plane.spanType, plane.bitOffset));
  Value updated = arith::OrIOp::create(
      builder, location, preserved,
      arith::AndIOp::create(builder, location, extended, mask));
  LLVM::StoreOp::create(builder, location, updated, plane.address, 1);
  if (!trackChange)
    return llvmConstant(builder, location, builder.getI1Type(), 0);
  Value old = extractDirectPackedPlane(builder, location, plane, inputType);
  return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne, old,
                               input);
}

} // namespace detail

namespace detail {

Value loadStatePlane(ConversionPatternRewriter &rewriter, Location location,
                     Value handle, IntegerType resultType, StringRef globalName,
                     bool unknownFallback, uint64_t stateBitCount,
                     const NativeStateLayout *directLayout,
                     Value guardedPermission, bool assumeClean) {
  std::optional<DirectStaticStateRange> range = resolveDirectStaticStateRange(
      handle, resultType.getWidth(), directLayout);
  if (range && (!range->guarded || assumeClean))
    return extractDirectPackedPlane(
        rewriter, location,
        loadDirectPackedPlane(rewriter, location, globalName, range->offset,
                              resultType.getWidth()),
        resultType);
  std::optional<DirectDynamicStateRange> dynamicRange =
      resolveDirectDynamicStateRange(handle, resultType.getWidth(),
                                     directLayout);
  if (dynamicRange && (!dynamicRange->guarded || assumeClean))
    return loadDirectDynamicPackedPlane(rewriter, location, globalName,
                                        resultType, *dynamicRange,
                                        unknownFallback);

  auto emitGeneric = [&]() -> Value {
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    IntegerType i32 = rewriter.getI32Type();
    IntegerType i64 = rewriter.getI64Type();
    Value base =
        LLVM::AddressOfOp::create(rewriter, location, pointer, globalName);
    Value out = entryAlloca(rewriter, location, resultType, 1, 1);
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    LLVM::CallOp::create(
        rewriter, location, TypeRange{i32},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_native_state_load_plane"),
        ValueRange{
            context, base, llvmConstant(rewriter, location, i64, stateBitCount),
            handle,
            llvmConstant(rewriter, location, i64, resultType.getWidth()),
            llvmConstant(rewriter, location, i32,
                         globalName == "__obelisk_state_unknown" ? 1 : 0),
            llvmConstant(rewriter, location, i32, unknownFallback ? 1 : 0),
            out});
    return LLVM::LoadOp::create(rewriter, location, resultType, out, 1);
  };
  if ((!range || !range->guarded) && (!dynamicRange || !dynamicRange->guarded))
    return emitGeneric();

  Block *head = rewriter.getInsertionBlock();
  Block *continuation = rewriter.splitBlock(head, rewriter.getInsertionPoint());
  BlockArgument result = continuation->addArgument(resultType, location);
  Region *region = head->getParent();
  Block *directBlock =
      rewriter.createBlock(region, continuation->getIterator());
  Block *genericBlock =
      rewriter.createBlock(region, continuation->getIterator());
  recordStaticSpecializationCFGBlocks(rewriter, head, 3);

  rewriter.setInsertionPointToEnd(head);
  Value useDirect = guardedPermission
                        ? guardedPermission
                        : staticSpecializationGuard(
                              rewriter, location,
                              range ? range->staticID : dynamicRange->staticID,
                              OBELISK_RT_STATIC_ROOT_READ);
  markLikelyTrue(cf::CondBranchOp::create(rewriter, location, useDirect,
                                          directBlock, ValueRange{},
                                          genericBlock, ValueRange{}));

  rewriter.setInsertionPointToEnd(directBlock);
  // The guard certifies storage addressing, not the selected value's
  // knownness. Keep the dynamic range's own bounds and X/Z fallback checks.
  Value direct =
      range ? extractDirectPackedPlane(
                  rewriter, location,
                  loadDirectPackedPlane(rewriter, location, globalName,
                                        range->offset, resultType.getWidth()),
                  resultType)
            : loadDirectDynamicPackedPlane(rewriter, location, globalName,
                                           resultType, *dynamicRange,
                                           unknownFallback);
  cf::BranchOp::create(rewriter, location, continuation, ValueRange{direct});

  rewriter.setInsertionPointToEnd(genericBlock);
  Value generic = emitGeneric();
  cf::BranchOp::create(rewriter, location, continuation, ValueRange{generic});

  rewriter.setInsertionPointToStart(continuation);
  return result;
}

Value storeStatePlane(ConversionPatternRewriter &rewriter, Location location,
                      Value handle, Value input, StringRef globalName,
                      uint64_t stateBitCount,
                      const NativeStateLayout *directLayout,
                      Value guardedPermission, bool assumeClean,
                      bool trackChange, bool continuous) {
  IntegerType inputType = cast<IntegerType>(input.getType());
  std::optional<DirectStaticStateRange> range =
      resolveDirectStaticStateRange(handle, inputType.getWidth(), directLayout);
  if (range && continuous && (assumeClean || !range->guarded) &&
      directLayout->directContinuous) {
    // Keep the actual contribution even when no writer is attached. These
    // pointers bind the runtime's own retained planes, so a later force,
    // deposit, or release needs neither reconstruction nor a shadow import.
    StringRef retained = globalName == "__obelisk_state_unknown"
                             ? "__obelisk_continuous_unknown"
                             : "__obelisk_continuous_value";
    storeDirectPackedPlane(rewriter, location, input, retained, range->offset,
                           false);
    if (globalName == "__obelisk_state_value") {
      Value mask = arith::ConstantOp::create(
          rewriter, location, inputType,
          rewriter.getIntegerAttr(inputType,
                                  APInt::getAllOnes(inputType.getWidth())));
      storeDirectPackedPlane(rewriter, location, mask,
                             "__obelisk_continuous_mask", range->offset, false);
    }
    return storeDirectPackedPlane(rewriter, location, input, globalName,
                                  range->offset, trackChange);
  }
  // Unguarded roots can never be forced or procedurally assigned, so they do
  // not need a retained continuous value for release. Guarded continuous
  // roots still need the runtime path even in an assume-clean specialization:
  // the clean publication may precede a later force.
  if (range && (!range->guarded || (!continuous && assumeClean)))
    return storeDirectPackedPlane(rewriter, location, input, globalName,
                                  range->offset, trackChange);

  auto emitGeneric = [&]() -> Value {
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    IntegerType i8 = rewriter.getI8Type();
    IntegerType i32 = rewriter.getI32Type();
    IntegerType i64 = rewriter.getI64Type();
    Value base =
        LLVM::AddressOfOp::create(rewriter, location, pointer, globalName);
    Value in = entryAlloca(rewriter, location, inputType, 1, 1);
    LLVM::StoreOp::create(rewriter, location, input, in, 1);
    Value changed = entryAlloca(rewriter, location, i8, 1, 1);
    LLVM::StoreOp::create(rewriter, location,
                          llvmConstant(rewriter, location, i8, 0), changed, 1);
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    LLVM::CallOp::create(
        rewriter, location, TypeRange{i32},
        SymbolRefAttr::get(
            rewriter.getContext(),
            continuous ? "obelisk_rt_v1_native_state_store_continuous_plane"
                       : "obelisk_rt_v1_native_state_store_plane"),
        ValueRange{
            context, base, llvmConstant(rewriter, location, i64, stateBitCount),
            handle, llvmConstant(rewriter, location, i64, inputType.getWidth()),
            llvmConstant(rewriter, location, i32,
                         globalName == "__obelisk_state_unknown" ? 1 : 0),
            in, changed});
    Value changedByte =
        LLVM::LoadOp::create(rewriter, location, i8, changed, 1);
    return arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::ne,
                                 changedByte,
                                 llvmConstant(rewriter, location, i8, 0));
  };
  if (continuous || !range || !range->guarded)
    return emitGeneric();

  Block *head = rewriter.getInsertionBlock();
  Block *continuation = rewriter.splitBlock(head, rewriter.getInsertionPoint());
  BlockArgument changed =
      continuation->addArgument(rewriter.getI1Type(), location);
  Region *region = head->getParent();
  Block *directBlock =
      rewriter.createBlock(region, continuation->getIterator());
  Block *genericBlock =
      rewriter.createBlock(region, continuation->getIterator());
  recordStaticSpecializationCFGBlocks(rewriter, head, 3);

  rewriter.setInsertionPointToEnd(head);
  Value useDirect =
      guardedPermission
          ? guardedPermission
          : staticSpecializationGuard(rewriter, location, range->staticID,
                                      OBELISK_RT_STATIC_ROOT_WRITE);
  markLikelyTrue(cf::CondBranchOp::create(rewriter, location, useDirect,
                                          directBlock, ValueRange{},
                                          genericBlock, ValueRange{}));

  rewriter.setInsertionPointToEnd(directBlock);
  Value directChanged = storeDirectPackedPlane(
      rewriter, location, input, globalName, range->offset, trackChange);
  cf::BranchOp::create(rewriter, location, continuation,
                       ValueRange{directChanged});

  rewriter.setInsertionPointToEnd(genericBlock);
  Value genericChanged = emitGeneric();
  cf::BranchOp::create(rewriter, location, continuation,
                       ValueRange{genericChanged});

  rewriter.setInsertionPointToStart(continuation);
  return changed;
}

bool emitDirectDynamicPackedStore(ConversionPatternRewriter &rewriter,
                                  Location location, Value handle, Value value,
                                  Value unknown,
                                  const NativeStateLayout *layout,
                                  bool assumeClean, bool continuous,
                                  bool twoState,
                                  schedule::SourceOwnerAttr sourceOwner) {
  auto inputType = dyn_cast<IntegerType>(value.getType());
  if (!inputType || !layout || !layout->transitionHandlesExact)
    return false;
  auto range =
      resolveDirectDynamicStateRange(handle, inputType.getWidth(), layout);
  if (!range || (range->guarded && (continuous || !assumeClean)))
    return false;
  if (range->rootWidth > 64) {
    // Touch only the selected byte window, independently of the aggregate
    // root's size. Clip partial and invalid selects exactly as LRM 11.5.1
    // requires, and publish after both planes have been stored (4.6(a)).
    auto i64 = rewriter.getI64Type();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value zero = llvmConstant(rewriter, location, i64, 0);
    Value offset = range->bitOffset;
    Value valid = arith::AndIOp::create(
        rewriter, location, range->valid,
        arith::AndIOp::create(
            rewriter, location,
            arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::sgt,
                                  offset,
                                  llvmConstant(rewriter, location, i64,
                                               -int64_t(inputType.getWidth()))),
            arith::CmpIOp::create(
                rewriter, location, arith::CmpIPredicate::slt, offset,
                llvmConstant(rewriter, location, i64, range->rootWidth))));
    Value safe =
        arith::SelectOp::create(rewriter, location, valid, offset, zero);
    Value negative = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::slt, safe, zero);
    Value last = llvmConstant(rewriter, location, i64,
                              range->rootWidth - inputType.getWidth());
    Value above = arith::CmpIOp::create(rewriter, location,
                                        arith::CmpIPredicate::sgt, safe, last);
    Value clamped = arith::SelectOp::create(
        rewriter, location, above, last,
        arith::SelectOp::create(rewriter, location, negative, zero, safe));
    Value lowClip = arith::SelectOp::create(
        rewriter, location, negative,
        arith::SubIOp::create(rewriter, location, zero, safe), zero);
    Value highClip = arith::SelectOp::create(
        rewriter, location, above,
        arith::SubIOp::create(rewriter, location, safe, last), zero);
    Value absolute = arith::AddIOp::create(
        rewriter, location, clamped,
        llvmConstant(rewriter, location, i64, range->rootOffset));
    Value byte = arith::ShRUIOp::create(
        rewriter, location, absolute, llvmConstant(rewriter, location, i64, 3));
    Value bit = arith::AndIOp::create(rewriter, location, absolute,
                                      llvmConstant(rewriter, location, i64, 7));
    auto windowType = rewriter.getIntegerType(
        llvm::alignTo(inputType.getWidth() + 7, uint64_t{8}));
    auto resize = [&](Value value, IntegerType type) -> Value {
      if (value.getType() == type)
        return value;
      return cast<IntegerType>(value.getType()).getWidth() < type.getWidth()
                 ? LLVM::ZExtOp::create(rewriter, location, type, value)
                       .getResult()
                 : LLVM::TruncOp::create(rewriter, location, type, value)
                       .getResult();
    };
    Value left = resize(highClip, windowType);
    Value right = resize(lowClip, windowType);
    Value shift = resize(bit, windowType);
    Value mask = arith::ConstantOp::create(
        rewriter, location, windowType,
        rewriter.getIntegerAttr(
            windowType,
            APInt::getLowBitsSet(windowType.getWidth(), inputType.getWidth())));
    mask = arith::ShLIOp::create(
        rewriter, location,
        arith::ShRUIOp::create(rewriter, location, mask, right), left);
    mask = arith::ShLIOp::create(rewriter, location, mask, shift);
    mask = arith::SelectOp::create(
        rewriter, location, valid, mask,
        llvmConstant(rewriter, location, windowType, 0));
    auto store = [&](StringRef plane, Value input) {
      Value base =
          LLVM::AddressOfOp::create(rewriter, location, pointer, plane);
      Value address =
          LLVM::GEPOp::create(rewriter, location, pointer, rewriter.getI8Type(),
                              base, ValueRange{byte});
      Value positioned = input
                             ? resize(input, windowType)
                             : llvmConstant(rewriter, location, windowType, 0);
      positioned = arith::ShLIOp::create(
          rewriter, location,
          arith::ShLIOp::create(
              rewriter, location,
              arith::ShRUIOp::create(rewriter, location, positioned, right),
              left),
          shift);
      auto writeWindow = [&](unsigned width) {
        auto spanType = rewriter.getIntegerType(width);
        Value old = resize(
            LLVM::LoadOp::create(rewriter, location, spanType, address, 1),
            windowType);
        Value updated = arith::XOrIOp::create(
            rewriter, location, old,
            arith::AndIOp::create(
                rewriter, location, mask,
                arith::XOrIOp::create(rewriter, location, old, positioned)));
        LLVM::StoreOp::create(rewriter, location, resize(updated, spanType),
                              address, 1);
        return std::pair<Value, Value>{
            resize(arith::ShRUIOp::create(rewriter, location, old, shift),
                   inputType),
            resize(arith::ShRUIOp::create(rewriter, location, updated, shift),
                   inputType)};
      };
      unsigned minimumWidth = llvm::alignTo(inputType.getWidth(), uint64_t{8});
      if (minimumWidth == windowType.getWidth())
        return writeWindow(minimumWidth);
      // IEEE 1800-2023 11.5.1: touching the selected storage cannot access
      // beyond the plane. Aligned windows need no speculative trailing byte.
      Block *head = rewriter.getInsertionBlock();
      Block *join = rewriter.splitBlock(head, rewriter.getInsertionPoint());
      Value old = join->addArgument(inputType, location);
      Value updated = join->addArgument(inputType, location);
      Block *narrow =
          rewriter.createBlock(head->getParent(), join->getIterator());
      Block *wide =
          rewriter.createBlock(head->getParent(), join->getIterator());
      recordStaticSpecializationCFGBlocks(rewriter, head, 3);
      rewriter.setInsertionPointToEnd(head);
      Value needsWide = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::ugt, bit,
          llvmConstant(rewriter, location, i64,
                       minimumWidth - inputType.getWidth()));
      cf::CondBranchOp::create(rewriter, location, needsWide, wide, narrow);
      for (auto [block, width] : {std::pair{narrow, minimumWidth},
                                  std::pair{wide, windowType.getWidth()}}) {
        rewriter.setInsertionPointToEnd(block);
        auto [previous, next] = writeWindow(width);
        cf::BranchOp::create(rewriter, location, join,
                             ValueRange{previous, next});
      }
      rewriter.setInsertionPointToStart(join);
      return std::pair<Value, Value>{old, updated};
    };
    auto [oldValue, newValue] = store("__obelisk_state_value", value);
    Value oldUnknown = llvmConstant(rewriter, location, inputType, 0);
    Value newUnknown = oldUnknown;
    if (!twoState)
      std::tie(oldUnknown, newUnknown) =
          store("__obelisk_state_unknown", unknown);
    if (layout->transitionHandles.contains(range->staticID)) {
      Value context = LLVM::LoadOp::create(
          rewriter, location, pointer,
          LLVM::AddressOfOp::create(rewriter, location, pointer,
                                    "__obelisk_current_context"),
          8);
      for (unsigned low = 0; low < inputType.getWidth(); low += 64) {
        unsigned width = std::min(64u, inputType.getWidth() - low);
        auto chunk = [&](Value value) -> Value {
          if (low)
            value = arith::ShRUIOp::create(
                rewriter, location, value,
                llvmConstant(rewriter, location, inputType, low));
          value = resize(value, rewriter.getIntegerType(width));
          return resize(value, i64);
        };
        auto transition = LLVM::CallOp::create(
            rewriter, location, TypeRange{},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_static_transition"),
            ValueRange{context,
                       llvmConstant(rewriter, location, rewriter.getI32Type(),
                                    range->staticID),
                       arith::AddIOp::create(
                           rewriter, location, clamped,
                           llvmConstant(rewriter, location, i64, low)),
                       llvmConstant(rewriter, location, i64, width),
                       chunk(oldValue), chunk(oldUnknown), chunk(newValue),
                       chunk(newUnknown)});
        if (sourceOwner)
          ::obelisk::schedule::set<schedule::metadata::evalSourceOwner>(
              transition, sourceOwner);
      }
    }
    return true;
  }
  // One machine-word root permits an exact read/modify/write without a
  // runtime handle lookup. Invalid selects are no-ops; overhanging selects
  // update only their in-range bits (LRM 11.5.1).
  auto i64 = rewriter.getI64Type();
  auto rootType = rewriter.getIntegerType(range->rootWidth);
  Value zero = llvmConstant(rewriter, location, i64, 0);
  Value offset = range->bitOffset;
  Value aboveBegin = arith::CmpIOp::create(
      rewriter, location, arith::CmpIPredicate::sgt, offset,
      llvmConstant(rewriter, location, i64, -int64_t(inputType.getWidth())));
  Value belowEnd = arith::CmpIOp::create(
      rewriter, location, arith::CmpIPredicate::slt, offset,
      llvmConstant(rewriter, location, i64, range->rootWidth));
  Value valid = arith::AndIOp::create(
      rewriter, location, range->valid,
      arith::AndIOp::create(rewriter, location, aboveBegin, belowEnd));
  // Sanitize before either shift, not merely before the final select.
  Value safe = arith::SelectOp::create(rewriter, location, valid, offset, zero);
  Value negative = arith::CmpIOp::create(rewriter, location,
                                         arith::CmpIPredicate::slt, safe, zero);
  Value right = arith::SelectOp::create(
      rewriter, location, negative,
      arith::SubIOp::create(rewriter, location, zero, safe), zero);
  Value left =
      arith::SelectOp::create(rewriter, location, negative, zero, safe);
  Value mask = llvmConstant(rewriter, location, i64,
                            inputType.getWidth() == 64
                                ? UINT64_MAX
                                : (uint64_t{1} << inputType.getWidth()) - 1);
  mask = arith::ShRUIOp::create(rewriter, location, mask, right);
  mask = arith::ShLIOp::create(rewriter, location, mask, left);
  mask = arith::SelectOp::create(rewriter, location, valid, mask, zero);
  auto extend = [&](Value input) -> Value {
    if (!input)
      return zero;
    return input.getType() == i64
               ? input
               : LLVM::ZExtOp::create(rewriter, location, i64, input)
                     .getResult();
  };
  auto load = [&](StringRef plane) -> Value {
    return extractDirectPackedPlane(
        rewriter, location,
        loadDirectPackedPlane(rewriter, location, plane, range->rootOffset,
                              range->rootWidth),
        rootType);
  };
  auto merge = [&](Value old, Value input) -> Value {
    Value positioned =
        arith::ShRUIOp::create(rewriter, location, extend(input), right);
    positioned = arith::ShLIOp::create(rewriter, location, positioned, left);
    Value previous = extend(old);
    Value merged = arith::XOrIOp::create(
        rewriter, location, previous,
        arith::AndIOp::create(
            rewriter, location, mask,
            arith::XOrIOp::create(rewriter, location, previous, positioned)));
    return rootType == i64
               ? merged
               : LLVM::TruncOp::create(rewriter, location, rootType, merged)
                     .getResult();
  };
  Value oldValue = load("__obelisk_state_value");
  Value newValue = merge(oldValue, value);
  Value oldUnknown = twoState ? llvmConstant(rewriter, location, rootType, 0)
                              : load("__obelisk_state_unknown");
  Value newUnknown = twoState ? oldUnknown : merge(oldUnknown, unknown);
  storeDirectPackedPlane(rewriter, location, newValue, "__obelisk_state_value",
                         range->rootOffset, false);
  if (!twoState)
    storeDirectPackedPlane(rewriter, location, newUnknown,
                           "__obelisk_state_unknown", range->rootOffset, false);
  if (layout->transitionHandles.contains(range->staticID)) {
    Value rootHandle =
        llvmConstant(rewriter, location, i64,
                     obelisk_rt_stable_handle_encode(
                         OBELISK_RT_STABLE_HANDLE_STATIC, range->staticID, 0));
    notifySignal(rewriter, location, rootHandle, range->rootWidth, oldValue,
                 oldUnknown, newValue, newUnknown,
                 DirectStaticStateRange{range->rootOffset, 0, range->staticID,
                                        range->guarded},
                 sourceOwner);
  }
  return true;
}

} // namespace detail

} // namespace obelisk
