#include "talos/Analysis/ScopedIntegerRangeAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/Utils/InferIntRangeCommon.h"
#include "talos/Analysis/BoundedLoopAnalysis.h"
#include "talos/Analysis/IntegerRangeIPO.h"
#include "talos/Analysis/ScalarEvolutionAnalysis.h"
#include "talos/Dialect/Schedule/ScheduleOps.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace talos::analysis {
static arith::CmpIPredicate swapComparison(arith::CmpIPredicate predicate) {
  switch (predicate) {
  case arith::CmpIPredicate::slt:
    return arith::CmpIPredicate::sgt;
  case arith::CmpIPredicate::sle:
    return arith::CmpIPredicate::sge;
  case arith::CmpIPredicate::sgt:
    return arith::CmpIPredicate::slt;
  case arith::CmpIPredicate::sge:
    return arith::CmpIPredicate::sle;
  case arith::CmpIPredicate::ult:
    return arith::CmpIPredicate::ugt;
  case arith::CmpIPredicate::ule:
    return arith::CmpIPredicate::uge;
  case arith::CmpIPredicate::ugt:
    return arith::CmpIPredicate::ult;
  case arith::CmpIPredicate::uge:
    return arith::CmpIPredicate::ule;
  default:
    return predicate;
  }
}
ChangeResult ScopedRangeLattice::merge(const Ranges &incoming) {
  if (!reachable) {
    reachable = true;
    ranges = incoming;
    return ChangeResult::Change;
  }
  if (ranges.sameAs(incoming))
    return ChangeResult::NoChange;
  // Most joins are identity transfers; the structural merge returns the
  // same tree when nothing changes. A fact absent from `incoming` is
  // dropped; one that keeps expanding is widened away after a few joins,
  // while independently proved induction caps remain available through
  // getRange.
  bool widen = expansions >= 8;
  bool changed = ranges.intersectWith(
      incoming,
      [&](const ConstantIntRanges &mine,
          const ConstantIntRanges &theirs) -> std::optional<ConstantIntRanges> {
        ConstantIntRanges next = mine.rangeUnion(theirs);
        if (next == mine)
          return mine;
        if (widen)
          return std::nullopt;
        return next;
      });
  if (!changed)
    return ChangeResult::NoChange;
  ++expansions;
  return ChangeResult::Change;
}
ChangeResult ScopedRangeLattice::join(const AbstractDenseLattice &rhs) {
  const auto &other = static_cast<const ScopedRangeLattice &>(rhs);
  return other.reachable ? merge(other.ranges) : ChangeResult::NoChange;
}
void ScopedRangeLattice::print(raw_ostream &os) const {
  os << getRanges().size();
}

