//===- SimulationToLLVMCoroutine.cpp - Native process coroutines ---------===//

#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Analysis/NetConnectivityAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Analysis/SimulationStorageAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Analysis/StaticSpecializationAnalysis.h"
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Conversion/SimulationToRuntime.h"
#include "obelisk/Conversion/SimulationToStandard.h"
#include "obelisk/Conversion/SimulationTimeLowering.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Analysis/Liveness.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/LLVMCommon/LoweringOptions.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Conversion/MathToLLVM/MathToLLVM.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Math/IR/Math.h"
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
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <type_traits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTOBELISKSIMPROCESSESTOLLVMCOROUTINESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using detail::byteGEP;
using detail::castIntegerWidth;
using detail::entryAlloca;
using detail::getOrDeclareLLVMFunction;
using detail::llvmConstant;
using detail::loadAt;
using detail::lowerNativeDPICalls;
using detail::materializeDPIThunks;
using detail::storeAt;

constexpr uint64_t kNoOffset = std::numeric_limits<uint64_t>::max();
constexpr uint64_t kWaitHeaderSize = sizeof(obelisk_rt_wait_record_v1);
constexpr uint64_t kWaitEntrySize = sizeof(obelisk_rt_wait_entry_v1);
constexpr uint32_t kWaitEdgeNone = std::numeric_limits<uint32_t>::max();
constexpr StringLiteral kAutomaticOwnerReleaseMarker =
    "__obelisk_release_automatic_owner";
constexpr StringLiteral kNativeTwoStateBlockUnknownsAttr =
    "obelisk.native.two_state_block_unknowns";
constexpr StringLiteral kAssumeCleanSpecializationAttr =
    "obelisk.native.assume_clean_specialization";

constexpr uint64_t kInstanceAllocationOffset =
    offsetof(obelisk_rt_process_instance_v1, allocation);
constexpr uint64_t kInstanceFrameOffset =
    offsetof(obelisk_rt_process_instance_v1, frame);
constexpr uint64_t kInstanceScratchOffset =
    offsetof(obelisk_rt_process_instance_v1, scratch_offset);
constexpr uint64_t kInstanceNativeHandleOffset =
    offsetof(obelisk_rt_process_instance_v1, native_handle);
constexpr uint64_t kInstanceContinuationOffset =
    offsetof(obelisk_rt_process_instance_v1, continuation);
constexpr uint64_t kInstanceStatusOffset =
    offsetof(obelisk_rt_process_instance_v1, status);
constexpr uint64_t kInstanceContextOffset =
    offsetof(obelisk_rt_process_instance_v1, context);
constexpr uint64_t kInstanceActionOffset =
    offsetof(obelisk_rt_process_instance_v1, action);

bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result) {
  if (value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
    return false;
  result = llvm::alignTo(value, alignment);
  return true;
}

uint32_t suspensionKind(Operation *operation) {
  return TypeSwitch<Operation *, uint32_t>(operation)
      .Case<sim::SimSuspendDelayOp>([](auto) { return 1; })
      .Case<sim::SimSuspendChangeOp>([](auto) { return 2; })
      .Case<sim::SimSuspendLevelOp>([](auto) { return 2; })
      .Case<sim::SimSuspendEdgeOp>([](auto) { return 3; })
      .Case<sim::SimSuspendEdgeIffOp>([](auto) { return 3; })
      .Case<sim::SimSuspendAnyOp>([](auto) { return 3; })
      .Case<sim::SimSuspendEventOp>([](auto) { return 4; })
      .Case<sim::SimSuspendAwaitOp>([](auto) { return 5; })
      .Case<sim::SimSuspendJoinOp>([](auto) { return 6; })
      .Case<sim::SimSuspendForeverOp>([](auto) { return 7; })
      .Case<sim::SimSuspendChildrenOp>([](auto) { return 9; })
      .Case<sim::SimSuspendObserveOp>([](auto) { return 10; })
      .Default([](Operation *) { return 0; });
}

bool containsLogic(Type type);

std::optional<unsigned> nativeStateWidth(Type type) {
  return analysis::getSimulationStorageBitWidth(type);
}

std::optional<uint64_t> unionPayloadSpan(Type type) {
  if (auto packed = dyn_cast<sim::PackedUnionType>(type)) {
    std::optional<unsigned> width = sim::getPackedWidth(type);
    if (!width || packed.getTagBits() > *width)
      return std::nullopt;
    return static_cast<uint64_t>(*width - packed.getTagBits());
  }
  if (isa<sim::UnpackedUnionType>(type))
    return sim::getProvenanceSpan(type);
  return std::nullopt;
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
      checkStruct({i64, pointer, i32, i32}, {0, 8, 16, 20}, 24, 8) &&
      checkStruct({i32, i32, i32, i32, pointer, i64, pointer, i64, i64, i64,
                   pointer, i64, i32, i32, pointer, i64},
                  {0, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72, 80, 84, 88, 96},
                  104, 8) &&
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

bool containsLogic(Type type) {
  if (sim::isManagedHandleType(type))
    return false;
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
  SimFuncSignatureConversion(const TypeConverter &converter,
                             MLIRContext *context,
                             const DenseSet<Value> &twoStateValues)
      : OpConversionPattern(converter, context),
        twoStateValues(&twoStateValues) {}

  LogicalResult
  matchAndRewrite(sim::SimFuncOp function, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    FunctionType type = function.getFunctionType();
    TypeConverter::SignatureConversion entry(type.getNumInputs());
    SmallVector<Type> results;
    SmallVector<SmallVector<int64_t>> zeroUnknownArguments;
    zeroUnknownArguments.reserve(function.getBody().getBlocks().size());
    for (Block &block : function.getBody()) {
      SmallVector<int64_t> unknowns;
      unsigned physical = 0;
      for (BlockArgument argument : block.getArguments()) {
        SmallVector<Type> converted;
        if (failed(
                getTypeConverter()->convertType(argument.getType(), converted)))
          return failure();
        if (isa<sim::LogicType>(argument.getType()) &&
            twoStateValues->contains(argument) && converted.size() == 2)
          unknowns.push_back(static_cast<int64_t>(physical + 1));
        physical += converted.size();
      }
      zeroUnknownArguments.push_back(std::move(unknowns));
    }
    if (failed(getTypeConverter()->convertSignatureArgs(type.getInputs(),
                                                        entry)) ||
        failed(getTypeConverter()->convertTypes(type.getResults(), results)) ||
        (!function.getBody().empty() &&
         failed(rewriter.convertRegionTypes(&function.getBody(),
                                            *getTypeConverter(), &entry))))
      return failure();
    SmallVector<Attribute> zeroUnknownAttr;
    zeroUnknownAttr.reserve(zeroUnknownArguments.size());
    for (ArrayRef<int64_t> indices : zeroUnknownArguments)
      zeroUnknownAttr.push_back(rewriter.getDenseI64ArrayAttr(indices));
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
      function->setAttr(kNativeTwoStateBlockUnknownsAttr,
                        rewriter.getArrayAttr(zeroUnknownAttr));
    });
    return success();
  }

private:
  const DenseSet<Value> *twoStateValues;
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

class ArithSelectTypeConversion final
    : public OpConversionPattern<arith::SelectOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(arith::SelectOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto operands = adaptor.getOperands();
    if (operands.size() != 3 || operands[0].size() != 1 ||
        operands[1].size() != operands[2].size())
      return rewriter.notifyMatchFailure(
          operation, "select operands have incompatible converted arity");

    SmallVector<Value> results;
    results.reserve(operands[1].size());
    for (auto [trueValue, falseValue] :
         llvm::zip_equal(operands[1], operands[2])) {
      auto selected =
          arith::SelectOp::create(rewriter, operation.getLoc(),
                                  operands[0].front(), trueValue, falseValue);
      selected->setAttrs(operation->getAttrs());
      results.push_back(selected);
    }
    SmallVector<SmallVector<Value>> replacements;
    replacements.push_back(std::move(results));
    rewriter.replaceOpWithMultiple(operation, std::move(replacements));
    return success();
  }
};

SmallVector<int32_t> suspensionWaitWidths(Operation *operation);

class SimObserverBindTypeConversion final
    : public OpConversionPattern<sim::SimObserverBindOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimObserverBindOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Type> results;
    if (failed(getTypeConverter()->convertType(operation.getResult().getType(),
                                               results)) ||
        results.size() != 1)
      return rewriter.notifyMatchFailure(
          operation, "observer token must convert to one physical value");
    auto observerType = operation.getResult().getType().getResultType();
    std::optional<unsigned> resultWidth =
        isa<FloatType>(observerType)
            ? std::optional<unsigned>(cast<FloatType>(observerType).getWidth())
            : sim::getPackedWidth(observerType);
    if (!resultWidth)
      return operation.emitOpError("observer result must remain packed");
    auto evaluator = SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(
        operation, operation.getEvaluatorAttr());
    if (!evaluator || !evaluator.getCodeUnitId())
      return operation.emitOpError(
          "observer evaluator is missing its stable code-unit ID");

    SmallVector<int32_t> dependencyKinds;
    SmallVector<int32_t> dependencyWidths;
    for (Value dependency : operation.getDependencies()) {
      if (isa<sim::EventType>(dependency.getType())) {
        dependencyKinds.push_back(OBELISK_RT_OBSERVER_DEPENDENCY_EVENT);
        dependencyWidths.push_back(1);
        continue;
      }
      auto type =
          isa<sim::RefType>(dependency.getType())
              ? cast<sim::RefType>(dependency.getType()).getElementType()
              : cast<sim::NetType>(dependency.getType()).getElementType();
      std::optional<unsigned> width =
          isa<FloatType>(type)
              ? std::optional<unsigned>(cast<FloatType>(type).getWidth())
              : sim::getPackedWidth(type);
      if (!width)
        return operation.emitOpError(
            "observer signal dependency must have a packed width");
      dependencyKinds.push_back(OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL);
      dependencyWidths.push_back(static_cast<int32_t>(*width));
    }

    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addTypes(results);
    state.addAttributes(operation->getAttrs());
    state.addAttribute("obelisk.coro.observer_id",
                       rewriter.getI64IntegerAttr(
                           static_cast<uint64_t>(*evaluator.getCodeUnitId())));
    state.addAttribute("obelisk.coro.observer_width",
                       rewriter.getI32IntegerAttr(*resultWidth));
    state.addAttribute("obelisk.coro.observer_four_state",
                       rewriter.getBoolAttr(isa<sim::LogicType>(observerType)));
    state.addAttribute("obelisk.coro.dependency_kinds",
                       rewriter.getDenseI32ArrayAttr(dependencyKinds));
    state.addAttribute("obelisk.coro.dependency_widths",
                       rewriter.getDenseI32ArrayAttr(dependencyWidths));
    rewriter.replaceOp(operation, rewriter.create(state)->getResults());
    return success();
  }
};

class SimSuspendObserveTypeConversion final
    : public OpConversionPattern<sim::SimSuspendObserveOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimSuspendObserveOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    size_t primaryCount = operation.getEdges().size();
    size_t conditionCount = operation.getConditionCount();
    if (operation.getNumOperands() < primaryCount * 2 + conditionCount)
      return operation.emitOpError("has a truncated observer inventory");
    ArrayRef<ValueRange> converted = adaptor.getOperands();
    SmallVector<Value> operands;
    SmallVector<int32_t> initialPlaneCounts;
    for (size_t index = 0; index != primaryCount; ++index)
      llvm::append_range(operands, converted[index]);
    for (size_t index = 0; index != primaryCount; ++index) {
      ValueRange planes = converted[primaryCount + index];
      if (planes.empty() || planes.size() > 2)
        return operation.emitOpError(
            "observer initial value must lower to one or two planes");
      initialPlaneCounts.push_back(static_cast<int32_t>(planes.size()));
      llvm::append_range(operands, planes);
    }
    size_t conditionBegin = operands.size();
    for (size_t index = 0; index != conditionCount; ++index) {
      ValueRange token = converted[primaryCount * 2 + index];
      if (token.size() != 1)
        return operation.emitOpError(
            "condition observer token must lower to one value");
      llvm::append_range(operands, token);
    }
    size_t continuationBegin = operands.size();
    for (size_t index = primaryCount * 2 + conditionCount;
         index != converted.size(); ++index)
      llvm::append_range(operands, converted[index]);

    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(operands);
    state.addSuccessors(operation->getSuccessors());
    state.addAttributes(operation->getAttrs());
    state.addAttribute("obelisk.coro.initial_plane_counts",
                       rewriter.getDenseI32ArrayAttr(initialPlaneCounts));
    state.addAttribute("obelisk.coro.condition_operand_begin",
                       rewriter.getI64IntegerAttr(conditionBegin));
    state.addAttribute("obelisk.coro.continuation_operand_begin",
                       rewriter.getI64IntegerAttr(continuationBegin));
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

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
  Value contextAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  Value context =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  Value status = LLVM::CallOp::create(
                     builder, location, TypeRange{i32},
                     SymbolRefAttr::get(builder.getContext(),
                                        "obelisk_rt_v1_native_state_retain"),
                     ValueRange{context, handle})
                     .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(builder.getContext(), "obelisk_rt_v1_scheduler_fail"),
      ValueRange{context, status});
}

class SimCallTypeConversion final : public OpConversionPattern<sim::SimCallOp> {
public:
  SimCallTypeConversion(const TypeConverter &converter, MLIRContext *context,
                        const DenseSet<Value> &twoStateValues)
      : OpConversionPattern(converter, context),
        twoStateValues(&twoStateValues) {}
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
    SmallVector<SmallVector<Value>> replacements;
    replacements.reserve(resultSizes.size());
    size_t offset = 0;
    for (auto [index, size] : llvm::enumerate(resultSizes)) {
      SmallVector<Value> values(replacement->getResults().slice(offset, size));
      if (size == 2 && twoStateValues->contains(operation.getResult(index))) {
        auto type = dyn_cast<IntegerType>(values[1].getType());
        if (!type)
          return failure();
        values[1] = arith::ConstantOp::create(
            rewriter, operation.getLoc(), type,
            rewriter.getIntegerAttr(type, APInt::getZero(type.getWidth())));
      }
      replacements.push_back(std::move(values));
      offset += size;
    }
    rewriter.replaceOpWithMultiple(operation, std::move(replacements));
    return success();
  }

private:
  const DenseSet<Value> *twoStateValues;
};

class SimTaskCallTypeConversion final
    : public OpConversionPattern<sim::SimTaskCallOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimTaskCallOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    uint64_t logicalArguments = operation.getArgumentCount();
    if (logicalArguments > adaptor.getOperands().size())
      return failure();
    uint64_t physicalArguments = 0;
    for (ValueRange values :
         ArrayRef(adaptor.getOperands()).take_front(logicalArguments))
      physicalArguments += values.size();
    for (auto [operand, converted] :
         llvm::zip_equal(operation.getOperands(), adaptor.getOperands()))
      if (isa<sim::RefType>(operand.getType()) && converted.size() == 1)
        emitNativeStateRetain(rewriter, operation.getLoc(), converted.front());
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addSuccessors(operation->getSuccessors());
    state.addAttributes(operation->getAttrs());
    state.attributes.set(operation.getArgumentCountAttrName(),
                         rewriter.getI64IntegerAttr(physicalArguments));
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

class SimDisableChildrenConversion final
    : public OpConversionPattern<sim::SimDisableChildrenOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimDisableChildrenOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    (void)adaptor;
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, operation.getLoc(), pointer, "__obelisk_current_context");
    Value context = LLVM::LoadOp::create(rewriter, operation.getLoc(), pointer,
                                         contextAddress, 8);
    Value status =
        LLVM::CallOp::create(
            rewriter, operation.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_disable_children"),
            ValueRange{context})
            .getResult();
    LLVM::CallOp::create(rewriter, operation.getLoc(), TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{context, status});
    rewriter.eraseOp(operation);
    return success();
  }
};

static std::pair<Value, Type>
loadCurrentRuntimeContext(ConversionPatternRewriter &rewriter,
                          Location location) {
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  Value address = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                            "__obelisk_current_context");
  return {LLVM::LoadOp::create(rewriter, location, pointer, address, 8),
          pointer};
}

static void reportRuntimeControlStatus(ConversionPatternRewriter &rewriter,
                                       Location location, Value context,
                                       Value status) {
  LLVM::CallOp::create(
      rewriter, location, TypeRange{},
      SymbolRefAttr::get(rewriter.getContext(), "obelisk_rt_v1_scheduler_fail"),
      ValueRange{context, status});
}

class SimControlEnterConversion final
    : public OpConversionPattern<sim::SimControlEnterOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimControlEnterOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    auto [context, pointer] = loadCurrentRuntimeContext(rewriter, location);
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Value out = entryAlloca(rewriter, location, i64, 1, 8);
    LLVM::StoreOp::create(rewriter, location,
                          llvmConstant(rewriter, location, i64, 0), out, 8);
    Value status =
        LLVM::CallOp::create(rewriter, location, TypeRange{i32},
                             SymbolRefAttr::get(rewriter.getContext(),
                                                "obelisk_rt_v1_control_enter"),
                             ValueRange{context,
                                        llvmConstant(rewriter, location, i64,
                                                     operation.getTargetId()),
                                        out})
            .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    Value activation = LLVM::LoadOp::create(rewriter, location, i64, out, 8);
    rewriter.replaceOp(operation, activation);
    return success();
  }
};

class SimControlLeaveConversion final
    : public OpConversionPattern<sim::SimControlLeaveOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimControlLeaveOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getControl().size() != 1)
      return failure();
    Location location = operation.getLoc();
    auto [context, pointer] = loadCurrentRuntimeContext(rewriter, location);
    (void)pointer;
    Value status = LLVM::CallOp::create(
                       rewriter, location, TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_control_leave"),
                       ValueRange{context, adaptor.getControl().front()})
                       .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(operation);
    return success();
  }
};

class SimControlDisableConversion final
    : public OpConversionPattern<sim::SimControlDisableOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimControlDisableOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    auto [context, pointer] = loadCurrentRuntimeContext(rewriter, location);
    (void)pointer;
    Value activation =
        adaptor.getActivation().empty()
            ? llvmConstant(rewriter, location, rewriter.getI64Type(), 0)
            : adaptor.getActivation().front();
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_control_disable"),
            ValueRange{context,
                       llvmConstant(rewriter, location, rewriter.getI64Type(),
                                    operation.getTargetId()),
                       activation,
                       llvmConstant(rewriter, location, rewriter.getI32Type(),
                                    operation.getHierarchical() ? 1 : 0)})
            .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(operation);
    return success();
  }
};

class SimStaticOnceConversion final
    : public OpConversionPattern<sim::SimStaticOnceOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimStaticOnceOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    auto [context, pointer] = loadCurrentRuntimeContext(rewriter, location);
    (void)pointer;
    Value claimed = LLVM::CallOp::create(
                        rewriter, location, TypeRange{rewriter.getI32Type()},
                        SymbolRefAttr::get(rewriter.getContext(),
                                           "obelisk_rt_v1_static_once"),
                        ValueRange{context, llvmConstant(rewriter, location,
                                                         rewriter.getI64Type(),
                                                         operation.getId())})
                        .getResult();
    Value first = arith::TruncIOp::create(rewriter, location,
                                          rewriter.getI1Type(), claimed);
    rewriter.replaceOp(operation, first);
    return success();
  }
};

class SimDeferredOnceConversion final
    : public OpConversionPattern<sim::SimDeferredOnceOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimDeferredOnceOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    auto [context, pointer] = loadCurrentRuntimeContext(rewriter, location);
    (void)pointer;
    Value claimed = LLVM::CallOp::create(
                        rewriter, location, TypeRange{rewriter.getI32Type()},
                        SymbolRefAttr::get(rewriter.getContext(),
                                           "obelisk_rt_v1_deferred_once"),
                        ValueRange{context, llvmConstant(rewriter, location,
                                                         rewriter.getI64Type(),
                                                         operation.getId())})
                        .getResult();
    Value first = arith::TruncIOp::create(rewriter, location,
                                          rewriter.getI1Type(), claimed);
    rewriter.replaceOp(operation, first);
    return success();
  }
};

class SimMonitorRegisterConversion final
    : public OpConversionPattern<sim::SimMonitorRegisterOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimMonitorRegisterOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getProcess().size() != 1)
      return failure();
    Location location = operation.getLoc();
    auto [context, pointer] = loadCurrentRuntimeContext(rewriter, location);
    (void)pointer;
    Value status = LLVM::CallOp::create(
                       rewriter, location, TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_monitor_register"),
                       ValueRange{context, adaptor.getProcess().front(),
                                  llvmConstant(rewriter, location,
                                               rewriter.getI32Type(), 0)})
                       .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(operation);
    return success();
  }
};

class SimMonitorControlConversion final
    : public OpConversionPattern<sim::SimMonitorControlOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimMonitorControlOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    auto [context, pointer] = loadCurrentRuntimeContext(rewriter, location);
    (void)pointer;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_monitor_control"),
            ValueRange{context,
                       llvmConstant(rewriter, location, rewriter.getI32Type(),
                                    operation.getEnabled() ? 1 : 0)})
            .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(operation);
    return success();
  }
};

class SimMonitorCurrentConversion final
    : public OpConversionPattern<sim::SimMonitorCurrentOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimMonitorCurrentOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    auto [context, pointer] = loadCurrentRuntimeContext(rewriter, location);
    (void)pointer;
    Value current = LLVM::CallOp::create(
                        rewriter, location, TypeRange{rewriter.getI32Type()},
                        SymbolRefAttr::get(rewriter.getContext(),
                                           "obelisk_rt_v1_monitor_current"),
                        ValueRange{context})
                        .getResult();
    rewriter.replaceOpWithNewOp<arith::TruncIOp>(operation,
                                                 rewriter.getI1Type(), current);
    return success();
  }
};

class SimDPICallTypeConversion final
    : public OpConversionPattern<sim::SimDPICallOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimDPICallOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
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
    state.addAttribute(
        "obelisk.dpi.logical_operand_count",
        rewriter.getI32IntegerAttr(operation.getArguments().size()));
    Operation *replacement = rewriter.create(state);
    SmallVector<SmallVector<Value>> replacements;
    replacements.reserve(resultSizes.size());
    size_t offset = 0;
    for (size_t size : resultSizes) {
      replacements.emplace_back(replacement->getResults().slice(offset, size));
      offset += size;
    }
    rewriter.replaceOpWithMultiple(operation, std::move(replacements));
    return success();
  }
};

