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

    // Threading only adds block arguments and successor operands, so the CFG
    // is invariant and dominance is computed once. Liveness is not: threading
    // a value across one suspension replaces its later uses with the new
    // continuation argument, and that argument is what the next suspension in
    // the chain has to forward.
    DominanceInfo dominance(function);
    Block &entry = function.getBody().front();

    for (Operation *suspension : suspensions) {
      Liveness liveness(function);
      const auto &liveOut = liveness.getLiveOut(suspension->getBlock());

      SmallVector<Value> orderedValues;
      for (Block &block : function.getBody()) {
        llvm::append_range(orderedValues, block.getArguments());
        for (Operation &op : block)
          llvm::append_range(orderedValues, op.getResults());
      }

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
        for (Block *predecessor : continuation->getPredecessors()) {
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
        for (IncomingEdge &incoming : incomingEdges)
          incoming.branch.getSuccessorOperands(incoming.successorIndex)
              .append(incoming.value);

        value.replaceUsesWithIf(threaded, [&](OpOperand &use) {
          return dominance.dominates(continuation, use.getOwner()->getBlock());
        });
      }
    }
  }
};

} // namespace
} // namespace obelisk
