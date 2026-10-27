#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "talos/Analysis/BoundedLoopAnalysis.h"
#include "talos/Analysis/IntegerRangeIPO.h"
#include "talos/Analysis/ScopedIntegerRangeAnalysis.h"
#include "talos/Analysis/StateDomainAnalysis.h"
#include "talos/Conversion/Passes.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace talos {
#define GEN_PASS_DEF_FOLDNATIVESCALARVALUESPASS
#include "talos/Conversion/Passes.h.inc"
namespace {
class FoldNativeScalarValuesPass final
    : public impl::FoldNativeScalarValuesPassBase<FoldNativeScalarValuesPass> {
public:
  void runOnOperation() override {
    auto function = getOperation();
    if (function.isExternal() || function.getBody().empty())
      return;
    function.walk([](Operation *op) {
      sim::remove<sim::metadata::KnownLogicResult>(op);
    });
    // Scheduling's contextual memory analysis still consumes structural
    // induction certificates. Replacing a single-iteration loop's body phi or
    // increment with a constant hides that recurrence and loses per-iteration
    // memory facts. Keep its control slice until scheduling has consumed it;
    // data expressions and no-X/Z certificates remain eligible for folding.
    DenseSet<Value> loopControl;
    SmallVector<Value> pending;
    auto loops = analysis::analyzeBoundedLoops(function.getBody());
    llvm::append_range(
        loops, analysis::analyzeActivationBoundedLoops(function.getBody()));
    for (auto &loop : loops) {
      pending.push_back(loop.induction);
      pending.push_back(loop.condition.getCondition());
    }
    while (!pending.empty()) {
      Value value = pending.pop_back_val();
      if (!isa<IntegerType, sim::LogicType>(value.getType()) ||
          !loopControl.insert(value).second)
        continue;
      if (Operation *op = value.getDefiningOp()) {
        llvm::append_range(pending, op->getOperands());
      } else if (auto arg = dyn_cast<BlockArgument>(value)) {
        for (Block *pred : arg.getOwner()->getPredecessors())
          if (auto branch = dyn_cast<BranchOpInterface>(pred->getTerminator()))
            for (unsigned i = 0; i < pred->getNumSuccessors(); ++i)
              if (pred->getSuccessor(i) == arg.getOwner()) {
                auto operands = branch.getSuccessorOperands(i);
                if (arg.getArgNumber() >= operands.getProducedOperandCount())
                  pending.push_back(operands[arg.getArgNumber()]);
              }
      }
    }
    DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
    solver.load<dataflow::DeadCodeAnalysis>();
    solver.load<dataflow::SparseConstantPropagation>();
    auto *ranges =
        solver.load<analysis::ScopedIntegerRangeAnalysis>(function, true);
    if (failed(solver.initializeAndRun(function)))
      return signalPassFailure();
    SmallVector<std::pair<Value, APInt>> constants;
    auto inspect = [&](Value value, ProgramPoint *point) {
      if (value.use_empty() ||
          !isa<IntegerType, sim::LogicType>(value.getType()))
        return;
      auto *state = solver.lookupState<analysis::ScopedRangeLattice>(point);
      if (!state || !state->reachable)
        return;
      if (isTwoStateSpecializationEnabled(function) &&
          isa<sim::LogicType>(value.getType()) && ranges->excludesXZ(value))
        if (auto result = dyn_cast<OpResult>(value);
            result && result.getOwner()->getNumResults() == 1)
          sim::set<sim::metadata::KnownLogicResult>(
              result.getOwner(), UnitAttr::get(&getContext()));
      auto range = ranges->getRange(value, *state);
      if (range && !loopControl.contains(value))
        if (auto constant = range->getConstantValue())
          constants.emplace_back(value, *constant);
    };
    for (Block &block : function.getBody()) {
      for (BlockArgument arg : block.getArguments())
        inspect(arg, solver.getProgramPointBefore(&block));
      for (Operation &op : block)
        if (!op.hasTrait<OpTrait::ConstantLike>())
          for (Value result : op.getResults())
            inspect(result, solver.getProgramPointAfter(&op));
    }
    // Collect against the fixed point before mutating SSA. Call effects remain
    // in the CFG even when IPO proves a returned value constant.
    OpBuilder builder(&getContext());
    for (auto &[value, bits] : constants) {
      if (value.use_empty())
        continue;
      if (auto result = dyn_cast<OpResult>(value))
        builder.setInsertionPointAfter(result.getOwner());
      else
        builder.setInsertionPointToStart(cast<BlockArgument>(value).getOwner());
      Value constant;
      if (auto type = dyn_cast<IntegerType>(value.getType()))
        constant = arith::ConstantOp::create(
            builder, value.getLoc(), builder.getIntegerAttr(type, bits));
      else {
        auto logicType = cast<sim::LogicType>(value.getType());
        auto integer = builder.getIntegerType(bits.getBitWidth());
        constant = sim::SimLogicConstantOp::create(
            builder, value.getLoc(), logicType,
            builder.getIntegerAttr(integer, bits),
            builder.getIntegerAttr(integer, APInt(bits.getBitWidth(), 0)));
      }
      value.replaceAllUsesWith(constant);
    }
  }
};
} // namespace
} // namespace talos
