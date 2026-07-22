//===- SimulationToLLVMCoroutine.cpp - Native process coroutines ---------===//

#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"

#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Conversion/SimulationToRuntime.h"
#include "obelisk/Conversion/SimulationToStandard.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Analysis/Liveness.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/LLVMCommon/LoweringOptions.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <limits>
#include <set>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTOBELISKSIMPROCESSESTOLLVMCOROUTINESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

constexpr uint64_t kNoOffset = std::numeric_limits<uint64_t>::max();
constexpr uint64_t kWaitHeaderSize = 32;
constexpr uint64_t kWaitEntrySize = 16;
constexpr uint32_t kWaitEdgeNone = std::numeric_limits<uint32_t>::max();
constexpr StringLiteral kAutomaticOwnerReleaseMarker =
    "__obelisk_release_automatic_owner";

constexpr uint64_t kInstanceAllocationOffset = 8;
constexpr uint64_t kInstanceFrameOffset = 16;
constexpr uint64_t kInstanceScratchOffset = 32;
constexpr uint64_t kInstanceNativeHandleOffset = 48;
constexpr uint64_t kInstanceContinuationOffset = 56;
constexpr uint64_t kInstanceStatusOffset = 68;
constexpr uint64_t kInstanceContextOffset = 72;
constexpr uint64_t kInstanceActionOffset = 80;

bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result) {
  if (value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
    return false;
  result = llvm::alignTo(value, alignment);
  return true;
}

uint64_t appendHash(uint64_t hash, uint64_t value, unsigned bytes) {
  for (unsigned index = 0; index != bytes; ++index) {
    hash ^= static_cast<uint8_t>(value >> (index * 8));
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

bool isSuspension(Operation *operation) {
  return isa<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
             sim::SimSuspendEdgeOp, sim::SimSuspendAnyOp,
             sim::SimSuspendEventOp, sim::SimSuspendAwaitOp,
             sim::SimSuspendJoinOp>(operation);
}

uint32_t suspensionKind(Operation *operation) {
  return TypeSwitch<Operation *, uint32_t>(operation)
      .Case<sim::SimSuspendDelayOp>([](auto) { return 1; })
      .Case<sim::SimSuspendChangeOp>([](auto) { return 2; })
      .Case<sim::SimSuspendEdgeOp>([](auto) { return 3; })
      .Case<sim::SimSuspendAnyOp>([](auto) { return 3; })
      .Case<sim::SimSuspendEventOp>([](auto) { return 4; })
      .Case<sim::SimSuspendAwaitOp>([](auto) { return 5; })
      .Case<sim::SimSuspendJoinOp>([](auto) { return 6; })
      .Default([](Operation *) { return 0; });
}

uint32_t waitEntryCount(Operation *operation) {
  return TypeSwitch<Operation *, uint32_t>(operation)
      .Case<sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
            sim::SimSuspendEventOp, sim::SimSuspendAwaitOp>(
          [](auto) { return 1; })
      .Case<sim::SimSuspendAnyOp>(
          [](auto op) { return static_cast<uint32_t>(op.getWatched().size()); })
      .Case<sim::SimSuspendJoinOp>([](auto op) {
        return static_cast<uint32_t>(op.getProcesses().size());
      })
      .Default([](Operation *) { return 0; });
}

struct StorageProperties {
  uint64_t size;
  uint32_t alignment;
  bool fourState;
};

bool containsLogic(Type type);

std::optional<unsigned> nativeStateWidth(Type type) {
  if (std::optional<unsigned> packed = sim::getPackedWidth(type))
    return packed;
  std::optional<uint64_t> span = sim::getProvenanceSpan(type);
  if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type);
      unionType && unionType.getIsTagged() && span) {
    uint64_t tagBits = llvm::Log2_64_Ceil(
        static_cast<uint64_t>(sim::getAggregateNumElements(type)) + 1);
    if (tagBits > std::numeric_limits<uint64_t>::max() - *span)
      return std::nullopt;
    *span += tagBits;
  }
  if (!span || *span == 0 || *span > std::numeric_limits<unsigned>::max())
    return std::nullopt;
  return static_cast<unsigned>(*span);
}

LogicalResult convertNativeAggregateType(Type type,
                                         SmallVectorImpl<Type> &results) {
  std::optional<unsigned> width = nativeStateWidth(type);
  if (!width)
    return failure();
  Type plane = IntegerType::get(type.getContext(), *width);
  results.push_back(plane);
  if (containsLogic(type))
    results.push_back(plane);
  return success();
}

LogicalResult validateProcessABI(ModuleOp module,
                                 const llvm::DataLayout &layout) {
  // DataLayout caches StructLayout objects by LLVM type identity. Keep those
  // entries local to the LLVMContext that owns the validation-only types.
  llvm::DataLayout validationLayout(layout.getStringRepresentation());
  llvm::LLVMContext context;
  llvm::Type *pointer = llvm::PointerType::get(context, 0);
  llvm::Type *i32 = llvm::Type::getInt32Ty(context);
  llvm::Type *i64 = llvm::Type::getInt64Ty(context);
  auto checkType = [&](llvm::Type *type, uint64_t size, uint64_t alignment) {
    llvm::TypeSize actualSize = validationLayout.getTypeAllocSize(type);
    return !actualSize.isScalable() && actualSize.getFixedValue() == size &&
           validationLayout.getABITypeAlign(type).value() == alignment;
  };
  auto checkStruct = [&](llvm::ArrayRef<llvm::Type *> elements,
                         llvm::ArrayRef<uint64_t> offsets, uint64_t size,
                         uint64_t alignment) {
    auto *type = llvm::StructType::get(context, elements);
    if (!checkType(type, size, alignment))
      return false;
    const llvm::StructLayout *structLayout =
        validationLayout.getStructLayout(type);
    return llvm::all_of(llvm::enumerate(offsets), [&](auto indexedOffset) {
      return structLayout->getElementOffset(indexedOffset.index()) ==
             indexedOffset.value();
    });
  };
  auto *handle = llvm::StructType::get(context, {i32, i32, i64});
  bool compatible =
      checkType(pointer, 8, 8) && checkType(i32, 4, 4) &&
      checkType(i64, 8, 8) &&
      checkStruct({i32, i32, i32, i32, i64, i64}, {0, 4, 8, 12, 16, 24}, 32,
                  8) &&
      checkStruct({i64, i32, i32}, {0, 8, 12}, 16, 8) &&
      checkStruct({i32, i32, i64, i64, i32, i32}, {0, 4, 8, 16, 24, 28}, 32,
                  8) &&
      checkStruct({i32, i32, i64, i64, pointer, i32, i32, pointer, i64},
                  {0, 4, 8, 16, 24, 32, 36, 40, 48}, 56, 8) &&
      checkStruct({i32, i32, i32, i32, pointer, i64, pointer, i64, i64, i64},
                  {0, 4, 8, 12, 16, 24, 32, 40, 48, 56}, 64, 8) &&
      checkStruct({pointer, i32, i32}, {0, 8, 12}, 16, 8) &&
      checkStruct({handle, i32, i32, i32, i32, pointer, pointer, pointer,
                   pointer, pointer, pointer, pointer},
                  {0, 16, 20, 24, 28, 32, 40, 48, 56, 64, 72, 80}, 88, 8) &&
      checkStruct({pointer, pointer, pointer, i64, i64, i64, pointer, i32, i32,
                   i32, i32, pointer, pointer, pointer},
                  {0, 8, 16, 24, 32, 40, 48, 56, 60, 64, 68, 72, 80, 88}, 96,
                  8);
  if (!compatible)
    return module.emitError(
        "LLVM data layout is incompatible with the Obelisk process ABI");
  if (auto tripleAttr =
          module->getAttrOfType<StringAttr>("llvm.target_triple")) {
    llvm::Triple triple(tripleAttr.getValue());
    if (!triple.isArch64Bit() || !triple.isLittleEndian())
      return module.emitError(
          "llvm.target_triple is inconsistent with the Obelisk process ABI");
  }
  return success();
}

FailureOr<StorageProperties> storageProperties(Type type,
                                               const llvm::DataLayout &layout,
                                               llvm::LLVMContext &context) {
  llvm::Type *llvmType = nullptr;
  bool fourState = false;
  if (auto logic = dyn_cast<sim::LogicType>(type)) {
    llvmType = llvm::IntegerType::get(context, logic.getWidth());
    fourState = true;
  } else if (auto integer = dyn_cast<IntegerType>(type)) {
    llvmType = llvm::IntegerType::get(context, integer.getWidth());
  } else if (isa<sim::TimeType>(type)) {
    llvmType = llvm::Type::getInt64Ty(context);
  } else if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
                 sim::ProcessType>(type)) {
    // Simulation handles remain frame-relative stable IDs. They must never
    // become host pointers in the canonical frame shared with bytecode.
    llvmType = llvm::Type::getInt64Ty(context);
  } else if (sim::isAggregateType(type)) {
    std::optional<unsigned> width = nativeStateWidth(type);
    if (!width)
      return failure();
    llvmType = llvm::IntegerType::get(context, *width);
    fourState = containsLogic(type);
  } else {
    return failure();
  }
  llvm::TypeSize typeSize = layout.getTypeAllocSize(llvmType);
  if (typeSize.isScalable() || typeSize.getFixedValue() == 0)
    return failure();
  return StorageProperties{
      typeSize.getFixedValue(),
      static_cast<uint32_t>(layout.getABITypeAlign(llvmType).value()),
      fourState};
}

bool containsLogic(Type type) {
  bool result = false;
  type.walk([&](sim::LogicType) { result = true; });
  return result;
}

bool hasNoLogic(Operation *operation) {
  for (Type type : operation->getOperandTypes())
    if (containsLogic(type))
      return false;
  for (Type type : operation->getResultTypes())
    if (containsLogic(type))
      return false;
  for (Region &region : operation->getRegions())
    for (Block &block : region)
      for (BlockArgument argument : block.getArguments())
        if (containsLogic(argument.getType()))
          return false;
  return true;
}

SmallVector<Value> flatten(ArrayRef<ValueRange> ranges) {
  SmallVector<Value> values;
  for (ValueRange range : ranges)
    llvm::append_range(values, range);
  return values;
}

class SimContextRuntimeLowering final : public ConversionPattern {
public:
  SimContextRuntimeLowering(const TypeConverter &converter,
                            MLIRContext *context)
      : ConversionPattern(converter,
                          sim::SimContextRuntimeOp::getOperationName(), 1,
                          context) {}

  LogicalResult
  matchAndRewrite(Operation *operation, ArrayRef<Value> operands,
                  ConversionPatternRewriter &rewriter) const override {
    if (operands.size() != 1)
      return rewriter.notifyMatchFailure(operation,
                                         "context projection must convert 1:1");
    rewriter.replaceOp(operation, operands.front());
    return success();
  }
};

class SimFuncSignatureConversion final
    : public OpConversionPattern<sim::SimFuncOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimFuncOp function, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    FunctionType type = function.getFunctionType();
    TypeConverter::SignatureConversion entry(type.getNumInputs());
    SmallVector<Type> results;
    if (failed(getTypeConverter()->convertSignatureArgs(type.getInputs(),
                                                        entry)) ||
        failed(getTypeConverter()->convertTypes(type.getResults(), results)) ||
        (!function.getBody().empty() &&
         failed(rewriter.convertRegionTypes(&function.getBody(),
                                            *getTypeConverter(), &entry))))
      return failure();
    SmallVector<Attribute> argAttrs(entry.getConvertedTypes().size(),
                                    rewriter.getDictionaryAttr({}));
    SmallVector<Attribute> resultAttrs(results.size(),
                                       rewriter.getDictionaryAttr({}));
    rewriter.modifyOpInPlace(function, [&] {
      function.setType(FunctionType::get(rewriter.getContext(),
                                         entry.getConvertedTypes(), results));
      function.setArgAttrsAttr(rewriter.getArrayAttr(argAttrs));
      if (!resultAttrs.empty())
        function.setResAttrsAttr(rewriter.getArrayAttr(resultAttrs));
    });
    return success();
  }
};

class SimReturnTypeConversion final
    : public OpConversionPattern<sim::SimReturnOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimReturnOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addAttributes(operation->getAttrs());
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

SmallVector<int32_t> suspensionWaitWidths(Operation *operation);

template <typename Op>
class SimSuspendTypeConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(Op operation,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addSuccessors(operation->getSuccessors());
    state.addAttributes(operation->getAttrs());
    SmallVector<int32_t> waitWidths = suspensionWaitWidths(operation);
    if (!waitWidths.empty())
      state.addAttribute("obelisk.coro.wait_widths",
                         rewriter.getDenseI32ArrayAttr(waitWidths));
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

void emitNativeStateRetain(OpBuilder &builder, Location location,
                           Value handle) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Value contextAddress = LLVM::AddressOfOp::create(
      builder, location, pointer, "__obelisk_current_context");
  Value context =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  Value status =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(builder.getContext(),
                             "obelisk_rt_v1_native_state_retain"),
          ValueRange{context, handle})
          .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(builder.getContext(),
                         "obelisk_rt_v1_scheduler_fail"),
      ValueRange{context, status});
}

class SimCallTypeConversion final : public OpConversionPattern<sim::SimCallOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCallOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    for (auto [operand, converted] :
         llvm::zip_equal(operation.getOperands(), adaptor.getOperands()))
      if (isa<sim::RefType>(operand.getType()) && converted.size() == 1)
        emitNativeStateRetain(rewriter, operation.getLoc(), converted.front());
    SmallVector<Type> results;
    SmallVector<size_t> resultSizes;
    for (Type type : operation.getResultTypes()) {
      size_t start = results.size();
      if (failed(getTypeConverter()->convertType(type, results)))
        return failure();
      resultSizes.push_back(results.size() - start);
    }
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addTypes(results);
    state.addAttributes(operation->getAttrs());
    Operation *replacement = rewriter.create(state);
    SmallVector<ValueRange> replacements;
    size_t offset = 0;
    for (size_t size : resultSizes) {
      replacements.push_back(replacement->getResults().slice(offset, size));
      offset += size;
    }
    rewriter.replaceOpWithMultiple(operation, replacements);
    return success();
  }
};

SmallVector<int32_t> suspensionWaitWidths(Operation *operation) {
  SmallVector<Value> watched;
  TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendChangeOp>(
          [&](auto op) { watched.push_back(op.getWatched()); })
      .Case<sim::SimSuspendEdgeOp>(
          [&](auto op) { watched.push_back(op.getWatched()); })
      .Case<sim::SimSuspendAnyOp>(
          [&](auto op) { llvm::append_range(watched, op.getWatched()); })
      .Case<sim::SimSuspendEventOp>(
          [&](auto op) { watched.push_back(op.getEvent()); })
      .Case<sim::SimSuspendAwaitOp>(
          [&](auto op) { watched.push_back(op.getProcess()); })
      .Case<sim::SimSuspendJoinOp>(
          [&](auto op) { llvm::append_range(watched, op.getProcesses()); });
  SmallVector<int32_t> widths;
  widths.reserve(watched.size());
  for (Value value : watched) {
    Type type = value.getType();
    if (auto reference = dyn_cast<sim::RefType>(type))
      type = reference.getElementType();
    else if (auto net = dyn_cast<sim::NetType>(type))
      type = net.getElementType();
    else
      type = {};
    std::optional<unsigned> width = type ? nativeStateWidth(type) : std::nullopt;
    widths.push_back(width ? static_cast<int32_t>(*width) : 0);
  }
  return widths;
}

Value resizeNativeInteger(OpBuilder &builder, Location location, Value value,
                          IntegerType result, bool isSigned = false) {
  auto input = cast<IntegerType>(value.getType());
  if (input == result)
    return value;
  if (input.getWidth() < result.getWidth()) {
    if (isSigned)
      return arith::ExtSIOp::create(builder, location, result, value);
    return arith::ExtUIOp::create(builder, location, result, value);
  }
  return arith::TruncIOp::create(builder, location, result, value);
}

struct SignedI64Index {
  Value value;
  Value representable;
};

SignedI64Index resizeSignedIndexToI64(OpBuilder &builder, Location location,
                                      Value source) {
  IntegerType i64 = builder.getI64Type();
  auto sourceType = cast<IntegerType>(source.getType());
  Value value = resizeNativeInteger(builder, location, source, i64, true);
  Value representable = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(true));
  if (sourceType.getWidth() > i64.getWidth()) {
    Value roundTripped =
        resizeNativeInteger(builder, location, value, sourceType, true);
    representable = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, source, roundTripped);
  }
  return {value, representable};
}

