//===- ThreadSuspension.cpp - Make live state explicit at suspensions ---===//
//
// After promotion, values that stay live across a suspension are ordinary SSA
// values defined before the suspending terminator. A continuation resumes on a
// different scheduler activation, so every such value must arrive as an
// explicit continuation operand rather than by dominance.
//
//===----------------------------------------------------------------------===//

#include "Detail.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"

#include "mlir/Analysis/Liveness.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <optional>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMTHREADSUSPENSIONPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using namespace obelisk::simlowering;

class ObeliskSimThreadSuspensionPass
    : public impl::ObeliskSimThreadSuspensionPassBase<
          ObeliskSimThreadSuspensionPass> {
public:
  void runOnOperation() override {
    sim::SimFuncOp function = getOperation();
    if (function.getBody().empty())
      return;

    // Constants need not occupy canonical-frame slots. In particular, byte
    // strings cannot be persisted because their representation contains a
    // native pointer. Rematerializing every cross-block constant use also
    // avoids carrying scalar constants through loop continuations: a resumed
    // bytecode activation initializes only its explicit continuation inputs.
    //
    // Mark these clones so compute-graph cost remains invariant when a graph
    // was built before this pass and verified afterward.
    SmallVector<OpResult> constants;
    function.walk([&](Operation *op) {
      if (!op->hasTrait<OpTrait::ConstantLike>() || op->getNumOperands() != 0 ||
          op->getNumResults() != 1)
        return;
      constants.push_back(cast<OpResult>(op->getResult(0)));
    });
    for (OpResult constant : constants) {
      Operation *definition = constant.getOwner();
      for (OpOperand &use :
           llvm::make_early_inc_range(constant.getUses())) {
        Operation *consumer = use.getOwner();
        if (consumer->getBlock() == definition->getBlock())
          continue;
        OpBuilder builder(consumer);
        Operation *clone = builder.clone(*definition);
        clone->setAttr("obelisk_sim.rematerialized", builder.getUnitAttr());
        use.set(clone->getResult(0));
      }
    }

    SmallVector<Operation *> suspensions;
    function.walk([&](Operation *op) {
      if (isSuspensionTerminator(op))
        suspensions.push_back(op);
    });
    if (suspensions.empty())
      return;

    // Observer callbacks retain captured automatic references while the
    // process is suspended even when the controlled statement never reads
    // them after resumption. Route only the suspension edge through a private
    // bridge which owns those handles until resume. Appending them directly to
    // a shared continuation would require fabricating non-dominating values
    // on unrelated predecessors.
    for (Operation *suspension : suspensions) {
      auto observe = dyn_cast<sim::SimSuspendObserveOp>(suspension);
      if (!observe)
        continue;
      Liveness liveness(function);
      const auto &liveOut = liveness.getLiveOut(suspension->getBlock());
      llvm::SetVector<Value> retained;
      SmallVector<Value> observerValues(observe.getPrimaries());
      llvm::append_range(observerValues, observe.getConditions());
      for (Value observer : observerValues) {
        auto binding = observer.getDefiningOp<sim::SimObserverBindOp>();
        if (!binding)
          continue;
        for (Value capture : binding.getCaptures())
          // A live capture is threaded through the ordinary continuation
          // operands below, which both preserves its value and pins its
          // automatic activation. The private bridge is only needed for
          // captures whose sole remaining owner is the observer callback.
          if (isa<sim::RefType>(capture.getType()) &&
              !liveOut.contains(capture))
            retained.insert(capture);
      }
      auto branch = cast<BranchOpInterface>(suspension);
      SuccessorOperands successorOperands = branch.getSuccessorOperands(0);
      SmallVector<Value> forwarded(
          successorOperands.getForwardedOperands().begin(),
          successorOperands.getForwardedOperands().end());
      for (Value value : forwarded)
        retained.remove(value);
      if (retained.empty())
        continue;

      Block *continuation = suspension->getSuccessor(0);
      auto *resume = new Block;
      function.getBody().getBlocks().insert(Region::iterator(continuation),
                                            resume);
      for (Value value : forwarded)
        resume->addArgument(value.getType(), suspension->getLoc());
      for (Value value : retained)
        resume->addArgument(value.getType(), suspension->getLoc());
      successorOperands.getMutableForwardedOperands().append(
          retained.getArrayRef());
      suspension->setSuccessor(resume, 0);
      OpBuilder builder(resume, resume->begin());
      auto bridge = cf::BranchOp::create(
          builder, suspension->getLoc(), continuation,
          resume->getArguments().take_front(forwarded.size()));
      bridge->setAttr("obelisk_sim.observer_capture_bridge",
                      builder.getUnitAttr());
    }

    // Apart from graph-transparent observer-capture bridges, threading only
    // adds block arguments and successor operands. Liveness is not invariant:
    // threading a value across one suspension replaces its later uses with the
    // new continuation argument, and that argument is what the next suspension
    // in the chain has to forward.
    DominanceInfo dominance(function);
    Block &entry = function.getBody().front();
    DenseMap<Block *, DenseMap<Value, BlockArgument>> threadedValues;
    DenseMap<Value, Value> threadedRoots;
    DenseMap<Block *, DenseSet<Value>> suspensionReentryRoots;
    auto markSuspensionReentry = [&](Operation *suspension, Block *continuation,
                                     Value root) {
      for (Block *predecessor : suspension->getBlock()->getPredecessors())
        if (dominance.dominates(continuation, predecessor)) {
          suspensionReentryRoots[suspension->getBlock()].insert(root);
          break;
        }
    };

    // Lowering may have already made some values explicit on a suspension
    // edge. Record those lanes before discovering additional live values.
    // Otherwise later CFG splits can keep using the pre-suspension SSA value
    // even though a resumed bytecode activation only initializes the
    // continuation argument.
    for (Operation *suspension : suspensions) {
      auto branch = cast<BranchOpInterface>(suspension);
      Block *continuation = suspension->getSuccessor(0);
      SuccessorOperands successorOperands = branch.getSuccessorOperands(0);
      ValueRange forwarded = successorOperands.getForwardedOperands();
      unsigned produced = successorOperands.getProducedOperandCount();
      if (produced > continuation->getNumArguments() ||
          forwarded.size() != continuation->getNumArguments() - produced) {
        suspension->emitError(
            "continuation operands do not match continuation arguments");
        signalPassFailure();
        return;
      }
      for (auto [value, argument] : llvm::zip_equal(
               forwarded, continuation->getArguments().drop_front(produced))) {
        Value root = threadedRoots.lookup(value);
        if (!root)
          root = value;
        markSuspensionReentry(suspension, continuation, root);
        threadedValues[continuation].try_emplace(root, argument);
        threadedRoots.try_emplace(argument, root);
        value.replaceUsesWithIf(argument, [&](OpOperand &use) {
          return dominance.dominates(continuation, use.getOwner()->getBlock());
        });
      }
    }

    // Threading is what invalidates liveness and the candidate list: it adds
    // continuation arguments and rewrites later uses onto them. Suspensions
    // that thread nothing leave both intact, so recompute on mutation instead
    // of per suspension — a process with many suspension points would
    // otherwise pay a whole-function walk for each one.
    std::optional<Liveness> liveness;
    SmallVector<Value> orderedValues;
    for (Operation *suspension : suspensions) {
      if (!liveness) {
        liveness.emplace(function);
        orderedValues.clear();
        for (Block &block : function.getBody()) {
          llvm::append_range(orderedValues, block.getArguments());
          for (Operation &op : block)
            llvm::append_range(orderedValues, op.getResults());
        }
      }
      const auto &liveOut = liveness->getLiveOut(suspension->getBlock());

      auto branch = cast<BranchOpInterface>(suspension);
      Block *continuation = suspension->getSuccessor(0);

      DenseSet<Value> alreadyForwarded;
      for (Value value : branch.getSuccessorOperands(0).getForwardedOperands())
        alreadyForwarded.insert(value);

      SmallVector<Value> toThread;
      for (Value value : orderedValues) {
        if (!liveOut.contains(value) || alreadyForwarded.contains(value) ||
            !dominance.dominates(value, suspension))
          continue;
        // Entry arguments are the process captures, which the scheduler
        // re-supplies on every activation.
        if (auto argument = dyn_cast<BlockArgument>(value);
            argument && argument.getOwner() == &entry)
          continue;

        toThread.push_back(value);
      }

      for (Value value : toThread) {
        struct IncomingEdge {
          BranchOpInterface branch;
          unsigned successorIndex;
          Value value;
        };
        SmallVector<IncomingEdge> incomingEdges;
        bool unavailable = false;
        llvm::SmallPtrSet<Block *, 8> visitedPredecessors;
        for (Block *predecessor : continuation->getPredecessors()) {
          if (!visitedPredecessors.insert(predecessor).second)
            continue;
          Operation *terminator = predecessor->getTerminator();
          auto incoming = dyn_cast<BranchOpInterface>(terminator);
          if (!incoming || !dominance.dominates(value, terminator)) {
            unavailable = true;
            break;
          }
          for (unsigned index = 0, end = terminator->getNumSuccessors();
               index != end; ++index)
            if (terminator->getSuccessor(index) == continuation)
              incomingEdges.push_back({incoming, index, value});
        }
        if (unavailable || incomingEdges.empty()) {
          suspension->emitError()
              << "cannot make live value " << value
              << " explicit on every continuation predecessor";
          signalPassFailure();
          return;
        }

        BlockArgument threaded =
            continuation->addArgument(value.getType(), suspension->getLoc());
        Value root = threadedRoots.lookup(value);
        if (!root)
          root = value;
        markSuspensionReentry(suspension, continuation, root);
        threadedValues[continuation].try_emplace(root, threaded);
        threadedRoots.try_emplace(threaded, root);
        for (IncomingEdge &incoming : incomingEdges)
          incoming.branch.getSuccessorOperands(incoming.successorIndex)
              .append(incoming.value);

        value.replaceUsesWithIf(threaded, [&](OpOperand &use) {
          return dominance.dominates(continuation, use.getOwner()->getBlock());
        });
      }
      if (!toThread.empty())
        liveness.reset();
    }

    // A continuation argument can flow into a later merge block which is not
    // dominated by that continuation. Make those downstream phi lanes
    // explicit as well. This is required by both executable tiers: each
    // bytecode dispatch starts with cleared scratch registers, while native
    // coroutine lowering must not leave an SSA definition on only one side of
    // a resume edge.
    bool changed;
    do {
      changed = false;
      for (Block &block : function.getBody()) {
        if (&block == &entry)
          continue;
        llvm::SetVector<Value> externalRoots;
        for (Operation &operation : block)
          for (Value value : operation.getOperands()) {
            if (value.getParentBlock() == &block)
              continue;
            if (auto argument = dyn_cast<BlockArgument>(value);
                argument && argument.getOwner() == &entry)
              continue;
            Value root = threadedRoots.lookup(value);
            if (!root)
              root = value;
            bool suspensionLive = suspensionReentryRoots[&block].contains(root);
            for (Block *predecessor : block.getPredecessors())
              suspensionLive |= threadedValues[predecessor].count(root) != 0;
            if (suspensionLive)
              externalRoots.insert(root);
          }
        for (Value root : externalRoots) {
          auto &threaded = threadedValues[&block];
          auto replaceExternalUses = [&](Value replacement) {
            for (Operation &operation : block)
              for (OpOperand &operand : operation.getOpOperands()) {
                Value value = operand.get();
                if (value.getParentBlock() == &block)
                  continue;
                Value operandRoot = threadedRoots.lookup(value);
                if (!operandRoot)
                  operandRoot = value;
                if (operandRoot == root)
                  operand.set(replacement);
              }
          };
          auto existing = threaded.find(root);
          if (existing != threaded.end()) {
            replaceExternalUses(existing->second);
            continue;
          }

          struct IncomingEdge {
            BranchOpInterface branch;
            unsigned successorIndex;
            Value value;
          };
          SmallVector<IncomingEdge> incomingEdges;
          bool unavailable = false;
          llvm::SmallPtrSet<Block *, 8> visitedPredecessors;
          for (Block *predecessor : block.getPredecessors()) {
            if (!visitedPredecessors.insert(predecessor).second)
              continue;
            Operation *terminator = predecessor->getTerminator();
            auto branch = dyn_cast<BranchOpInterface>(terminator);
            if (!branch) {
              terminator->emitError(
                  "cannot thread suspension-live state through a non-branch "
                  "terminator");
              signalPassFailure();
              return;
            }
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
            if (!incoming && dominance.dominates(root, terminator))
              incoming = root;
            if (!incoming) {
              unavailable = true;
              break;
            }
            bool foundSuccessor = false;
            for (auto [successorIndex, successor] :
                 llvm::enumerate(predecessor->getSuccessors())) {
              if (successor != &block)
                continue;
              incomingEdges.push_back(
                  {branch, static_cast<unsigned>(successorIndex), incoming});
              foundSuccessor = true;
            }
            if (!foundSuccessor) {
              terminator->emitError("predecessor is missing its CFG successor");
              signalPassFailure();
              return;
            }
          }
          if (unavailable || incomingEdges.empty())
            continue;

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
    } while (changed);
  }
};

} // namespace
} // namespace obelisk
