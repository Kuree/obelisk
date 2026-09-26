//===- Utils.cpp - Shared simulation transformation helpers ------------===//

#include "Utils.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;

namespace obelisk::simlowering {

bool isSuspensionTerminator(Operation *op) {
  return getFragmentActionKind(op) != sim::ComputeActionKind::Continue &&
         !isa<sim::SimReturnOp>(op);
}

sim::ComputeActionKind getFragmentActionKind(Operation *terminator) {
  return llvm::TypeSwitch<Operation *, sim::ComputeActionKind>(terminator)
      .Case<sim::SimSuspendDelayOp>(
          [](auto) { return sim::ComputeActionKind::SuspendDelay; })
      .Case<sim::SimSuspendChangeOp>(
          [](auto) { return sim::ComputeActionKind::SuspendChange; })
      .Case<sim::SimSuspendEdgeOp>(
          [](auto) { return sim::ComputeActionKind::SuspendEdge; })
      .Case<sim::SimSuspendEdgeIffOp>(
          [](auto) { return sim::ComputeActionKind::SuspendEdge; })
      .Case<sim::SimSuspendLevelOp>(
          [](auto) { return sim::ComputeActionKind::SuspendChange; })
      .Case<sim::SimSuspendAnyOp>(
          [](auto) { return sim::ComputeActionKind::SuspendAny; })
      .Case<sim::SimSuspendEventOp>(
          [](auto) { return sim::ComputeActionKind::SuspendEvent; })
      .Case<sim::SimSuspendEventOrderOp>(
          [](auto) { return sim::ComputeActionKind::SuspendEvent; })
      .Case<sim::SimSuspendMailboxOp>(
          [](auto) { return sim::ComputeActionKind::SuspendMailbox; })
      .Case<sim::SimSuspendSemaphoreOp>(
          [](auto) { return sim::ComputeActionKind::SuspendSemaphore; })
      .Case<sim::SimSuspendForeverOp>(
          [](auto) { return sim::ComputeActionKind::SuspendAny; })
      .Case<sim::SimSuspendAwaitOp>(
          [](auto) { return sim::ComputeActionKind::SuspendAwait; })
      .Case<sim::SimSuspendJoinOp>(
          [](auto) { return sim::ComputeActionKind::SuspendJoin; })
      .Case<sim::SimSuspendChildrenOp>(
          [](auto) { return sim::ComputeActionKind::SuspendChildren; })
      .Case<sim::SimSuspendObserveOp>(
          [](auto) { return sim::ComputeActionKind::SuspendObserve; })
      .Case<sim::SimTaskCallOp, sim::SimClassVirtualTaskCallOp>(
          [](auto) { return sim::ComputeActionKind::TaskCall; })
      .Case<sim::SimProcessControlOp>(
          [](auto) { return sim::ComputeActionKind::ProcessControl; })
      .Case<sim::SimReturnOp>(
          [](auto) { return sim::ComputeActionKind::Terminate; })
      .Default([](Operation *) { return sim::ComputeActionKind::Continue; });
}

sim::ContinuationSiteAttr getContinuationSite(Operation *operation) {
  sim::ContinuationSiteAttr site;
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

void setContinuationSite(Operation *operation, sim::ContinuationSiteAttr site) {
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

bool isConstantTimeValue(Value value) {
  // A value is a compiled-calendar delay when every definition reaching it is
  // the same constant. Carrying an argument around a loop preserves, rather
  // than creates, that proof, so a self-reference contributes no definition.
  // Whether the proof holds must depend only on the definitions reached, never
  // on the order the worklist happens to visit them.
  SmallVector<Value> worklist{value};
  DenseSet<Value> visited;
  std::optional<APInt> constantValue;
  while (!worklist.empty()) {
    Value current = worklist.pop_back_val();
    if (!visited.insert(current).second)
      continue;
    if (auto constant = current.getDefiningOp<sim::SimTimeConstantOp>()) {
      APInt value = constant.getValueAttr().getValue();
      if (constantValue && *constantValue != value)
        return false;
      constantValue = value;
      continue;
    }
    auto argument = dyn_cast<BlockArgument>(current);
    if (!argument)
      return false;
    Block *block = argument.getOwner();
    if (block->isEntryBlock() || block->hasNoPredecessors())
      return false;
    for (Block *predecessor : block->getPredecessors()) {
      auto branch = dyn_cast<BranchOpInterface>(predecessor->getTerminator());
      if (!branch)
        return false;
      for (unsigned successor = 0;
           successor != predecessor->getTerminator()->getNumSuccessors();
           ++successor) {
        if (predecessor->getTerminator()->getSuccessor(successor) != block)
          continue;
        auto forwarded =
            branch.getSuccessorOperands(successor).getForwardedOperands();
        if (argument.getArgNumber() >= forwarded.size())
          return false;
        Value incoming = forwarded[argument.getArgNumber()];
        if (incoming != current)
          worklist.push_back(incoming);
      }
    }
  }
  // An argument defined only by itself reaches no constant at all.
  return constantValue.has_value();
}

} // namespace obelisk::simlowering