SmallVector<int32_t> suspensionWaitWidths(Operation *operation) {
  SmallVector<Value> watched;
  SmallVector<bool> scalarEdge;
  TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendChangeOp>([&](auto op) {
        watched.push_back(op.getWatched());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendLevelOp>([&](auto op) {
        watched.push_back(op.getWatched());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendEdgeOp>([&](auto op) {
        watched.push_back(op.getWatched());
        scalarEdge.push_back(true);
      })
      .Case<sim::SimSuspendEdgeIffOp>([&](auto op) {
        watched.push_back(op.getWatched());
        scalarEdge.push_back(true);
        watched.push_back(op.getCondition());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendAnyOp>([&](auto op) {
        llvm::append_range(watched, op.getWatched());
        for (int32_t edge : op.getEdges())
          scalarEdge.push_back(edge !=
                               static_cast<int32_t>(sim::EdgeKind::Change));
      })
      .Case<sim::SimSuspendEventOp>([&](auto op) {
        watched.push_back(op.getEvent());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendAwaitOp>([&](auto op) {
        watched.push_back(op.getProcess());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendJoinOp>([&](auto op) {
        llvm::append_range(watched, op.getProcesses());
        scalarEdge.append(op.getProcesses().size(), false);
      });
  SmallVector<int32_t> widths;
  widths.reserve(watched.size());
  for (auto [index, value] : llvm::enumerate(watched)) {
    if (scalarEdge[index]) {
      widths.push_back(1);
      continue;
    }
    Type type = value.getType();
    if (auto reference = dyn_cast<sim::RefType>(type))
      type = reference.getElementType();
    else if (auto net = dyn_cast<sim::NetType>(type))
      type = net.getElementType();
    else
      type = {};
    std::optional<unsigned> width =
        type ? nativeStateWidth(type) : std::nullopt;
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
        adaptor.getReplacement().empty() || subelement->first > *resultWidth ||
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
        rewriter.getIntegerAttr(planeType,
                                APInt(*resultWidth, subelement->first)));
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
        replacement = sourceType == planeType
                          ? source
                          : arith::ExtUIOp::create(rewriter, op.getLoc(),
                                                   planeType, source)
                                .getResult();
        if (subelement->first != 0)
          replacement =
              arith::ShLIOp::create(rewriter, op.getLoc(), replacement, shift);
      }
      Value preserved =
          arith::AndIOp::create(rewriter, op.getLoc(), input, keepMask);
      results.push_back(
          arith::OrIOp::create(rewriter, op.getLoc(), preserved, replacement));
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
    SignedI64Index convertedIndex =
        resizeSignedIndexToI64(rewriter, location, adaptor.getIndex().front());
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
    std::optional<uint64_t> payloadSpan = unionPayloadSpan(unionType);
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
      results.push_back(inputType == resultType
                            ? plane
                            : arith::TruncIOp::create(rewriter, op.getLoc(),
                                                      resultType, plane)
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

class UnionIsActiveConversion final
    : public OpConversionPattern<sim::SimUnionIsActiveOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimUnionIsActiveOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type unionType = op.getInput().getType();
    std::optional<uint64_t> payloadSpan = unionPayloadSpan(unionType);
    if (!payloadSpan || adaptor.getInput().empty())
      return failure();
    unsigned tagBits = 0;
    uint64_t expected = 0;
    if (auto packed = dyn_cast<sim::PackedUnionType>(unionType)) {
      tagBits = packed.getTagBits();
      expected = op.getIndex();
    } else if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(unionType)) {
      tagBits = llvm::Log2_64_Ceil(
          static_cast<uint64_t>(sim::getAggregateNumElements(unionType)) + 1);
      expected = static_cast<uint64_t>(op.getIndex()) + 1;
    }
    if (tagBits == 0) {
      Value active =
          arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI1Type(),
                                    rewriter.getBoolAttr(true));
      rewriter.replaceOp(op, active);
      return success();
    }
    auto extractTag = [&](Value plane) -> FailureOr<Value> {
      auto planeType = dyn_cast<IntegerType>(plane.getType());
      if (!planeType || *payloadSpan + tagBits > planeType.getWidth())
        return failure();
      Value shifted = plane;
      if (*payloadSpan != 0) {
        Value amount = arith::ConstantOp::create(
            rewriter, op.getLoc(), planeType,
            rewriter.getIntegerAttr(planeType, *payloadSpan));
        shifted = arith::ShRUIOp::create(rewriter, op.getLoc(), plane, amount);
      }
      auto tagType = rewriter.getIntegerType(tagBits);
      if (tagType != planeType)
        shifted =
            arith::TruncIOp::create(rewriter, op.getLoc(), tagType, shifted);
      return shifted;
    };
    FailureOr<Value> tag = extractTag(adaptor.getInput().front());
    if (failed(tag))
      return failure();
    auto tagType = cast<IntegerType>((*tag).getType());
    Value expectedTag =
        arith::ConstantOp::create(rewriter, op.getLoc(), tagType,
                                  rewriter.getIntegerAttr(tagType, expected));
    Value equal = arith::CmpIOp::create(
        rewriter, op.getLoc(), arith::CmpIPredicate::eq, *tag, expectedTag);
    if (adaptor.getInput().size() == 2) {
      FailureOr<Value> unknownTag = extractTag(adaptor.getInput()[1]);
      if (failed(unknownTag))
        return failure();
      Value zero = arith::ConstantOp::create(
          rewriter, op.getLoc(), tagType, rewriter.getIntegerAttr(tagType, 0));
      Value known = arith::CmpIOp::create(
          rewriter, op.getLoc(), arith::CmpIPredicate::eq, *unknownTag, zero);
      equal = arith::AndIOp::create(rewriter, op.getLoc(), equal, known);
    }
    rewriter.replaceOp(op, equal);
    return success();
  }
};

Type convertProcessType(Type type, MLIRContext *context) {
  if (isa<sim::ContextType, runtime::ContextType,
          runtime::ProcessDescriptorType, runtime::ProcessInstanceType>(type))
    return LLVM::LLVMPointerType::get(context);
  if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
          sim::ProcessType, sim::ControlType, sim::CovergroupHandleType>(type))
    return IntegerType::get(context, 64);
  if (sim::isManagedHandleType(type))
    return IntegerType::get(context, 64);
  if (isa<sim::ArgumentRefType>(type))
    return IntegerType::get(context, 192);
  if (isa<sim::TimeType>(type))
    return IntegerType::get(context, 64);
  return type;
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
  if (!isa<IntegerType, Float64Type, LLVM::LLVMPointerType>(value.getType()))
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
    if (slot.hasSecondaryStorage()) {
      if (physical >= operands.size() ||
          failed(storeFrameValue(builder, location, frame, operands[physical++],
                                 slot.getSecondaryOffset(), slot.alignment)))
        return operation->emitError(
            "cannot store secondary continuation value in frame");
    }
  }
  if (physical != operands.size())
    return operation->emitError(
        "converted continuation arity disagrees with frame analysis");

  if (auto task = dyn_cast<sim::SimTaskCallOp>(operation)) {
    Type i32 = builder.getI32Type();
    Type i64 = builder.getI64Type();
    storeAt(builder, location, instance, kInstanceContinuationOffset,
            llvmConstant(builder, location, i32, continuationID), 4);
    std::string helper = (task.getCallee() + ".__obelisk_activate").str();
    Value activation =
        LLVM::CallOp::create(builder, location, TypeRange{i64},
                             SymbolRefAttr::get(builder.getContext(), helper),
                             task.getArguments())
            .getResult();
    publishAction(builder, location, instance, 3, 0, continuationID, 0,
                  activation, 0);
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

  uint64_t waitOffset = waitOffsetAttr.getInt();
  uint64_t waitSize = waitSizeAttr.getInt();
  uint32_t kind = suspensionKind(operation);
  uint32_t count = sim::getWaitEntryCount(operation);
  Value wait = byteGEP(builder, location, frame, waitOffset);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  llvm::SetVector<Operation *> observerBindings;
  if (auto observe = dyn_cast<sim::SimSuspendObserveOp>(operation)) {
    uint32_t primaryCount = observe.getEdges().size();
    uint32_t conditionCount = observe.getConditionCount();
    uint32_t observerCount = primaryCount + conditionCount;
    auto planeCounts = operation->getAttrOfType<DenseI32ArrayAttr>(
        "obelisk.coro.initial_plane_counts");
    auto conditionBegin = operation->getAttrOfType<IntegerAttr>(
        "obelisk.coro.condition_operand_begin");
    if (!planeCounts || planeCounts.size() != primaryCount || !conditionBegin)
      return operation->emitError(
          "missing converted observer operand metadata");
    SmallVector<sim::SimObserverBindOp> bindings;
    bindings.reserve(observerCount);
    for (uint32_t index = 0; index != primaryCount; ++index) {
      auto binding =
          operation->getOperand(index).getDefiningOp<sim::SimObserverBindOp>();
      if (!binding)
        return operation->emitError(
            "primary observer token is not produced by observer.bind");
      bindings.push_back(binding);
    }
    uint64_t conditionOperand = conditionBegin.getValue().getZExtValue();
    for (uint32_t index = 0; index != conditionCount; ++index) {
      if (conditionOperand + index >= operation->getNumOperands())
        return operation->emitError(
            "condition observer inventory is truncated");
      auto binding = operation->getOperand(conditionOperand + index)
                         .getDefiningOp<sim::SimObserverBindOp>();
      if (!binding)
        return operation->emitError(
            "condition observer token is not produced by observer.bind");
      bindings.push_back(binding);
    }
    uint32_t captureCount = 0;
    uint32_t dependencyCount = 0;
    uint32_t previousLimbs = 0;
    SmallVector<uint32_t> widths;
    SmallVector<bool> fourState;
    for (auto [index, binding] : llvm::enumerate(bindings)) {
      captureCount += binding.getCaptureCount();
      dependencyCount += binding.getDependencies().size();
      auto width =
          binding->getAttrOfType<IntegerAttr>("obelisk.coro.observer_width");
      auto four =
          binding->getAttrOfType<BoolAttr>("obelisk.coro.observer_four_state");
      if (!width || !four)
        return binding.emitOpError("missing converted observer metadata");
      widths.push_back(width.getValue().getZExtValue());
      fourState.push_back(four.getValue());
      if (index < primaryCount)
        previousLimbs += (uint64_t{widths.back()} + 63) / 64;
    }
    uint64_t observersOffset = sizeof(obelisk_rt_computed_wait_record_v1);
    uint64_t capturesOffset =
        observersOffset +
        uint64_t{observerCount} * sizeof(obelisk_rt_computed_observer_v1);
    uint64_t dependenciesOffset =
        capturesOffset +
        uint64_t{captureCount} * sizeof(obelisk_rt_computed_capture_v1);
    uint64_t clausesOffset =
        dependenciesOffset +
        uint64_t{dependencyCount} * sizeof(obelisk_rt_computed_dependency_v1);
    uint64_t previousValueOffset =
        clausesOffset +
        uint64_t{primaryCount} * sizeof(obelisk_rt_computed_clause_v1);
    uint64_t previousUnknownOffset = 0;
    uint64_t totalSize =
        previousValueOffset + uint64_t{previousLimbs} * sizeof(uint64_t) * 2;
    if (totalSize > waitSize)
      return operation->emitError(
          "computed observer wait exceeds its canonical frame field");

    auto storeI32 = [&](uint64_t offset, uint32_t value) {
      storeAt(builder, location, wait, offset,
              llvmConstant(builder, location, i32, value), 4);
    };
    auto storeI64 = [&](uint64_t offset, uint64_t value) {
      storeAt(builder, location, wait, offset,
              llvmConstant(builder, location, i64, value), 8);
    };
    storeI32(0, OBELISK_RT_VERSION);
    storeI32(4, OBELISK_RT_SUSPEND_OBSERVER);
    storeI32(8, OBELISK_RT_COMPUTED_WAIT_INTERLEAVED);
    storeI32(12, primaryCount);
    storeI32(16, observerCount);
    storeI32(20, captureCount);
    storeI32(24, dependencyCount);
    storeI32(28, previousLimbs);
    storeI64(32, observersOffset);
    storeI64(40, capturesOffset);
    storeI64(48, dependenciesOffset);
    storeI64(56, clausesOffset);
    storeI64(64, previousValueOffset);
    storeI64(72, previousUnknownOffset);
    storeI64(80, totalSize);
    storeI64(88, 0);

    uint32_t captureCursor = 0;
    uint32_t dependencyCursor = 0;
    uint32_t previousCursor = 0;
    for (auto [index, binding] : llvm::enumerate(bindings)) {
      auto observerID =
          binding->getAttrOfType<IntegerAttr>("obelisk.coro.observer_id");
      auto dependencyKinds = binding->getAttrOfType<DenseI32ArrayAttr>(
          "obelisk.coro.dependency_kinds");
      auto dependencyWidths = binding->getAttrOfType<DenseI32ArrayAttr>(
          "obelisk.coro.dependency_widths");
      if (!observerID || !dependencyKinds || !dependencyWidths ||
          static_cast<size_t>(dependencyKinds.size()) !=
              binding.getDependencies().size() ||
          static_cast<size_t>(dependencyWidths.size()) !=
              binding.getDependencies().size())
        return binding.emitOpError(
            "has malformed converted dependency metadata");
      uint64_t entry =
          observersOffset + index * sizeof(obelisk_rt_computed_observer_v1);
      storeI64(entry, observerID.getValue().getZExtValue());
      storeI32(entry + 8, captureCursor);
      storeI32(entry + 12, binding.getCaptureCount());
      storeI32(entry + 16, dependencyCursor);
      storeI32(entry + 20, binding.getDependencies().size());
      storeI32(entry + 24, index < primaryCount ? static_cast<uint32_t>(
                                                      previousValueOffset +
                                                      uint64_t{previousCursor} *
                                                          sizeof(uint64_t) * 2)
                                                : UINT32_MAX);
      storeI32(entry + 28, 0);
      for (Value capture : binding.getCaptures()) {
        uint64_t captureOffset =
            capturesOffset +
            uint64_t{captureCursor++} * sizeof(obelisk_rt_computed_capture_v1);
        storeAt(builder, location, wait, captureOffset,
                asI64(builder, location, capture), 8);
        storeI64(captureOffset + 8, 0);
        storeI64(captureOffset + 16, 0);
        storeI64(captureOffset + 24, 0);
      }
      for (auto [dependencyIndex, dependency] :
           llvm::enumerate(binding.getDependencies())) {
        uint64_t dependencyOffset =
            dependenciesOffset + uint64_t{dependencyCursor++} *
                                     sizeof(obelisk_rt_computed_dependency_v1);
        storeAt(builder, location, wait, dependencyOffset,
                asI64(builder, location, dependency), 8);
        storeI32(dependencyOffset + 8, dependencyKinds[dependencyIndex]);
        storeI32(dependencyOffset + 12, dependencyWidths[dependencyIndex]);
      }
      if (index < primaryCount)
        previousCursor += (uint64_t{widths[index]} + 63) / 64;
      observerBindings.insert(binding);
    }
    for (uint32_t index = 0; index != primaryCount; ++index) {
      uint64_t clause = clausesOffset +
                        uint64_t{index} * sizeof(obelisk_rt_computed_clause_v1);
      int32_t conditionIndex = observe.getConditionIndices()[index];
      storeI32(clause, index);
      storeI32(clause + 4,
               conditionIndex < 0
                   ? OBELISK_RT_OBSERVER_CONDITION_NONE
                   : primaryCount + static_cast<uint32_t>(conditionIndex));
      storeI32(clause + 8, observe.getEdges()[index]);
      storeI32(clause + 12,
               bindings[index]->hasAttr("obelisk_sim.event_primary")
                   ? OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY
                   : 0);
    }
    for (uint32_t limb = 0; limb != previousLimbs * 2; ++limb)
      storeI64(previousValueOffset + uint64_t{limb} * 8, 0);
    uint64_t initialOperand = primaryCount;
    previousCursor = 0;
    for (uint32_t index = 0; index != primaryCount; ++index) {
      uint32_t planes = planeCounts[index];
      if (planes == 0 || planes > 2 ||
          initialOperand + planes > operation->getNumOperands())
        return operation->emitError(
            "computed observer initial plane inventory is malformed");
      storeAt(builder, location, wait,
              previousValueOffset + uint64_t{previousCursor} * 16,
              operation->getOperand(initialOperand), 1);
      if (planes == 2)
        storeAt(builder, location, wait,
                previousValueOffset + uint64_t{previousCursor} * 16 +
                    ((uint64_t{widths[index]} + 63) / 64) * 8,
                operation->getOperand(initialOperand + 1), 1);
      initialOperand += planes;
      previousCursor += (uint64_t{widths[index]} + 63) / 64;
    }
  } else {
    storeAt(builder, location, wait, 0,
            llvmConstant(builder, location, i32, OBELISK_RT_VERSION), 4);
    storeAt(builder, location, wait, 4,
            llvmConstant(builder, location, i32, kind), 4);
    uint32_t waitFlags = 0;
    if (auto join = dyn_cast<sim::SimSuspendJoinOp>(operation))
      waitFlags = static_cast<uint32_t>(join.getKind());
    else if (isa<sim::SimSuspendLevelOp>(operation))
      waitFlags = OBELISK_RT_WAIT_LEVEL_TRUE;
    else if (isa<sim::SimSuspendEdgeIffOp>(operation))
      waitFlags = OBELISK_RT_WAIT_EDGE_IFF;
    storeAt(builder, location, wait, 8,
            llvmConstant(builder, location, i32, waitFlags), 4);
    storeAt(builder, location, wait, 12,
            llvmConstant(builder, location, i32, count), 4);
    Value payload = llvmConstant(builder, location, i64, 0);
    if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation))
      payload = asI64(builder, location, delay.getDelay());
    storeAt(builder, location, wait, 16, payload, 8);
    storeAt(builder, location, wait, 24,
            llvmConstant(builder, location, i64, 0), 8);

    SmallVector<Value> watched;
    SmallVector<uint32_t> watchedEdges;
    TypeSwitch<Operation *>(operation)
        .Case<sim::SimSuspendChangeOp>([&](auto op) {
          watched.push_back(op.getWatched());
          watchedEdges.push_back(static_cast<uint32_t>(sim::EdgeKind::Change));
        })
        .Case<sim::SimSuspendLevelOp>([&](auto op) {
          watched.push_back(op.getWatched());
          watchedEdges.push_back(static_cast<uint32_t>(sim::EdgeKind::Change));
        })
        .Case<sim::SimSuspendEdgeOp>([&](auto op) {
          watched.push_back(op.getWatched());
          watchedEdges.push_back(static_cast<uint32_t>(op.getEdge()));
        })
        .Case<sim::SimSuspendEdgeIffOp>([&](auto op) {
          watched.push_back(op.getWatched());
          watchedEdges.push_back(static_cast<uint32_t>(op.getEdge()));
          watched.push_back(op.getCondition());
          watchedEdges.push_back(kWaitEdgeNone);
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
        (!waitWidths ||
         static_cast<size_t>(waitWidths.size()) != watched.size()))
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
  }

  storeAt(builder, location, instance, kInstanceContinuationOffset,
          llvmConstant(builder, location, i32, continuationID), 4);
  uint32_t actionFlags = getRuntimeResumeActionFlags(operation);
  if (actionFlags == UINT32_MAX)
    return operation->emitError("has no executable resume region");
  publishAction(builder, location, instance, 1, kind, continuationID,
                OBELISK_RT_ACTION_FRAME_WAIT_RECORD | actionFlags,
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
  for (Operation *binding : observerBindings)
    if (binding->use_empty())
      builder.eraseOp(binding);
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
  Value runtimeContext =
      loadAt(builder, location, instance, kInstanceContextOffset, pointer, 8);
  Value currentContext = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
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
        handle = insertValue(builder, location, handle,
                             llvmConstant(builder, location, i64, stableID), 2);
        Value descriptor =
            LLVM::ZeroOp::create(builder, location, descriptorType);
        descriptor = insertValue(builder, location, descriptor, handle, 0);
        descriptor = insertValue(
            builder, location, descriptor,
            llvmConstant(builder, location, i32, OBELISK_RT_VERSION), 1);
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

constexpr StringLiteral kManagedRootRangeRecordAttr =
    "obelisk.managed_root_range_record";
constexpr StringLiteral kManagedRootRangePushCheckAttr =
    "obelisk.managed_root_range_push_check";

LLVM::AllocaOp findManagedRootRangeRecord(Operation *scope) {
  LLVM::AllocaOp record;
  scope->walk([&](LLVM::AllocaOp allocation) {
    if (!record && allocation->hasAttr(kManagedRootRangeRecordAttr))
      record = allocation;
  });
  return record;
}

void emitManagedRootRangePop(OpBuilder &builder, Location location,
                             Operation *scope) {
  LLVM::AllocaOp record = findManagedRootRangeRecord(scope);
  if (!record)
    return;
  MLIRContext *context = builder.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Value contextAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  Value runtimeContext =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  Value lane = LLVM::CallOp::create(
                   builder, location, TypeRange{pointer},
                   SymbolRefAttr::get(context, "obelisk_rt_v1_gc_current_lane"),
                   runtimeContext)
                   .getResult();
  Value status = LLVM::CallOp::create(
                     builder, location, TypeRange{builder.getI32Type()},
                     SymbolRefAttr::get(
                         context, "obelisk_rt_v1_gc_managed_root_range_pop"),
                     ValueRange{lane, record})
                     .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
      ValueRange{runtimeContext, status});
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
        (function.getEntryKind() != sim::EntryKind::Function &&
         function.getEntryKind() != sim::EntryKind::Observer))
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
      bool pushFailure = check->hasAttr(kManagedRootRangePushCheckAttr);
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
      if (!pushFailure)
        emitManagedRootRangePop(rewriter, function.getLoc(), function);
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
  Value runtimeContext =
      loadAt(builder, location, instance, kInstanceContextOffset, pointer, 8);
  Value currentContext = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
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
    if (slot.hasSecondaryStorage()) {
      Type secondaryType = bodyEntry.getArgument(physicalArgument++).getType();
      arguments.push_back(loadAt(builder, location, frame,
                                 slot.getSecondaryOffset(), secondaryType,
                                 slot.alignment));
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
  if (failed(lowerSimulationTimeOperations(function)))
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
  if (failed(lowerNativeDPICalls(body)))
    return failure();

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
    bool pushFailure = check->hasAttr(kManagedRootRangePushCheckAttr);
    rewriter.eraseOp(check);
    rewriter.setInsertionPointToStart(failure);
    Value bits = runtime::RTStatusToBitsOp::create(
        rewriter, body.getLoc(), rewriter.getI32Type(), status);
    if (!pushFailure)
      emitManagedRootRangePop(rewriter, body.getLoc(), body);
    func::ReturnOp::create(rewriter, body.getLoc(), bits);
  }

  if (failed(makePlainNativeWrappers(module, body, baseName, analysis)))
    return failure();
  return makeProcessDescriptor(module, location, baseName, stableID, analysis);
}

LogicalResult
lowerSuspendableProcess(sim::SimFuncOp function,
                        const SimulationProcessFrameAnalysis &analysis) {
  if (failed(lowerSimulationTimeOperations(function)))
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
  if (failed(lowerNativeDPICalls(ramp)))
    return failure();

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

  // Fixed ABI temporaries and managed-root records were deliberately hoisted
  // to the source function entry. The coroutine ramp adds a new dispatch
  // entry which can resume directly at a continuation shim, bypassing that
  // source block. Move the allocas to the ramp's execute block so they
  // dominate initial execution and every resume and become stable coroutine
  // frame storage instead of per-iteration stack growth.
  SmallVector<LLVM::AllocaOp> activationAllocas;
  SmallVector<LLVM::GEPOp> activationAddresses;
  llvm::SetVector<Operation *> allocaConstants;
  for (LLVM::AllocaOp alloca : oldEntry->getOps<LLVM::AllocaOp>()) {
    activationAllocas.push_back(alloca);
    Operation *count = alloca.getArraySize().getDefiningOp();
    if (count && count->getBlock() == oldEntry && isa<LLVM::ConstantOp>(count))
      allocaConstants.insert(count);
  }
  for (LLVM::GEPOp address : oldEntry->getOps<LLVM::GEPOp>())
    if (auto allocation = address.getBase().getDefiningOp<LLVM::AllocaOp>();
        allocation && llvm::is_contained(activationAllocas, allocation))
      activationAddresses.push_back(address);
  for (LLVM::AllocaOp alloca : activationAllocas)
    alloca->moveBefore(execute, execute->begin());
  for (Operation *constant : allocaConstants)
    constant->moveBefore(execute, execute->begin());
  for (LLVM::GEPOp address : activationAddresses)
    address->moveBefore(execute, execute->end());

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
    if (!block.empty() && sim::isSuspensionOp(block.getTerminator()))
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
      for (uint64_t rootOffset : slot.managedRootOffsets)
        storeAt(builder, location, frame, slot.valueOffset + rootOffset,
                llvmConstant(builder, location, builder.getI64Type(), 0), 8);
      if (slot.hasSecondaryStorage()) {
        Type secondaryType =
            continuation->getArgument(argumentIndex++).getType();
        loaded.push_back(loadAt(builder, location, frame,
                                slot.getSecondaryOffset(), secondaryType,
                                slot.alignment));
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
    if (slot.hasSecondaryStorage()) {
      BlockArgument secondaryArgument =
          oldEntry->getArgument(physicalArgument++);
      Type secondaryType = secondaryArgument.getType();
      entryArguments.push_back(loadAt(builder, location, frame,
                                      slot.getSecondaryOffset(), secondaryType,
                                      slot.alignment));
      refreshFrameArgument(secondaryArgument, secondaryType,
                           slot.getSecondaryOffset(), slot.alignment);
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
    if (sim::isSuspensionOp(operation) || isa<sim::SimReturnOp>(operation))
      terminators.push_back(operation);
  });
  for (Operation *operation : terminators) {
    if (sim::isSuspensionOp(operation)) {
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
    bool pushFailure = check->hasAttr(kManagedRootRangePushCheckAttr);
    check.erase();
    builder.setInsertionPointToStart(failure);
    Value bits = runtime::RTStatusToBitsOp::create(
        builder, location, builder.getI32Type(), status);
    storeAt(builder, location, instance, kInstanceStatusOffset, bits, 4);
    if (!pushFailure)
      emitManagedRootRangePop(builder, location, ramp);
    cf::BranchOp::create(builder, location, blocks.terminate);
  }
  if (failed(makeNativeWrappers(module, ramp, baseName)))
    return failure();
  return makeProcessDescriptor(module, location, baseName, stableID, analysis);
}

LogicalResult makeNativeObserverThunk(ModuleOp module,
                                      LLVM::LLVMFuncOp evaluator,
                                      uint32_t resultWidth, bool fourState) {
  MLIRContext *context = module.getContext();
  Location location = evaluator.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  ArrayRef<Type> inputs = evaluator.getFunctionType().getParams();
  if (inputs.empty() || inputs.front() != pointer || resultWidth == 0)
    return evaluator.emitError("observer evaluator has an invalid native ABI");
  uint32_t captureCount = inputs.size() - 1;
  if (!llvm::all_of(inputs.drop_front(),
                    [&](Type type) { return type == i64; }))
    return evaluator.emitError(
        "observer captures must lower to stable 64-bit handles");
  unsigned expectedResults = fourState ? 2 : 1;
  SmallVector<Type> results;
  Type returnType = evaluator.getFunctionType().getReturnType();
  if (auto aggregate = dyn_cast<LLVM::LLVMStructType>(returnType))
    llvm::append_range(results, aggregate.getBody());
  else if (!isa<LLVM::LLVMVoidType>(returnType))
    results.push_back(returnType);
  unsigned resultCount = results.size();
  bool returnsStatus = resultCount == expectedResults + 1;
  if (resultCount != expectedResults && !returnsStatus)
    return evaluator.emitError(
        "observer result plane count does not match its descriptor");
  for (Type type : ArrayRef<Type>(results).take_front(expectedResults)) {
    auto integer = dyn_cast<IntegerType>(type);
    auto floating = dyn_cast<FloatType>(type);
    if ((!integer && !floating) ||
        (integer ? integer.getWidth() : floating.getWidth()) != resultWidth)
      return evaluator.emitError(
          "observer result plane width does not match its descriptor");
  }

  OpBuilder builder(module.getContext());
  std::string thunkName =
      (evaluator.getSymName() + ".__obelisk_observer").str();
  auto thunk = module.lookupSymbol<LLVM::LLVMFuncOp>(thunkName);
  if (!thunk)
    return evaluator.emitError(
        "native observer thunk declaration was not materialized");
  if (!thunk.getBody().empty())
    return evaluator.emitError("native observer thunk is defined twice");
  Block *entry = thunk.addEntryBlock(builder);
  builder.setInsertionPointToStart(entry);
  SmallVector<Value> arguments{entry->getArgument(0)};
  Value captures = entry->getArgument(1);
  for (uint32_t index = 0; index != captureCount; ++index)
    arguments.push_back(LLVM::LoadOp::create(
        builder, location, i64,
        byteGEP(builder, location, captures, uint64_t{index} * 8), 8));
  auto call = LLVM::CallOp::create(
      builder, location, TypeRange{returnType},
      SymbolRefAttr::get(context, evaluator.getSymName()), arguments);
  auto resultAt = [&](unsigned index) -> Value {
    if (resultCount == 1)
      return call.getResult();
    return LLVM::ExtractValueOp::create(
        builder, location, results[index], call.getResult(),
        ArrayRef<int64_t>{static_cast<int64_t>(index)});
  };
  uint32_t limbs = (resultWidth + 63) / 64;
  Value zero = llvmConstant(builder, location, i64, 0);
  for (uint32_t index = 0; index != limbs; ++index) {
    LLVM::StoreOp::create(
        builder, location, zero,
        byteGEP(builder, location, entry->getArgument(3), uint64_t{index} * 8),
        8);
    LLVM::StoreOp::create(
        builder, location, zero,
        byteGEP(builder, location, entry->getArgument(4), uint64_t{index} * 8),
        8);
  }
  LLVM::StoreOp::create(builder, location, resultAt(0), entry->getArgument(3),
                        1);
  if (fourState)
    LLVM::StoreOp::create(builder, location, resultAt(1), entry->getArgument(4),
                          1);
  Value status = returnsStatus ? resultAt(expectedResults)
                               : llvmConstant(builder, location, i32, 0);
  LLVM::ReturnOp::create(builder, location, status);
  return success();
}

LogicalResult materializeNativeObserverThunks(ModuleOp module) {
  SmallVector<LLVM::LLVMFuncOp> evaluators;
  module.walk([&](LLVM::LLVMFuncOp function) {
    if (function->hasAttr("obelisk.observer_width"))
      evaluators.push_back(function);
  });
  for (LLVM::LLVMFuncOp evaluator : evaluators) {
    auto width =
        evaluator->getAttrOfType<IntegerAttr>("obelisk.observer_width");
    auto fourState =
        evaluator->getAttrOfType<BoolAttr>("obelisk.observer_four_state");
    if (!width || !fourState ||
        failed(makeNativeObserverThunk(
            module, evaluator,
            static_cast<uint32_t>(width.getValue().getZExtValue()),
            fourState.getValue())))
      return failure();
  }
  return success();
}

LogicalResult lowerOrdinaryFunction(sim::SimFuncOp function) {
  if (failed(lowerSimulationTimeOperations(function)))
    return failure();
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
  bool observer = function.getEntryKind() == sim::EntryKind::Observer;
  auto observerWidth =
      function->getAttrOfType<IntegerAttr>("obelisk_sim.observer_width");
  auto observerFourState =
      function->getAttrOfType<BoolAttr>("obelisk_sim.observer_four_state");
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
  if (failed(lowerNativeDPICalls(replacement)))
    return failure();
  IRRewriter rewriter(replacement.getContext());
  SmallVector<Operation *> operations;
  replacement.walk([&](Operation *operation) {
    if (isa<sim::SimReturnOp, sim::SimCallOp, sim::SimSpawnOp>(operation))
      operations.push_back(operation);
  });
  for (Operation *operation : operations) {
    rewriter.setInsertionPoint(operation);
    if (auto returnOp = dyn_cast<sim::SimReturnOp>(operation)) {
      func::ReturnOp::create(rewriter, returnOp.getLoc(),
                             returnOp.getOperands());
      rewriter.eraseOp(returnOp);
    } else if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
      SmallVector<Type> convertedResults;
      for (Type type : call.getResultTypes())
        convertedResults.push_back(
            convertProcessType(type, replacement.getContext()));
      auto converted =
          func::CallOp::create(rewriter, call.getLoc(), call.getCallee(),
                               convertedResults, call.getOperands());
      rewriter.replaceOp(call, converted.getResults());
    } else {
      auto spawn = cast<sim::SimSpawnOp>(operation);
      auto converted = LLVM::CallOp::create(
          rewriter, spawn.getLoc(), TypeRange{rewriter.getI64Type()},
          SymbolRefAttr::get(rewriter.getContext(),
                             (spawn.getCallee() + ".__obelisk_spawn").str()),
          spawn.getOperands());
      rewriter.replaceOp(spawn, converted.getResults());
    }
  }
  if (observer) {
    if (!observerWidth || !observerFourState)
      return replacement.emitError(
          "observer entry is missing native descriptor metadata");
    replacement->setAttr(
        "obelisk.observer_width",
        builder.getI32IntegerAttr(observerWidth.getValue().getZExtValue()));
    replacement->setAttr("obelisk.observer_four_state",
                         builder.getBoolAttr(observerFourState.getValue()));
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
  // Suspension lowering may already have made some values explicit successor
  // operands. Record those lanes before finding external uses so a value does
  // not acquire a second continuation argument when it is also live through a
  // later ordinary CFG edge.
  for (Block &block : llvm::drop_begin(function.getBody())) {
    for (auto [argumentIndex, argument] :
         llvm::enumerate(block.getArguments())) {
      Value incomingValue;
      bool commonIncoming = true;
      bool sawIncoming = false;
      for (Block &predecessor : function.getBody()) {
        auto branch = dyn_cast<BranchOpInterface>(predecessor.getTerminator());
        if (!branch)
          continue;
        for (auto [successorIndex, successor] :
             llvm::enumerate(predecessor.getSuccessors())) {
          if (successor != &block)
            continue;
          SuccessorOperands operands =
              branch.getSuccessorOperands(successorIndex);
          if (argumentIndex >= operands.size() ||
              operands.isOperandProduced(argumentIndex)) {
            commonIncoming = false;
            break;
          }
          Value value = operands[argumentIndex];
          if (!sawIncoming) {
            incomingValue = value;
            sawIncoming = true;
          } else if (incomingValue != value) {
            commonIncoming = false;
            break;
          }
        }
        if (!commonIncoming)
          break;
      }
      if (sawIncoming && commonIncoming)
        threadedValues[&block].try_emplace(incomingValue, argument);
    }
  }

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
    SmallVector<uint64_t, 2> managedRootOffsets;
  };
  struct Net {
    uint64_t id;
    uint32_t handleID;
    uint64_t offset;
    unsigned width;
    bool fourState;
    sim::NetResolutionKind resolution;
  };
  struct Driver {
    uint64_t id;
    uint64_t netId;
    uint32_t handleID;
    uint64_t offset;
    unsigned width;
    unsigned drivenLow;
    unsigned drivenWidth;
  };
  DenseMap<uint64_t, uint64_t> storage;
  DenseMap<uint64_t, uint64_t> nets;
  DenseMap<uint64_t, uint64_t> drivers;
  DenseSet<uint32_t> directHandles;
  DenseSet<uint32_t> guardedHandles;
  DenseSet<uint32_t> nbaHandles;
  DenseSet<uint32_t> transitionHandles;
  bool transitionHandlesExact = false;
  SmallVector<Bound> bounds;
  SmallVector<Net> netLayouts;
  SmallVector<Driver> driverLayouts;
  DenseMap<std::pair<uint64_t, uint64_t>, std::pair<uint64_t, uint64_t>>
      connectivityCanonical;
  DenseMap<std::pair<uint64_t, uint64_t>,
           SmallVector<::obelisk::analysis::NetBit>>
      connectivityComponents;
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
    SmallVector<uint64_t, 2> managedRootOffsets;
    if (!sim::getManagedHandleOffsets(type, managedRootOffsets))
      return failure();
    if (!managedRootOffsets.empty()) {
      uint64_t aligned;
      if (!alignUp(layout.bitCount, 64, aligned))
        return failure();
      layout.bitCount = aligned;
    }
    offset = layout.bitCount;
    if (layout.bitCount > std::numeric_limits<uint64_t>::max() - *width)
      return failure();
    layout.bitCount += *width;
    handle = encodeNativeStaticHandle(nextHandleID);
    layout.bounds.push_back(
        {nextHandleID++, offset, *width, std::move(managedRootOffsets)});
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
                                   containsLogic(declaration.getType()),
                                   declaration.getResolutionKind()});
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
      uint64_t drivenLow =
          declaration.getDrivenLowAttr()
              ? declaration.getDrivenLowAttr().getValue().getZExtValue()
              : 0;
      uint64_t drivenWidth =
          declaration.getDrivenWidthAttr()
              ? declaration.getDrivenWidthAttr().getValue().getZExtValue()
              : *width;
      if (drivenLow > *width || drivenWidth > *width - drivenLow) {
        declaration.emitError("native driver has an invalid driven range");
        return WalkResult::interrupt();
      }
      layout.drivers[declaration.getId()] = handle;
      layout.driverLayouts.push_back(
          {declaration.getId(), declaration.getNetId(), nextHandleID - 1,
           offset, *width, static_cast<unsigned>(drivenLow),
           static_cast<unsigned>(drivenWidth)});
    }
    return WalkResult::advance();
  });
  if (walked.wasInterrupted())
    return failure();
  SmallVector<sim::SimDesignOp> designs;
  module.walk([&](sim::SimDesignOp design) { designs.push_back(design); });
  if (designs.size() > 1) {
    module.emitError("native lowering requires at most one simulation design");
    return failure();
  }
  if (!designs.empty()) {
    ::obelisk::analysis::NetConnectivityAnalysis connectivity(designs.front());
    for (const NativeStateLayout::Net &net : layout.netLayouts) {
      for (uint64_t bit = 0; bit != net.width; ++bit) {
        ArrayRef<::obelisk::analysis::NetBit> component =
            connectivity.getComponent({net.id, bit});
        if (component.size() <= 1)
          continue;
        std::pair<uint64_t, uint64_t> key{net.id, bit};
        std::pair<uint64_t, uint64_t> canonical{component.front().net,
                                                component.front().offset};
        layout.connectivityCanonical[key] = canonical;
        if (key == canonical)
          llvm::append_range(layout.connectivityComponents[canonical],
                             component);
      }
    }

    DenseMap<std::pair<uint64_t, uint64_t>, uint64_t> uwireDrivers;
    for (const NativeStateLayout::Driver &driver : layout.driverLayouts) {
      auto target = llvm::find_if(layout.netLayouts, [&](const auto &net) {
        return net.id == driver.netId;
      });
      if (target == layout.netLayouts.end() ||
          target->resolution != sim::NetResolutionKind::UWire)
        continue;
      for (uint64_t bit = driver.drivenLow;
           bit != uint64_t{driver.drivenLow} + driver.drivenWidth; ++bit) {
        ArrayRef<::obelisk::analysis::NetBit> component =
            connectivity.getComponent({driver.netId, bit});
        ::obelisk::analysis::NetBit canonical =
            component.empty() ? ::obelisk::analysis::NetBit{driver.netId, bit}
                              : component.front();
        if (++uwireDrivers[{canonical.net, canonical.offset}] > 1) {
          module.emitError()
              << "uwire connectivity component " << canonical.net << "["
              << canonical.offset << "] has more than one driver";
          return failure();
        }
      }
    }
  }
  if (layout.bitCount >= OBELISK_RT_STABLE_HANDLE_STATIC_TAG) {
    module.emitError("native static state exceeds the handle address space");
    return failure();
  }
  // Keep one byte addressable so poison-free invalid-handle paths always have
  // a safe GEP base even for a design with no state.
  layout.bitCount = std::max<uint64_t>(layout.bitCount, 8);
  return layout;
}

struct NativeSchedulePlan {
  uint32_t initialRank = UINT32_MAX;
  SmallVector<std::pair<uint32_t, uint32_t>> continuations;
  SmallVector<uint32_t> bytecodeContinuations;
  std::optional<uint32_t> actorSlot;
};

LogicalResult
specializeNativeAOTCaptures(ModuleOp module,
                            const analysis::NativeAOTAnalysis &eligibility) {
  sim::SimFuncOp root;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      root = function;
  });
  if (!root)
    return module.emitError(
        "cannot specialize AOT captures without a root initializer");

  WalkResult specialized = root.walk([&](sim::SimSpawnOp spawn) {
    sim::SimDesignOp design = spawn->getParentOfType<sim::SimDesignOp>();
    sim::SimFuncOp target =
        design ? design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee())
               : nullptr;
    if (!target ||
        !eligibility.getActorSlots().contains(target.getOperation()))
      return WalkResult::advance();
    Block &entry = target.getBody().front();
    if (spawn.getNumOperands() != entry.getNumArguments()) {
      spawn.emitOpError("AOT capture specialization found an invalid arity");
      return WalkResult::interrupt();
    }
    if (entry.getNumArguments() == 0 ||
        !isa<sim::ContextType>(entry.getArgument(0).getType())) {
      target.emitOpError(
          "AOT capture specialization requires a context entry capture");
      return WalkResult::interrupt();
    }

    for (unsigned index = 1; index != entry.getNumArguments(); ++index) {
      Operation *producer = spawn.getOperand(index).getDefiningOp();
      if (!producer ||
          !isa<sim::SimContextStorageOp, sim::SimContextNetOp,
               sim::SimContextDriverOp, sim::SimContextEventOp>(producer))
        continue;
      if (producer->getNumOperands() != 1 ||
          producer->getOperand(0) != spawn.getOperand(0) ||
          producer->getNumResults() != 1 ||
          producer->getResult(0) != spawn.getOperand(index))
        continue;

      SmallVector<OpOperand *> uses;
      for (OpOperand &use : entry.getArgument(index).getUses())
        uses.push_back(&use);
      DenseMap<Block *, Value> specializedByBlock;
      for (OpOperand *use : uses) {
        Block *block = use->getOwner()->getBlock();
        auto [position, inserted] =
            specializedByBlock.try_emplace(block, Value{});
        if (inserted) {
          OpBuilder builder(target.getContext());
          builder.setInsertionPointToStart(block);
          IRMapping mapping;
          mapping.map(spawn.getOperand(0), entry.getArgument(0));
          position->second = builder.clone(*producer, mapping)->getResult(0);
        }
        use->set(position->second);
      }
    }
    return WalkResult::advance();
  });
  return specialized.wasInterrupted() ? failure() : success();
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
  auto handleConstant = handle.getDefiningOp<arith::ConstantOp>();
  auto offsetConstant = offset.getDefiningOp<arith::ConstantOp>();
  auto handleValue = handleConstant
                         ? dyn_cast<IntegerAttr>(handleConstant.getValue())
                         : IntegerAttr{};
  auto offsetValue = offsetConstant
                         ? dyn_cast<IntegerAttr>(offsetConstant.getValue())
                         : IntegerAttr{};
  if (handleValue && offsetValue) {
    uint64_t folded =
        obelisk_rt_stable_handle_offset(handleValue.getValue().getZExtValue(),
                                        offsetValue.getValue().getSExtValue());
    return arith::ConstantOp::create(
        builder, location, builder.getI64Type(),
        builder.getI64IntegerAttr(static_cast<int64_t>(folded)));
  }
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

struct DirectStaticStateRange {
  uint64_t offset;
  uint64_t localOffset;
  uint32_t staticID;
  bool guarded;
};

std::optional<uint64_t> resolveCFGConstantInteger(Value value);

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
  return DirectStaticStateRange{bound->offset +
                                    static_cast<uint64_t>(decoded.offset),
                                static_cast<uint64_t>(decoded.offset),
                                decoded.id, guarded};
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
                             StringRef globalName, uint64_t bitOffset) {
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
  Value old = extractDirectPackedPlane(builder, location, plane, inputType);
  return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne, old,
                               input);
}

void recordStaticSpecializationCFGBlocks(ConversionPatternRewriter &rewriter,
                                         Block *head, unsigned newBlockCount);

void markLikelyTrue(cf::CondBranchOp branch) {
  constexpr int32_t hot = (1 << 20) - 1;
  constexpr int32_t cold = 1;
  branch.setBranchWeights(ArrayRef<int32_t>{hot, cold});
}

Value loadStaticSpecializationFast(ConversionPatternRewriter &builder,
                                   Location location) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  IntegerType i32 = builder.getI32Type();
  Value fastAddress = LLVM::AddressOfOp::create(
      builder, location, pointer, "__obelisk_static_specialization_fast_v1");
  Value fast = LLVM::LoadOp::create(builder, location, i32, fastAddress, 4);
  auto useFast =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne, fast,
                            llvmConstant(builder, location, i32, 0));
  return useFast;
}

/// Prove which NBA enqueues in a closed-world fused activation may use the
/// clean native body selected by AOT actor dispatch.
struct StaticNBADestination {
  uint32_t staticID;
  uint64_t offset;
};

/// Resolve the same fixed reference forms that native handle conversion folds
/// to a constant stable handle. Dynamic and CFG-selected references
/// deliberately do not satisfy this proof.
std::optional<StaticNBADestination>
resolveStaticNBADestination(Value value, const NativeStateLayout &layout,
                            DenseSet<Value> &active) {
  if (!value || !active.insert(value).second)
    return std::nullopt;
  auto finish = [&](std::optional<StaticNBADestination> result) {
    active.erase(value);
    return result;
  };
  auto addOffset = [&](std::optional<StaticNBADestination> base,
                       uint64_t offset) -> std::optional<StaticNBADestination> {
    if (!base || offset > std::numeric_limits<uint64_t>::max() - base->offset)
      return std::nullopt;
    base->offset += offset;
    return base;
  };
  auto resolveDescriptor =
      [&](uint64_t descriptor) -> std::optional<StaticNBADestination> {
    auto handle = layout.storage.find(descriptor);
    if (handle == layout.storage.end())
      return std::nullopt;
    obelisk_rt_stable_handle_v1 decoded{};
    if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
        decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset < 0)
      return std::nullopt;
    return StaticNBADestination{decoded.id,
                                static_cast<uint64_t>(decoded.offset)};
  };

  if (auto argument = dyn_cast<BlockArgument>(value)) {
    auto function =
        dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
    if (!function)
      return finish(std::nullopt);
    // Capture specialization has already replaced every direct context
    // descriptor with a SimContextStorageOp. A surviving entry argument may
    // be a view or another runtime-selected handle; descriptor provenance does
    // not prove that native lowering will materialize it as a constant.
    if (argument.getOwner() == &function.getBody().front())
      return finish(std::nullopt);

    std::optional<StaticNBADestination> resolved;
    Block *block = argument.getOwner();
    for (Block *predecessor : block->getPredecessors()) {
      Operation *terminator = predecessor->getTerminator();
      auto branch = dyn_cast<BranchOpInterface>(terminator);
      if (!branch)
        return finish(std::nullopt);
      for (unsigned successor = 0; successor != terminator->getNumSuccessors();
           ++successor) {
        if (terminator->getSuccessor(successor) != block)
          continue;
        SuccessorOperands operands = branch.getSuccessorOperands(successor);
        unsigned index = argument.getArgNumber();
        if (index >= operands.size() || operands.isOperandProduced(index))
          return finish(std::nullopt);
        std::optional<StaticNBADestination> incoming =
            resolveStaticNBADestination(operands[index], layout, active);
        if (!incoming ||
            (resolved && (resolved->staticID != incoming->staticID ||
                          resolved->offset != incoming->offset)))
          return finish(std::nullopt);
        resolved = incoming;
      }
    }
    return finish(resolved);
  }

  if (auto storage = value.getDefiningOp<sim::SimContextStorageOp>())
    return finish(resolveDescriptor(storage.getId()));
  if (auto view = value.getDefiningOp<sim::SimRefExtractOp>())
    return finish(
        addOffset(resolveStaticNBADestination(view.getInput(), layout, active),
                  view.getLowBit()));
  if (auto view = value.getDefiningOp<sim::SimRefSubelementOp>()) {
    uint64_t offset = 0;
    Type type = cast<sim::RefType>(view.getInput().getType()).getElementType();
    for (int64_t index : view.getIndices()) {
      if (index < 0)
        return finish(std::nullopt);
      auto child = sim::getAggregateProvenanceSubelement(
          type, static_cast<unsigned>(index));
      if (!child ||
          child->first > std::numeric_limits<uint64_t>::max() - offset)
        return finish(std::nullopt);
      offset += child->first;
      type = sim::getAggregateElementType(type, static_cast<unsigned>(index));
    }
    return finish(addOffset(
        resolveStaticNBADestination(view.getInput(), layout, active), offset));
  }
  return finish(std::nullopt);
}

std::optional<StaticNBADestination>
resolveStaticNBADestination(Value value, const NativeStateLayout &layout) {
  DenseSet<Value> active;
  return resolveStaticNBADestination(value, layout, active);
}

bool isNonInvalidatingStaticNBA(
    sim::SimNBAEnqueueOp op,
    const DenseMap<uint64_t, uint32_t> &staticNBASiteRoots,
    ArrayRef<obelisk_rt_static_nba_root> staticNBARoots,
    const NativeStateLayout &stateLayout) {
  sim::NBASiteAttr site = op.getSiteAttr();
  std::optional<unsigned> width = nativeStateWidth(op.getValue().getType());
  if (!site || !width || *width > 64 || op.getDelay() || site.getTiming() ||
      site.getStorage() == sim::ComputeNBAStorageKind::DynamicFrontier)
    return false;
  auto planned = staticNBASiteRoots.find(site.getId());
  if (planned == staticNBASiteRoots.end() ||
      planned->second >= staticNBARoots.size())
    return false;
  std::optional<StaticNBADestination> destination =
      resolveStaticNBADestination(op.getDestination(), stateLayout);
  const obelisk_rt_static_nba_root &root = staticNBARoots[planned->second];
  return destination && destination->staticID == root.static_state &&
         destination->offset <= root.bit_width &&
         *width <= root.bit_width - destination->offset;
}

LogicalResult markCleanStaticNBAsInGuardedBodies(
    ModuleOp module, bool enabled,
    const DenseMap<uint64_t, uint32_t> &staticNBASiteRoots,
    ArrayRef<obelisk_rt_static_nba_root> staticNBARoots,
    const NativeStateLayout &stateLayout) {
  SmallVector<sim::SimFuncOp> functions;
  module.walk([&](sim::SimFuncOp function) {
    if (function->hasAttr(sim::metadata::nativeGuardedSpecializationBody))
      functions.push_back(function);
  });

  for (sim::SimFuncOp function : functions) {
    function->removeAttr(sim::metadata::nativeGuardedSpecializationBody);
    if (!enabled)
      continue;

    Operation *suspension = nullptr;
    bool multipleSuspensions = false;
    function.walk([&](Operation *operation) {
      if (!sim::isSuspensionOp(operation))
        return;
      multipleSuspensions |= suspension != nullptr;
      suspension = operation;
    });
    if (multipleSuspensions || !suspension ||
        suspension->getNumSuccessors() != 1)
      return function.emitOpError(
                 "has invalid guarded-specialization activation structure"),
             failure();

    Block *activationEntry = suspension->getSuccessor(0);
    if (activationEntry == &function.getBody().front() ||
        activationEntry->getParent() != &function.getBody())
      return function.emitOpError(
                 "has invalid guarded-specialization continuation"),
             failure();

    // The runtime selects native or bytecode execution at this activation
    // boundary. Keep a single native body and make it the clean form; dirty
    // actors and globally slow environments never enter it.
    SmallVector<Block *> activationBlocks;
    SmallVector<Block *> pending{activationEntry};
    llvm::SmallPtrSet<Block *, 16> visited;
    Block *suspensionBlock = suspension->getBlock();
    while (!pending.empty()) {
      Block *block = pending.pop_back_val();
      if (block == suspensionBlock || !visited.insert(block).second)
        continue;
      if (block->getParent() != &function.getBody())
        return function.emitOpError(
                   "guarded-specialization body leaves its process region"),
               failure();
      if (llvm::any_of(*block, [](Operation &operation) {
            return sim::isSuspensionOp(&operation);
          }))
        return function.emitOpError(
                   "guarded-specialization body contains a suspension"),
               failure();
      activationBlocks.push_back(block);
      for (Block *successor : block->getSuccessors())
        if (successor != suspensionBlock)
          pending.push_back(successor);
    }
    if (activationBlocks.empty())
      return function.emitOpError("has an empty guarded-specialization body"),
             failure();

    // A writable-VPI handover cannot race an activation. NBA lowering has an
    // additional slot-local invariant: a generic enqueue claims its root's
    // slow path for the rest of the slot. Only elide per-site NBA guards when
    // every enqueue reachable in this activation is statically staged and
    // therefore cannot invalidate that invariant mid-activation.
    bool nbaActivationIsNonInvalidating = true;
    for (Block *block : activationBlocks)
      block->walk([&](sim::SimNBAEnqueueOp nba) {
        nbaActivationIsNonInvalidating &= isNonInvalidatingStaticNBA(
            nba, staticNBASiteRoots, staticNBARoots, stateLayout);
      });

    for (Block *source : activationBlocks)
      for (Operation &operation : *source)
        operation.walk([&](Operation *nested) {
          if (nbaActivationIsNonInvalidating &&
              isa<sim::SimNBAEnqueueOp>(nested))
            nested->setAttr(kAssumeCleanSpecializationAttr,
                            UnitAttr::get(function.getContext()));
        });
  }
  return success();
}