class PackedAggregateExtractConversion final
    : public OpConversionPattern<sim::SimAggregateExtractOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAggregateExtractOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto subelement = sim::getAggregateProvenanceSubelement(
        op.getInput().getType(), static_cast<unsigned>(op.getIndex()));
    std::optional<unsigned> resultWidth =
        nativeStateWidth(op.getResult().getType());
    if (!subelement || !resultWidth || adaptor.getInput().empty())
      return failure();
    SmallVector<Value> results;
    for (Value plane : adaptor.getInput()) {
      auto inputType = dyn_cast<IntegerType>(plane.getType());
      if (!inputType)
        return failure();
      Value selected = plane;
      if (subelement->first != 0) {
        Value amount = arith::ConstantOp::create(
            rewriter, op.getLoc(), inputType,
            rewriter.getIntegerAttr(
                inputType, APInt(inputType.getWidth(), subelement->first)));
        selected = arith::ShRUIOp::create(rewriter, op.getLoc(), plane, amount);
      }
      IntegerType outputType = rewriter.getIntegerType(*resultWidth);
      results.push_back(inputType == outputType
                            ? selected
                            : arith::TruncIOp::create(rewriter, op.getLoc(),
                                                      outputType, selected)
                                  .getResult());
    }
    if (containsLogic(op.getResult().getType())) {
      if (results.size() != 2)
        return failure();
    } else if (results.size() > 1) {
      results.resize(1);
    }
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class PackedAggregateInsertConversion final
    : public OpConversionPattern<sim::SimAggregateInsertOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAggregateInsertOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    std::optional<unsigned> resultWidth =
        nativeStateWidth(op.getResult().getType());
    auto subelement = sim::getAggregateProvenanceSubelement(
        op.getInput().getType(), static_cast<unsigned>(op.getIndex()));
    if (!resultWidth || !subelement || adaptor.getInput().empty() ||
        adaptor.getReplacement().empty() ||
        subelement->first > *resultWidth ||
        subelement->second > *resultWidth - subelement->first)
      return failure();
    IntegerType planeType = rewriter.getIntegerType(*resultWidth);
    APInt fieldMask = APInt::getBitsSet(*resultWidth, subelement->first,
                                       subelement->first + subelement->second);
    Value keepMask = arith::ConstantOp::create(
        rewriter, op.getLoc(), planeType,
        rewriter.getIntegerAttr(planeType, ~fieldMask));
    Value shift = arith::ConstantOp::create(
        rewriter, op.getLoc(), planeType,
        rewriter.getIntegerAttr(
            planeType, APInt(*resultWidth, subelement->first)));
    Value zero = arith::ConstantOp::create(
        rewriter, op.getLoc(), planeType,
        rewriter.getIntegerAttr(planeType, APInt::getZero(*resultWidth)));
    SmallVector<Value> results;
    for (auto [index, input] : llvm::enumerate(adaptor.getInput())) {
      auto inputType = dyn_cast<IntegerType>(input.getType());
      if (!inputType || inputType != planeType)
        return failure();
      Value replacement = zero;
      if (index < adaptor.getReplacement().size()) {
        Value source = adaptor.getReplacement()[index];
        auto sourceType = dyn_cast<IntegerType>(source.getType());
        if (!sourceType || sourceType.getWidth() > *resultWidth)
          return failure();
        replacement =
            sourceType == planeType
                ? source
                : arith::ExtUIOp::create(rewriter, op.getLoc(), planeType,
                                          source)
                      .getResult();
        if (subelement->first != 0)
          replacement = arith::ShLIOp::create(rewriter, op.getLoc(),
                                               replacement, shift);
      }
      Value preserved =
          arith::AndIOp::create(rewriter, op.getLoc(), input, keepMask);
      results.push_back(arith::OrIOp::create(
          rewriter, op.getLoc(), preserved, replacement));
    }
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class PackedAggregateConstructConversion final
    : public OpConversionPattern<sim::SimAggregateConstructOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAggregateConstructOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    std::optional<unsigned> resultWidth =
        nativeStateWidth(op.getResult().getType());
    if (!resultWidth || adaptor.getElements().size() != op.getElements().size())
      return failure();
    IntegerType outputType = rewriter.getIntegerType(*resultWidth);
    auto zero = [&] {
      return arith::ConstantOp::create(
          rewriter, op.getLoc(), outputType,
          rewriter.getIntegerAttr(outputType, APInt::getZero(*resultWidth)));
    };
    auto place = [&](Value destination, Value source,
                     uint64_t offset) -> FailureOr<Value> {
      auto sourceType = dyn_cast<IntegerType>(source.getType());
      if (!sourceType || sourceType.getWidth() > outputType.getWidth())
        return failure();
      Value extended = sourceType == outputType
                           ? source
                           : arith::ExtUIOp::create(rewriter, op.getLoc(),
                                                    outputType, source);
      if (offset != 0) {
        Value amount = arith::ConstantOp::create(
            rewriter, op.getLoc(), outputType,
            rewriter.getIntegerAttr(outputType,
                                    APInt(outputType.getWidth(), offset)));
        extended =
            arith::ShLIOp::create(rewriter, op.getLoc(), extended, amount);
      }
      return arith::OrIOp::create(rewriter, op.getLoc(), destination, extended)
          .getResult();
    };

    Value value = zero();
    Value unknown = zero();
    for (auto [index, converted] : llvm::enumerate(adaptor.getElements())) {
      auto subelement = sim::getAggregateProvenanceSubelement(
          op.getResult().getType(), index);
      if (!subelement || converted.empty())
        return failure();
      FailureOr<Value> placed =
          place(value, converted.front(), subelement->first);
      if (failed(placed))
        return failure();
      value = *placed;
      if (converted.size() == 2) {
        placed = place(unknown, converted[1], subelement->first);
        if (failed(placed))
          return failure();
        unknown = *placed;
      }
    }
    SmallVector<Value> results{value};
    if (containsLogic(op.getResult().getType()))
      results.push_back(unknown);
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class AggregateDynamicExtractConversion final
    : public OpConversionPattern<sim::SimArrayDynExtractOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimArrayDynExtractOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().empty() || adaptor.getIndex().empty())
      return failure();
    Type array = op.getInput().getType();
    int64_t left;
    int64_t right;
    bool packed;
    Type element;
    if (auto type = dyn_cast<sim::PackedArrayType>(array)) {
      left = type.getLeft();
      right = type.getRight();
      packed = true;
      element = type.getElementType();
    } else if (auto type = dyn_cast<sim::UnpackedArrayType>(array)) {
      left = type.getLeft();
      right = type.getRight();
      packed = false;
      element = type.getElementType();
    } else {
      return failure();
    }
    std::optional<unsigned> resultWidth = nativeStateWidth(element);
    std::optional<uint64_t> span = sim::getProvenanceSpan(element);
    uint64_t count = sim::getAggregateNumElements(array);
    if (!resultWidth || !span || count == 0)
      return failure();

    Location location = op.getLoc();
    IntegerType i64 = rewriter.getI64Type();
    SignedI64Index convertedIndex = resizeSignedIndexToI64(
        rewriter, location, adaptor.getIndex().front());
    Value index = convertedIndex.value;
    Value leftValue = arith::ConstantOp::create(
        rewriter, location, i64, rewriter.getI64IntegerAttr(left));
    Value rightValue = arith::ConstantOp::create(
        rewriter, location, i64, rewriter.getI64IntegerAttr(right));
    Value valid;
    Value ordinal;
    if (left >= right) {
      valid = arith::AndIOp::create(
          rewriter, location,
          arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::sle,
                                index, leftValue),
          arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::sge,
                                index, rightValue));
      ordinal = arith::SubIOp::create(rewriter, location, leftValue, index);
    } else {
      valid = arith::AndIOp::create(
          rewriter, location,
          arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::sge,
                                index, leftValue),
          arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::sle,
                                index, rightValue));
      ordinal = arith::SubIOp::create(rewriter, location, index, leftValue);
    }
    // The declared bounds fit in i64, but truncating a wider source index can
    // wrap an out-of-range value into that range.
    valid = arith::AndIOp::create(rewriter, location, valid,
                                  convertedIndex.representable);
    if (adaptor.getIndex().size() == 2) {
      Value known = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::eq, adaptor.getIndex()[1],
          arith::ConstantOp::create(
              rewriter, location, adaptor.getIndex()[1].getType(),
              rewriter.getZeroAttr(adaptor.getIndex()[1].getType())));
      valid = arith::AndIOp::create(rewriter, location, valid, known);
    }
    if (packed) {
      Value last = arith::ConstantOp::create(
          rewriter, location, i64,
          rewriter.getI64IntegerAttr(static_cast<int64_t>(count - 1)));
      ordinal = arith::SubIOp::create(rewriter, location, last, ordinal);
    }
    Value safeOrdinal = arith::SelectOp::create(
        rewriter, location, valid, ordinal,
        arith::ConstantOp::create(rewriter, location, i64,
                                  rewriter.getI64IntegerAttr(0)));
    Value offset = arith::MulIOp::create(
        rewriter, location, safeOrdinal,
        arith::ConstantOp::create(rewriter, location, i64,
                                  rewriter.getI64IntegerAttr(*span)));
    IntegerType outputType = rewriter.getIntegerType(*resultWidth);
    SmallVector<Value> results;
    for (auto [planeIndex, plane] : llvm::enumerate(adaptor.getInput())) {
      auto planeType = cast<IntegerType>(plane.getType());
      Value amount = resizeNativeInteger(rewriter, location, offset, planeType);
      Value shifted = arith::ShRUIOp::create(rewriter, location, plane, amount);
      Value extracted =
          planeType == outputType
              ? shifted
              : arith::TruncIOp::create(rewriter, location, outputType, shifted)
                    .getResult();
      APInt fallback = planeIndex == 1 && containsLogic(element)
                           ? APInt::getAllOnes(*resultWidth)
                           : APInt::getZero(*resultWidth);
      Value defaultValue = arith::ConstantOp::create(
          rewriter, location, outputType,
          rewriter.getIntegerAttr(outputType, fallback));
      results.push_back(arith::SelectOp::create(rewriter, location, valid,
                                                extracted, defaultValue));
    }
    if (containsLogic(element)) {
      if (results.size() != 2)
        return failure();
    } else if (results.size() > 1) {
      results.resize(1);
    }
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class AggregateDefaultConversion final
    : public OpConversionPattern<sim::SimAggregateDefaultOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAggregateDefaultOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    std::optional<unsigned> width = nativeStateWidth(op.getResult().getType());
    if (!width)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    Value zero = arith::ConstantOp::create(
        rewriter, op.getLoc(), plane,
        rewriter.getIntegerAttr(plane, APInt::getZero(*width)));
    SmallVector<Value> results{zero};
    if (containsLogic(op.getResult().getType()))
      results.push_back(arith::ConstantOp::create(
          rewriter, op.getLoc(), plane,
          rewriter.getIntegerAttr(plane, APInt::getAllOnes(*width))));
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class UnionConstructConversion final
    : public OpConversionPattern<sim::SimUnionConstructOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimUnionConstructOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getValue().empty())
      return failure();
    Type unionType = op.getResult().getType();
    std::optional<unsigned> width = nativeStateWidth(unionType);
    std::optional<uint64_t> payloadSpan = sim::getProvenanceSpan(unionType);
    if (!width || !payloadSpan || *payloadSpan > *width)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    auto extend = [&](Value value) -> FailureOr<Value> {
      auto type = dyn_cast<IntegerType>(value.getType());
      if (!type || type.getWidth() > plane.getWidth())
        return failure();
      if (type == plane)
        return value;
      return arith::ExtUIOp::create(rewriter, op.getLoc(), plane, value)
          .getResult();
    };
    FailureOr<Value> value = extend(adaptor.getValue().front());
    if (failed(value))
      return failure();
    Value unknown = arith::ConstantOp::create(
        rewriter, op.getLoc(), plane,
        rewriter.getIntegerAttr(plane, APInt::getZero(*width)));
    if (adaptor.getValue().size() == 2) {
      FailureOr<Value> convertedUnknown = extend(adaptor.getValue()[1]);
      if (failed(convertedUnknown))
        return failure();
      unknown = *convertedUnknown;
    }

    uint64_t tag = 0;
    unsigned tagBits = 0;
    if (auto packed = dyn_cast<sim::PackedUnionType>(unionType);
        packed && packed.getIsTagged()) {
      tag = op.getIndex();
      tagBits = packed.getTagBits();
    } else if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(unionType);
               unpacked && unpacked.getIsTagged()) {
      tag = static_cast<uint64_t>(op.getIndex()) + 1;
      tagBits = llvm::Log2_64_Ceil(
          static_cast<uint64_t>(sim::getAggregateNumElements(unionType)) + 1);
    }
    if (tagBits != 0) {
      APInt tagValue(*width, tag);
      tagValue <<= *payloadSpan;
      Value encoded =
          arith::ConstantOp::create(rewriter, op.getLoc(), plane,
                                    rewriter.getIntegerAttr(plane, tagValue));
      *value = arith::OrIOp::create(rewriter, op.getLoc(), *value, encoded);
    }
    SmallVector<Value> results{*value};
    if (containsLogic(unionType))
      results.push_back(unknown);
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class UnionExtractConversion final
    : public OpConversionPattern<sim::SimUnionExtractOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimUnionExtractOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    std::optional<unsigned> resultWidth =
        nativeStateWidth(op.getResult().getType());
    if (!resultWidth || adaptor.getInput().empty())
      return failure();
    IntegerType resultType = rewriter.getIntegerType(*resultWidth);
    SmallVector<Value> results;
    for (Value plane : adaptor.getInput()) {
      auto inputType = dyn_cast<IntegerType>(plane.getType());
      if (!inputType || inputType.getWidth() < *resultWidth)
        return failure();
      results.push_back(
          inputType == resultType
              ? plane
              : arith::TruncIOp::create(rewriter, op.getLoc(), resultType,
                                         plane)
                    .getResult());
    }
    if (containsLogic(op.getResult().getType())) {
      if (results.size() != 2)
        return failure();
    } else if (results.size() > 1) {
      results.resize(1);
    }
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

Type convertProcessType(Type type, MLIRContext *context) {
  if (isa<sim::ContextType, runtime::ContextType,
          runtime::ProcessDescriptorType, runtime::ProcessInstanceType>(type))
    return LLVM::LLVMPointerType::get(context);
  if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
          sim::ProcessType>(type))
    return IntegerType::get(context, 64);
  if (isa<sim::TimeType>(type))
    return IntegerType::get(context, 64);
  return type;
}

Value llvmConstant(OpBuilder &builder, Location location, Type type,
                   uint64_t value) {
  return LLVM::ConstantOp::create(builder, location, type,
                                  builder.getIntegerAttr(type, value));
}

Value byteGEP(OpBuilder &builder, Location location, Value base,
              uint64_t offset) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i8 = builder.getI8Type();
  constexpr uint64_t maxConstantIndex =
      (uint64_t{1} << (LLVM::kGEPConstantBitWidth - 1)) - 1;
  SmallVector<LLVM::GEPArg> indices;
  if (offset <= maxConstantIndex)
    indices.emplace_back(static_cast<int32_t>(offset));
  else
    indices.emplace_back(
        llvmConstant(builder, location, builder.getI64Type(), offset));
  return LLVM::GEPOp::create(builder, location, pointer, i8, base, indices);
}

Value loadAt(OpBuilder &builder, Location location, Value base, uint64_t offset,
             Type type, unsigned alignment) {
  return LLVM::LoadOp::create(builder, location, type,
                              byteGEP(builder, location, base, offset),
                              alignment);
}

void storeAt(OpBuilder &builder, Location location, Value base, uint64_t offset,
             Value value, unsigned alignment) {
  LLVM::StoreOp::create(builder, location, value,
                        byteGEP(builder, location, base, offset), alignment);
}

Value asI64(OpBuilder &builder, Location location, Value value) {
  Type i64 = builder.getI64Type();
  if (isa<LLVM::LLVMPointerType>(value.getType()))
    return LLVM::PtrToIntOp::create(builder, location, i64, value);
  auto integer = cast<IntegerType>(value.getType());
  if (integer.getWidth() == 64)
    return value;
  if (integer.getWidth() < 64)
    return arith::ExtUIOp::create(builder, location, i64, value);
  return arith::TruncIOp::create(builder, location, i64, value);
}

void publishAction(OpBuilder &builder, Location location, Value instance,
                   uint32_t actionKind, uint32_t suspendKind,
                   uint32_t continuation, uint32_t flags, Value payload,
                   uint64_t auxiliary) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Value action =
      loadAt(builder, location, instance, kInstanceActionOffset, pointer, 8);
  storeAt(builder, location, action, 0,
          llvmConstant(builder, location, i32, actionKind), 4);
  storeAt(builder, location, action, 4,
          llvmConstant(builder, location, i32, suspendKind), 4);
  storeAt(builder, location, action, 8,
          llvmConstant(builder, location, i32, continuation), 4);
  storeAt(builder, location, action, 12,
          llvmConstant(builder, location, i32, flags), 4);
  storeAt(builder, location, action, 16, payload, 8);
  storeAt(builder, location, action, 24,
          llvmConstant(builder, location, i64, auxiliary), 8);
}

void addFrameAttributes(LLVM::LLVMFuncOp ramp,
                        const SimulationProcessFrameAnalysis &analysis,
                        OpBuilder &builder) {
  ramp->setAttr("obelisk.frame.size",
                builder.getI64IntegerAttr(analysis.getFrameSize()));
  ramp->setAttr("obelisk.frame.alignment",
                builder.getI64IntegerAttr(analysis.getFrameAlignment()));
  ramp->setAttr("obelisk.frame.checksum",
                builder.getI64IntegerAttr(analysis.getChecksum()));
  SmallVector<int32_t> continuationIDs;
  for (uint32_t continuation : analysis.getContinuations())
    continuationIDs.push_back(static_cast<int32_t>(continuation));
  ramp->setAttr("obelisk.frame.continuations",
                builder.getDenseI32ArrayAttr(continuationIDs));
  SmallVector<Attribute> fields;
  for (const ProcessFrameField &field : analysis.getFields()) {
    NamedAttrList attributes;
    attributes.set(
        "kind", builder.getI32IntegerAttr(static_cast<uint32_t>(field.kind)));
    attributes.set(
        "flags", builder.getI32IntegerAttr(static_cast<uint32_t>(field.flags)));
    attributes.set("offset", builder.getI64IntegerAttr(field.offset));
    attributes.set("size", builder.getI64IntegerAttr(field.size));
    attributes.set("alignment", builder.getI32IntegerAttr(field.alignment));
    fields.push_back(attributes.getDictionary(builder.getContext()));
  }
  ramp->setAttr("obelisk.frame.fields", builder.getArrayAttr(fields));
}

LogicalResult lowerTimeOperations(sim::SimFuncOp function) {
  IRRewriter rewriter(function.getContext());
  SmallVector<Operation *> operations;
  function.walk([&](Operation *operation) {
    if (isa<sim::SimTimeConstantOp, sim::SimTimeAddOp, sim::SimTimeScaleOp>(
            operation))
      operations.push_back(operation);
  });
  for (Operation *operation : operations) {
    rewriter.setInsertionPoint(operation);
    Location location = operation->getLoc();
    if (auto constant = dyn_cast<sim::SimTimeConstantOp>(operation)) {
      Value value = arith::ConstantOp::create(
          rewriter, location, rewriter.getI64Type(),
          rewriter.getI64IntegerAttr(constant.getValue()));
      rewriter.replaceOp(operation, value);
      continue;
    }
    if (auto add = dyn_cast<sim::SimTimeAddOp>(operation)) {
      Value value =
          arith::AddIOp::create(rewriter, location, add.getLhs(), add.getRhs());
      rewriter.replaceOp(operation, value);
      continue;
    }
    auto scale = cast<sim::SimTimeScaleOp>(operation);
    Value input = scale.getInput();
    auto inputType = cast<IntegerType>(input.getType());
    Value extended = input;
    if (inputType.getWidth() < 64) {
      if (scale.getIsSigned())
        extended = arith::ExtSIOp::create(rewriter, location,
                                          rewriter.getI64Type(), input);
      else
        extended = arith::ExtUIOp::create(rewriter, location,
                                          rewriter.getI64Type(), input);
    } else if (inputType.getWidth() > 64)
      extended = arith::TruncIOp::create(rewriter, location,
                                         rewriter.getI64Type(), input);
    Value multiplier =
        arith::ConstantOp::create(rewriter, location, rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(scale.getScale()));
    rewriter.replaceOp(operation, arith::MulIOp::create(rewriter, location,
                                                        extended, multiplier));
  }
  return success();
}

struct RampBlocks {
  Block *suspendReturn;
  Block *terminate;
  Block *cleanup;
  DenseMap<Block *, Block *> shims;
};

Block *makeCoroutineReturnBlock(Region &region, Location location,
                                Value handle) {
  Block *block = new Block;
  region.push_back(block);
  OpBuilder builder(block, block->begin());
  Value unwind = llvmConstant(builder, location, builder.getI1Type(), 0);
  Value none = LLVM::NoneTokenOp::create(builder, location);
  LLVM::CoroEndOp::create(builder, location, builder.getI1Type(), handle,
                          unwind, none);
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  return block;
}

LogicalResult storeFrameValue(OpBuilder &builder, Location location,
                              Value frame, Value value, uint64_t offset,
                              uint32_t alignment) {
  if (!isa<IntegerType, LLVM::LLVMPointerType>(value.getType()))
    return failure();
  storeAt(builder, location, frame, offset, value, alignment);
  return success();
}

LogicalResult
lowerSuspendTerminator(Operation *operation, Value instance, Value handle,
                       const SimulationProcessFrameAnalysis &analysis,
                       const RampBlocks &blocks) {
  IRRewriter builder(operation->getContext());
  builder.setInsertionPoint(operation);
  Location location = operation->getLoc();
  auto branch = cast<BranchOpInterface>(operation);
  auto continuationAttr =
      operation->getAttrOfType<IntegerAttr>("obelisk.coro.continuation");
  auto waitOffsetAttr =
      operation->getAttrOfType<IntegerAttr>("obelisk.coro.wait_offset");
  auto waitSizeAttr =
      operation->getAttrOfType<IntegerAttr>("obelisk.coro.wait_size");
  if (!continuationAttr || !waitOffsetAttr || !waitSizeAttr)
    return operation->emitError("missing coroutine frame analysis metadata");
  uint32_t continuationID = continuationAttr.getInt();
  Block *continuation = operation->getSuccessor(0);
  ArrayRef<ProcessFrameValue> layout =
      analysis.getContinuationLayout(continuationID);
  SmallVector<Value> operands(
      branch.getSuccessorOperands(0).getForwardedOperands().begin(),
      branch.getSuccessorOperands(0).getForwardedOperands().end());
  Value frame = loadAt(builder, location, instance, kInstanceFrameOffset,
                       LLVM::LLVMPointerType::get(builder.getContext()), 8);
  size_t physical = 0;
  for (const ProcessFrameValue &slot : layout) {
    if (physical >= operands.size() ||
        failed(storeFrameValue(builder, location, frame, operands[physical++],
                               slot.valueOffset, slot.alignment)))
      return operation->emitError("cannot store continuation value in frame");
    if (slot.isFourState()) {
      if (physical >= operands.size() ||
          failed(storeFrameValue(builder, location, frame, operands[physical++],
                                 slot.unknownOffset, slot.alignment)))
        return operation->emitError(
            "cannot store four-state unknown plane in frame");
    }
  }
  if (physical != operands.size())
    return operation->emitError(
        "converted continuation arity disagrees with frame analysis");

  uint64_t waitOffset = waitOffsetAttr.getInt();
  uint64_t waitSize = waitSizeAttr.getInt();
  uint32_t kind = suspensionKind(operation);
  uint32_t count = waitEntryCount(operation);
  Value wait = byteGEP(builder, location, frame, waitOffset);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  storeAt(builder, location, wait, 0, llvmConstant(builder, location, i32, 2),
          4);
  storeAt(builder, location, wait, 4,
          llvmConstant(builder, location, i32, kind), 4);
  uint32_t waitFlags = 0;
  if (auto join = dyn_cast<sim::SimSuspendJoinOp>(operation))
    waitFlags = static_cast<uint32_t>(join.getKind());
  storeAt(builder, location, wait, 8,
          llvmConstant(builder, location, i32, waitFlags), 4);
  storeAt(builder, location, wait, 12,
          llvmConstant(builder, location, i32, count), 4);
  Value payload = llvmConstant(builder, location, i64, 0);
  if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation))
    payload = asI64(builder, location, delay.getDelay());
  storeAt(builder, location, wait, 16, payload, 8);
  storeAt(builder, location, wait, 24, llvmConstant(builder, location, i64, 0),
          8);

  SmallVector<Value> watched;
  SmallVector<uint32_t> watchedEdges;
  TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendChangeOp>([&](auto op) {
        watched.push_back(op.getWatched());
        watchedEdges.push_back(static_cast<uint32_t>(sim::EdgeKind::Change));
      })
      .Case<sim::SimSuspendEdgeOp>([&](auto op) {
        watched.push_back(op.getWatched());
        watchedEdges.push_back(static_cast<uint32_t>(op.getEdge()));
      })
      .Case<sim::SimSuspendAnyOp>([&](auto op) {
        llvm::append_range(watched, op.getWatched());
        for (int32_t edge : op.getEdges())
          watchedEdges.push_back(static_cast<uint32_t>(edge));
      })
      .Case<sim::SimSuspendEventOp>([&](auto op) {
        watched.push_back(op.getEvent());
        watchedEdges.push_back(kWaitEdgeNone);
      })
      .Case<sim::SimSuspendAwaitOp>([&](auto op) {
        watched.push_back(op.getProcess());
        watchedEdges.push_back(kWaitEdgeNone);
      })
      .Case<sim::SimSuspendJoinOp>([&](auto op) {
        llvm::append_range(watched, op.getProcesses());
        watchedEdges.append(op.getProcesses().size(), kWaitEdgeNone);
      });
  if (watched.size() != watchedEdges.size())
    return operation->emitError("wait handle and edge inventories disagree");
  auto waitWidths =
      operation->getAttrOfType<DenseI32ArrayAttr>("obelisk.coro.wait_widths");
  if (!watched.empty() &&
      (!waitWidths || static_cast<size_t>(waitWidths.size()) != watched.size()))
    return operation->emitError("wait handle and width inventories disagree");
  for (auto [index, value] : llvm::enumerate(watched)) {
    uint64_t entryOffset = kWaitHeaderSize + index * kWaitEntrySize;
    storeAt(builder, location, wait, entryOffset,
            asI64(builder, location, value), 8);
    storeAt(builder, location, wait, entryOffset + 8,
            llvmConstant(builder, location, i32, watchedEdges[index]), 4);
    storeAt(builder, location, wait, entryOffset + 12,
            llvmConstant(builder, location, i32,
                         static_cast<uint32_t>(waitWidths[index])),
            4);
  }

  storeAt(builder, location, instance, kInstanceContinuationOffset,
          llvmConstant(builder, location, i32, continuationID), 4);
  publishAction(builder, location, instance, 1, kind, continuationID, 1,
                llvmConstant(builder, location, i64, waitOffset), waitSize);

  Value final = llvmConstant(builder, location, builder.getI1Type(), 0);
  Value save = LLVM::CoroSaveOp::create(
      builder, location, LLVM::LLVMTokenType::get(builder.getContext()),
      handle);
  Value state = LLVM::CoroSuspendOp::create(builder, location,
                                            builder.getI8Type(), save, final);
  SmallVector<Block *> destinations{blocks.shims.lookup(continuation),
                                    blocks.cleanup};
  SmallVector<ValueRange> destinationOperands(2);
  SmallVector<APInt> caseValues{APInt(8, 0), APInt(8, 1)};
  LLVM::SwitchOp::create(builder, location, state, blocks.suspendReturn,
                         ValueRange{}, caseValues, destinations,
                         destinationOperands, ArrayRef<int32_t>{});
  builder.eraseOp(operation);
  return success();
}

