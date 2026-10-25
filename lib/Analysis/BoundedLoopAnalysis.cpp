#include "obelisk/Analysis/BoundedLoopAnalysis.h"
#include "mlir/Analysis/CFGLoopInfo.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/InferIntRangeInterface.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/RegionUtils.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/SmallPtrSet.h"
using namespace mlir;
namespace obelisk::analysis {
std::optional<ConstantIntRanges> getInductionRange(const BoundedLoop &loop,
                                                   bool includeExit) {
  if (!arith::applyCmpPredicate(loop.predicate, loop.init, loop.limit))
    return includeExit
               ? std::optional(
                     loop.initValue
                         ? ConstantIntRanges::range(loop.initMin, loop.initMax,
                                                    loop.signedCompare)
                         : ConstantIntRanges::constant(loop.init))
               : std::nullopt;
  if (loop.stride.isZero())
    return std::nullopt;
  bool increasing;
  bool inclusive;
  switch (loop.predicate) {
  case arith::CmpIPredicate::slt:
  case arith::CmpIPredicate::ult:
    increasing = true;
    inclusive = false;
    break;
  case arith::CmpIPredicate::sle:
  case arith::CmpIPredicate::ule:
    increasing = true;
    inclusive = true;
    break;
  case arith::CmpIPredicate::sgt:
  case arith::CmpIPredicate::ugt:
    increasing = false;
    inclusive = false;
    break;
  case arith::CmpIPredicate::sge:
  case arith::CmpIPredicate::uge:
    increasing = false;
    inclusive = true;
    break;
  default:
    return std::nullopt;
  }
  unsigned width = loop.init.getBitWidth();
  unsigned proofWidth = width + 2;
  auto extend = [&](const APInt &value) {
    return loop.signedCompare ? value.sext(proofWidth) : value.zext(proofWidth);
  };
  APInt start = extend(loop.init), limit = extend(loop.limit);
  APInt delta = extend(loop.stride);
  if (!loop.isAdd)
    delta = -delta;
  if (delta.isNegative() == increasing)
    return std::nullopt;
  APInt stride = increasing ? delta : -delta;
  APInt minimum = loop.signedCompare
                      ? APInt::getSignedMinValue(width).sext(proofWidth)
                      : APInt::getZero(proofWidth);
  APInt maximum = loop.signedCompare
                      ? APInt::getSignedMaxValue(width).sext(proofWidth)
                      : APInt::getMaxValue(width).zext(proofWidth);
  if (loop.initValue && loop.initMin != loop.initMax) {
    // Different initializers may occupy different stride residue classes.
    // Bound the final step from the last possible body value, rather than
    // extrapolating the sequence of just one endpoint. Include zero-trip
    // initializers in the header range, but not in the body range.
    APInt bodyEnd = limit;
    if (!inclusive)
      bodyEnd += increasing ? -APInt(proofWidth, 1) : APInt(proofWidth, 1);
    APInt exitEnd = increasing ? bodyEnd + stride : bodyEnd - stride;
    if (!exitEnd.sge(minimum) || !exitEnd.sle(maximum))
      return std::nullopt;
    APInt low = increasing ? extend(loop.initMin) : bodyEnd;
    APInt high = increasing ? bodyEnd : extend(loop.initMax);
    if (includeExit) {
      low = increasing ? low
                       : llvm::APIntOps::smin(extend(loop.initMin), exitEnd);
      high = increasing ? llvm::APIntOps::smax(extend(loop.initMax), exitEnd)
                        : high;
    }
    return ConstantIntRanges::range(low.trunc(width), high.trunc(width),
                                    loop.signedCompare);
  }
  APInt distance = increasing ? limit - start : start - limit;
  if (inclusive)
    ++distance;
  APInt trips = distance.udiv(stride);
  if (!distance.urem(stride).isZero())
    ++trips;
  APInt last = increasing ? start + trips * stride : start - trips * stride;
  if (!last.sge(minimum) || !last.sle(maximum))
    return std::nullopt;
  if (!includeExit)
    last = increasing ? last - stride : last + stride;
  APInt low = (increasing ? start : last).trunc(width);
  APInt high = (increasing ? last : start).trunc(width);
  return ConstantIntRanges::range(low, high, loop.signedCompare);
}
// Preserve the exact sequential activations of IEEE 1800-2023 12.7. A small
// constant induction loop is not a scheduler control loop: exposing its finite
// CFG before compute-graph construction also exposes constant packed indices.
// Suspension and unproved cycles are excluded. Termination marking accepts
// early exits, matching constant entries and already proved inner loops;
// replication additionally requires repairable SSA state. No loop-progress
// assumption comes from a source type or an arbitrary iteration cutoff.
//
// Termination and unrolling are separate questions. The structural proof below
// establishes that the loop finishes without any external event; whether to
// replicate its body is a code-size policy on top of that. A large constant
// induction loop -- an X-initialization sweep over a memory, a reset sweep over
// a queue -- must keep the proof even when replication is refused, because the
// scheduler otherwise has to treat it as an unbounded control loop and hand its
// whole process to bytecode.

arith::CmpIPredicate swapPredicate(arith::CmpIPredicate predicate) {
  using P = arith::CmpIPredicate;
  switch (predicate) {
  case P::slt:
    return P::sgt;
  case P::sle:
    return P::sge;
  case P::sgt:
    return P::slt;
  case P::sge:
    return P::sle;
  case P::ult:
    return P::ugt;
  case P::ule:
    return P::uge;
  case P::ugt:
    return P::ult;
  case P::uge:
    return P::ule;
  case P::eq:
  case P::ne:
    return predicate;
  }
  llvm_unreachable("unknown integer comparison");
}

/// Prove the induction sequence leaves the continuation predicate after a
/// finite number of steps, in closed form, without enumerating iterations. Only
/// the monotone inequality and exactly reachable modular equality cases
/// qualify.
bool terminatesInFiniteSteps(const BoundedLoop &loop) {
  // IEEE 1800-2023 11.6.1 and 12.7.1: induction arithmetic has a fixed
  // width. Reaching the bound in mathematical integers is insufficient if
  // the last stride wraps before the next condition is evaluated.
  if (!arith::applyCmpPredicate(loop.predicate, loop.init, loop.limit))
    return true;
  if (loop.stride.isZero())
    return false;
  if (loop.predicate == arith::CmpIPredicate::eq)
    return true; // A nonzero modular step leaves equality after one iteration.
  if (loop.predicate == arith::CmpIPredicate::ne) {
    // A modular induction visits exactly one residue class modulo gcd(step,
    // 2^width). This also proves != loops that intentionally cross zero.
    APInt distance = loop.limit - loop.init;
    return distance.countr_zero() >= loop.stride.countr_zero();
  }

  return getInductionRange(loop, true).has_value();
}

/// Packed wrappers preserve both planes. Widening an unsigned known counter
/// preserves its integer value; narrowing before the comparison does not.
static Value unwrapUnsignedInduction(Value value) {
  while (Operation *op = value.getDefiningOp()) {
    if (isa<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(op)) {
      value = op->getOperand(0);
      continue;
    }
    if (auto extend = dyn_cast<arith::ExtUIOp>(op)) {
      value = extend.getIn();
      continue;
    }
    if (auto resize = dyn_cast<sim::SimLogicResizeOp>(op)) {
      if (resize.getIsSigned() ||
          cast<sim::LogicType>(resize.getResult().getType()).getWidth() <
              cast<sim::LogicType>(resize.getInput().getType()).getWidth())
        return {};
      value = resize.getInput();
      continue;
    }
    break;
  }
  return value;
}

static bool matchInductionConstant(Value value, APInt &bits) {
  if (matchPattern(value, m_ConstantInt(&bits)))
    return true;
  if (auto constant = value.getDefiningOp<sim::SimLogicConstantOp>()) {
    if (!constant.getUnknown().isZero())
      return false;
    bits = constant.getValue();
    return true;
  }
  Operation *op = value.getDefiningOp();
  if (!op)
    return false;
  if (isa<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp,
          sim::SimLogicFromBitsOp>(op))
    return matchInductionConstant(op->getOperand(0), bits);
  if (auto resize = dyn_cast<sim::SimLogicResizeOp>(op)) {
    if (!matchInductionConstant(resize.getInput(), bits))
      return false;
    unsigned width = cast<sim::LogicType>(resize.getType()).getWidth();
    bits = resize.getIsSigned() ? bits.sextOrTrunc(width)
                                : bits.zextOrTrunc(width);
    return true;
  }
  return false;
}

/// A four-state induction test against a known bound. Canonicalization
/// spells the same test in several ways: a condition `x == c` becomes the
/// case equality `x === c`, and `x != 0` becomes the truth value of `x`
/// (IEEE 1800-2023 11.4.5, 12.4). For a known counter and a known bound these
/// are all one comparison.
struct LogicInductionTest {
  Value compared;
  sim::CompareKind kind;
  APInt bound;
  SmallVector<Operation *, 2> operations;
};

static std::optional<LogicInductionTest>
matchLogicInductionTest(Value condition) {
  auto knownBound = [](Value value) -> std::optional<APInt> {
    auto constant = value.getDefiningOp<sim::SimLogicConstantOp>();
    if (!constant || !constant.getUnknown().isZero())
      return std::nullopt;
    return constant.getValue();
  };
  if (auto compare = condition.getDefiningOp<sim::SimLogicCompareOp>()) {
    if (compare.getKind() != sim::CompareKind::CaseEq &&
        compare.getKind() != sim::CompareKind::CaseNe)
      return std::nullopt;
    std::optional<APInt> bound = knownBound(compare.getRhs());
    if (!bound)
      return std::nullopt;
    return LogicInductionTest{compare.getLhs(),
                              compare.getKind() == sim::CompareKind::CaseEq
                                  ? sim::CompareKind::Eq
                                  : sim::CompareKind::Ne,
                              *bound,
                              {compare.getOperation()}};
  }
  auto isTrue = condition.getDefiningOp<sim::SimLogicIsTrueOp>();
  if (!isTrue)
    return std::nullopt;
  if (auto compare =
          isTrue.getInput().getDefiningOp<sim::SimLogicCompareOp>()) {
    std::optional<APInt> bound = knownBound(compare.getRhs());
    if (!bound)
      return std::nullopt;
    return LogicInductionTest{compare.getLhs(),
                              compare.getKind(),
                              *bound,
                              {isTrue.getOperation(), compare.getOperation()}};
  }
  auto logic = cast<sim::LogicType>(isTrue.getInput().getType());
  return LogicInductionTest{isTrue.getInput(),
                            sim::CompareKind::Ne,
                            APInt::getZero(logic.getWidth()),
                            {isTrue.getOperation()}};
}

/// Infer invariant expressions through dialect interfaces. Only constants and
/// enclosing inductions are leaves: neither mutable design state nor a value
/// carried by the current loop can establish a finite recurrence.
static std::optional<ConstantIntRanges>
inferInvariantRange(Value value, Block *header, ArrayRef<BoundedLoop> enclosing,
                    DominanceInfo &dominance) {
  DenseMap<Value, ConstantIntRanges> ranges;
  DenseSet<Value> visiting;
  auto infer = [&](Value value,
                   auto &infer) -> std::optional<ConstantIntRanges> {
    if (auto found = ranges.find(value); found != ranges.end())
      return found->second;
    APInt constant;
    if (matchPattern(value, m_ConstantInt(&constant)))
      return ConstantIntRanges::constant(constant);
    for (const auto &outer : enclosing)
      if (outer.induction == value &&
          (outer.members.contains(header) ||
           dominance.dominates(outer.condition->getSuccessor(0), header)))
        return getInductionRange(outer, false);
    Operation *definition = value.getDefiningOp();
    auto interface = dyn_cast_or_null<InferIntRangeInterface>(definition);
    if (!interface || !isMemoryEffectFree(definition) ||
        !isSpeculatable(definition) || !visiting.insert(value).second)
      return std::nullopt;
    SmallVector<ConstantIntRanges> inputs;
    for (Value operand : definition->getOperands()) {
      auto input = infer(operand, infer);
      if (!input)
        return std::nullopt;
      inputs.push_back(*input);
    }
    interface.inferResultRanges(
        inputs, [&](Value result, const ConstantIntRanges &range) {
          ranges.try_emplace(result, range);
        });
    visiting.erase(value);
    auto found = ranges.find(value);
    return found == ranges.end()
               ? std::nullopt
               : std::optional<ConstantIntRanges>(found->second);
  };
  return infer(value, infer);
}

/// Structural recognition shared by the marking and unrolling consumers.
/// `provenBackedges` names latch branches already proven to terminate, so an
/// enclosing loop can be recognized once its inner loops are: the DAG check
/// below would otherwise reject every nested sweep.
///
/// `forMarking` asks only whether the latch runs a bounded number of times,
/// not whether the loop can be replicated. IEEE 1800-2023 12.7.1 exits a
/// for-loop as soon as its test fails, and the induction variable here is an
/// SSA value no other process can modify. The marking therefore accepts a body
/// that leaves before the induction bound: `break` and `return` (12.8) only
/// leave the loop sooner, and neither does the `$finish` check that an inlined
/// call leaves behind (20.2), a terminator with no successors. It also accepts
/// other header computations and values used after the loop: none of them
/// can change how often the induction test passes. Replication also supports
/// loop-carried accumulators when their results leave via exit block arguments;
/// direct uses outside the loop still require a separate SSA repair.
static std::optional<BoundedLoop>
recognizeBoundedLoopImpl(Block *header,
                         const llvm::DenseSet<Operation *> &provenBackedges,
                         bool forMarking, ArrayRef<BoundedLoop> enclosing,
                         DominanceInfo &dominance) {
  if (header->getNumArguments() == 0)
    return std::nullopt;
  auto condition = dyn_cast<cf::CondBranchOp>(header->getTerminator());
  if (!condition)
    return std::nullopt;
  // The induction variable is the header argument the continuation test
  // reads; any other argument is a loop-carried value.
  Value tested;
  if (auto compare = condition.getCondition().getDefiningOp<arith::CmpIOp>()) {
    Value lhs = forMarking ? unwrapUnsignedInduction(compare.getLhs())
                          : compare.getLhs();
    Value rhs = forMarking ? unwrapUnsignedInduction(compare.getRhs())
                          : compare.getRhs();
    tested = isa_and_nonnull<BlockArgument>(lhs) &&
                     cast<BlockArgument>(lhs).getOwner() == header
                 ? lhs
                 : rhs;
  } else if (auto test = matchLogicInductionTest(condition.getCondition())) {
    if (auto fromBits = test->compared.getDefiningOp<sim::SimLogicFromBitsOp>())
      tested = fromBits.getInput();
    else
      tested =
          forMarking ? unwrapUnsignedInduction(test->compared) : test->compared;
  }
  auto induction = dyn_cast_or_null<BlockArgument>(tested);
  if (!induction || induction.getOwner() != header)
    return std::nullopt;
  bool fourState = isa_and_nonnull<sim::LogicType>(
      sim::getPackedScalarType(induction.getType()));
  bool directLogic = isa<sim::LogicType>(induction.getType());
  auto packedWidth = sim::getPackedWidth(induction.getType());
  if (!packedWidth ||
      (!isa<IntegerType>(induction.getType()) && !directLogic && !forMarking))
    return std::nullopt;
  unsigned inductionWidth = *packedWidth;
  unsigned inductionIndex = induction.getArgNumber();

  // The continuation test appears in two equivalent shapes. A two-state
  // comparison lowers to arith.cmpi directly. A comparison written against an
  // elaborated parameter keeps SystemVerilog's four-state form:
  //   from_bits(induction) -> logic.compare -> is_true
  // Both describe the same induction test. Accept either, but require the
  // four-state bound to have an empty unknown plane: an X in the bound makes
  // the comparison result unknown, which proves nothing about termination.
  llvm::SmallPtrSet<Operation *, 4> testOperations;
  std::optional<arith::CmpIPredicate> predicate;
  APInt limit;
  Value limitValue;
  APInt limitMin, limitMax;
  if (auto compare = condition.getCondition().getDefiningOp<arith::CmpIOp>()) {
    bool reversed = (forMarking ? unwrapUnsignedInduction(compare.getRhs())
                               : compare.getRhs()) == induction;
    Value bound = reversed ? compare.getLhs() : compare.getRhs();
    Value compared = reversed ? compare.getRhs() : compare.getLhs();
    if (compare->getBlock() != header)
      return std::nullopt;
    predicate = reversed ? swapPredicate(compare.getPredicate())
                         : compare.getPredicate();
    if (!matchPattern(bound, m_ConstantInt(&limit))) {
      bool signedBound = *predicate == arith::CmpIPredicate::slt ||
                         *predicate == arith::CmpIPredicate::sgt;
      bool increasing = *predicate == arith::CmpIPredicate::slt ||
                        *predicate == arith::CmpIPredicate::ult;
      if (!signedBound && *predicate != arith::CmpIPredicate::ult &&
          *predicate != arith::CmpIPredicate::ugt)
        return std::nullopt;
      // Use dialect range interfaces for invariant expressions such as
      // `1 << i`. Their leaves must be constants or enclosing inductions;
      // neither a design load nor this loop's recurrence is an invariant.
      // Expressions may be rematerialized in the header by CFG threading.
      auto range = inferInvariantRange(bound, header, enclosing, dominance);
      if (!range)
        return std::nullopt;
      limitValue = bound;
      limitMin = signedBound ? range->smin() : range->umin();
      limitMax = signedBound ? range->smax() : range->umax();
      limit = increasing ? limitMax : limitMin;
    }
    if (compared != induction) {
      // The recurrence still wraps at its storage width, even when the
      // two-state comparison sees a flattened, zero-extended counter.
      // Restrict this form to unsigned tests with a representable bound.
      if (limitValue ||
          (*predicate != arith::CmpIPredicate::ult &&
           *predicate != arith::CmpIPredicate::ule &&
           *predicate != arith::CmpIPredicate::ugt &&
           *predicate != arith::CmpIPredicate::uge &&
           *predicate != arith::CmpIPredicate::eq &&
           *predicate != arith::CmpIPredicate::ne) ||
          !limit.isIntN(inductionWidth))
        return std::nullopt;
    }
    testOperations.insert(compare.getOperation());
  } else if (auto test = matchLogicInductionTest(condition.getCondition())) {
    if (llvm::any_of(test->operations, [&](Operation *operation) {
          return operation->getBlock() != header;
        }))
      return std::nullopt;
    auto fromBits = test->compared.getDefiningOp<sim::SimLogicFromBitsOp>();
    if ((!fourState && (!fromBits || fromBits.getInput() != induction)) ||
        (fourState && (forMarking ? unwrapUnsignedInduction(test->compared)
                                  : test->compared) != induction))
      return std::nullopt;
    switch (test->kind) {
    case sim::CompareKind::SLT:
      predicate = arith::CmpIPredicate::slt;
      break;
    case sim::CompareKind::SLE:
      predicate = arith::CmpIPredicate::sle;
      break;
    case sim::CompareKind::SGT:
      predicate = arith::CmpIPredicate::sgt;
      break;
    case sim::CompareKind::SGE:
      predicate = arith::CmpIPredicate::sge;
      break;
    case sim::CompareKind::ULT:
      predicate = arith::CmpIPredicate::ult;
      break;
    case sim::CompareKind::ULE:
      predicate = arith::CmpIPredicate::ule;
      break;
    case sim::CompareKind::UGT:
      predicate = arith::CmpIPredicate::ugt;
      break;
    case sim::CompareKind::UGE:
      predicate = arith::CmpIPredicate::uge;
      break;
    case sim::CompareKind::Eq:
      predicate = arith::CmpIPredicate::eq;
      break;
    case sim::CompareKind::Ne:
      predicate = arith::CmpIPredicate::ne;
      break;
    default:
      // Case/wildcard forms are not induction comparisons.
      return std::nullopt;
    }
    limit = test->bound;
    if (fourState && !(directLogic && test->compared == induction &&
                       limit.getBitWidth() == inductionWidth)) {
      // The comparison sees a zero-extended unsigned counter. Retain the
      // recurrence's storage width, rather than proving a wider nonwrapping
      // sequence while its packed backedge actually wraps.
      if ((*predicate != arith::CmpIPredicate::ult &&
           *predicate != arith::CmpIPredicate::ule &&
           *predicate != arith::CmpIPredicate::ugt &&
           *predicate != arith::CmpIPredicate::uge &&
           *predicate != arith::CmpIPredicate::eq &&
           *predicate != arith::CmpIPredicate::ne) ||
          !limit.isIntN(inductionWidth))
        return std::nullopt;
    }
    testOperations.insert(test->operations.begin(), test->operations.end());
    if (fromBits)
      testOperations.insert(fromBits.getOperation());
  } else {
    return std::nullopt;
  }
  // The induction variable and the limit are compared as one width.
  if (limit.getBitWidth() != inductionWidth)
    limit = limit.zextOrTrunc(inductionWidth);
  bool signedCompare = *predicate == arith::CmpIPredicate::slt ||
                       *predicate == arith::CmpIPredicate::sle ||
                       *predicate == arith::CmpIPredicate::sgt ||
                       *predicate == arith::CmpIPredicate::sge;
  if (!forMarking)
    for (Operation &op : header->without_terminator())
      if (!testOperations.contains(&op) &&
          !isa<arith::ConstantOp, sim::SimLogicConstantOp>(op))
        return std::nullopt;

  // The entry predecessor is a plain branch for a top-level loop, but a
  // conditional branch whenever the loop is nested inside another sweep or
  // guarded by an `if` -- the two shapes RTL reset and initialization loops
  // actually take. Folding an empty `if` join into the header leaves several
  // entry edges; they are one entry when all start at the same SSA value or
  // constant. Identify the latch structurally, since an invariant initializer
  // need not be a constant. The latch must stay a plain branch: a conditional
  // backedge would be a second exit that the termination argument does not
  // cover.
  SmallVector<Operation *, 2> entryOps;
  Value entryValue;
  cf::BranchOp entry, latch;
  for (auto edge = header->pred_begin(); edge != header->pred_end(); ++edge) {
    Block *pred = *edge;
    Operation *terminator = pred->getTerminator();
    Value incoming;
    if (auto branch = dyn_cast<cf::BranchOp>(terminator)) {
      if (branch.getDestOperands().size() != header->getNumArguments())
        return std::nullopt;
      incoming = branch.getDestOperands()[inductionIndex];
      if (dominance.dominates(header, pred)) {
        if (latch)
          return std::nullopt;
        latch = branch;
        continue;
      }
    } else if (auto branch = dyn_cast<cf::CondBranchOp>(terminator)) {
      if (dominance.dominates(header, pred))
        return std::nullopt;
      // Both edges may reach this header with different carried state. Only
      // their induction initializers must agree; inspect each actual edge.
      OperandRange operands = edge.getSuccessorIndex() == 0
                                  ? branch.getTrueDestOperands()
                                  : branch.getFalseDestOperands();
      if (operands.size() != header->getNumArguments())
        return std::nullopt;
      incoming = operands[inductionIndex];
    } else {
      return std::nullopt;
    }
    if (entryValue && entryValue != incoming) {
      APInt previous, current;
      if (!matchInductionConstant(entryValue, previous) ||
          !matchInductionConstant(incoming, current) || previous != current)
        return std::nullopt;
    }
    entryOps.push_back(terminator);
    entryValue = incoming;
  }
  if (entryOps.empty() || !latch)
    return std::nullopt;
  // Replication rewrites a single plain entry branch in place.
  if (entryOps.size() == 1)
    entry = dyn_cast<cf::BranchOp>(entryOps.front());
  Value increment = latch.getDestOperands()[inductionIndex];
  APInt stride;
  bool isAdd;
  if (fourState) {
    // IEEE 1800-2023 11.6.1 and 12.7.1: a known direct logic induction
    // follows the same fixed-width recurrence as an integer induction. Prove
    // its initializer, bound and step have empty unknown planes before using
    // that fact. Replication and signed tests require the comparison and the
    // update at the original width; the older unsigned marking path may also
    // recognize zero-extension/truncation wrappers.
    bool directStep = !forMarking || signedCompare;
    Value arithmetic = increment;
    while (!directStep) {
      Operation *op = arithmetic.getDefiningOp();
      if (!op)
        break;
      if (isa<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(op)) {
        arithmetic = op->getOperand(0);
        continue;
      }
      if (auto resize = dyn_cast<sim::SimLogicResizeOp>(op)) {
        if (cast<sim::LogicType>(resize.getInput().getType()).getWidth() <
                inductionWidth ||
            cast<sim::LogicType>(resize.getType()).getWidth() < inductionWidth)
          return std::nullopt;
        arithmetic = resize.getInput();
        continue;
      }
      // A truncation is canonically the low slice (11.6.1), with the same
      // width condition as a truncating resize.
      if (auto slice = dyn_cast<sim::SimLogicExtractOp>(op);
          slice && slice.getLowBit() == 0) {
        if (slice.getInput().getType().getWidth() < inductionWidth ||
            slice.getType().getWidth() < inductionWidth)
          return std::nullopt;
        arithmetic = slice.getInput();
        continue;
      }
      break;
    }
    auto binary = arithmetic.getDefiningOp<sim::SimLogicBinaryOp>();
    if (!binary || (binary.getKind() != sim::BinaryKind::Add &&
                    binary.getKind() != sim::BinaryKind::Sub))
      return std::nullopt;
    if (directStep && binary.getType() != induction.getType())
      return std::nullopt;
    isAdd = binary.getKind() == sim::BinaryKind::Add;
    auto isInduction = [&](Value value) {
      return (directStep ? value : unwrapUnsignedInduction(value)) == induction;
    };
    Value step;
    if (isInduction(binary.getLhs()))
      step = binary.getRhs();
    else if (isAdd && isInduction(binary.getRhs()))
      step = binary.getLhs();
    else
      return std::nullopt;
    if (!matchInductionConstant(step, stride))
      return std::nullopt;
    if (directStep && stride.getBitWidth() != inductionWidth)
      return std::nullopt;
    stride = stride.zextOrTrunc(inductionWidth);
  } else {
    Value arithmetic = increment;
    if (forMarking)
      while (Operation *op = arithmetic.getDefiningOp()) {
        if (isa<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(op)) {
          arithmetic = op->getOperand(0);
          continue;
        }
        if (auto truncate = dyn_cast<arith::TruncIOp>(op)) {
          if (truncate.getOverflowFlags() != arith::IntegerOverflowFlags::none ||
              cast<IntegerType>(truncate.getType()).getWidth() < inductionWidth)
            return std::nullopt;
          arithmetic = truncate.getIn();
          continue;
        }
        break;
      }
    auto add = arithmetic.getDefiningOp<arith::AddIOp>();
    auto sub = arithmetic.getDefiningOp<arith::SubIOp>();
    auto isInduction = [&](Value value) {
      return (forMarking ? unwrapUnsignedInduction(value) : value) == induction;
    };
    Value step;
    if (add && isInduction(add.getLhs()))
      step = add.getRhs();
    else if (add && isInduction(add.getRhs()))
      step = add.getLhs();
    else if (sub && isInduction(sub.getLhs()))
      step = sub.getRhs();
    else
      return std::nullopt;
    if (!matchPattern(step, m_ConstantInt(&stride)))
      return std::nullopt;
    isAdd = static_cast<bool>(add);
    stride = stride.zextOrTrunc(inductionWidth);
  }

  BoundedLoop loop;
  loop.header = header;
  loop.induction = induction;
  loop.condition = condition;
  loop.entry = entry;
  loop.latch = latch;
  loop.exit = condition.getFalseDest();
  loop.limit = limit;
  loop.limitValue = limitValue;
  loop.limitMin = limitValue ? limitMin : limit;
  loop.limitMax = limitValue ? limitMax : limit;
  loop.stride = stride;
  loop.predicate = *predicate;
  loop.isAdd = isAdd;
  loop.signedCompare = signedCompare;
  if (!matchInductionConstant(entryValue, loop.init)) {
    if (fourState || loop.predicate == arith::CmpIPredicate::eq ||
        loop.predicate == arith::CmpIPredicate::ne)
      return std::nullopt;
    auto range = inferInvariantRange(entryValue, header, enclosing, dominance);
    if (!range || llvm::any_of(entryOps, [&](Operation *entryOp) {
          return !dominance.dominates(entryValue, entryOp);
        }))
      return std::nullopt;
    loop.initValue = entryValue;
    loop.initMin = loop.signedCompare ? range->smin() : range->umin();
    loop.initMax = loop.signedCompare ? range->smax() : range->umax();
    bool increasing = loop.predicate == arith::CmpIPredicate::slt ||
                      loop.predicate == arith::CmpIPredicate::sle ||
                      loop.predicate == arith::CmpIPredicate::ult ||
                      loop.predicate == arith::CmpIPredicate::ule;
    loop.init = increasing ? loop.initMin : loop.initMax;
  } else {
    if (directLogic && loop.init.getBitWidth() != inductionWidth)
      return std::nullopt;
    loop.initMin = loop.initMax = loop.init;
  }

  DenseSet<Block *> naturalMembers{header};
  if (forMarking) {
    // A break can bypass the header's normal exit block and join a later
    // continuation. Bound the natural recurrence, not that continuation (it
    // may belong to an enclosing loop). Every residual cycle, including a
    // nonterminating exit path, is checked separately by hasFiniteControlFlow.
    SmallVector<Block *> reverse{latch->getBlock()};
    while (!reverse.empty()) {
      Block *block = reverse.pop_back_val();
      if (!naturalMembers.insert(block).second)
        continue;
      if (!dominance.dominates(header, block))
        return std::nullopt;
      llvm::append_range(reverse, block->getPredecessors());
    }
  }
  loop.members.insert(header);
  loop.blocks.push_back(header);
  SmallVector<Block *> pending{condition.getTrueDest()};
  loop.operations = header->getOperations().size();
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    if (llvm::any_of(entryOps, [&](Operation *entryOp) {
          return entryOp->getBlock() == block;
        }))
      return std::nullopt;
    if (forMarking && !naturalMembers.contains(block))
      continue;
    if (block == loop.exit)
      return std::nullopt;
    if (!loop.members.insert(block).second)
      continue;
    Operation *terminator = block->getTerminator();
    bool leavesProcedure = forMarking && terminator->getNumSuccessors() == 0;
    if (block->getParent() != header->getParent() ||
        (!leavesProcedure &&
         !isa<cf::BranchOp, cf::CondBranchOp, cf::SwitchOp>(terminator)))
      return std::nullopt;
    loop.blocks.push_back(block);
    loop.operations += block->getOperations().size();
    for (Operation &op : *block)
      if (op.getNumRegions() != 0)
        return std::nullopt;
    for (Block *successor : block->getSuccessors())
      if (successor != header)
        pending.push_back(successor);
  }
  if (!loop.members.contains(latch->getBlock()) ||
      loop.members.contains(loop.exit))
    return std::nullopt;
  if (limitValue) {
    if (!dominance.dominates(limitValue, header->getTerminator()))
      return std::nullopt;
  }
  llvm::DenseMap<Block *, unsigned> indegree;
  for (Block *block : loop.blocks) {
    if (block == header)
      continue;
    for (Block *pred : block->getPredecessors()) {
      if (!loop.members.contains(pred))
        return std::nullopt;
      ++indegree[block];
    }
  }
  // Removing this backedge, and any inner backedge already proven to terminate,
  // must leave a DAG -- not another loop that merely happens to be reachable
  // from this constant induction header.
  auto ignoredEdge = [&](Block *block, Block *successor) {
    if (successor == header)
      return true;
    auto branch = dyn_cast<cf::BranchOp>(block->getTerminator());
    return branch && provenBackedges.contains(branch.getOperation()) &&
           branch.getDest() == successor;
  };
  for (Block *block : loop.blocks)
    for (Block *successor : block->getSuccessors())
      if (successor != header && loop.members.contains(successor) &&
          ignoredEdge(block, successor))
        --indegree[successor];
  pending = {header};
  unsigned visited = 0;
  llvm::DenseSet<Block *> seen{header};
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    ++visited;
    for (Block *successor : block->getSuccessors())
      if (loop.members.contains(successor) && !ignoredEdge(block, successor) &&
          --indegree[successor] == 0 && seen.insert(successor).second)
        pending.push_back(successor);
  }
  if (visited != loop.blocks.size())
    return std::nullopt;
  // Replication rewrites each value defined in the loop; marking does not.
  if (!forMarking)
    for (Block *block : loop.blocks) {
      auto escapes = [&](Value value) {
        return llvm::any_of(value.getUsers(), [&](Operation *user) {
          return !loop.members.contains(user->getBlock());
        });
      };
      if (llvm::any_of(block->getArguments(), escapes))
        return std::nullopt;
      for (Operation &op : *block)
        if (llvm::any_of(op.getResults(), escapes))
          return std::nullopt;
    }
  if (!terminatesInFiniteSteps(loop))
    return std::nullopt;
  return loop;
}

