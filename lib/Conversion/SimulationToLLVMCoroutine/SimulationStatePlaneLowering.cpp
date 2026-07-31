//===- SimulationStatePlaneLowering.cpp - Native state planes --------===//

#include "SimulationToLLVMCoroutinePrivate.h"

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

void notifySignal(OpBuilder &builder, Location location, Value handle,
                  uint64_t width, Value oldValue, Value oldUnknown,
                  Value newValue, Value newUnknown,
                  std::optional<DirectStaticStateRange> directRange) {
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
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(builder.getContext(),
                           "obelisk_rt_v1_scheduler_static_transition"),
        ValueRange{
            context,
            llvmConstant(builder, location, i32, directRange->staticID),
            llvmConstant(builder, location, i64, directRange->localOffset),
            llvmConstant(builder, location, i64, width), scalar(oldValue),
            scalar(oldUnknown), scalar(newValue), scalar(newUnknown)});
    return;
  }
  auto save = [&](Value value) {
    if (!value)
      return LLVM::ZeroOp::create(builder, location, pointer).getResult();
    Value storage = entryAlloca(builder, location, value.getType(), 1, 1);
    LLVM::StoreOp::create(builder, location, value, storage, 1);
    return storage;
  };
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(builder.getContext(),
                         "obelisk_rt_v1_scheduler_signal_transition"),
      ValueRange{context, handle, llvmConstant(builder, location, i64, width),
                 save(oldValue), save(oldUnknown), save(newValue),
                 save(newUnknown)});
}
std::optional<DirectStaticStateRange>
resolveDirectStaticStateRange(Value handle, unsigned width,
                              const NativeStateLayout *layout) {
  if (!layout || width == 0 || width > 64)
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
  if (!range || !range->guarded)
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
  Value useDirect =
      guardedPermission
          ? guardedPermission
          : staticSpecializationGuard(rewriter, location, range->staticID,
                                      OBELISK_RT_STATIC_ROOT_READ);
  markLikelyTrue(cf::CondBranchOp::create(rewriter, location, useDirect,
                                          directBlock, ValueRange{},
                                          genericBlock, ValueRange{}));

  rewriter.setInsertionPointToEnd(directBlock);
  Value direct = extractDirectPackedPlane(
      rewriter, location,
      loadDirectPackedPlane(rewriter, location, globalName, range->offset,
                            resultType.getWidth()),
      resultType);
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
                      bool trackChange) {
  IntegerType inputType = cast<IntegerType>(input.getType());
  std::optional<DirectStaticStateRange> range =
      resolveDirectStaticStateRange(handle, inputType.getWidth(), directLayout);
  if (range && (!range->guarded || assumeClean))
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
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_native_state_store_plane"),
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
  if (!range || !range->guarded)
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

} // namespace detail

} // namespace obelisk