LogicalResult lowerFinalReturn(sim::SimReturnOp operation,
                               const RampBlocks &blocks) {
  if (!operation.getOperands().empty())
    return operation.emitError("suspendable process cannot return values");
  IRRewriter builder(operation->getContext());
  builder.setInsertionPoint(operation);
  cf::BranchOp::create(builder, operation.getLoc(), blocks.terminate);
  builder.eraseOp(operation);
  return success();
}

LogicalResult makeNativeWrappers(ModuleOp module, LLVM::LLVMFuncOp ramp,
                                 StringRef baseName) {
  OpBuilder builder(ramp);
  builder.setInsertionPointAfter(ramp);
  Location location = ramp.getLoc();
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type voidType = LLVM::LLVMVoidType::get(context);
  auto makeName = [&](StringRef suffix) { return (baseName + suffix).str(); };

  auto requirements = LLVM::LLVMFuncOp::create(
      builder, location, makeName(".__obelisk_native_requirements"),
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
  Block *requirementsEntry = requirements.addEntryBlock(builder);
  builder.setInsertionPointToStart(requirementsEntry);
  Value null = LLVM::ZeroOp::create(builder, location, pointer);
  Value mode = llvmConstant(builder, location, i32, 0);
  auto requirementsCall = LLVM::CallOp::create(
      builder, location, TypeRange{}, SymbolRefAttr::get(ramp),
      ValueRange{null, mode, requirementsEntry->getArgument(0),
                 requirementsEntry->getArgument(1)});
  (void)requirementsCall;
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, 0));

  builder.setInsertionPointAfter(requirements);
  auto execute = LLVM::LLVMFuncOp::create(
      builder, location, makeName(".__obelisk_native_execute"),
      LLVM::LLVMFunctionType::get(i32, {pointer}, false));
  Block *executeEntry = execute.addEntryBlock(builder);
  Block *start = new Block;
  Block *resume = new Block;
  Block *done = new Block;
  execute.getBody().push_back(start);
  execute.getBody().push_back(resume);
  execute.getBody().push_back(done);
  builder.setInsertionPointToStart(executeEntry);
  Value instance = executeEntry->getArgument(0);
  Value runtimeContext = loadAt(builder, location, instance,
                                kInstanceContextOffset, pointer, 8);
  Value currentContext = LLVM::AddressOfOp::create(
      builder, location, pointer, "__obelisk_current_context");
  LLVM::StoreOp::create(builder, location, runtimeContext, currentContext, 8);
  Value handle = loadAt(builder, location, instance,
                        kInstanceNativeHandleOffset, pointer, 8);
  Value bits =
      LLVM::PtrToIntOp::create(builder, location, builder.getI64Type(), handle);
  Value isNull = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, bits,
      llvmConstant(builder, location, builder.getI64Type(), 0));
  cf::CondBranchOp::create(builder, location, isNull, start, ValueRange{},
                           resume, ValueRange{});
  builder.setInsertionPointToStart(start);
  Value modeExecute = llvmConstant(builder, location, i32, 1);
  Value nullOut = LLVM::ZeroOp::create(builder, location, pointer);
  LLVM::CallOp::create(builder, location, TypeRange{}, SymbolRefAttr::get(ramp),
                       ValueRange{instance, modeExecute, nullOut, nullOut});
  cf::BranchOp::create(builder, location, done);
  builder.setInsertionPointToStart(resume);
  LLVM::CoroResumeOp::create(builder, location, handle);
  cf::BranchOp::create(builder, location, done);
  builder.setInsertionPointToStart(done);
  Value status =
      loadAt(builder, location, instance, kInstanceStatusOffset, i32, 4);
  LLVM::ReturnOp::create(builder, location, status);

  builder.setInsertionPointAfter(execute);
  auto destroy = LLVM::LLVMFuncOp::create(
      builder, location, makeName(".__obelisk_native_destroy"),
      LLVM::LLVMFunctionType::get(voidType, {pointer}, false));
  Block *destroyEntry = destroy.addEntryBlock(builder);
  Block *destroyCall = new Block;
  Block *destroyDone = new Block;
  destroy.getBody().push_back(destroyCall);
  destroy.getBody().push_back(destroyDone);
  builder.setInsertionPointToStart(destroyEntry);
  Value destroyInstance = destroyEntry->getArgument(0);
  Value destroyHandle = loadAt(builder, location, destroyInstance,
                               kInstanceNativeHandleOffset, pointer, 8);
  Value destroyBits = LLVM::PtrToIntOp::create(
      builder, location, builder.getI64Type(), destroyHandle);
  Value destroyIsNull = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, destroyBits,
      llvmConstant(builder, location, builder.getI64Type(), 0));
  cf::CondBranchOp::create(builder, location, destroyIsNull, destroyDone,
                           ValueRange{}, destroyCall, ValueRange{});
  builder.setInsertionPointToStart(destroyCall);
  LLVM::CallIntrinsicOp::create(builder, location,
                                builder.getStringAttr("llvm.coro.destroy"),
                                destroyHandle);
  storeAt(builder, location, destroyInstance, kInstanceNativeHandleOffset,
          LLVM::ZeroOp::create(builder, location, pointer), 8);
  cf::BranchOp::create(builder, location, destroyDone);
  builder.setInsertionPointToStart(destroyDone);
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  return success();
}

template <typename Initializer>
LLVM::GlobalOp makeConstantGlobal(ModuleOp module, Location location, Type type,
                                  StringRef name, LLVM::Linkage linkage,
                                  uint64_t alignment,
                                  Initializer &&initializer) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  auto global = LLVM::GlobalOp::create(builder, location, type, true, linkage,
                                       name, Attribute{}, alignment);
  Block *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToStart(block);
  LLVM::ReturnOp::create(builder, location, initializer(builder));
  return global;
}

Value insertValue(OpBuilder &builder, Location location, Value aggregate,
                  Value element, int64_t index) {
  return LLVM::InsertValueOp::create(builder, location, aggregate, element,
                                     ArrayRef<int64_t>{index});
}

uint64_t stableProcessID(StringRef name) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (unsigned char byte : name.bytes()) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

LogicalResult
makeProcessDescriptor(ModuleOp module, Location location, StringRef baseName,
                      uint64_t stableID,
                      const SimulationProcessFrameAnalysis &analysis) {
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  auto fieldType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64, i64, i32, i32});
  auto fieldsType =
      LLVM::LLVMArrayType::get(fieldType, analysis.getFields().size());
  auto continuationsType =
      LLVM::LLVMArrayType::get(i32, analysis.getContinuations().size());
  auto layoutType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, pointer, i32, i32, pointer, i64});
  auto handleType = LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64});
  auto descriptorType = LLVM::LLVMStructType::getLiteral(
      context, {handleType, i32, i32, i32, i32, pointer, pointer, pointer,
                pointer, pointer, pointer, pointer});

  std::string fieldsName = (baseName + ".__obelisk_frame_fields").str();
  std::string continuationsName = (baseName + ".__obelisk_continuations").str();
  std::string layoutName = (baseName + ".__obelisk_frame_layout").str();
  std::string descriptorName =
      (baseName + ".__obelisk_process_descriptor").str();
  std::string designBytecodeName =
      (baseName + ".__obelisk_bytecode_entry").str();
  constexpr StringLiteral executionName = "__obelisk_execution_descriptor_v1";
  bool hasExecution = module.lookupSymbol(executionName) != nullptr;
  bool hasDesignBytecode = module.lookupSymbol(designBytecodeName) != nullptr;

  makeConstantGlobal(
      module, location, fieldsType, fieldsName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &builder) {
        Value array = LLVM::ZeroOp::create(builder, location, fieldsType);
        for (auto [index, field] : llvm::enumerate(analysis.getFields())) {
          Value value = LLVM::ZeroOp::create(builder, location, fieldType);
          value = insertValue(builder, location, value,
                              llvmConstant(builder, location, i32,
                                           static_cast<uint32_t>(field.kind)),
                              0);
          value = insertValue(builder, location, value,
                              llvmConstant(builder, location, i32,
                                           static_cast<uint32_t>(field.flags)),
                              1);
          value = insertValue(
              builder, location, value,
              llvmConstant(builder, location, i64, field.offset), 2);
          value =
              insertValue(builder, location, value,
                          llvmConstant(builder, location, i64, field.size), 3);
          value = insertValue(
              builder, location, value,
              llvmConstant(builder, location, i32, field.alignment), 4);
          array = LLVM::InsertValueOp::create(
              builder, location, array, value,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return array;
      });
  makeConstantGlobal(module, location, continuationsType, continuationsName,
                     LLVM::Linkage::Internal, 4, [&](OpBuilder &builder) {
                       Value array = LLVM::ZeroOp::create(builder, location,
                                                          continuationsType);
                       for (auto [index, continuation] :
                            llvm::enumerate(analysis.getContinuations()))
                         array = LLVM::InsertValueOp::create(
                             builder, location, array,
                             llvmConstant(builder, location, i32, continuation),
                             ArrayRef<int64_t>{static_cast<int64_t>(index)});
                       return array;
                     });
  makeConstantGlobal(
      module, location, layoutType, layoutName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &builder) {
        Value layout = LLVM::ZeroOp::create(builder, location, layoutType);
        layout = insertValue(builder, location, layout,
                             llvmConstant(builder, location, i32, 1), 0);
        layout = insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i64, analysis.getFrameSize()), 2);
        layout = insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i64, analysis.getFrameAlignment()),
            3);
        layout = insertValue(
            builder, location, layout,
            LLVM::AddressOfOp::create(builder, location, pointer, fieldsName),
            4);
        layout = insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i32, analysis.getFields().size()),
            5);
        layout = insertValue(builder, location, layout,
                             llvmConstant(builder, location, i32,
                                          analysis.getContinuations().size()),
                             6);
        layout = insertValue(builder, location, layout,
                             LLVM::AddressOfOp::create(
                                 builder, location, pointer, continuationsName),
                             7);
        return insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i64, analysis.getChecksum()), 8);
      });
  makeConstantGlobal(
      module, location, descriptorType, descriptorName, LLVM::Linkage::External,
      8, [&](OpBuilder &builder) {
        Value handle = LLVM::ZeroOp::create(builder, location, handleType);
        handle = insertValue(builder, location, handle,
                             llvmConstant(builder, location, i32, 6), 0);
        handle = insertValue(
            builder, location, handle,
            llvmConstant(builder, location, i64, stableID), 2);
        Value descriptor =
            LLVM::ZeroOp::create(builder, location, descriptorType);
        descriptor = insertValue(builder, location, descriptor, handle, 0);
        descriptor = insertValue(builder, location, descriptor,
                                 llvmConstant(builder, location, i32, 1), 1);
        descriptor = insertValue(
            builder, location, descriptor,
            llvmConstant(builder, location, i32, hasDesignBytecode ? 3 : 1), 3);
        descriptor = insertValue(
            builder, location, descriptor,
            LLVM::AddressOfOp::create(builder, location, pointer, layoutName),
            5);
        descriptor = insertValue(
            builder, location, descriptor,
            LLVM::AddressOfOp::create(
                builder, location, pointer,
                (baseName + ".__obelisk_native_requirements").str()),
            6);
        descriptor =
            insertValue(builder, location, descriptor,
                        LLVM::AddressOfOp::create(
                            builder, location, pointer,
                            (baseName + ".__obelisk_native_execute").str()),
                        7);
        descriptor =
            insertValue(builder, location, descriptor,
                        LLVM::AddressOfOp::create(
                            builder, location, pointer,
                            (baseName + ".__obelisk_native_destroy").str()),
                        8);
        if (hasExecution)
          descriptor =
              insertValue(builder, location, descriptor,
                          LLVM::AddressOfOp::create(builder, location, pointer,
                                                    executionName),
                          10);
        if (hasDesignBytecode)
          descriptor =
              insertValue(builder, location, descriptor,
                          LLVM::AddressOfOp::create(builder, location, pointer,
                                                    designBytecodeName),
                          11);
        return descriptor;
      });
  return success();
}

LogicalResult threadRuntimeStatuses(ModuleOp module) {
  MLIRContext *context = module.getContext();
  llvm::StringMap<sim::SimFuncOp> functions;
  SmallVector<sim::SimFuncOp> orderedFunctions;
  module.walk([&](sim::SimFuncOp function) {
    functions[function.getSymName()] = function;
    orderedFunctions.push_back(function);
  });

  llvm::DenseSet<Operation *> mayFail;
  for (sim::SimFuncOp function : orderedFunctions)
    function.walk([&](sim::SimStatusCheckOp) {
      mayFail.insert(function.getOperation());
    });

  bool changed;
  do {
    changed = false;
    for (sim::SimFuncOp function : orderedFunctions) {
      if (mayFail.contains(function.getOperation()))
        continue;
      bool callsFailing = false;
      function.walk([&](sim::SimCallOp call) {
        auto callee = functions.find(call.getCallee());
        callsFailing |= callee != functions.end() &&
                        mayFail.contains(callee->second.getOperation());
      });
      if (callsFailing)
        changed |= mayFail.insert(function.getOperation()).second;
    }
  } while (changed);

  llvm::DenseSet<Operation *> statusReturning;
  OpBuilder builder(context);
  Type statusType = runtime::StatusType::get(context);
  for (sim::SimFuncOp function : orderedFunctions) {
    if (!mayFail.contains(function.getOperation()) ||
        function.getEntryKind() != sim::EntryKind::Function)
      continue;
    statusReturning.insert(function.getOperation());
    SmallVector<Type> results(function.getResultTypes());
    results.push_back(statusType);
    function.setType(
        FunctionType::get(context, function.getArgumentTypes(), results));
    SmallVector<Attribute> resultAttrs;
    if (auto attrs = function.getResAttrs())
      llvm::append_range(resultAttrs, *attrs);
    while (resultAttrs.size() != results.size())
      resultAttrs.push_back(builder.getDictionaryAttr({}));
    function.setResAttrsAttr(builder.getArrayAttr(resultAttrs));
  }

  SmallVector<sim::SimCallOp> calls;
  module.walk([&](sim::SimCallOp call) { calls.push_back(call); });
  IRRewriter rewriter(context);
  for (sim::SimCallOp call : calls) {
    auto callee = functions.find(call.getCallee());
    if (callee == functions.end() ||
        !statusReturning.contains(callee->second.getOperation()))
      continue;
    SmallVector<Type> results(call.getResultTypes());
    results.push_back(statusType);
    SmallVector<Attribute> resultAttrs;
    if (auto attrs = call.getResAttrs())
      llvm::append_range(resultAttrs, *attrs);
    while (resultAttrs.size() != results.size())
      resultAttrs.push_back(rewriter.getDictionaryAttr({}));
    rewriter.setInsertionPoint(call);
    auto replacement = sim::SimCallOp::create(
        rewriter, call.getLoc(), results, call.getCalleeAttr(),
        call.getOperands(), call.getArgAttrsAttr(),
        rewriter.getArrayAttr(resultAttrs));
    for (auto [oldResult, newResult] : llvm::zip_equal(
             call.getResults(), replacement.getResults().drop_back()))
      oldResult.replaceAllUsesWith(newResult);
    rewriter.setInsertionPointAfter(replacement);
    sim::SimStatusCheckOp::create(rewriter, call.getLoc(),
                                  replacement.getResults().back());
    rewriter.eraseOp(call);
  }

  for (sim::SimFuncOp function : orderedFunctions) {
    if (!statusReturning.contains(function.getOperation()))
      continue;
    SmallVector<sim::SimReturnOp> returns;
    function.walk(
        [&](sim::SimReturnOp operation) { returns.push_back(operation); });
    for (sim::SimReturnOp operation : returns) {
      rewriter.setInsertionPoint(operation);
      Value zero = arith::ConstantOp::create(rewriter, operation.getLoc(),
                                             rewriter.getI32Type(),
                                             rewriter.getI32IntegerAttr(0));
      Value ok = runtime::RTStatusFromBitsOp::create(
          rewriter, operation.getLoc(), statusType, zero);
      SmallVector<Value> operands(operation.getOperands());
      operands.push_back(ok);
      sim::SimReturnOp::create(rewriter, operation.getLoc(), operands);
      rewriter.eraseOp(operation);
    }

    SmallVector<sim::SimStatusCheckOp> checks;
    function.walk(
        [&](sim::SimStatusCheckOp check) { checks.push_back(check); });
    for (sim::SimStatusCheckOp check : checks) {
      Block *source = check->getBlock();
      Block *continuation = source->splitBlock(std::next(check->getIterator()));
      Block *failure = new Block;
      function.getBody().push_back(failure);
      rewriter.setInsertionPoint(check);
      Value ok = runtime::RTStatusIsOp::create(
          rewriter, check.getLoc(), rewriter.getI1Type(), check.getStatus(), 0);
      cf::CondBranchOp::create(rewriter, check.getLoc(), ok, continuation,
                               ValueRange{}, failure, ValueRange{});
      Value status = check.getStatus();
      rewriter.eraseOp(check);

      rewriter.setInsertionPointToStart(failure);
      SmallVector<Value> values;
      for (Type type : function.getResultTypes().drop_back()) {
        auto integer = dyn_cast<IntegerType>(type);
        if (!integer)
          return function.emitError()
                 << "cannot materialize a failure result for " << type;
        values.push_back(
            arith::ConstantOp::create(rewriter, function.getLoc(), integer,
                                      rewriter.getIntegerAttr(integer, 0)));
      }
      values.push_back(status);
      sim::SimReturnOp::create(rewriter, function.getLoc(), values);
    }
  }
  return success();
}

LogicalResult
makePlainNativeWrappers(ModuleOp module, func::FuncOp body, StringRef baseName,
                        const SimulationProcessFrameAnalysis &analysis) {
  OpBuilder builder(body);
  builder.setInsertionPointAfter(body);
  Location location = body.getLoc();
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Type voidType = LLVM::LLVMVoidType::get(context);

  auto requirements = LLVM::LLVMFuncOp::create(
      builder, location, (baseName + ".__obelisk_native_requirements").str(),
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
  Block *requirementsEntry = requirements.addEntryBlock(builder);
  builder.setInsertionPointToStart(requirementsEntry);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, 0),
                        requirementsEntry->getArgument(0), 8);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, 1),
                        requirementsEntry->getArgument(1), 8);
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, 0));

  builder.setInsertionPointAfter(requirements);
  auto execute = LLVM::LLVMFuncOp::create(
      builder, location, (baseName + ".__obelisk_native_execute").str(),
      LLVM::LLVMFunctionType::get(i32, {pointer}, false));
  Block *executeEntry = execute.addEntryBlock(builder);
  builder.setInsertionPointToStart(executeEntry);
  Value instance = executeEntry->getArgument(0);
  Value runtimeContext = loadAt(builder, location, instance,
                                kInstanceContextOffset, pointer, 8);
  Value currentContext = LLVM::AddressOfOp::create(
      builder, location, pointer, "__obelisk_current_context");
  LLVM::StoreOp::create(builder, location, runtimeContext, currentContext, 8);
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
  SmallVector<Value> arguments;
  size_t physicalArgument = 0;
  Block &bodyEntry = body.getBody().front();
  for (const ProcessFrameValue &slot : analysis.getEntryCaptureLayout()) {
    if (slot.valueOffset == kNoOffset) {
      arguments.push_back(loadAt(builder, location, instance,
                                 kInstanceContextOffset, pointer, 8));
      ++physicalArgument;
      continue;
    }
    Type valueType = bodyEntry.getArgument(physicalArgument++).getType();
    arguments.push_back(loadAt(builder, location, frame, slot.valueOffset,
                               valueType, slot.alignment));
    if (slot.isFourState()) {
      Type unknownType = bodyEntry.getArgument(physicalArgument++).getType();
      arguments.push_back(loadAt(builder, location, frame, slot.unknownOffset,
                                 unknownType, slot.alignment));
    }
  }
  if (arguments.size() != bodyEntry.getNumArguments())
    return body.emitError(
        "converted entry arity disagrees with canonical capture layout");
  auto call = func::CallOp::create(builder, location, body.getSymName(),
                                   TypeRange{i32}, arguments);
  storeAt(builder, location, instance, kInstanceContinuationOffset,
          llvmConstant(builder, location, i32, 0), 4);
  publishAction(builder, location, instance, 2, 0, 0, 0,
                llvmConstant(builder, location, i64, 0), 0);
  LLVM::ReturnOp::create(builder, location, call.getResult(0));

  builder.setInsertionPointAfter(execute);
  auto destroy = LLVM::LLVMFuncOp::create(
      builder, location, (baseName + ".__obelisk_native_destroy").str(),
      LLVM::LLVMFunctionType::get(voidType, {pointer}, false));
  Block *destroyEntry = destroy.addEntryBlock(builder);
  builder.setInsertionPointToStart(destroyEntry);
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  return success();
}

