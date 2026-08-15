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
  bool changed;
  do {
    changed = false;
    Operation *unresolvedUse = nullptr;
    for (Block &block : function.getBody()) {
      if (&block == entry)
        continue;
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