Value staticSpecializationGuard(ConversionPatternRewriter &builder,
                                Location location, uint32_t staticID,
                                uint32_t flags) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  IntegerType i32 = builder.getI32Type();
  Block *head = builder.getInsertionBlock();
  Block *continuation = builder.splitBlock(head, builder.getInsertionPoint());
  BlockArgument result =
      continuation->addArgument(builder.getI1Type(), location);
  Region *region = head->getParent();
  Block *slowBlock = builder.createBlock(region, continuation->getIterator());
  recordStaticSpecializationCFGBlocks(builder, head, 2);

  builder.setInsertionPointToEnd(head);
  Value useFast = loadStaticSpecializationFast(builder, location);
  Value fastAllowed = llvmConstant(builder, location, builder.getI1Type(), 1);
  markLikelyTrue(cf::CondBranchOp::create(builder, location, useFast,
                                          continuation, ValueRange{fastAllowed},
                                          slowBlock, ValueRange{}));

  builder.setInsertionPointToEnd(slowBlock);
  Value contextAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  Value context =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  Value allowed =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(builder.getContext(),
                             "obelisk_rt_v1_static_specialization_guard"),
          ValueRange{context, llvmConstant(builder, location, i32, UINT32_MAX),
                     llvmConstant(builder, location, i32, staticID),
                     llvmConstant(builder, location, i32, flags)})
          .getResult();
  Value useDirect =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                            allowed, llvmConstant(builder, location, i32, 0));
  cf::BranchOp::create(builder, location, continuation, ValueRange{useDirect});

  builder.setInsertionPointToStart(continuation);
  return result;
}

Value staticNBASpecializationGuard(ConversionPatternRewriter &builder,
                                   Location location, uint32_t rootIndex) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  IntegerType i32 = builder.getI32Type();
  Block *head = builder.getInsertionBlock();
  Block *continuation = builder.splitBlock(head, builder.getInsertionPoint());
  BlockArgument result =
      continuation->addArgument(builder.getI1Type(), location);
  Region *region = head->getParent();
  Block *slowBlock = builder.createBlock(region, continuation->getIterator());
  recordStaticSpecializationCFGBlocks(builder, head, 2);

  builder.setInsertionPointToEnd(head);
  Value useFast = loadStaticSpecializationFast(builder, location);
  Value fastAllowed = llvmConstant(builder, location, builder.getI1Type(), 1);
  markLikelyTrue(cf::CondBranchOp::create(builder, location, useFast,
                                          continuation, ValueRange{fastAllowed},
                                          slowBlock, ValueRange{}));

  builder.setInsertionPointToEnd(slowBlock);
  Value contextAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  Value context =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  Value allowed =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(builder.getContext(),
                             "obelisk_rt_v1_static_nba_specialization_guard"),
          ValueRange{context, llvmConstant(builder, location, i32, rootIndex)})
          .getResult();
  Value useDirect =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                            allowed, llvmConstant(builder, location, i32, 0));
  cf::BranchOp::create(builder, location, continuation, ValueRange{useDirect});

  builder.setInsertionPointToStart(continuation);
  return result;
}

void recordStaticSpecializationCFGBlocks(ConversionPatternRewriter &rewriter,
                                         Block *head, unsigned newBlockCount) {
  auto function = dyn_cast<sim::SimFuncOp>(head->getParentOp());
  if (!function)
    return;
  auto metadata =
      function->getAttrOfType<ArrayAttr>(kNativeTwoStateBlockUnknownsAttr);
  if (!metadata)
    return;
  unsigned headIndex = static_cast<unsigned>(
      std::distance(function.getBody().begin(), head->getIterator()));
  SmallVector<Attribute> entries(metadata.begin(), metadata.end());
  entries.insert(entries.begin() + headIndex + 1, newBlockCount,
                 rewriter.getDenseI64ArrayAttr({}));
  rewriter.modifyOpInPlace(function, [&] {
    function->setAttr(kNativeTwoStateBlockUnknownsAttr,
                      rewriter.getArrayAttr(entries));
  });
}

Value loadStatePlane(ConversionPatternRewriter &rewriter, Location location,
                     Value handle, IntegerType resultType, StringRef globalName,
                     bool unknownFallback, uint64_t stateBitCount,
                     const NativeStateLayout *directLayout = nullptr,
                     Value guardedPermission = {}, bool assumeClean = false) {
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
                      const NativeStateLayout *directLayout = nullptr,
                      Value guardedPermission = {}, bool assumeClean = false) {
  IntegerType inputType = cast<IntegerType>(input.getType());
  std::optional<DirectStaticStateRange> range =
      resolveDirectStaticStateRange(handle, inputType.getWidth(), directLayout);
  if (range && (!range->guarded || assumeClean))
    return storeDirectPackedPlane(rewriter, location, input, globalName,
                                  range->offset);

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
  Value directChanged = storeDirectPackedPlane(rewriter, location, input,
                                               globalName, range->offset);
  cf::BranchOp::create(rewriter, location, continuation,
                       ValueRange{directChanged});

  rewriter.setInsertionPointToEnd(genericBlock);
  Value genericChanged = emitGeneric();
  cf::BranchOp::create(rewriter, location, continuation,
                       ValueRange{genericChanged});

  rewriter.setInsertionPointToStart(continuation);
  return changed;
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
                  Value newValue, Value newUnknown,
                  std::optional<DirectStaticStateRange> directRange =
                      std::nullopt);
sim::SimStatusCheckOp reportManagedStatus(OpBuilder &builder, Location location,
                                          Value context, Value status);

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
                    uint64_t stateBitCount,
                    const NativeStateLayout *directLayout)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        directLayout(directLayout) {}
  LogicalResult
  matchAndRewrite(sim::SimRefLoadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = op.getResult().getType();
    std::optional<unsigned> width = nativeStateWidth(resultType);
    if (!width || adaptor.getReference().size() != 1)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    bool assumeClean = op->hasAttr(kAssumeCleanSpecializationAttr);
    Value guardedPermission;
    if (auto range = resolveDirectStaticStateRange(
            adaptor.getReference().front(), *width, directLayout);
        range && range->guarded && !assumeClean)
      guardedPermission = staticSpecializationGuard(
          rewriter, op.getLoc(), range->staticID, OBELISK_RT_STATIC_ROOT_READ);
    Value value =
        loadStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                       plane, "__obelisk_state_value", false, stateBitCount,
                       directLayout, guardedPermission, assumeClean);
    if (isa<FloatType>(resultType))
      value =
          arith::BitcastOp::create(rewriter, op.getLoc(), resultType, value);
    SmallVector<Value> converted{value};
    if (containsLogic(resultType))
      converted.push_back(
          loadStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                         plane, "__obelisk_state_unknown", true, stateBitCount,
                         directLayout, guardedPermission, assumeClean));
    SmallVector<ValueRange> replacements{ValueRange(converted)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStateLayout *directLayout;
};

class RefStoreConversion final
    : public OpConversionPattern<sim::SimRefStoreOp> {
public:
  RefStoreConversion(const TypeConverter &converter, MLIRContext *context,
                     uint64_t stateBitCount,
                     const NativeStateLayout *directLayout)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        directLayout(directLayout) {}
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
    bool assumeClean = op->hasAttr(kAssumeCleanSpecializationAttr);
    std::optional<DirectStaticStateRange> directRange =
        resolveDirectStaticStateRange(adaptor.getReference().front(), *width,
                                      directLayout);
    Value guardedPermission;
    if (directRange && directRange->guarded && !assumeClean)
      guardedPermission = staticSpecializationGuard(
          rewriter, op.getLoc(), directRange->staticID,
          OBELISK_RT_STATIC_ROOT_READ | OBELISK_RT_STATIC_ROOT_WRITE);
    Value oldValue =
        loadStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                       plane, "__obelisk_state_value", false, stateBitCount,
                       directLayout, guardedPermission, assumeClean);
    Value oldUnknown;
    if (containsLogic(valueType))
      oldUnknown =
          loadStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                         plane, "__obelisk_state_unknown", true, stateBitCount,
                         directLayout, guardedPermission, assumeClean);
    Value storedValue = adaptor.getValue().front();
    if (isa<FloatType>(valueType))
      storedValue =
          arith::BitcastOp::create(rewriter, op.getLoc(), plane, storedValue);
    Value notificationValue = storedValue;
    if (isa<sim::StringType>(valueType)) {
      Value comparison =
          LLVM::CallOp::create(
              rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_string_compare"),
              ValueRange{oldValue, storedValue})
              .getResult();
      Value equal = arith::CmpIOp::create(
          rewriter, op.getLoc(), arith::CmpIPredicate::eq, comparison,
          llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(), 0));
      notificationValue = arith::SelectOp::create(rewriter, op.getLoc(), equal,
                                                  oldValue, storedValue);
    }
    Value changed =
        storeStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                        storedValue, "__obelisk_state_value", stateBitCount,
                        directLayout, guardedPermission, assumeClean);
    if (adaptor.getValue().size() == 2)
      changed = arith::OrIOp::create(
          rewriter, op.getLoc(), changed,
          storeStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                          adaptor.getValue()[1], "__obelisk_state_unknown",
                          stateBitCount, directLayout, guardedPermission,
                          assumeClean));
    (void)changed;
    bool needsNotification = true;
    // Exact fanout proves that an absent root has no language-level waiter.
    // Direct roots are also immune to external writes (VPI-off/read), while a
    // guarded VPI-full root may elide observers only in its clean fast body.
    if (directLayout && directLayout->transitionHandlesExact)
      if (directRange && (assumeClean || !directRange->guarded))
        needsNotification =
            directLayout->transitionHandles.contains(directRange->staticID);
    if (!needsNotification) {
      rewriter.eraseOp(op);
      return success();
    }
    if (isa<FloatType>(valueType)) {
      Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
      auto save = [&](Value value) {
        Value storage =
            entryAlloca(rewriter, op.getLoc(), value.getType(), 1, 1);
        LLVM::StoreOp::create(rewriter, op.getLoc(), value, storage, 1);
        return storage;
      };
      Value contextAddress = LLVM::AddressOfOp::create(
          rewriter, op.getLoc(), pointer, "__obelisk_current_context");
      Value runtimeContext = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                                  pointer, contextAddress, 8);
      LLVM::CallOp::create(
          rewriter, op.getLoc(), TypeRange{},
          SymbolRefAttr::get(rewriter.getContext(),
                             "obelisk_rt_v1_scheduler_real_transition"),
          ValueRange{runtimeContext, adaptor.getReference().front(),
                     llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(),
                                  *width),
                     save(oldValue), save(storedValue)});
    } else {
      notifySignal(rewriter, op.getLoc(), adaptor.getReference().front(),
                   *width, oldValue, oldUnknown, notificationValue,
                   adaptor.getValue().size() == 2 ? adaptor.getValue()[1]
                                                  : Value{},
                   directRange &&
                           (assumeClean || !directRange->guarded) &&
                           directLayout &&
                           directLayout->transitionHandlesExact
                       ? directRange
                       : std::nullopt);
    }
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStateLayout *directLayout;
};

class OverrideConversion final
    : public OpConversionPattern<sim::SimOverrideOp> {
public:
  OverrideConversion(const TypeConverter &converter, MLIRContext *context,
                     uint64_t stateBitCount)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimOverrideOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getTarget().size() != 1 || adaptor.getValue().empty())
      return failure();
    Type valueType = op.getValue().getType();
    std::optional<unsigned> width = nativeStateWidth(valueType);
    if (!width)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    IntegerType plane = rewriter.getIntegerType(*width);
    IntegerType i32 = rewriter.getI32Type();
    IntegerType i64 = rewriter.getI64Type();
    Location location = op.getLoc();

    Value value = adaptor.getValue().front();
    if (isa<FloatType>(valueType))
      value = arith::BitcastOp::create(rewriter, location, plane, value);
    Value valueStorage = entryAlloca(rewriter, location, plane, 1, 1);
    LLVM::StoreOp::create(rewriter, location, value, valueStorage, 1);
    Value unknownStorage = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknownStorage = entryAlloca(rewriter, location, plane, 1, 1);
      LLVM::StoreOp::create(rewriter, location, adaptor.getValue()[1],
                            unknownStorage, 1);
    }
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    Value globalValue = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                  "__obelisk_state_value");
    Value globalUnknown = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                    "__obelisk_state_unknown");
    uint32_t descriptorKind = isa<sim::NetType>(op.getTarget().getType())
                                  ? OBELISK_RT_DESCRIPTOR_NET
                                  : OBELISK_RT_DESCRIPTOR_STORAGE;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_native_override"),
            ValueRange{
                context, globalValue, globalUnknown,
                llvmConstant(rewriter, location, i64, stateBitCount),
                adaptor.getTarget().front(),
                llvmConstant(rewriter, location, i64, *width),
                llvmConstant(rewriter, location, i32, descriptorKind),
                llvmConstant(rewriter, location, i32, op.getIsAssign() ? 1 : 0),
                valueStorage, unknownStorage})
            .getResult();
    LLVM::CallOp::create(rewriter, location, TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{context, status});
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
};

class ReleaseOverrideConversion final
    : public OpConversionPattern<sim::SimReleaseOverrideOp> {
public:
  ReleaseOverrideConversion(const TypeConverter &converter,
                            MLIRContext *context, uint64_t stateBitCount)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimReleaseOverrideOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getTarget().size() != 1)
      return failure();
    Type elementType;
    if (auto ref = dyn_cast<sim::RefType>(op.getTarget().getType()))
      elementType = ref.getElementType();
    else if (auto net = dyn_cast<sim::NetType>(op.getTarget().getType()))
      elementType = net.getElementType();
    std::optional<unsigned> width = nativeStateWidth(elementType);
    if (!width)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    IntegerType i32 = rewriter.getI32Type();
    IntegerType i64 = rewriter.getI64Type();
    Location location = op.getLoc();
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    Value globalValue = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                  "__obelisk_state_value");
    Value globalUnknown = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                    "__obelisk_state_unknown");
    uint32_t descriptorKind = isa<sim::NetType>(op.getTarget().getType())
                                  ? OBELISK_RT_DESCRIPTOR_NET
                                  : OBELISK_RT_DESCRIPTOR_STORAGE;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_native_release_override"),
            ValueRange{context, globalValue, globalUnknown,
                       llvmConstant(rewriter, location, i64, stateBitCount),
                       adaptor.getTarget().front(),
                       llvmConstant(rewriter, location, i64, *width),
                       llvmConstant(rewriter, location, i32, descriptorKind),
                       llvmConstant(rewriter, location, i32,
                                    op.getIsAssign() ? 1 : 0)})
            .getResult();
    LLVM::CallOp::create(rewriter, location, TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{context, status});
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
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount) {}
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
      converted.push_back(
          loadStatePlane(rewriter, op.getLoc(), adaptor.getNet().front(), plane,
                         "__obelisk_state_unknown", true, stateBitCount));
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
    auto function =
        dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
    if (!function)
      return std::nullopt;
    auto descriptor = function.getArgAttrOfType<IntegerAttr>(
        argument.getArgNumber(), sim::metadata::descriptorId);
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
    IntegerType i1 = rewriter.getI1Type();
    auto boolean = [&](bool value) {
      return arith::ConstantOp::create(rewriter, op.getLoc(), i1,
                                       rewriter.getBoolAttr(value));
    };
    if (adaptor.getValue().size() == 2) {
      storeStatePlane(rewriter, op.getLoc(), adaptor.getDriver().front(),
                      adaptor.getValue()[1], "__obelisk_state_unknown",
                      layout.bitCount);
    } else {
      storeStatePlane(rewriter, op.getLoc(), adaptor.getDriver().front(),
                      boolean(false), "__obelisk_state_unknown",
                      layout.bitCount);
    }
    Value changed =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI1Type(),
                                  rewriter.getBoolAttr(false));
    std::optional<uint64_t> affectedNet;
    if (auto netID = op->getAttrOfType<IntegerAttr>("obelisk.native.net_id"))
      affectedNet = netID.getInt();
    struct Publication {
      Value handle;
      Value oldValue;
      Value oldUnknown;
      Value value;
      Value unknown;
      bool fourState;
    };
    SmallVector<Publication> publications;
    SmallVector<std::pair<uint64_t, uint64_t>> resolvedComponents;
    for (const NativeStateLayout::Net &net : layout.netLayouts) {
      if (affectedNet && net.id != *affectedNet)
        continue;
      for (unsigned bit = 0; bit < net.width; ++bit) {
        std::pair<uint64_t, uint64_t> logical{net.id, bit};
        auto foundCanonical = layout.connectivityCanonical.find(logical);
        std::pair<uint64_t, uint64_t> canonical =
            foundCanonical == layout.connectivityCanonical.end()
                ? logical
                : foundCanonical->second;
        auto foundComponent = layout.connectivityComponents.find(canonical);
        SmallVector<::obelisk::analysis::NetBit> fallback;
        ArrayRef<::obelisk::analysis::NetBit> component;
        if (foundComponent == layout.connectivityComponents.end() ||
            foundComponent->second.empty()) {
          fallback.push_back({net.id, bit});
          component = fallback;
        } else {
          component = foundComponent->second;
        }
        if (llvm::is_contained(resolvedComponents, canonical))
          continue;
        resolvedComponents.push_back(canonical);

        Value resolvedValue = boolean(true);
        Value resolvedUnknown = boolean(true);
        for (const NativeStateLayout::Driver &driver : layout.driverLayouts) {
          for (const ::obelisk::analysis::NetBit &member : component) {
            if (member.net != driver.netId ||
                member.offset < driver.drivenLow ||
                member.offset - driver.drivenLow >= driver.drivenWidth ||
                member.offset >= driver.width)
              continue;
            // Every bit is an independent driver contribution. A topology
            // component may contain several bits from the same vector driver
            // (for example, an inout concatenation that shorts them).
            Value handle = arith::ConstantOp::create(
                rewriter, op.getLoc(), rewriter.getI64Type(),
                rewriter.getI64IntegerAttr(encodeNativeStaticHandle(
                    driver.handleID, static_cast<int32_t>(member.offset))));
            Value driverValue =
                loadStatePlane(rewriter, op.getLoc(), handle, i1,
                               "__obelisk_state_value", false, layout.bitCount);
            Value driverUnknown = loadStatePlane(rewriter, op.getLoc(), handle,
                                                 i1, "__obelisk_state_unknown",
                                                 true, layout.bitCount);
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
                                          arith::CmpIPredicate::ne,
                                          resolvedValue, driverValue)));
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
        }
        for (const ::obelisk::analysis::NetBit &member : component) {
          auto memberNet =
              llvm::find_if(layout.netLayouts, [&](const auto &candidate) {
                return candidate.id == member.net;
              });
          if (memberNet == layout.netLayouts.end() ||
              member.offset >= memberNet->width)
            return failure();
          Value netHandle = arith::ConstantOp::create(
              rewriter, op.getLoc(), rewriter.getI64Type(),
              rewriter.getI64IntegerAttr(encodeNativeStaticHandle(
                  memberNet->handleID, static_cast<int32_t>(member.offset))));
          Value oldResolvedValue =
              loadStatePlane(rewriter, op.getLoc(), netHandle, i1,
                             "__obelisk_state_value", false, layout.bitCount);
          Value oldResolvedUnknown =
              loadStatePlane(rewriter, op.getLoc(), netHandle, i1,
                             "__obelisk_state_unknown", true, layout.bitCount);
          Value publishValue = resolvedValue;
          Value publishUnknown = resolvedUnknown;
          if (!memberNet->fourState) {
            publishValue =
                arith::SelectOp::create(rewriter, op.getLoc(), resolvedUnknown,
                                        boolean(false), resolvedValue);
            publishUnknown = boolean(false);
          }
          publications.push_back({netHandle, oldResolvedValue,
                                  oldResolvedUnknown, publishValue,
                                  publishUnknown, memberNet->fourState});
        }
      }
    }
    // Publish every component affected by this vector drive before emitting
    // any transition notification. This matches bytecode atomic publication
    // and prevents observers from seeing a partially updated topology.
    for (const Publication &publication : publications) {
      changed = arith::OrIOp::create(
          rewriter, op.getLoc(), changed,
          storeStatePlane(rewriter, op.getLoc(), publication.handle,
                          publication.value, "__obelisk_state_value",
                          layout.bitCount));
      changed = arith::OrIOp::create(
          rewriter, op.getLoc(), changed,
          storeStatePlane(rewriter, op.getLoc(), publication.handle,
                          publication.unknown, "__obelisk_state_unknown",
                          layout.bitCount));
    }
    for (const Publication &publication : publications)
      notifySignal(rewriter, op.getLoc(), publication.handle, 1,
                   publication.oldValue, publication.oldUnknown,
                   publication.value,
                   publication.fourState ? publication.unknown : Value{});
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
    rewriter.replaceOp(op,
                       offsetNativeHandle(rewriter, op.getLoc(),
                                          adaptor.getInput().front(), offset));
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
    SignedI64Index convertedLow =
        resizeSignedIndexToI64(rewriter, location, adaptor.getLowBit().front());
    Value low = convertedLow.value;
    unsigned inputWidth =
        *sim::getPackedWidth(op.getInput().getType().getElementType());
    unsigned resultWidth =
        *sim::getPackedWidth(op.getResult().getType().getElementType());
    Value minimum = arith::ConstantOp::create(
        rewriter, location, rewriter.getI64Type(),
        rewriter.getI64IntegerAttr(-static_cast<int64_t>(resultWidth - 1)));
    Value maximum =
        arith::ConstantOp::create(rewriter, location, rewriter.getI64Type(),
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
    Value selected =
        offsetNativeHandle(rewriter, location, adaptor.getInput().front(), low);
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
    rewriter.replaceOp(op,
                       offsetNativeHandle(rewriter, op.getLoc(),
                                          adaptor.getInput().front(), amount));
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
    SignedI64Index convertedIndex =
        resizeSignedIndexToI64(rewriter, location, adaptor.getIndex().front());
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

void emitNativeStateRelease(OpBuilder &builder, Location location, Value handle,
                            bool ownerReference);

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
      for (unsigned successorIndex = 0, end = user->getNumSuccessors();
           successorIndex != end; ++successorIndex) {
        Block *successor = user->getSuccessor(successorIndex);
        SuccessorOperands successorOperands =
            branch.getSuccessorOperands(successorIndex);
        for (unsigned
                 argumentIndex = successorOperands.getProducedOperandCount(),
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
            argument && argument.getOwner() == block)
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
      for (unsigned successorIndex = 0, end = terminator->getNumSuccessors();
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
        insertAutomaticOwnerReleaseMarker(cleanupBuilder, allocation.getLoc(),
                                          cleanupHandle);
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
    auto savePlane = [&](Value value) {
      Value address = entryAlloca(rewriter, location, value.getType(), 1, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    Value initial = adaptor.getInitialValue().front();
    if (isa<FloatType>(op.getInitialValue().getType()))
      initial =
          arith::BitcastOp::create(rewriter, location,
                                   rewriter.getIntegerType(*nativeStateWidth(
                                       op.getInitialValue().getType())),
                                   initial);
    Value value = savePlane(initial);
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getInitialValue().size() == 2)
      unknown = savePlane(adaptor.getInitialValue()[1]);
    Value outHandle = entryAlloca(rewriter, location, i64, 1, 8);
    Value invalid = llvmConstant(rewriter, location, i64, UINT64_MAX);
    LLVM::StoreOp::create(rewriter, location, invalid, outHandle, 8);
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    SmallVector<uint64_t, 2> rootOffsets;
    if (!sim::getManagedHandleOffsets(op.getInitialValue().getType(),
                                      rootOffsets))
      return failure();
    SmallVector<Value> arguments{
        context, llvmConstant(rewriter, location, i64, *width), value, unknown};
    StringRef allocation = "obelisk_rt_v1_native_state_alloc";
    if (!rootOffsets.empty()) {
      Value count = llvmConstant(rewriter, location, i64, rootOffsets.size());
      Value offsets =
          entryAlloca(rewriter, location, i64, rootOffsets.size(), 8);
      for (auto [index, offset] : llvm::enumerate(rootOffsets))
        LLVM::StoreOp::create(
            rewriter, location, llvmConstant(rewriter, location, i64, offset),
            byteGEP(rewriter, location, offsets, index * sizeof(uint64_t)), 8);
      allocation = "obelisk_rt_v1_native_state_alloc_with_roots";
      arguments.push_back(offsets);
      arguments.push_back(count);
    }
    arguments.push_back(outHandle);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(), allocation), arguments)
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    Value handle = LLVM::LoadOp::create(rewriter, location, i64, outHandle, 8);
    rewriter.replaceOp(op, handle);
    return success();
  }
};

using ReferenceArgumentMap = llvm::DenseMap<Operation *, SmallVector<unsigned>>;

void emitNativeStateRelease(OpBuilder &builder, Location location, Value handle,
                            bool ownerReference) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Value contextAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  Value context =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  Value owner = arith::ConstantOp::create(
      builder, location, i32,
      builder.getI32IntegerAttr(ownerReference ? 1 : 0));
  Value status = LLVM::CallOp::create(
                     builder, location, TypeRange{i32},
                     SymbolRefAttr::get(builder.getContext(),
                                        "obelisk_rt_v1_native_state_release"),
                     ValueRange{context, handle, owner})
                     .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(builder.getContext(), "obelisk_rt_v1_scheduler_fail"),
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

LogicalResult
releaseNativeAutomaticState(ModuleOp module,
                            const ReferenceArgumentMap &referenceArguments) {
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

struct NativeStaticNBAPlan {
  SmallVector<obelisk_rt_static_nba_root> roots;
  SmallVector<obelisk_rt_static_nba_site> sites;
  DenseMap<uint64_t, uint32_t> siteRoots;
  SmallVector<std::string> generatedAccumulators;
};

LogicalResult
materializeGeneratedNBAAccumulators(ModuleOp module,
                                    const NativeStaticNBAPlan &plan) {
  if (plan.generatedAccumulators.size() != plan.roots.size())
    return module.emitError("generated NBA accumulator plan is malformed");
  OpBuilder builder(module.getContext());
  Location location = module.getLoc();
  Type storageType = LLVM::LLVMArrayType::get(
      builder.getI8Type(), sizeof(obelisk_rt_generated_nba_accumulator_256));
  for (StringRef name : plan.generatedAccumulators) {
    if (name.empty())
      continue;
    if (module.lookupSymbol<LLVM::GlobalOp>(name))
      return module.emitError("generated NBA accumulator symbol is duplicated");
    builder.setInsertionPointToStart(module.getBody());
    auto global =
        LLVM::GlobalOp::create(builder, location, storageType, false,
                               LLVM::Linkage::Internal, name, Attribute{}, 32);
    Block *initializer = new Block;
    global.getInitializerRegion().push_back(initializer);
    builder.setInsertionPointToStart(initializer);
    LLVM::ReturnOp::create(
        builder, location,
        LLVM::ZeroOp::create(builder, location, storageType));
  }
  return success();
}

struct NativeStaticFanoutPlan {
  SmallVector<obelisk_rt_static_fanout_entry> entries;
  bool exact = false;
};

std::optional<uint64_t> resolveCFGConstantInteger(Value value,
                                                  DenseSet<Value> &active) {
  if (auto constant = value.getDefiningOp<arith::ConstantOp>())
    if (auto integer = dyn_cast<IntegerAttr>(constant.getValue()))
      return integer.getValue().getZExtValue();
  auto argument = dyn_cast<BlockArgument>(value);
  if (!argument || !active.insert(value).second)
    return std::nullopt;
  std::optional<uint64_t> resolved;
  Block *block = argument.getOwner();
  for (Block *predecessor : block->getPredecessors()) {
    Operation *terminator = predecessor->getTerminator();
    auto branch = dyn_cast<BranchOpInterface>(terminator);
    if (!branch) {
      active.erase(value);
      return std::nullopt;
    }
    for (unsigned successor = 0; successor != terminator->getNumSuccessors();
         ++successor) {
      if (terminator->getSuccessor(successor) != block)
        continue;
      SuccessorOperands operands = branch.getSuccessorOperands(successor);
      unsigned index = argument.getArgNumber();
      if (index >= operands.size() || operands.isOperandProduced(index)) {
        active.erase(value);
        return std::nullopt;
      }
      std::optional<uint64_t> incoming =
          resolveCFGConstantInteger(operands[index], active);
      if (!incoming || (resolved && *resolved != *incoming)) {
        active.erase(value);
        return std::nullopt;
      }
      resolved = incoming;
    }
  }
  active.erase(value);
  return resolved;
}

std::optional<uint64_t> resolveCFGConstantInteger(Value value) {
  DenseSet<Value> active;
  return resolveCFGConstantInteger(value, active);
}

FailureOr<SmallVector<obelisk_rt_static_actor_root>>
buildNativeStaticActorRootPlan(
    ModuleOp module, const NativeStateLayout &stateLayout,
    const DenseMap<Operation *, uint32_t> &actorSlots) {
  SmallVector<obelisk_rt_static_actor_root> plan;
  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  auto specialization =
      design ? design->getAttrOfType<sim::StaticSpecializationAttr>(
                   sim::metadata::staticSpecialization)
             : sim::StaticSpecializationAttr{};
  if (!specialization)
    return plan;
  for (Attribute attribute : specialization.getActorRoots()) {
    auto dependency = dyn_cast<sim::StaticActorRootAttr>(attribute);
    if (!dependency)
      return module.emitError("invalid static actor/root dependency"),
             failure();
    sim::SimFuncOp function =
        design.lookupSymbol<sim::SimFuncOp>(dependency.getFunction());
    auto actor =
        function ? actorSlots.find(function.getOperation()) : actorSlots.end();
    if (!function || actor == actorSlots.end())
      continue;
    auto handle = stateLayout.storage.find(dependency.getDescriptor());
    if (handle == stateLayout.storage.end())
      return module.emitError(
                 "static actor/root dependency references unknown storage"),
             failure();
    obelisk_rt_stable_handle_v1 decoded{};
    if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
        decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset != 0)
      return module.emitError(
                 "static actor/root dependency has an invalid native handle"),
             failure();
    uint32_t flags = (dependency.getRead() ? OBELISK_RT_STATIC_ROOT_READ : 0) |
                     (dependency.getWrite() ? OBELISK_RT_STATIC_ROOT_WRITE : 0);
    if (flags != 0)
      plan.push_back({actor->second, decoded.id, flags, 0});
  }
  llvm::sort(plan, [](const auto &lhs, const auto &rhs) {
    return std::tuple{lhs.actor_slot, lhs.static_state, lhs.flags} <
           std::tuple{rhs.actor_slot, rhs.static_state, rhs.flags};
  });
  plan.erase(std::unique(plan.begin(), plan.end(),
                         [](const auto &lhs, const auto &rhs) {
                           return lhs.actor_slot == rhs.actor_slot &&
                                  lhs.static_state == rhs.static_state &&
                                  lhs.flags == rhs.flags;
                         }),
             plan.end());
  return plan;
}

