//===- SimulationProcessCFGThreading.cpp - Thread process CFG state -------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMTHREADPROCESSCFGPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace detail {

LogicalResult threadProcessStateThroughCFG(sim::SimFuncOp function) {
  if (function.getBody().empty())
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
  // Suspension lowering may already have made some values explicit successor
  // operands. Record those lanes before finding external uses so a value does
  // not acquire a second continuation argument when it is also live through a
  // later ordinary CFG edge.
  for (Block &block : llvm::drop_begin(function.getBody())) {
    for (auto [argumentIndex, argument] :
         llvm::enumerate(block.getArguments())) {
      Value incomingValue;
      bool commonIncoming = true;
      bool sawIncoming = false;
      for (Block &predecessor : function.getBody()) {
        auto branch = dyn_cast<BranchOpInterface>(predecessor.getTerminator());
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
          Value value = operands[argumentIndex];
          if (!sawIncoming) {
            incomingValue = value;
            sawIncoming = true;
          } else if (incomingValue != value) {
            commonIncoming = false;
            break;
          }
        }
        if (!commonIncoming)
          break;
      }
      if (sawIncoming && commonIncoming)
        threadedValues[&block].try_emplace(incomingValue, argument);
    }
  }

  bool changed;
  do {
    changed = false;
    for (Block &block : function.getBody()) {
      if (&block == entry)
        continue;
      llvm::SetVector<Value> externalValues;
      for (Operation &operation : block)
        for (Value value : operation.getOperands()) {
          if (value.getParentBlock() == &block)
            continue;
          if (auto argument = dyn_cast<BlockArgument>(value);
              argument && argument.getOwner() == entry)
            continue;
          externalValues.insert(value);
        }
      for (Value value : externalValues) {
        auto &threaded = threadedValues[&block];
        auto existing = threaded.find(value);
        if (existing != threaded.end()) {
          for (Operation &operation : block)
            for (OpOperand &operand : operation.getOpOperands())
              if (operand.get() == value)
                operand.set(existing->second);
          continue;
        }
        BlockArgument argument =
            block.addArgument(value.getType(), value.getLoc());
        threaded.insert({value, argument});
        SmallVector<OpOperand *> uses;
        for (Operation &operation : block)
          for (OpOperand &operand : operation.getOpOperands())
            if (operand.get() == value)
              uses.push_back(&operand);
        for (OpOperand *use : uses)
          use->set(argument);

        for (Block *predecessor : block.getPredecessors()) {
          auto branch =
              dyn_cast<BranchOpInterface>(predecessor->getTerminator());
          if (!branch)
            return predecessor->getTerminator()->emitError(
                "cannot thread suspension-live state through a non-branch "
                "terminator");
          bool found = false;
          for (auto [index, successor] :
               llvm::enumerate(predecessor->getSuccessors())) {
            if (successor != &block)
              continue;
            branch.getSuccessorOperands(index).append(value);
            found = true;
          }
          if (!found)
            return predecessor->getTerminator()->emitError(
                "predecessor is missing its CFG successor");
        }
        changed = true;
      }
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
