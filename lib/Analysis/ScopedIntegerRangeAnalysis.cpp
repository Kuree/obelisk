#include "obelisk/Analysis/ScopedIntegerRangeAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/Utils/InferIntRangeCommon.h"
#include "obelisk/Analysis/BoundedLoopAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk::analysis {
ChangeResult ScopedRangeLattice::merge(const Ranges &incoming) {
  if (!reachable) {
    reachable = true;
    ranges = incoming;
    return ChangeResult::Change;
  }
  bool changed = false;
  SmallVector<Value> remove;
  for (auto &entry : ranges) {
    auto found = incoming.find(entry.first);
    if (found == incoming.end()) {
      remove.push_back(entry.first);
      continue;
    }
    auto next = entry.second.rangeUnion(found->second);
    if (next == entry.second)
      continue;
    changed = true;
    // Widen only expanding joins. Independently proved induction caps remain
    // available through getRange, including after a loop-carried range widens.
    if (expansions >= 8)
      remove.push_back(entry.first);
    else
      entry.second = next;
  }
  for (Value value : remove)
    ranges.erase(value);
  changed |= !remove.empty();
  if (changed)
    ++expansions;
  return changed ? ChangeResult::Change : ChangeResult::NoChange;
}
ChangeResult ScopedRangeLattice::join(const AbstractDenseLattice &rhs) {
  const auto &other = static_cast<const ScopedRangeLattice &>(rhs);
  return other.reachable ? merge(other.ranges) : ChangeResult::NoChange;
}
void ScopedRangeLattice::print(raw_ostream &os) const { os << ranges.size(); }