LogicalResult
lowerPlainNativeProcess(sim::SimFuncOp function,
                        const SimulationProcessFrameAnalysis &analysis) {
  if (!function.getResultTypes().empty())
    return function.emitError("simulation process cannot return values");
  if (failed(lowerTimeOperations(function)))
    return failure();
  ModuleOp module = function->getParentOfType<ModuleOp>();
  Location location = function.getLoc();
  MLIRContext *context = function.getContext();
  std::string baseName = function.getSymName().str();
  uint64_t stableID = function.getCodeUnitId().value_or(
      stableProcessID(baseName) &
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
  context->getOrLoadDialect<func::FuncDialect>();
  SmallVector<Type> inputTypes;
  for (BlockArgument argument : function.getBody().front().getArguments())
    inputTypes.push_back(convertProcessType(argument.getType(), context));
  OpBuilder builder(function);
  auto body = func::FuncOp::create(
      builder, location, baseName,
      FunctionType::get(context, inputTypes, TypeRange{builder.getI32Type()}));
  body->setAttr("obelisk.entry_kind",
                builder.getI32IntegerAttr(
                    static_cast<uint32_t>(function.getEntryKind())));
  body->setAttr("obelisk.native_scratch_size", builder.getI64IntegerAttr(0));
  body.getBody().takeBody(function.getBody());
  function.erase();
  for (Block &block : body.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(convertProcessType(argument.getType(), context));

  IRRewriter rewriter(context);
  SmallVector<Operation *> operations;
  body.walk([&](Operation *operation) {
    if (isa<sim::SimReturnOp, sim::SimCallOp, sim::SimSpawnOp>(operation))
      operations.push_back(operation);
  });
  for (Operation *operation : operations) {
    rewriter.setInsertionPoint(operation);
    if (auto returnOp = dyn_cast<sim::SimReturnOp>(operation)) {
      Value zero = arith::ConstantOp::create(rewriter, returnOp.getLoc(),
                                             rewriter.getI32Type(),
                                             rewriter.getI32IntegerAttr(0));
      func::ReturnOp::create(rewriter, returnOp.getLoc(), zero);
      rewriter.eraseOp(returnOp);
      continue;
    }
    if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
      auto converted =
          func::CallOp::create(rewriter, call.getLoc(), call.getCallee(),
                               call.getResultTypes(), call.getOperands());
      rewriter.replaceOp(call, converted.getResults());
      continue;
    }
    auto spawn = cast<sim::SimSpawnOp>(operation);
    auto converted = LLVM::CallOp::create(
        rewriter, spawn.getLoc(), TypeRange{rewriter.getI64Type()},
        SymbolRefAttr::get(rewriter.getContext(),
                           (spawn.getCallee() + ".__obelisk_spawn").str()),
        spawn.getOperands());
    rewriter.replaceOp(spawn, converted.getResults());
  }

  SmallVector<sim::SimStatusCheckOp> checks;
  body.walk([&](sim::SimStatusCheckOp check) { checks.push_back(check); });
  for (sim::SimStatusCheckOp check : checks) {
    Block *source = check->getBlock();
    Block *continuation = source->splitBlock(std::next(check->getIterator()));
    Block *failure = new Block;
    body.getBody().push_back(failure);
    rewriter.setInsertionPoint(check);
    Value ok = runtime::RTStatusIsOp::create(
        rewriter, check.getLoc(), rewriter.getI1Type(), check.getStatus(), 0);
    cf::CondBranchOp::create(rewriter, check.getLoc(), ok, continuation,
                             ValueRange{}, failure, ValueRange{});
    Value status = check.getStatus();
    rewriter.eraseOp(check);
    rewriter.setInsertionPointToStart(failure);
    Value bits = runtime::RTStatusToBitsOp::create(
        rewriter, body.getLoc(), rewriter.getI32Type(), status);
    func::ReturnOp::create(rewriter, body.getLoc(), bits);
  }

  if (failed(makePlainNativeWrappers(module, body, baseName, analysis)))
    return failure();
  return makeProcessDescriptor(module, location, baseName, stableID, analysis);
}

LogicalResult
lowerSuspendableProcess(sim::SimFuncOp function,
                        const SimulationProcessFrameAnalysis &analysis) {
  if (failed(lowerTimeOperations(function)))
    return failure();
  ModuleOp module = function->getParentOfType<ModuleOp>();
  OpBuilder builder(function);
  Location location = function.getLoc();
  MLIRContext *context = function.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  std::string baseName = function.getSymName().str();
  uint64_t stableID = function.getCodeUnitId().value_or(
      stableProcessID(baseName) &
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
  std::string rampName = baseName + ".__obelisk_coro_ramp";
  auto ramp = LLVM::LLVMFuncOp::create(
      builder, location, rampName,
      LLVM::LLVMFunctionType::get(LLVM::LLVMVoidType::get(context),
                                  {pointer, i32, pointer, pointer}, false));
  ramp->setAttr(
      "passthrough",
      builder.getArrayAttr({builder.getStringAttr("presplitcoroutine")}));
  addFrameAttributes(ramp, analysis, builder);
  ramp.getBody().takeBody(function.getBody());
  function.erase();

  for (Block &block : ramp.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(convertProcessType(argument.getType(), context));

  IRRewriter callRewriter(context);
  SmallVector<Operation *> calls;
  ramp.walk([&](Operation *operation) {
    if (isa<sim::SimCallOp, sim::SimSpawnOp>(operation))
      calls.push_back(operation);
  });
  for (Operation *operation : calls) {
    callRewriter.setInsertionPoint(operation);
    if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
      auto converted =
          func::CallOp::create(callRewriter, call.getLoc(), call.getCallee(),
                               call.getResultTypes(), call.getOperands());
      callRewriter.replaceOp(call, converted.getResults());
      continue;
    }
    auto spawn = cast<sim::SimSpawnOp>(operation);
    auto converted = LLVM::CallOp::create(
        callRewriter, spawn.getLoc(), TypeRange{callRewriter.getI64Type()},
        SymbolRefAttr::get(callRewriter.getContext(),
                           (spawn.getCallee() + ".__obelisk_spawn").str()),
        spawn.getOperands());
    callRewriter.replaceOp(spawn, converted.getResults());
  }

  Block *oldEntry = &ramp.getBody().front();
  Block *entry = new Block;
  for (Type type : {pointer, i32, pointer, pointer})
    entry->addArgument(type, location);
  ramp.getBody().getBlocks().insert(ramp.getBody().begin(), entry);
  Block *requirements = new Block;
  Block *execute = new Block;
  ramp.getBody().push_back(requirements);
  ramp.getBody().push_back(execute);
  builder.setInsertionPointToStart(entry);
  Value zero32 = llvmConstant(builder, location, i32, 0);
  Value null = LLVM::ZeroOp::create(builder, location, pointer);
  Value id = LLVM::CoroIdOp::create(builder, location,
                                    LLVM::LLVMTokenType::get(context), zero32,
                                    null, null, null);
  Value requirementsMode =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                            entry->getArgument(1), zero32);
  cf::CondBranchOp::create(builder, location, requirementsMode, requirements,
                           ValueRange{}, execute, ValueRange{});

  builder.setInsertionPointToStart(requirements);
  Value size = LLVM::CoroSizeOp::create(builder, location, i64);
  Value alignment = LLVM::CoroAlignOp::create(builder, location, i64);
  LLVM::StoreOp::create(builder, location, size, entry->getArgument(2), 8);
  LLVM::StoreOp::create(builder, location, alignment, entry->getArgument(3), 8);
  LLVM::ReturnOp::create(builder, location, ValueRange{});

  builder.setInsertionPointToStart(execute);
  Value instance = entry->getArgument(0);
  Value allocation = loadAt(builder, location, instance,
                            kInstanceAllocationOffset, pointer, 8);
  Value scratchOffset =
      loadAt(builder, location, instance, kInstanceScratchOffset, i64, 8);
  Value scratch =
      LLVM::GEPOp::create(builder, location, pointer, builder.getI8Type(),
                          allocation, ValueRange{scratchOffset});
  Value handle =
      LLVM::CoroBeginOp::create(builder, location, pointer, id, scratch);
  storeAt(builder, location, instance, kInstanceNativeHandleOffset, handle, 8);

  RampBlocks blocks;
  blocks.suspendReturn =
      makeCoroutineReturnBlock(ramp.getBody(), location, handle);
  blocks.cleanup = new Block;
  bool canTerminate = false;
  ramp.walk([&](Operation *operation) {
    canTerminate |= isa<sim::SimReturnOp, sim::SimStatusCheckOp>(operation);
  });
  blocks.terminate = canTerminate ? new Block : nullptr;
  ramp.getBody().push_back(blocks.cleanup);
  if (blocks.terminate)
    ramp.getBody().push_back(blocks.terminate);

  llvm::SetVector<Block *> continuationBlocks;
  for (Block &block : ramp.getBody())
    if (!block.empty() && isSuspension(block.getTerminator()))
      continuationBlocks.insert(block.getTerminator()->getSuccessor(0));
  for (Block *continuation : continuationBlocks) {
    Block *shim = new Block;
    ramp.getBody().push_back(shim);
    blocks.shims[continuation] = shim;
    builder.setInsertionPointToStart(shim);
    Value frame =
        loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
    SmallVector<Value> loaded;
    size_t argumentIndex = 0;
    uint32_t continuationID = 0;
    for (Block *predecessor : continuation->getPredecessors()) {
      if (auto id = predecessor->getTerminator()->getAttrOfType<IntegerAttr>(
              "obelisk.coro.continuation")) {
        continuationID = id.getInt();
        break;
      }
    }
    if (continuationID == 0)
      return continuation->getParentOp()->emitError(
          "continuation block is missing its stable continuation ID");
    for (const ProcessFrameValue &slot :
         analysis.getContinuationLayout(continuationID)) {
      Type valueType = continuation->getArgument(argumentIndex++).getType();
      loaded.push_back(loadAt(builder, location, frame, slot.valueOffset,
                              valueType, slot.alignment));
      if (slot.isFourState()) {
        Type unknownType = continuation->getArgument(argumentIndex++).getType();
        loaded.push_back(loadAt(builder, location, frame, slot.unknownOffset,
                                unknownType, slot.alignment));
      }
    }
    cf::BranchOp::create(builder, location, continuation, loaded);
  }

  builder.setInsertionPointToEnd(execute);
  SmallVector<Value> entryArguments;
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
  ArrayRef<ProcessFrameValue> captureLayout = analysis.getEntryCaptureLayout();
  size_t physicalArgument = 0;
  auto refreshFrameArgument = [&](BlockArgument argument, Type type,
                                  uint64_t offset, uint32_t alignment) {
    SmallVector<OpOperand *> uses;
    for (OpOperand &use : argument.getUses())
      uses.push_back(&use);
    OpBuilder refreshBuilder(context);
    for (OpOperand *use : uses) {
      Location useLocation = use->getOwner()->getLoc();
      refreshBuilder.setInsertionPoint(use->getOwner());
      Value currentFrame = loadAt(refreshBuilder, useLocation, instance,
                                  kInstanceFrameOffset, pointer, 8);
      use->set(loadAt(refreshBuilder, useLocation, currentFrame, offset, type,
                      alignment));
    }
  };
  for (const ProcessFrameValue &slot : captureLayout) {
    if (slot.valueOffset == kNoOffset) {
      entryArguments.push_back(loadAt(builder, location, instance,
                                      kInstanceContextOffset, pointer, 8));
      // The scheduler may supply a different transient context on every
      // invocation.  Do not let LLVM preserve the first context in the
      // coroutine frame: reload it through the runtime-owned instance at each
      // actual use, including uses reached after a resume.
      BlockArgument contextArgument = oldEntry->getArgument(physicalArgument);
      SmallVector<OpOperand *> contextUses;
      for (OpOperand &use : contextArgument.getUses())
        contextUses.push_back(&use);
      OpBuilder refreshBuilder(context);
      for (OpOperand *use : contextUses) {
        refreshBuilder.setInsertionPoint(use->getOwner());
        Value currentContext =
            loadAt(refreshBuilder, use->getOwner()->getLoc(), instance,
                   kInstanceContextOffset, pointer, 8);
        use->set(currentContext);
      }
      ++physicalArgument;
      continue;
    }
    BlockArgument valueArgument = oldEntry->getArgument(physicalArgument++);
    Type valueType = valueArgument.getType();
    entryArguments.push_back(loadAt(builder, location, frame, slot.valueOffset,
                                    valueType, slot.alignment));
    // Captures are canonical-frame state, not coroutine-frame state. Reload
    // them through the instance at every use so LLVM only needs to preserve
    // the instance/control pointer across suspension.
    refreshFrameArgument(valueArgument, valueType, slot.valueOffset,
                         slot.alignment);
    if (slot.isFourState()) {
      BlockArgument unknownArgument = oldEntry->getArgument(physicalArgument++);
      Type unknownType = unknownArgument.getType();
      entryArguments.push_back(loadAt(builder, location, frame,
                                      slot.unknownOffset, unknownType,
                                      slot.alignment));
      refreshFrameArgument(unknownArgument, unknownType, slot.unknownOffset,
                           slot.alignment);
    }
  }
  if (entryArguments.size() != oldEntry->getNumArguments())
    return ramp.emitError(
        "converted entry arity disagrees with canonical capture layout");
  for (auto [argument, loaded] :
       llvm::zip_equal(oldEntry->getArguments(), entryArguments))
    argument.replaceAllUsesWith(loaded);

  // Dynamic entry dispatch supports bytecode -> native reconstruction at any
  // stable semantic continuation. Ordinary CFG edges remain direct branches.
  Block *dispatch = new Block;
  ramp.getBody().push_back(dispatch);
  cf::BranchOp::create(builder, location, dispatch);
  builder.setInsertionPointToStart(dispatch);
  Value continuationID =
      loadAt(builder, location, instance, kInstanceContinuationOffset, i32, 4);
  SmallVector<std::pair<uint32_t, Block *>> targets;
  targets.emplace_back(0, oldEntry);
  for (uint32_t idValue : analysis.getContinuations()) {
    if (idValue == 0)
      continue;
    Block *target = nullptr;
    for (Block *candidate : continuationBlocks) {
      for (Block *predecessor : candidate->getPredecessors()) {
        Operation *terminator = predecessor->getTerminator();
        auto idAttr =
            terminator->getAttrOfType<IntegerAttr>("obelisk.coro.continuation");
        if (idAttr && idAttr.getInt() == idValue)
          target = blocks.shims.lookup(candidate);
      }
    }
    if (target)
      targets.emplace_back(idValue, target);
  }
  Block *invalid = new Block;
  ramp.getBody().push_back(invalid);
  builder.setInsertionPointToStart(invalid);
  storeAt(builder, location, instance, kInstanceStatusOffset,
          llvmConstant(builder, location, i32, 12), 4);
  cf::BranchOp::create(builder, location, blocks.cleanup);
  Block *test = dispatch;
  for (auto [index, target] : llvm::enumerate(targets)) {
    builder.setInsertionPointToEnd(test);
    Value expected = llvmConstant(builder, location, i32, target.first);
    Value equal = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, continuationID, expected);
    Block *next = index + 1 == targets.size() ? invalid : new Block;
    if (next != invalid)
      ramp.getBody().push_back(next);
    ValueRange arguments =
        target.first == 0 ? ValueRange(entryArguments) : ValueRange{};
    cf::CondBranchOp::create(builder, location, equal, target.second, arguments,
                             next, ValueRange{});
    test = next;
  }

  if (blocks.terminate) {
    // LLVM permits exactly one final suspend per switched-resume coroutine.
    // Funnel every semantic process return through this shared block.
    builder.setInsertionPointToStart(blocks.terminate);
    publishAction(builder, location, instance, 2, 0, 0, 0,
                  llvmConstant(builder, location, i64, 0), 0);
    Value final = llvmConstant(builder, location, builder.getI1Type(), 1);
    Value save = LLVM::CoroSaveOp::create(
        builder, location, LLVM::LLVMTokenType::get(context), handle);
    Value finalState = LLVM::CoroSuspendOp::create(
        builder, location, builder.getI8Type(), save, final);
    Block *invalidFinalResume = new Block;
    ramp.getBody().push_back(invalidFinalResume);
    builder.setInsertionPointToStart(invalidFinalResume);
    LLVM::UnreachableOp::create(builder, location);
    builder.setInsertionPointToEnd(blocks.terminate);
    SmallVector<Block *> destinations{invalidFinalResume, blocks.cleanup};
    SmallVector<ValueRange> destinationOperands(2);
    SmallVector<APInt> caseValues{APInt(8, 0), APInt(8, 1)};
    LLVM::SwitchOp::create(builder, location, finalState, blocks.suspendReturn,
                           ValueRange{}, caseValues, destinations,
                           destinationOperands, ArrayRef<int32_t>{});
  }

  builder.setInsertionPointToStart(blocks.cleanup);
  storeAt(builder, location, instance, kInstanceNativeHandleOffset,
          LLVM::ZeroOp::create(builder, location, pointer), 8);
  cf::BranchOp::create(builder, location, blocks.suspendReturn);

  SmallVector<Operation *> terminators;
  ramp.walk([&](Operation *operation) {
    if (isSuspension(operation) || isa<sim::SimReturnOp>(operation))
      terminators.push_back(operation);
  });
  for (Operation *operation : terminators) {
    if (isSuspension(operation)) {
      if (failed(lowerSuspendTerminator(operation, instance, handle, analysis,
                                        blocks)))
        return failure();
    } else if (failed(lowerFinalReturn(cast<sim::SimReturnOp>(operation),
                                       blocks))) {
      return failure();
    }
  }

  SmallVector<sim::SimStatusCheckOp> checks;
  ramp.walk([&](sim::SimStatusCheckOp check) { checks.push_back(check); });
  for (sim::SimStatusCheckOp check : checks) {
    Block *source = check->getBlock();
    Block *continuation = source->splitBlock(std::next(check->getIterator()));
    Block *failure = new Block;
    ramp.getBody().push_back(failure);
    builder.setInsertionPoint(check);
    Value ok = runtime::RTStatusIsOp::create(
        builder, check.getLoc(), builder.getI1Type(), check.getStatus(), 0);
    cf::CondBranchOp::create(builder, check.getLoc(), ok, continuation,
                             ValueRange{}, failure, ValueRange{});
    Value status = check.getStatus();
    check.erase();
    builder.setInsertionPointToStart(failure);
    Value bits = runtime::RTStatusToBitsOp::create(
        builder, location, builder.getI32Type(), status);
    storeAt(builder, location, instance, kInstanceStatusOffset, bits, 4);
    cf::BranchOp::create(builder, location, blocks.terminate);
  }
  if (failed(makeNativeWrappers(module, ramp, baseName)))
    return failure();
  return makeProcessDescriptor(module, location, baseName, stableID, analysis);
}

