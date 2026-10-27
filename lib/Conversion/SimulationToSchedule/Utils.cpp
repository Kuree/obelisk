//===- Utils.cpp - Shared simulation transformation helpers ------------===//

#include "talos/Conversion/SimulationToSchedule/Utils.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "talos/Dialect/Native/NativeDialect.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;

namespace talos::simlowering {

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


/// IEEE 1800-2023 4.3: a simulator may use any algorithm whose user-visible
/// effect is consistent with the reference algorithm. A process that only
/// reads state and waits for changes writes nothing, calls nothing, reports
/// nothing and schedules no future event, so erasing it changes no value, no
/// event order and no end of simulation. The VPI-off contract excludes the
/// external observer that could still see it. Such processes remain when
/// stores to write-only state are removed (SimplifyBodies and compute-fusion
/// materialization); left in place they stay scheduled on every input change
/// and count as observers of those inputs. SimplifyBodies and the compute
/// graph build call this after those removals.
unsigned removeEffectFreeProcesses(sim::SimDesignOp design) {
  // One walk over the design: the result-less spawns of each function, and
  // the functions anything else refers to. The design's own attributes hold
  // only the compute graph, which the caller rebuilds after a change.
  DenseMap<StringAttr, SmallVector<Operation *>> spawns;
  DenseSet<StringAttr> otherwiseUsed;
  design.walk([&](Operation *op) {
    if (op == design.getOperation())
      return;
    if (auto spawn = dyn_cast<sim::SimSpawnOp>(op);
        spawn && spawn.getResult().use_empty()) {
      spawns[spawn.getCalleeAttr().getAttr()].push_back(op);
      return;
    }
    for (NamedAttribute attr : op->getAttrs())
      attr.getValue().walk([&](SymbolRefAttr ref) {
        otherwiseUsed.insert(ref.getRootReference());
        for (FlatSymbolRefAttr nested : ref.getNestedReferences())
          otherwiseUsed.insert(nested.getAttr());
      });
  });
  SmallVector<sim::SimFuncOp> dead;
  unsigned pinned = 0;
  for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
    switch (function.getEntryKind()) {
    case sim::EntryKind::Always:
    case sim::EntryKind::AlwaysComb:
    case sim::EntryKind::AlwaysLatch:
    case sim::EntryKind::Continuous:
    case sim::EntryKind::PortInput:
    case sim::EntryKind::PortOutput:
      break;
    default:
      continue;
    }
    if (function.isExternal())
      continue;
    bool effectFree =
        !function.getBody()
             .walk([&](Operation *op) {
               if (isa<cf::BranchOp, cf::CondBranchOp, sim::SimReturnOp,
                       sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
                       sim::SimSuspendAnyOp, sim::SimRefLoadOp,
                       sim::SimNetReadOp>(op))
                 return WalkResult::advance();
               return op->getNumRegions() == 0 && isMemoryEffectFree(op)
                          ? WalkResult::advance()
                          : WalkResult::interrupt();
             })
             .wasInterrupted();
    if (!effectFree)
      continue;
    if (otherwiseUsed.contains(function.getSymNameAttr())) {
      ++pinned;
      continue;
    }
    dead.push_back(function);
  }
  for (auto function : dead) {
    for (Operation *spawn : spawns.lookup(function.getSymNameAttr()))
      spawn->erase();
    function.erase();
  }
  if (auto module = design->getParentOfType<ModuleOp>();
      module && native::has<native::DebugTiming>(module))
    llvm::errs() << "talos effect-free processes: removed=" << dead.size()
                 << " pinned=" << pinned << '\n';
  return dead.size();
}
} // namespace talos::simlowering