std::optional<BoundedLoop>
recognizeBoundedLoop(Block *header,
                     const llvm::DenseSet<Operation *> &provenBackedges,
                     bool forMarking, ArrayRef<BoundedLoop> enclosing) {
  DominanceInfo dominance;
  return recognizeBoundedLoopImpl(header, provenBackedges, forMarking,
                                  enclosing, dominance);
}

SmallVector<BoundedLoop, 0> analyzeBoundedLoops(Region &region) {
  SmallVector<BoundedLoop, 0> loops;
  if (region.empty() || region.hasOneBlock())
    return loops;
  DominanceInfo dominance;
  // Neither proof sweep mutates the CFG. Share this tree across every loop
  // and invariant expression instead of rebuilding it for each recurrence.
  CFGLoopInfo loopInfo(dominance.getDomTree(&region));
  llvm::DenseSet<Operation *> provenBackedges;
  // LLVM's generic LoopInfo supplies natural-loop headers and nesting. Visit
  // children before parents once, instead of rescanning every block until the
  // nesting proof converges. Keep the fixed-width induction/early-exit proof:
  // LoopInfo alone says nothing about termination or four-state conditions.
  // Its loop membership also excludes terminating side paths, which the
  // recognizer must still check for unsupported control or residual cycles.
  auto preorder = loopInfo.getLoopsInPreorder();
  // A finishing sweep on a break path is a sibling natural loop, but its
  // initializer may still use the enclosing iteration's true-edge range.
  // Dominator depth visits every such defining header before its consumers.
  llvm::stable_sort(preorder, [&](CFGLoop *a, CFGLoop *b) {
    return dominance.getNode(a->getHeader())->getLevel() <
           dominance.getNode(b->getHeader())->getLevel();
  });
  // Discover induction envelopes from the outside in. Cutting natural inner
  // latches here establishes only each envelope, not termination of a body.
  // The second, inside-out pass validates every nested cycle before exposing
  // a termination certificate. This avoids circular termination assumptions
  // for triangular loops such as `for (k = 0; k < i; ++k)`.
  DenseSet<Operation *> naturalBackedges;
  for (CFGLoop *natural : preorder)
    if (Block *latch = natural->getLoopLatch())
      naturalBackedges.insert(latch->getTerminator());
  SmallVector<BoundedLoop, 0> envelopes;
  for (CFGLoop *natural : preorder)
    if (natural->getLoopLatch())
      if (auto loop =
              recognizeBoundedLoopImpl(natural->getHeader(), naturalBackedges,
                                       true, envelopes, dominance))
        envelopes.push_back(std::move(*loop));
  for (CFGLoop *natural : llvm::reverse(preorder)) {
    if (!natural->getLoopLatch())
      continue;
    auto loop = recognizeBoundedLoopImpl(natural->getHeader(), provenBackedges,
                                         true, envelopes, dominance);
    if (!loop)
      continue;
    provenBackedges.insert(loop->latch.getOperation());
    loops.push_back(std::move(*loop));
  }
  return loops;
}