LogicalResult lowerOrdinaryFunction(sim::SimFuncOp function) {
  Location location = function.getLoc();
  std::string symbolName = function.getSymName().str();
  FunctionType functionType = function.getFunctionType();
  SmallVector<Type> inputTypes;
  SmallVector<Type> resultTypes;
  for (Type type : functionType.getInputs())
    inputTypes.push_back(convertProcessType(type, function.getContext()));
  for (Type type : functionType.getResults())
    resultTypes.push_back(convertProcessType(type, function.getContext()));
  uint32_t entryKind = static_cast<uint32_t>(function.getEntryKind());
  function.getContext()->getOrLoadDialect<func::FuncDialect>();
  OpBuilder builder(function.getContext());
  builder.setInsertionPoint(function);
  auto replacement =
      func::FuncOp::create(builder, location, builder.getStringAttr(symbolName),
                           TypeAttr::get(FunctionType::get(
                               function.getContext(), inputTypes, resultTypes)),
                           StringAttr{}, ArrayAttr{}, ArrayAttr{}, UnitAttr{});
  replacement->setAttr("obelisk.entry_kind",
                       builder.getI32IntegerAttr(entryKind));
  replacement->setAttr("obelisk.native_scratch_size",
                       builder.getI64IntegerAttr(0));
  replacement.getBody().takeBody(function.getBody());
  function.erase();
  for (Block &block : replacement.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(
          convertProcessType(argument.getType(), replacement.getContext()));
  IRRewriter rewriter(replacement.getContext());
  SmallVector<Operation *> operations;
  replacement.walk([&](Operation *operation) {
    if (isa<sim::SimReturnOp, sim::SimCallOp>(operation))
      operations.push_back(operation);
  });
  for (Operation *operation : operations) {
    rewriter.setInsertionPoint(operation);
    if (auto returnOp = dyn_cast<sim::SimReturnOp>(operation)) {
      func::ReturnOp::create(rewriter, returnOp.getLoc(),
                             returnOp.getOperands());
      rewriter.eraseOp(returnOp);
    } else {
      auto call = cast<sim::SimCallOp>(operation);
      SmallVector<Type> convertedResults;
      for (Type type : call.getResultTypes())
        convertedResults.push_back(
            convertProcessType(type, replacement.getContext()));
      auto converted =
          func::CallOp::create(rewriter, call.getLoc(), call.getCallee(),
                               convertedResults, call.getOperands());
      rewriter.replaceOp(call, converted.getResults());
    }
  }
  return success();
}

/// Make every non-capture value crossing a block boundary explicit in block
/// arguments. In particular, this turns values live across a semantic
/// suspension into successor operands, so the canonical continuation lanes
/// own them instead of LLVM's coroutine frame. The fixed point also threads a
/// value through ordinary CFG blocks before or after a suspension.
LogicalResult threadProcessStateThroughCFG(sim::SimFuncOp function) {
  Block *entry = &function.getBody().front();

  // Front-end suspension threading is deliberately conservative and may
  // forward literal constants into continuation arguments. Recreate those
  // constants in the continuation instead: immutable byte spans contain a
  // generated native address and therefore must never be persisted in the
  // pointer-free canonical frame.
  for (Block &block : llvm::drop_begin(function.getBody())) {
    for (int64_t argumentIndex =
             static_cast<int64_t>(block.getNumArguments()) - 1;
         argumentIndex >= 0; --argumentIndex) {
      Value incomingValue;
      SmallVector<std::pair<Operation *, unsigned>> incomingEdges;
      bool canRematerialize = true;
      for (Block &predecessor : function.getBody()) {
        Operation *terminator = predecessor.getTerminator();
        auto branch = dyn_cast<BranchOpInterface>(terminator);
        if (!branch) {
          if (llvm::is_contained(predecessor.getSuccessors(), &block)) {
            canRematerialize = false;
            break;
          }
          continue;
        }
        for (auto [successorIndex, successor] :
             llvm::enumerate(predecessor.getSuccessors())) {
          if (successor != &block)
            continue;
          SuccessorOperands operands =
              branch.getSuccessorOperands(successorIndex);
          if (static_cast<unsigned>(argumentIndex) >= operands.size() ||
              operands.isOperandProduced(argumentIndex)) {
            canRematerialize = false;
            break;
          }
          Value value = operands[argumentIndex];
          if (!incomingValue)
            incomingValue = value;
          else if (incomingValue != value) {
            canRematerialize = false;
            break;
          }
          incomingEdges.emplace_back(terminator, successorIndex);
        }
        if (!canRematerialize)
          break;
      }
      auto result = dyn_cast_or_null<OpResult>(incomingValue);
      Operation *constant = result ? result.getOwner() : nullptr;
      if (!canRematerialize || incomingEdges.empty() || !constant ||
          constant->getNumOperands() != 0 ||
          !constant->hasTrait<OpTrait::ConstantLike>())
        continue;

      OpBuilder builder(&block, block.begin());
      Operation *clone = builder.clone(*constant);
      block.getArgument(argumentIndex)
          .replaceAllUsesWith(clone->getResult(result.getResultNumber()));
      for (auto [terminator, successorIndex] : incomingEdges) {
        auto branch = cast<BranchOpInterface>(terminator);
        branch.getSuccessorOperands(successorIndex)
            .erase(static_cast<unsigned>(argumentIndex));
      }
      block.eraseArgument(static_cast<unsigned>(argumentIndex));
    }
  }

  DenseMap<Block *, DenseMap<Value, BlockArgument>> threadedValues;
  bool changed;
  do {
    changed = false;
    for (Block &block : function.getBody()) {
      if (&block == entry)
        continue;
      llvm::SetVector<Value> externalValues;
      for (Operation &operation : block)
        for (Value value : operation.getOperands()) {
          if (value.getParentBlock() == &block)
            continue;
          if (auto argument = dyn_cast<BlockArgument>(value);
              argument && argument.getOwner() == entry)
            continue;
          externalValues.insert(value);
        }
      for (Value value : externalValues) {
        auto &threaded = threadedValues[&block];
        auto existing = threaded.find(value);
        if (existing != threaded.end()) {
          for (Operation &operation : block)
            for (OpOperand &operand : operation.getOpOperands())
              if (operand.get() == value)
                operand.set(existing->second);
          continue;
        }
        BlockArgument argument =
            block.addArgument(value.getType(), value.getLoc());
        threaded.insert({value, argument});
        SmallVector<OpOperand *> uses;
        for (Operation &operation : block)
          for (OpOperand &operand : operation.getOpOperands())
            if (operand.get() == value)
              uses.push_back(&operand);
        for (OpOperand *use : uses)
          use->set(argument);

        for (Block *predecessor : block.getPredecessors()) {
          auto branch =
              dyn_cast<BranchOpInterface>(predecessor->getTerminator());
          if (!branch)
            return predecessor->getTerminator()->emitError(
                "cannot thread suspension-live state through a non-branch "
                "terminator");
          bool found = false;
          for (auto [index, successor] :
               llvm::enumerate(predecessor->getSuccessors())) {
            if (successor != &block)
              continue;
            branch.getSuccessorOperands(index).append(value);
            found = true;
          }
          if (!found)
            return predecessor->getTerminator()->emitError(
                "predecessor is missing its CFG successor");
        }
        changed = true;
      }
    }
  } while (changed);
  return success();
}

// Native design state is represented by two deterministic bit planes. Static
// handles name a compiler-assigned root object plus a signed relative offset;
// the runtime registers each root's absolute plane range before scheduling.
// This keeps captures pointer-free and prevents partial out-of-range views from
// crossing into an adjacent object.
uint64_t encodeNativeStaticHandle(uint32_t id, int32_t offset = 0) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, id,
                                         offset);
}

struct NativeStateLayout {
  struct Bound {
    uint32_t handleID;
    uint64_t offset;
    unsigned width;
  };
  struct Net {
    uint64_t id;
    uint32_t handleID;
    uint64_t offset;
    unsigned width;
    bool fourState;
  };
  struct Driver {
    uint64_t id;
    uint64_t netId;
    uint32_t handleID;
    uint64_t offset;
    unsigned width;
  };
  DenseMap<uint64_t, uint64_t> storage;
  DenseMap<uint64_t, uint64_t> nets;
  DenseMap<uint64_t, uint64_t> drivers;
  SmallVector<Bound> bounds;
  SmallVector<Net> netLayouts;
  SmallVector<Driver> driverLayouts;
  uint64_t bitCount = 0;
};

FailureOr<NativeStateLayout> buildNativeStateLayout(ModuleOp module) {
  NativeStateLayout layout;
  uint32_t nextHandleID = 1;
  auto allocate = [&](Type type, uint64_t &offset,
                      uint64_t &handle) -> LogicalResult {
    std::optional<unsigned> width = nativeStateWidth(type);
    if (!width || *width == 0 || *width > INT32_MAX || nextHandleID == 0 ||
        nextHandleID > OBELISK_RT_STABLE_HANDLE_MAX_STATIC_ID)
      return failure();
    offset = layout.bitCount;
    if (layout.bitCount > std::numeric_limits<uint64_t>::max() - *width)
      return failure();
    layout.bitCount += *width;
    handle = encodeNativeStaticHandle(nextHandleID);
    layout.bounds.push_back({nextHandleID++, offset, *width});
    return success();
  };
  WalkResult walked = module.walk([&](Operation *operation) {
    if (auto declaration = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      uint64_t offset;
      uint64_t handle;
      if (failed(allocate(declaration.getType(), offset, handle))) {
        declaration.emitError("native storage must have a fixed packed width");
        return WalkResult::interrupt();
      }
      layout.storage[declaration.getId()] = handle;
    } else if (auto declaration = dyn_cast<sim::SimNetDeclOp>(operation)) {
      uint64_t offset;
      uint64_t handle;
      if (failed(allocate(declaration.getType(), offset, handle))) {
        declaration.emitError("native net must have a fixed packed width");
        return WalkResult::interrupt();
      }
      layout.nets[declaration.getId()] = handle;
      layout.netLayouts.push_back({declaration.getId(), nextHandleID - 1,
                                   offset,
                                   *nativeStateWidth(declaration.getType()),
                                   containsLogic(declaration.getType())});
    } else if (auto declaration = dyn_cast<sim::SimDriverDeclOp>(operation)) {
      auto found = layout.nets.find(declaration.getNetId());
      if (found == layout.nets.end()) {
        declaration.emitError("native driver references an unknown net");
        return WalkResult::interrupt();
      }
      uint64_t offset;
      uint64_t handle;
      std::optional<unsigned> width = nativeStateWidth(declaration.getType());
      if (!width || failed(allocate(declaration.getType(), offset, handle))) {
        declaration.emitError("native driver must have a fixed packed width");
        return WalkResult::interrupt();
      }
      layout.drivers[declaration.getId()] = handle;
      layout.driverLayouts.push_back(
          {declaration.getId(), declaration.getNetId(), nextHandleID - 1,
           offset, *width});
    }
    return WalkResult::advance();
  });
  if (walked.wasInterrupted())
    return failure();
  if (layout.bitCount >= OBELISK_RT_STABLE_HANDLE_STATIC_TAG) {
    module.emitError("native static state exceeds the handle address space");
    return failure();
  }
  // Keep one byte addressable so poison-free invalid-handle paths always have
  // a safe GEP base even for a design with no state.
  layout.bitCount = std::max<uint64_t>(layout.bitCount, 8);
  return layout;
}

struct NativeScheduleRanks {
  DenseMap<Operation *, uint32_t> entries;
  DenseMap<Block *, uint32_t> blocks;
};

struct NativeSchedulePlan {
  uint32_t initialRank = UINT32_MAX;
  SmallVector<std::pair<uint32_t, uint32_t>> continuations;
};

NativeScheduleRanks buildNativeScheduleRanks(ModuleOp module) {
  NativeScheduleRanks ranks;
  uint32_t fallback = 0;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::Function)
      return;
    ranks.entries[function.getOperation()] = fallback;
    for (Block &block : function.getBody())
      ranks.blocks[&block] = fallback;
    ++fallback;
  });
  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  if (!design || !design.getComputeGraphAttr())
    return ranks;
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  ArrayAttr nodes = graph.getNodes();
  uint32_t rank = 0;
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = dyn_cast<sim::ComputeRegionAttr>(regionAttribute);
    if (!region || (region.getKind() != sim::ComputeRegionKind::Active &&
                    region.getKind() != sim::ComputeRegionKind::Postponed))
      continue;
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = dyn_cast<sim::ComputeGroupAttr>(groupAttribute);
      if (!group)
        continue;
      for (int64_t member : group.getFragments().asArrayRef()) {
        if (member < 0 || static_cast<uint64_t>(member) >= nodes.size())
          continue;
        auto fragment = dyn_cast<sim::ComputeFragmentAttr>(nodes[member]);
        if (!fragment)
          continue;
        if (auto function = design.lookupSymbol<sim::SimFuncOp>(
                fragment.getFunction().getValue())) {
          if (fragment.getBlock() >= function.getBody().getBlocks().size())
            continue;
          auto block = function.getBody().begin();
          std::advance(block, fragment.getBlock());
          ranks.blocks[&*block] = rank;
          if (fragment.getBlock() == 0)
            ranks.entries[function.getOperation()] = rank;
        }
      }
      if (rank != UINT32_MAX)
        ++rank;
    }
  }
  return ranks;
}

LLVM::GlobalOp
makeStatePlane(ModuleOp module, StringRef name, uint64_t bytes, bool unknown,
               ArrayRef<NativeStateLayout::Driver> highImpedanceDrivers = {},
               ArrayRef<NativeStateLayout::Net> highImpedanceNets = {}) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  Location location = module.getLoc();
  Type i8 = builder.getI8Type();
  Type array = LLVM::LLVMArrayType::get(i8, bytes);
  auto global =
      LLVM::GlobalOp::create(builder, location, array, false,
                             LLVM::Linkage::Internal, name, Attribute{}, 8);
  Block *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToStart(block);
  Value value = LLVM::ZeroOp::create(builder, location, array);
  SmallVector<uint8_t> initial(bytes, unknown ? UINT8_MAX : 0);
  if (!unknown)
    for (const NativeStateLayout::Driver &driver : highImpedanceDrivers)
      for (unsigned bit = 0; bit < driver.width; ++bit) {
        uint64_t absolute = driver.offset + bit;
        initial[absolute / 8] |= static_cast<uint8_t>(1u << (absolute % 8));
      }
  if (!unknown)
    for (const NativeStateLayout::Net &net : highImpedanceNets) {
      if (!net.fourState)
        continue;
      for (unsigned bit = 0; bit < net.width; ++bit) {
        uint64_t absolute = net.offset + bit;
        initial[absolute / 8] |= static_cast<uint8_t>(1u << (absolute % 8));
      }
    }
  for (auto [index, byte] : llvm::enumerate(initial))
    if (byte != 0)
      value = LLVM::InsertValueOp::create(
          builder, location, value, llvmConstant(builder, location, i8, byte),
          ArrayRef<int64_t>{static_cast<int64_t>(index)});
  LLVM::ReturnOp::create(builder, location, value);
  return global;
}

Value offsetNativeHandle(OpBuilder &builder, Location location, Value handle,
                         Value offset) {
  return LLVM::CallOp::create(
             builder, location, TypeRange{builder.getI64Type()},
             SymbolRefAttr::get(builder.getContext(),
                                "obelisk_rt_v1_native_handle_offset"),
             ValueRange{handle, offset})
      .getResult();
}

Value isValidHandle(OpBuilder &builder, Location location, Value handle) {
  Value invalid = arith::ConstantOp::create(
      builder, location, builder.getI64Type(),
      builder.getI64IntegerAttr(static_cast<int64_t>(UINT64_MAX)));
  return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                               handle, invalid);
}

Value loadStatePlane(OpBuilder &builder, Location location, Value handle,
                     IntegerType resultType, StringRef globalName,
                     bool unknownFallback, uint64_t stateBitCount) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  IntegerType i32 = builder.getI32Type();
  IntegerType i64 = builder.getI64Type();
  Value base =
      LLVM::AddressOfOp::create(builder, location, pointer, globalName);
  Value one = llvmConstant(builder, location, i64, 1);
  Value out = LLVM::AllocaOp::create(builder, location, pointer, resultType,
                                    one, 1);
  Value contextAddress = LLVM::AddressOfOp::create(
      builder, location, pointer, "__obelisk_current_context");
  Value context =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(builder.getContext(),
                         "obelisk_rt_v1_native_state_load_plane"),
      ValueRange{context, base,
                 llvmConstant(builder, location, i64, stateBitCount), handle,
                 llvmConstant(builder, location, i64, resultType.getWidth()),
                 llvmConstant(builder, location, i32,
                              globalName == "__obelisk_state_unknown" ? 1 : 0),
                 llvmConstant(builder, location, i32,
                              unknownFallback ? 1 : 0),
                 out});
  return LLVM::LoadOp::create(builder, location, resultType, out, 1);
}

Value storeStatePlane(OpBuilder &builder, Location location, Value handle,
                      Value input, StringRef globalName,
                      uint64_t stateBitCount) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  IntegerType inputType = cast<IntegerType>(input.getType());
  IntegerType i8 = builder.getI8Type();
  IntegerType i32 = builder.getI32Type();
  IntegerType i64 = builder.getI64Type();
  Value base =
      LLVM::AddressOfOp::create(builder, location, pointer, globalName);
  Value one = llvmConstant(builder, location, i64, 1);
  Value in = LLVM::AllocaOp::create(builder, location, pointer, inputType, one,
                                   1);
  LLVM::StoreOp::create(builder, location, input, in, 1);
  Value changed = LLVM::AllocaOp::create(builder, location, pointer, i8, one,
                                        1);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i8, 0), changed, 1);
  Value contextAddress = LLVM::AddressOfOp::create(
      builder, location, pointer, "__obelisk_current_context");
  Value context =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(builder.getContext(),
                         "obelisk_rt_v1_native_state_store_plane"),
      ValueRange{context, base,
                 llvmConstant(builder, location, i64, stateBitCount), handle,
                 llvmConstant(builder, location, i64, inputType.getWidth()),
                 llvmConstant(builder, location, i32,
                              globalName == "__obelisk_state_unknown" ? 1 : 0),
                 in, changed});
  Value changedByte = LLVM::LoadOp::create(builder, location, i8, changed, 1);
  return arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ne, changedByte,
      llvmConstant(builder, location, i8, 0));
}

template <typename Op>
class ContextHandleConversion final : public OpConversionPattern<Op> {
public:
  ContextHandleConversion(const TypeConverter &converter, MLIRContext *context,
                          const DenseMap<uint64_t, uint64_t> &offsets)
      : OpConversionPattern<Op>(converter, context), offsets(offsets) {}

  LogicalResult
  matchAndRewrite(Op op, typename OpConversionPattern<Op>::OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto found = offsets.find(op.getId());
    if (found == offsets.end())
      return rewriter.notifyMatchFailure(op, "descriptor has no native layout");
    Value value =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(found->second));
    rewriter.replaceOp(op, value);
    return success();
  }

private:
  const DenseMap<uint64_t, uint64_t> &offsets;
};

void notifySignal(OpBuilder &builder, Location location, Value handle,
                  uint64_t width, Value oldValue, Value oldUnknown,
                  Value newValue, Value newUnknown);

class EventHandleConversion final
    : public OpConversionPattern<sim::SimContextEventOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContextEventOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Value value =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(op.getId()));
    rewriter.replaceOp(op, value);
    return success();
  }
};

class RefLoadConversion final : public OpConversionPattern<sim::SimRefLoadOp> {
public:
  RefLoadConversion(const TypeConverter &converter, MLIRContext *context,
                    uint64_t stateBitCount)
      : OpConversionPattern(converter, context),
        stateBitCount(stateBitCount) {}
  LogicalResult
  matchAndRewrite(sim::SimRefLoadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = op.getResult().getType();
    std::optional<unsigned> width = nativeStateWidth(resultType);
    if (!width || adaptor.getReference().size() != 1)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    Value value =
        loadStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                       plane, "__obelisk_state_value", false, stateBitCount);
    SmallVector<Value> converted{value};
    if (containsLogic(resultType))
      converted.push_back(loadStatePlane(rewriter, op.getLoc(),
                                         adaptor.getReference().front(), plane,
                                         "__obelisk_state_unknown", true,
                                         stateBitCount));
    SmallVector<ValueRange> replacements{ValueRange(converted)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }

private:
  uint64_t stateBitCount;
};

class RefStoreConversion final
    : public OpConversionPattern<sim::SimRefStoreOp> {
public:
  RefStoreConversion(const TypeConverter &converter, MLIRContext *context,
                     uint64_t stateBitCount)
      : OpConversionPattern(converter, context),
        stateBitCount(stateBitCount) {}
  LogicalResult
  matchAndRewrite(sim::SimRefStoreOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 1 || adaptor.getValue().empty())
      return failure();
    Type valueType = op.getValue().getType();
    std::optional<unsigned> width = nativeStateWidth(valueType);
    if (!width)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    Value oldValue = loadStatePlane(
        rewriter, op.getLoc(), adaptor.getReference().front(), plane,
        "__obelisk_state_value", false, stateBitCount);
    Value oldUnknown;
    if (containsLogic(valueType))
      oldUnknown = loadStatePlane(
          rewriter, op.getLoc(), adaptor.getReference().front(), plane,
          "__obelisk_state_unknown", true, stateBitCount);
    Value changed =
        storeStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                        adaptor.getValue().front(), "__obelisk_state_value",
                        stateBitCount);
    if (adaptor.getValue().size() == 2)
      changed = arith::OrIOp::create(
          rewriter, op.getLoc(), changed,
          storeStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                          adaptor.getValue()[1], "__obelisk_state_unknown",
                          stateBitCount));
    (void)changed;
    notifySignal(rewriter, op.getLoc(), adaptor.getReference().front(), *width,
                 oldValue, oldUnknown, adaptor.getValue().front(),
                 adaptor.getValue().size() == 2 ? adaptor.getValue()[1]
                                                : Value{});
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
};