class ImmediateNBAConversion final
    : public OpConversionPattern<sim::SimNBAEnqueueOp> {
public:
  ImmediateNBAConversion(const TypeConverter &converter, MLIRContext *context,
                         uint64_t stateBitCount,
                         const NativeStaticNBAPlan *staticPlan,
                         bool staticSitesEnabled, bool guardedClaims)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        staticPlan(staticPlan), staticSitesEnabled(staticSitesEnabled),
        guardedClaims(guardedClaims) {}
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

    sim::NBASiteAttr site = op.getSiteAttr();
    auto staticRoot = site && staticPlan
                          ? staticPlan->siteRoots.find(site.getId())
                          : DenseMap<uint64_t, uint32_t>::const_iterator{};
    std::optional<uint64_t> destinationValue =
        resolveCFGConstantInteger(adaptor.getDestination().front());
    obelisk_rt_stable_handle_v1 decoded{};
    bool packedStaticStage =
        site && staticPlan && staticRoot != staticPlan->siteRoots.end() &&
        staticRoot->second < staticPlan->roots.size() &&
        adaptor.getDelay().empty() && !site.getTiming() &&
        site.getStorage() != sim::ComputeNBAStorageKind::DynamicFrontier &&
        *width <= 64 && destinationValue &&
        obelisk_rt_stable_handle_decode(*destinationValue, &decoded) &&
        decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC &&
        decoded.id == staticPlan->roots[staticRoot->second].static_state &&
        decoded.offset >= 0 &&
        static_cast<uint64_t>(decoded.offset) <=
            staticPlan->roots[staticRoot->second].bit_width &&
        *width <= staticPlan->roots[staticRoot->second].bit_width -
                      static_cast<uint64_t>(decoded.offset) &&
        (llvm::all_of(
             adaptor.getValue(),
             [](Value value) { return isa<IntegerType>(value.getType()); }) ||
         (*width <= 64 && adaptor.getValue().size() == 1 &&
          isa<LLVM::LLVMPointerType>(adaptor.getValue().front().getType())));
    if (packedStaticStage) {
      bool assumeClean = op->hasAttr(kAssumeCleanSpecializationAttr);
      bool useGuardedClaim =
          !assumeClean &&
          (guardedClaims ||
           staticPlan->roots[staticRoot->second].bit_width <= 64);
      auto widen = [&](Value value) {
        if (isa<LLVM::LLVMPointerType>(value.getType()))
          value = LLVM::LoadOp::create(
              rewriter, location,
              IntegerType::get(rewriter.getContext(), *width), value, 1);
        auto type = cast<IntegerType>(value.getType());
        return type.getWidth() == 64
                   ? value
                   : LLVM::ZExtOp::create(rewriter, location, i64, value)
                         .getResult();
      };
      Value unknown = llvmConstant(rewriter, location, i64, 0);
      if (adaptor.getValue().size() == 2)
        unknown = widen(adaptor.getValue()[1]);
      Value value = widen(adaptor.getValue().front());
      StringRef generatedAccumulator =
          staticRoot->second < staticPlan->generatedAccumulators.size()
              ? staticPlan->generatedAccumulators[staticRoot->second]
              : StringRef{};
      sim::SimFuncOp function = op->getParentOfType<sim::SimFuncOp>();
      uint32_t homeRegion =
          function ? getRuntimeEventRegion(function.getHomeRegion())
                   : UINT32_MAX;
      uint32_t commitRegion = homeRegion == OBELISK_RT_REGION_ACTIVE ||
                                      homeRegion == OBELISK_RT_REGION_REACTIVE
                                  ? homeRegion + 2
                                  : UINT32_MAX;
      bool directGeneratedStage =
          !generatedAccumulator.empty() && *width == 32 &&
          adaptor.getValue().size() == 1 && (decoded.offset & 31) == 0 &&
          commitRegion != UINT32_MAX;
      auto emitDirectGeneratedStage = [&] {
        Value base = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                               generatedAccumulator);
        uint64_t laneOffset = static_cast<uint64_t>(decoded.offset / 8);
        Value laneValue = LLVM::TruncOp::create(rewriter, location, i32, value);
        LLVM::StoreOp::create(
            rewriter, location, laneValue,
            byteGEP(rewriter, location, base,
                    offsetof(obelisk_rt_generated_nba_accumulator_256, value) +
                        laneOffset),
            4);
        // This direct form is restricted to two-state values. The generated
        // record is zero-initialized and no other path writes its unknown
        // lanes, so repeatedly storing zero here only adds hot-path traffic.
        LLVM::StoreOp::create(
            rewriter, location,
            llvmConstant(rewriter, location, i32, UINT32_MAX),
            byteGEP(
                rewriter, location, base,
                offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask) +
                    laneOffset),
            4);
        LLVM::StoreOp::create(
            rewriter, location, llvmConstant(rewriter, location, i32, 1),
            byteGEP(rewriter, location, base,
                    offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
        LLVM::StoreOp::create(
            rewriter, location,
            llvmConstant(rewriter, location, i32, commitRegion),
            byteGEP(rewriter, location, base,
                    offsetof(obelisk_rt_generated_nba_accumulator_256,
                             exec_region)),
            4);
      };
      if (!useGuardedClaim && directGeneratedStage) {
        emitDirectGeneratedStage();
        rewriter.eraseOp(op);
        return success();
      }
      if (useGuardedClaim && directGeneratedStage) {
        Value useDirect = staticNBASpecializationGuard(rewriter, location,
                                                       staticRoot->second);
        Block *head = rewriter.getInsertionBlock();
        Block *continuation =
            rewriter.splitBlock(head, rewriter.getInsertionPoint());
        Region *region = head->getParent();
        Block *directBlock =
            rewriter.createBlock(region, continuation->getIterator());
        Block *claimBlock =
            rewriter.createBlock(region, continuation->getIterator());
        recordStaticSpecializationCFGBlocks(rewriter, head, 3);

        rewriter.setInsertionPointToEnd(head);
        markLikelyTrue(cf::CondBranchOp::create(rewriter, location, useDirect,
                                                directBlock, ValueRange{},
                                                claimBlock, ValueRange{}));

        rewriter.setInsertionPointToEnd(directBlock);
        emitDirectGeneratedStage();
        cf::BranchOp::create(rewriter, location, continuation);

        rewriter.setInsertionPointToEnd(claimBlock);
        Value contextAddress = LLVM::AddressOfOp::create(
            rewriter, location, pointer, "__obelisk_current_context");
        Value runtimeContext = LLVM::LoadOp::create(rewriter, location, pointer,
                                                    contextAddress, 8);
        Value status =
            LLVM::CallOp::create(
                rewriter, location, TypeRange{i32},
                SymbolRefAttr::get(rewriter.getContext(),
                                   "obelisk_rt_v1_static_nba_claim"),
                ValueRange{
                    runtimeContext,
                    llvmConstant(rewriter, location, i32, staticRoot->second),
                    LLVM::AddressOfOp::create(rewriter, location, pointer,
                                              "__obelisk_state_value"),
                    LLVM::ZeroOp::create(rewriter, location, pointer),
                    llvmConstant(rewriter, location, i64, stateBitCount),
                    llvmConstant(rewriter, location, i64,
                                 static_cast<uint64_t>(decoded.offset)),
                    llvmConstant(rewriter, location, i64, *width), value,
                    unknown})
                .getResult();
        LLVM::CallOp::create(rewriter, location, TypeRange{},
                             SymbolRefAttr::get(rewriter.getContext(),
                                                "obelisk_rt_v1_scheduler_fail"),
                             ValueRange{runtimeContext, status});
        cf::BranchOp::create(rewriter, location, continuation);

        rewriter.setInsertionPointToStart(continuation);
        rewriter.eraseOp(op);
        return success();
      }
      Value contextAddress = LLVM::AddressOfOp::create(
          rewriter, location, pointer, "__obelisk_current_context");
      Value runtimeContext =
          LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
      if (!useGuardedClaim) {
        LLVM::CallOp::create(
            rewriter, location, TypeRange{},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_static_nba_stage_wide"),
            ValueRange{
                runtimeContext,
                llvmConstant(rewriter, location, i32, staticRoot->second),
                llvmConstant(rewriter, location, i64,
                             static_cast<uint64_t>(decoded.offset)),
                llvmConstant(rewriter, location, i64, *width), value, unknown,
                llvmConstant(rewriter, location, i32,
                             adaptor.getValue().size() == 2 ? 1 : 0)});
        rewriter.eraseOp(op);
        return success();
      }
      Value status =
          LLVM::CallOp::create(
              rewriter, location, TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_static_nba_claim"),
              ValueRange{
                  runtimeContext,
                  llvmConstant(rewriter, location, i32, staticRoot->second),
                  LLVM::AddressOfOp::create(rewriter, location, pointer,
                                            "__obelisk_state_value"),
                  adaptor.getValue().size() == 2
                      ? LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                  "__obelisk_state_unknown")
                            .getResult()
                      : LLVM::ZeroOp::create(rewriter, location, pointer)
                            .getResult(),
                  llvmConstant(rewriter, location, i64, stateBitCount),
                  llvmConstant(rewriter, location, i64,
                               static_cast<uint64_t>(decoded.offset)),
                  llvmConstant(rewriter, location, i64, *width), value,
                  unknown})
              .getResult();
      LLVM::CallOp::create(rewriter, location, TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{runtimeContext, status});
      rewriter.eraseOp(op);
      return success();
    }

    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value runtimeContext =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    auto savePlane = [&](Value value) {
      Value address = entryAlloca(rewriter, location, value.getType(), 1, 1);
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
    Value delay = adaptor.getDelay().empty()
                      ? llvmConstant(rewriter, location, i64, 0)
                      : adaptor.getDelay().front();
    if (isa<sim::StringType>(op.getValue().getType())) {
      Value status =
          LLVM::CallOp::create(
              rewriter, location, TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_scheduler_string_nba"),
              ValueRange{runtimeContext, valuePlane,
                         llvmConstant(rewriter, location, i64, stateBitCount),
                         adaptor.getDestination().front(), delay,
                         adaptor.getValue().front()})
              .getResult();
      LLVM::CallOp::create(rewriter, location, TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{runtimeContext, status});
    } else {
      bool staticallyStaged =
          staticSitesEnabled && staticPlan && site &&
          staticPlan->siteRoots.contains(site.getId()) &&
          adaptor.getDelay().empty() && !site.getTiming() &&
          site.getStorage() != sim::ComputeNBAStorageKind::DynamicFrontier;
      SmallVector<Value> arguments{
          runtimeContext,
          valuePlane,
          unknownPlane,
          llvmConstant(rewriter, location, i64, stateBitCount),
          adaptor.getDestination().front(),
          llvmConstant(rewriter, location, i64, *width)};
      if (staticallyStaged)
        arguments.insert(arguments.begin() + 1,
                         llvmConstant(rewriter, location, i64, site.getId()));
      else
        arguments.push_back(delay);
      arguments.push_back(value);
      arguments.push_back(unknown);
      Value status =
          LLVM::CallOp::create(
              rewriter, location, TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 staticallyStaged
                                     ? "obelisk_rt_v1_scheduler_static_nba"
                                     : "obelisk_rt_v1_scheduler_nba"),
              arguments)
              .getResult();
      LLVM::CallOp::create(rewriter, location, TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{runtimeContext, status});
    }
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStaticNBAPlan *staticPlan;
  bool staticSitesEnabled;
  bool guardedClaims;
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

std::string managedClassDescriptorName(SymbolRefAttr className) {
  return (className.getRootReference().getValue() +
          ".__obelisk_class_descriptor")
      .str();
}

std::string managedMethodThunkName(sim::SimClassMethodDeclOp method) {
  return (method.getSymName() + ".__obelisk_native_thunk").str();
}

struct ManagedFieldLayout {
  sim::SimClassFieldDeclOp declaration;
  uint64_t offset = 0;
  uint64_t planeSize = 0;
  uint32_t alignment = 1;
  bool fourState = false;
};

struct ManagedTraceLayout {
  uint64_t offset = 0;
  bool weak = false;
  uint32_t slotKind = OBELISK_RT_MANAGED_SLOT_CLASS;
};

struct ManagedClassLayout {
  sim::SimClassDeclOp declaration;
  uint64_t size = sizeof(void *);
  uint32_t alignment = alignof(void *);
  SmallVector<ManagedFieldLayout> fields;
  SmallVector<ManagedTraceLayout> tracedFields;
  SmallVector<sim::SimClassMethodDeclOp> methods;
};

LogicalResult collectManagedTraceSlots(
    Type type, uint64_t baseBitOffset,
    SmallVectorImpl<std::pair<uint64_t, uint32_t>> &slots) {
  if (sim::isManagedHandleType(type)) {
    if ((baseBitOffset & 7) != 0)
      return failure();
    uint32_t kind = OBELISK_RT_MANAGED_SLOT_CLASS;
    if (isa<sim::StringType>(type))
      kind = OBELISK_RT_MANAGED_SLOT_STRING;
    else if (isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
                 type))
      kind = OBELISK_RT_MANAGED_SLOT_CONTAINER;
    slots.push_back({baseBitOffset / 8, kind});
    return success();
  }
  if (!sim::isAggregateType(type))
    return success();
  for (unsigned index = 0; index < sim::getAggregateNumElements(type);
       ++index) {
    auto child = sim::getAggregateProvenanceSubelement(type, index);
    if (!child || child->first > UINT64_MAX - baseBitOffset)
      return failure();
    if (failed(
            collectManagedTraceSlots(sim::getAggregateElementType(type, index),
                                     baseBitOffset + child->first, slots)))
      return failure();
  }
  return success();
}

LLVM::GlobalOp makeByteArrayGlobal(ModuleOp module, Location location,
                                   StringRef name, StringRef bytes) {
  MLIRContext *context = module.getContext();
  Type i8 = IntegerType::get(context, 8);
  Type type = LLVM::LLVMArrayType::get(i8, bytes.size());
  return makeConstantGlobal(
      module, location, type, name, LLVM::Linkage::Internal, 1,
      [&](OpBuilder &builder) {
        Value value = LLVM::ZeroOp::create(builder, location, type);
        for (auto [index, byte] : llvm::enumerate(bytes.bytes()))
          value = LLVM::InsertValueOp::create(
              builder, location, value,
              llvmConstant(builder, location, i8, byte),
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        return value;
      });
}

LogicalResult
prepareManagedClassInventory(ModuleOp module,
                             const llvm::DataLayout &dataLayout,
                             llvm::StringMap<ManagedClassLayout> &layouts) {
  SmallVector<sim::SimClassDeclOp> classes;
  SmallVector<sim::SimClassFieldDeclOp> fields;
  SmallVector<sim::SimClassMethodDeclOp> methods;
  module.walk([&](sim::SimClassDeclOp op) { classes.push_back(op); });
  module.walk([&](sim::SimClassFieldDeclOp op) { fields.push_back(op); });
  module.walk([&](sim::SimClassMethodDeclOp op) { methods.push_back(op); });
  if (classes.empty())
    return success();

  llvm::StringMap<sim::SimClassDeclOp> classesByName;
  llvm::StringMap<SmallVector<sim::SimClassFieldDeclOp>> fieldsByOwner;
  llvm::StringMap<SmallVector<sim::SimClassMethodDeclOp>> methodsByOwner;
  for (sim::SimClassDeclOp declaration : classes)
    classesByName[declaration.getSymName()] = declaration;
  for (sim::SimClassFieldDeclOp field : fields)
    fieldsByOwner[field.getOwner()].push_back(field);
  for (sim::SimClassMethodDeclOp method : methods)
    methodsByOwner[method.getOwner()].push_back(method);
  for (auto &entry : fieldsByOwner)
    llvm::sort(entry.second, [](auto lhs, auto rhs) {
      return lhs.getOrdinal() < rhs.getOrdinal();
    });

  llvm::SmallPtrSet<Operation *, 8> active;
  std::function<LogicalResult(sim::SimClassDeclOp)> compute =
      [&](sim::SimClassDeclOp declaration) -> LogicalResult {
    if (layouts.count(declaration.getSymName()))
      return success();
    if (!active.insert(declaration).second)
      return declaration.emitError("managed class layout contains a cycle");

    ManagedClassLayout layout;
    layout.declaration = declaration;
    if (auto baseName = declaration.getBase()) {
      auto base = classesByName.find(*baseName);
      if (base == classesByName.end() || failed(compute(base->second)))
        return declaration.emitError(
            "managed class layout references an unknown base");
      const ManagedClassLayout &baseLayout = layouts[base->getKey()];
      layout.size = baseLayout.size;
      layout.alignment = baseLayout.alignment;
      layout.tracedFields = baseLayout.tracedFields;
      layout.methods = baseLayout.methods;
    }
    if (declaration.getWeakReferentAttr()) {
      uint64_t referentOffset;
      if (!alignUp(layout.size, alignof(void *), referentOffset) ||
          referentOffset >
              std::numeric_limits<uint64_t>::max() - sizeof(void *))
        return declaration.emitError("weak referent layout overflow");
      layout.size = referentOffset + sizeof(void *);
      layout.alignment = std::max<uint32_t>(layout.alignment, alignof(void *));
      layout.tracedFields.push_back(
          {referentOffset, true, OBELISK_RT_MANAGED_SLOT_CLASS});
    }

    llvm::DataLayout localDataLayout(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    for (sim::SimClassFieldDeclOp field :
         fieldsByOwner[declaration.getSymName()]) {
      if (field.getIsStatic())
        continue;
      FailureOr<analysis::SimulationStorageProperties> storage =
          analysis::getSimulationStorageProperties(
              field.getType(), localDataLayout, llvmContext);
      if (failed(storage))
        return field.emitError(
            "class property has no fixed native managed layout");
      uint64_t offset;
      if (!alignUp(layout.size, storage->alignment, offset))
        return field.emitError("class property offset overflow");
      uint64_t planes = storage->fourState ? 2 : 1;
      if (storage->size >
          (std::numeric_limits<uint64_t>::max() - offset) / planes)
        return field.emitError("class property size overflow");
      layout.size = offset + storage->size * planes;
      layout.alignment =
          std::max<uint32_t>(layout.alignment, storage->alignment);
      ManagedFieldLayout fieldLayout{field, offset, storage->size,
                                     storage->alignment, storage->fourState};
      layout.fields.push_back(fieldLayout);
      SmallVector<std::pair<uint64_t, uint32_t>, 2> traceSlots;
      if (failed(collectManagedTraceSlots(field.getType(), 0, traceSlots)) ||
          traceSlots.size() != storage->managedRootOffsets.size())
        return field.emitError("class property has no typed managed layout");
      for (auto [rootOffset, slotKind] : traceSlots)
        layout.tracedFields.push_back(
            {offset + rootOffset,
             isa<sim::ClassHandleType>(field.getType()) && field.getIsWeak(),
             slotKind});
      if (auto existing = field->getAttrOfType<IntegerAttr>("offset");
          existing && existing.getValue().getZExtValue() != offset)
        return field.emitError("native and bytecode class layouts disagree");
      field->setAttr(
          "offset",
          IntegerAttr::get(IntegerType::get(module.getContext(), 64), offset));
    }
    uint64_t alignedSize;
    if (!alignUp(layout.size, layout.alignment, alignedSize))
      return declaration.emitError("class instance size overflow");
    layout.size = alignedSize;

    for (sim::SimClassMethodDeclOp method :
         methodsByOwner[declaration.getSymName()]) {
      if (!method.getSlot())
        continue;
      if (declaration.getIsInterface())
        continue;
      uint64_t slot = *method.getSlot();
      if (slot >= std::numeric_limits<size_t>::max())
        return method.emitError("virtual method slot exceeds host limits");
      if (layout.methods.size() <= slot)
        layout.methods.resize(static_cast<size_t>(slot) + 1);
      layout.methods[slot] = method;
    }
    for (auto [slot, method] : llvm::enumerate(layout.methods))
      if (!method)
        return declaration.emitError()
               << "virtual method table has an empty slot " << slot;

    declaration->setAttr(
        "obelisk.native.instance_size",
        IntegerAttr::get(IntegerType::get(module.getContext(), 64),
                         layout.size));
    declaration->setAttr(
        "obelisk.native.instance_alignment",
        IntegerAttr::get(IntegerType::get(module.getContext(), 32),
                         layout.alignment));
    active.erase(declaration);
    layouts[declaration.getSymName()] = std::move(layout);
    return success();
  };
  for (sim::SimClassDeclOp declaration : classes)
    if (failed(compute(declaration)))
      return failure();

  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  Type traceEntryType = LLVM::LLVMStructType::getLiteral(
      context, {i64, i64, i64, i32, i32, pointer});
  Type traceLayoutType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, pointer, i64});
  Type methodType = LLVM::LLVMStructType::getLiteral(
      context, {i64, i32, i32, pointer, pointer});
  Type classType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, i64, pointer, pointer, i64, pointer,
                pointer, i64, pointer, i64});

  // Base descriptors must exist before derived initializers take their
  // addresses. Repeatedly materialize classes whose base is ready.
  llvm::StringSet<> materialized;
  while (materialized.size() != classes.size()) {
    bool progress = false;
    for (sim::SimClassDeclOp declaration : classes) {
      if (materialized.count(declaration.getSymName()))
        continue;
      if (auto base = declaration.getBase(); base && !materialized.count(*base))
        continue;
      progress = true;
      ManagedClassLayout &layout = layouts[declaration.getSymName()];
      std::string prefix = declaration.getSymName().str();
      std::string entriesName = prefix + ".__obelisk_trace_entries";
      std::string traceName = prefix + ".__obelisk_trace_layout";
      std::string methodsName = prefix + ".__obelisk_methods";
      std::string interfacesName = prefix + ".__obelisk_interfaces";
      std::string debugName = prefix + ".__obelisk_debug_name";
      std::string descriptorName = managedClassDescriptorName(
          FlatSymbolRefAttr::get(context, declaration.getSymName()));
      Location location = declaration.getLoc();

      Type entriesType =
          LLVM::LLVMArrayType::get(traceEntryType, layout.tracedFields.size());
      if (!layout.tracedFields.empty())
        makeConstantGlobal(
            module, location, entriesType, entriesName, LLVM::Linkage::Internal,
            8, [&](OpBuilder &builder) {
              Value array =
                  LLVM::ZeroOp::create(builder, location, entriesType);
              for (auto [index, field] : llvm::enumerate(layout.tracedFields)) {
                Value entry =
                    LLVM::ZeroOp::create(builder, location, traceEntryType);
                entry = insertValue(
                    builder, location, entry,
                    llvmConstant(builder, location, i64, field.offset), 0);
                entry = insertValue(builder, location, entry,
                                    llvmConstant(builder, location, i64, 1), 2);
                entry = insertValue(builder, location, entry,
                                    llvmConstant(builder, location, i32,
                                                 field.weak
                                                     ? OBELISK_RT_TRACE_WEAK
                                                     : OBELISK_RT_TRACE_STRONG),
                                    3);
                entry = insertValue(
                    builder, location, entry,
                    llvmConstant(builder, location, i32, field.slotKind), 4);
                array = LLVM::InsertValueOp::create(
                    builder, location, array, entry,
                    ArrayRef<int64_t>{static_cast<int64_t>(index)});
              }
              return array;
            });
      makeConstantGlobal(
          module, location, traceLayoutType, traceName, LLVM::Linkage::Internal,
          8, [&](OpBuilder &builder) {
            Value trace =
                LLVM::ZeroOp::create(builder, location, traceLayoutType);
            trace = insertValue(
                builder, location, trace,
                llvmConstant(builder, location, i32, OBELISK_RT_VERSION), 0);
            trace = insertValue(
                builder, location, trace,
                llvmConstant(builder, location, i64, layout.size), 2);
            trace = insertValue(
                builder, location, trace,
                llvmConstant(builder, location, i64, layout.alignment), 3);
            if (!layout.tracedFields.empty())
              trace = insertValue(builder, location, trace,
                                  LLVM::AddressOfOp::create(
                                      builder, location, pointer, entriesName),
                                  4);
            trace = insertValue(builder, location, trace,
                                llvmConstant(builder, location, i64,
                                             layout.tracedFields.size()),
                                5);
            return trace;
          });

      Type methodsType =
          LLVM::LLVMArrayType::get(methodType, layout.methods.size());
      if (!layout.methods.empty())
        makeConstantGlobal(
            module, location, methodsType, methodsName, LLVM::Linkage::Internal,
            8, [&](OpBuilder &builder) {
              Value array =
                  LLVM::ZeroOp::create(builder, location, methodsType);
              for (auto indexedMethod : llvm::enumerate(layout.methods)) {
                auto index = indexedMethod.index();
                auto method = indexedMethod.value();
                Value entry =
                    LLVM::ZeroOp::create(builder, location, methodType);
                entry = insertValue(builder, location, entry,
                                    llvmConstant(builder, location, i64,
                                                 *method.getSignatureId()),
                                    0);
                uint32_t flags =
                    method.getIsTask() ? OBELISK_RT_METHOD_TASK : 0;
                if (method.getIsPure())
                  flags |= OBELISK_RT_METHOD_PURE;
                entry =
                    insertValue(builder, location, entry,
                                llvmConstant(builder, location, i32, flags), 1);
                entry = insertValue(
                    builder, location, entry,
                    llvmConstant(
                        builder, location, i32,
                        [&] {
                          if (method.getImplementation()) {
                            if (auto function =
                                    SymbolTable::lookupNearestSymbolFrom<
                                        sim::SimFuncOp>(
                                        method, method.getImplementationAttr()))
                              if (auto index =
                                      function->getAttrOfType<IntegerAttr>(
                                          "obelisk.bytecode.function"))
                                return static_cast<uint32_t>(
                                    index.getValue().getZExtValue());
                          }
                          return uint32_t{OBELISK_RT_METHOD_NO_BYTECODE};
                        }()),
                    2);
                if (!method.getIsPure())
                  entry = insertValue(
                      builder, location, entry,
                      LLVM::AddressOfOp::create(builder, location, pointer,
                                                managedMethodThunkName(method)),
                      3);
                array = LLVM::InsertValueOp::create(
                    builder, location, array, entry,
                    ArrayRef<int64_t>{static_cast<int64_t>(index)});
              }
              return array;
            });

      SmallVector<uint64_t> interfaceIDs;
      if (ArrayAttr interfaces = declaration.getInterfacesAttr())
        for (Attribute attribute : interfaces) {
          auto reference = cast<FlatSymbolRefAttr>(attribute);
          auto found = classesByName.find(reference.getValue());
          if (found == classesByName.end())
            return declaration.emitError(
                "managed interface descriptor is missing");
          interfaceIDs.push_back(found->second.getId());
        }
      Type interfacesType = LLVM::LLVMArrayType::get(i64, interfaceIDs.size());
      if (!interfaceIDs.empty())
        makeConstantGlobal(
            module, location, interfacesType, interfacesName,
            LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
              Value array =
                  LLVM::ZeroOp::create(builder, location, interfacesType);
              for (auto [index, id] : llvm::enumerate(interfaceIDs))
                array = LLVM::InsertValueOp::create(
                    builder, location, array,
                    llvmConstant(builder, location, i64, id),
                    ArrayRef<int64_t>{static_cast<int64_t>(index)});
              return array;
            });

      StringRef debug = declaration.getDebugNameAttr()
                            ? declaration.getDebugNameAttr().getValue()
                            : StringRef{};
      if (!debug.empty())
        makeByteArrayGlobal(module, location, debugName, debug);
      makeConstantGlobal(
          module, location, classType, descriptorName, LLVM::Linkage::Internal,
          8, [&](OpBuilder &builder) {
            Value descriptor =
                LLVM::ZeroOp::create(builder, location, classType);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i32, OBELISK_RT_VERSION), 0);
            uint32_t flags =
                declaration.getIsAbstract() ? OBELISK_RT_CLASS_ABSTRACT : 0;
            if (declaration.getIsInterface())
              flags |= OBELISK_RT_CLASS_INTERFACE;
            if (declaration.getIsFinal())
              flags |= OBELISK_RT_CLASS_FINAL;
            if (declaration.getWeakReferentAttr())
              flags |= OBELISK_RT_CLASS_WEAK_WRAPPER;
            descriptor =
                insertValue(builder, location, descriptor,
                            llvmConstant(builder, location, i32, flags), 1);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, declaration.getId()), 2);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, layout.size), 3);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, layout.alignment), 4);
            if (auto base = declaration.getBase())
              descriptor =
                  insertValue(builder, location, descriptor,
                              LLVM::AddressOfOp::create(
                                  builder, location, pointer,
                                  managedClassDescriptorName(
                                      FlatSymbolRefAttr::get(context, *base))),
                              5);
            if (!interfaceIDs.empty())
              descriptor =
                  insertValue(builder, location, descriptor,
                              LLVM::AddressOfOp::create(
                                  builder, location, pointer, interfacesName),
                              6);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, interfaceIDs.size()), 7);
            descriptor = insertValue(builder, location, descriptor,
                                     LLVM::AddressOfOp::create(
                                         builder, location, pointer, traceName),
                                     8);
            if (!layout.methods.empty())
              descriptor =
                  insertValue(builder, location, descriptor,
                              LLVM::AddressOfOp::create(builder, location,
                                                        pointer, methodsName),
                              9);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, layout.methods.size()),
                10);
            if (!debug.empty())
              descriptor =
                  insertValue(builder, location, descriptor,
                              LLVM::AddressOfOp::create(builder, location,
                                                        pointer, debugName),
                              11);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, debug.size()), 12);
            return descriptor;
          });
      materialized.insert(declaration.getSymName());
    }
    if (!progress)
      return module.emitError(
          "could not topologically materialize managed class descriptors");
  }
  return success();
}

LogicalResult normalizeManagedDirectCalls(ModuleOp module) {
  SmallVector<sim::SimClassDirectCallOp> calls;
  module.walk([&](sim::SimClassDirectCallOp call) { calls.push_back(call); });
  IRRewriter rewriter(module.getContext());
  for (sim::SimClassDirectCallOp call : calls) {
    sim::SimFuncOp function = call->getParentOfType<sim::SimFuncOp>();
    if (!function || function.getBody().empty() ||
        function.getBody().front().getNumArguments() == 0 ||
        !isa<sim::ContextType>(
            function.getBody().front().getArgument(0).getType()))
      return call.emitError(
          "managed direct call has no dominating simulation context");
    SmallVector<Value> operands{function.getBody().front().getArgument(0),
                                call.getReceiver()};
    llvm::append_range(operands, call.getArguments());
    rewriter.setInsertionPoint(call);
    auto replacement = sim::SimCallOp::create(
        rewriter, call.getLoc(), call.getResultTypes(), call.getCalleeAttr(),
        operands, ArrayAttr{}, ArrayAttr{});
    rewriter.replaceOp(call, replacement.getResults());
  }
  return success();
}

/// Arithmetic selects only support arithmetic and shaped result types. The
/// simulation dialect nevertheless permits class handles to flow through SSA
/// merges, and a conditional class assignment can therefore be represented as
/// an arith.select before native type conversion. Do not rely on the arithmetic
/// folder seeing that foreign type: spell these selects as ordinary CFG joins
/// while their managed type and root liveness are still visible.
void expandManagedSelectsToCFG(ModuleOp module) {
  SmallVector<arith::SelectOp> selects;
  module.walk([&](arith::SelectOp select) {
    if (sim::isManagedHandleType(select.getType()))
      selects.push_back(select);
  });

  IRRewriter rewriter(module.getContext());
  for (arith::SelectOp select : selects) {
    Block *head = select->getBlock();
    Block *continuation = rewriter.splitBlock(head, select->getIterator());
    BlockArgument merged =
        continuation->addArgument(select.getType(), select.getLoc());
    select.getResult().replaceAllUsesWith(merged);
    Value condition = select.getCondition();
    Value trueValue = select.getTrueValue();
    Value falseValue = select.getFalseValue();
    rewriter.eraseOp(select);

    rewriter.setInsertionPointToEnd(head);
    cf::CondBranchOp::create(rewriter, merged.getLoc(), condition, continuation,
                             ValueRange{trueValue}, continuation,
                             ValueRange{falseValue});
  }
}

bool managedOperationMayCollect(Operation *operation) {
  return isa<
      sim::SimClassAllocOp, sim::SimClassCopyOp, sim::SimWeakCreateOp,
      sim::SimReferencePathIndexOp, sim::SimReferencePathAssocOp,
      sim::SimContainerCreateLikeOp, sim::SimContainerCreateOp,
      sim::SimContainerCloneOp, sim::SimContainerWriteOp, sim::SimQueueInsertOp,
      sim::SimAssocCreateOp, sim::SimAssocWriteOp, sim::SimAssocSetDefaultOp,
      sim::SimAssocTraverseOp, sim::SimArgumentRefStoreOp,
      sim::SimReferencePathNBAEnqueueOp, sim::SimGCSafepointOp,
      sim::SimStringLiteralOp, sim::SimStringFromPackedOp,
      sim::SimStringConcatOp, sim::SimStringRepeatOp, sim::SimStringPutcOp,
      sim::SimStringSubstrOp, sim::SimStringCaseConvertOp,
      sim::SimStringFormatIntegerOp, sim::SimStringFormatRealOp,
      sim::SimFileGetlineStringOp, sim::SimCallOp, sim::SimClassDirectCallOp,
      sim::SimClassVirtualCallOp, sim::SimDPICallOp>(operation);
}

LogicalResult instrumentManagedRoots(ModuleOp module) {
  SmallVector<sim::SimFuncOp> functions;
  module.walk([&](sim::SimFuncOp function) {
    if (!function.isExternal())
      functions.push_back(function);
  });
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  Type rootType =
      LLVM::LLVMStructType::getLiteral(context, {pointer, i64, pointer, i64});
  for (sim::SimFuncOp function : functions) {
    Liveness liveness(function);
    struct ManagedSSAValue {
      Value value;
      uint64_t bitOffset;
    };
    SmallVector<ManagedSSAValue> handles;
    SmallVector<Operation *> collectionPoints;
    auto collectHandles = [&](Value value) -> LogicalResult {
      SmallVector<uint64_t, 2> offsets;
      if (isa<sim::ManagedRefType, sim::ArgumentRefType>(value.getType()))
        offsets.push_back(0);
      else if (!sim::getManagedHandleOffsets(value.getType(), offsets))
        return failure();
      for (uint64_t offset : offsets)
        handles.push_back({value, offset});
      return success();
    };
    for (Block &block : function.getBody()) {
      for (BlockArgument argument : block.getArguments())
        if (failed(collectHandles(argument)))
          return function.emitError(
              "block argument has no fixed managed root layout");
      for (Operation &operation : block) {
        if (managedOperationMayCollect(&operation))
          collectionPoints.push_back(&operation);
        for (Value result : operation.getResults())
          if (failed(collectHandles(result)))
            return operation.emitError(
                "result has no fixed managed root layout");
      }
    }
    if (handles.empty() || collectionPoints.empty())
      continue;
    llvm::erase_if(handles, [&](const ManagedSSAValue &handle) {
      return llvm::none_of(collectionPoints, [&](Operation *point) {
        const LivenessBlockInfo *blockInfo =
            liveness.getLiveness(point->getBlock());
        if (!blockInfo)
          return false;
        Liveness::ValueSetT live = blockInfo->currentlyLiveValues(point);
        return live.contains(handle.value) &&
               handle.value.getDefiningOp() != point;
      });
    });
    if (handles.empty())
      continue;

    OpBuilder builder(context);
    Block &entry = function.getBody().front();
    builder.setInsertionPointToStart(&entry);
    Location location = function.getLoc();
    Value rootCount = llvmConstant(builder, location, i64, handles.size());
    SmallVector<Value> slots;
    slots.reserve(handles.size());
    Value slotsBase = LLVM::AllocaOp::create(builder, location, pointer,
                                             pointer, rootCount, 8);
    for (size_t index = 0; index != handles.size(); ++index)
      slots.push_back(
          byteGEP(builder, location, slotsBase, index * sizeof(void *)));
    Value one = llvmConstant(builder, location, i64, 1);
    Value record =
        LLVM::AllocaOp::create(builder, location, pointer, rootType, one, 8);
    record.getDefiningOp()->setAttr(kManagedRootRangeRecordAttr,
                                    builder.getUnitAttr());

    // Each coroutine activation owns roots only while it is running on the
    // current worker lane. Pop before suspension and reacquire on the resume
    // block so a continuation may migrate to another host thread.
    llvm::SetVector<Block *> activationBlocks;
    activationBlocks.insert(&entry);
    function.walk([&](Operation *operation) {
      if (sim::isSuspensionOp(operation) &&
          operation->getNumSuccessors() != 0)
        activationBlocks.insert(operation->getSuccessor(0));
    });
    DenseMap<Block *, Operation *> activationEnds;
    for (Block *block : activationBlocks) {
      if (block == &entry)
        builder.setInsertionPointAfter(record.getDefiningOp());
      else
        builder.setInsertionPointToStart(block);
      Value contextAddress = LLVM::AddressOfOp::create(
          builder, location, pointer, "__obelisk_current_context");
      Value runtimeContext =
          LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
      Value lane =
          LLVM::CallOp::create(
              builder, location, TypeRange{pointer},
              SymbolRefAttr::get(context, "obelisk_rt_v1_gc_current_lane"),
              runtimeContext)
              .getResult();
      Operation *last = lane.getDefiningOp();
      for (Value slot : slots)
        LLVM::StoreOp::create(builder, location,
                              LLVM::ZeroOp::create(builder, location, pointer),
                              slot, 8);
      LLVM::StoreOp::create(builder, location,
                            LLVM::ZeroOp::create(builder, location, rootType),
                            record, 8);
      Value status =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context,
                                 "obelisk_rt_v1_gc_managed_root_range_push"),
              ValueRange{lane, record, slotsBase, rootCount})
              .getResult();
      sim::SimStatusCheckOp check =
          reportManagedStatus(builder, location, runtimeContext, status);
      check->setAttr(kManagedRootRangePushCheckAttr, builder.getUnitAttr());
      last = check.getOperation();
      activationEnds[block] = last;
    }

    for (auto [handle, slot] : llvm::zip_equal(handles, slots)) {
      if (auto argument = dyn_cast<BlockArgument>(handle.value)) {
        Block *owner = argument.getOwner();
        if (Operation *activationEnd = activationEnds.lookup(owner))
          builder.setInsertionPointAfter(activationEnd);
        else
          builder.setInsertionPointToStart(owner);
      } else {
        builder.setInsertionPointAfter(cast<OpResult>(handle.value).getOwner());
      }
      sim::SimClassRootBindOp::create(
          builder, handle.value.getLoc(), handle.value, slot,
          builder.getI64IntegerAttr(handle.bitOffset));
    }

    // A root slot outlives its SSA value and is reused on loop backedges.
    // Clear values that are dead at each collection-capable operation so weak
    // reachability follows SSA liveness and a later collection never observes
    // a pointer into an already reclaimed span.
    for (Operation *collectionPoint : collectionPoints) {
      const LivenessBlockInfo *blockInfo =
          liveness.getLiveness(collectionPoint->getBlock());
      if (!blockInfo)
        continue;
      Liveness::ValueSetT live =
          blockInfo->currentlyLiveValues(collectionPoint);
      builder.setInsertionPoint(collectionPoint);
      for (auto [handle, slot] : llvm::zip_equal(handles, slots)) {
        if (live.contains(handle.value) &&
            handle.value.getDefiningOp() != collectionPoint)
          continue;
        LLVM::StoreOp::create(
            builder, collectionPoint->getLoc(),
            LLVM::ZeroOp::create(builder, collectionPoint->getLoc(), pointer),
            slot, 8);
      }
    }

    SmallVector<Operation *> exits;
    function.walk([&](Operation *operation) {
      if (isa<sim::SimReturnOp>(operation) ||
          sim::isSuspensionOp(operation))
        exits.push_back(operation);
    });
    for (Operation *exit : exits) {
      builder.setInsertionPoint(exit);
      emitManagedRootRangePop(builder, exit->getLoc(), function);
    }
  }
  return success();
}

void makeCurrentContextGlobal(ModuleOp module) {
  if (module.lookupSymbol("__obelisk_current_context"))
    return;
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  Type pointer = LLVM::LLVMPointerType::get(module.getContext());
  auto global = LLVM::GlobalOp::create(
      builder, module.getLoc(), pointer, false, LLVM::Linkage::Internal,
      "__obelisk_current_context", Attribute{}, 8, 0, false, true);
  Block *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToStart(block);
  LLVM::ReturnOp::create(
      builder, module.getLoc(),
      LLVM::ZeroOp::create(builder, module.getLoc(), pointer));
}

void makeStaticSpecializationFastGlobal(ModuleOp module) {
  constexpr StringLiteral name = "__obelisk_static_specialization_fast_v1";
  if (module.lookupSymbol(name))
    return;
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  Type i32 = builder.getI32Type();
  auto global =
      LLVM::GlobalOp::create(builder, module.getLoc(), i32, false,
                             LLVM::Linkage::Internal, name, Attribute{}, 4);
  Block *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToStart(block);
  LLVM::ReturnOp::create(
      builder, module.getLoc(),
      llvmConstant(builder, module.getLoc(), i32, uint32_t{0}));
}

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

Value managedObjectPointer(OpBuilder &builder, Location location,
                           Value handle) {
  return LLVM::IntToPtrOp::create(
      builder, location, LLVM::LLVMPointerType::get(builder.getContext()),
      handle);
}

Value managedObjectHandle(OpBuilder &builder, Location location, Value object) {
  return LLVM::PtrToIntOp::create(builder, location, builder.getI64Type(),
                                  object);
}

std::pair<Value, Value> managedContextAndLane(OpBuilder &builder,
                                              Location location) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Value address = LLVM::AddressOfOp::create(builder, location, pointer,
                                            "__obelisk_current_context");
  Value context = LLVM::LoadOp::create(builder, location, pointer, address, 8);
  Value lane =
      LLVM::CallOp::create(builder, location, TypeRange{pointer},
                           SymbolRefAttr::get(builder.getContext(),
                                              "obelisk_rt_v1_gc_current_lane"),
                           context)
          .getResult();
  return {context, lane};
}

sim::SimStatusCheckOp reportManagedStatus(OpBuilder &builder, Location location,
                                          Value context, Value status) {
  (void)context;
  Type statusType = runtime::StatusType::get(builder.getContext());
  Value runtimeStatus = runtime::RTStatusFromBitsOp::create(builder, location,
                                                            statusType, status);
  return sim::SimStatusCheckOp::create(builder, location, runtimeStatus);
}

class ClassNullConversion final
    : public OpConversionPattern<sim::SimClassNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassNullOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(op, arith::ConstantOp::create(
                               rewriter, op.getLoc(), rewriter.getI64Type(),
                               rewriter.getI64IntegerAttr(0)));
    return success();
  }
};

