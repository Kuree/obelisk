//===- BytecodeRegisterPlanning.cpp - Bytecode register planning ----------===//

#include "BytecodeRegisterPlanning.h"

#include "obelisk/Analysis/StateDomainAnalysis.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace obelisk::bytecode {

/// Use the whole-design X/Z proof for local bytecode scratch values. Exact ABI
/// boundaries deliberately remain four-state: process frames, CFG maps, calls,
/// and returns all use representation-preserving copies. Within a block,
/// bytecode operations that require identical layouts form constraint
/// components; one unproven member keeps the entire component four-state.
FailureOr<llvm::DenseSet<Value>>
planTwoStateRegisters(sim::SimDesignOp design) {
  FailureOr<StateDomainAnalysis> analysis =
      StateDomainAnalysis::compute(design);
  if (failed(analysis))
    return failure();

  llvm::DenseSet<Value> candidates;
  llvm::DenseSet<Value> forcedFourState;
  llvm::DenseMap<Value, SmallVector<Value>> compatibleLayouts;
  auto isLogic = [](Value value) {
    return value && isa<sim::LogicType>(value.getType());
  };
  auto consider = [&](Value value) {
    if (!isLogic(value))
      return;
    if (analysis->isTwoState(value))
      candidates.insert(value);
    else
      forcedFourState.insert(value);
  };
  auto force = [&](Value value) {
    if (isLogic(value))
      forcedFourState.insert(value);
  };
  auto constrain = [&](Value lhs, Value rhs) {
    if (!isLogic(lhs) || !isLogic(rhs))
      return;
    compatibleLayouts[lhs].push_back(rhs);
    compatibleLayouts[rhs].push_back(lhs);
  };
  auto constrainResultTo = [&](Value result, ValueRange operands) {
    for (Value operand : operands)
      constrain(result, operand);
  };

  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>()) {
    if (function.isExternal())
      continue;
    for (Block &block : function.getBody()) {
      // Block arguments participate in parallel CFG or canonical-frame
      // copies. Keep those stable and specialize computations after them.
      for (BlockArgument argument : block.getArguments()) {
        consider(argument);
        force(argument);
      }
      for (Operation &operation : block) {
        for (Value result : operation.getResults())
          consider(result);

        if (isa<BranchOpInterface, sim::SimCallOp, sim::SimTaskCallOp,
                sim::SimDPICallOp, sim::SimSpawnOp, sim::SimReturnOp>(
                operation)) {
          for (Value operand : operation.getOperands())
            force(operand);
          for (Value result : operation.getResults())
            force(result);
        }

        // Automatic logic storage remains four-state for its full lifetime,
        // even when its initializer is known. Later stores through escaped
        // references must retain X/Z rather than inheriting the initializer's
        // compact one-plane register layout.
        if (auto alloc = dyn_cast<sim::SimRefAllocOp>(operation))
          force(alloc.getInitialValue());

        if (auto op = dyn_cast<sim::SimLogicUnaryOp>(operation)) {
          if (op.getKind() != sim::UnaryKind::LogicalNot)
            constrain(op.getResult(), op.getInput());
        } else if (auto op = dyn_cast<sim::SimLogicBinaryOp>(operation)) {
          constrainResultTo(op.getResult(), op.getOperands());
        } else if (auto op = dyn_cast<sim::SimLogicMuxOp>(operation)) {
          constrain(op.getResult(), op.getTrueValue());
          constrain(op.getResult(), op.getFalseValue());
          force(op.getCondition());
        } else if (auto op = dyn_cast<sim::SimLogicShiftOp>(operation)) {
          constrain(op.getResult(), op.getInput());
        } else if (auto op = dyn_cast<sim::SimLogicCompareOp>(operation)) {
          constrain(op.getLhs(), op.getRhs());
        } else if (auto op = dyn_cast<sim::SimLogicConcatOp>(operation)) {
          constrainResultTo(op.getResult(), op.getInputs());
        } else if (auto op = dyn_cast<sim::SimLogicReplicateOp>(operation)) {
          constrain(op.getResult(), op.getInput());
        } else if (auto op = dyn_cast<sim::SimLogicInsertOp>(operation)) {
          constrain(op.getResult(), op.getInput());
        } else if (auto op = dyn_cast<arith::SelectOp>(operation)) {
          constrain(op.getResult(), op.getTrueValue());
          constrain(op.getResult(), op.getFalseValue());
        } else if (auto op = dyn_cast<sim::SimAggregateInsertOp>(operation)) {
          constrain(op.getResult(), op.getInput());
        }
      }
    }
  }

  // A bytecode instruction that requires representation-compatible registers
  // makes its entire undirected component four-state when any member is
  // unproven or fixed by an ABI boundary. Propagate from those roots once
  // instead of repeatedly rescanning all constraints.
  SmallVector<Value> worklist(forcedFourState.begin(), forcedFourState.end());
  for (size_t index = 0; index != worklist.size(); ++index) {
    Value value = worklist[index];
    candidates.erase(value);
    auto found = compatibleLayouts.find(value);
    if (found == compatibleLayouts.end())
      continue;
    for (Value adjacent : found->second)
      if (forcedFourState.insert(adjacent).second)
        worklist.push_back(adjacent);
  }
  return candidates;
}

} // namespace obelisk::bytecode