class NetReadConversion final : public OpConversionPattern<sim::SimNetReadOp> {
public:
  NetReadConversion(const TypeConverter &converter, MLIRContext *context,
                    uint64_t stateBitCount)
      : OpConversionPattern(converter, context),
        stateBitCount(stateBitCount) {}
  LogicalResult
  matchAndRewrite(sim::SimNetReadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = op.getResult().getType();
    std::optional<unsigned> width = nativeStateWidth(resultType);
    if (!width || adaptor.getNet().size() != 1)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    SmallVector<Value> converted{
        loadStatePlane(rewriter, op.getLoc(), adaptor.getNet().front(), plane,
                       "__obelisk_state_value", false, stateBitCount)};
    if (containsLogic(resultType))
      converted.push_back(loadStatePlane(rewriter, op.getLoc(),
                                         adaptor.getNet().front(), plane,
                                         "__obelisk_state_unknown", true,
                                         stateBitCount));
    SmallVector<ValueRange> replacements{ValueRange(converted)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }

private:
  uint64_t stateBitCount;
};

std::optional<uint64_t> getStaticDriverID(Value value) {
  while (value) {
    if (auto context = value.getDefiningOp<sim::SimContextDriverOp>())
      return context.getId();
    Operation *definition = value.getDefiningOp();
    if (auto extract = dyn_cast_or_null<sim::SimDriverExtractOp>(definition)) {
      value = extract.getInput();
      continue;
    }
    if (auto extract =
            dyn_cast_or_null<sim::SimDriverDynExtractOp>(definition)) {
      value = extract.getInput();
      continue;
    }
    if (auto subelement =
            dyn_cast_or_null<sim::SimDriverSubelementOp>(definition)) {
      value = subelement.getInput();
      continue;
    }
    if (auto element =
            dyn_cast_or_null<sim::SimDriverArrayElementOp>(definition)) {
      value = element.getInput();
      continue;
    }
    auto argument = dyn_cast<BlockArgument>(value);
    if (!argument)
      return std::nullopt;
    auto function = dyn_cast<sim::SimFuncOp>(
        argument.getOwner()->getParentOp());
    if (!function)
      return std::nullopt;
    auto descriptor = function.getArgAttrOfType<IntegerAttr>(
        argument.getArgNumber(), "obelisk_sim.descriptor_id");
    return descriptor ? std::optional<uint64_t>(descriptor.getInt())
                      : std::nullopt;
  }
  return std::nullopt;
}

class DriverDriveConversion final
    : public OpConversionPattern<sim::SimDriverDriveOp> {
public:
  DriverDriveConversion(const TypeConverter &converter, MLIRContext *context,
                        const NativeStateLayout &layout)
      : OpConversionPattern(converter, context), layout(layout) {}
  LogicalResult
  matchAndRewrite(sim::SimDriverDriveOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDriver().size() != 1 || adaptor.getValue().empty())
      return failure();
    storeStatePlane(rewriter, op.getLoc(), adaptor.getDriver().front(),
                    adaptor.getValue().front(), "__obelisk_state_value",
                    layout.bitCount);
    if (adaptor.getValue().size() == 2)
      storeStatePlane(rewriter, op.getLoc(), adaptor.getDriver().front(),
                      adaptor.getValue()[1], "__obelisk_state_unknown",
                      layout.bitCount);
    Value changed =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI1Type(),
                                  rewriter.getBoolAttr(false));
    IntegerType i1 = rewriter.getI1Type();
    auto boolean = [&](bool value) {
      return arith::ConstantOp::create(rewriter, op.getLoc(), i1,
                                       rewriter.getBoolAttr(value));
    };
    std::optional<uint64_t> affectedNet;
    if (auto netID = op->getAttrOfType<IntegerAttr>("obelisk.native.net_id"))
      affectedNet = netID.getInt();
    for (const NativeStateLayout::Net &net : layout.netLayouts) {
      if (affectedNet && net.id != *affectedNet)
        continue;
      SmallVector<const NativeStateLayout::Driver *> netDrivers;
      for (const NativeStateLayout::Driver &driver : layout.driverLayouts)
        if (driver.netId == net.id)
          netDrivers.push_back(&driver);
      if (netDrivers.empty())
        continue;
      for (unsigned bit = 0; bit < net.width; ++bit) {
        Value resolvedValue = boolean(net.fourState);
        Value resolvedUnknown = boolean(net.fourState);
        for (const NativeStateLayout::Driver *driver : netDrivers) {
          Value handle = arith::ConstantOp::create(
              rewriter, op.getLoc(), rewriter.getI64Type(),
              rewriter.getI64IntegerAttr(encodeNativeStaticHandle(
                  driver->handleID, static_cast<int32_t>(bit))));
          Value driverValue = loadStatePlane(rewriter, op.getLoc(), handle, i1,
                                             "__obelisk_state_value", false,
                                             layout.bitCount);
          if (!net.fourState) {
            resolvedValue = driverValue;
            continue;
          }
          Value driverUnknown =
              loadStatePlane(rewriter, op.getLoc(), handle, i1,
                             "__obelisk_state_unknown", true,
                             layout.bitCount);
          Value currentZ = arith::AndIOp::create(
              rewriter, op.getLoc(), resolvedUnknown, resolvedValue);
          Value driverZ = arith::AndIOp::create(rewriter, op.getLoc(),
                                                driverUnknown, driverValue);
          Value currentX = arith::AndIOp::create(
              rewriter, op.getLoc(), resolvedUnknown,
              arith::XOrIOp::create(rewriter, op.getLoc(), resolvedValue,
                                    boolean(true)));
          Value driverX = arith::AndIOp::create(
              rewriter, op.getLoc(), driverUnknown,
              arith::XOrIOp::create(rewriter, op.getLoc(), driverValue,
                                    boolean(true)));
          Value conflict = arith::OrIOp::create(
              rewriter, op.getLoc(), currentX,
              arith::OrIOp::create(
                  rewriter, op.getLoc(), driverX,
                  arith::CmpIOp::create(rewriter, op.getLoc(),
                                        arith::CmpIPredicate::ne, resolvedValue,
                                        driverValue)));
          Value mergedValue = arith::SelectOp::create(
              rewriter, op.getLoc(), conflict, boolean(false), resolvedValue);
          Value mergedUnknown = arith::SelectOp::create(
              rewriter, op.getLoc(), conflict, boolean(true), boolean(false));
          Value withoutCurrentZ = arith::SelectOp::create(
              rewriter, op.getLoc(), driverZ, resolvedValue, mergedValue);
          Value withoutCurrentZUnknown = arith::SelectOp::create(
              rewriter, op.getLoc(), driverZ, resolvedUnknown, mergedUnknown);
          resolvedValue = arith::SelectOp::create(
              rewriter, op.getLoc(), currentZ, driverValue, withoutCurrentZ);
          resolvedUnknown =
              arith::SelectOp::create(rewriter, op.getLoc(), currentZ,
                                      driverUnknown, withoutCurrentZUnknown);
        }
        Value netHandle = arith::ConstantOp::create(
            rewriter, op.getLoc(), rewriter.getI64Type(),
            rewriter.getI64IntegerAttr(encodeNativeStaticHandle(
                net.handleID, static_cast<int32_t>(bit))));
        Value oldResolvedValue = loadStatePlane(
            rewriter, op.getLoc(), netHandle, i1, "__obelisk_state_value",
            false, layout.bitCount);
        Value oldResolvedUnknown;
        if (net.fourState)
          oldResolvedUnknown = loadStatePlane(
              rewriter, op.getLoc(), netHandle, i1,
              "__obelisk_state_unknown", true, layout.bitCount);
        changed = arith::OrIOp::create(
            rewriter, op.getLoc(), changed,
            storeStatePlane(rewriter, op.getLoc(), netHandle, resolvedValue,
                            "__obelisk_state_value", layout.bitCount));
        if (net.fourState)
          changed = arith::OrIOp::create(
              rewriter, op.getLoc(), changed,
              storeStatePlane(rewriter, op.getLoc(), netHandle, resolvedUnknown,
                              "__obelisk_state_unknown", layout.bitCount));
        notifySignal(rewriter, op.getLoc(), netHandle, 1, oldResolvedValue,
                     oldResolvedUnknown, resolvedValue,
                     net.fourState ? resolvedUnknown : Value{});
      }
    }
    (void)changed;
    rewriter.eraseOp(op);
    return success();
  }

private:
  const NativeStateLayout &layout;
};

template <typename Op>
class StaticHandleExtractConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Value offset =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(op.getLowBit()));
    rewriter.replaceOp(op, offsetNativeHandle(rewriter, op.getLoc(),
                                               adaptor.getInput().front(),
                                               offset));
    return success();
  }
};

template <typename Op>
class DynamicHandleExtractConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1 || adaptor.getLowBit().empty())
      return failure();
    Location location = op.getLoc();
    SignedI64Index convertedLow = resizeSignedIndexToI64(
        rewriter, location, adaptor.getLowBit().front());
    Value low = convertedLow.value;
    unsigned inputWidth =
        *sim::getPackedWidth(op.getInput().getType().getElementType());
    unsigned resultWidth =
        *sim::getPackedWidth(op.getResult().getType().getElementType());
    Value minimum = arith::ConstantOp::create(
        rewriter, location, rewriter.getI64Type(),
        rewriter.getI64IntegerAttr(-static_cast<int64_t>(resultWidth - 1)));
    Value maximum = arith::ConstantOp::create(
        rewriter, location, rewriter.getI64Type(),
        rewriter.getI64IntegerAttr(inputWidth - 1));
    Value overlapsLow = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::sge, low, minimum);
    Value inRange = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::sle, low, maximum);
    Value valid =
        arith::AndIOp::create(rewriter, location, overlapsLow, inRange);
    valid = arith::AndIOp::create(rewriter, location, valid,
                                  convertedLow.representable);
    valid = arith::AndIOp::create(
        rewriter, location, valid,
        isValidHandle(rewriter, location, adaptor.getInput().front()));
    if (adaptor.getLowBit().size() == 2) {
      Value known = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::eq, adaptor.getLowBit()[1],
          arith::ConstantOp::create(
              rewriter, location, adaptor.getLowBit()[1].getType(),
              rewriter.getZeroAttr(adaptor.getLowBit()[1].getType())));
      valid = arith::AndIOp::create(rewriter, location, valid, known);
    }
    Value selected = offsetNativeHandle(rewriter, location,
                                        adaptor.getInput().front(), low);
    Value invalid = arith::ConstantOp::create(
        rewriter, location, rewriter.getI64Type(),
        rewriter.getI64IntegerAttr(static_cast<int64_t>(UINT64_MAX)));
    rewriter.replaceOp(op, arith::SelectOp::create(rewriter, location, valid,
                                                   selected, invalid));
    return success();
  }
};

template <typename Op>
class SubelementHandleConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Type current = op.getInput().getType().getElementType();
    uint64_t offset = 0;
    for (int64_t rawIndex : op.getIndices()) {
      if (rawIndex < 0 || static_cast<uint64_t>(rawIndex) >=
                              sim::getAggregateNumElements(current))
        return failure();
      unsigned index = static_cast<unsigned>(rawIndex);
      auto subelement = sim::getAggregateProvenanceSubelement(current, index);
      if (!subelement ||
          subelement->first > std::numeric_limits<uint64_t>::max() - offset)
        return failure();
      offset += subelement->first;
      current = sim::getAggregateElementType(current, index);
    }
    Value amount =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(offset));
    rewriter.replaceOp(op, offsetNativeHandle(rewriter, op.getLoc(),
                                               adaptor.getInput().front(),
                                               amount));
    return success();
  }
};

template <typename Op>
class ArrayElementHandleConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1 || adaptor.getIndex().empty())
      return failure();
    Type array = op.getInput().getType().getElementType();
    int64_t left;
    int64_t right;
    bool packed;
    Type element;
    if (auto type = dyn_cast<sim::PackedArrayType>(array)) {
      left = type.getLeft();
      right = type.getRight();
      packed = true;
      element = type.getElementType();
    } else if (auto type = dyn_cast<sim::UnpackedArrayType>(array)) {
      left = type.getLeft();
      right = type.getRight();
      packed = false;
      element = type.getElementType();
    } else {
      return failure();
    }
    std::optional<uint64_t> span = sim::getProvenanceSpan(element);
    uint64_t count = sim::getAggregateNumElements(array);
    if (!span || count == 0)
      return failure();

    Location location = op.getLoc();
    IntegerType i64 = rewriter.getI64Type();
    SignedI64Index convertedIndex = resizeSignedIndexToI64(
        rewriter, location, adaptor.getIndex().front());
    Value index = convertedIndex.value;
    Value leftValue = arith::ConstantOp::create(
        rewriter, location, i64, rewriter.getI64IntegerAttr(left));
    Value rightValue = arith::ConstantOp::create(
        rewriter, location, i64, rewriter.getI64IntegerAttr(right));
    Value valid;
    Value ordinal;
    if (left >= right) {
      Value atMostLeft = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::sle, index, leftValue);
      Value atLeastRight = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::sge, index, rightValue);
      valid =
          arith::AndIOp::create(rewriter, location, atMostLeft, atLeastRight);
      ordinal = arith::SubIOp::create(rewriter, location, leftValue, index);
    } else {
      Value atLeastLeft = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::sge, index, leftValue);
      Value atMostRight = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::sle, index, rightValue);
      valid =
          arith::AndIOp::create(rewriter, location, atLeastLeft, atMostRight);
      ordinal = arith::SubIOp::create(rewriter, location, index, leftValue);
    }
    valid = arith::AndIOp::create(rewriter, location, valid,
                                  convertedIndex.representable);
    if (adaptor.getIndex().size() == 2) {
      Value known = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::eq, adaptor.getIndex()[1],
          arith::ConstantOp::create(
              rewriter, location, adaptor.getIndex()[1].getType(),
              rewriter.getZeroAttr(adaptor.getIndex()[1].getType())));
      valid = arith::AndIOp::create(rewriter, location, valid, known);
    }
    valid = arith::AndIOp::create(
        rewriter, location, valid,
        isValidHandle(rewriter, location, adaptor.getInput().front()));
    if (packed) {
      Value last = arith::ConstantOp::create(
          rewriter, location, i64,
          rewriter.getI64IntegerAttr(static_cast<int64_t>(count - 1)));
      ordinal = arith::SubIOp::create(rewriter, location, last, ordinal);
    }
    Value stride = arith::ConstantOp::create(rewriter, location, i64,
                                             rewriter.getI64IntegerAttr(*span));
    Value offset = arith::MulIOp::create(rewriter, location, ordinal, stride);
    Value selected = offsetNativeHandle(rewriter, location,
                                        adaptor.getInput().front(), offset);
    Value invalid = arith::ConstantOp::create(
        rewriter, location, i64,
        rewriter.getI64IntegerAttr(static_cast<int64_t>(UINT64_MAX)));
    rewriter.replaceOp(op, arith::SelectOp::create(rewriter, location, valid,
                                                   selected, invalid));
    return success();
  }
};

void emitNativeStateRelease(OpBuilder &builder, Location location,
                            Value handle, bool ownerReference);

bool isReferenceView(Operation *operation) {
  return isa<sim::SimRefExtractOp, sim::SimRefDynExtractOp,
             sim::SimRefSubelementOp, sim::SimRefArrayElementOp>(operation);
}

llvm::SetVector<Value> collectReferenceFamily(Value root) {
  llvm::SetVector<Value> family;
  family.insert(root);
  for (size_t index = 0; index != family.size(); ++index) {
    Value reference = family[index];
    for (OpOperand &use : reference.getUses()) {
      Operation *user = use.getOwner();
      if (isReferenceView(user)) {
        for (Value result : user->getResults())
          if (isa<sim::RefType>(result.getType()))
            family.insert(result);
      }
      auto branch = dyn_cast<BranchOpInterface>(user);
      if (!branch)
        continue;
      for (unsigned successorIndex = 0,
                    end = user->getNumSuccessors();
           successorIndex != end; ++successorIndex) {
        Block *successor = user->getSuccessor(successorIndex);
        SuccessorOperands successorOperands =
            branch.getSuccessorOperands(successorIndex);
        for (unsigned argumentIndex =
                          successorOperands.getProducedOperandCount(),
                      argumentEnd = successorOperands.size();
             argumentIndex != argumentEnd; ++argumentIndex)
          if (successorOperands[argumentIndex] == reference)
            family.insert(successor->getArgument(argumentIndex));
      }
    }
  }
  return family;
}

void insertAutomaticOwnerReleaseMarker(OpBuilder &builder, Location location,
                                       Value handle) {
  func::CallOp::create(builder, location, kAutomaticOwnerReleaseMarker,
                       TypeRange{}, ValueRange{handle});
}

LogicalResult insertAutomaticOwnerReleases(sim::SimFuncOp function) {
  SmallVector<sim::SimRefAllocOp> allocations;
  function.walk([&](sim::SimRefAllocOp allocation) {
    allocations.push_back(allocation);
  });
  if (allocations.empty())
    return success();

  for (sim::SimRefAllocOp allocation : allocations) {
    llvm::SetVector<Value> family =
        collectReferenceFamily(allocation.getResult());

    // Reference ownership is represented by the allocation rather than by an
    // SSA value.  A block argument fed by more than one ownership family would
    // therefore make the release below path-dependent: independently
    // instrumenting both families could release the selected handle twice (or
    // release a borrowed handle).  Reject such merges until ownership tokens
    // are represented explicitly in SSA.
    for (Value reference : family) {
      auto argument = dyn_cast<BlockArgument>(reference);
      if (!argument)
        continue;
      Block *owner = argument.getOwner();
      unsigned argumentIndex = argument.getArgNumber();
      for (Block &predecessor : function.getBody()) {
        Operation *terminator = predecessor.getTerminator();
        for (auto [successorIndex, successor] :
             llvm::enumerate(predecessor.getSuccessors())) {
          if (successor != owner)
            continue;
          auto branch = dyn_cast<BranchOpInterface>(terminator);
          if (!branch)
            return allocation.emitError(
                "automatic reference block argument has an unsupported "
                "incoming edge");
          SuccessorOperands operands =
              branch.getSuccessorOperands(successorIndex);
          if (argumentIndex >= operands.size() ||
              operands.isOperandProduced(argumentIndex) ||
              !family.contains(operands[argumentIndex]))
            return allocation.emitError(
                "automatic reference block argument merges distinct "
                "ownership origins");
        }
      }
    }

    // Earlier allocations may have split lifetime-exit edges, so recompute
    // dominance for the current CFG rather than retaining a stale analysis.
    DominanceInfo dominance(function);
    Liveness liveness(function);
    auto isLiveInto = [&](Block *block) {
      for (Value reference : family) {
        if (liveness.getLiveIn(block).contains(reference))
          return true;
        if (auto argument = dyn_cast<BlockArgument>(reference);
            argument && argument.getOwner() == block && !argument.use_empty())
          return true;
      }
      return false;
    };
    auto representativeAt = [&](Operation *operation) -> Value {
      for (Value reference : family)
        if (dominance.dominates(reference, operation))
          return reference;
      return {};
    };

    llvm::DenseSet<Block *> activeBlocks;
    SmallVector<Block *> worklist{allocation->getBlock()};
    while (!worklist.empty()) {
      Block *block = worklist.pop_back_val();
      if (!activeBlocks.insert(block).second)
        continue;
      Operation *terminator = block->getTerminator();
      Value representative = representativeAt(terminator);
      if (!representative)
        return allocation.emitError(
            "cannot identify an automatic reference on a CFG lifetime exit");

      SmallVector<bool> liveEdges;
      liveEdges.reserve(terminator->getNumSuccessors());
      bool anyLive = false;
      for (Block *successor : terminator->getSuccessors()) {
        bool live = isLiveInto(successor);
        liveEdges.push_back(live);
        anyLive |= live;
        if (live)
          worklist.push_back(successor);
      }

      OpBuilder builder(terminator);
      if (!anyLive) {
        insertAutomaticOwnerReleaseMarker(builder, allocation.getLoc(),
                                          representative);
        continue;
      }
      if (llvm::all_of(liveEdges, [](bool live) { return live; }))
        continue;

      auto branch = dyn_cast<BranchOpInterface>(terminator);
      if (!branch)
        return allocation.emitError(
            "cannot split an automatic-reference lifetime exit edge");
      for (unsigned successorIndex = 0,
                    end = terminator->getNumSuccessors();
           successorIndex != end; ++successorIndex) {
        if (liveEdges[successorIndex])
          continue;
        Block *destination = terminator->getSuccessor(successorIndex);
        SuccessorOperands successorOperands =
            branch.getSuccessorOperands(successorIndex);
        if (successorOperands.getProducedOperandCount() != 0)
          return allocation.emitError(
              "cannot split a produced automatic-reference CFG edge");
        SmallVector<Value> forwarded(
            successorOperands.getForwardedOperands().begin(),
            successorOperands.getForwardedOperands().end());
        successorOperands.getMutableForwardedOperands().append(representative);

        auto *cleanup = new Block;
        function.getBody().push_back(cleanup);
        for (Value value : forwarded)
          cleanup->addArgument(value.getType(), terminator->getLoc());
        BlockArgument cleanupHandle = cleanup->addArgument(
            representative.getType(), terminator->getLoc());
        terminator->setSuccessor(cleanup, successorIndex);

        OpBuilder cleanupBuilder(cleanup, cleanup->end());
        insertAutomaticOwnerReleaseMarker(
            cleanupBuilder, allocation.getLoc(), cleanupHandle);
        cf::BranchOp::create(cleanupBuilder, terminator->getLoc(), destination,
                             cleanup->getArguments().drop_back());
      }
    }
    allocation->setAttr("obelisk.owner_release_instrumented",
                        UnitAttr::get(function.getContext()));
  }
  return success();
}

class RefAllocConversion final
    : public OpConversionPattern<sim::SimRefAllocOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimRefAllocOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInitialValue().empty())
      return failure();
    std::optional<unsigned> width =
        nativeStateWidth(op.getInitialValue().getType());
    if (!width || *width == 0)
      return failure();
    Location location = op.getLoc();
    if (!op->hasAttr("obelisk.owner_release_instrumented"))
      return op.emitError("automatic reference lifetime was not instrumented");
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Value one = llvmConstant(rewriter, location, i64, 1);
    auto savePlane = [&](Value value) {
      Value address = LLVM::AllocaOp::create(rewriter, location, pointer,
                                             value.getType(), one, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    Value value = savePlane(adaptor.getInitialValue().front());
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getInitialValue().size() == 2)
      unknown = savePlane(adaptor.getInitialValue()[1]);
    Value outHandle = LLVM::AllocaOp::create(rewriter, location, pointer, i64,
                                            one, 8);
    Value invalid = llvmConstant(rewriter, location, i64, UINT64_MAX);
    LLVM::StoreOp::create(rewriter, location, invalid, outHandle, 8);
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    LLVM::CallOp::create(
        rewriter, location, TypeRange{i32},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_native_state_alloc"),
        ValueRange{context, llvmConstant(rewriter, location, i64, *width),
                   value, unknown, outHandle});
    Value handle = LLVM::LoadOp::create(rewriter, location, i64, outHandle, 8);
    rewriter.replaceOp(op, handle);
    return success();
  }
};

using ReferenceArgumentMap =
    llvm::DenseMap<Operation *, SmallVector<unsigned>>;

void emitNativeStateRelease(OpBuilder &builder, Location location,
                            Value handle, bool ownerReference) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Value contextAddress = LLVM::AddressOfOp::create(
      builder, location, pointer, "__obelisk_current_context");
  Value context =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  Value owner = arith::ConstantOp::create(
      builder, location, i32,
      builder.getI32IntegerAttr(ownerReference ? 1 : 0));
  Value status =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(builder.getContext(),
                             "obelisk_rt_v1_native_state_release"),
          ValueRange{context, handle, owner})
          .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(builder.getContext(),
                         "obelisk_rt_v1_scheduler_fail"),
      ValueRange{context, status});
}