class ManagedNullConversion final
    : public OpConversionPattern<sim::SimManagedNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimManagedNullOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(op, arith::ConstantOp::create(
                               rewriter, op.getLoc(), rewriter.getI64Type(),
                               rewriter.getI64IntegerAttr(0)));
    return success();
  }
};

class ManagedIsNullConversion final
    : public OpConversionPattern<sim::SimManagedIsNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimManagedIsNullOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Value zero = llvmConstant(rewriter, op.getLoc(),
                              adaptor.getInput().front().getType(), 0);
    rewriter.replaceOpWithNewOp<arith::CmpIOp>(
        op, arith::CmpIPredicate::eq, adaptor.getInput().front(), zero);
    return success();
  }
};

class EventNullConversion final
    : public OpConversionPattern<sim::SimEventNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimEventNullOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(op, arith::ConstantOp::create(
                               rewriter, op.getLoc(), rewriter.getI64Type(),
                               rewriter.getI64IntegerAttr(-1)));
    return success();
  }
};

static Value zeroNativeValue(OpBuilder &builder, Location location, Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getIntegerAttr(integer, 0));
  if (auto floating = dyn_cast<FloatType>(type))
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getFloatAttr(floating, 0.0));
  return {};
}

class ContainerSizeConversion final
    : public OpConversionPattern<sim::SimContainerSizeOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerSizeOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContainer().size() != 1)
      return failure();
    rewriter.replaceOp(
        op, LLVM::CallOp::create(
                rewriter, op.getLoc(), TypeRange{rewriter.getI64Type()},
                SymbolRefAttr::get(rewriter.getContext(),
                                   "obelisk_rt_v1_container_size"),
                managedObjectPointer(rewriter, op.getLoc(),
                                     adaptor.getContainer().front()))
                .getResult());
    return success();
  }
};

class ContainerCreateLikeConversion final
    : public OpConversionPattern<sim::SimContainerCreateLikeOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerCreateLikeOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getPreferred().size() != 1 ||
        adaptor.getFallback().size() != 1 || adaptor.getSize().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_container_create_like"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getPreferred().front()),
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getFallback().front()),
                       adaptor.getSize().front(), output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value result =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), result));
    return success();
  }
};

class ContainerCreateConversion final
    : public OpConversionPattern<sim::SimContainerCreateOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerCreateOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getSize().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    auto c32 = [&](uint32_t value) {
      return llvmConstant(rewriter, op.getLoc(), i32, value);
    };
    auto c64 = [&](uint64_t value) {
      return llvmConstant(rewriter, op.getLoc(), i64, value);
    };
    ArrayRef<int64_t> traceOffsets = op.getTraceOffsets();
    ArrayRef<int32_t> traceKinds = op.getTraceKinds();
    Value traceSlots = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (!traceOffsets.empty()) {
      std::string bytes(
          traceOffsets.size() * sizeof(obelisk_rt_element_trace_slot_v1), '\0');
      for (auto [index, offset, kind] :
           llvm::enumerate(traceOffsets, traceKinds)) {
        obelisk_rt_element_trace_slot_v1 slot{
            static_cast<uint64_t>(offset),
            static_cast<obelisk_rt_managed_slot_kind_v1>(kind), 0};
        std::memcpy(bytes.data() +
                        index * sizeof(obelisk_rt_element_trace_slot_v1),
                    &slot, sizeof(slot));
      }
      ModuleOp module = op->getParentOfType<ModuleOp>();
      std::string name =
          "__obelisk_element_trace_" + std::to_string(op.getTypeId());
      LLVM::GlobalOp global = module.lookupSymbol<LLVM::GlobalOp>(name);
      if (!global)
        global = makeByteArrayGlobal(module, op.getLoc(), name, bytes);
      traceSlots = LLVM::AddressOfOp::create(rewriter, op.getLoc(), global);
    }
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_container_create_typed"),
            ValueRange{lane, c32(op.getContainerKind()), c64(op.getTypeId()),
                       c32(op.getElementKind()), c32(op.getElementFlags()),
                       c64(op.getValueSize()), c64(op.getAlignment()),
                       c64(op.getBitWidth()), traceSlots,
                       c64(traceOffsets.size()), adaptor.getSize().front(),
                       c64(op.getBound()), output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value result =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), result));
    return success();
  }
};

class ContainerCloneConversion final
    : public OpConversionPattern<sim::SimContainerCloneOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerCloneOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_container_clone"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getInput().front()),
                       output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value result =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), result));
    return success();
  }
};

class ContainerDeleteConversion final
    : public OpConversionPattern<sim::SimContainerDeleteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerDeleteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContainer().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_container_delete"),
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getContainer().front()))
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class QueueDeleteConversion final
    : public OpConversionPattern<sim::SimQueueDeleteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimQueueDeleteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getQueue().size() != 1 || adaptor.getIndex().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_queue_delete_index"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getQueue().front()),
                       adaptor.getIndex().front()})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class QueueInsertConversion final
    : public OpConversionPattern<sim::SimQueueInsertOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimQueueInsertOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getQueue().size() != 1 || adaptor.getIndex().size() != 1 ||
        adaptor.getValue().empty() || adaptor.getValue().size() > 2)
      return failure();
    SmallVector<Value> storage;
    for (Value value : adaptor.getValue()) {
      Value slot = entryAlloca(rewriter, op.getLoc(), value.getType(), 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), value, slot, 8);
      storage.push_back(slot);
    }
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown =
        storage.size() == 2
            ? storage[1]
            : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_queue_insert"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getQueue().front()),
                       adaptor.getIndex().front(), storage.front(), unknown})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class RandomNextConversion final
    : public OpConversionPattern<sim::SimRandomNextOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimRandomNextOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContext().size() != 1)
      return failure();
    Value context = adaptor.getContext().front();
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_random_next"),
                       ValueRange{context, output})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(op,
                       LLVM::LoadOp::create(rewriter, op.getLoc(),
                                            rewriter.getI64Type(), output, 8));
    return success();
  }
};

class RandomSeedConversion final
    : public OpConversionPattern<sim::SimRandomSeedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimRandomSeedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContext().size() != 1 || adaptor.getSeed().size() != 1)
      return failure();
    Value context = adaptor.getContext().front();
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_random_seed"),
                       ValueRange{context, adaptor.getSeed().front()})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class RandomBoundedConversion final
    : public OpConversionPattern<sim::SimRandomBoundedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimRandomBoundedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContext().size() != 1 || adaptor.getBound().size() != 1)
      return failure();
    Value context = adaptor.getContext().front();
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_random_bounded"),
                       ValueRange{context, adaptor.getBound().front(), output})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(op,
                       LLVM::LoadOp::create(rewriter, op.getLoc(),
                                            rewriter.getI64Type(), output, 8));
    return success();
  }
};

class ContainerReadConversion final
    : public OpConversionPattern<sim::SimContainerReadOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerReadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContainer().size() != 1 || adaptor.getIndex().size() != 1)
      return failure();
    SmallVector<Type> types;
    if (failed(
            getTypeConverter()->convertType(op.getResult().getType(), types)) ||
        types.empty() || types.size() > 2)
      return failure();
    SmallVector<Value> storage;
    for (Type type : types) {
      Value zero = zeroNativeValue(rewriter, op.getLoc(), type);
      if (!zero)
        return failure();
      Value slot = entryAlloca(rewriter, op.getLoc(), type, 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), zero, slot, 8);
      storage.push_back(slot);
    }
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown =
        storage.size() == 2
            ? storage[1]
            : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_container_read"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getContainer().front()),
                       adaptor.getIndex().front(), storage.front(), unknown})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    SmallVector<Value> values;
    for (auto [type, slot] : llvm::zip_equal(types, storage))
      values.push_back(
          LLVM::LoadOp::create(rewriter, op.getLoc(), type, slot, 8));
    rewriter.replaceOpWithMultiple(op, {ValueRange(values)});
    return success();
  }
};

class ContainerWriteConversion final
    : public OpConversionPattern<sim::SimContainerWriteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerWriteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContainer().size() != 1 || adaptor.getIndex().size() != 1 ||
        adaptor.getValue().empty() || adaptor.getValue().size() > 2)
      return failure();
    SmallVector<Value> storage;
    for (Value value : adaptor.getValue()) {
      Value slot = entryAlloca(rewriter, op.getLoc(), value.getType(), 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), value, slot, 8);
      storage.push_back(slot);
    }
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown =
        storage.size() == 2
            ? storage[1]
            : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_container_write"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getContainer().front()),
                       adaptor.getIndex().front(), storage.front(), unknown})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

static Value makeNativeAssocKey(OpBuilder &builder, Location location,
                                sim::AssocArrayType array, ValueRange values) {
  Type i8 = builder.getI8Type();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Value storage =
      entryAlloca(builder, location, i8, sizeof(obelisk_rt_assoc_key_v1), 8);
  auto store32 = [&](uint64_t offset, uint32_t value) {
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i32, value),
                          byteGEP(builder, location, storage, offset), 4);
  };
  auto store64 = [&](uint64_t offset, Value value) {
    LLVM::StoreOp::create(builder, location,
                          castIntegerWidth(builder, location, value, i64),
                          byteGEP(builder, location, storage, offset), 8);
  };
  bool stringKey = isa<sim::StringType>(array.getKeyType());
  uint32_t keyKind =
      stringKey ? OBELISK_RT_ASSOC_KEY_STRING
                : (array.getSignedKey() ? OBELISK_RT_ASSOC_KEY_SIGNED
                                        : OBELISK_RT_ASSOC_KEY_UNSIGNED);
  uint64_t keyWidth = stringKey ? 0 : *sim::getPackedWidth(array.getKeyType());
  store32(offsetof(obelisk_rt_assoc_key_v1, kind), keyKind);
  store32(offsetof(obelisk_rt_assoc_key_v1, reserved), 0);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, keyWidth),
                        byteGEP(builder, location, storage,
                                offsetof(obelisk_rt_assoc_key_v1, width)),
                        8);
  Value zero = llvmConstant(builder, location, i64, 0);
  if (stringKey) {
    LLVM::StoreOp::create(builder, location, zero,
                          byteGEP(builder, location, storage,
                                  offsetof(obelisk_rt_assoc_key_v1, value)),
                          8);
    LLVM::StoreOp::create(builder, location, zero,
                          byteGEP(builder, location, storage,
                                  offsetof(obelisk_rt_assoc_key_v1, unknown)),
                          8);
    store64(offsetof(obelisk_rt_assoc_key_v1, string), values.front());
  } else {
    store64(offsetof(obelisk_rt_assoc_key_v1, value), values.front());
    if (values.size() == 2)
      store64(offsetof(obelisk_rt_assoc_key_v1, unknown), values[1]);
    else
      LLVM::StoreOp::create(builder, location, zero,
                            byteGEP(builder, location, storage,
                                    offsetof(obelisk_rt_assoc_key_v1, unknown)),
                            8);
    LLVM::StoreOp::create(builder, location, zero,
                          byteGEP(builder, location, storage,
                                  offsetof(obelisk_rt_assoc_key_v1, string)),
                          8);
  }
  return storage;
}

static SmallVector<Value> makeNativeValueStorage(OpBuilder &builder,
                                                 Location location,
                                                 ValueRange values) {
  SmallVector<Value> storage;
  for (Value value : values) {
    Value slot = entryAlloca(builder, location, value.getType(), 1, 8);
    LLVM::StoreOp::create(builder, location, value, slot, 8);
    storage.push_back(slot);
  }
  return storage;
}

static Value makeNativeAssocTrace(OpBuilder &builder, Location location,
                                  Operation *operation, uint64_t typeID,
                                  ArrayRef<int64_t> offsets,
                                  ArrayRef<int32_t> kinds) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  if (offsets.empty())
    return LLVM::ZeroOp::create(builder, location, pointer);
  std::string bytes(offsets.size() * sizeof(obelisk_rt_element_trace_slot_v1),
                    '\0');
  for (auto [index, offset, kind] : llvm::enumerate(offsets, kinds)) {
    obelisk_rt_element_trace_slot_v1 slot{
        static_cast<uint64_t>(offset),
        static_cast<obelisk_rt_managed_slot_kind_v1>(kind), 0};
    std::memcpy(bytes.data() + index * sizeof(obelisk_rt_element_trace_slot_v1),
                &slot, sizeof(slot));
  }
  ModuleOp module = operation->getParentOfType<ModuleOp>();
  std::string name = "__obelisk_element_trace_" + std::to_string(typeID);
  LLVM::GlobalOp global = module.lookupSymbol<LLVM::GlobalOp>(name);
  if (!global)
    global = makeByteArrayGlobal(module, location, name, bytes);
  return LLVM::AddressOfOp::create(builder, location, global);
}

class AssocCreateConversion final
    : public OpConversionPattern<sim::SimAssocCreateOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocCreateOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value trace =
        makeNativeAssocTrace(rewriter, op.getLoc(), op, op.getTypeId(),
                             op.getTraceOffsets(), op.getTraceKinds());
    auto c32 = [&](uint32_t value) {
      return llvmConstant(rewriter, op.getLoc(), i32, value);
    };
    auto c64 = [&](uint64_t value) {
      return llvmConstant(rewriter, op.getLoc(), i64, value);
    };
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_create_typed"),
            ValueRange{lane, c64(op.getTypeId()), c32(op.getElementKind()),
                       c32(op.getElementFlags()), c64(op.getValueSize()),
                       c64(op.getAlignment()), c64(op.getBitWidth()), trace,
                       c64(op.getTraceOffsets().size()), c32(op.getKeyKind()),
                       c64(op.getKeyWidth()), output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value result =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), result));
    return success();
  }
};

class AssocReadConversion final
    : public OpConversionPattern<sim::SimAssocReadOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocReadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    SmallVector<Type> types;
    if (failed(
            getTypeConverter()->convertType(op.getResult().getType(), types)) ||
        types.empty() || types.size() > 2)
      return failure();
    SmallVector<Value> storage;
    for (Type type : types) {
      Value zero = zeroNativeValue(rewriter, op.getLoc(), type);
      if (!zero)
        return failure();
      Value slot = entryAlloca(rewriter, op.getLoc(), type, 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), zero, slot, 8);
      storage.push_back(slot);
    }
    sim::AssocArrayType array = op.getArray().getType();
    Value key =
        makeNativeAssocKey(rewriter, op.getLoc(), array, adaptor.getKey());
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown =
        storage.size() == 2
            ? storage[1]
            : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
    Value present =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    Value planeSize =
        llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                     (sim::getPackedWidth(op.getResult().getType())
                          .value_or(static_cast<unsigned>(
                              types.front().getIntOrFloatBitWidth())) +
                      7) /
                         8);
    if (sim::isManagedHandleType(op.getResult().getType()))
      planeSize = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                               sizeof(void *));
    Value unknownSize =
        storage.size() == 2
            ? planeSize
            : llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_read_checked"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key, storage.front(), planeSize, unknown, unknownSize,
                       present})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    SmallVector<Value> values;
    for (auto [type, slot] : llvm::zip_equal(types, storage))
      values.push_back(
          LLVM::LoadOp::create(rewriter, op.getLoc(), type, slot, 8));
    rewriter.replaceOpWithMultiple(op, {ValueRange(values)});
    return success();
  }
};

template <typename Op, typename Adaptor>
static LogicalResult
lowerAssocValueMutation(Op op, Adaptor adaptor,
                        ConversionPatternRewriter &rewriter, StringRef callee) {
  if (adaptor.getArray().size() != 1 || adaptor.getValue().empty() ||
      adaptor.getValue().size() > 2)
    return failure();
  SmallVector<Value> storage =
      makeNativeValueStorage(rewriter, op.getLoc(), adaptor.getValue());
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  Value unknown =
      storage.size() == 2
          ? storage[1]
          : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
  uint64_t bytes = 0;
  if (auto width = sim::getPackedWidth(op.getValue().getType()))
    bytes = (*width + 7) / 8;
  else if (sim::isManagedHandleType(op.getValue().getType()))
    bytes = sizeof(void *);
  else if (isa<sim::EventType>(op.getValue().getType()))
    bytes = sizeof(uint64_t);
  else if (auto floating = dyn_cast<FloatType>(op.getValue().getType()))
    bytes = floating.getWidth() / 8;
  else if (auto span = sim::getProvenanceSpan(op.getValue().getType()))
    bytes = (*span + 7) / 8;
  if (bytes == 0)
    return failure();
  Value size =
      llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), bytes);
  Value unknownSize =
      storage.size() == 2
          ? size
          : llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
  SmallVector<Value> arguments{
      managedContextAndLane(rewriter, op.getLoc()).second,
      managedObjectPointer(rewriter, op.getLoc(), adaptor.getArray().front())};
  if constexpr (std::is_same_v<Op, sim::SimAssocWriteOp>) {
    if (adaptor.getKey().empty() || adaptor.getKey().size() > 2)
      return failure();
    arguments.push_back(makeNativeAssocKey(
        rewriter, op.getLoc(), op.getArray().getType(), adaptor.getKey()));
  }
  arguments.append({storage.front(), size, unknown, unknownSize});
  auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
  (void)lane;
  Value status =
      LLVM::CallOp::create(
          rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
          SymbolRefAttr::get(rewriter.getContext(), callee), arguments)
          .getResult();
  reportManagedStatus(rewriter, op.getLoc(), context, status);
  rewriter.eraseOp(op);
  return success();
}

class AssocWriteConversion final
    : public OpConversionPattern<sim::SimAssocWriteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocWriteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    return lowerAssocValueMutation(op, adaptor, rewriter,
                                   "obelisk_rt_v1_assoc_write_checked");
  }
};

class AssocDefaultConversion final
    : public OpConversionPattern<sim::SimAssocSetDefaultOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocSetDefaultOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    return lowerAssocValueMutation(op, adaptor, rewriter,
                                   "obelisk_rt_v1_assoc_set_default_checked");
  }
};

class AssocExistsConversion final
    : public OpConversionPattern<sim::SimAssocExistsOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocExistsOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    Value key = makeNativeAssocKey(rewriter, op.getLoc(),
                                   op.getArray().getType(), adaptor.getKey());
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_exists"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key, output})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value loaded = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                        rewriter.getI32Type(), output, 4);
    rewriter.replaceOpWithNewOp<arith::TruncIOp>(op, rewriter.getI1Type(),
                                                 loaded);
    return success();
  }
};

class AssocDeleteConversion final
    : public OpConversionPattern<sim::SimAssocDeleteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocDeleteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    Value key = makeNativeAssocKey(rewriter, op.getLoc(),
                                   op.getArray().getType(), adaptor.getKey());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_delete"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class AssocTraverseConversion final
    : public OpConversionPattern<sim::SimAssocTraverseOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocTraverseOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    sim::AssocArrayType array = op.getArray().getType();
    Value key =
        makeNativeAssocKey(rewriter, op.getLoc(), array, adaptor.getKey());
    Value successSlot =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    StringRef callee;
    int32_t direction = static_cast<int32_t>(op.getDirection());
    if (op.getEndpoint())
      callee = direction > 0 ? "obelisk_rt_v1_assoc_first"
                             : "obelisk_rt_v1_assoc_last";
    else
      callee = direction > 0 ? "obelisk_rt_v1_assoc_next"
                             : "obelisk_rt_v1_assoc_prev";
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), callee),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key, successSlot})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    SmallVector<Value> keyValues;
    Type i64 = rewriter.getI64Type();
    if (isa<sim::StringType>(array.getKeyType())) {
      Value loaded = LLVM::LoadOp::create(
          rewriter, op.getLoc(), i64,
          byteGEP(rewriter, op.getLoc(), key,
                  offsetof(obelisk_rt_assoc_key_v1, string)),
          8);
      keyValues.push_back(loaded);
    } else {
      SmallVector<Type> converted;
      if (failed(
              getTypeConverter()->convertType(array.getKeyType(), converted)) ||
          converted.empty() || converted.size() > 2)
        return failure();
      for (auto [index, type] : llvm::enumerate(converted)) {
        uint64_t offset = index == 0
                              ? offsetof(obelisk_rt_assoc_key_v1, value)
                              : offsetof(obelisk_rt_assoc_key_v1, unknown);
        Value loaded = LLVM::LoadOp::create(
            rewriter, op.getLoc(), i64,
            byteGEP(rewriter, op.getLoc(), key, offset), 8);
        keyValues.push_back(
            castIntegerWidth(rewriter, op.getLoc(), loaded, type));
      }
    }
    Value success32 = LLVM::LoadOp::create(
        rewriter, op.getLoc(), rewriter.getI32Type(), successSlot, 4);
    Value success1 = arith::TruncIOp::create(rewriter, op.getLoc(),
                                             rewriter.getI1Type(), success32);
    SmallVector<ValueRange> replacements;
    replacements.push_back(keyValues);
    replacements.push_back(ValueRange{success1});
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

static Value finishStringAllocation(ConversionPatternRewriter &rewriter,
                                    Location location, Value context,
                                    Value status, Value output) {
  reportManagedStatus(rewriter, location, context, status);
  return LLVM::LoadOp::create(rewriter, location, rewriter.getI64Type(), output,
                              8);
}

class StringLiteralConversion final
    : public OpConversionPattern<sim::SimStringLiteralOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringLiteralOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ModuleOp module = op->getParentOfType<ModuleOp>();
    if (!module)
      return failure();
    std::string name;
    for (uint64_t suffix = 0;; ++suffix) {
      name = ("__obelisk_string_literal." + Twine(suffix)).str();
      if (!module.lookupSymbol(name))
        break;
    }
    StringRef bytes = op.getValue();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value data = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (!bytes.empty()) {
      LLVM::GlobalOp global =
          makeByteArrayGlobal(module, op.getLoc(), name, bytes);
      data = LLVM::AddressOfOp::create(rewriter, op.getLoc(), global);
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()),
        output, 8);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_string_create"),
            ValueRange{lane, data,
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), bytes.size()),
                       output})
            .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }
};

class StringFromPackedConversion final
    : public OpConversionPattern<sim::SimStringFromPackedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringFromPackedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    std::optional<unsigned> width =
        sim::getPackedWidth(op.getInput().getType());
    if (!width ||
        (adaptor.getInput().size() != 1 && adaptor.getInput().size() != 2))
      return failure();
    Type planeType = adaptor.getInput().front().getType();
    Value value = entryAlloca(rewriter, op.getLoc(), planeType, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(), adaptor.getInput().front(),
                          value, 8);
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (adaptor.getInput().size() == 2) {
      unknown = entryAlloca(rewriter, op.getLoc(), planeType, 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), adaptor.getInput()[1],
                            unknown, 8);
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()),
        output, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_string_from_packed"),
                       ValueRange{lane, value, unknown,
                                  llvmConstant(rewriter, op.getLoc(),
                                               rewriter.getI64Type(), *width),
                                  output})
                       .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }
};

class StringToPackedConversion final
    : public OpConversionPattern<sim::SimStringToPackedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringToPackedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto resultType = dyn_cast<IntegerType>(
        getTypeConverter()->convertType(op.getResult().getType()));
    if (!resultType || adaptor.getInput().size() != 1)
      return failure();
    Value output = entryAlloca(rewriter, op.getLoc(), resultType, 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), resultType), output, 8);
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_string_to_packed"),
            ValueRange{adaptor.getInput().front(), output,
                       LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(),
                                    resultType.getWidth())})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), resultType, output, 8));
    return success();
  }
};

class StringConcatConversion final
    : public OpConversionPattern<sim::SimStringConcatOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringConcatOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type i64 = rewriter.getI64Type();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value spans = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (!adaptor.getInputs().empty()) {
      spans = entryAlloca(rewriter, op.getLoc(), i64,
                          adaptor.getInputs().size(), 8);
      for (auto [index, input] : llvm::enumerate(adaptor.getInputs())) {
        if (input.size() != 1)
          return failure();
        LLVM::StoreOp::create(
            rewriter, op.getLoc(), input.front(),
            byteGEP(rewriter, op.getLoc(), spans, index * sizeof(uint64_t)), 8);
      }
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i64),
                          output, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_string_concat_many"),
                       ValueRange{lane, spans,
                                  llvmConstant(rewriter, op.getLoc(), i64,
                                               adaptor.getInputs().size()),
                                  output})
                       .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }
};

template <typename Op>
class StringAllocatingCallConversion final : public OpConversionPattern<Op> {
public:
  StringAllocatingCallConversion(const TypeConverter &converter,
                                 MLIRContext *context, StringRef symbol)
      : OpConversionPattern<Op>(converter, context), symbol(symbol) {}

  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Value> operands;
    for (ValueRange range : adaptor.getOperands()) {
      if (range.size() != 1)
        return failure();
      operands.push_back(range.front());
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    operands.insert(operands.begin(), lane);
    if constexpr (std::is_same_v<Op, sim::SimStringPutcOp>)
      operands.back() = arith::ExtUIOp::create(
          rewriter, op.getLoc(), rewriter.getI32Type(), operands.back());
    if constexpr (std::is_same_v<Op, sim::SimStringCaseConvertOp>)
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(),
                                      op.getToUpper() ? 1 : 0));
    if constexpr (std::is_same_v<Op, sim::SimStringFormatIntegerOp>) {
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(), op.getRadix()));
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(),
                                      op.getIsSigned() ? 1 : 0));
    }
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()),
        output, 8);
    operands.push_back(output);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), symbol), operands)
            .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }

private:
  std::string symbol;
};

class StringLengthConversion final
    : public OpConversionPattern<sim::SimStringLengthOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringLengthOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(
        op, LLVM::CallOp::create(
                rewriter, op.getLoc(), TypeRange{rewriter.getI64Type()},
                SymbolRefAttr::get(rewriter.getContext(),
                                   "obelisk_rt_v1_string_length"),
                adaptor.getInput().front())
                .getResult());
    return success();
  }
};

class StringGetcConversion final
    : public OpConversionPattern<sim::SimStringGetcOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringGetcOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Value result =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_string_getc"),
            ValueRange{adaptor.getInput().front(), adaptor.getIndex().front()})
            .getResult();
    rewriter.replaceOp(op,
                       arith::TruncIOp::create(rewriter, op.getLoc(),
                                               rewriter.getI8Type(), result));
    return success();
  }
};

class StringCompareConversion final
    : public OpConversionPattern<sim::SimStringCompareOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringCompareOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    StringRef symbol = op.getCaseInsensitive()
                           ? "obelisk_rt_v1_string_compare_insensitive"
                           : "obelisk_rt_v1_string_compare";
    rewriter.replaceOp(
        op, LLVM::CallOp::create(
                rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                SymbolRefAttr::get(rewriter.getContext(), symbol),
                ValueRange{adaptor.getLhs().front(), adaptor.getRhs().front()})
                .getResult());
    return success();
  }
};

template <typename Op>
class StringFileOpenConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI32Type()),
        output, 4);
    SmallVector<Value> operands{context, adaptor.getPath().front()};
    StringRef symbol = "obelisk_rt_v1_file_open_string_mcd";
    if constexpr (std::is_same_v<Op, sim::SimFileOpenStringOp>) {
      operands.push_back(adaptor.getMode().front());
      symbol = "obelisk_rt_v1_file_open_string";
    }
    operands.push_back(output);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), symbol), operands)
            .getResult();
    Value descriptor = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                            rewriter.getI32Type(), output, 4);
    Value ok = arith::CmpIOp::create(
        rewriter, op.getLoc(), arith::CmpIPredicate::eq, status,
        llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(), 0));
    rewriter.replaceOp(op, arith::SelectOp::create(
                               rewriter, op.getLoc(), ok, descriptor,
                               LLVM::ZeroOp::create(rewriter, op.getLoc(),
                                                    rewriter.getI32Type())));
    return success();
  }
};

class StringFileGetlineConversion final
    : public OpConversionPattern<sim::SimFileGetlineStringOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimFileGetlineStringOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Type i64 = rewriter.getI64Type();
    Type i32 = rewriter.getI32Type();
    Value stringOutput = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    Value countOutput = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i64),
                          stringOutput, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i32),
                          countOutput, 4);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_file_getline_string"),
            ValueRange{context, lane, adaptor.getDescriptor().front(),
                       stringOutput, countOutput})
            .getResult();
    Value ok = arith::CmpIOp::create(
        rewriter, op.getLoc(), arith::CmpIPredicate::eq, status,
        llvmConstant(rewriter, op.getLoc(), i32, 0));
    Value string =
        LLVM::LoadOp::create(rewriter, op.getLoc(), i64, stringOutput, 8);
    Value count =
        LLVM::LoadOp::create(rewriter, op.getLoc(), i32, countOutput, 4);
    rewriter.replaceOp(
        op, ValueRange{arith::SelectOp::create(
                           rewriter, op.getLoc(), ok, string,
                           LLVM::ZeroOp::create(rewriter, op.getLoc(), i64)),
                       arith::SelectOp::create(
                           rewriter, op.getLoc(), ok, count,
                           LLVM::ZeroOp::create(rewriter, op.getLoc(), i32))});
    return success();
  }
};

template <typename Op>
class StringParseConversion final : public OpConversionPattern<Op> {
public:
  StringParseConversion(const TypeConverter &converter, MLIRContext *context,
                        StringRef symbol)
      : OpConversionPattern<Op>(converter, context), symbol(symbol) {}

  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = std::is_same_v<Op, sim::SimStringParseIntegerOp>
                          ? Type(rewriter.getI64Type())
                          : Type(rewriter.getF64Type());
    Value output = entryAlloca(rewriter, op.getLoc(), resultType, 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), resultType), output, 8);
    SmallVector<Value> operands{adaptor.getInput().front()};
    if constexpr (std::is_same_v<Op, sim::SimStringParseIntegerOp>)
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(), op.getRadix()));
    operands.push_back(output);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), symbol), operands)
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), resultType, output, 8));
    return success();
  }

private:
  std::string symbol;
};

class ClassAllocConversion final
    : public OpConversionPattern<sim::SimClassAllocOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassAllocOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    auto classType = cast<sim::ClassHandleType>(op.getResult().getType());
    Value descriptor = LLVM::AddressOfOp::create(
        rewriter, op.getLoc(), pointer,
        managedClassDescriptorName(classType.getClassName()));
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_object_allocate"),
                       ValueRange{lane, descriptor, output})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value object =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), object));
    return success();
  }
};

class CovergroupNullConversion final
    : public OpConversionPattern<sim::SimCovergroupNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupNullOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(
        op, LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()));
    return success();
  }
};

class CovergroupCreateConversion final
    : public OpConversionPattern<sim::SimCovergroupCreateOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupCreateOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
            op, op.getDeclarationAttr());
    if (!declaration)
      return failure();
    Type i64 = rewriter.getI64Type();
    Value bins = entryAlloca(rewriter, op.getLoc(), i64,
                             declaration.getCoverpointBins().size(), 8);
    for (auto [index, count] : llvm::enumerate(declaration.getCoverpointBins()))
      LLVM::StoreOp::create(
          rewriter, op.getLoc(),
          llvmConstant(rewriter, op.getLoc(), i64,
                       static_cast<uint64_t>(count)),
          byteGEP(rewriter, op.getLoc(), bins, index * sizeof(uint64_t)), 8);
    Value output = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i64),
                          output, 8);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_create"),
            ValueRange{
                context,
                llvmConstant(rewriter, op.getLoc(), i64, declaration.getId()),
                bins,
                llvmConstant(rewriter, op.getLoc(), i64,
                             declaration.getCoverpointBins().size()),
                output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), i64, output, 8));
    return success();
  }
};

template <typename Op, bool Enabled>
class CovergroupControlConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_set_enabled"),
            ValueRange{context, adaptor.getHandle().front(),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(), Enabled)})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupEnabledConversion final
    : public OpConversionPattern<sim::SimCovergroupSampleEnabledOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSampleEnabledOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    Type i32 = rewriter.getI32Type();
    Value output = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_sample_enabled"),
            ValueRange{context, adaptor.getHandle().front(), output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value enabled = LLVM::LoadOp::create(rewriter, op.getLoc(), i32, output, 4);
    rewriter.replaceOp(op,
                       LLVM::TruncOp::create(rewriter, op.getLoc(),
                                             rewriter.getI1Type(), enabled));
    return success();
  }
};

class CovergroupBinHitConversion final
    : public OpConversionPattern<sim::SimCovergroupBinHitOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupBinHitOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_bin_hit"),
            ValueRange{context, adaptor.getHandle().front(),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(), op.getCoverpoint()),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(), op.getBin())})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupSampleConversion final
    : public OpConversionPattern<sim::SimCovergroupSampleOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSampleOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    SmallVector<Value> hitValues = flatten(adaptor.getHits());
    Type i8 = rewriter.getI8Type();
    Value hits = entryAlloca(rewriter, op.getLoc(), i8, hitValues.size(), 1);
    for (auto [index, hit] : llvm::enumerate(hitValues))
      LLVM::StoreOp::create(
          rewriter, op.getLoc(),
          LLVM::ZExtOp::create(rewriter, op.getLoc(), i8, hit),
          byteGEP(rewriter, op.getLoc(), hits, index), 1);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_sample"),
            ValueRange{context, adaptor.getHandle().front(), hits,
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), hitValues.size())})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

template <typename Op, bool IsType>
class CovergroupQueryConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type f64 = rewriter.getF64Type();
    Type i32 = rewriter.getI32Type();
    Value percentage = entryAlloca(rewriter, op.getLoc(), f64, 1, 8);
    Value covered = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    Value total = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value key;
    StringRef symbol;
    SmallVector<Value> arguments{context};
    if constexpr (IsType) {
      auto declaration =
          SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
              op, op.getDeclarationAttr());
      if (!declaration)
        return failure();
      key = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                         declaration.getId());
      symbol = "obelisk_rt_v1_covergroup_type_query";
      arguments.push_back(key);
      Type i64 = rewriter.getI64Type();
      Value bins = entryAlloca(rewriter, op.getLoc(), i64,
                               declaration.getCoverpointBins().size(), 8);
      for (auto [index, count] :
           llvm::enumerate(declaration.getCoverpointBins()))
        LLVM::StoreOp::create(
            rewriter, op.getLoc(),
            llvmConstant(rewriter, op.getLoc(), i64,
                         static_cast<uint64_t>(count)),
            byteGEP(rewriter, op.getLoc(), bins, index * sizeof(uint64_t)), 8);
      arguments.push_back(bins);
      arguments.push_back(llvmConstant(rewriter, op.getLoc(), i64,
                                       declaration.getCoverpointBins().size()));
    } else {
      if (adaptor.getHandle().size() != 1)
        return failure();
      key = adaptor.getHandle().front();
      symbol = "obelisk_rt_v1_covergroup_instance_query";
      arguments.push_back(key);
    }
    arguments.append({percentage, covered, total});
    Value status =
        LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{i32},
                             SymbolRefAttr::get(rewriter.getContext(), symbol),
                             arguments)
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, ValueRange{
                LLVM::LoadOp::create(rewriter, op.getLoc(), f64, percentage, 8),
                LLVM::LoadOp::create(rewriter, op.getLoc(), i32, covered, 4),
                LLVM::LoadOp::create(rewriter, op.getLoc(), i32, total, 4)});
    return success();
  }
};

class ClassCopyConversion final
    : public OpConversionPattern<sim::SimClassCopyOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassCopyOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getSource().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    auto classType = cast<sim::ClassHandleType>(op.getResult().getType());
    Value descriptor = LLVM::AddressOfOp::create(
        rewriter, op.getLoc(), pointer,
        managedClassDescriptorName(classType.getClassName()));
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value source = managedObjectPointer(rewriter, op.getLoc(),
                                        adaptor.getSource().front());
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_object_shallow_copy"),
                       ValueRange{lane, descriptor, source, output})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value object =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), object));
    return success();
  }
};

class ClassIsInstanceConversion final
    : public OpConversionPattern<sim::SimClassIsInstanceOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassIsInstanceOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getObject().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value object = managedObjectPointer(rewriter, op.getLoc(),
                                        adaptor.getObject().front());
    Value descriptor = LLVM::AddressOfOp::create(
        rewriter, op.getLoc(), pointer,
        managedClassDescriptorName(op.getTargetAttr()));
    Value result = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_object_is_instance"),
                       ValueRange{object, descriptor})
                       .getResult();
    rewriter.replaceOp(op, LLVM::TruncOp::create(rewriter, op.getLoc(),
                                                 rewriter.getI1Type(), result));
    return success();
  }
};

class ClassIdConversion final : public OpConversionPattern<sim::SimClassIdOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassIdOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getObject().size() != 1)
      return failure();
    Value object = managedObjectPointer(rewriter, op.getLoc(),
                                        adaptor.getObject().front());
    rewriter.replaceOp(
        op, LLVM::CallOp::create(rewriter, op.getLoc(),
                                 TypeRange{rewriter.getI64Type()},
                                 SymbolRefAttr::get(rewriter.getContext(),
                                                    "obelisk_rt_v1_object_id"),
                                 object)
                .getResults());
    return success();
  }
};

class ClassCastConversion final
    : public OpConversionPattern<sim::SimClassCastOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassCastOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getObject().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    auto targetType = cast<sim::ClassHandleType>(op.getResult().getType());
    Value descriptor = LLVM::AddressOfOp::create(
        rewriter, op.getLoc(), pointer,
        managedClassDescriptorName(targetType.getClassName()));
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_object_cast"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getObject().front()),
                       descriptor, output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value result =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), result));
    return success();
  }
};

