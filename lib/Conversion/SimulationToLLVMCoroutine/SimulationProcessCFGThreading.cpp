//===- SimulationProcessCFGThreading.cpp - Thread process CFG state -------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMTHREADPROCESSCFGPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace detail {

LogicalResult threadProcessStateThroughCFG(sim::SimFuncOp function) {
  if (function.getBody().empty())
    return success();
  // Zero-time callable and observer entries never leave their interpreter or
  // native call frame. In particular, process.control transfers synchronously
  // to its successor and deliberately has no canonical-frame operands.
  if (function.getEntryKind() == sim::EntryKind::Function ||
      function.getEntryKind() == sim::EntryKind::Observer)
    return success();
  Block *entry = &function.getBody().front();

  // IEEE 1800-2017 31.7 condition observers are static descriptors consumed
  // by their clock suspension. Native/AOT fragment extraction can hoist a
  // descriptor back to an entry block after front-end suspension threading;
  // rematerialize it at each clock_set so CFG threading never gives it a
  // canonical-frame lane.
  SmallVector<sim::SimSuspendClockSetOp> clockSets;
  function.walk(
      [&](sim::SimSuspendClockSetOp clocks) { clockSets.push_back(clocks); });
  for (sim::SimSuspendClockSetOp clocks : clockSets) {
    IRRewriter rewriter(function.getContext());
    rewriter.setInsertionPoint(clocks);
    DenseMap<Operation *, sim::SimObserverBindOp> clones;
    for (OpOperand &operand : clocks->getOpOperands()) {
      auto binding = operand.get().getDefiningOp<sim::SimObserverBindOp>();
      if (!binding || binding->getBlock() == clocks->getBlock())
        continue;
      auto [entry, inserted] = clones.try_emplace(binding.getOperation());
      if (inserted)
        entry->second = cast<sim::SimObserverBindOp>(rewriter.clone(*binding));
      operand.set(entry->second.getResult());
      if (binding.getResult().use_empty())
        rewriter.eraseOp(binding);
    }
  }

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
  DenseMap<Value, Value> threadedRoots;
  auto rootOf = [&](Value value) {
    while (Value root = threadedRoots.lookup(value))
      value = root;
    return value;
  };
  // Suspension lowering may already have made some values explicit successor
  // operands. Record those lanes before finding external uses so a value does
  // not acquire a second continuation argument when it is also live through a
  // later ordinary CFG edge.
  // Resolve these roots to a fixed point. A loop header can receive the
  // original value on its entry edge and a restored continuation argument on
  // its backedge; the restored argument itself may be declared later in the
  // region. Both lanes still represent the same semantic value.
  bool discoveredRoot;
  do {
    discoveredRoot = false;
    for (Block &block : llvm::drop_begin(function.getBody())) {
      for (auto [argumentIndex, argument] :
           llvm::enumerate(block.getArguments())) {
        if (threadedRoots.count(argument))
          continue;
        Value commonRoot;
        bool commonIncoming = true;
        bool sawIncoming = false;
        for (Block &predecessor : function.getBody()) {
          auto branch =
              dyn_cast<BranchOpInterface>(predecessor.getTerminator());
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
            Value root = rootOf(operands[argumentIndex]);
            // A loop-carried argument can feed itself on a backedge. That
            // edge adds no root information; use the concrete entry or
            // continuation edge to identify the semantic value instead.
            if (root == argument)
              continue;
            if (!sawIncoming) {
              commonRoot = root;
              sawIncoming = true;
            } else if (commonRoot != root) {
              commonIncoming = false;
              break;
            }
          }
          if (!commonIncoming)
            break;
        }
        if (!sawIncoming || !commonIncoming)
          continue;
        threadedRoots.try_emplace(argument, commonRoot);
        threadedValues[&block].try_emplace(commonRoot, argument);
        discoveredRoot = true;
      }
    }
  } while (discoveredRoot);

  // A continuation operand is restored from the canonical process frame and
  // therefore starts a second dynamic path for its semantic root. Ordinary
  // dominance does not model that path: after coroutine splitting, it may
  // reach a loop exit without executing the original SSA definition again.
  // Remember the direct continuation blocks now, before later threading adds
  // ordinary phi-like arguments that must not themselves be treated as frame
  // restores.
  DenseMap<Value, llvm::SmallPtrSet<Block *, 4>> restoredRoots;
  for (Block &predecessor : function.getBody()) {
    Operation *terminator = predecessor.getTerminator();
    if (!isa<sim::SimTaskCallOp, sim::SimClassVirtualTaskCallOp,
             sim::SimProcessControlOp, sim::SimSuspendDelayOp,
             sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
             sim::SimSuspendEdgeIffOp, sim::SimSuspendLevelOp,
             sim::SimSuspendAnyOp, sim::SimSuspendClockSetOp,
             sim::SimSuspendEventOp, sim::SimSuspendEventOrderOp,
             sim::SimSuspendMailboxOp, sim::SimSuspendSemaphoreOp,
             sim::SimSuspendObserveOp, sim::SimSuspendAwaitOp,
             sim::SimSuspendJoinOp, sim::SimSuspendChildrenOp>(terminator))
      continue;
    auto branch = cast<BranchOpInterface>(terminator);
    for (auto [successorIndex, successor] :
         llvm::enumerate(predecessor.getSuccessors())) {
      SuccessorOperands operands = branch.getSuccessorOperands(successorIndex);
      for (auto [argumentIndex, argument] :
           llvm::enumerate(successor->getArguments())) {
        if (argumentIndex >= operands.size() ||
            operands.isOperandProduced(argumentIndex))
          continue;
        Value root = rootOf(argument);
        if (root && rootOf(operands[argumentIndex]) == root)
          restoredRoots[root].insert(successor);
      }
    }
  }

  DominanceInfo dominance(function);

  // A control boundary has a scheduler resume edge that is deliberately not
  // represented in the ordinary CFG: after a remote disable the coroutine is
  // entered directly at the boundary's resume successor.  Find values used by
  // the synchronous body that can be reached again from that hidden edge and
  // make them explicit resume operands.  Without this step ordinary dominance
  // incorrectly says that a loop-carried value is available after resumption.
  struct ControlRequirement {
    sim::SimControlBoundaryOp boundary;
    Value root;
    bool seededResume;
  };
  SmallVector<ControlRequirement> controlRequirements;
  DenseSet<Value> controlRoots;
  auto reachesWithoutRedefinition = [&](Block *start, Block *target,
                                        Block *definition) {
    SmallVector<Block *> worklist{start};
    llvm::SmallPtrSet<Block *, 16> visited;
    while (!worklist.empty()) {
      Block *block = worklist.pop_back_val();
      if (!visited.insert(block).second)
        continue;
      if (block == target)
        return true;
      if (block == definition)
        continue;
      llvm::append_range(worklist, block->getSuccessors());
    }
    return false;
  };
  function.walk([&](sim::SimControlBoundaryOp boundary) {
    Block *body = boundary.getBody();
    llvm::SetVector<Value> roots;
    for (Block &block : function.getBody()) {
      if (!dominance.dominates(body, &block))
        continue;
      for (Operation &operation : block)
        for (Value value : operation.getOperands()) {
          if (value.getParentBlock() == &block)
            continue;
          Value root = rootOf(value);
          Block *definition = root.getParentBlock();
          if (!definition || definition == entry ||
              !dominance.dominates(root, boundary.getOperation()) ||
              !reachesWithoutRedefinition(boundary.getResume(), &block,
                                          definition))
            continue;
          roots.insert(root);
        }
    }
    for (Value root : roots) {
      auto &resumeValues = threadedValues[boundary.getResume()];
      bool seededResume = false;
      if (!resumeValues.count(root)) {
        boundary.getResumeOperandsMutable().append(root);
        BlockArgument argument =
            boundary.getResume()->addArgument(root.getType(), root.getLoc());
        resumeValues.insert({root, argument});
        threadedRoots.try_emplace(argument, root);
        seededResume = true;
      }
      controlRequirements.push_back({boundary, root, seededResume});
      controlRoots.insert(root);
    }
  });

  // Return the value of a semantic root available at a block under the
  // extended CFG above.  Phi-like block arguments are created lazily only at
  // reconvergence points between ordinary execution and a restored resume
  // lane, keeping both the canonical frame and native CFG compact.
  DenseMap<Value, llvm::SmallPtrSet<Block *, 16>> resumeReachable;
  auto markRestoredReachability = [&](Value root, Block *start) {
    Block *definition = root.getParentBlock();
    SmallVector<Block *> worklist{start};
    auto &reachable = resumeReachable[root];
    while (!worklist.empty()) {
      Block *block = worklist.pop_back_val();
      if (block == definition || !reachable.insert(block).second)
        continue;
      llvm::append_range(worklist, block->getSuccessors());
    }
  };
  for (ControlRequirement &requirement : controlRequirements) {
    markRestoredReachability(requirement.root,
                             requirement.boundary.getResume());
  }
  for (auto &[root, starts] : restoredRoots)
    for (Block *start : starts)
      markRestoredReachability(root, start);

  std::function<FailureOr<Value>(Value, Block *)> makeAvailable;
  struct CreatedThreadArgument {
    Block *block;
    Value root;
    BlockArgument argument;
  };
  struct AppendedThreadOperand {
    Operation *terminator;
    unsigned successorIndex;
  };
  SmallVector<CreatedThreadArgument> createdThreadArguments;
  SmallVector<AppendedThreadOperand> appendedThreadOperands;
  makeAvailable = [&](Value root, Block *block) -> FailureOr<Value> {
    if (block == entry)
      return dominance.dominates(root, block->getTerminator())
                 ? FailureOr<Value>(root)
                 : FailureOr<Value>(failure());
    auto existing = threadedValues[block].find(root);
    if (existing != threadedValues[block].end())
      return existing->second;

    bool bypassed = resumeReachable[root].contains(block);
    if (!bypassed && dominance.dominates(root, &block->front()))
      return root;

    // A boundary body cannot take successor operands.  Resolve the value at
    // the boundary itself; that value dominates the synchronous body edge.
    auto predecessors = block->getPredecessors();
    if (predecessors.begin() != predecessors.end() &&
        std::next(predecessors.begin()) == predecessors.end()) {
      Block *predecessor = *predecessors.begin();
      if (auto boundary =
              dyn_cast<sim::SimControlBoundaryOp>(predecessor->getTerminator());
          boundary && boundary.getBody() == block)
        return makeAvailable(root, predecessor);
    }

    BlockArgument argument = block->addArgument(root.getType(), root.getLoc());
    threadedValues[block].insert({root, argument});
    threadedRoots.try_emplace(argument, root);
    createdThreadArguments.push_back({block, root, argument});

    llvm::SmallPtrSet<Block *, 4> seenPredecessors;
    for (Block *predecessor : block->getPredecessors()) {
      if (!seenPredecessors.insert(predecessor).second)
        continue;
      auto branch = dyn_cast<BranchOpInterface>(predecessor->getTerminator());
      if (!branch)
        return failure();
      FailureOr<Value> incoming = makeAvailable(root, predecessor);
      if (failed(incoming))
        return failure();
      for (auto [index, successor] :
           llvm::enumerate(predecessor->getSuccessors()))
        if (successor == block) {
          branch.getSuccessorOperands(index).append(*incoming);
          appendedThreadOperands.push_back(
              {predecessor->getTerminator(), static_cast<unsigned>(index)});
        }
    }
    return argument;
  };

  // Recursive reconstruction creates phi-like lanes eagerly to break CFG
  // cycles. If a later predecessor proves unavailable, roll the entire
  // speculative subgraph back; otherwise abandoned block arguments accumulate
  // without corresponding successor operands and invalidate unrelated loops.
  auto tryMakeAvailable = [&](Value root, Block *block) -> FailureOr<Value> {
    size_t argumentCheckpoint = createdThreadArguments.size();
    size_t operandCheckpoint = appendedThreadOperands.size();
    FailureOr<Value> result = makeAvailable(root, block);
    if (succeeded(result))
      return result;
    while (appendedThreadOperands.size() != operandCheckpoint) {
      AppendedThreadOperand appended = appendedThreadOperands.pop_back_val();
      auto branch = cast<BranchOpInterface>(appended.terminator);
      SuccessorOperands operands =
          branch.getSuccessorOperands(appended.successorIndex);
      operands.erase(operands.size() - 1);
    }
    while (createdThreadArguments.size() != argumentCheckpoint) {
      CreatedThreadArgument created = createdThreadArguments.pop_back_val();
      threadedValues[created.block].erase(created.root);
      threadedRoots.erase(created.argument);
      created.block->eraseArgument(created.argument.getArgNumber());
    }
    return failure();
  };

  // The resume successor can also have ordinary local-control predecessors.
  // They reach the same block without going through control.boundary, so add
  // the newly seeded lane to those explicit branch edges as well.
  for (ControlRequirement &requirement : controlRequirements) {
    if (!requirement.seededResume)
      continue;
    Block *resume = requirement.boundary.getResume();
    llvm::SmallPtrSet<Block *, 4> seenPredecessors;
    for (Block *predecessor : resume->getPredecessors()) {
      if (!seenPredecessors.insert(predecessor).second ||
          predecessor == requirement.boundary->getBlock())
        continue;
      auto branch = dyn_cast<BranchOpInterface>(predecessor->getTerminator());
      if (!branch)
        return predecessor->getTerminator()->emitError(
            "cannot thread control-resume state through a non-branch "
            "terminator");
      FailureOr<Value> incoming =
          tryMakeAvailable(requirement.root, predecessor);
      if (failed(incoming))
        return predecessor->getTerminator()->emitError(
            "cannot reconstruct control-resume state on this predecessor");
      for (auto [index, successor] :
           llvm::enumerate(predecessor->getSuccessors()))
        if (successor == resume)
          branch.getSuccessorOperands(index).append(*incoming);
    }
  }

  for (ControlRequirement &requirement : controlRequirements) {
    Block *boundaryBlock = requirement.boundary->getBlock();
    if (!resumeReachable[requirement.root].contains(boundaryBlock))
      continue;
    FailureOr<Value> available =
        tryMakeAvailable(requirement.root, boundaryBlock);
    if (failed(available))
      return requirement.boundary.emitOpError(
          "cannot reconstruct control-resume state on every predecessor");
    Block *body = requirement.boundary.getBody();
    for (Block &block : function.getBody()) {
      if (!dominance.dominates(body, &block))
        continue;
      for (Operation &operation : block)
        for (OpOperand &operand : operation.getOpOperands())
          if (operand.get().getParentBlock() != &block &&
              rootOf(operand.get()) == requirement.root)
            operand.set(*available);
    }
  }

  // Recursive reconstruction can discover equivalent roots in a different
  // order on adjacent blocks.  Reassociate every threaded successor lane by
  // semantic root so operand order always follows the target block arguments.
  for (Block &predecessor : function.getBody()) {
    auto branch = dyn_cast<BranchOpInterface>(predecessor.getTerminator());
    if (!branch)
      continue;
    for (auto [successorIndex, successor] :
         llvm::enumerate(predecessor.getSuccessors())) {
      SuccessorOperands operands = branch.getSuccessorOperands(successorIndex);
      for (auto [argumentIndex, argument] :
           llvm::enumerate(successor->getArguments())) {
        Value root = threadedRoots.lookup(argument);
        if (!root || !controlRoots.contains(root) ||
            argumentIndex >= operands.size() ||
            operands.isOperandProduced(argumentIndex))
          continue;
        FailureOr<Value> incoming = tryMakeAvailable(root, &predecessor);
        if (failed(incoming))
          return predecessor.getTerminator()->emitError(
              "cannot normalize threaded control-resume state");
        operands.slice(argumentIndex, 1).assign(*incoming);
      }
    }
  }

  SmallVector<Block *> synchronousControlBodies;
  function.walk([&](sim::SimControlBoundaryOp boundary) {
    synchronousControlBodies.push_back(boundary.getBody());
  });
  bool changed;
  do {
    changed = false;
    Operation *unresolvedUse = nullptr;
    Value unresolvedRoot;
    for (Block &block : function.getBody()) {
      if (&block == entry)
        continue;
      bool controlBoundaryBody =
          llvm::any_of(synchronousControlBodies, [&](Block *body) {
            return dominance.dominates(body, &block);
          });
      bool controlBoundaryEntryBody =
          llvm::is_contained(synchronousControlBodies, &block);
      llvm::SetVector<Value> externalRoots;
      DenseMap<Value, Value> rematerializedConstants;
      DenseSet<Value> nonRematerializableConstants;
      OpBuilder rematerializationBuilder(&block, block.begin());
      std::function<Value(Value)> rematerializeConstantExpression =
          [&](Value value) -> Value {
        if (Value replacement = rematerializedConstants.lookup(value))
          return replacement;
        if (nonRematerializableConstants.contains(value))
          return {};
        auto result = dyn_cast<OpResult>(value);
        Operation *definition = result ? result.getOwner() : nullptr;
        if (!definition || definition->getNumRegions() != 0 ||
            !isMemoryEffectFree(definition) || !isSpeculatable(definition)) {
          nonRematerializableConstants.insert(value);
          return {};
        }
        IRMapping mapping;
        for (Value operand : definition->getOperands()) {
          Value replacement = rematerializeConstantExpression(operand);
          if (!replacement) {
            nonRematerializableConstants.insert(value);
            return {};
          }
          mapping.map(operand, replacement);
        }
        Operation *clone = rematerializationBuilder.clone(*definition, mapping);
        for (auto [original, replacement] :
             llvm::zip_equal(definition->getResults(), clone->getResults()))
          rematerializedConstants.try_emplace(original, replacement);
        return rematerializedConstants.lookup(value);
      };
      for (Operation &operation : block)
        for (OpOperand &operand : operation.getOpOperands()) {
          Value value = operand.get();
          if (value.getParentBlock() == &block)
            continue;
          if (auto argument = dyn_cast<BlockArgument>(value);
              argument && argument.getOwner() == entry)
            continue;
          if (Value replacement = rematerializeConstantExpression(value)) {
            operand.set(replacement);
            continue;
          }
          externalRoots.insert(rootOf(value));
        }
      for (Value root : externalRoots) {
        auto replaceExternalUses = [&](Value replacement) {
          for (Operation &operation : block)
            for (OpOperand &operand : operation.getOpOperands()) {
              Value value = operand.get();
              if (value.getParentBlock() != &block && rootOf(value) == root)
                operand.set(replacement);
            }
        };
        // A control boundary's body edge and every block it dominates execute
        // synchronously. Prefer the closest restored lane when the hidden
        // resume edge can reach this body; otherwise ordinary SSA dominance
        // is sufficient and no frame slot is needed.
        bool restoredPath = resumeReachable[root].contains(&block);
        Value dominating =
            !restoredPath && dominance.dominates(root, &block.front())
                ? root
                : Value{};
        Block *dominatingBlock = dominating ? root.getParentBlock() : nullptr;
        for (auto &[candidateBlock, candidates] : threadedValues) {
          auto found = candidates.find(root);
          if (found == candidates.end() ||
              !dominance.dominates(candidateBlock, &block))
            continue;
          if (!dominatingBlock ||
              dominance.dominates(dominatingBlock, candidateBlock)) {
            dominating = found->second;
            dominatingBlock = candidateBlock;
          }
        }
        if (controlBoundaryBody && dominating &&
            (!restoredPath || controlBoundaryEntryBody)) {
          replaceExternalUses(dominating);
          continue;
        }
        auto existing = threadedValues[&block].find(root);
        if (existing != threadedValues[&block].end()) {
          replaceExternalUses(existing->second);
          continue;
        }

        // Block::getPredecessors() visits predecessor edges, so a cond_br with
        // both destinations equal yields the same block twice. Update every
        // successor edge during one visit to each predecessor; otherwise each
        // edge receives the threaded operand twice.
        struct IncomingEdge {
          BranchOpInterface branch;
          unsigned successorIndex;
          Value value;
        };
        SmallVector<IncomingEdge> incomingEdges;
        bool unavailable = false;
        llvm::SmallPtrSet<Block *, 4> seenPredecessors;
        for (Block *predecessor : block.getPredecessors()) {
          if (!seenPredecessors.insert(predecessor).second)
            continue;
          auto branch =
              dyn_cast<BranchOpInterface>(predecessor->getTerminator());
          if (!branch)
            return predecessor->getTerminator()->emitError(
                "cannot thread suspension-live state through a non-branch "
                "terminator");
          FailureOr<Value> incoming =
              !resumeReachable[root].contains(predecessor) &&
                      dominance.dominates(root, predecessor->getTerminator())
                  ? FailureOr<Value>(root)
                  : tryMakeAvailable(root, predecessor);
          if (failed(incoming)) {
            unavailable = true;
            break;
          }
          bool found = false;
          for (auto [index, successor] :
               llvm::enumerate(predecessor->getSuccessors())) {
            if (successor != &block)
              continue;
            incomingEdges.push_back(
                {branch, static_cast<unsigned>(index), *incoming});
            found = true;
          }
          if (!found)
            return predecessor->getTerminator()->emitError(
                "predecessor is missing its CFG successor");
        }
        if (unavailable || incomingEdges.empty()) {
          if (!block.hasNoPredecessors()) {
            unresolvedUse = &block.front();
            unresolvedRoot = root;
          }
          continue;
        }

        BlockArgument argument =
            block.addArgument(root.getType(), root.getLoc());
        // `tryMakeAvailable` above may insert entries for predecessor blocks
        // and rehash the outer map. Do not retain a reference to this inner
        // map across that recursion.
        threadedValues[&block].insert({root, argument});
        threadedRoots.try_emplace(argument, root);
        replaceExternalUses(argument);
        for (IncomingEdge &incoming : incomingEdges)
          incoming.branch.getSuccessorOperands(incoming.successorIndex)
              .append(incoming.value);
        changed = true;
      }
    }
    if (!changed && unresolvedUse) {
      InFlightDiagnostic diagnostic = unresolvedUse->emitError(
          "cannot reconstruct suspension-live state on every predecessor");
      if (unresolvedRoot)
        diagnostic << " for " << unresolvedRoot;
      return failure();
    }
  } while (changed);
  return success();
}

} // namespace detail

namespace {

class ObeliskSimThreadProcessCFGPass
    : public impl::ObeliskSimThreadProcessCFGPassBase<
          ObeliskSimThreadProcessCFGPass> {
public:
  void runOnOperation() override {
    if (failed(detail::threadProcessStateThroughCFG(getOperation())))
      signalPassFailure();
  }
};

} // namespace
} // namespace obelisk