class AutomaticOwnerReleaseMarkerConversion final
    : public OpConversionPattern<func::CallOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(func::CallOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (op.getCallee() != kAutomaticOwnerReleaseMarker)
      return failure();
    if (adaptor.getOperands().size() != 1)
      return op.emitError("malformed automatic owner release marker");
    emitNativeStateRelease(rewriter, op.getLoc(), adaptor.getOperands().front(),
                           true);
    rewriter.eraseOp(op);
    return success();
  }
};

LogicalResult releaseNativeAutomaticState(
    ModuleOp module, const ReferenceArgumentMap &referenceArguments) {
  SmallVector<sim::SimFuncOp> functions;
  module.walk([&](sim::SimFuncOp function) { functions.push_back(function); });
  for (sim::SimFuncOp function : functions) {
    auto arguments = referenceArguments.find(function.getOperation());
    if (arguments == referenceArguments.end())
      continue;
    if (function.getBody().empty())
      continue;
    SmallVector<sim::SimReturnOp> returns;
    function.walk(
        [&](sim::SimReturnOp operation) { returns.push_back(operation); });
    for (sim::SimReturnOp operation : returns) {
      OpBuilder builder(operation);
      for (unsigned index : arguments->second) {
        if (index >= function.getBody().front().getNumArguments())
          return function.emitError(
              "converted automatic-reference argument index is invalid");
        emitNativeStateRelease(builder, operation.getLoc(),
                               function.getBody().front().getArgument(index),
                               false);
      }
    }
  }
  return success();
}

class ImmediateNBAConversion final
    : public OpConversionPattern<sim::SimNBAEnqueueOp> {
public:
  ImmediateNBAConversion(const TypeConverter &converter, MLIRContext *context,
                         uint64_t stateBitCount)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount) {}
  LogicalResult
  matchAndRewrite(sim::SimNBAEnqueueOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDestination().size() != 1 || adaptor.getValue().empty())
      return failure();
    std::optional<unsigned> width = nativeStateWidth(op.getValue().getType());
    if (!width)
      return failure();
    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Value one = llvmConstant(rewriter, location, i64, 1);
    auto savePlane = [&](Value value) {
      Value address = LLVM::AllocaOp::create(rewriter, location, pointer,
                                             value.getType(), one, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    Value value = savePlane(adaptor.getValue().front());
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    Value unknownPlane = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknown = savePlane(adaptor.getValue()[1]);
      unknownPlane = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                               "__obelisk_state_unknown");
    }
    Value valuePlane = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                 "__obelisk_state_value");
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value runtimeContext =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    Value delay = adaptor.getDelay().empty()
                      ? llvmConstant(rewriter, location, i64, 0)
                      : adaptor.getDelay().front();
    LLVM::CallOp::create(
        rewriter, location, TypeRange{i32},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_scheduler_nba"),
        ValueRange{runtimeContext, valuePlane, unknownPlane,
                   llvmConstant(rewriter, location, i64, stateBitCount),
                   adaptor.getDestination().front(),
                   llvmConstant(rewriter, location, i64, *width), delay, value,
                   unknown});
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
};

class SpawnTypeConversion final : public OpConversionPattern<sim::SimSpawnOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimSpawnOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    for (auto [operand, converted] :
         llvm::zip_equal(op.getOperands(), adaptor.getOperands()))
      if (isa<sim::RefType>(operand.getType()) && converted.size() == 1)
        emitNativeStateRetain(rewriter, op.getLoc(), converted.front());
    OperationState state(op.getLoc(), op->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addTypes(rewriter.getI64Type());
    state.addAttributes(op->getAttrs());
    Operation *replacement = rewriter.create(state);
    rewriter.replaceOp(op, replacement->getResults());
    return success();
  }
};

class SchedulerEffectEraseConversion final : public ConversionPattern {
public:
  SchedulerEffectEraseConversion(const TypeConverter &converter,
                                 StringRef operation, MLIRContext *context)
      : ConversionPattern(converter, operation, 1, context) {}
  LogicalResult
  matchAndRewrite(Operation *operation, ArrayRef<Value>,
                  ConversionPatternRewriter &rewriter) const override {
    if (operation->getNumResults() == 0) {
      rewriter.eraseOp(operation);
      return success();
    }
    Value zero = arith::ConstantOp::create(rewriter, operation->getLoc(),
                                           rewriter.getI64Type(),
                                           rewriter.getI64IntegerAttr(0));
    rewriter.replaceOp(operation, zero);
    return success();
  }
};

LLVM::LLVMFuncOp getOrDeclareLLVMFunction(ModuleOp module, StringRef name,
                                          Type result,
                                          ArrayRef<Type> arguments) {
  if (auto existing = module.lookupSymbol<LLVM::LLVMFuncOp>(name))
    return existing;
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  return LLVM::LLVMFuncOp::create(
      builder, module.getLoc(), name,
      LLVM::LLVMFunctionType::get(result, arguments, false));
}

void makeCurrentContextGlobal(ModuleOp module) {
  if (module.lookupSymbol("__obelisk_current_context"))
    return;
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  Type pointer = LLVM::LLVMPointerType::get(module.getContext());
  auto global = LLVM::GlobalOp::create(
      builder, module.getLoc(), pointer, false, LLVM::Linkage::Internal,
      "__obelisk_current_context", Attribute{}, 8);
  Block *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToStart(block);
  LLVM::ReturnOp::create(
      builder, module.getLoc(),
      LLVM::ZeroOp::create(builder, module.getLoc(), pointer));
}

void notifySignal(OpBuilder &builder, Location location, Value handle,
                  uint64_t width, Value oldValue, Value oldUnknown,
                  Value newValue, Value newUnknown) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i64 = builder.getI64Type();
  Value address = LLVM::AddressOfOp::create(builder, location, pointer,
                                            "__obelisk_current_context");
  Value context = LLVM::LoadOp::create(builder, location, pointer, address, 8);
  Value one = llvmConstant(builder, location, i64, 1);
  auto save = [&](Value value) {
    if (!value)
      return LLVM::ZeroOp::create(builder, location, pointer).getResult();
    Value storage = LLVM::AllocaOp::create(builder, location, pointer,
                                           value.getType(), one, 1);
    LLVM::StoreOp::create(builder, location, value, storage, 1);
    return storage;
  };
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(builder.getContext(),
                         "obelisk_rt_v1_scheduler_signal_transition"),
      ValueRange{context, handle,
                 llvmConstant(builder, location, i64, width), save(oldValue),
                 save(oldUnknown), save(newValue), save(newUnknown)});
}

class EventTriggerConversion final
    : public OpConversionPattern<sim::SimEventTriggerOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimEventTriggerOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getEvent().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value address = LLVM::AddressOfOp::create(
        rewriter, op.getLoc(), pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, address, 8);
    LLVM::CallOp::create(
        rewriter, op.getLoc(), TypeRange{},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_scheduler_event"),
        ValueRange{context, adaptor.getEvent().front(),
                   llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(),
                                op.getNonblocking() ? 1 : 0)});
    rewriter.eraseOp(op);
    return success();
  }
};

LogicalResult
makeProcessSpawnHelper(ModuleOp module, sim::SimFuncOp function,
                       const SimulationProcessFrameAnalysis &analysis,
                       const NativeSchedulePlan &schedule) {
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = function.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  SmallVector<Type> arguments;
  for (BlockArgument argument : function.getBody().front().getArguments())
    arguments.push_back(convertProcessType(argument.getType(), context));
  std::string helperName = (function.getSymName() + ".__obelisk_spawn").str();
  if (module.lookupSymbol(helperName))
    return success();
  std::string continuationName =
      (function.getSymName() + ".__obelisk_schedule_continuations").str();
  std::string rankName =
      (function.getSymName() + ".__obelisk_schedule_ranks").str();
  if (!schedule.continuations.empty()) {
    auto arrayType =
        LLVM::LLVMArrayType::get(i32, schedule.continuations.size());
    auto makeArray = [&](StringRef name, unsigned element) {
      makeConstantGlobal(
          module, location, arrayType, name, LLVM::Linkage::Internal, 4,
          [&](OpBuilder &initializer) {
            Value array =
                LLVM::ZeroOp::create(initializer, location, arrayType);
            for (auto [index, continuation] :
                 llvm::enumerate(schedule.continuations))
              array = LLVM::InsertValueOp::create(
                  initializer, location, array,
                  llvmConstant(initializer, location, i32,
                               element == 0 ? continuation.first
                                            : continuation.second),
                  ArrayRef<int64_t>{static_cast<int64_t>(index)});
            return array;
          });
    };
    makeArray(continuationName, 0);
    makeArray(rankName, 1);
  }
  builder.setInsertionPointAfter(function);
  auto helper = LLVM::LLVMFuncOp::create(
      builder, location, helperName,
      LLVM::LLVMFunctionType::get(i64, arguments, false));
  Block *entry = helper.addEntryBlock(builder);
  Block *created = new Block;
  Block *createFailed = new Block;
  Block *added = new Block;
  Block *addFailed = new Block;
  helper.getBody().push_back(created);
  helper.getBody().push_back(createFailed);
  helper.getBody().push_back(added);
  helper.getBody().push_back(addFailed);
  builder.setInsertionPointToStart(entry);
  Value one = llvmConstant(builder, location, i64, 1);
  Value outInstance =
      LLVM::AllocaOp::create(builder, location, pointer, pointer, one, 8);
  LLVM::StoreOp::create(builder, location,
                        LLVM::ZeroOp::create(builder, location, pointer),
                        outInstance, 8);
  Value descriptor = LLVM::AddressOfOp::create(
      builder, location, pointer,
      (function.getSymName() + ".__obelisk_process_descriptor").str());
  auto create = LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, "obelisk_rt_v1_process_instance_create"),
      ValueRange{descriptor, outInstance});
  Value createSucceeded = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, create.getResult(),
      llvmConstant(builder, location, i32, 0));
  LLVM::CondBrOp::create(builder, location, createSucceeded, created,
                         createFailed);

  builder.setInsertionPointToStart(createFailed);
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
      ValueRange{entry->getArgument(0), create.getResult()});
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i64, 0));

  builder.setInsertionPointToStart(created);
  Value instance =
      LLVM::LoadOp::create(builder, location, pointer, outInstance, 8);
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
  size_t physicalArgument = 0;
  for (const ProcessFrameValue &slot : analysis.getEntryCaptureLayout()) {
    if (slot.valueOffset == kNoOffset) {
      ++physicalArgument;
      continue;
    }
    if (physicalArgument >= entry->getNumArguments())
      return helper.emitError("spawn capture layout has too few arguments");
    storeAt(builder, location, frame, slot.valueOffset,
            entry->getArgument(physicalArgument++), slot.alignment);
    if (slot.isFourState()) {
      if (physicalArgument >= entry->getNumArguments())
        return helper.emitError(
            "spawn four-state capture is missing its unknown plane");
      storeAt(builder, location, frame, slot.unknownOffset,
              entry->getArgument(physicalArgument++), slot.alignment);
    }
  }
  if (physicalArgument != entry->getNumArguments())
    return helper.emitError("spawn capture layout has excess arguments");
  uint32_t phase = function.getEntryKind() == sim::EntryKind::Final ? 1 : 0;
  Value null = LLVM::ZeroOp::create(builder, location, pointer);
  Value continuationAddress = null;
  Value rankAddress = null;
  if (!schedule.continuations.empty()) {
    continuationAddress = LLVM::AddressOfOp::create(
        builder, location, pointer, continuationName);
    rankAddress =
        LLVM::AddressOfOp::create(builder, location, pointer, rankName);
  }
  auto add = LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_add_planned"),
      ValueRange{entry->getArgument(0), instance,
                 llvmConstant(builder, location, i32, phase),
                 llvmConstant(builder, location, i32, schedule.initialRank),
                 continuationAddress, rankAddress,
                 llvmConstant(builder, location, i32,
                              schedule.continuations.size())});
  Value addSucceeded = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, add.getResult(),
      llvmConstant(builder, location, i32, 0));
  LLVM::CondBrOp::create(builder, location, addSucceeded, added, addFailed);

  builder.setInsertionPointToStart(addFailed);
  LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, "obelisk_rt_v1_process_instance_destroy"),
      instance);
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
      ValueRange{entry->getArgument(0), add.getResult()});
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i64, 0));

  builder.setInsertionPointToStart(added);
  Value token = LLVM::CallOp::create(
                    builder, location, TypeRange{i64},
                    SymbolRefAttr::get(
                        context, "obelisk_rt_v1_scheduler_process_token"),
                    ValueRange{entry->getArgument(0), instance})
                    .getResult();
  LLVM::ReturnOp::create(builder, location, token);

  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_instance_create", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_add_planned", i32,
                           {pointer, pointer, i32, i32, pointer, pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_process_token", i64,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_fail",
                           LLVM::LLVMVoidType::get(context), {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_instance_destroy",
                           i32, {pointer});
  return success();
}

LogicalResult makeSchedulerMain(ModuleOp module,
                                const NativeStateLayout &stateLayout) {
  if (module.lookupSymbol("main"))
    return success();
  sim::SimFuncOp root;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getSymName() == "__obelisk_root")
      root = function;
  });
  if (!root)
    return success();
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  builder.setInsertionPointToEnd(module.getBody());
  Location location = module.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Type voidType = LLVM::LLVMVoidType::get(context);
  auto main = LLVM::LLVMFuncOp::create(
      builder, location, "main", LLVM::LLVMFunctionType::get(i32, {}, false));
  Block *entry = main.addEntryBlock(builder);
  Block *ready = new Block;
  Block *failed = new Block;
  main.getBody().push_back(ready);
  main.getBody().push_back(failed);
  builder.setInsertionPointToStart(entry);
  Value one = llvmConstant(builder, location, i64, 1);
  Value outContext =
      LLVM::AllocaOp::create(builder, location, pointer, pointer, one, 8);
  LLVM::StoreOp::create(builder, location,
                        LLVM::ZeroOp::create(builder, location, pointer),
                        outContext, 8);
  constexpr StringLiteral executionName = "__obelisk_execution_descriptor_v1";
  bool hasExecution = module.lookupSymbol(executionName) != nullptr;
  if (hasExecution) {
    Value execution =
        LLVM::AddressOfOp::create(builder, location, pointer, executionName);
    auto create = LLVM::CallOp::create(
        builder, location, TypeRange{i32},
        SymbolRefAttr::get(context, "obelisk_rt_v1_context_create_for_design"),
        ValueRange{execution, outContext});
    Value succeeded = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, create.getResult(),
        llvmConstant(builder, location, i32, 0));
    LLVM::CondBrOp::create(builder, location, succeeded, ready, failed,
                           create.getResult());
  } else {
    auto create = LLVM::CallOp::create(
        builder, location, TypeRange{i32},
        SymbolRefAttr::get(context, "obelisk_rt_v1_context_create"),
        outContext);
    Value succeeded = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, create.getResult(),
        llvmConstant(builder, location, i32, 0));
    LLVM::CondBrOp::create(builder, location, succeeded, ready, failed,
                           create.getResult());
  }
  failed->addArgument(i32, location);
  builder.setInsertionPointToStart(failed);
  LLVM::ReturnOp::create(builder, location, failed->getArgument(0));

  builder.setInsertionPointToStart(ready);
  Value runtimeContext =
      LLVM::LoadOp::create(builder, location, pointer, outContext, 8);
  Value currentAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  LLVM::StoreOp::create(builder, location, runtimeContext, currentAddress, 8);
  for (const NativeStateLayout::Bound &bound : stateLayout.bounds) {
    auto status = LLVM::CallOp::create(
        builder, location, TypeRange{i32},
        SymbolRefAttr::get(context,
                           "obelisk_rt_v1_native_state_register_static"),
        ValueRange{runtimeContext,
                   llvmConstant(builder, location, i32, bound.handleID),
                   llvmConstant(builder, location, i64, bound.offset),
                   llvmConstant(builder, location, i64, bound.width)});
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, status.getResult()});
  }
  LLVM::CallOp::create(
      builder, location, TypeRange{i64},
      SymbolRefAttr::get(context, "__obelisk_root.__obelisk_spawn"),
      runtimeContext);
  auto run = LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_run"),
      runtimeContext);
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_context_destroy"),
      runtimeContext);
  LLVM::ReturnOp::create(builder, location, run.getResult());

  if (hasExecution)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_context_create_for_design",
                             i32, {pointer, pointer});
  else
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_context_create", i32,
                             {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_context_destroy", voidType,
                           {pointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_register_static", i32,
      {pointer, i32, i64, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_fail", voidType,
                           {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run", i32,
                           {pointer});
  return success();
}

LogicalResult prepareSimulationProcessesForLLVMCoroutinesImpl(
    ModuleOp module, const llvm::DataLayout &dataLayout) {
  MLIRContext *context = module.getContext();
  FailureOr<NativeStateLayout> stateLayout = buildNativeStateLayout(module);
  if (failed(stateLayout))
    return failure();
  NativeScheduleRanks scheduleRanks = buildNativeScheduleRanks(module);
  uint64_t stateBytes = (stateLayout->bitCount + 7) / 8;
  makeStatePlane(module, "__obelisk_state_value", stateBytes, false,
                 stateLayout->driverLayouts, stateLayout->netLayouts);
  makeStatePlane(module, "__obelisk_state_unknown", stateBytes, true);
  makeCurrentContextGlobal(module);
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_signal",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_signal_transition",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_event",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_handle_offset", IntegerType::get(context, 64),
      {IntegerType::get(context, 64), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_nba", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_alloc", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_retain", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_release", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_load_plane",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_store_plane",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  llvm::MapVector<Operation *, std::unique_ptr<SimulationProcessFrameAnalysis>>
      analyses;
  WalkResult analyzed = module.walk([&](sim::SimFuncOp function) {
    bool suspendable = false;
    function.walk(
        [&](Operation *operation) { suspendable |= isSuspension(operation); });
    bool process = function.getEntryKind() != sim::EntryKind::Function;
    if (suspendable && failed(threadProcessStateThroughCFG(function)))
      return WalkResult::interrupt();
    if (failed(insertAutomaticOwnerReleases(function)))
      return WalkResult::interrupt();
    if (!suspendable && !process)
      return WalkResult::advance();
    auto analysis =
        SimulationProcessFrameAnalysis::create(function, dataLayout);
    if (failed(analysis))
      return WalkResult::interrupt();
    for (const ProcessSuspension &suspension : (*analysis)->getSuspensions()) {
      suspension.operation->setAttr(
          "obelisk.coro.continuation",
          IntegerAttr::get(IntegerType::get(context, 32),
                           suspension.continuationID));
      suspension.operation->setAttr(
          "obelisk.coro.wait_offset",
          IntegerAttr::get(IntegerType::get(context, 64),
                           suspension.waitOffset));
      suspension.operation->setAttr(
          "obelisk.coro.wait_size",
          IntegerAttr::get(IntegerType::get(context, 64), suspension.waitSize));
    }
    analyses.insert({function.getOperation(), std::move(*analysis)});
    return WalkResult::advance();
  });
  if (analyzed.wasInterrupted())
    return failure();

  // Record the net driven by each operation before dialect conversion starts
  // rewriting function signatures and their block arguments.  Conversion
  // patterns should inspect stable operation metadata instead of chasing the
  // source SSA graph while it is being replaced.
  module.walk([&](sim::SimDriverDriveOp drive) {
    std::optional<uint64_t> driverID = getStaticDriverID(drive.getDriver());
    if (!driverID)
      return;
    for (const NativeStateLayout::Driver &driver :
         stateLayout->driverLayouts) {
      if (driver.id != *driverID)
        continue;
      drive->setAttr("obelisk.native.net_id",
                     IntegerAttr::get(IntegerType::get(context, 64),
                                      driver.netId));
      return;
    }
  });

  SimulationToStandardTypeConverter packedConverter;
  addSimulationPackedAggregateTypeConversions(packedConverter);
  packedConverter.addConversion(
      [](sim::UnpackedArrayType type, SmallVectorImpl<Type> &results) {
        return convertNativeAggregateType(type, results);
      });
  packedConverter.addConversion(
      [](sim::UnpackedStructType type, SmallVectorImpl<Type> &results) {
        return convertNativeAggregateType(type, results);
      });
  packedConverter.addConversion(
      [](sim::UnpackedUnionType type, SmallVectorImpl<Type> &results) {
        return convertNativeAggregateType(type, results);
      });
  addSimulationToRuntimeTypeConversions(packedConverter);
  packedConverter.addConversion([context](Type type) -> std::optional<Type> {
    if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
            sim::ProcessType>(type))
      return IntegerType::get(context, 64);
    return std::nullopt;
  });
  ReferenceArgumentMap referenceArguments;
  WalkResult lifetimeInputs = module.walk([&](sim::SimFuncOp function) {
    if (function.getBody().empty())
      return WalkResult::advance();
    unsigned physical = 0;
    for (BlockArgument argument : function.getBody().front().getArguments()) {
      SmallVector<Type> converted;
      if (failed(packedConverter.convertType(argument.getType(), converted)))
        return WalkResult::interrupt();
      if (isa<sim::RefType>(argument.getType())) {
        if (converted.size() != 1)
          return WalkResult::interrupt();
        referenceArguments[function.getOperation()].push_back(physical);
      }
      physical += converted.size();
    }
    return WalkResult::advance();
  });
  if (lifetimeInputs.wasInterrupted())
    return failure();
  RewritePatternSet packedPatterns(context);
  populateSimulationToStandardPatterns(packedConverter, packedPatterns);
  populateSimulationPackedAggregateViewPatterns(packedConverter,
                                                packedPatterns);
  populateSimulationToRuntimePatterns(packedConverter, packedPatterns);
  packedPatterns.add<SimFuncSignatureConversion, SimReturnTypeConversion,
                     SimCallTypeConversion,
                     AutomaticOwnerReleaseMarkerConversion,
                     PackedAggregateExtractConversion,
                     PackedAggregateInsertConversion,
                     PackedAggregateConstructConversion,
                     AggregateDynamicExtractConversion,
                     AggregateDefaultConversion, UnionConstructConversion,
                     UnionExtractConversion,
                     SimSuspendTypeConversion<sim::SimSuspendDelayOp>,
                     SimSuspendTypeConversion<sim::SimSuspendChangeOp>,
                     SimSuspendTypeConversion<sim::SimSuspendEdgeOp>,
                     SimSuspendTypeConversion<sim::SimSuspendAnyOp>,
                     SimSuspendTypeConversion<sim::SimSuspendEventOp>,
                     SimSuspendTypeConversion<sim::SimSuspendAwaitOp>,
                     SimSuspendTypeConversion<sim::SimSuspendJoinOp>>(
      packedConverter, context);
  packedPatterns.add<ContextHandleConversion<sim::SimContextStorageOp>>(
      packedConverter, context, stateLayout->storage);
  packedPatterns.add<ContextHandleConversion<sim::SimContextNetOp>>(
      packedConverter, context, stateLayout->nets);
  packedPatterns.add<ContextHandleConversion<sim::SimContextDriverOp>>(
      packedConverter, context, stateLayout->drivers);
  packedPatterns.add<EventHandleConversion,
                     StaticHandleExtractConversion<sim::SimRefExtractOp>,
                     StaticHandleExtractConversion<sim::SimDriverExtractOp>,
                     DynamicHandleExtractConversion<sim::SimRefDynExtractOp>,
                     DynamicHandleExtractConversion<sim::SimDriverDynExtractOp>,
                     SubelementHandleConversion<sim::SimRefSubelementOp>,
                     SubelementHandleConversion<sim::SimDriverSubelementOp>,
                     ArrayElementHandleConversion<sim::SimRefArrayElementOp>,
                     ArrayElementHandleConversion<sim::SimDriverArrayElementOp>,
                     EventTriggerConversion, SpawnTypeConversion>(
      packedConverter, context);
  packedPatterns.add<RefLoadConversion, RefStoreConversion, NetReadConversion>(
      packedConverter, context, stateLayout->bitCount);
  packedPatterns.add<DriverDriveConversion>(packedConverter, context,
                                            *stateLayout);
  packedPatterns.add<ImmediateNBAConversion>(packedConverter, context,
                                             stateLayout->bitCount);
  packedPatterns.add<RefAllocConversion>(packedConverter, context);
  ConversionTarget packedTarget(*context);
  packedTarget.addIllegalOp<
      sim::SimBytesConstantOp, sim::SimDisplayOp, sim::SimFileOpenMCDOp,
      sim::SimFileOpenOp, sim::SimFileCloseOp, sim::SimFileFlushOp,
      sim::SimFileGetcOp, sim::SimFileUngetcOp, sim::SimFileGetlineOp,
      sim::SimFileReadPackedOp, sim::SimFileEofOp, sim::SimFileSeekOp,
      sim::SimFileTellOp, sim::SimFileRewindOp>();
  packedTarget.addIllegalOp<
      sim::SimContextStorageOp, sim::SimContextNetOp, sim::SimContextDriverOp,
      sim::SimContextEventOp, sim::SimRefAllocOp, sim::SimRefLoadOp,
      sim::SimRefStoreOp, sim::SimRefExtractOp, sim::SimRefDynExtractOp,
      sim::SimRefSubelementOp, sim::SimRefArrayElementOp, sim::SimNetReadOp,
      sim::SimDriverDriveOp, sim::SimDriverExtractOp,
      sim::SimDriverDynExtractOp, sim::SimDriverSubelementOp,
      sim::SimDriverArrayElementOp, sim::SimNBAEnqueueOp,
      sim::SimEventTriggerOp, sim::SimBitsDynExtractOp>();
  packedTarget
      .addIllegalOp<sim::SimAggregateDefaultOp, sim::SimAggregateConstructOp,
                    sim::SimAggregateExtractOp, sim::SimAggregateInsertOp,
                    sim::SimArrayDynExtractOp, sim::SimUnionConstructOp,
                    sim::SimUnionExtractOp>();
  packedTarget.addLegalDialect<runtime::ObeliskRuntimeDialect>();
  packedTarget.addLegalOp<sim::SimContextRuntimeOp, sim::SimStatusCheckOp>();
  packedTarget.addDynamicallyLegalOp<sim::SimFuncOp>(
      [&](sim::SimFuncOp function) {
        return packedConverter.isSignatureLegal(function.getFunctionType()) &&
               packedConverter.isLegal(&function.getBody());
      });
  packedTarget.addDynamicallyLegalOp<
      sim::SimCallOp, sim::SimSpawnOp, sim::SimReturnOp,
      sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp,
      sim::SimSuspendDelayOp, sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
      sim::SimSuspendAnyOp, sim::SimSuspendEventOp, sim::SimSuspendAwaitOp,
      sim::SimSuspendJoinOp>(
      [&](Operation *operation) { return packedConverter.isLegal(operation); });
  packedTarget.addDynamicallyLegalDialect<
      sim::ObeliskSimulationDialect, arith::ArithDialect,
      cf::ControlFlowDialect, func::FuncDialect>(hasNoLogic);
  packedTarget.addDynamicallyLegalOp<func::CallOp>([&](func::CallOp call) {
    return call.getCallee() != kAutomaticOwnerReleaseMarker &&
           hasNoLogic(call);
  });
  packedTarget.addDynamicallyLegalOp<cf::BranchOp, cf::CondBranchOp>(
      [&](Operation *operation) { return packedConverter.isLegal(operation); });
  packedTarget.addDynamicallyLegalOp<ModuleOp>(hasNoLogic);
  packedTarget.markUnknownOpDynamicallyLegal(hasNoLogic);
  if (failed(applyFullConversion(module, packedTarget,
                                 std::move(packedPatterns))) ||
      failed(threadRuntimeStatuses(module)) ||
      failed(releaseNativeAutomaticState(module, referenceArguments)))
    return failure();
  if (failed(validateRuntimeToLLVMPreconditions(module, dataLayout)))
    return failure();

  for (auto &entry : analyses) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    if (!function)
      return failure();
    NativeSchedulePlan schedule;
    schedule.initialRank = scheduleRanks.entries.lookup(entry.first);
    for (const ProcessSuspension &suspension :
         entry.second->getSuspensions())
      schedule.continuations.emplace_back(
          suspension.continuationID,
          scheduleRanks.blocks.lookup(suspension.continuation));
    llvm::sort(schedule.continuations,
               [](const auto &left, const auto &right) {
                 return left.first < right.first;
               });
    if (failed(makeProcessSpawnHelper(module, function, *entry.second,
                                      schedule)))
      return failure();
  }
  if (failed(makeSchedulerMain(module, *stateLayout)))
    return failure();

  SmallVector<sim::SimFuncOp> ordinary;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::Function)
      ordinary.push_back(function);
  });
  for (sim::SimFuncOp function : ordinary)
    if (failed(lowerOrdinaryFunction(function)))
      return failure();

  for (auto &entry : analyses) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    if (!function)
      return failure();
    LogicalResult lowered =
        entry.second->getSuspensions().empty()
            ? lowerPlainNativeProcess(function, *entry.second)
            : lowerSuspendableProcess(function, *entry.second);
    if (failed(lowered))
      return failure();
  }

  SmallVector<sim::SimDesignOp> designs;
  module.walk([&](sim::SimDesignOp design) { designs.push_back(design); });
  for (sim::SimDesignOp design : designs) {
    SmallVector<Operation *> nested;
    for (Operation &operation : design.getBody().front())
      nested.push_back(&operation);
    for (Operation *operation : nested) {
      if (isa<sim::SimScopeDeclOp, sim::SimCodeUnitDeclOp,
              sim::SimStorageDeclOp, sim::SimNetDeclOp,
              sim::SimDriverDeclOp>(operation)) {
        operation->erase();
        continue;
      }
      operation->moveBefore(design);
    }
    design.erase();
  }
  return success();
}