class ClassFieldRefConversion final
    : public OpConversionPattern<sim::SimClassFieldRefOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassFieldRefOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getObject().size() != 1)
      return failure();
    auto field = SymbolTable::lookupNearestSymbolFrom<sim::SimClassFieldDeclOp>(
        op, op.getFieldAttr());
    if (!field || !field.getOffset())
      return op.emitError("managed class field has no native offset");
    SmallVector<Value> values{
        adaptor.getObject().front(),
        arith::ConstantOp::create(
            rewriter, op.getLoc(), rewriter.getI64Type(),
            rewriter.getI64IntegerAttr(*field.getOffset()))};
    SmallVector<ValueRange> replacements{ValueRange(values)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class ClassRootBindConversion final
    : public OpConversionPattern<sim::SimClassRootBindOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassRootBindOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if ((adaptor.getObject().size() != 1 && adaptor.getObject().size() != 2) ||
        adaptor.getSlot().size() != 1)
      return failure();
    Value owner = adaptor.getObject().front();
    Type sourceType = op.getObject().getType();
    uint64_t bitOffset = op.getBitOffset();
    if (isa<sim::ArgumentRefType>(sourceType)) {
      owner = arith::TruncIOp::create(rewriter, op.getLoc(),
                                      rewriter.getI64Type(), owner);
    } else if (!isa<sim::ManagedRefType>(sourceType)) {
      auto integer = dyn_cast<IntegerType>(owner.getType());
      if (!integer || integer.getWidth() < 64 ||
          bitOffset > integer.getWidth() - 64)
        return failure();
      if (bitOffset != 0)
        owner = arith::ShRUIOp::create(
            rewriter, op.getLoc(), owner,
            llvmConstant(rewriter, op.getLoc(), integer, bitOffset));
      if (integer.getWidth() != 64)
        owner = arith::TruncIOp::create(rewriter, op.getLoc(),
                                        rewriter.getI64Type(), owner);
    }
    LLVM::StoreOp::create(rewriter, op.getLoc(), owner,
                          adaptor.getSlot().front(), 8);
    rewriter.eraseOp(op);
    return success();
  }
};

Value packArgumentReference(OpBuilder &builder, Location location, Value owner,
                            Value payload, uint32_t managed) {
  Type i192 = builder.getIntegerType(192);
  Value packed = arith::ExtUIOp::create(builder, location, i192, owner);
  Value extendedPayload =
      arith::ExtUIOp::create(builder, location, i192, payload);
  Value payloadShift = llvmConstant(builder, location, i192, 64);
  extendedPayload =
      arith::ShLIOp::create(builder, location, extendedPayload, payloadShift);
  packed = arith::OrIOp::create(builder, location, packed, extendedPayload);
  if (managed) {
    APInt tag(192, managed);
    tag <<= 128;
    Value tagValue = arith::ConstantOp::create(
        builder, location, i192, builder.getIntegerAttr(i192, tag));
    packed = arith::OrIOp::create(builder, location, packed, tagValue);
  }
  return packed;
}

struct UnpackedArgumentReference {
  Value owner;
  Value payload;
  Value managed;
};

UnpackedArgumentReference unpackArgumentReference(OpBuilder &builder,
                                                  Location location,
                                                  Value reference) {
  Type i192 = builder.getIntegerType(192);
  Type i64 = builder.getI64Type();
  Value owner = arith::TruncIOp::create(builder, location, i64, reference);
  Value payloadShift = llvmConstant(builder, location, i192, 64);
  Value shiftedPayload =
      arith::ShRUIOp::create(builder, location, reference, payloadShift);
  Value payload =
      arith::TruncIOp::create(builder, location, i64, shiftedPayload);
  Value tagShift = llvmConstant(builder, location, i192, 128);
  Value shiftedTag =
      arith::ShRUIOp::create(builder, location, reference, tagShift);
  Value managed = arith::TruncIOp::create(builder, location,
                                          builder.getI32Type(), shiftedTag);
  return {owner, payload, managed};
}

class ArgumentRefFromRefConversion final
    : public OpConversionPattern<sim::SimArgumentRefFromRefOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimArgumentRefFromRefOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Value zero = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
    rewriter.replaceOp(op, packArgumentReference(rewriter, op.getLoc(), zero,
                                                 adaptor.getInput().front(),
                                                 false));
    return success();
  }
};

class ArgumentRefFromManagedConversion final
    : public OpConversionPattern<sim::SimArgumentRefFromManagedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimArgumentRefFromManagedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 2)
      return failure();
    rewriter.replaceOp(op, packArgumentReference(rewriter, op.getLoc(),
                                                 adaptor.getInput()[0],
                                                 adaptor.getInput()[1], true));
    return success();
  }
};

class ReferencePathIndexConversion final
    : public OpConversionPattern<sim::SimReferencePathIndexOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimReferencePathIndexOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContainer().size() != 1 || adaptor.getIndex().size() != 1 ||
        adaptor.getOwnerReference().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    UnpackedArgumentReference ownerReference = unpackArgumentReference(
        rewriter, op.getLoc(), adaptor.getOwnerReference().front());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_reference_path_index_create"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getContainer().front()),
                       adaptor.getIndex().front(),
                       managedObjectPointer(rewriter, op.getLoc(),
                                            ownerReference.owner),
                       ownerReference.payload, ownerReference.managed, output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value path =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), path));
    return success();
  }
};

class ReferencePathAssocConversion final
    : public OpConversionPattern<sim::SimReferencePathAssocOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimReferencePathAssocOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2 || adaptor.getOwnerReference().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value key = makeNativeAssocKey(rewriter, op.getLoc(),
                                   op.getArray().getType(), adaptor.getKey());
    UnpackedArgumentReference ownerReference = unpackArgumentReference(
        rewriter, op.getLoc(), adaptor.getOwnerReference().front());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_reference_path_assoc_create"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            ownerReference.owner),
                       ownerReference.payload, ownerReference.managed, output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value path =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), path));
    return success();
  }
};

class ArgumentRefFromPathConversion final
    : public OpConversionPattern<sim::SimArgumentRefFromPathOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimArgumentRefFromPathOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Value zero = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
    rewriter.replaceOp(op, packArgumentReference(rewriter, op.getLoc(),
                                                 adaptor.getInput().front(),
                                                 zero, 2));
    return success();
  }
};

class ArgumentRefLoadConversion final
    : public OpConversionPattern<sim::SimArgumentRefLoadOp> {
public:
  ArgumentRefLoadConversion(const TypeConverter &converter,
                            MLIRContext *context,
                            const llvm::DataLayout &dataLayout,
                            uint64_t stateBitCount)
      : OpConversionPattern(converter, context), dataLayout(dataLayout),
        stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimArgumentRefLoadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 1)
      return failure();
    SmallVector<Type> convertedTypes;
    if (failed(getTypeConverter()->convertType(op.getResult().getType(),
                                               convertedTypes)) ||
        convertedTypes.empty())
      return failure();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(op.getResult().getType(),
                                                 local, llvmContext);
    std::optional<unsigned> bitWidth =
        nativeStateWidth(op.getResult().getType());
    if (failed(storage) || !bitWidth ||
        convertedTypes.size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    UnpackedArgumentReference reference = unpackArgumentReference(
        rewriter, location, adaptor.getReference().front());
    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value valueOut = entryAlloca(rewriter, location, convertedTypes[0], 1,
                                 storage->alignment);
    LLVM::StoreOp::create(
        rewriter, location,
        LLVM::ZeroOp::create(rewriter, location, convertedTypes[0]), valueOut,
        storage->alignment);
    Value unknownOut = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (convertedTypes.size() == 2) {
      unknownOut = entryAlloca(rewriter, location, convertedTypes[1], 1,
                               storage->alignment);
      LLVM::StoreOp::create(
          rewriter, location,
          LLVM::ZeroOp::create(rewriter, location, convertedTypes[1]),
          unknownOut, storage->alignment);
    }
    Value valueState = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                 "__obelisk_state_value");
    Value unknownState = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                   "__obelisk_state_unknown");
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_argument_ref_load"),
            ValueRange{
                context, valueState, unknownState,
                llvmConstant(rewriter, location, i64, stateBitCount),
                managedObjectPointer(rewriter, location, reference.owner),
                reference.payload, reference.managed,
                llvmConstant(rewriter, location, i64, *bitWidth),
                llvmConstant(rewriter, location, i64, storage->size),
                llvmConstant(rewriter, location, i32, storage->fourState),
                llvmConstant(
                    rewriter, location, i32,
                    isa<sim::StringType>(op.getResult().getType())
                        ? OBELISK_RT_ARGUMENT_VALUE_STRING
                    : sim::isManagedHandleType(op.getResult().getType())
                        ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                        : OBELISK_RT_ARGUMENT_VALUE_BITS),
                valueOut, unknownOut})
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    SmallVector<Value> results{LLVM::LoadOp::create(
        rewriter, location, convertedTypes[0], valueOut, storage->alignment)};
    if (convertedTypes.size() == 2)
      results.push_back(LLVM::LoadOp::create(rewriter, location,
                                             convertedTypes[1], unknownOut,
                                             storage->alignment));
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
  uint64_t stateBitCount;
};

class ArgumentRefStoreConversion final
    : public OpConversionPattern<sim::SimArgumentRefStoreOp> {
public:
  ArgumentRefStoreConversion(const TypeConverter &converter,
                             MLIRContext *context,
                             const llvm::DataLayout &dataLayout,
                             uint64_t stateBitCount)
      : OpConversionPattern(converter, context), dataLayout(dataLayout),
        stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimArgumentRefStoreOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 1 || adaptor.getValue().empty())
      return failure();
    Type valueType = op.getValue().getType();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(valueType, local,
                                                 llvmContext);
    std::optional<unsigned> bitWidth = nativeStateWidth(valueType);
    if (failed(storage) || !bitWidth ||
        adaptor.getValue().size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    UnpackedArgumentReference reference = unpackArgumentReference(
        rewriter, location, adaptor.getReference().front());
    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value valueState = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                 "__obelisk_state_value");
    Value unknownState = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                   "__obelisk_state_unknown");
    SmallVector<Value> commonArguments{
        context,
        valueState,
        unknownState,
        llvmConstant(rewriter, location, i64, stateBitCount),
        managedObjectPointer(rewriter, location, reference.owner),
        reference.payload,
        reference.managed,
        llvmConstant(rewriter, location, i64, *bitWidth),
        llvmConstant(rewriter, location, i64, storage->size),
        llvmConstant(rewriter, location, i32, storage->fourState),
        llvmConstant(rewriter, location, i32,
                     isa<sim::StringType>(valueType)
                         ? OBELISK_RT_ARGUMENT_VALUE_STRING
                     : sim::isManagedHandleType(valueType)
                         ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                         : OBELISK_RT_ARGUMENT_VALUE_BITS)};
    Value valueIn =
        entryAlloca(rewriter, location, adaptor.getValue().front().getType(), 1,
                    storage->alignment);
    LLVM::StoreOp::create(rewriter, location, adaptor.getValue().front(),
                          valueIn, storage->alignment);
    Value unknownIn = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknownIn =
          entryAlloca(rewriter, location, adaptor.getValue()[1].getType(), 1,
                      storage->alignment);
      LLVM::StoreOp::create(rewriter, location, adaptor.getValue()[1],
                            unknownIn, storage->alignment);
    }
    SmallVector<Value> storeArguments(commonArguments);
    storeArguments.push_back(valueIn);
    storeArguments.push_back(unknownIn);
    Value status = LLVM::CallOp::create(
                       rewriter, location, TypeRange{i32},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_argument_ref_store"),
                       storeArguments)
                       .getResult();
    reportManagedStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
  uint64_t stateBitCount;
};

class ManagedLoadConversion final
    : public OpConversionPattern<sim::SimManagedLoadOp> {
public:
  ManagedLoadConversion(const TypeConverter &converter, MLIRContext *context,
                        const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimManagedLoadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 2)
      return failure();
    SmallVector<Type> convertedTypes;
    if (failed(getTypeConverter()->convertType(op.getResult().getType(),
                                               convertedTypes)) ||
        convertedTypes.empty())
      return failure();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(op.getResult().getType(),
                                                 local, llvmContext);
    if (failed(storage) ||
        convertedTypes.size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();

    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i64 = rewriter.getI64Type();
    Value object =
        managedObjectPointer(rewriter, op.getLoc(), adaptor.getReference()[0]);
    Value baseOffset = adaptor.getReference()[1];
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    SmallVector<Value> results;
    if (isa<sim::ClassHandleType>(op.getResult().getType())) {
      Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
      LLVM::StoreOp::create(
          rewriter, op.getLoc(),
          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer), output, 8);
      Value status =
          LLVM::CallOp::create(
              rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_object_field_load"),
              ValueRange{object, baseOffset, output})
              .getResult();
      reportManagedStatus(rewriter, op.getLoc(), context, status);
      Value loaded =
          LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
      results.push_back(managedObjectHandle(rewriter, op.getLoc(), loaded));
    } else {
      SmallVector<Value> outputs;
      for (Type type : convertedTypes) {
        Value output =
            entryAlloca(rewriter, op.getLoc(), type, 1, storage->alignment);
        LLVM::StoreOp::create(rewriter, op.getLoc(),
                              LLVM::ZeroOp::create(rewriter, op.getLoc(), type),
                              output, storage->alignment);
        outputs.push_back(output);
      }
      SmallVector<Value> arguments{
          object, baseOffset, outputs.front(),
          llvmConstant(rewriter, op.getLoc(), i64, storage->size)};
      StringRef callee = "obelisk_rt_v1_object_read";
      if (outputs.size() == 2) {
        callee = "obelisk_rt_v1_object_read_planes";
        arguments.insert(arguments.end() - 1, outputs[1]);
      }
      Value status =
          LLVM::CallOp::create(
              rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
              SymbolRefAttr::get(rewriter.getContext(), callee), arguments)
              .getResult();
      reportManagedStatus(rewriter, op.getLoc(), context, status);
      for (auto [type, output] : llvm::zip_equal(convertedTypes, outputs))
        results.push_back(LLVM::LoadOp::create(rewriter, op.getLoc(), type,
                                               output, storage->alignment));
    }
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
};

class ManagedStoreConversion final
    : public OpConversionPattern<sim::SimManagedStoreOp> {
public:
  ManagedStoreConversion(const TypeConverter &converter, MLIRContext *context,
                         const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimManagedStoreOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 2 || adaptor.getValue().empty())
      return failure();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(op.getValue().getType(),
                                                 local, llvmContext);
    if (failed(storage) ||
        adaptor.getValue().size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();
    Type i64 = rewriter.getI64Type();
    Value object =
        managedObjectPointer(rewriter, op.getLoc(), adaptor.getReference()[0]);
    Value baseOffset = adaptor.getReference()[1];
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status;
    if (isa<sim::ClassHandleType>(op.getValue().getType())) {
      status = LLVM::CallOp::create(
                   rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                   SymbolRefAttr::get(rewriter.getContext(),
                                      "obelisk_rt_v1_object_field_store"),
                   ValueRange{object, baseOffset,
                              managedObjectPointer(rewriter, op.getLoc(),
                                                   adaptor.getValue().front())})
                   .getResult();
    } else {
      SmallVector<Value> inputs;
      for (Value value : adaptor.getValue()) {
        Value input = entryAlloca(rewriter, op.getLoc(), value.getType(), 1,
                                  storage->alignment);
        LLVM::StoreOp::create(rewriter, op.getLoc(), value, input,
                              storage->alignment);
        inputs.push_back(input);
      }
      SmallVector<Value> arguments{
          object, baseOffset, inputs.front(),
          llvmConstant(rewriter, op.getLoc(), i64, storage->size)};
      StringRef callee = "obelisk_rt_v1_object_write";
      if (inputs.size() == 2) {
        callee = "obelisk_rt_v1_object_write_planes";
        arguments.insert(arguments.end() - 1, inputs[1]);
      }
      status = LLVM::CallOp::create(
                   rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                   SymbolRefAttr::get(rewriter.getContext(), callee), arguments)
                   .getResult();
    }
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
};

class ManagedNBAConversion final
    : public OpConversionPattern<sim::SimManagedNBAEnqueueOp> {
public:
  ManagedNBAConversion(const TypeConverter &converter, MLIRContext *context,
                       const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimManagedNBAEnqueueOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDestination().size() != 2 || adaptor.getValue().empty())
      return failure();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(op.getValue().getType(),
                                                 local, llvmContext);
    if (failed(storage) ||
        adaptor.getValue().size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i64 = rewriter.getI64Type();
    auto savePlane = [&](Value value, uint32_t alignment) {
      Value address =
          entryAlloca(rewriter, location, value.getType(), 1, alignment);
      LLVM::StoreOp::create(rewriter, location, value, address, alignment);
      return address;
    };

    Value value;
    if (isa<sim::ClassHandleType>(op.getValue().getType()))
      value = savePlane(
          managedObjectPointer(rewriter, location, adaptor.getValue().front()),
          storage->alignment);
    else
      value = savePlane(adaptor.getValue().front(), storage->alignment);
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (storage->fourState)
      unknown = savePlane(adaptor.getValue()[1], storage->alignment);
    Value delay = adaptor.getDelay().empty()
                      ? llvmConstant(rewriter, location, i64, 0)
                      : adaptor.getDelay().front();
    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_managed_nba"),
            ValueRange{context,
                       managedObjectPointer(rewriter, location,
                                            adaptor.getDestination()[0]),
                       adaptor.getDestination()[1], value, unknown,
                       llvmConstant(rewriter, location, i64, storage->size),
                       delay})
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
};

class ReferencePathNBAConversion final
    : public OpConversionPattern<sim::SimReferencePathNBAEnqueueOp> {
public:
  ReferencePathNBAConversion(const TypeConverter &converter,
                             MLIRContext *context,
                             const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimReferencePathNBAEnqueueOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDestination().size() != 1 || adaptor.getValue().empty())
      return failure();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(op.getValue().getType(),
                                                 local, llvmContext);
    if (failed(storage) ||
        adaptor.getValue().size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i64 = rewriter.getI64Type();
    auto savePlane = [&](Value value, uint32_t alignment) {
      Value address =
          entryAlloca(rewriter, location, value.getType(), 1, alignment);
      LLVM::StoreOp::create(rewriter, location, value, address, alignment);
      return address;
    };

    Value value;
    if (isa<sim::ClassHandleType>(op.getValue().getType()))
      value = savePlane(
          managedObjectPointer(rewriter, location, adaptor.getValue().front()),
          storage->alignment);
    else
      value = savePlane(adaptor.getValue().front(), storage->alignment);
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (storage->fourState)
      unknown = savePlane(adaptor.getValue()[1], storage->alignment);
    Value delay = adaptor.getDelay().empty()
                      ? llvmConstant(rewriter, location, i64, 0)
                      : adaptor.getDelay().front();
    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_managed_nba"),
            ValueRange{context,
                       managedObjectPointer(rewriter, location,
                                            adaptor.getDestination().front()),
                       llvmConstant(rewriter, location, i64, UINT64_MAX), value,
                       unknown,
                       llvmConstant(rewriter, location, i64, storage->size),
                       delay})
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
};

template <typename Op>
class ManagedObjectOutputConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange input;
    if constexpr (std::is_same_v<Op, sim::SimWeakCreateOp>)
      input = adaptor.getReferent();
    else
      input = adaptor.getWeak();
    if (input.size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    SmallVector<Value> arguments;
    if constexpr (std::is_same_v<Op, sim::SimWeakCreateOp>) {
      arguments.push_back(lane);
      auto wrapperType = cast<sim::ClassHandleType>(op.getResult().getType());
      arguments.push_back(LLVM::AddressOfOp::create(
          rewriter, op.getLoc(), pointer,
          managedClassDescriptorName(wrapperType.getClassName())));
    }
    arguments.push_back(
        managedObjectPointer(rewriter, op.getLoc(), input.front()));
    arguments.push_back(output);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               std::is_same_v<Op, sim::SimWeakCreateOp>
                                   ? "obelisk_rt_v1_weak_create"
                                   : "obelisk_rt_v1_weak_get"),
            arguments)
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value object =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), object));
    return success();
  }
};

class WeakClearConversion final
    : public OpConversionPattern<sim::SimWeakClearOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimWeakClearOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getWeak().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_weak_clear"),
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getWeak().front()))
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class GCSafepointConversion final
    : public OpConversionPattern<sim::SimGCSafepointOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimGCSafepointOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_gc_safepoint"),
                       lane)
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class ClassVirtualCallConversion final
    : public OpConversionPattern<sim::SimClassVirtualCallOp> {
public:
  ClassVirtualCallConversion(const TypeConverter &converter,
                             MLIRContext *context,
                             const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimClassVirtualCallOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReceiver().size() != 1 ||
        adaptor.getArguments().size() != op.getArguments().size())
      return failure();
    for (auto [argument, converted] :
         llvm::zip_equal(op.getArguments(), adaptor.getArguments()))
      if (isa<sim::RefType>(argument.getType()) && converted.size() == 1)
        emitNativeStateRetain(rewriter, op.getLoc(), converted.front());
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Type argumentType =
        LLVM::LLVMStructType::getLiteral(rewriter.getContext(), {pointer, i64});
    SmallVector<std::pair<Value, uint64_t>> physicalArguments;
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    for (auto [original, converted] :
         llvm::zip_equal(op.getArguments(), adaptor.getArguments())) {
      FailureOr<analysis::SimulationStorageProperties> storage =
          analysis::getSimulationStorageProperties(original.getType(), local,
                                                   llvmContext);
      if (failed(storage) ||
          converted.size() !=
              analysis::getSimulationPhysicalStorageCount(*storage))
        return failure();
      for (Value value : converted)
        physicalArguments.push_back({value, storage->size});
    }

    Value argumentArray = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (!physicalArguments.empty()) {
      argumentArray = entryAlloca(rewriter, op.getLoc(), argumentType,
                                  physicalArguments.size(), 8);
      for (auto [index, argument] : llvm::enumerate(physicalArguments)) {
        Value data =
            entryAlloca(rewriter, op.getLoc(), argument.first.getType(), 1, 8);
        LLVM::StoreOp::create(rewriter, op.getLoc(), argument.first, data, 1);
        Value record =
            LLVM::ZeroOp::create(rewriter, op.getLoc(), argumentType);
        record = insertValue(rewriter, op.getLoc(), record, data, 0);
        record = insertValue(
            rewriter, op.getLoc(), record,
            llvmConstant(rewriter, op.getLoc(), i64, argument.second), 1);
        LLVM::StoreOp::create(
            rewriter, op.getLoc(), record,
            byteGEP(rewriter, op.getLoc(), argumentArray, index * 16), 8);
      }
    }

    SmallVector<Type> convertedResults;
    SmallVector<uint64_t> resultOffsets;
    uint64_t resultSize = 0;
    uint32_t resultAlignment = 1;
    for (Type original : op.getResultTypes()) {
      FailureOr<analysis::SimulationStorageProperties> storage =
          analysis::getSimulationStorageProperties(original, local,
                                                   llvmContext);
      SmallVector<Type> converted;
      if (failed(storage) ||
          failed(getTypeConverter()->convertType(original, converted)) ||
          converted.size() !=
              analysis::getSimulationPhysicalStorageCount(*storage))
        return failure();
      for (Type type : converted) {
        uint64_t offset;
        if (!alignUp(resultSize, storage->alignment, offset) ||
            storage->size > std::numeric_limits<uint64_t>::max() - offset)
          return op.emitError("managed method result layout overflow");
        resultOffsets.push_back(offset);
        resultSize = offset + storage->size;
        resultAlignment =
            std::max<uint32_t>(resultAlignment, storage->alignment);
        convertedResults.push_back(type);
      }
    }
    if (!alignUp(resultSize, resultAlignment, resultSize))
      return op.emitError("managed method result layout overflow");
    Value resultStorage = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (resultSize != 0)
      resultStorage = entryAlloca(rewriter, op.getLoc(), rewriter.getI8Type(),
                                  resultSize, resultAlignment);

    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_method_invoke"),
            ValueRange{
                lane,
                managedObjectPointer(rewriter, op.getLoc(),
                                     adaptor.getReceiver().front()),
                llvmConstant(rewriter, op.getLoc(), i64, op.getSlot()),
                llvmConstant(rewriter, op.getLoc(), i64, op.getSignatureId()),
                argumentArray,
                llvmConstant(rewriter, op.getLoc(), i32,
                             physicalArguments.size()),
                resultStorage,
                llvmConstant(rewriter, op.getLoc(), i64, resultSize)})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    SmallVector<Value> results;
    for (auto [type, offset] : llvm::zip_equal(convertedResults, resultOffsets))
      results.push_back(LLVM::LoadOp::create(
          rewriter, op.getLoc(), type,
          byteGEP(rewriter, op.getLoc(), resultStorage, offset), 1));

    SmallVector<SmallVector<Value>> replacements;
    size_t physical = 0;
    for (Type original : op.getResultTypes()) {
      SmallVector<Type> converted;
      if (failed(getTypeConverter()->convertType(original, converted)))
        return failure();
      replacements.emplace_back(results.begin() + physical,
                                results.begin() + physical + converted.size());
      physical += converted.size();
    }
    rewriter.replaceOpWithMultiple(op, std::move(replacements));
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
};

LogicalResult
materializeManagedMethodThunks(ModuleOp module,
                               const llvm::DataLayout &dataLayout) {
  SmallVector<sim::SimClassMethodDeclOp> methods;
  module.walk([&](sim::SimClassMethodDeclOp method) {
    if (method.getImplementation() && method.getIsVirtual())
      methods.push_back(method);
  });
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  for (sim::SimClassMethodDeclOp method : methods) {
    std::string thunkName = managedMethodThunkName(method);
    if (module.lookupSymbol(thunkName))
      continue;
    OpBuilder builder(context);
    if (method.getIsTask()) {
      builder.setInsertionPointToStart(module.getBody());
      auto thunk = LLVM::LLVMFuncOp::create(
          builder, method.getLoc(), thunkName,
          LLVM::LLVMFunctionType::get(
              i32, {pointer, pointer, pointer, pointer, i32, pointer, i64},
              false),
          LLVM::Linkage::Internal);
      Block *entry = thunk.addEntryBlock(builder);
      builder.setInsertionPointToStart(entry);
      LLVM::ReturnOp::create(builder, method.getLoc(),
                             llvmConstant(builder, method.getLoc(), i32,
                                          OBELISK_RT_TIER_UNAVAILABLE));
      continue;
    }
    auto implementation = SymbolTable::lookupNearestSymbolFrom<func::FuncOp>(
        method, method.getImplementationAttr());
    if (!implementation)
      return method.emitError(
          "managed method implementation was not lowered to func");
    builder.setInsertionPoint(implementation);
    auto thunk = LLVM::LLVMFuncOp::create(
        builder, method.getLoc(), thunkName,
        LLVM::LLVMFunctionType::get(
            i32, {pointer, pointer, pointer, pointer, i32, pointer, i64},
            false),
        LLVM::Linkage::Internal);
    Block *entry = thunk.addEntryBlock(builder);
    builder.setInsertionPointToStart(entry);

    SmallVector<Value> callArguments{
        entry->getArgument(0),
        managedObjectHandle(builder, method.getLoc(), entry->getArgument(2))};
    FunctionType implementationType = implementation.getFunctionType();
    if (implementationType.getNumInputs() < 2)
      return method.emitError("managed method implementation has no receiver");
    unsigned physicalArgumentCount = implementationType.getNumInputs() - 2;
    for (unsigned index = 0; index != physicalArgumentCount; ++index) {
      Type type = implementationType.getInput(index + 2);
      Value record =
          byteGEP(builder, method.getLoc(), entry->getArgument(3), index * 16);
      Value data =
          LLVM::LoadOp::create(builder, method.getLoc(), pointer, record, 8);
      callArguments.push_back(
          LLVM::LoadOp::create(builder, method.getLoc(), type, data, 1));
    }
    auto call = func::CallOp::create(
        builder, method.getLoc(), implementation.getSymName(),
        implementationType.getResults(), callArguments);

    FunctionType semanticType = cast<FunctionType>(method.getFunctionType());
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    uint64_t resultOffset = 0;
    unsigned physicalResult = 0;
    for (Type resultType : semanticType.getResults()) {
      FailureOr<analysis::SimulationStorageProperties> storage =
          analysis::getSimulationStorageProperties(resultType, local,
                                                   llvmContext);
      if (failed(storage))
        return method.emitError("managed method result has no native layout");
      unsigned planes =
          analysis::getSimulationPhysicalStorageCount(*storage);
      for (unsigned plane = 0; plane != planes; ++plane) {
        if (physicalResult >= call.getNumResults())
          return method.emitError(
              "managed method result flattening is inconsistent");
        uint64_t aligned;
        if (!alignUp(resultOffset, storage->alignment, aligned))
          return method.emitError("managed method result layout overflow");
        resultOffset = aligned;
        LLVM::StoreOp::create(builder, method.getLoc(),
                              call.getResult(physicalResult++),
                              byteGEP(builder, method.getLoc(),
                                      entry->getArgument(5), resultOffset),
                              1);
        if (storage->size > std::numeric_limits<uint64_t>::max() - resultOffset)
          return method.emitError("managed method result layout overflow");
        resultOffset += storage->size;
      }
    }
    Value returnedStatus =
        llvmConstant(builder, method.getLoc(), i32, OBELISK_RT_OK);
    if (physicalResult + 1 == call.getNumResults()) {
      Value status = call.getResult(physicalResult);
      if (status.getType() == i32) {
        returnedStatus = status;
        ++physicalResult;
      } else if (isa<runtime::StatusType>(status.getType())) {
        returnedStatus = runtime::RTStatusToBitsOp::create(
            builder, method.getLoc(), i32, status);
        ++physicalResult;
      }
    }
    if (physicalResult != call.getNumResults())
      return method.emitError(
          "managed method result flattening is inconsistent");
    LLVM::ReturnOp::create(builder, method.getLoc(), returnedStatus);
  }
  return success();
}

class EventTriggerConversion final
    : public OpConversionPattern<sim::SimEventTriggerOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimEventTriggerOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getEvent().size() != 1 || adaptor.getDelay().size() > 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i64 = rewriter.getI64Type();
    Value address = LLVM::AddressOfOp::create(rewriter, op.getLoc(), pointer,
                                              "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, address, 8);
    Value delay = adaptor.getDelay().empty()
                      ? llvmConstant(rewriter, op.getLoc(), i64, 0)
                      : adaptor.getDelay().front();
    LLVM::CallOp::create(
        rewriter, op.getLoc(), TypeRange{},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_scheduler_event_after"),
        ValueRange{context, adaptor.getEvent().front(),
                   llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(),
                                op.getNonblocking() ? 1 : 0),
                   delay});
    rewriter.eraseOp(op);
    return success();
  }
};

class EventTriggeredConversion final
    : public OpConversionPattern<sim::SimEventTriggeredOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimEventTriggeredOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getEvent().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value address = LLVM::AddressOfOp::create(rewriter, op.getLoc(), pointer,
                                              "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, address, 8);
    auto call = LLVM::CallOp::create(
        rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_scheduler_event_triggered"),
        ValueRange{context, adaptor.getEvent().front()});
    Value result = LLVM::TruncOp::create(
        rewriter, op.getLoc(), rewriter.getI1Type(), call.getResult());
    rewriter.replaceOp(op, result);
    return success();
  }
};

class EventEqualConversion final
    : public OpConversionPattern<sim::SimEventEqualOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimEventEqualOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getLhs().size() != 1 || adaptor.getRhs().size() != 1)
      return failure();
    Value result = arith::CmpIOp::create(
        rewriter, op.getLoc(), arith::CmpIPredicate::eq,
        adaptor.getLhs().front(), adaptor.getRhs().front());
    rewriter.replaceOp(op, result);
    return success();
  }
};

