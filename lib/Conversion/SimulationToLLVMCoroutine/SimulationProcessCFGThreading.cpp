//===- SimulationProcessCFGThreading.cpp - Thread process CFG state -------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Dominance.h"
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
  for (ControlRequirement &requirement : controlRequirements) {
    Block *definition = requirement.root.getParentBlock();
    SmallVector<Block *> worklist{requirement.boundary.getResume()};
    auto &reachable = resumeReachable[requirement.root];
    while (!worklist.empty()) {
      Block *block = worklist.pop_back_val();
      if (block == definition)
        continue;
      if (!reachable.insert(block).second)
        continue;
      llvm::append_range(worklist, block->getSuccessors());
    }
  }

  std::function<FailureOr<Value>(Value, Block *)> makeAvailable;
  makeAvailable = [&](Value root, Block *block) -> FailureOr<Value> {
    if (block == entry)
      return failure();
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
        if (successor == block)
          branch.getSuccessorOperands(index).append(*incoming);
    }
    return argument;
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
      FailureOr<Value> incoming = makeAvailable(requirement.root, predecessor);
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
    FailureOr<Value> available = makeAvailable(requirement.root, boundaryBlock);
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
        FailureOr<Value> incoming = makeAvailable(root, &predecessor);
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
    for (Block &block : function.getBody()) {
      if (&block == entry)
        continue;
      bool controlBoundaryBody =
          llvm::any_of(synchronousControlBodies, [&](Block *body) {
            return dominance.dominates(body, &block);
          });
      llvm::SetVector<Value> externalRoots;
      for (Operation &operation : block)
        for (Value value : operation.getOperands()) {
          if (value.getParentBlock() == &block)
            continue;
          if (auto argument = dyn_cast<BlockArgument>(value);
              argument && argument.getOwner() == entry)
            continue;
          externalRoots.insert(rootOf(value));
        }
      for (Value root : externalRoots) {
        auto &threaded = threadedValues[&block];
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
        Value dominating =
            dominance.dominates(root, &block.front()) ? root : Value{};
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
        if (controlBoundaryBody && dominating) {
          replaceExternalUses(dominating);
          continue;
        }
        auto existing = threaded.find(root);
        if (existing != threaded.end()) {
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
          Value incoming;
          Block *incomingBlock = nullptr;
          for (auto &[candidateBlock, candidates] : threadedValues) {
            auto found = candidates.find(root);
            if (found == candidates.end() ||
                !dominance.dominates(candidateBlock, predecessor))
              continue;
            if (!incomingBlock ||
                dominance.dominates(incomingBlock, candidateBlock)) {
              incoming = found->second;
              incomingBlock = candidateBlock;
            }
          }
          if (!incoming &&
              dominance.dominates(root, predecessor->getTerminator()))
            incoming = root;
          if (!incoming) {
            unavailable = true;
            break;
          }
          bool found = false;
          for (auto [index, successor] :
               llvm::enumerate(predecessor->getSuccessors())) {
            if (successor != &block)
              continue;
            incomingEdges.push_back(
                {branch, static_cast<unsigned>(index), incoming});
            found = true;
          }
          if (!found)
            return predecessor->getTerminator()->emitError(
                "predecessor is missing its CFG successor");
        }
        if (unavailable || incomingEdges.empty()) {
          if (!block.hasNoPredecessors())
            unresolvedUse = &block.front();
          continue;
        }

        BlockArgument argument =
            block.addArgument(root.getType(), root.getLoc());
        threaded.insert({root, argument});
        threadedRoots.try_emplace(argument, root);
        replaceExternalUses(argument);
        for (IncomingEdge &incoming : incomingEdges)
          incoming.branch.getSuccessorOperands(incoming.successorIndex)
              .append(incoming.value);
        changed = true;
      }
    }
    if (!changed && unresolvedUse)
      return unresolvedUse->emitError(
          "cannot reconstruct suspension-live state on every predecessor");
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
