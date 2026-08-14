//===- BytecodeWaitEncoding.cpp - Bytecode suspension encoding -----------===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"
#include "obelisk/Conversion/SimulationRuntime.h"

#include <cstddef>

using namespace mlir;

namespace obelisk::bytecode {

LogicalResult Encoder::encodeObserverWait(FunctionPlan &plan,
                                          sim::SimSuspendObserveOp operation) {
  if (!plan.frame)
    return operation.emitOpError("suspension has no canonical frame");
  const ProcessSuspension *suspension = plan.frame->getSuspension(operation);
  if (!suspension)
    return operation.emitOpError("suspension is missing frame analysis");
  ArrayRef<ProcessFrameValue> slots =
      plan.frame->getContinuationLayout(suspension->continuationID);
  if (slots.size() != operation.getContinuationOperands().size())
    return operation.emitOpError("continuation frame arity mismatch");
  for (auto [value, slot] :
       llvm::zip_equal(operation.getContinuationOperands(), slots)) {
    uint64_t transferSize =
        slot.storageSize * (slot.hasSecondaryStorage() ? 2 : 1);
    if (transferSize > UINT32_MAX)
      return operation.emitOpError(
          "canonical frame transfer exceeds the bytecode ABI limit");
    emitFrameTransfer(plan, StoreFrame, value, slot.valueOffset,
                      static_cast<uint32_t>(transferSize));
  }

  uint32_t primaryCount = operation.getEdges().size();
  uint32_t conditionCount = operation.getConditionCount();
  uint32_t observerCount = primaryCount + conditionCount;
  SmallVector<sim::SimObserverBindOp> bindings;
  for (Value value : operation.getPrimaries()) {
    auto binding = value.getDefiningOp<sim::SimObserverBindOp>();
    if (!binding)
      return operation.emitOpError(
          "primary token is not produced by observer.bind");
    bindings.push_back(binding);
  }
  for (Value value : operation.getConditions()) {
    auto binding = value.getDefiningOp<sim::SimObserverBindOp>();
    if (!binding)
      return operation.emitOpError(
          "condition token is not produced by observer.bind");
    bindings.push_back(binding);
  }
  if (bindings.size() != observerCount)
    return operation.emitOpError("observer inventory is truncated");
  uint32_t captureCount = 0;
  uint32_t dependencyCount = 0;
  uint32_t previousLimbs = 0;
  SmallVector<uint32_t> widths;
  for (auto [index, binding] : llvm::enumerate(bindings)) {
    captureCount += binding.getCaptures().size();
    dependencyCount += binding.getDependencies().size();
    auto observerType = cast<sim::ObserverType>(binding.getResult().getType());
    std::optional<uint32_t> width =
        simulationWidth(observerType.getResultType());
    if (!width)
      return binding.emitOpError("observer result has no packed width");
    widths.push_back(*width);
    if (index < primaryCount)
      previousLimbs += (*width + 63) / 64;
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
  uint64_t previousOffset =
      clausesOffset +
      uint64_t{primaryCount} * sizeof(obelisk_rt_computed_clause_v1);
  uint64_t totalSize =
      previousOffset + uint64_t{previousLimbs} * sizeof(uint64_t) * 2;
  if (totalSize > suspension->waitSize)
    return operation.emitOpError(
        "computed wait exceeds its canonical frame field");

  SmallVector<uint8_t> bytes(suspension->waitSize, 0);
  write32(bytes, 0, OBELISK_RT_VERSION);
  write32(bytes, 4, OBELISK_RT_SUSPEND_OBSERVER);
  write32(bytes, 8, OBELISK_RT_COMPUTED_WAIT_INTERLEAVED);
  write32(bytes, 12, primaryCount);
  write32(bytes, 16, observerCount);
  write32(bytes, 20, captureCount);
  write32(bytes, 24, dependencyCount);
  write32(bytes, 28, previousLimbs);
  write64(bytes, 32, observersOffset);
  write64(bytes, 40, capturesOffset);
  write64(bytes, 48, dependenciesOffset);
  write64(bytes, 56, clausesOffset);
  write64(bytes, 64, previousOffset);
  write64(bytes, 72, 0);
  write64(bytes, 80, totalSize);

  uint32_t captureCursor = 0;
  uint32_t dependencyCursor = 0;
  uint32_t previousCursor = 0;
  for (auto [index, binding] : llvm::enumerate(bindings)) {
    auto found = indices.find(binding.getEvaluator());
    if (found == indices.end())
      return binding.emitOpError("observer evaluator has no bytecode body");
    FunctionPlan &evaluator = plans[found->second];
    uint64_t entry =
        observersOffset + index * sizeof(obelisk_rt_computed_observer_v1);
    write64(bytes, entry, evaluator.stableID);
    write32(bytes, entry + 8, captureCursor);
    write32(bytes, entry + 12, binding.getCaptures().size());
    write32(bytes, entry + 16, dependencyCursor);
    write32(bytes, entry + 20, binding.getDependencies().size());
    write32(bytes, entry + 24,
            index < primaryCount
                ? static_cast<uint32_t>(previousOffset +
                                        uint64_t{previousCursor} * 16)
                : UINT32_MAX);
    captureCursor += binding.getCaptures().size();
    for (Value dependency : binding.getDependencies()) {
      uint64_t entryOffset =
          dependenciesOffset + uint64_t{dependencyCursor} *
                                   sizeof(obelisk_rt_computed_dependency_v1);
      if (isa<sim::ManagedWatchType>(dependency.getType())) {
        write32(bytes, entryOffset + 8,
                OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED);
        write32(bytes, entryOffset + 12, 1);
      } else if (auto event = dyn_cast<sim::EventType>(dependency.getType())) {
        (void)event;
        write32(bytes, entryOffset + 8, OBELISK_RT_OBSERVER_DEPENDENCY_EVENT);
        write32(bytes, entryOffset + 12, 1);
      } else {
        Type element =
            isa<sim::RefType>(dependency.getType())
                ? cast<sim::RefType>(dependency.getType()).getElementType()
                : cast<sim::NetType>(dependency.getType()).getElementType();
        std::optional<uint32_t> width = simulationWidth(element);
        if (!width)
          return operation.emitOpError(
              "observer dependency has no simulation storage width");
        write32(bytes, entryOffset + 8, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL);
        write32(bytes, entryOffset + 12, *width);
      }
      ++dependencyCursor;
    }
    if (index < primaryCount)
      previousCursor += (uint64_t{widths[index]} + 63) / 64;
  }
  bool levelTrue =
      operation->hasAttr("obelisk_sim.concurrent_cancel_level_true");
  for (uint32_t index = 0; index != primaryCount; ++index) {
    uint64_t clause =
        clausesOffset + uint64_t{index} * sizeof(obelisk_rt_computed_clause_v1);
    int32_t condition = operation.getConditionIndices()[index];
    write32(bytes, clause, index);
    write32(bytes, clause + 4,
            condition < 0 ? OBELISK_RT_OBSERVER_CONDITION_NONE
                          : primaryCount + static_cast<uint32_t>(condition));
    write32(bytes, clause + 8, operation.getEdges()[index]);
    write32(bytes, clause + 12,
            bindings[index]->hasAttr("obelisk_sim.event_primary")
                ? OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY
                : (levelTrue ? OBELISK_RT_COMPUTED_CLAUSE_LEVEL_TRUE : 0));
  }

  Layout record{Bits,
                0,
                static_cast<uint32_t>(suspension->waitSize * 8),
                0,
                suspension->waitSize,
                0};
  uint32_t recordRegister = plan.layouts.size();
  record.offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
  plan.scratchSize = record.offset + record.size;
  plan.layouts.push_back(record);
  uint64_t constantOffset = constants.size();
  llvm::append_range(constants, bytes);
  emit({Constant, 0, recordRegister, 0, 0, 0, 0, constantOffset});
  emit({StoreFrame, 0, 0, recordRegister, 0, 0, 0, suspension->waitOffset});
  // Dynamic captures and dependencies overwrite their zeroed record slots
  // after the constant header has been copied.
  // Re-emit them because the stores above intentionally describe the final
  // frame addresses but precede this constant in the instruction stream.
  captureCursor = 0;
  dependencyCursor = 0;
  for (sim::SimObserverBindOp binding : bindings) {
    for (Value capture : binding.getCaptures()) {
      uint32_t transferSize =
          sim::isManagedHandleType(capture.getType())
              ? static_cast<uint32_t>(sizeof(uint64_t))
              : static_cast<uint32_t>(
                    sizeof(obelisk_rt_computed_capture_v1));
      emitFrameTransfer(plan, StoreFrame, capture,
                        suspension->waitOffset + capturesOffset +
                            uint64_t{captureCursor++} *
                                sizeof(obelisk_rt_computed_capture_v1),
                        transferSize);
    }
    for (Value dependency : binding.getDependencies()) {
      if (isa<sim::ManagedWatchType>(dependency.getType())) {
        emitFrameTransfer(
            plan, StoreFrame, dependency,
            suspension->waitOffset + dependenciesOffset +
                uint64_t{dependencyCursor++} *
                    sizeof(obelisk_rt_computed_dependency_v1),
            sizeof(uint64_t));
        continue;
      }
      uint32_t stableID =
          temporary(plan, IntegerType::get(operation.getContext(), 64));
      if (stableID == kInvalidRegister)
        return failure();
      emit({HandleID, 0, stableID, reg(plan, dependency)});
      emit({StoreFrame, 0, 0, stableID, 0, 0, 0,
            suspension->waitOffset + dependenciesOffset +
                uint64_t{dependencyCursor++} *
                    sizeof(obelisk_rt_computed_dependency_v1)});
    }
  }
  previousCursor = 0;
  for (auto [index, initial] : llvm::enumerate(operation.getInitialValues())) {
    uint64_t offset = suspension->waitOffset + previousOffset +
                      uint64_t{previousCursor} * sizeof(uint64_t) * 2;
    Layout layout = plan.layouts[reg(plan, initial)];
    if (layout.size > UINT32_MAX)
      return operation.emitOpError("observer initial value is too large");
    emitFrameTransfer(plan, StoreFrame, initial, offset,
                      static_cast<uint32_t>(layout.size));
    previousCursor += (uint64_t{widths[index]} + 63) / 64;
  }
  Layout offsetLayout{Bits, 0, 64, 0, 8, 0};
  uint32_t offsetRegister = plan.layouts.size();
  offsetLayout.offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
  plan.scratchSize = offsetLayout.offset + offsetLayout.size;
  plan.layouts.push_back(offsetLayout);
  emit({Constant, 0, offsetRegister, 0, 0, 0, 0,
        addConstant(offsetLayout, APInt(64, suspension->waitOffset))});
  uint32_t actionFlags = getRuntimeResumeActionFlags(operation);
  if (actionFlags == UINT32_MAX)
    return operation.emitOpError("has no executable resume region");
  emit({Suspend, OBELISK_RT_SUSPEND_OBSERVER, 0, offsetRegister, 0, 0,
        actionFlags, suspension->continuationID});
  return success();
}

LogicalResult Encoder::encodeWait(FunctionPlan &plan, Operation *operation,
                                  ValueRange continuationOperands,
                                  obelisk_rt_suspend_kind kind,
                                  obelisk_rt_wait_flags flags,
                                  ArrayRef<uint32_t> edges,
                                  ArrayRef<Value> watched, Value delay) {
  if (!plan.frame)
    return operation->emitOpError("suspension has no canonical frame");
  const ProcessSuspension *suspension = plan.frame->getSuspension(operation);
  if (!suspension)
    return operation->emitOpError("suspension is missing frame analysis");
  ArrayRef<ProcessFrameValue> slots =
      plan.frame->getContinuationLayout(suspension->continuationID);
  if (slots.size() != continuationOperands.size())
    return operation->emitOpError("continuation frame arity mismatch");
  for (auto [value, slot] : llvm::zip_equal(continuationOperands, slots))
    if (slot.storageSize > UINT32_MAX ||
        (slot.hasSecondaryStorage() && slot.storageSize > UINT32_MAX / 2))
      return operation->emitOpError(
          "canonical frame transfer exceeds the bytecode ABI limit");
    else {
      uint64_t transferSize =
          slot.storageSize * (slot.hasSecondaryStorage() ? 2 : 1);
      emitFrameTransfer(plan, StoreFrame, value, slot.valueOffset,
                        static_cast<uint32_t>(transferSize));
    }
  if (suspension->waitSize < 32 || (suspension->waitSize - 32) % 16 != 0)
    return operation->emitOpError("wait record size does not match operands");
  uint64_t entryCapacity = (suspension->waitSize - 32) / 16;
  if (edges.size() > entryCapacity || watched.size() != edges.size())
    return operation->emitOpError("wait record size does not match operands");
  Layout record{Bits,
                0,
                static_cast<uint32_t>(suspension->waitSize * 8),
                0,
                suspension->waitSize,
                0};
  uint32_t recordRegister = plan.layouts.size();
  record.offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
  plan.scratchSize = record.offset + record.size;
  plan.layouts.push_back(record);
  SmallVector<uint8_t> bytes(suspension->waitSize, 0);
  bool signalWait = kind == OBELISK_RT_SUSPEND_CHANGE ||
                    kind == OBELISK_RT_SUSPEND_EDGE;
  write32(bytes, 0, OBELISK_RT_VERSION);
  write32(bytes, 4, kind);
  write32(bytes, 8, flags);
  write32(bytes, 12, edges.size());
  for (auto [index, edge] : llvm::enumerate(edges)) {
    write32(bytes, 32 + index * 16 + 8, edge);
    if (signalWait) {
      Type type = watched[index].getType();
      Type element;
      if (auto reference = dyn_cast<sim::RefType>(type))
        element = reference.getElementType();
      else if (auto net = dyn_cast<sim::NetType>(type))
        element = net.getElementType();
      else if (auto driver = dyn_cast<sim::DriverType>(type))
        element = driver.getElementType();
      std::optional<uint32_t> width = simulationWidth(element);
      if (!width)
        return operation->emitOpError(
            "signal wait handle has no fixed-width element");
      if (edge != static_cast<uint32_t>(sim::EdgeKind::Change) &&
          edge != OBELISK_RT_WAIT_EDGE_NONE)
        *width = 1;
      write32(bytes, 32 + index * 16 + 12, *width);
    }
  }
  uint64_t constantOffset = constants.size();
  llvm::append_range(constants, bytes);
  emit({Constant, 0, recordRegister, 0, 0, 0, 0, constantOffset});
  emit({StoreFrame, 0, 0, recordRegister, 0, 0, 0, suspension->waitOffset});
  for (auto [index, handle] : llvm::enumerate(watched)) {
    // Process and mailbox waits carry runtime handles directly. Other wait
    // operands are design handles and must be converted to stable IDs.
    if (isa<sim::ProcessType, sim::MailboxType, sim::SemaphoreType>(
            handle.getType())) {
      emit({StoreFrame, 0, 0, reg(plan, handle), 0, 0, 0,
            suspension->waitOffset + sizeof(obelisk_rt_wait_record_v1) +
                index * sizeof(obelisk_rt_wait_entry_v1)});
      continue;
    }
    uint32_t stableID =
        temporary(plan, IntegerType::get(operation->getContext(), 64));
    if (stableID == kInvalidRegister)
      return failure();
    emit({HandleID, 0, stableID, reg(plan, handle)});
    emit({StoreFrame, 0, 0, stableID, 0, 0, 0,
          suspension->waitOffset + sizeof(obelisk_rt_wait_record_v1) +
              index * sizeof(obelisk_rt_wait_entry_v1)});
  }
  if (delay)
    emit({StoreFrame, 0, 0, reg(plan, delay), 0, 0, 0,
          suspension->waitOffset +
              offsetof(obelisk_rt_wait_record_v1, payload)});
  Layout offsetLayout{Bits, 0, 64, 0, 8, 0};
  uint32_t offsetRegister = plan.layouts.size();
  offsetLayout.offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
  plan.scratchSize = offsetLayout.offset + offsetLayout.size;
  plan.layouts.push_back(offsetLayout);
  APInt waitOffset(64, suspension->waitOffset);
  emit({Constant, 0, offsetRegister, 0, 0, 0, 0,
        addConstant(offsetLayout, waitOffset)});
  uint32_t actionFlags = getRuntimeResumeActionFlags(operation);
  if (actionFlags == UINT32_MAX)
    return operation->emitOpError("has no executable resume region");
  emit({Suspend, static_cast<uint16_t>(kind), 0, offsetRegister, 0, 0,
        actionFlags, suspension->continuationID});
  return success();
}

} // namespace obelisk::bytecode