LogicalResult
makeProcessActivationHelper(ModuleOp module, sim::SimFuncOp function,
                            const SimulationProcessFrameAnalysis &analysis) {
  if (function.getEntryKind() != sim::EntryKind::Task)
    return success();
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = function.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  SmallVector<Type> arguments;
  for (BlockArgument argument : function.getBody().front().getArguments())
    arguments.push_back(convertProcessType(argument.getType(), context));
  std::string helperName =
      (function.getSymName() + ".__obelisk_activate").str();
  if (module.lookupSymbol(helperName))
    return success();
  builder.setInsertionPointAfter(function);
  auto helper = LLVM::LLVMFuncOp::create(
      builder, location, helperName,
      LLVM::LLVMFunctionType::get(i64, arguments, false));
  Block *entry = helper.addEntryBlock(builder);
  Block *created = new Block;
  Block *failed = new Block;
  helper.getBody().push_back(created);
  helper.getBody().push_back(failed);
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
  Value status =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_process_instance_create"),
          ValueRange{descriptor, outInstance})
          .getResult();
  Value succeeded =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq, status,
                            llvmConstant(builder, location, i32, 0));
  LLVM::CondBrOp::create(builder, location, succeeded, created, failed);

  builder.setInsertionPointToStart(failed);
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
      ValueRange{entry->getArgument(0), status});
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
      return helper.emitError(
          "activation capture layout has too few arguments");
    storeAt(builder, location, frame, slot.valueOffset,
            entry->getArgument(physicalArgument++), slot.alignment);
    if (slot.hasSecondaryStorage()) {
      if (physicalArgument >= entry->getNumArguments())
        return helper.emitError(
            "activation capture is missing its secondary value");
      storeAt(builder, location, frame, slot.getSecondaryOffset(),
              entry->getArgument(physicalArgument++), slot.alignment);
    }
  }
  if (physicalArgument != entry->getNumArguments())
    return helper.emitError("activation capture layout has excess arguments");
  Value encoded = LLVM::PtrToIntOp::create(builder, location, i64, instance);
  LLVM::ReturnOp::create(builder, location, encoded);
  return success();
}

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
  std::string bytecodeContinuationName =
      (function.getSymName() + ".__obelisk_bytecode_continuations").str();
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
  if (!schedule.bytecodeContinuations.empty()) {
    auto arrayType =
        LLVM::LLVMArrayType::get(i32, schedule.bytecodeContinuations.size());
    makeConstantGlobal(
        module, location, arrayType, bytecodeContinuationName,
        LLVM::Linkage::Internal, 4, [&](OpBuilder &initializer) {
          Value array = LLVM::ZeroOp::create(initializer, location, arrayType);
          for (auto [index, continuation] :
               llvm::enumerate(schedule.bytecodeContinuations))
            array = LLVM::InsertValueOp::create(
                initializer, location, array,
                llvmConstant(initializer, location, i32, continuation),
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          return array;
        });
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
    if (slot.hasSecondaryStorage()) {
      if (physicalArgument >= entry->getNumArguments())
        return helper.emitError("spawn capture is missing its secondary value");
      storeAt(builder, location, frame, slot.getSecondaryOffset(),
              entry->getArgument(physicalArgument++), slot.alignment);
    }
  }
  if (physicalArgument != entry->getNumArguments())
    return helper.emitError("spawn capture layout has excess arguments");
  uint32_t homeRegion = getRuntimeEventRegion(function.getHomeRegion());
  if (homeRegion == UINT32_MAX)
    return function.emitOpError("has no executable runtime home region");
  uint32_t scheduleFlags = OBELISK_RT_SCHEDULE_HOME(homeRegion) |
                           (function.getEntryKind() == sim::EntryKind::Final
                                ? OBELISK_RT_SCHEDULE_FINAL
                                : 0);
  Value null = LLVM::ZeroOp::create(builder, location, pointer);
  Value continuationAddress = null;
  Value rankAddress = null;
  if (!schedule.continuations.empty()) {
    continuationAddress =
        LLVM::AddressOfOp::create(builder, location, pointer, continuationName);
    rankAddress =
        LLVM::AddressOfOp::create(builder, location, pointer, rankName);
  }
  SmallVector<Value> addArguments{
      entry->getArgument(0), instance,
      llvmConstant(builder, location, i32, scheduleFlags)};
  StringRef addName = "obelisk_rt_v1_scheduler_add_planned";
  if (schedule.actorSlot) {
    addName = "obelisk_rt_v1_scheduler_add_aot";
    addArguments.push_back(
        llvmConstant(builder, location, i32, *schedule.actorSlot));
  }
  llvm::append_range(
      addArguments,
      ValueRange{
          llvmConstant(builder, location, i32, schedule.initialRank),
          continuationAddress, rankAddress,
          llvmConstant(builder, location, i32, schedule.continuations.size())});
  if (schedule.actorSlot) {
    Value bytecodeContinuations = null;
    if (!schedule.bytecodeContinuations.empty())
      bytecodeContinuations = LLVM::AddressOfOp::create(
          builder, location, pointer, bytecodeContinuationName);
    addArguments.push_back(bytecodeContinuations);
    addArguments.push_back(llvmConstant(builder, location, i32,
                                        schedule.bytecodeContinuations.size()));
  }
  auto add =
      LLVM::CallOp::create(builder, location, TypeRange{i32},
                           SymbolRefAttr::get(context, addName), addArguments);
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
  Value token =
      LLVM::CallOp::create(
          builder, location, TypeRange{i64},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_process_token"),
          ValueRange{entry->getArgument(0), instance})
          .getResult();
  LLVM::ReturnOp::create(builder, location, token);

  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_instance_create", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_add_planned", i32,
                           {pointer, pointer, i32, i32, pointer, pointer, i32});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_add_aot", i32,
      {pointer, pointer, i32, i32, i32, pointer, pointer, i32, pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_process_token", i64,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_fail",
                           LLVM::LLVMVoidType::get(context), {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_disable_children",
                           i32, {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_control_enter", i32,
                           {pointer, i64, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_control_leave", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_control_disable", i32,
                           {pointer, i64, i64, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_once", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_deferred_once", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_monitor_register", i32,
                           {pointer, i64, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_monitor_control", i32,
                           {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_monitor_current", i32,
                           {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_instance_destroy",
                           i32, {pointer});
  return success();
}

FailureOr<NativeStaticNBAPlan>
buildNativeStaticNBAPlan(ModuleOp module, const NativeStateLayout &stateLayout,
                         ArrayRef<sim::ComputeNBACommitAttr> orderedCommits,
                         bool enabled) {
  NativeStaticNBAPlan plan;
  if (!enabled)
    return plan;
  for (sim::ComputeNBACommitAttr commit : orderedCommits) {
    sim::ComputeEffectAttr effect = commit.getEffect();
    if (effect.getResource() != sim::ComputeResourceKind::Storage ||
        effect.getTarget() != sim::ComputeTargetKind::Descriptor ||
        effect.getDynamic() || effect.getDeferred())
      return module.emitError(
                 "static NBA commit does not identify one fixed storage root"),
             failure();
    auto handle = stateLayout.storage.find(effect.getDescriptor());
    if (handle == stateLayout.storage.end())
      return module.emitError(
                 "static NBA commit references an unknown storage descriptor"),
             failure();
    obelisk_rt_stable_handle_v1 decoded{};
    if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
        decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset != 0)
      return module.emitError(
                 "static NBA commit has an invalid native state root"),
             failure();
    auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
      return candidate.handleID == decoded.id;
    });
    if (bound == stateLayout.bounds.end())
      return module.emitError(
                 "static NBA commit root is absent from native state layout"),
             failure();
    if (!stateLayout.nbaHandles.contains(decoded.id) ||
        !commit.getFrontierSites().empty())
      continue;
    uint32_t root = static_cast<uint32_t>(plan.roots.size());
    plan.roots.push_back({commit.getId(), decoded.id,
                          static_cast<uint64_t>(bound->width), nullptr});
    plan.generatedAccumulators.emplace_back();
    if (bound->width > OBELISK_RT_SCALAR_NBA_MAX_BITS &&
        bound->width <= OBELISK_RT_GENERATED_NBA_MAX_BITS)
      plan.generatedAccumulators.back() =
          ("__obelisk_aot_nba_accumulator_" + Twine(root)).str();
    auto appendSites = [&](DenseI64ArrayAttr ids, uint32_t storage) {
      for (int64_t id : ids.asArrayRef()) {
        if (id < 0)
          return failure();
        plan.sites.push_back({static_cast<uint64_t>(id), root, storage});
      }
      return success();
    };
    if (failed(
            appendSites(commit.getSlots(), OBELISK_RT_STATIC_NBA_FIXED_SLOT)) ||
        failed(appendSites(commit.getAccumulatorSites(),
                           OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR)))
      return module.emitError("static NBA site identity is negative"),
             failure();
  }
  llvm::sort(plan.sites, [](const auto &left, const auto &right) {
    return left.site < right.site;
  });
  if (std::adjacent_find(plan.sites.begin(), plan.sites.end(),
                         [](const auto &left, const auto &right) {
                           return left.site == right.site;
                         }) != plan.sites.end())
    return module.emitError("static NBA site identity is duplicated"),
           failure();
  for (const obelisk_rt_static_nba_site &site : plan.sites)
    plan.siteRoots.try_emplace(site.site, site.root);
  return plan;
}

FailureOr<NativeStaticFanoutPlan> buildNativeStaticFanoutPlan(
    ModuleOp module, const NativeStateLayout &stateLayout,
    const DenseMap<Operation *, uint32_t> &actorSlots, bool enabled) {
  NativeStaticFanoutPlan plan;
  plan.exact = enabled;
  if (!enabled)
    return plan;
  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  sim::ComputeGraphAttr graph = design ? design.getComputeGraphAttr() : nullptr;
  if (!graph)
    return module.emitError("static fanout plan requires a compute graph"),
           failure();
  auto disableExactFanout = [&] {
    plan.entries.clear();
    plan.exact = false;
  };
  for (Attribute node : graph.getNodes()) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(node);
    if (!fragment)
      continue;
    SmallVector<sim::ComputeEffectAttr> watches;
    for (Attribute effectAttribute : fragment.getEffects()) {
      auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
      if (effect.getEffect() == sim::ComputeEffectKind::Watch)
        watches.push_back(effect);
    }
    if (watches.empty())
      continue;
    sim::SimFuncOp function =
        design.lookupSymbol<sim::SimFuncOp>(fragment.getFunction().getValue());
    Block *block =
        function
            ? analysis::lookupComputeGraphBlock(function, fragment.getBlock())
            : nullptr;
    auto actor =
        function ? actorSlots.find(function.getOperation()) : actorSlots.end();
    if (!function || !block || actor == actorSlots.end())
      return module.emitError(
                 "static fanout references a stale compute fragment"),
             failure();
    Operation *terminator = block->getTerminator();
    sim::ContinuationSiteAttr site;
    if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(terminator))
      site = suspend.getSiteAttr();
    else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(terminator))
      site = suspend.getSiteAttr();
    else {
      disableExactFanout();
      continue;
    }
    if (!site || site.getId() == 0)
      return terminator->emitError(
                 "static fanout suspension has no continuation metadata"),
             failure();
    for (sim::ComputeEffectAttr effect : watches) {
      if (effect.getTarget() != sim::ComputeTargetKind::Descriptor ||
          effect.getDynamic() || effect.getDeferred() ||
          (effect.getResource() != sim::ComputeResourceKind::Storage &&
           effect.getResource() != sim::ComputeResourceKind::Net)) {
        disableExactFanout();
        continue;
      }
      const auto &handles =
          effect.getResource() == sim::ComputeResourceKind::Storage
              ? stateLayout.storage
              : stateLayout.nets;
      auto handle = handles.find(effect.getDescriptor());
      if (handle == handles.end())
        return terminator->emitError(
                   "static fanout references an unknown state descriptor"),
               failure();
      obelisk_rt_stable_handle_v1 decoded{};
      if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
          decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
          decoded.offset != 0)
        return terminator->emitError(
                   "static fanout descriptor has an invalid native root"),
               failure();
      auto bound =
          llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
            return candidate.handleID == decoded.id;
          });
      if (bound == stateLayout.bounds.end() || effect.getWidth() == 0 ||
          effect.getLow() > bound->width ||
          effect.getWidth() > bound->width - effect.getLow())
        return terminator->emitError("static fanout range is out of bounds"),
               failure();
      uint32_t edge;
      switch (effect.getTrigger()) {
      case sim::ComputeTriggerKind::Change:
        edge = OBELISK_RT_WAIT_EDGE_CHANGE;
        break;
      case sim::ComputeTriggerKind::Posedge:
        edge = OBELISK_RT_WAIT_EDGE_POSEDGE;
        break;
      case sim::ComputeTriggerKind::Negedge:
        edge = OBELISK_RT_WAIT_EDGE_NEGEDGE;
        break;
      case sim::ComputeTriggerKind::Both:
        edge = OBELISK_RT_WAIT_EDGE_BOTH;
        break;
      default:
        disableExactFanout();
        continue;
      }
      plan.entries.push_back({decoded.id, actor->second, site.getId(), edge,
                              effect.getLow(), effect.getWidth()});
    }
  }
  llvm::sort(plan.entries, [](const auto &lhs, const auto &rhs) {
    return std::tuple{lhs.static_state, lhs.low_bit, lhs.actor_slot,
                      lhs.continuation} <
           std::tuple{rhs.static_state, rhs.low_bit, rhs.actor_slot,
                      rhs.continuation};
  });
  if (std::adjacent_find(plan.entries.begin(), plan.entries.end(),
                         [](const auto &lhs, const auto &rhs) {
                           return lhs.static_state == rhs.static_state &&
                                  lhs.actor_slot == rhs.actor_slot &&
                                  lhs.continuation == rhs.continuation &&
                                  lhs.edge == rhs.edge &&
                                  lhs.low_bit == rhs.low_bit &&
                                  lhs.bit_width == rhs.bit_width;
                         }) != plan.entries.end())
    return module.emitError("static fanout entry is duplicated"), failure();
  return plan;
}

LogicalResult
makeNativeAOTPlan(ModuleOp module, uint32_t actorCount,
                  ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
                  const NativeStateLayout &stateLayout,
                  const NativeStaticNBAPlan &staticNBAPlan,
                  const NativeStaticFanoutPlan &staticFanoutPlan,
                  ArrayRef<obelisk_rt_static_actor_root> actorRoots,
                  bool enableDirectState, bool enableStaticNBA,
                  bool enableStaticControl, bool enableStaticFanout,
                  bool enableCleanSuperstep, bool fullyStatic,
                  bool rootSlotZero) {
  if (actorCount == 0 || executableNodes.empty())
    return module.emitError("AOT schedule has no executable actor nodes");
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = module.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  ArrayRef<obelisk_rt_static_nba_root> nbaRoots = staticNBAPlan.roots;
  ArrayRef<obelisk_rt_static_nba_site> nbaSites = staticNBAPlan.sites;
  ArrayRef<obelisk_rt_static_fanout_entry> fanoutEntries =
      staticFanoutPlan.exact
          ? ArrayRef<obelisk_rt_static_fanout_entry>(staticFanoutPlan.entries)
          : ArrayRef<obelisk_rt_static_fanout_entry>{};
  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  sim::ComputeGraphAttr graph = design ? design.getComputeGraphAttr() : nullptr;
  // Static time/region control and exact actor fanout are independent of VPI
  // reads. Writable VPI hands dirty roots and the affected event slot to the
  // existing guarded state/control paths; the exact dependency table remains
  // valid and can continue to wake native actors without subscriptions.
  bool staticControlEnabled = enableStaticControl && fullyStatic && graph;
  bool staticFanoutEnabled =
      enableStaticFanout && staticFanoutPlan.exact && fullyStatic && graph;
  bool guardedFanoutEnabled =
      !staticFanoutEnabled && staticFanoutPlan.exact && fullyStatic && graph;
  bool guardedSpecializationEnabled =
      graph && graph.getVpi() == sim::ComputeVPIMode::Full &&
      (enableDirectState || enableStaticNBA);
  bool cleanSuperstepEnabled =
      enableCleanSuperstep && staticControlEnabled &&
      staticFanoutPlan.exact && fullyStatic;
  uint64_t graphLayoutChecksum = 0;
  if (auto image =
          module->getAttrOfType<DenseI8ArrayAttr>("obelisk.bytecode.image")) {
    ArrayRef<int8_t> bytes = image.asArrayRef();
    if (bytes.size() < 40)
      return module.emitError("embedded bytecode checksum is truncated");
    for (unsigned byte = 0; byte != 8; ++byte)
      graphLayoutChecksum |= uint64_t{static_cast<uint8_t>(bytes[32 + byte])}
                             << (byte * 8);
  }
  Type stateType = LLVM::LLVMArrayType::get(pointer, actorCount);
  constexpr StringLiteral stateName = "__obelisk_aot_schedule_state_v1";
  constexpr StringLiteral nodesName = "__obelisk_aot_schedule_nodes_v1";
  constexpr StringLiteral nbaRootsName = "__obelisk_aot_nba_roots_v1";
  constexpr StringLiteral nbaSitesName = "__obelisk_aot_nba_sites_v1";
  constexpr StringLiteral fanoutName = "__obelisk_aot_static_fanout_v1";
  constexpr StringLiteral actorRootsName =
      "__obelisk_aot_static_actor_roots_v1";
  constexpr StringLiteral bindName = "__obelisk_aot_schedule_bind_v1";
  constexpr StringLiteral runName = "__obelisk_aot_schedule_run_v1";
  constexpr StringLiteral snapshotName = "__obelisk_aot_schedule_snapshot_v1";
  constexpr StringLiteral nbaCommitName = "__obelisk_aot_static_nba_commit_v1";
  constexpr StringLiteral planName = "__obelisk_aot_schedule_plan_v1";

  builder.setInsertionPointToStart(module.getBody());
  auto state = LLVM::GlobalOp::create(builder, location, stateType, false,
                                      LLVM::Linkage::Internal, stateName,
                                      Attribute{}, 8);
  Block *initializer = new Block;
  state.getInitializerRegion().push_back(initializer);
  builder.setInsertionPointToStart(initializer);
  LLVM::ReturnOp::create(builder, location,
                         LLVM::ZeroOp::create(builder, location, stateType));

  Type nodeType = LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32});
  Type nodesType = LLVM::LLVMArrayType::get(nodeType, executableNodes.size());
  makeConstantGlobal(
      module, location, nodesType, nodesName, LLVM::Linkage::Internal, 4,
      [&](OpBuilder &initializerBuilder) {
        Value nodes =
            LLVM::ZeroOp::create(initializerBuilder, location, nodesType);
        for (auto [index, node] : llvm::enumerate(executableNodes)) {
          Value value =
              LLVM::ZeroOp::create(initializerBuilder, location, nodeType);
          value = insertValue(
              initializerBuilder, location, value,
              llvmConstant(initializerBuilder, location, i32, node.actor_slot),
              0);
          value = insertValue(initializerBuilder, location, value,
                              llvmConstant(initializerBuilder, location, i32,
                                           node.continuation),
                              1);
          value = insertValue(initializerBuilder, location, value,
                              llvmConstant(initializerBuilder, location, i32,
                                           node.fusion_group),
                              2);
          nodes = LLVM::InsertValueOp::create(
              initializerBuilder, location, nodes, value,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return nodes;
      });

  Type nbaRootType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64, pointer});
  if (!nbaRoots.empty()) {
    Type rootsType = LLVM::LLVMArrayType::get(nbaRootType, nbaRoots.size());
    makeConstantGlobal(
        module, location, rootsType, nbaRootsName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value roots =
              LLVM::ZeroOp::create(initializerBuilder, location, rootsType);
          for (auto [index, root] : llvm::enumerate(nbaRoots)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, nbaRootType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             root.commit_node),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             root.static_state),
                                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, root.bit_width),
                2);
            Value accumulator =
                index < staticNBAPlan.generatedAccumulators.size() &&
                        !staticNBAPlan.generatedAccumulators[index].empty()
                    ? LLVM::AddressOfOp::create(
                          initializerBuilder, location, pointer,
                          staticNBAPlan.generatedAccumulators[index])
                          .getResult()
                    : LLVM::ZeroOp::create(initializerBuilder, location,
                                           pointer)
                          .getResult();
            value = insertValue(initializerBuilder, location, value,
                                accumulator, 3);
            roots = LLVM::InsertValueOp::create(
                initializerBuilder, location, roots, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return roots;
        });
  }
  Type nbaSiteType = LLVM::LLVMStructType::getLiteral(context, {i64, i32, i32});
  if (!nbaSites.empty()) {
    Type sitesType = LLVM::LLVMArrayType::get(nbaSiteType, nbaSites.size());
    makeConstantGlobal(
        module, location, sitesType, nbaSitesName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value sites =
              LLVM::ZeroOp::create(initializerBuilder, location, sitesType);
          for (auto [index, site] : llvm::enumerate(nbaSites)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, nbaSiteType);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, site.site), 0);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, site.root), 1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, site.storage),
                2);
            sites = LLVM::InsertValueOp::create(
                initializerBuilder, location, sites, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return sites;
        });
  }
  Type fanoutType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32, i32, i64, i64});
  if (!fanoutEntries.empty()) {
    Type entriesType =
        LLVM::LLVMArrayType::get(fanoutType, fanoutEntries.size());
    makeConstantGlobal(
        module, location, entriesType, fanoutName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value entries =
              LLVM::ZeroOp::create(initializerBuilder, location, entriesType);
          for (auto [index, entry] : llvm::enumerate(fanoutEntries)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, fanoutType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.static_state),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.actor_slot),
                                1);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.continuation),
                                2);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.edge), 3);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, entry.low_bit),
                4);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             entry.bit_width),
                                5);
            entries = LLVM::InsertValueOp::create(
                initializerBuilder, location, entries, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return entries;
        });
  }
  Type actorRootType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32, i32});
  if (!actorRoots.empty()) {
    Type entriesType =
        LLVM::LLVMArrayType::get(actorRootType, actorRoots.size());
    makeConstantGlobal(
        module, location, entriesType, actorRootsName, LLVM::Linkage::Internal,
        4, [&](OpBuilder &initializerBuilder) {
          Value entries =
              LLVM::ZeroOp::create(initializerBuilder, location, entriesType);
          for (auto [index, entry] : llvm::enumerate(actorRoots)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               actorRootType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.actor_slot),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.static_state),
                                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.flags),
                2);
            entries = LLVM::InsertValueOp::create(
                initializerBuilder, location, entries, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return entries;
        });
  }

  builder.setInsertionPointToEnd(module.getBody());
  auto bind = LLVM::LLVMFuncOp::create(
      builder, location, bindName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  Block *bindEntry = bind.addEntryBlock(builder);
  builder.setInsertionPointToStart(bindEntry);
  Value slot =
      LLVM::ZExtOp::create(builder, location, i64, bindEntry->getArgument(2));
  Value actorAddress =
      LLVM::GEPOp::create(builder, location, pointer, pointer,
                          bindEntry->getArgument(0), ValueRange{slot});
  LLVM::StoreOp::create(builder, location, bindEntry->getArgument(3),
                        actorAddress, 8);
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, OBELISK_RT_OK));

  builder.setInsertionPointToEnd(module.getBody());
  auto run = LLVM::LLVMFuncOp::create(
      builder, location, runName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
  Block *runEntry = run.addEntryBlock(builder);
  builder.setInsertionPointToStart(runEntry);
  Value nodes =
      LLVM::AddressOfOp::create(builder, location, pointer, nodesName);
  Value runStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(
              context, fullyStatic ? "obelisk_rt_v1_scheduler_run_aot_nodes"
                                   : "obelisk_rt_v1_scheduler_run"),
          fullyStatic ? ValueRange{runEntry->getArgument(1), nodes,
                                   llvmConstant(builder, location, i32,
                                                executableNodes.size())}
                      : ValueRange{runEntry->getArgument(1)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, runStatus);

  builder.setInsertionPointToEnd(module.getBody());
  auto snapshot = LLVM::LLVMFuncOp::create(
      builder, location, snapshotName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, pointer}, false));
  Block *snapshotEntry = snapshot.addEntryBlock(builder);
  builder.setInsertionPointToStart(snapshotEntry);
  Value snapshotStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_snapshot_aot"),
          ValueRange{snapshotEntry->getArgument(1),
                     snapshotEntry->getArgument(2)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, snapshotStatus);

  builder.setInsertionPointToEnd(module.getBody());
  auto nbaCommit = LLVM::LLVMFuncOp::create(
      builder, location, nbaCommitName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  Block *nbaCommitEntry = nbaCommit.addEntryBlock(builder);
  builder.setInsertionPointToStart(nbaCommitEntry);
  Value nbaCommitStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_static_nba_commit_roots"),
          ValueRange{nbaCommitEntry->getArgument(1),
                     llvmConstant(builder, location, i32, nbaRoots.size()),
                     nbaCommitEntry->getArgument(2),
                     nbaCommitEntry->getArgument(3)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, nbaCommitStatus);

  auto planType = LLVM::LLVMStructType::getLiteral(
      context,
      {i32, i64,     pointer, i64,     i32,     i32,     pointer, pointer,
       i64, pointer, pointer, pointer, pointer, i32,     i32,     pointer,
       i64, pointer, i64,     pointer, i64,     pointer, pointer});
  makeConstantGlobal(
      module, location, planType, planName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &initializerBuilder) {
        Value value =
            LLVM::ZeroOp::create(initializerBuilder, location, planType);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32,
                                     sizeof(obelisk_rt_native_schedule_plan)),
                        0);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         graphLayoutChecksum),
                            1);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, stateName),
                        2);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         uint64_t{actorCount} * sizeof(void *)),
                            3);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, actorCount), 4);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(
                initializerBuilder, location, i32,
                (fullyStatic ? OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC : 0) |
                    (rootSlotZero ? OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO
                                  : 0) |
                    (staticControlEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL
                         : 0) |
                    (staticFanoutEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT
                         : 0) |
                    (enableDirectState ? OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE
                                       : 0) |
                    (enableStaticNBA ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA
                                     : 0) |
                    (fullyStatic ? OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS
                                 : 0) |
                    (guardedFanoutEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT
                         : 0) |
                    (guardedSpecializationEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION
                         : 0) |
                    (cleanSuperstepEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP
                         : 0)),
            5);
        value = insertValue(initializerBuilder, location, value,
                            LLVM::AddressOfOp::create(initializerBuilder,
                                                      location, pointer,
                                                      "__obelisk_state_value"),
                            6);
        value = insertValue(
            initializerBuilder, location, value,
            LLVM::AddressOfOp::create(initializerBuilder, location, pointer,
                                      "__obelisk_state_unknown"),
            7);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         stateLayout.bitCount),
                            8);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, bindName),
                        9);
        value = insertValue(initializerBuilder, location, value,
                            LLVM::AddressOfOp::create(
                                initializerBuilder, location, pointer, runName),
                            10);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, snapshotName),
                        11);
        Value rootsAddress =
            nbaRoots.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaRootsName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, rootsAddress, 12);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, nbaRoots.size()),
            13);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32, 0), 14);
        Value sitesAddress =
            nbaSites.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaSitesName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, sitesAddress, 15);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i64, nbaSites.size()),
            16);
        Value fanoutAddress =
            fanoutEntries.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, fanoutName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, fanoutAddress, 17);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         fanoutEntries.size()),
                            18);
        Value actorRootsAddress =
            actorRoots.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, actorRootsName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value,
                            actorRootsAddress, 19);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i64, actorRoots.size()),
            20);
        Value commitAddress =
            enableStaticNBA
                ? LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaCommitName)
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, commitAddress, 21);
        Value specializationFast =
            guardedSpecializationEnabled
                ? LLVM::AddressOfOp::create(
                      initializerBuilder, location, pointer,
                      "__obelisk_static_specialization_fast_v1")
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        return insertValue(initializerBuilder, location, value,
                           specializationFast, 22);
      });
  if (fullyStatic)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run_aot_nodes",
                             i32, {pointer, pointer, i32});
  else
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run", i32,
                             {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_snapshot_aot", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_root", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_roots", i32,
                           {pointer, i32, i32, pointer});
  return success();
}