class ConvertObeliskSimProcessesToLLVMCoroutinesPass final
    : public impl::ConvertObeliskSimProcessesToLLVMCoroutinesPassBase<
          ConvertObeliskSimProcessesToLLVMCoroutinesPass> {
public:
  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto layoutAttr = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!layoutAttr) {
      module.emitError(
          "coroutine lowering requires an explicit llvm.data_layout");
      return signalPassFailure();
    }
    llvm::Expected<llvm::DataLayout> parsed =
        llvm::DataLayout::parse(layoutAttr.getValue());
    if (!parsed) {
      module.emitError() << "invalid LLVM data layout: "
                         << llvm::toString(parsed.takeError());
      return signalPassFailure();
    }
    if (!parsed->isLittleEndian() || parsed->getPointerSizeInBits() != 64) {
      module.emitError("coroutine lowering currently requires a 64-bit "
                       "little-endian target");
      return signalPassFailure();
    }
    if (failed(validateProcessABI(module, *parsed)))
      return signalPassFailure();
    if (failed(validateRuntimeToLLVMPreconditions(module, *parsed)))
      return signalPassFailure();
    if (failed(materializeEmbeddedSimulationDesign(module)))
      return signalPassFailure();

    if (failed(prepareSimulationProcessesToLLVMCoroutines(module, *parsed)))
      return signalPassFailure();

    LowerToLLVMOptions options(&getContext());
    options.dataLayout = *parsed;
    LLVMTypeConverter converter(&getContext(), options);
    converter.addConversion([&](Type type) -> std::optional<Type> {
      Type converted = convertProcessType(type, &getContext());
      if (converted != type)
        return converted;
      return std::nullopt;
    });
    addRuntimeToLLVMTypeConversions(converter);
    RewritePatternSet patterns(&getContext());
    populateSimulationCoroutineToLLVMPatterns(converter, patterns);
    if (failed(verify(module)))
      return signalPassFailure();
    ConversionTarget target(getContext());
    target.addLegalDialect<LLVM::LLVMDialect>();
    target.addLegalOp<ModuleOp>();
    target.markUnknownOpDynamicallyLegal(
        [](Operation *operation) { return isa<LLVM::LLVMFuncOp>(operation); });
    if (failed(applyFullConversion(module, target, std::move(patterns)))) {
      signalPassFailure();
      return;
    }
    if (failed(verify(module)))
      signalPassFailure();
  }
};

} // namespace

LogicalResult
prepareSimulationProcessesToLLVMCoroutines(ModuleOp module,
                                           const llvm::DataLayout &dataLayout) {
  return prepareSimulationProcessesForLLVMCoroutinesImpl(module, dataLayout);
}

FailureOr<std::unique_ptr<SimulationProcessFrameAnalysis>>
SimulationProcessFrameAnalysis::create(sim::SimFuncOp function,
                                       const llvm::DataLayout &dataLayout) {
  auto result = std::make_unique<SimulationProcessFrameAnalysis>();
  // Do not leave cached layouts for types owned by this temporary context in
  // a DataLayout that is subsequently reused by another analysis.
  llvm::DataLayout analysisLayout(dataLayout.getStringRepresentation());
  llvm::LLVMContext llvmContext;
  uint64_t cursor = 0;
  auto allocate =
      [&](Type type, ProcessFrameFieldKind kind,
          SmallVectorImpl<ProcessFrameValue> &values) -> LogicalResult {
    FailureOr<StorageProperties> storage =
        storageProperties(type, analysisLayout, llvmContext);
    if (failed(storage)) {
      function.emitError() << "cannot place type " << type
                           << " in the canonical process frame";
      return failure();
    }
    uint64_t valueOffset;
    if (!alignUp(cursor, storage->alignment, valueOffset))
      return function.emitError("canonical process frame offset overflow");
    uint64_t end;
    if (valueOffset > std::numeric_limits<uint64_t>::max() - storage->size)
      return function.emitError("canonical process frame size overflow");
    end = valueOffset + storage->size;
    uint64_t unknownOffset = kNoOffset;
    ProcessFrameFieldFlags flags = storage->fourState
                                       ? ProcessFrameFieldFlags::FourStateValue
                                       : ProcessFrameFieldFlags::None;
    result->fields.push_back(
        {kind, flags, valueOffset, storage->size, storage->alignment});
    if (storage->fourState) {
      unknownOffset = end;
      if (end > std::numeric_limits<uint64_t>::max() - storage->size)
        return function.emitError("canonical process frame size overflow");
      end += storage->size;
      result->fields.push_back({kind, ProcessFrameFieldFlags::FourStateUnknown,
                                unknownOffset, storage->size,
                                storage->alignment});
    }
    cursor = end;
    result->frameAlignment =
        std::max<uint64_t>(result->frameAlignment, storage->alignment);
    values.push_back(
        {valueOffset, unknownOffset, storage->size, storage->alignment});
    return success();
  };

  Block &entry = function.getBody().front();
  for (auto [index, argument] : llvm::enumerate(entry.getArguments())) {
    if (index == 0 && isa<sim::ContextType>(argument.getType())) {
      result->entryCaptureLayout.push_back({kNoOffset, kNoOffset, 0, 1});
      continue;
    }
    if (failed(allocate(argument.getType(), ProcessFrameFieldKind::Capture,
                        result->entryCaptureLayout)))
      return failure();
  }

  SmallVector<Operation *> suspensionOps;
  function.walk([&](Operation *operation) {
    if (isSuspension(operation))
      suspensionOps.push_back(operation);
  });
  llvm::SetVector<Block *> continuationBlocks;
  for (Operation *operation : suspensionOps)
    continuationBlocks.insert(operation->getSuccessor(0));

  // Exactly one semantic continuation is resident while a serial process is
  // suspended.  Treat each successor-argument position as a liveness lane and
  // size it for the largest value that can occupy that lane.  Distinct
  // continuation targets then share storage without copying on tier changes.
  struct ContinuationLane {
    uint64_t planeSize = 0;
    uint32_t alignment = 1;
    bool fourState = false;
    uint64_t valueOffset = 0;
    uint64_t unknownOffset = kNoOffset;
  };
  SmallVector<ContinuationLane> lanes;
  for (Block *block : continuationBlocks) {
    if (lanes.size() < block->getNumArguments())
      lanes.resize(block->getNumArguments());
    for (auto [index, argument] : llvm::enumerate(block->getArguments())) {
      FailureOr<StorageProperties> storage =
          storageProperties(argument.getType(), analysisLayout, llvmContext);
      if (failed(storage)) {
        function.emitError() << "cannot place type " << argument.getType()
                             << " in the canonical process frame";
        return failure();
      }
      ContinuationLane &lane = lanes[index];
      lane.planeSize = std::max(lane.planeSize, storage->size);
      lane.alignment = std::max(lane.alignment, storage->alignment);
      lane.fourState |= storage->fourState;
    }
  }
  for (ContinuationLane &lane : lanes) {
    if (!alignUp(lane.planeSize, lane.alignment, lane.planeSize) ||
        !alignUp(cursor, lane.alignment, lane.valueOffset) ||
        lane.valueOffset >
            std::numeric_limits<uint64_t>::max() - lane.planeSize)
      return function.emitError("canonical process frame size overflow");
    uint64_t end = lane.valueOffset + lane.planeSize;
    result->fields.push_back(
        {ProcessFrameFieldKind::Continuation,
         lane.fourState ? ProcessFrameFieldFlags::FourStateValue
                        : ProcessFrameFieldFlags::None,
         lane.valueOffset, lane.planeSize, lane.alignment});
    if (lane.fourState) {
      lane.unknownOffset = end;
      if (end > std::numeric_limits<uint64_t>::max() - lane.planeSize)
        return function.emitError("canonical process frame size overflow");
      end += lane.planeSize;
      result->fields.push_back({ProcessFrameFieldKind::Continuation,
                                ProcessFrameFieldFlags::FourStateUnknown,
                                lane.unknownOffset, lane.planeSize,
                                lane.alignment});
    }
    cursor = end;
    result->frameAlignment =
        std::max<uint64_t>(result->frameAlignment, lane.alignment);
  }
  for (Block *block : continuationBlocks) {
    auto &layout = result->continuationLayouts[block];
    for (auto [index, argument] : llvm::enumerate(block->getArguments())) {
      FailureOr<StorageProperties> storage =
          storageProperties(argument.getType(), analysisLayout, llvmContext);
      if (failed(storage))
        return failure();
      const ContinuationLane &lane = lanes[index];
      layout.push_back({lane.valueOffset,
                        storage->fourState ? lane.unknownOffset : kNoOffset,
                        storage->size, storage->alignment});
    }
  }

  uint64_t nextID = 1;
  // LLVM's DenseSet reserves the two largest unsigned values as sentinels,
  // but both are valid continuation IDs in the runtime ABI.
  std::set<uint32_t> usedIDs;
  uint64_t maxWaitSize = kWaitHeaderSize;
  for (Operation *operation : suspensionOps) {
    if (auto site =
            operation->getAttrOfType<sim::ContinuationSiteAttr>("site")) {
      if (site.getId() == 0 || !usedIDs.insert(site.getId()).second) {
        operation->emitError("requires a unique nonzero continuation ID");
        return failure();
      }
    }
    uint64_t entries = waitEntryCount(operation);
    if (entries > (std::numeric_limits<uint64_t>::max() - kWaitHeaderSize) /
                      kWaitEntrySize)
      return operation->emitError("wait record size overflow");
    maxWaitSize =
        std::max(maxWaitSize, kWaitHeaderSize + entries * kWaitEntrySize);
  }
  uint64_t waitOffset = kNoOffset;
  if (!suspensionOps.empty()) {
    if (!alignUp(cursor, 8, waitOffset) ||
        waitOffset > std::numeric_limits<uint64_t>::max() - maxWaitSize)
      return function.emitError("canonical wait record offset overflow");
    result->fields.push_back({ProcessFrameFieldKind::Wait,
                              ProcessFrameFieldFlags::None, waitOffset,
                              maxWaitSize, 8});
    result->frameAlignment = std::max<uint64_t>(result->frameAlignment, 8);
    cursor = waitOffset + maxWaitSize;
  }

  for (Operation *operation : suspensionOps) {
    uint32_t id;
    if (auto site = operation->getAttrOfType<sim::ContinuationSiteAttr>("site"))
      id = site.getId();
    else {
      while (nextID <= std::numeric_limits<uint32_t>::max() &&
             usedIDs.count(static_cast<uint32_t>(nextID)) != 0)
        ++nextID;
      if (nextID > std::numeric_limits<uint32_t>::max())
        return operation->emitError("continuation ID space is exhausted");
      id = static_cast<uint32_t>(nextID++);
      usedIDs.insert(id);
    }
    result->suspensions.push_back(
        {operation, operation->getSuccessor(0), id, waitOffset, maxWaitSize});
    result->continuationLayoutsByID[id] =
        result->continuationLayouts.lookup(operation->getSuccessor(0));
  }
  result->continuations.push_back(0);
  for (uint32_t id : usedIDs)
    result->continuations.push_back(id);
  llvm::sort(result->continuations);
  if (result->frameAlignment > 4096 ||
      result->fields.size() > std::numeric_limits<uint32_t>::max() ||
      result->continuations.size() > std::numeric_limits<uint32_t>::max())
    return function.emitError(
        "canonical process frame exceeds the runtime ABI limits");
  if (!alignUp(cursor, result->frameAlignment, result->frameSize))
    return function.emitError("canonical process frame size overflow");

  uint64_t hash = UINT64_C(14695981039346656037);
  hash = appendHash(hash, 1, 4);
  hash = appendHash(hash, 0, 4);
  hash = appendHash(hash, result->frameSize, 8);
  hash = appendHash(hash, result->frameAlignment, 8);
  hash = appendHash(hash, result->fields.size(), 4);
  hash = appendHash(hash, result->continuations.size(), 4);
  for (const ProcessFrameField &field : result->fields) {
    hash = appendHash(hash, static_cast<uint32_t>(field.kind), 4);
    hash = appendHash(hash, static_cast<uint32_t>(field.flags), 4);
    hash = appendHash(hash, field.offset, 8);
    hash = appendHash(hash, field.size, 8);
    hash = appendHash(hash, field.alignment, 4);
    hash = appendHash(hash, 0, 4);
  }
  for (uint32_t continuation : result->continuations)
    hash = appendHash(hash, continuation, 4);
  result->checksum = hash;
  return result;
}

ArrayRef<ProcessFrameValue>
SimulationProcessFrameAnalysis::getContinuationLayout(Block *block) const {
  auto found = continuationLayouts.find(block);
  return found == continuationLayouts.end()
             ? ArrayRef<ProcessFrameValue>{}
             : ArrayRef<ProcessFrameValue>(found->second);
}

ArrayRef<ProcessFrameValue>
SimulationProcessFrameAnalysis::getContinuationLayout(
    uint32_t continuationID) const {
  auto found = continuationLayoutsByID.find(continuationID);
  return found == continuationLayoutsByID.end()
             ? ArrayRef<ProcessFrameValue>{}
             : ArrayRef<ProcessFrameValue>(found->second);
}

const ProcessSuspension *
SimulationProcessFrameAnalysis::getSuspension(Operation *operation) const {
  for (const ProcessSuspension &suspension : suspensions)
    if (suspension.operation == operation)
      return &suspension;
  return nullptr;
}

void populateSimulationCoroutineToLLVMPatterns(
    const LLVMTypeConverter &converter, RewritePatternSet &patterns) {
  populateRuntimeToLLVMPatterns(converter, patterns);
  patterns.add<SimContextRuntimeLowering>(converter, patterns.getContext());
  arith::populateArithToLLVMConversionPatterns(converter, patterns);
  cf::populateControlFlowToLLVMConversionPatterns(converter, patterns);
  populateFuncToLLVMConversionPatterns(converter, patterns);
}

} // namespace obelisk
