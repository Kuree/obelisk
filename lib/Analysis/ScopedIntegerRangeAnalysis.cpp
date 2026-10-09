#include "obelisk/Analysis/ScopedIntegerRangeAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/Utils/InferIntRangeCommon.h"
#include "obelisk/Analysis/BoundedLoopAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk::analysis {
const ScopedRangeLattice::Ranges &ScopedRangeLattice::getRanges() const {
  static const Ranges empty;
  return ranges ? *ranges : empty;
}
ChangeResult ScopedRangeLattice::merge(Ranges incoming) {
  return mergeSnapshot(std::make_shared<const Ranges>(std::move(incoming)));
}
ChangeResult
ScopedRangeLattice::mergeSnapshot(std::shared_ptr<const Ranges> incoming) {
  if (!reachable) {
    reachable = true;
    ranges = std::move(incoming);
    return ChangeResult::Change;
  }
  if (ranges == incoming)
    return ChangeResult::NoChange;
  // Most joins are identity transfers. Copy only when this program point's
  // facts actually change; published snapshots are never modified in place.
  std::shared_ptr<Ranges> updated;
  for (const auto &entry : getRanges()) {
    auto found = incoming->find(entry.first);
    bool remove = found == incoming->end();
    auto next = remove ? entry.second : entry.second.rangeUnion(found->second);
    if (!remove && next == entry.second)
      continue;
    // Widen only expanding joins. Independently proved induction caps remain
    // available through getRange, including after a loop-carried range widens.
    if (!updated)
      updated = std::make_shared<Ranges>(getRanges());
    if (remove || expansions >= 8)
      updated->erase(entry.first);
    else
      updated->find(entry.first)->second = next;
  }
  if (!updated)
    return ChangeResult::NoChange;
  ranges = std::move(updated);
  ++expansions;
  return ChangeResult::Change;
}
ChangeResult ScopedRangeLattice::join(const AbstractDenseLattice &rhs) {
  const auto &other = static_cast<const ScopedRangeLattice &>(rhs);
  return other.reachable ? mergeSnapshot(other.ranges) : ChangeResult::NoChange;
}
void ScopedRangeLattice::print(raw_ostream &os) const {
  os << getRanges().size();
}

ScopedIntegerRangeAnalysis::ScopedIntegerRangeAnalysis(DataFlowSolver &solver,
                                                       Operation *function)
    : DenseForwardDataFlowAnalysis(solver), liveness(function) {
  SmallVector<Value> pending;
  function->walk([&](Operation *op) {
    if (isa<sim::SimRefArrayElementOp, sim::SimRefDynExtractOp,
            schedule::NativeWordViewOp, sim::SimDriverArrayElementOp,
            sim::SimDriverDynExtractOp, sim::SimLogicDynExtractOp,
            sim::SimBitsDynExtractOp, sim::SimArrayDynExtractOp>(op))
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
  const auto &ranges = state.getRanges();
  auto scoped = ranges.find(value);
  if (cap != inductionBounds.end())
    return scoped == ranges.end() ? cap->second
                                  : cap->second.intersection(scoped->second);
  if (scoped != ranges.end())
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
  if (llvm::none_of(op->getResults(),
                    [&](Value value) { return relevant.contains(value); })) {
    propagateIfChanged(after, after->join(before));
    return success();
  }
  auto next = before.getRanges();
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
  propagateIfChanged(after, after->merge(std::move(next)));
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
  auto next = before.getRanges();
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
  edge.merge(next);
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
      if (arg.getArgNumber() < operands.getProducedOperandCount()) {
        incoming.reset();
        break;
      }
      auto range = getRange(operands[arg.getArgNumber()], edge);
      // Two successor edges of the same terminator may carry different
      // values. An unknown logic input on either edge must keep X/Z possible;
      // taking only the other edge's known range would invent a proof.
      if (!range) {
        incoming.reset();
        break;
      }
      incoming = incoming ? incoming->rangeUnion(*range) : *range;
    }
    if (incoming)
      next.insert_or_assign(arg, *incoming);
  }
  // A fused activation can contain thousands of independent selector loops.
  // Keeping every preceding loop's SSA facts in each immutable block state
  // makes both memory and joins quadratic in the activation's length. Only
  // values live into this block and its freshly mapped arguments can be
  // queried here. Map all predecessor operands first: parallel phi copies
  // may consume values that are dead on entry to the successor.
  const auto &liveIn = liveness.getLiveIn(block);
  for (const auto &entry : llvm::make_early_inc_range(next)) {
    auto argument = dyn_cast<BlockArgument>(entry.first);
    if (!(argument && argument.getOwner() == block) &&
        !liveIn.contains(entry.first))
      next.erase(entry.first);
  }
  propagateIfChanged(after, after->merge(std::move(next)));
}
} // namespace obelisk::analysis