LogicalResult makeSchedulerMain(ModuleOp module,
                                const NativeStateLayout &stateLayout,
                                bool useAOT) {
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
      builder, location, "main",
      LLVM::LLVMFunctionType::get(i32, {i32, pointer}, false));
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
  bool hasDesignBytecode = false;
  if (auto flags =
          module->getAttrOfType<IntegerAttr>("obelisk.execution.flags"))
    hasDesignBytecode = (flags.getValue().getZExtValue() &
                         OBELISK_RT_EXECUTION_HAS_BYTECODE) != 0;
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
  Value configureStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_context_configure_argv"),
          ValueRange{runtimeContext, entry->getArgument(0),
                     entry->getArgument(1)})
          .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
      ValueRange{runtimeContext, configureStatus});
  Value currentAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  LLVM::StoreOp::create(builder, location, runtimeContext, currentAddress, 8);
  SmallVector<sim::SimClassDeclOp> managedClasses;
  module.walk([&](sim::SimClassDeclOp declaration) {
    managedClasses.push_back(declaration);
  });
  llvm::sort(managedClasses,
             [](auto lhs, auto rhs) { return lhs.getId() < rhs.getId(); });
  for (sim::SimClassDeclOp declaration : managedClasses) {
    Value descriptor = LLVM::AddressOfOp::create(
        builder, location, pointer,
        managedClassDescriptorName(
            FlatSymbolRefAttr::get(context, declaration.getSymName())));
    Value status =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_class_register"),
            ValueRange{runtimeContext, descriptor})
            .getResult();
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, status});
  }
  SmallVector<LLVM::LLVMFuncOp> dpiThunks;
  module.walk([&](LLVM::LLVMFuncOp function) {
    if (function->hasAttr("obelisk.dpi.import_id"))
      dpiThunks.push_back(function);
  });
  llvm::sort(dpiThunks, [](LLVM::LLVMFuncOp lhs, LLVM::LLVMFuncOp rhs) {
    return lhs->getAttrOfType<IntegerAttr>("obelisk.dpi.import_id").getInt() <
           rhs->getAttrOfType<IntegerAttr>("obelisk.dpi.import_id").getInt();
  });
  for (LLVM::LLVMFuncOp thunk : dpiThunks) {
    auto importID = thunk->getAttrOfType<IntegerAttr>("obelisk.dpi.import_id");
    auto abiHash = thunk->getAttrOfType<IntegerAttr>("obelisk.dpi.abi_hash");
    if (!abiHash)
      return thunk.emitError("DPI thunk is missing its ABI signature hash");
    Value callback = LLVM::AddressOfOp::create(builder, location, pointer,
                                               thunk.getSymName());
    Value userData = LLVM::ZeroOp::create(builder, location, pointer);
    Value status =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(
                context, "obelisk_rt_v1_context_register_import_signature"),
            ValueRange{runtimeContext,
                       llvmConstant(builder, location, i32,
                                    importID.getValue().getZExtValue()),
                       llvmConstant(builder, location, i64,
                                    abiHash.getValue().getZExtValue()),
                       callback, userData})
            .getResult();
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, status});
  }
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
    for (uint64_t rootOffset : bound.managedRootOffsets) {
      if ((bound.offset + rootOffset) & 7)
        return module.emitError("managed static root is not byte aligned");
      Value state = LLVM::AddressOfOp::create(builder, location, pointer,
                                              "__obelisk_state_value");
      Value slot =
          byteGEP(builder, location, state, (bound.offset + rootOffset) / 8);
      Value rootStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context,
                                 "obelisk_rt_v1_gc_static_root_register"),
              ValueRange{runtimeContext, slot})
              .getResult();
      LLVM::CallOp::create(
          builder, location, TypeRange{},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
          ValueRange{runtimeContext, rootStatus});
      if (hasDesignBytecode) {
        Value designRootStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(context,
                                   "obelisk_rt_v1_gc_design_root_register"),
                ValueRange{runtimeContext,
                           llvmConstant(builder, location, i64,
                                        bound.offset + rootOffset)})
                .getResult();
        LLVM::CallOp::create(
            builder, location, TypeRange{},
            SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
            ValueRange{runtimeContext, designRootStatus});
      }
    }
  }
  if (useAOT) {
    Value plan = LLVM::AddressOfOp::create(builder, location, pointer,
                                           "__obelisk_aot_schedule_plan_v1");
    Value installStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_install_aot"),
            ValueRange{runtimeContext, plan})
            .getResult();
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, installStatus});
  }
  LLVM::CallOp::create(
      builder, location, TypeRange{i64},
      SymbolRefAttr::get(context, "__obelisk_root.__obelisk_spawn"),
      runtimeContext);
  auto run = LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, useAOT ? "obelisk_rt_v1_scheduler_run_aot"
                                         : "obelisk_rt_v1_scheduler_run"),
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
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_context_configure_argv", i32,
                           {pointer, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_native_state_register_static",
                           i32, {pointer, i32, i64, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_static_root_register", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_design_root_register", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_class_register", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_context_register_import_signature",
                           i32, {pointer, i32, i64, pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_fail", voidType,
                           {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run", i32,
                           {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_install_aot", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run_aot", i32,
                           {pointer});
  return success();
}

LogicalResult prepareSimulationProcessesForLLVMCoroutinesImpl(
    ModuleOp module, const llvm::DataLayout &dataLayout) {
  MLIRContext *context = module.getContext();
  llvm::StringMap<ManagedClassLayout> managedClassLayouts;
  if (failed(prepareManagedClassInventory(module, dataLayout,
                                          managedClassLayouts)) ||
      failed(normalizeManagedDirectCalls(module)))
    return failure();
  expandManagedSelectsToCFG(module);
  FailureOr<NativeStateLayout> stateLayout = buildNativeStateLayout(module);
  if (failed(stateLayout))
    return failure();
  sim::StaticSpecializationAttr staticSpecialization;
  sim::StaticSuperstepAttr staticSuperstep;
  SmallVector<sim::ComputeNBACommitAttr> staticNBACommits;
  sim::SimDesignOp metadataDesign;
  module.walk([&](sim::SimDesignOp design) {
    metadataDesign = design;
    staticSuperstep = design->getAttrOfType<sim::StaticSuperstepAttr>(
        sim::metadata::staticSuperstep);
  });
  if (staticSuperstep &&
      (!metadataDesign ||
       staticSuperstep.getSourceGraph() !=
           metadataDesign.getComputeGraphAttr()))
    return module.emitError(
        "native lowering rejected stale static-superstep metadata");
  if (metadataDesign) {
    FailureOr<analysis::StaticSpecializationAnalysis> analyzed =
        analysis::StaticSpecializationAnalysis::compute(metadataDesign);
    if (failed(analyzed))
      return failure();
    staticSpecialization = analyzed->getPlan();
    llvm::append_range(staticNBACommits, analyzed->getOrderedNBACommits());
    DenseSet<uint64_t> plannedNBARoots;
    for (const auto &[descriptor, root] : analyzed->getRoots()) {
      if (!root.getDirect() && !root.getGuarded() && !root.getNba())
        continue;
      if (root.getWidth() == 0 ||
          ((root.getDirect() || root.getGuarded()) &&
           root.getWidth() > staticSpecialization.getMaxPackedWidth()))
        return module.emitError(
            "native lowering rejected invalid static-specialization root");
      auto handle = stateLayout->storage.find(descriptor);
      if (handle == stateLayout->storage.end())
        return module.emitError(
            "static-specialization root references unknown storage");
      obelisk_rt_stable_handle_v1 decoded{};
      if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
          decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
          decoded.offset != 0)
        return module.emitError(
            "static-specialization root has an invalid native handle");
      auto bound = llvm::find_if(stateLayout->bounds, [&](const auto &entry) {
        return entry.handleID == decoded.id;
      });
      if (bound == stateLayout->bounds.end() || bound->width != root.getWidth())
        return module.emitError(
            "static-specialization root disagrees with native state layout");
      if (root.getDirect())
        stateLayout->directHandles.insert(decoded.id);
      if (root.getGuarded())
        stateLayout->guardedHandles.insert(decoded.id);
      if (root.getNba()) {
        plannedNBARoots.insert(descriptor);
        stateLayout->nbaHandles.insert(decoded.id);
      }
    }
    if (analyzed->getNBARoots().size() != plannedNBARoots.size())
      return module.emitError(
          "static-specialization NBA root policies disagree with the "
          "ordered inventory");
  }
  sim::NativeSchedulerMode nativeScheduler = sim::NativeSchedulerMode::Auto;
  if (auto mode = module->getAttrOfType<sim::NativeSchedulerModeAttr>(
          "obelisk.native_scheduler"))
    nativeScheduler = mode.getValue();
  analysis::NativeAOTAnalysis aotEligibility;
  bool useAOT = false;
  DenseMap<Operation *, SmallVector<uint32_t>> aotBytecodeContinuations;
  uint64_t stateBytes = (stateLayout->bitCount + 7) / 8;
  makeStatePlane(module, "__obelisk_state_value", stateBytes, false,
                 stateLayout->driverLayouts, stateLayout->netLayouts);
  makeStatePlane(module, "__obelisk_state_unknown", stateBytes, true);
  makeCurrentContextGlobal(module);
  makeStaticSpecializationFastGlobal(module);
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
      module, "obelisk_rt_v1_scheduler_static_transition",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_real_transition",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_specialization_guard",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       IntegerType::get(context, 32), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_specialization_guard",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_event", LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_event_after",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_event_triggered",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_handle_offset",
      IntegerType::get(context, 64),
      {IntegerType::get(context, 64), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_nba", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_static_nba",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_claim", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_static_nba_packed",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_stage_wide",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_string_nba",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_managed_nba",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_alloc", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_alloc_with_roots",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_retain",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_native_state_release",
                           IntegerType::get(context, 32),
                           {LLVM::LLVMPointerType::get(context),
                            IntegerType::get(context, 64),
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
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_override", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_release_override",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_argument_ref_load", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_argument_ref_store", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  Type managedPointer = LLVM::LLVMPointerType::get(context);
  Type managedI32 = IntegerType::get(context, 32);
  Type managedI64 = IntegerType::get(context, 64);
  Type managedF64 = Float64Type::get(context);
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_current_lane",
                           managedPointer, {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_root_push", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_root_pop", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_gc_root_range_push", managedI32,
      {managedPointer, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_root_range_pop",
                           managedI32, {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_gc_managed_root_range_push", managedI32,
      {managedPointer, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_managed_root_range_pop",
                           managedI32, {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_safepoint", managedI32,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_allocate", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_reference_path_index_create", managedI32,
      {managedPointer, managedPointer, managedI64, managedPointer, managedI64,
       managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_size", managedI64,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_create_like",
                           managedI32,
                           {managedPointer, managedPointer, managedPointer,
                            managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_container_create_typed", managedI32,
      {managedPointer, managedI32, managedI64, managedI32, managedI32,
       managedI64, managedI64, managedI64, managedPointer, managedI64,
       managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_clone", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_delete", managedI32,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_queue_delete_index",
                           managedI32, {managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_queue_insert", managedI32,
                           {managedPointer, managedPointer, managedI64,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_random_bounded", managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_random_next", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_random_seed", managedI32,
                           {managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_create", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_covergroup_set_enabled",
                           managedI32,
                           {managedPointer, managedI64, managedI32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_covergroup_sample_enabled",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_bin_hit", managedI32,
      {managedPointer, managedI64, managedI32, managedI32});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_sample", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_covergroup_instance_query",
                           managedI32,
                           {managedPointer, managedI64, managedPointer,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_type_query", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64, managedPointer,
       managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_container_read", managedI32,
      {managedPointer, managedI64, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_write", managedI32,
                           {managedPointer, managedPointer, managedI64,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_assoc_create_typed", managedI32,
      {managedPointer, managedI64, managedI32, managedI32, managedI64,
       managedI64, managedI64, managedPointer, managedI64, managedI32,
       managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_assoc_read_checked", managedI32,
      {managedPointer, managedPointer, managedPointer, managedI64,
       managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_assoc_write_checked", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer,
       managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assoc_exists", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assoc_delete", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assoc_set_default_checked",
                           managedI32,
                           {managedPointer, managedPointer, managedPointer,
                            managedI64, managedPointer, managedI64});
  for (StringRef name :
       {"obelisk_rt_v1_assoc_first", "obelisk_rt_v1_assoc_last",
        "obelisk_rt_v1_assoc_next", "obelisk_rt_v1_assoc_prev"})
    getOrDeclareLLVMFunction(
        module, name, managedI32,
        {managedPointer, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_reference_path_assoc_create", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer,
       managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_shallow_copy", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_read", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_write", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_read_planes", managedI32,
      {managedPointer, managedI64, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_write_planes", managedI32,
      {managedPointer, managedI64, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_field_load",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_field_store",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_is_instance",
                           managedI32, {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_id", managedI64,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_cast", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_method_invoke", managedI32,
                           {managedPointer, managedPointer, managedI64,
                            managedI64, managedPointer, managedI32,
                            managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_weak_create", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_weak_get", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_weak_clear", managedI32,
                           {managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_create", managedI32,
      {managedPointer, managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_from_packed",
                           managedI32,
                           {managedPointer, managedPointer, managedPointer,
                            managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_to_packed", managedI32,
      {managedI64, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_concat_many", managedI32,
      {managedPointer, managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_repeat", managedI32,
      {managedPointer, managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_length", managedI64,
                           {managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_getc", managedI32,
                           {managedI64, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_putc", managedI32,
      {managedPointer, managedI64, managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_substr", managedI32,
      {managedPointer, managedI64, managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_compare", managedI32,
                           {managedI64, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_compare_insensitive",
                           managedI32, {managedI64, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_case_convert", managedI32,
      {managedPointer, managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_parse_integer",
                           managedI32,
                           {managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_parse_real",
                           managedI32, {managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_format_integer", managedI32,
      {managedPointer, managedI64, managedI32, managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_format_real",
                           managedI32,
                           {managedPointer, managedF64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_file_open_string_mcd",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_file_open_string", managedI32,
      {managedPointer, managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_file_getline_string",
                           managedI32,
                           {managedPointer, managedPointer, managedI32,
                            managedPointer, managedPointer});
  llvm::MapVector<Operation *, std::unique_ptr<SimulationProcessFrameAnalysis>>
      analyses;
  WalkResult analyzed = module.walk([&](sim::SimFuncOp function) {
    bool suspendable = false;
    function.walk(
        [&](Operation *operation) {
          suspendable |= sim::isSuspensionOp(operation);
        });
    bool process = function.getEntryKind() != sim::EntryKind::Function &&
                   function.getEntryKind() != sim::EntryKind::Observer;
    if (failed(insertAutomaticOwnerReleases(function)))
      return WalkResult::interrupt();
    if (suspendable && failed(threadProcessStateThroughCFG(function)))
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
  if (nativeScheduler != sim::NativeSchedulerMode::Generic) {
    aotEligibility = analysis::NativeAOTAnalysis::compute(module);
    useAOT = aotEligibility.isEligible();
    if (nativeScheduler == sim::NativeSchedulerMode::AOT &&
        !aotEligibility.isFullyEligible()) {
      InFlightDiagnostic diagnostic =
          module.emitError("design is ineligible for native AOT scheduling: ");
      if (aotEligibility.getReasons().empty())
        diagnostic << "no statically schedulable process actors";
      else
        llvm::interleaveComma(aotEligibility.getReasons(), diagnostic);
      return failure();
    }
  }
  bool cleanSuperstep = false;
  if (staticSuperstep && useAOT && aotEligibility.isFullyEligible()) {
    ArrayAttr actors = staticSuperstep.getActors();
    if (actors.size() != aotEligibility.getActorSlots().size())
      return module.emitError(
          "native lowering rejected stale static-superstep actor inventory");
    for (auto [slot, attribute] : llvm::enumerate(actors)) {
      auto actor = dyn_cast<FlatSymbolRefAttr>(attribute);
      sim::SimFuncOp function =
          actor ? metadataDesign.lookupSymbol<sim::SimFuncOp>(actor.getValue())
                : nullptr;
      auto planned =
          function
              ? aotEligibility.getActorSlots().find(function.getOperation())
              : aotEligibility.getActorSlots().end();
      if (!function || planned == aotEligibility.getActorSlots().end() ||
          planned->second != slot)
        return module.emitError(
            "native lowering rejected stale static-superstep actor order");
    }
    cleanSuperstep = true;
  }
  if (useAOT && failed(specializeNativeAOTCaptures(module, aotEligibility)))
    return failure();
  bool staticControl = false;
  bool staticFanout = false;
  bool staticFanoutMetadata = false;
  bool vpiOff = false;
  bool vpiRead = false;
  bool vpiFull = false;
  bool directStaticState = false;
  bool staticNBA = false;
  NativeStaticNBAPlan staticNBAPlan;
  NativeStaticFanoutPlan staticFanoutPlan;
  SmallVector<obelisk_rt_static_actor_root> staticActorRoots;
  if (useAOT && aotEligibility.isFullyEligible())
    module.walk([&](sim::SimDesignOp design) {
      sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
      staticControl = graph != nullptr;
      staticFanoutMetadata = graph != nullptr;
      vpiOff = graph && graph.getVpi() == sim::ComputeVPIMode::Off;
      vpiRead = graph && graph.getVpi() == sim::ComputeVPIMode::Read;
      vpiFull = graph && graph.getVpi() == sim::ComputeVPIMode::Full;
      // Read-only VPI observes the same canonical planes but cannot mutate
      // roots or invalidate the closed-world waiter inventory. It therefore
      // uses the fully static fanout schedule just like VPI-off.
      staticFanout = staticFanoutMetadata && (vpiOff || vpiRead);
      directStaticState = staticSpecialization && graph &&
                          (!stateLayout->directHandles.empty() ||
                           !stateLayout->guardedHandles.empty());
      staticNBA = staticSpecialization && !stateLayout->nbaHandles.empty();
    });
  if (staticControl) {
    module.walk([&](Operation *operation) {
      if (llvm::any_of(operation->getOperandTypes(),
                       [](Type type) { return isa<FloatType>(type); }) ||
          llvm::any_of(operation->getResultTypes(),
                       [](Type type) { return isa<FloatType>(type); })) {
        staticControl = false;
        staticFanout = false;
        staticFanoutMetadata = false;
      }
    });
  }
  // State, NBA, and fanout are independent capabilities. Direct access is
  // selected per operation by resolveDirectStaticStateRange; a wide or
  // otherwise generic root does not prevent an independent narrow root from
  // using generated planes.
  if (staticNBA) {
    FailureOr<NativeStaticNBAPlan> plan =
        buildNativeStaticNBAPlan(module, *stateLayout, staticNBACommits, true);
    if (failed(plan))
      return failure();
    staticNBAPlan = std::move(*plan);
    staticNBA = !staticNBAPlan.roots.empty();
    if (failed(materializeGeneratedNBAAccumulators(module, staticNBAPlan)))
      return failure();
    directStaticState |=
        llvm::any_of(staticNBAPlan.generatedAccumulators,
                     [](const std::string &name) { return !name.empty(); });
    for (auto [root, accumulator] : llvm::zip_equal(
             staticNBAPlan.roots, staticNBAPlan.generatedAccumulators))
      if (!accumulator.empty())
        stateLayout->directHandles.insert(root.static_state);
  }
  if (staticFanoutMetadata) {
    FailureOr<NativeStaticFanoutPlan> fanout = buildNativeStaticFanoutPlan(
        module, *stateLayout, aotEligibility.getActorSlots(), true);
    if (failed(fanout))
      return failure();
    staticFanoutPlan = std::move(*fanout);
    staticFanoutMetadata &= staticFanoutPlan.exact;
    staticFanout &= staticFanoutPlan.exact;
    if (staticFanoutPlan.exact) {
      stateLayout->transitionHandlesExact = true;
      for (const obelisk_rt_static_fanout_entry &entry :
           staticFanoutPlan.entries)
        stateLayout->transitionHandles.insert(entry.static_state);
    }
  }
  if (staticSpecialization && useAOT) {
    FailureOr<SmallVector<obelisk_rt_static_actor_root>> dependencies =
        buildNativeStaticActorRootPlan(module, *stateLayout,
                                       aotEligibility.getActorSlots());
    if (failed(dependencies))
      return failure();
    staticActorRoots = std::move(*dependencies);
  }
  FailureOr<analysis::SimulationScheduleAnalysis> scheduleRanks =
      analysis::SimulationScheduleAnalysis::compute(module);
  if (failed(scheduleRanks))
    return failure();
  if (useAOT) {
    for (auto &entry : analyses) {
      auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
      if (!function)
        return failure();
      auto bytecode =
          aotEligibility.getBytecodeFragments().find(entry.first);
      if (bytecode == aotEligibility.getBytecodeFragments().end())
        continue;
      SmallPtrSet<Block *, 8> bytecodeBlocks(bytecode->second.begin(),
                                             bytecode->second.end());
      auto activationRequiresBytecode = [&](Block *start) {
        SmallVector<Block *> pending{start};
        SmallPtrSet<Block *, 16> visited;
        while (!pending.empty()) {
          Block *block = pending.pop_back_val();
          if (!visited.insert(block).second)
            continue;
          if (bytecodeBlocks.contains(block))
            return true;
          Operation *terminator = block->getTerminator();
          if (sim::isSuspensionOp(terminator))
            continue;
          llvm::append_range(pending, terminator->getSuccessors());
        }
        return false;
      };
      SmallVector<uint32_t> &continuations =
          aotBytecodeContinuations[entry.first];
      if (activationRequiresBytecode(&function.getBody().front()))
        continuations.push_back(0);
      for (const ProcessSuspension &suspension : entry.second->getSuspensions())
        if (activationRequiresBytecode(suspension.continuation))
          continuations.push_back(suspension.continuationID);
      llvm::sort(continuations);
      continuations.erase(
          std::unique(continuations.begin(), continuations.end()),
          continuations.end());
    }
  }
  // Root records are native implementation details, not canonical process
  // state. Insert them only after suspension-live semantic values have been
  // threaded and the shared native/bytecode frame has been analyzed. LLVM
  // coroutine lowering preserves these fixed entry allocas across resume.
  if (failed(instrumentManagedRoots(module)))
    return failure();
  bool guardedAOTSpecialization = staticSpecialization && useAOT &&
                                  aotEligibility.isFullyEligible() && vpiFull &&
                                  (directStaticState || staticNBA);
  // AOT dispatch checks the specialization invariant once per actor
  // activation. Apply that proof to every non-bootstrap actor, including
  // delay/clock processes that are not part of a fused compute body.
  if (guardedAOTSpecialization)
    for (const auto &entry : aotEligibility.getActorSlots()) {
      auto function = dyn_cast<sim::SimFuncOp>(entry.first);
      if (!function ||
          function.getEntryKind() == sim::EntryKind::RootInitializer)
        continue;
      function.walk([&](Operation *nested) {
        if (isa<sim::SimRefLoadOp, sim::SimRefStoreOp>(nested))
          nested->setAttr(kAssumeCleanSpecializationAttr,
                          UnitAttr::get(context));
      });
    }
  if (failed(markCleanStaticNBAsInGuardedBodies(
          module, guardedAOTSpecialization, staticNBAPlan.siteRoots,
          staticNBAPlan.roots, *stateLayout)))
    return failure();

  // Consume the whole-design X/Z proof in the AOT path after suspension
  // threading has reached its final SSA shape. Signatures and canonical frames
  // remain two-plane ABI objects, but proven block arguments, call results,
  // and local producers expose a constant-zero unknown plane to LLVM.
  DenseSet<Value> nativeTwoStateValues;
  DenseSet<Operation *> nativeTwoStateOperations;
  WalkResult stateDomainsComputed = module.walk([&](sim::SimDesignOp design) {
    FailureOr<StateDomainAnalysis> stateDomains =
        StateDomainAnalysis::compute(design);
    if (failed(stateDomains))
      return WalkResult::interrupt();
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      if (function.isExternal())
        continue;
      for (Block &block : function.getBody()) {
        for (BlockArgument argument : block.getArguments())
          if (isa<sim::LogicType>(argument.getType()) &&
              stateDomains->isTwoState(argument))
            nativeTwoStateValues.insert(argument);
        for (Operation &operation : block)
          for (Value result : operation.getResults())
            if (isa<sim::LogicType>(result.getType()) &&
                stateDomains->isTwoState(result))
              nativeTwoStateValues.insert(result);
      }
    }
    return WalkResult::advance();
  });
  if (stateDomainsComputed.wasInterrupted())
    return failure();
  for (Value value : nativeTwoStateValues) {
    auto result = dyn_cast<OpResult>(value);
    if (!result || result.getOwner()->getNumResults() != 1)
      continue;
    nativeTwoStateOperations.insert(result.getOwner());
  }

  // Record the net driven by each operation before dialect conversion starts
  // rewriting function signatures and their block arguments.  Conversion
  // patterns should inspect stable operation metadata instead of chasing the
  // source SSA graph while it is being replaced.
  module.walk([&](sim::SimDriverDriveOp drive) {
    std::optional<uint64_t> driverID = getStaticDriverID(drive.getDriver());
    if (!driverID)
      return;
    for (const NativeStateLayout::Driver &driver : stateLayout->driverLayouts) {
      if (driver.id != *driverID)
        continue;
      drive->setAttr(
          "obelisk.native.net_id",
          IntegerAttr::get(IntegerType::get(context, 64), driver.netId));
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
            sim::ProcessType, sim::ControlType, sim::ObserverType,
            sim::CovergroupHandleType>(type))
      return IntegerType::get(context, 64);
    return std::nullopt;
  });
  packedConverter.addConversion([context](Type type) -> std::optional<Type> {
    if (sim::isManagedHandleType(type))
      return IntegerType::get(context, 64);
    return std::nullopt;
  });
  packedConverter.addConversion([context](sim::ArgumentRefType) -> Type {
    return IntegerType::get(context, 192);
  });
  packedConverter.addConversion(
      [context](sim::ManagedRefType, SmallVectorImpl<Type> &results) {
        results.push_back(IntegerType::get(context, 64));
        results.push_back(IntegerType::get(context, 64));
        return success();
      });
  ReferenceArgumentMap referenceArguments;
  WalkResult lifetimeInputs = module.walk([&](sim::SimFuncOp function) {
    if (function.getBody().empty())
      return WalkResult::advance();
    // Observer captures are borrowed from the persistent computed-wait
    // record. Unlike an ordinary direct call, invoking an observer does not
    // transfer one retained reference per argument, so its return must not
    // consume captured automatic state. The waiting activation owns that
    // state across suspension and releases it on resumption or cancellation.
    if (function.getEntryKind() == sim::EntryKind::Observer)
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
  // This is transaction-local metadata produced only by the AOT signature
  // pattern below. Never consume a same-named source attribute.
  module.walk([](sim::SimFuncOp function) {
    function->removeAttr(kNativeTwoStateBlockUnknownsAttr);
  });
  RewritePatternSet packedPatterns(context);
  populateSimulationToStandardPatterns(packedConverter, packedPatterns,
                                       nativeTwoStateOperations);
  populateSimulationPackedAggregateViewPatterns(packedConverter,
                                                packedPatterns);
  populateSimulationToRuntimePatterns(packedConverter, packedPatterns);
  packedPatterns.add<SimFuncSignatureConversion>(packedConverter, context,
                                                 nativeTwoStateValues);
  packedPatterns.add<SimCallTypeConversion>(packedConverter, context,
                                            nativeTwoStateValues);
  packedPatterns.add<SimTaskCallTypeConversion>(packedConverter, context);
  packedPatterns.add<SimDPICallTypeConversion>(packedConverter, context);
  packedPatterns.add<
      ArithSelectTypeConversion, SimReturnTypeConversion,
      SimObserverBindTypeConversion, SimSuspendObserveTypeConversion,
      AutomaticOwnerReleaseMarkerConversion, PackedAggregateExtractConversion,
      PackedAggregateInsertConversion, PackedAggregateConstructConversion,
      AggregateDynamicExtractConversion, AggregateDefaultConversion,
      UnionConstructConversion, UnionExtractConversion, UnionIsActiveConversion,
      SimSuspendTypeConversion<sim::SimSuspendDelayOp>,
      SimSuspendTypeConversion<sim::SimSuspendChangeOp>,
      SimSuspendTypeConversion<sim::SimSuspendEdgeOp>,
      SimSuspendTypeConversion<sim::SimSuspendEdgeIffOp>,
      SimSuspendTypeConversion<sim::SimSuspendLevelOp>,
      SimSuspendTypeConversion<sim::SimSuspendAnyOp>,
      SimSuspendTypeConversion<sim::SimSuspendEventOp>,
      SimSuspendTypeConversion<sim::SimSuspendForeverOp>,
      SimSuspendTypeConversion<sim::SimSuspendAwaitOp>,
      SimSuspendTypeConversion<sim::SimSuspendJoinOp>,
      SimSuspendTypeConversion<sim::SimSuspendChildrenOp>,
      SimDisableChildrenConversion, SimControlEnterConversion,
      SimControlLeaveConversion, SimControlDisableConversion,
      SimStaticOnceConversion, SimDeferredOnceConversion,
      SimMonitorRegisterConversion, SimMonitorControlConversion,
      SimMonitorCurrentConversion>(packedConverter, context);
  packedPatterns.add<ContextHandleConversion<sim::SimContextStorageOp>>(
      packedConverter, context, stateLayout->storage);
  packedPatterns.add<ContextHandleConversion<sim::SimContextNetOp>>(
      packedConverter, context, stateLayout->nets);
  packedPatterns.add<ContextHandleConversion<sim::SimContextDriverOp>>(
      packedConverter, context, stateLayout->drivers);
  packedPatterns.add<EventHandleConversion,
                     StaticHandleExtractConversion<sim::SimRefExtractOp>,
                     StaticHandleExtractConversion<sim::SimNetExtractOp>,
                     StaticHandleExtractConversion<sim::SimDriverExtractOp>,
                     DynamicHandleExtractConversion<sim::SimRefDynExtractOp>,
                     DynamicHandleExtractConversion<sim::SimDriverDynExtractOp>,
                     SubelementHandleConversion<sim::SimRefSubelementOp>,
                     SubelementHandleConversion<sim::SimDriverSubelementOp>,
                     ArrayElementHandleConversion<sim::SimRefArrayElementOp>,
                     ArrayElementHandleConversion<sim::SimDriverArrayElementOp>,
                     EventTriggerConversion, EventTriggeredConversion,
                     EventEqualConversion, SpawnTypeConversion>(packedConverter,
                                                                context);
  packedPatterns.add<RefLoadConversion, RefStoreConversion>(
      packedConverter, context, stateLayout->bitCount,
      staticSpecialization && useAOT && aotEligibility.isFullyEligible()
          ? &*stateLayout
          : nullptr);
  packedPatterns
      .add<OverrideConversion, ReleaseOverrideConversion, NetReadConversion>(
          packedConverter, context, stateLayout->bitCount);
  packedPatterns.add<
      ClassNullConversion, CovergroupNullConversion, CovergroupCreateConversion,
      CovergroupEnabledConversion, CovergroupBinHitConversion,
      CovergroupSampleConversion,
      CovergroupControlConversion<sim::SimCovergroupStartOp, true>,
      CovergroupControlConversion<sim::SimCovergroupStopOp, false>,
      CovergroupQueryConversion<sim::SimCovergroupInstanceQueryOp, false>,
      CovergroupQueryConversion<sim::SimCovergroupTypeQueryOp, true>,
      ManagedNullConversion, ManagedIsNullConversion, EventNullConversion,
      ContainerSizeConversion, ContainerCreateLikeConversion,
      ContainerCreateConversion, ContainerCloneConversion,
      ContainerDeleteConversion, QueueDeleteConversion, QueueInsertConversion,
      ContainerReadConversion, ContainerWriteConversion, AssocCreateConversion,
      AssocReadConversion, AssocWriteConversion, AssocDefaultConversion,
      AssocExistsConversion, AssocDeleteConversion, AssocTraverseConversion,
      RandomNextConversion, RandomSeedConversion, RandomBoundedConversion,
      StringLiteralConversion, StringFromPackedConversion,
      StringToPackedConversion, StringConcatConversion, StringLengthConversion,
      StringGetcConversion, StringCompareConversion, ClassAllocConversion,
      StringFileOpenConversion<sim::SimFileOpenStringMCDOp>,
      StringFileOpenConversion<sim::SimFileOpenStringOp>,
      StringFileGetlineConversion, ClassCopyConversion,
      ClassIsInstanceConversion, ClassIdConversion, ClassCastConversion,
      ClassFieldRefConversion, ClassRootBindConversion,
      ArgumentRefFromRefConversion, ArgumentRefFromManagedConversion,
      ReferencePathIndexConversion, ReferencePathAssocConversion,
      ArgumentRefFromPathConversion,
      ManagedObjectOutputConversion<sim::SimWeakCreateOp>,
      ManagedObjectOutputConversion<sim::SimWeakGetOp>, WeakClearConversion,
      GCSafepointConversion>(packedConverter, context);
  packedPatterns.add<StringAllocatingCallConversion<sim::SimStringRepeatOp>>(
      packedConverter, context, "obelisk_rt_v1_string_repeat");
  packedPatterns.add<StringAllocatingCallConversion<sim::SimStringPutcOp>>(
      packedConverter, context, "obelisk_rt_v1_string_putc");
  packedPatterns.add<StringAllocatingCallConversion<sim::SimStringSubstrOp>>(
      packedConverter, context, "obelisk_rt_v1_string_substr");
  packedPatterns
      .add<StringAllocatingCallConversion<sim::SimStringCaseConvertOp>>(
          packedConverter, context, "obelisk_rt_v1_string_case_convert");
  packedPatterns
      .add<StringAllocatingCallConversion<sim::SimStringFormatIntegerOp>>(
          packedConverter, context, "obelisk_rt_v1_string_format_integer");
  packedPatterns
      .add<StringAllocatingCallConversion<sim::SimStringFormatRealOp>>(
          packedConverter, context, "obelisk_rt_v1_string_format_real");
  packedPatterns.add<StringParseConversion<sim::SimStringParseIntegerOp>>(
      packedConverter, context, "obelisk_rt_v1_string_parse_integer");
  packedPatterns.add<StringParseConversion<sim::SimStringParseRealOp>>(
      packedConverter, context, "obelisk_rt_v1_string_parse_real");
  packedPatterns.add<ArgumentRefLoadConversion, ArgumentRefStoreConversion>(
      packedConverter, context, dataLayout, stateLayout->bitCount);
  packedPatterns
      .add<ManagedLoadConversion, ManagedStoreConversion, ManagedNBAConversion,
           ReferencePathNBAConversion, ClassVirtualCallConversion>(
          packedConverter, context, dataLayout);
  packedPatterns.add<DriverDriveConversion>(packedConverter, context,
                                            *stateLayout);
  packedPatterns.add<ImmediateNBAConversion>(
      packedConverter, context, stateLayout->bitCount,
      staticNBA ? &staticNBAPlan : nullptr, staticNBA, vpiFull);
  packedPatterns.add<RefAllocConversion>(packedConverter, context);
  ConversionTarget packedTarget(*context);
  packedTarget.addIllegalOp<
      sim::SimBytesConstantOp, sim::SimFinishOp, sim::SimStopOp,
      sim::SimFatalOp, sim::SimTerminationRequestedOp, sim::SimTimeNowOp,
      sim::SimDisplayOp, sim::SimFileOpenMCDOp, sim::SimFileOpenOp,
      sim::SimFileCloseOp, sim::SimFileFlushOp, sim::SimFileGetcOp,
      sim::SimFileUngetcOp, sim::SimFileGetlineOp, sim::SimFileReadPackedOp,
      sim::SimFileEofOp, sim::SimFileSeekOp, sim::SimFileTellOp,
      sim::SimFileRewindOp>();
  packedTarget.addIllegalOp<
      sim::SimContextStorageOp, sim::SimContextNetOp, sim::SimContextDriverOp,
      sim::SimContextEventOp, sim::SimRefAllocOp, sim::SimRefLoadOp,
      sim::SimRefStoreOp, sim::SimOverrideOp, sim::SimReleaseOverrideOp,
      sim::SimNetExtractOp, sim::SimRefExtractOp, sim::SimRefDynExtractOp,
      sim::SimRefSubelementOp, sim::SimRefArrayElementOp, sim::SimNetReadOp,
      sim::SimDriverDriveOp, sim::SimDriverExtractOp,
      sim::SimDriverDynExtractOp, sim::SimDriverSubelementOp,
      sim::SimDriverArrayElementOp, sim::SimNBAEnqueueOp,
      sim::SimEventTriggerOp, sim::SimEventTriggeredOp, sim::SimEventEqualOp,
      sim::SimDisableChildrenOp, sim::SimControlEnterOp, sim::SimControlLeaveOp,
      sim::SimControlDisableOp, sim::SimStaticOnceOp, sim::SimDeferredOnceOp,
      sim::SimMonitorRegisterOp, sim::SimMonitorControlOp,
      sim::SimMonitorCurrentOp, sim::SimBitsDynExtractOp, sim::SimClassNullOp,
      sim::SimCovergroupNullOp, sim::SimCovergroupCreateOp,
      sim::SimCovergroupSampleEnabledOp, sim::SimCovergroupBinHitOp,
      sim::SimCovergroupStartOp, sim::SimCovergroupStopOp,
      sim::SimCovergroupInstanceQueryOp, sim::SimCovergroupTypeQueryOp,
      sim::SimManagedNullOp, sim::SimManagedIsNullOp, sim::SimEventNullOp,
      sim::SimContainerSizeOp, sim::SimContainerCreateLikeOp,
      sim::SimContainerCreateOp, sim::SimContainerCloneOp,
      sim::SimContainerDeleteOp, sim::SimQueueDeleteOp, sim::SimQueueInsertOp,
      sim::SimContainerReadOp, sim::SimContainerWriteOp, sim::SimAssocCreateOp,
      sim::SimAssocReadOp, sim::SimAssocWriteOp, sim::SimAssocExistsOp,
      sim::SimAssocDeleteOp, sim::SimAssocSetDefaultOp, sim::SimAssocTraverseOp,
      sim::SimRandomNextOp, sim::SimRandomSeedOp, sim::SimRandomBoundedOp,
      sim::SimStringLiteralOp, sim::SimStringFromPackedOp,
      sim::SimStringToPackedOp, sim::SimStringConcatOp, sim::SimStringRepeatOp,
      sim::SimStringLengthOp, sim::SimStringGetcOp, sim::SimStringPutcOp,
      sim::SimStringSubstrOp, sim::SimStringCompareOp,
      sim::SimStringCaseConvertOp, sim::SimStringParseIntegerOp,
      sim::SimStringParseRealOp, sim::SimStringFormatIntegerOp,
      sim::SimStringFormatRealOp, sim::SimFileOpenStringMCDOp,
      sim::SimFileOpenStringOp, sim::SimFileGetlineStringOp,
      sim::SimClassAllocOp, sim::SimClassCopyOp, sim::SimClassIsInstanceOp,
      sim::SimClassIdOp, sim::SimClassCastOp, sim::SimClassFieldRefOp,
      sim::SimClassRootBindOp, sim::SimManagedLoadOp, sim::SimManagedStoreOp,
      sim::SimManagedNBAEnqueueOp, sim::SimReferencePathNBAEnqueueOp,
      sim::SimArgumentRefFromRefOp, sim::SimArgumentRefFromManagedOp,
      sim::SimReferencePathIndexOp, sim::SimReferencePathAssocOp,
      sim::SimArgumentRefFromPathOp, sim::SimArgumentRefLoadOp,
      sim::SimArgumentRefStoreOp, sim::SimClassDirectCallOp,
      sim::SimClassVirtualCallOp, sim::SimWeakCreateOp, sim::SimWeakGetOp,
      sim::SimWeakClearOp, sim::SimGCSafepointOp>();
  packedTarget
      .addIllegalOp<sim::SimAggregateDefaultOp, sim::SimAggregateConstructOp,
                    sim::SimAggregateExtractOp, sim::SimAggregateInsertOp,
                    sim::SimArrayDynExtractOp, sim::SimUnionConstructOp,
                    sim::SimUnionExtractOp, sim::SimUnionIsActiveOp>();
  packedTarget.addLegalDialect<runtime::ObeliskRuntimeDialect>();
  packedTarget.addLegalOp<sim::SimContextRuntimeOp, sim::SimStatusCheckOp>();
  packedTarget.addDynamicallyLegalOp<sim::SimFuncOp>(
      [&](sim::SimFuncOp function) {
        return packedConverter.isSignatureLegal(function.getFunctionType()) &&
               packedConverter.isLegal(&function.getBody());
      });
  packedTarget.addDynamicallyLegalOp<
      sim::SimCallOp, sim::SimDPICallOp, sim::SimSpawnOp, sim::SimReturnOp,
      sim::SimTaskCallOp, sim::SimObserverBindOp, sim::SimPackedFlattenOp,
      sim::SimPackedUnflattenOp, sim::SimSuspendDelayOp,
      sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
      sim::SimSuspendLevelOp, sim::SimSuspendAnyOp, sim::SimSuspendEventOp,
      sim::SimSuspendForeverOp, sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp,
      sim::SimSuspendChildrenOp, sim::SimSuspendObserveOp>(
      [&](Operation *operation) { return packedConverter.isLegal(operation); });
  packedTarget.addDynamicallyLegalDialect<
      sim::ObeliskSimulationDialect, arith::ArithDialect,
      cf::ControlFlowDialect, func::FuncDialect>([&](Operation *operation) {
    return hasNoLogic(operation) && packedConverter.isLegal(operation);
  });
  packedTarget.addDynamicallyLegalOp<func::CallOp>([&](func::CallOp call) {
    return call.getCallee() != kAutomaticOwnerReleaseMarker && hasNoLogic(call);
  });
  packedTarget.addDynamicallyLegalOp<cf::BranchOp, cf::CondBranchOp>(
      [&](Operation *operation) { return packedConverter.isLegal(operation); });
  packedTarget.addDynamicallyLegalOp<ModuleOp>(hasNoLogic);
  packedTarget.markUnknownOpDynamicallyLegal(hasNoLogic);
  if (failed(
          applyFullConversion(module, packedTarget, std::move(packedPatterns))))
    return failure();
  if (failed(materializeDPIThunks(module)))
    return failure();

  // Region signature conversion records the physical unknown-plane block
  // arguments that the whole-design proof made redundant. Replace them only
  // after dialect conversion has finished remapping every original logic use;
  // doing this inside the signature pattern would not update future one-to-N
  // operand adaptors owned by the conversion driver.
  WalkResult specializedBlockArguments =
      module.walk([&](sim::SimFuncOp function) {
        auto mappings = function->getAttrOfType<ArrayAttr>(
            kNativeTwoStateBlockUnknownsAttr);
        if (!mappings)
          return WalkResult::advance();
        if (mappings.size() != function.getBody().getBlocks().size()) {
          function.emitOpError(
              "has invalid native two-state block-argument metadata");
          return WalkResult::interrupt();
        }
        OpBuilder builder(context);
        for (auto [block, mapping] :
             llvm::zip_equal(function.getBody(), mappings)) {
          auto indices = dyn_cast<DenseI64ArrayAttr>(mapping);
          if (!indices) {
            function.emitOpError(
                "has malformed native two-state block-argument metadata");
            return WalkResult::interrupt();
          }
          builder.setInsertionPointToStart(&block);
          for (int64_t index : indices.asArrayRef()) {
            if (index < 0 ||
                static_cast<uint64_t>(index) >= block.getNumArguments()) {
              function.emitOpError(
                  "has out-of-range native two-state block argument");
              return WalkResult::interrupt();
            }
            BlockArgument argument =
                block.getArgument(static_cast<unsigned>(index));
            auto type = dyn_cast<IntegerType>(argument.getType());
            if (!type) {
              function.emitOpError(
                  "has non-integer native two-state unknown plane");
              return WalkResult::interrupt();
            }
            Value zero = arith::ConstantOp::create(
                builder, function.getLoc(), type,
                builder.getIntegerAttr(type, APInt::getZero(type.getWidth())));
            argument.replaceAllUsesWith(zero);
          }
        }
        function->removeAttr(kNativeTwoStateBlockUnknownsAttr);
        return WalkResult::advance();
      });
  if (specializedBlockArguments.wasInterrupted() ||
      failed(threadRuntimeStatuses(module)) ||
      failed(releaseNativeAutomaticState(module, referenceArguments)))
    return failure();
  if (failed(validateRuntimeToLLVMPreconditions(module, dataLayout)))
    return failure();

  DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> aotFusionGroups;
  if (useAOT) {
    sim::SimDesignOp design;
    module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
    ArrayAttr fusions =
        design ? design->getAttrOfType<ArrayAttr>(sim::metadata::staticFusion)
               : ArrayAttr{};
    sim::ComputeGraphAttr graph =
        design ? design.getComputeGraphAttr() : nullptr;
    if (fusions && graph) {
      for (Attribute fusionAttribute : fusions) {
        auto fusion = dyn_cast<sim::ComputeFusionAttr>(fusionAttribute);
        if (!fusion)
          return design.emitOpError("has malformed static fusion metadata"),
                 failure();
        for (int64_t fragmentIndex : fusion.getFragments().asArrayRef()) {
          if (fragmentIndex < 0 ||
              static_cast<uint64_t>(fragmentIndex) >= graph.getNodes().size())
            return design.emitOpError(
                       "static fusion references an invalid compute fragment"),
                   failure();
          auto fragment = dyn_cast<sim::ComputeFragmentAttr>(
              graph.getNodes()[static_cast<size_t>(fragmentIndex)]);
          sim::SimFuncOp function = fragment
                                        ? design.lookupSymbol<sim::SimFuncOp>(
                                              fragment.getFunction().getValue())
                                        : nullptr;
          Block *block = function ? analysis::lookupComputeGraphBlock(
                                        function, fragment.getBlock())
                                  : nullptr;
          auto actor =
              function
                  ? aotEligibility.getActorSlots().find(function.getOperation())
                  : aotEligibility.getActorSlots().end();
          sim::ContinuationSiteAttr site;
          if (block) {
            Operation *terminator = block->getTerminator();
            if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(terminator))
              site = suspend.getSiteAttr();
            else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(terminator))
              site = suspend.getSiteAttr();
          }
          if (!fragment || !block || !site)
            return design.emitOpError(
                       "static fusion references a stale AOT continuation"),
                   failure();
          // Fusion metadata describes graph-level opportunities and is built
          // before native AOT actor eligibility is known. Hybrid lowering must
          // retain valid bytecode-only fragments without treating them as
          // stale metadata.
          if (actor == aotEligibility.getActorSlots().end())
            continue;
          auto [entry, inserted] = aotFusionGroups.try_emplace(
              std::pair{actor->second, site.getId()}, fusion.getId());
          if (!inserted && entry->second != fusion.getId())
            return design.emitOpError(
                       "AOT continuation appears in multiple fusion groups"),
                   failure();
        }
      }
    }
  }
  auto fusionGroupFor = [&](uint32_t slot, uint32_t continuation) {
    auto found = aotFusionGroups.find({slot, continuation});
    return found == aotFusionGroups.end() ? UINT32_MAX : found->second;
  };

  SmallVector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>>
      rankedAOTNodes;
  for (auto &entry : analyses) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    if (!function)
      return failure();
    NativeSchedulePlan schedule;
    schedule.initialRank =
        scheduleRanks->getEntryRank(entry.first).value_or(0);
    if (useAOT) {
      auto slot = aotEligibility.getActorSlots().find(entry.first);
      if (slot != aotEligibility.getActorSlots().end())
        schedule.actorSlot = slot->second;
    }
    if (schedule.actorSlot) {
      auto bytecode = aotBytecodeContinuations.find(entry.first);
      if (bytecode != aotBytecodeContinuations.end())
        schedule.bytecodeContinuations = bytecode->second;
    }
    DenseMap<uint32_t, uint32_t> continuationRanks;
    for (const ProcessSuspension &suspension : entry.second->getSuspensions()) {
      uint32_t rank =
          scheduleRanks->getBlockRank(suspension.continuation).value_or(0);
      auto [rankIt, inserted] =
          continuationRanks.try_emplace(suspension.continuationID, rank);
      if (!inserted && rankIt->second != rank)
        return suspension.operation->emitError(
            "continuation ID has inconsistent schedule ranks");
    }
    for (auto [continuation, rank] : continuationRanks)
      schedule.continuations.emplace_back(continuation, rank);
    llvm::sort(schedule.continuations, [](const auto &left, const auto &right) {
      return left.first < right.first;
    });
    if (schedule.actorSlot) {
      rankedAOTNodes.emplace_back(
          scheduleRanks->getBlockRank(&function.getBody().front()).value_or(0),
          *schedule.actorSlot, 0, UINT32_MAX);
      for (const ProcessSuspension &suspension : entry.second->getSuspensions())
        rankedAOTNodes.emplace_back(
            scheduleRanks->getBlockRank(suspension.continuation).value_or(0),
            *schedule.actorSlot, suspension.continuationID,
            fusionGroupFor(*schedule.actorSlot, suspension.continuationID));
    }
    if (failed(makeProcessActivationHelper(module, function, *entry.second)))
      return failure();
    if (failed(
            makeProcessSpawnHelper(module, function, *entry.second, schedule)))
      return failure();
  }
  if (useAOT) {
    llvm::SmallDenseSet<uint32_t, 16> entrySlots;
    for (auto [rank, slot, continuation, fusionGroup] : rankedAOTNodes) {
      (void)rank;
      (void)fusionGroup;
      if (slot >= aotEligibility.getActorSlots().size())
        return module.emitError("AOT node references an invalid actor slot");
      if (continuation == 0)
        entrySlots.insert(slot);
    }
    if (entrySlots.size() != aotEligibility.getActorSlots().size())
      return module.emitError(
          "AOT node inventory is missing an actor entry continuation");
    llvm::sort(rankedAOTNodes);
    rankedAOTNodes.erase(
        std::unique(rankedAOTNodes.begin(), rankedAOTNodes.end()),
        rankedAOTNodes.end());
    SmallVector<obelisk_rt_native_schedule_node> executableNodes;
    executableNodes.reserve(rankedAOTNodes.size());
    for (auto [rank, slot, continuation, fusionGroup] : rankedAOTNodes) {
      (void)rank;
      executableNodes.push_back({slot, continuation, fusionGroup});
    }
    bool rootSlotZero =
        llvm::any_of(aotEligibility.getActorSlots(), [](const auto &entry) {
          auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
          return function &&
                 function.getEntryKind() == sim::EntryKind::RootInitializer &&
                 entry.second == 0;
        });
    if (failed(makeNativeAOTPlan(
            module, aotEligibility.getActorSlots().size(), executableNodes,
            *stateLayout, staticNBAPlan, staticFanoutPlan, staticActorRoots,
            directStaticState, staticNBA, staticControl, staticFanout,
            cleanSuperstep, aotEligibility.isFullyEligible(), rootSlotZero)))
      return failure();
  }
  if (failed(makeSchedulerMain(module, *stateLayout, useAOT)))
    return failure();

  SmallVector<sim::SimFuncOp> ordinary;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::Function ||
        function.getEntryKind() == sim::EntryKind::Observer)
      ordinary.push_back(function);
  });
  for (sim::SimFuncOp function : ordinary)
    if (failed(lowerOrdinaryFunction(function)))
      return failure();
  if (failed(materializeManagedMethodThunks(module, dataLayout)))
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
              sim::SimStorageDeclOp, sim::SimNetDeclOp, sim::SimDriverDeclOp,
              sim::SimNetConnectDeclOp, sim::SimClassDeclOp,
              sim::SimCovergroupDeclOp, sim::SimClassFieldDeclOp,
              sim::SimClassMethodDeclOp>(operation)) {
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
    if (failed(materializeNativeObserverThunks(module))) {
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

void populateSimulationCoroutineToLLVMPatterns(
    const LLVMTypeConverter &converter, RewritePatternSet &patterns) {
  populateRuntimeToLLVMPatterns(converter, patterns);
  patterns.add<SimContextRuntimeLowering>(converter, patterns.getContext());
  arith::populateArithToLLVMConversionPatterns(converter, patterns);
  cf::populateControlFlowToLLVMConversionPatterns(converter, patterns);
  populateMathToLLVMConversionPatterns(converter, patterns);
  populateFuncToLLVMConversionPatterns(converter, patterns);
}

} // namespace obelisk