ScopedIntegerRangeAnalysis::ScopedIntegerRangeAnalysis(DataFlowSolver &solver,
                                                       Operation *function,
                                                       bool allScalarValues)
    : DenseForwardDataFlowAnalysis(solver) {
  SmallVector<Value> pending;
  function->walk([&](Operation *op) {
    if (allScalarValues) {
      llvm::append_range(pending, op->getResults());
      for (Region &region : op->getRegions())
        for (Block &block : region)
          llvm::append_range(pending, block.getArguments());
    }
    if (isa<sim::SimCallOp, sim::SimReturnOp>(op))
      for (Value value : op->getOperands())
        if (isa<IntegerType, sim::LogicType>(value.getType()))
          pending.push_back(value);
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
      if (isa<InferIntRangeInterface, sim::SimCallOp, sim::SimLogicFromBitsOp,
              sim::SimLogicToBitsOp, sim::SimLogicBinaryOp,
              sim::SimLogicResizeOp, sim::SimLogicExtractOp,
              sim::SimLogicCompareOp, sim::SimLogicIsTrueOp, math::CtPopOp>(op))
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
  evolution = std::make_unique<ScalarEvolutionAnalysis>(function, relevant);
  // Liveness of the relevant slice only: a value is live into every block on
  // a path from a use back to its definition, as in mlir::Liveness. A fused
  // activation has thousands of unrelated values whose full liveness would
  // dominate this analysis.
  for (Value value : relevant) {
    Region *definingRegion = value.getParentRegion();
    Block *definingBlock = value.getParentBlock();
    for (Operation *user : value.getUsers()) {
      for (Block *block = user->getBlock(); block;) {
        bool inDefiningRegion = block->getParent() == definingRegion;
        SmallVector<Block *> worklist{block};
        while (!worklist.empty()) {
          Block *current = worklist.pop_back_val();
          if ((inDefiningRegion && current == definingBlock) ||
              !liveIns.insert({current, value}).second)
            continue;
          llvm::append_range(worklist, current->getPredecessors());
        }
        if (inDefiningRegion)
          break;
        Operation *parent = block->getParentOp();
        block = parent ? parent->getBlock() : nullptr;
      }
    }
  }
}
ScopedIntegerRangeAnalysis::~ScopedIntegerRangeAnalysis() = default;
bool ScopedIntegerRangeAnalysis::excludesXZ(Value value) const {
  return evolution->excludesXZ(value);
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
  const ConstantIntRanges *scoped =
      state.getRanges().find(ScopedRangeLattice::keyOf(value));
  std::optional<ConstantIntRanges> result;
  if (auto argument = dyn_cast<BlockArgument>(value))
    if (auto function =
            dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
        function && argument.getOwner()->isEntryBlock())
      result = decodeIntegerRange(
          function.getArgAttr(argument.getArgNumber(),
                              sim::metadata::IntegerRange::getNameStr()),
          width);
  if (auto range = getIntegerResultRange(value))
    result = result ? result->intersection(*range) : *range;
  if (cap != inductionBounds.end())
    result = result ? result->intersection(cap->second) : cap->second;
  if (scoped)
    result = result ? result->intersection(*scoped) : *scoped;
  if (auto symbolic = evolution->getRange(value))
    result = result ? result->intersection(*symbolic) : *symbolic;
  if (result)
    return result;
  // A logic range is also a proof that the selector has no X/Z bits. Missing
  // logic facts cannot be replaced by a two-state maximal interval.
  return isa<IntegerType>(value.getType())
             ? std::optional(ConstantIntRanges::maxRange(*width))
             : std::nullopt;
}
bool ScopedIntegerRangeAnalysis::proveBounds(Value value,
                                             const ScopedRangeLattice &state,
                                             const APInt &lower,
                                             const APInt &upper) const {
  return evolution->proveBounds(value, lower, upper, [&](Value input) {
    // Only facts available at this access may constrain an SMT expression.
    // A definition in another branch may alias a shared integer while its
    // path-specific range is inapplicable here.
    if (input == value ||
        state.getRanges().contains(ScopedRangeLattice::keyOf(input)))
      return getRange(input, state);
    if (auto argument = dyn_cast<BlockArgument>(input);
        argument && argument.getOwner()->isEntryBlock())
      return getRange(input, state);
    APInt constant;
    if (matchPattern(input, m_ConstantInt(&constant)))
      return std::optional(ConstantIntRanges::constant(constant));
    return std::optional<ConstantIntRanges>{};
  });
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
    next.erase(ScopedRangeLattice::keyOf(value));
  if (auto interface = dyn_cast<InferIntRangeInterface>(op)) {
    // A logic range certifies that the value has no X or Z bits. A logic
    // operand without that certificate may be X, so no logic result of an
    // interface operation (for example a select of logic arms) can have one,
    // and a width-mismatched placeholder must never reach the inference.
    SmallVector<ConstantIntRanges> inputs;
    bool uncertifiedLogic = false;
    for (Value value : op->getOperands()) {
      std::optional<ConstantIntRanges> range = getRange(value, before);
      uncertifiedLogic |= !range && isa<sim::LogicType>(value.getType());
      unsigned width = ConstantIntRanges::getStorageBitwidth(value.getType());
      if (!range)
        if (auto packed = sim::getPackedWidth(value.getType()))
          width = *packed;
      inputs.push_back(range.value_or(ConstantIntRanges::maxRange(width)));
    }
    interface.inferResultRanges(
        inputs, [&](Value value, const ConstantIntRanges &range) {
          if (relevant.contains(value) &&
              !(uncertifiedLogic && isa<sim::LogicType>(value.getType())))
            next.insert(ScopedRangeLattice::keyOf(value), range);
        });
  } else if (op->getNumResults() == 1 && relevant.contains(op->getResult(0))) {
    Value result = op->getResult(0);
    auto put = [&](const ConstantIntRanges &range) {
      next.insert(ScopedRangeLattice::keyOf(result), range);
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
    } else if (auto extract = dyn_cast<sim::SimLogicExtractOp>(op)) {
      // A slice is a logical shift right followed by a truncation; a
      // truncation is canonically the low slice.
      if (auto input = getRange(extract.getInput(), before)) {
        unsigned inputWidth = input->umin().getBitWidth();
        ConstantIntRanges shifted = *input;
        if (extract.getLowBit() != 0)
          shifted = intrange::inferShrU(
              {*input, ConstantIntRanges::constant(
                           APInt(inputWidth, extract.getLowBit()))});
        put(intrange::truncRange(shifted, extract.getType().getWidth()));
      }
    } else if (auto count = dyn_cast<math::CtPopOp>(op)) {
      // A population count never exceeds the width of its operand.
      unsigned width = cast<IntegerType>(count.getType()).getWidth();
      put(ConstantIntRanges::fromUnsigned(
          APInt::getZero(width),
          APInt(width, width, /*isSigned=*/false, /*implicitTrunc=*/true)));
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
    if (branch.getTrueDest() != branch.getFalseDest())
      if (auto cmp = branch.getCondition().getDefiningOp<arith::CmpIOp>()) {
        auto predicate = cmp.getPredicate();
        if (branch.getFalseDest() == block)
          predicate = arith::invertPredicate(predicate);
        for (unsigned side = 0; side < 2; ++side) {
          Value value = cmp->getOperand(side);
          if (!relevant.contains(value))
            continue;
          auto range = getRange(value, before);
          auto other = getRange(cmp->getOperand(1 - side), before);
          if (!range || !other)
            continue;
          auto pred = side ? swapComparison(predicate) : predicate;
          unsigned width = range->umin().getBitWidth();
          std::optional<ConstantIntRanges> edgeRange;
          APInt zero(width, 0), umax = APInt::getMaxValue(width);
          APInt smin = APInt::getSignedMinValue(width),
                smax = APInt::getSignedMaxValue(width);
          switch (pred) {
          case arith::CmpIPredicate::eq:
            edgeRange = *other;
            break;
          case arith::CmpIPredicate::ult:
            if (!other->umax().isZero())
              edgeRange =
                  ConstantIntRanges::fromUnsigned(zero, other->umax() - 1);
            break;
          case arith::CmpIPredicate::ule:
            edgeRange = ConstantIntRanges::fromUnsigned(zero, other->umax());
            break;
          case arith::CmpIPredicate::ugt:
            if (!other->umin().isMaxValue())
              edgeRange =
                  ConstantIntRanges::fromUnsigned(other->umin() + 1, umax);
            break;
          case arith::CmpIPredicate::uge:
            edgeRange = ConstantIntRanges::fromUnsigned(other->umin(), umax);
            break;
          case arith::CmpIPredicate::slt:
            if (!other->smax().isMinSignedValue())
              edgeRange =
                  ConstantIntRanges::fromSigned(smin, other->smax() - 1);
            break;
          case arith::CmpIPredicate::sle:
            edgeRange = ConstantIntRanges::fromSigned(smin, other->smax());
            break;
          case arith::CmpIPredicate::sgt:
            if (!other->smin().isMaxSignedValue())
              edgeRange =
                  ConstantIntRanges::fromSigned(other->smin() + 1, smax);
            break;
          case arith::CmpIPredicate::sge:
            edgeRange = ConstantIntRanges::fromSigned(other->smin(), smax);
            break;
          default:
            break;
          }
          if (edgeRange) {
            auto refined = range->intersection(*edgeRange);
            if (refined.umin().ule(refined.umax()) &&
                refined.smin().sle(refined.smax()))
              next.insert(ScopedRangeLattice::keyOf(value), refined);
          }
        }
      }
    // The validated structural certificate is an edge fact, never a global
    // narrowing of the induction variable to its body-only range.
    auto bound = bodyBounds.find(branch);
    if (bound != bodyBounds.end() && branch.getTrueDest() == block &&
        branch.getFalseDest() != block)
      next.insert(ScopedRangeLattice::keyOf(bound->second.first),
                  bound->second.second);
  }
  auto interface = dyn_cast<BranchOpInterface>(terminator);
  ScopedRangeLattice edge(getProgramPointAfter(terminator));
  (void)edge.merge(next);
  // Incoming operands refer to the predecessor's SSA bindings. Compute all
  // phi inputs before assigning any successor arguments (including swaps).
  for (Operation &op : *block)
    for (Value result : op.getResults())
      next.erase(ScopedRangeLattice::keyOf(result));
  for (BlockArgument arg : block->getArguments()) {
    next.erase(ScopedRangeLattice::keyOf(arg));
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
      next.insert(ScopedRangeLattice::keyOf(arg), *incoming);
  }
  // A fused activation can contain thousands of independent selector loops.
  // Keeping every preceding loop's SSA facts in each immutable block state
  // makes both memory and joins quadratic in the activation's length. Only
  // values live into this block and its freshly mapped arguments can be
  // queried here. Map all predecessor operands first: parallel phi copies
  // may consume values that are dead on entry to the successor.
  SmallVector<uint64_t> dead;
  next.forEach([&](uint64_t key, const ConstantIntRanges &) {
    Value value = ScopedRangeLattice::valueOf(key);
    auto argument = dyn_cast<BlockArgument>(value);
    if (!(argument && argument.getOwner() == block) &&
        !liveIns.contains({block, value}))
      dead.push_back(key);
  });
  for (uint64_t key : dead)
    next.erase(key);
  propagateIfChanged(after, after->merge(next));
}
} // namespace talos::analysis