ScopedIntegerRangeAnalysis::ScopedIntegerRangeAnalysis(DataFlowSolver &solver,
                                                       Operation *function)
    : DenseForwardDataFlowAnalysis(solver) {
  SmallVector<Value> pending;
  function->walk([&](Operation *op) {
    if (isa<sim::SimRefArrayElementOp, sim::SimRefDynExtractOp,
            sim::SimDriverArrayElementOp, sim::SimDriverDynExtractOp>(op))
      pending.push_back(op->getOperand(1));
  });
  while (!pending.empty()) {
    Value value = pending.pop_back_val();
    auto width = sim::getPackedWidth(value.getType());
    if (!isa<IntegerType, sim::LogicType>(value.getType()) || !width ||
        *width > 128 || !relevant.insert(value).second)
      continue;
    if (auto *op = value.getDefiningOp())
      if (isa<InferIntRangeInterface, sim::SimLogicFromBitsOp,
              sim::SimLogicToBitsOp, sim::SimLogicBinaryOp,
              sim::SimLogicResizeOp>(op))
        llvm::append_range(pending, op->getOperands());
    // Phi inputs are also data dependencies; no predecessor recursion is
    // required for range solving. This walk only builds the analysis slice.
    if (auto arg = dyn_cast<BlockArgument>(value))
      for (Block *pred : arg.getOwner()->getPredecessors()) {
        auto branch = dyn_cast<BranchOpInterface>(pred->getTerminator());
        if (!branch)
          continue;
        for (unsigned i = 0; i < pred->getNumSuccessors(); ++i)
          if (pred->getSuccessor(i) == arg.getOwner()) {
            auto operands = branch.getSuccessorOperands(i);
            if (arg.getArgNumber() >= operands.getProducedOperandCount())
              pending.push_back(operands[arg.getArgNumber()]);
          }
      }
  }
  for (Region &region : function->getRegions())
    for (const auto &loop : analyzeBoundedLoops(region)) {
      auto global = getInductionRange(loop, true);
      auto body = getInductionRange(loop, false);
      if (global && relevant.contains(loop.induction))
        inductionBounds.try_emplace(loop.induction, *global);
      if (body && relevant.contains(loop.induction))
        bodyBounds.try_emplace(loop.condition, loop.induction, *body);
    }
}
std::optional<ConstantIntRanges>
ScopedIntegerRangeAnalysis::getRange(Value value,
                                     const ScopedRangeLattice &state) const {
  auto width = sim::getPackedWidth(value.getType());
  if (!width || !isa<IntegerType, sim::LogicType>(value.getType()))
    return std::nullopt;
  if (auto constant = value.getDefiningOp<sim::SimLogicConstantOp>())
    if (constant.getUnknown().isZero())
      return ConstantIntRanges::constant(constant.getValue());
  APInt constant;
  if (matchPattern(value, m_ConstantInt(&constant)))
    return ConstantIntRanges::constant(constant);
  auto cap = inductionBounds.find(value);
  auto scoped = state.ranges.find(value);
  if (cap != inductionBounds.end())
    return scoped == state.ranges.end()
               ? cap->second
               : cap->second.intersection(scoped->second);
  if (scoped != state.ranges.end())
    return scoped->second;
  // A logic range is also a proof that the selector has no X/Z bits. Missing
  // logic facts cannot be replaced by a two-state maximal interval.
  return isa<IntegerType>(value.getType())
             ? std::optional(ConstantIntRanges::maxRange(*width))
             : std::nullopt;
}
void ScopedIntegerRangeAnalysis::setToEntryState(ScopedRangeLattice *state) {
  propagateIfChanged(state, state->merge(ScopedRangeLattice::Ranges{}));
}
LogicalResult
ScopedIntegerRangeAnalysis::visitOperation(Operation *op,
                                           const ScopedRangeLattice &before,
                                           ScopedRangeLattice *after) {
  if (!before.reachable)
    return success();
  auto next = before.ranges;
  for (Value value : op->getResults())
    next.erase(value);
  if (auto interface = dyn_cast<InferIntRangeInterface>(op)) {
    SmallVector<ConstantIntRanges> inputs;
    for (Value value : op->getOperands())
      inputs.push_back(
          getRange(value, before).value_or(ConstantIntRanges::maxRange(0)));
    interface.inferResultRanges(
        inputs, [&](Value value, const ConstantIntRanges &range) {
          if (relevant.contains(value))
            next.insert_or_assign(value, range);
        });
  } else if (op->getNumResults() == 1 && relevant.contains(op->getResult(0))) {
    Value result = op->getResult(0);
    auto put = [&](const ConstantIntRanges &range) {
      next.insert_or_assign(result, range);
    };
    if (auto from = dyn_cast<sim::SimLogicFromBitsOp>(op)) {
      if (auto input = getRange(from.getInput(), before))
        put(*input);
    } else if (auto to = dyn_cast<sim::SimLogicToBitsOp>(op)) {
      if (auto input = getRange(to.getInput(), before))
        put(*input);
    } else if (auto resize = dyn_cast<sim::SimLogicResizeOp>(op)) {
      if (auto input = getRange(resize.getInput(), before)) {
        unsigned width = *sim::getPackedWidth(result.getType());
        if (width <= input->umin().getBitWidth())
          put(intrange::truncRange(*input, width));
        else
          put(resize.getIsSigned() ? intrange::extSIRange(*input, width)
                                   : intrange::extUIRange(*input, width));
      }
    } else if (auto binary = dyn_cast<sim::SimLogicBinaryOp>(op)) {
      auto lhs = getRange(binary.getLhs(), before);
      auto rhs = getRange(binary.getRhs(), before);
      if (lhs && rhs) {
        SmallVector<ConstantIntRanges> inputs{*lhs, *rhs};
        // These four-state operations are ordinary fixed-width bit operations
        // when both inputs are proved known. Reuse MLIR's wrapping arithmetic
        // inference; do not assume nsw/nuw or use host integer arithmetic.
        switch (binary.getKind()) {
        case sim::BinaryKind::Add:
          put(intrange::inferAdd(inputs));
          break;
        case sim::BinaryKind::Sub:
          put(intrange::inferSub(inputs));
          break;
        case sim::BinaryKind::Mul:
          put(intrange::inferMul(inputs));
          break;
        case sim::BinaryKind::And:
          put(intrange::inferAnd(inputs));
          break;
        case sim::BinaryKind::Or:
          put(intrange::inferOr(inputs));
          break;
        case sim::BinaryKind::Xor:
          put(intrange::inferXor(inputs));
          break;
        default:
          break;
        }
      }
    }
  }
  propagateIfChanged(after, after->merge(next));
  return success();
}
void ScopedIntegerRangeAnalysis::visitCallControlFlowTransfer(
    CallOpInterface call, dataflow::CallControlFlowAction,
    const ScopedRangeLattice &before, ScopedRangeLattice *after) {
  // SSA integer snapshots cannot be changed by a callee. Call results have no
  // inferred range unless their operation supplies a verified interface.
  (void)visitOperation(call, before, after);
}
void ScopedIntegerRangeAnalysis::visitBlockTransfer(
    Block *block, ProgramPoint *, Block *predecessor,
    const ScopedRangeLattice &before, ScopedRangeLattice *after) {
  if (!before.reachable)
    return;
  auto next = before.ranges;
  Operation *terminator = predecessor->getTerminator();
  if (auto branch = dyn_cast<cf::CondBranchOp>(terminator)) {
    // The validated structural certificate is an edge fact, never a global
    // narrowing of the induction variable to its body-only range.
    auto bound = bodyBounds.find(branch);
    if (bound != bodyBounds.end() && branch.getTrueDest() == block &&
        branch.getFalseDest() != block)
      next.insert_or_assign(bound->second.first, bound->second.second);
  }
  auto interface = dyn_cast<BranchOpInterface>(terminator);
  ScopedRangeLattice edge(getProgramPointAfter(terminator));
  edge.reachable = true;
  edge.ranges = next;
  // Incoming operands refer to the predecessor's SSA bindings. Compute all
  // phi inputs before assigning any successor arguments (including swaps).
  for (Operation &op : *block)
    for (Value result : op.getResults())
      next.erase(result);
  for (BlockArgument arg : block->getArguments()) {
    next.erase(arg);
    if (!interface || !relevant.contains(arg))
      continue;
    std::optional<ConstantIntRanges> incoming;
    for (unsigned i = 0; i < predecessor->getNumSuccessors(); ++i) {
      if (predecessor->getSuccessor(i) != block)
        continue;
      auto operands = interface.getSuccessorOperands(i);
      if (arg.getArgNumber() < operands.getProducedOperandCount())
        continue;
      auto range = getRange(operands[arg.getArgNumber()], edge);
      if (range)
        incoming = incoming ? incoming->rangeUnion(*range) : *range;
    }
    if (incoming)
      next.insert_or_assign(arg, *incoming);
  }
  propagateIfChanged(after, after->merge(next));
}
} // namespace obelisk::analysis