SmallVector<BoundedLoop, 0> analyzeActivationBoundedLoops(Region &region) {
  if (llvm::none_of(region, [](Block &block) {
        return sim::isSuspensionOp(block.getTerminator());
      }))
    return analyzeBoundedLoops(region);
  IRMapping mapping;
  OwningOpRef<Operation *> copy(
      region.getParentOp()->cloneWithoutRegions(mapping));
  Region &activation = copy->getRegion(region.getRegionNumber());
  region.cloneInto(&activation, mapping);
  IRMapping originals;
  for (auto [from, to] : mapping.getValueMap())
    originals.map(to, from);
  for (auto [from, to] : mapping.getBlockMap())
    originals.map(to, from);
  for (auto [from, to] : mapping.getOperationMap())
    originals.map(to, from);
  for (Block &block : activation) {
    auto *term = block.getTerminator();
    if (!sim::isSuspensionOp(term))
      continue;
    OpBuilder builder(term);
    sim::SimReturnOp::create(builder, term->getLoc(), ValueRange{});
    term->erase();
  }
  IRRewriter rewriter(region.getContext());
  (void)eraseUnreachableBlocks(rewriter, activation);
  auto loops = analyzeBoundedLoops(activation);
  for (auto &loop : loops) {
    loop.header = originals.lookup(loop.header);
    loop.induction = cast<BlockArgument>(originals.lookup(loop.induction));
    if (loop.initValue)
      loop.initValue = originals.lookup(loop.initValue);
    if (loop.limitValue)
      loop.limitValue = originals.lookup(loop.limitValue);
    loop.condition =
        cast<cf::CondBranchOp>(originals.lookup(loop.condition.getOperation()));
    if (loop.entry)
      loop.entry =
          cast<cf::BranchOp>(originals.lookup(loop.entry.getOperation()));
    loop.latch =
        cast<cf::BranchOp>(originals.lookup(loop.latch.getOperation()));
    loop.exit = originals.lookup(loop.exit);
    loop.members.clear();
    for (Block *&block : loop.blocks) {
      block = originals.lookup(block);
      loop.members.insert(block);
    }
  }
  return loops;
}

bool hasFiniteControlFlow(Region &region) {
  DenseSet<Operation *> latches;
  for (const auto &loop : analyzeBoundedLoops(region))
    latches.insert(loop.latch);
  DenseMap<Block *, unsigned> incoming;
  for (Block &block : region)
    incoming[&block] = 0;
  for (Block &block : region) {
    Operation *term = block.getTerminator();
    if (isa<sim::SimReturnOp>(term) || latches.contains(term))
      continue;
    if (!isa<cf::BranchOp, cf::CondBranchOp, cf::SwitchOp>(term))
      return false;
    for (Block *next : block.getSuccessors())
      ++incoming[next];
  }
  SmallVector<Block *> ready;
  for (Block &block : region)
    if (!incoming[&block])
      ready.push_back(&block);
  for (size_t i = 0; i < ready.size(); ++i) {
    Operation *term = ready[i]->getTerminator();
    if (isa<sim::SimReturnOp>(term) || latches.contains(term))
      continue;
    for (Block *next : ready[i]->getSuccessors())
      if (!--incoming[next])
        ready.push_back(next);
  }
  return ready.size() == incoming.size();
}

} // namespace obelisk::analysis
