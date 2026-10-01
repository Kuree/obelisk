//===- Utils.cpp - Shared simulation transformation helpers ------------===//

#include "obelisk/Conversion/SimulationToSchedule/Utils.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;

namespace obelisk::simlowering {

bool isSuspensionTerminator(Operation *op) {
  return getFragmentActionKind(op) != schedule::ComputeActionKind::Continue &&
         !isa<sim::SimReturnOp>(op);
}

schedule::ComputeActionKind getFragmentActionKind(Operation *terminator) {
  return llvm::TypeSwitch<Operation *, schedule::ComputeActionKind>(terminator)
      .Case<sim::SimSuspendDelayOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendDelay; })
      .Case<sim::SimSuspendChangeOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendChange; })
      .Case<sim::SimSuspendEdgeOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendEdge; })
      .Case<sim::SimSuspendEdgeIffOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendEdge; })
      .Case<sim::SimSuspendLevelOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendChange; })
      .Case<sim::SimSuspendAnyOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendAny; })
      .Case<sim::SimSuspendEventOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendEvent; })
      .Case<sim::SimSuspendEventOrderOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendEvent; })
      .Case<sim::SimSuspendMailboxOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendMailbox; })
      .Case<sim::SimSuspendSemaphoreOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendSemaphore; })
      .Case<sim::SimSuspendForeverOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendAny; })
      .Case<sim::SimSuspendAwaitOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendAwait; })
      .Case<sim::SimSuspendJoinOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendJoin; })
      .Case<sim::SimSuspendChildrenOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendChildren; })
      .Case<sim::SimSuspendObserveOp>(
          [](auto) { return schedule::ComputeActionKind::SuspendObserve; })
      .Case<sim::SimTaskCallOp, sim::SimClassVirtualTaskCallOp>(
          [](auto) { return schedule::ComputeActionKind::TaskCall; })
      .Case<sim::SimProcessControlOp>(
          [](auto) { return schedule::ComputeActionKind::ProcessControl; })
      .Case<sim::SimReturnOp>(
          [](auto) { return schedule::ComputeActionKind::Terminate; })
      .Default(
          [](Operation *) { return schedule::ComputeActionKind::Continue; });
}

schedule::ContinuationSiteAttr getContinuationSite(Operation *operation) {
  schedule::ContinuationSiteAttr site;
  llvm::TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
            sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
            sim::SimSuspendLevelOp, sim::SimSuspendAnyOp,
            sim::SimSuspendEventOp, sim::SimSuspendEventOrderOp,
            sim::SimSuspendMailboxOp, sim::SimSuspendSemaphoreOp,
            sim::SimSuspendObserveOp, sim::SimSuspendForeverOp,
            sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp,
            sim::SimSuspendChildrenOp, sim::SimTaskCallOp,
            sim::SimClassVirtualTaskCallOp, sim::SimProcessControlOp>(
          [&](auto op) { site = op.getSiteAttr(); });
  return site;
}

void setContinuationSite(Operation *operation,
                         schedule::ContinuationSiteAttr site) {
  llvm::TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
            sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
            sim::SimSuspendLevelOp, sim::SimSuspendAnyOp,
            sim::SimSuspendEventOp, sim::SimSuspendEventOrderOp,
            sim::SimSuspendMailboxOp, sim::SimSuspendSemaphoreOp,
            sim::SimSuspendObserveOp, sim::SimSuspendForeverOp,
            sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp,
            sim::SimSuspendChildrenOp, sim::SimTaskCallOp,
            sim::SimClassVirtualTaskCallOp, sim::SimProcessControlOp>(
          [&](auto op) { op.setSiteAttr(site); });
}

ReexecutingBlockSet getReexecutingBlocks(sim::SimFuncOp function) {
  SmallVector<Block *> blocks;
  DenseMap<Block *, SmallVector<Block *>> successors;
  for (Block &block : function.getBody()) {
    blocks.push_back(&block);
    successors.try_emplace(&block, block.getTerminator()->getSuccessors());
  }

  ReexecutingBlockSet reexecuting;
  for (ArrayRef<Block *> component :
       computeStronglyConnectedComponents<Block *>(blocks, successors)) {
    // A single-block component only re-executes when it branches to itself.
    bool cyclic = component.size() > 1 ||
                  llvm::is_contained(successors.lookup(component.front()),
                                     component.front());
    if (cyclic)
      reexecuting.insert(component.begin(), component.end());
  }
  return reexecuting;
}


} // namespace obelisk::simlowering
