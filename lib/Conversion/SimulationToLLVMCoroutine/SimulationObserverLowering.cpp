//===- SimulationObserverLowering.cpp - Native observer lowering ----------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

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

} // namespace

LogicalResult
serializeComputedObserverWait(Operation *operation, Value wait,
                              uint64_t waitSize, OpBuilder &builder,
                              SmallVectorImpl<Operation *> &observerBindings) {
  auto observe = dyn_cast<sim::SimSuspendObserveOp>(operation);
  if (!observe)
    return operation->emitError("expected an observer suspension");
  Location location = operation->getLoc();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  uint32_t primaryCount = observe.getEdges().size();
  uint32_t conditionCount = observe.getConditionCount();
  uint32_t observerCount = primaryCount + conditionCount;
  auto planeCounts = operation->getAttrOfType<DenseI32ArrayAttr>(
      "obelisk.coro.initial_plane_counts");
  auto conditionBegin = operation->getAttrOfType<IntegerAttr>(
      "obelisk.coro.condition_operand_begin");
  if (!planeCounts || planeCounts.size() != primaryCount || !conditionBegin)
    return operation->emitError("missing converted observer operand metadata");
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
      return operation->emitError("condition observer inventory is truncated");
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
  for (auto [index, binding] : llvm::enumerate(bindings)) {
    captureCount += binding.getCaptureCount();
    dependencyCount += binding.getDependencies().size();
    auto width =
        binding->getAttrOfType<IntegerAttr>("obelisk.coro.observer_width");
    auto fourState =
        binding->getAttrOfType<BoolAttr>("obelisk.coro.observer_four_state");
    if (!width || !fourState)
      return binding.emitOpError("missing converted observer metadata");
    widths.push_back(width.getValue().getZExtValue());
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
  storeI64(72, 0);
  storeI64(80, totalSize);
  storeI64(88, 0);

  uint32_t captureCursor = 0;
  uint32_t dependencyCursor = 0;
  uint32_t previousCursor = 0;
  llvm::SetVector<Operation *> uniqueBindings;
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
      return binding.emitOpError("has malformed converted dependency metadata");
    uint64_t entry =
        observersOffset + index * sizeof(obelisk_rt_computed_observer_v1);
    storeI64(entry, observerID.getValue().getZExtValue());
    storeI32(entry + 8, captureCursor);
    storeI32(entry + 12, binding.getCaptureCount());
    storeI32(entry + 16, dependencyCursor);
    storeI32(entry + 20, binding.getDependencies().size());
    storeI32(entry + 24, index < primaryCount
                             ? static_cast<uint32_t>(previousValueOffset +
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
    uniqueBindings.insert(binding);
  }
  llvm::append_range(observerBindings, uniqueBindings);

  bool levelTrue =
      operation->hasAttr("obelisk_sim.concurrent_cancel_level_true");

  for (uint32_t index = 0; index != primaryCount; ++index) {
    uint64_t clause =
        clausesOffset + uint64_t{index} * sizeof(obelisk_rt_computed_clause_v1);
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
                 : (levelTrue ? OBELISK_RT_COMPUTED_CLAUSE_LEVEL_TRUE : 0));
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

} // namespace obelisk::detail
