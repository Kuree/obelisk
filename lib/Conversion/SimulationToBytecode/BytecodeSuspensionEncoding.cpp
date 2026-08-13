//===- BytecodeSuspensionEncoding.cpp - Suspension instruction selection -===//

#include "BytecodeEncoder.h"

using namespace mlir;

namespace obelisk::bytecode {

static uint32_t directSignalWaitFlags(Operation *operation) {
  return operation->hasAttr(sim::metadata::topLevelWildcardWait)
             ? OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF
             : OBELISK_RT_WAIT_FLAGS_NONE;
}

std::optional<LogicalResult>
Encoder::encodeSuspensionOperation(FunctionPlan &plan, Operation *operation) {
  if (isa<sim::SimObserverBindOp>(operation))
    return success();
  if (auto control = dyn_cast<sim::SimProcessControlOp>(operation)) {
    if (!plan.frame)
      return control.emitOpError("process control has no canonical frame");
    const ProcessSuspension *suspension = plan.frame->getSuspension(control);
    if (!suspension)
      return control.emitOpError("process control is missing frame analysis");
    ArrayRef<ProcessFrameValue> slots =
        plan.frame->getContinuationLayout(suspension->continuationID);
    if (slots.size() != control.getContinuationOperands().size())
      return control.emitOpError("continuation frame arity mismatch");
    for (auto [value, slot] :
         llvm::zip_equal(control.getContinuationOperands(), slots)) {
      uint64_t transferSize =
          slot.storageSize * (slot.hasSecondaryStorage() ? 2 : 1);
      if (transferSize > UINT32_MAX)
        return control.emitOpError(
            "canonical frame transfer exceeds the bytecode ABI limit");
      emitFrameTransfer(plan, StoreFrame, value, slot.valueOffset,
                        static_cast<uint32_t>(transferSize));
    }
    emit({ProcessControl, static_cast<uint16_t>(control.getKind()), 0,
          reg(plan, control.getProcess()), 0, 0, 0,
          suspension->continuationID});
    return success();
  }
  if (auto suspend = dyn_cast<sim::SimSuspendDelayOp>(operation))
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_DELAY, OBELISK_RT_WAIT_FLAGS_NONE, {},
                      {}, suspend.getDelay());
  if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(operation)) {
    uint32_t edge = OBELISK_RT_WAIT_EDGE_CHANGE;
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_CHANGE,
                      directSignalWaitFlags(suspend.getOperation()),
                      ArrayRef<uint32_t>(&edge, 1), {suspend.getWatched()});
  }
  if (auto suspend = dyn_cast<sim::SimSuspendLevelOp>(operation)) {
    uint32_t edge = OBELISK_RT_WAIT_EDGE_CHANGE;
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_CHANGE, OBELISK_RT_WAIT_LEVEL_TRUE,
                      ArrayRef<uint32_t>(&edge, 1), {suspend.getWatched()});
  }
  if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(operation)) {
    uint32_t edge = static_cast<uint32_t>(suspend.getEdge());
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_EDGE, OBELISK_RT_WAIT_FLAGS_NONE,
                      ArrayRef<uint32_t>(&edge, 1), {suspend.getWatched()});
  }
  if (auto suspend = dyn_cast<sim::SimSuspendEdgeIffOp>(operation)) {
    SmallVector<uint32_t> edges{static_cast<uint32_t>(suspend.getEdge()),
                                OBELISK_RT_WAIT_EDGE_NONE};
    SmallVector<Value> watched{suspend.getWatched(), suspend.getCondition()};
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_EDGE, OBELISK_RT_WAIT_EDGE_IFF, edges,
                      watched);
  }
  if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(operation)) {
    SmallVector<uint32_t> edges;
    for (int32_t edge : suspend.getEdges())
      edges.push_back(static_cast<uint32_t>(edge));
    SmallVector<Value> watched(suspend.getWatched());
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_EDGE,
                      directSignalWaitFlags(suspend.getOperation()), edges,
                      watched);
  }
  if (auto suspend = dyn_cast<sim::SimSuspendEventOp>(operation)) {
    uint32_t edge = OBELISK_RT_WAIT_EDGE_NONE;
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_EVENT, OBELISK_RT_WAIT_FLAGS_NONE,
                      ArrayRef<uint32_t>(&edge, 1), {suspend.getEvent()});
  }
  if (auto suspend = dyn_cast<sim::SimSuspendMailboxOp>(operation)) {
    uint32_t edge = OBELISK_RT_WAIT_EDGE_NONE;
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_MAILBOX,
                      static_cast<uint32_t>(suspend.getKind()),
                      ArrayRef<uint32_t>(&edge, 1), {suspend.getMailbox()});
  }
  if (auto suspend = dyn_cast<sim::SimSuspendSemaphoreOp>(operation)) {
    uint32_t edge = OBELISK_RT_WAIT_EDGE_NONE;
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_SEMAPHORE, OBELISK_RT_WAIT_FLAGS_NONE,
                      ArrayRef<uint32_t>(&edge, 1), {suspend.getSemaphore()},
                      suspend.getKeys());
  }
  if (auto suspend = dyn_cast<sim::SimSuspendForeverOp>(operation))
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_FOREVER, OBELISK_RT_WAIT_FLAGS_NONE,
                      {}, {});
  if (auto suspend = dyn_cast<sim::SimSuspendAwaitOp>(operation)) {
    uint32_t edge = OBELISK_RT_WAIT_EDGE_NONE;
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_AWAIT, OBELISK_RT_WAIT_FLAGS_NONE,
                      ArrayRef<uint32_t>(&edge, 1), {suspend.getProcess()});
  }
  if (auto suspend = dyn_cast<sim::SimSuspendJoinOp>(operation)) {
    SmallVector<uint32_t> edges(suspend.getProcesses().size(),
                                OBELISK_RT_WAIT_EDGE_NONE);
    SmallVector<Value> processes(suspend.getProcesses());
    return encodeWait(
        plan, suspend.getOperation(), suspend.getContinuationOperands(),
        OBELISK_RT_SUSPEND_JOIN,
        static_cast<obelisk_rt_wait_flags>(suspend.getKind()), edges,
        processes);
  }
  if (auto suspend = dyn_cast<sim::SimSuspendChildrenOp>(operation))
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(),
                      OBELISK_RT_SUSPEND_CHILDREN, OBELISK_RT_WAIT_FLAGS_NONE,
                      {}, {});
  if (auto suspend = dyn_cast<sim::SimSuspendObserveOp>(operation))
    return encodeObserverWait(plan, suspend);
  return std::nullopt;
}

} // namespace obelisk::bytecode
