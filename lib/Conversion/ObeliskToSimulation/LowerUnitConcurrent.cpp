//===- LowerUnitConcurrent.cpp - AOT concurrent assertion monitors -------===//
//
// Compiles the bounded single-clock SVA slice into ordinary simulation SSA.
// Runtime state is a compact bitset carried across clock suspensions; the
// runtime has no temporal interpreter and no solver dependency.
//
//===----------------------------------------------------------------------===//

#include "LowerUnit.h"

#include "obelisk/Solver/ConstraintSolver.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"

#include <optional>
#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

/// One deterministic bounded sequence. Each clock age contains the boolean
/// expressions that must all hold at that age. Empty ages represent ## gaps.
struct FixedSequenceAge {
  struct CaseGuard {
    Operation *selector = nullptr;
    Operation *label = nullptr;
    bool negated = false;
  };

  SmallVector<Operation *, 2> predicates;
  SmallVector<Operation *, 2> negatedPredicates;
  SmallVector<CaseGuard, 2> caseGuards;
  SmallVector<Operation *, 2> matchItems;
};

/// A successful trace crossing `age` selects this first_match group and
/// cancels the group's alternatives that would end later. The initial bounded
/// implementation admits one such boundary per trace; that covers a
/// first_match sequence followed by an ordinary bounded continuation without
/// conflating independent or nested priority scopes.
struct FirstMatchBoundary {
  Operation *group = nullptr;
  uint64_t age = 0;
};

struct FixedSequence {
  SmallVector<FixedSequenceAge, 8> ages;
  SmallVector<FirstMatchBoundary, 1> firstMatchBoundaries;
  bool vacuousSuccess = false;
};

struct BooleanMinimizationStats {
  size_t alternativesBefore = 0;
  size_t alternativesAfter = 0;
  size_t literalsBefore = 0;
  size_t literalsAfter = 0;
  uint64_t solverQueries = 0;
  StringRef backend;
};

/// One nonempty maximal clocked subsequence in the deliberately small
/// multi-clock slice below. Each stage is reached through a source `##1`, so
/// an attempt waits for the next occurrence of `clock` before evaluating its
/// predicates.
struct MultiClockSequenceStage {
  Operation *clock = nullptr;
  SmallVector<Operation *, 2> predicates;
};

struct MultiClockSequence {
  SmallVector<MultiClockSequenceStage, 4> stages;
  bool changesClock = false;
  bool hasLeadingDelay = false;
};

/// A common persistent repetition shape compiled to an aggregate token DFA.
/// `entry` is the deterministic bounded prefix ending at the first clock where
/// `term` is sampled. A terminal term, when present, is sampled one clock after
/// each eligible goto/nonconsecutive endpoint.
struct PersistentRepetitionSequence {
  FixedSequence entry;
  FixedSequenceAge term;
  FixedSequenceAge terminal;
  semantic::SVSequenceRepetitionKind kind =
      semantic::SVSequenceRepetitionKind::Consecutive;
  uint64_t minimum = 0;
  uint64_t maximum = 0;
  bool hasTerminal = false;
};

using FixedSequenceAlternatives = SmallVector<FixedSequence, 4>;

static constexpr size_t maxFixedSequenceAlternatives = 256;

static bool isSingleBooleanAge(const FixedSequence &sequence) {
  return sequence.ages.size() == 1 &&
         sequence.ages.front().matchItems.empty() &&
         sequence.ages.front().caseGuards.empty() &&
         sequence.ages.front().predicates.size() +
                 sequence.ages.front().negatedPredicates.size() ==
             1;
}

static FixedSequence negateSingleBooleanSequence(FixedSequence sequence) {
  assert(isSingleBooleanAge(sequence));
  std::swap(sequence.ages.front().predicates,
            sequence.ages.front().negatedPredicates);
  return sequence;
}

/// Minimize a same-endpoint property DNF before materializing monitor state.
/// Temporal endpoint identity is deliberately outside the propositional
/// solver: alternatives with different lengths, first_match boundaries, or
/// match-item effects retain their exact source multiplicity and ordering.
static std::optional<BooleanMinimizationStats>
minimizeBooleanAlternatives(FixedSequenceAlternatives &alternatives) {
  if (alternatives.size() < 2)
    return std::nullopt;
  size_t horizon = alternatives.front().ages.size();
  bool vacuousSuccess = alternatives.front().vacuousSuccess;
  if (horizon == 0 ||
      llvm::any_of(alternatives, [&](const FixedSequence &alternative) {
        return alternative.ages.size() != horizon ||
               alternative.vacuousSuccess != vacuousSuccess ||
               !alternative.firstMatchBoundaries.empty() ||
               llvm::any_of(alternative.ages, [](const FixedSequenceAge &age) {
                 return !age.matchItems.empty();
               });
      }))
    return std::nullopt;

  struct Atom {
    size_t age = 0;
    Operation *predicate = nullptr;
    FixedSequenceAge::CaseGuard caseGuard;
  };
  DenseMap<std::pair<size_t, Operation *>, uint32_t> atomIds;
  DenseMap<std::pair<size_t, Attribute>, uint32_t> namedAtomIds;
  DenseMap<std::pair<size_t, std::pair<Operation *, Operation *>>, uint32_t>
      caseAtomIds;
  SmallVector<Atom> atoms;
  auto getAtom = [&](size_t age, Operation *predicate) {
    if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(predicate)) {
      std::pair<size_t, Attribute> key{age, named.getReferencedSymbolAttr()};
      auto found = namedAtomIds.find(key);
      if (found != namedAtomIds.end())
        return found->second;
      uint32_t id = static_cast<uint32_t>(atoms.size());
      namedAtomIds.try_emplace(key, id);
      atoms.push_back({age, predicate, {}});
      return id;
    }
    std::pair<size_t, Operation *> atom{age, predicate};
    auto [found, inserted] =
        atomIds.try_emplace(atom, static_cast<uint32_t>(atoms.size()));
    if (inserted)
      atoms.push_back({age, predicate, {}});
    return found->second;
  };
  auto getCaseAtom = [&](size_t age, const FixedSequenceAge::CaseGuard &guard) {
    std::pair<size_t, std::pair<Operation *, Operation *>> key{
        age, {guard.selector, guard.label}};
    auto [found, inserted] =
        caseAtomIds.try_emplace(key, static_cast<uint32_t>(atoms.size()));
    if (inserted)
      atoms.push_back({age, nullptr, {guard.selector, guard.label, false}});
    return found->second;
  };

  BooleanMinimizationStats stats;
  stats.alternativesBefore = alternatives.size();
  std::vector<solver::BooleanCube> cubes;
  cubes.reserve(alternatives.size());
  for (const FixedSequence &alternative : alternatives) {
    solver::BooleanCube cube;
    for (auto [age, predicates] : llvm::enumerate(alternative.ages)) {
      for (Operation *predicate : predicates.predicates)
        cube.push_back({getAtom(age, predicate), false});
      for (Operation *predicate : predicates.negatedPredicates)
        cube.push_back({getAtom(age, predicate), true});
      for (const FixedSequenceAge::CaseGuard &guard : predicates.caseGuards)
        cube.push_back({getCaseAtom(age, guard), guard.negated});
    }
    stats.literalsBefore += cube.size();
    cubes.push_back(std::move(cube));
  }

  solver::BooleanDNFAnalysis analysis =
      solver::minimizeBooleanDNF(std::move(cubes));
  // An empty DNF is exactly false. Keep the original contradictory trace so
  // the normal monitor reports failure instead of misdiagnosing compilation.
  if (analysis.cubes.empty())
    return std::nullopt;

  FixedSequenceAlternatives minimized;
  minimized.reserve(analysis.cubes.size());
  for (const solver::BooleanCube &cube : analysis.cubes) {
    FixedSequence alternative;
    alternative.ages.resize(horizon);
    alternative.vacuousSuccess = vacuousSuccess;
    for (solver::BooleanLiteral literal : cube) {
      if (literal.variable >= atoms.size())
        return std::nullopt;
      const Atom &atom = atoms[literal.variable];
      if (atom.predicate) {
        (literal.negated ? alternative.ages[atom.age].negatedPredicates
                         : alternative.ages[atom.age].predicates)
            .push_back(atom.predicate);
      } else {
        FixedSequenceAge::CaseGuard guard = atom.caseGuard;
        guard.negated = literal.negated;
        alternative.ages[atom.age].caseGuards.push_back(guard);
      }
      ++stats.literalsAfter;
    }
    minimized.push_back(std::move(alternative));
  }
  stats.alternativesAfter = minimized.size();
  stats.solverQueries = analysis.solverQueries;
  stats.backend = analysis.backend;
  alternatives = std::move(minimized);
  return stats;
}

/// Diagnose SVA forms that the bounded monitor compiler intentionally leaves
/// unsupported. Keep these messages tied to the semantic construct instead of
/// reporting the generic fixed-trace compilation failure: users need to know
/// whether a property is malformed, exceeds a bounded implementation limit,
/// or requires a temporal semantic that has not been implemented yet.
static bool diagnoseUnsupportedConcurrentFeature(Operation *operation,
                                                 bool nested = false) {
  bool diagnosed = false;
  operation->walk([&](Operation *current) -> WalkResult {
    auto diagnose = [&](const Twine &message) {
      emitError(getSemanticLocation(current)) << message;
      diagnosed = true;
      return WalkResult::interrupt();
    };

    auto diagnoseRepetition = [&](semantic::SVSequenceRepetitionKind kind,
                                  bool unbounded,
                                  std::optional<int64_t> minimum) {
      if (kind == semantic::SVSequenceRepetitionKind::Nonconsecutive) {
        if (nested) {
          return diagnose(
              "nonconsecutive sequence repetition [=] does not yet compose "
              "with an implication antecedent or consequent");
        }
        return diagnose(
            "nonconsecutive sequence repetition [=] currently requires a "
            "positive finite range no greater than 63 on one boolean term, "
            "optionally preceded by a deterministic bounded prefix and "
            "followed by ##1 plus one boolean term");
      }
      if (kind == semantic::SVSequenceRepetitionKind::GoTo) {
        if (nested) {
          return diagnose(
              "goto sequence repetition [->] does not yet compose with an "
              "implication antecedent or consequent");
        }
        return diagnose(
            "goto sequence repetition [->] currently requires a positive "
            "finite range no greater than 63 on one boolean term, optionally "
            "preceded by a deterministic bounded prefix and followed by ##1 "
            "plus one boolean term");
      }
      if (unbounded)
        return diagnose(
            "unbounded consecutive sequence repetition [*...:$] is not "
            "executable yet");
      if (minimum && *minimum == 0)
        return diagnose("empty consecutive sequence repetition [*0...] is not "
                        "executable yet");
      return WalkResult::advance();
    };

    if (auto simple = dyn_cast<semantic::SVSimpleAssertionExprOp>(current);
        simple && simple.getHasRepetition()) {
      if (!simple.getRepetitionKind())
        return diagnose("sequence repetition is missing its repetition kind");
      WalkResult result = diagnoseRepetition(*simple.getRepetitionKind(),
                                             simple.getRepetitionIsUnbounded(),
                                             simple.getRepetitionMin());
      if (result.wasInterrupted())
        return result;
    }
    if (auto matched = dyn_cast<semantic::SVSequenceWithMatchExprOp>(current);
        matched && matched.getHasRepetition()) {
      if (!matched.getRepetitionKind())
        return diagnose("sequence repetition is missing its repetition kind");
      WalkResult result = diagnoseRepetition(*matched.getRepetitionKind(),
                                             matched.getRepetitionIsUnbounded(),
                                             matched.getRepetitionMin());
      if (result.wasInterrupted())
        return result;
    }
    if (auto concat = dyn_cast<semantic::SVSequenceConcatExprOp>(current)) {
      for (Attribute delayAttr : concat.getDelays()) {
        auto delay = dyn_cast<DictionaryAttr>(delayAttr);
        if (delay && delay.getAs<BoolAttr>("is_unbounded") &&
            delay.getAs<BoolAttr>("is_unbounded").getValue())
          return diagnose("unbounded sequence delay ##[...] is not executable "
                          "yet");
      }
    }
    if (auto first = dyn_cast<semantic::SVFirstMatchAssertionExprOp>(current)) {
      if (first.getMatchItemCount() != 0)
        return diagnose("bounded first_match does not yet support match items");
      bool persistent = false;
      first->walk([&](semantic::SVSimpleAssertionExprOp simple) {
        persistent |= simple.getHasRepetition() && simple.getRepetitionKind() &&
                      (*simple.getRepetitionKind() ==
                           semantic::SVSequenceRepetitionKind::Nonconsecutive ||
                       *simple.getRepetitionKind() ==
                           semantic::SVSequenceRepetitionKind::GoTo);
      });
      if (persistent)
        return diagnose(
            "first_match over persistent [->]/[=] repetition is not "
            "executable yet; the aggregate token monitor currently applies "
            "property-level earliest-success semantics");
    }
    if (auto unary = dyn_cast<semantic::SVUnaryAssertionExprOp>(current)) {
      if (unary.getOperatorKind() == semantic::SVAssertionUnaryOperator::Not)
        return diagnose(
            "SVA property operator 'not' currently requires one "
            "deterministic one-cycle boolean operand without match items");
      if (unary.getOperatorKind() ==
              semantic::SVAssertionUnaryOperator::NextTime ||
          unary.getOperatorKind() ==
              semantic::SVAssertionUnaryOperator::SNextTime)
        return diagnose(
            Twine("SVA property operator '") +
            semantic::stringifySVAssertionUnaryOperator(
                unary.getOperatorKind()) +
            "' currently requires one fixed nonnegative delay and a bounded "
            "operand within the 63-cycle horizon");
      if (unary.getOperatorKind() ==
              semantic::SVAssertionUnaryOperator::Always ||
          unary.getOperatorKind() ==
              semantic::SVAssertionUnaryOperator::SAlways ||
          unary.getOperatorKind() ==
              semantic::SVAssertionUnaryOperator::Eventually ||
          unary.getOperatorKind() ==
              semantic::SVAssertionUnaryOperator::SEventually)
        return diagnose(
            Twine("SVA property operator '") +
            semantic::stringifySVAssertionUnaryOperator(
                unary.getOperatorKind()) +
            "' currently requires an explicit finite nonnegative range and "
            "bounded branches without match items");
      return diagnose(
          Twine("SVA property operator '") +
          semantic::stringifySVAssertionUnaryOperator(unary.getOperatorKind()) +
          "' is not executable yet");
    }
    if (auto binary = dyn_cast<semantic::SVBinaryAssertionExprOp>(current)) {
      switch (binary.getOperatorKind()) {
      case semantic::SVAssertionBinaryOperator::And:
      case semantic::SVAssertionBinaryOperator::Or:
      case semantic::SVAssertionBinaryOperator::Intersect:
      case semantic::SVAssertionBinaryOperator::Throughout:
      case semantic::SVAssertionBinaryOperator::Within:
        break;
      case semantic::SVAssertionBinaryOperator::OverlappedImplication:
      case semantic::SVAssertionBinaryOperator::NonOverlappedImplication:
      case semantic::SVAssertionBinaryOperator::OverlappedFollowedBy:
      case semantic::SVAssertionBinaryOperator::NonOverlappedFollowedBy:
        if (nested || current != operation)
          return diagnose(
              "nested SVA implication/followed-by is not executable yet; "
              "bounded antecedent operators are currently supported only as "
              "the outermost property operator");
        break;
      case semantic::SVAssertionBinaryOperator::Iff:
      case semantic::SVAssertionBinaryOperator::Implies:
        return diagnose(
            Twine("SVA property operator '") +
            semantic::stringifySVAssertionBinaryOperator(
                binary.getOperatorKind()) +
            "' currently requires deterministic one-cycle boolean operands "
            "without match items");
      default:
        return diagnose(Twine("SVA property operator '") +
                        semantic::stringifySVAssertionBinaryOperator(
                            binary.getOperatorKind()) +
                        "' is not executable yet");
      }
    }
    if (auto strong = dyn_cast<semantic::SVStrongWeakAssertionExprOp>(current))
      return diagnose(
          Twine("SVA '") +
          semantic::stringifySVAssertionStrength(strong.getStrength()) +
          "' end-of-simulation qualification is not executable "
          "yet");
    if (auto abort = dyn_cast<semantic::SVAbortAssertionExprOp>(current))
      return diagnose(
          Twine("SVA property operator '") +
          (abort.getIsSynchronous() ? "sync_" : "") +
          semantic::stringifySVAssertionAbortAction(abort.getAction()) +
          "_on' is not executable yet");
    if (isa<semantic::SVConditionalAssertionExprOp>(current))
      return diagnose(
          "conditional SVA properties currently require bounded branches "
          "without unsupported locals or temporal operators");
    if (isa<semantic::SVCaseAssertionExprOp>(current))
      return diagnose(
          "case SVA properties currently require ordinary case-equality "
          "labels and bounded branches without unsupported locals or "
          "temporal operators");
    if (isa<semantic::SVDisableIffAssertionExprOp>(current))
      return diagnose("nested disable iff is not executable yet; disable iff "
                      "is currently supported only at the outermost property");
    return WalkResult::advance();
  });
  return diagnosed;
}

static bool areEquivalentDirectClocks(Operation *left, Operation *right) {
  auto lhs = dyn_cast_or_null<semantic::SVSignalEventControlOp>(left);
  auto rhs = dyn_cast_or_null<semantic::SVSignalEventControlOp>(right);
  if (!lhs || !rhs || lhs.getHasIff() || rhs.getHasIff() ||
      lhs.getEdgeKind() != rhs.getEdgeKind())
    return false;
  SmallVector<Operation *> lhsChildren = getChildren(lhs);
  SmallVector<Operation *> rhsChildren = getChildren(rhs);
  if (lhsChildren.size() != 1 || rhsChildren.size() != 1)
    return false;
  // A member clock may have the same leaf path through two different dynamic
  // receivers (for example vif0.clk and vif1.clk). This initial multi-clock
  // slice deliberately accepts only statically identified storage, where an
  // exact semantic symbol is sufficient to prove clock identity.
  if (!isa<semantic::SVNamedValueExpressionOp,
           semantic::SVHierarchicalValueExpressionOp>(lhsChildren.front()) ||
      !isa<semantic::SVNamedValueExpressionOp,
           semantic::SVHierarchicalValueExpressionOp>(rhsChildren.front()))
    return false;
  auto lhsSymbol =
      lhsChildren.front()->getAttrOfType<SymbolRefAttr>("referenced_symbol");
  auto rhsSymbol =
      rhsChildren.front()->getAttrOfType<SymbolRefAttr>("referenced_symbol");
  return lhsSymbol && rhsSymbol && lhsSymbol == rhsSymbol;
}

static bool isStaticDirectClock(Operation *operation) {
  auto event = dyn_cast_or_null<semantic::SVSignalEventControlOp>(operation);
  if (!event || event.getHasIff())
    return false;
  SmallVector<Operation *> children = getChildren(event);
  return children.size() == 1 &&
         isa<semantic::SVNamedValueExpressionOp,
             semantic::SVHierarchicalValueExpressionOp>(children.front()) &&
         children.front()->hasAttr("referenced_symbol") &&
         isAddressableExpression(children.front());
}

/// Return the substituted body of a nonrecursive assertion invocation. The
/// remaining children are the source actual/default and local-initializer
/// inventory retained for semantic tooling; they are deliberately not
/// evaluated by the monitor compiler.
static FailureOr<Operation *>
getExpandedAssertionBody(semantic::SVAssertionInstanceExpressionOp instance) {
  if (instance.getIsRecursiveProperty() || !instance.getHasExpandedBody())
    return failure();

  size_t initializedLocals = llvm::count_if(
      instance.getLocalVariableHasInitializer(),
      [](int64_t hasInitializer) { return hasInitializer != 0; });
  SmallVector<Operation *> children = getChildren(instance);
  size_t expectedChildren = 1 + instance.getArgumentCount() + initializedLocals;
  if (children.size() != expectedChildren)
    return failure();
  return children.front();
}

static FailureOr<FixedSequence> compileFixedSequence(Operation *operation) {
  if (auto instance =
          dyn_cast<semantic::SVAssertionInstanceExpressionOp>(operation)) {
    FailureOr<Operation *> body = getExpandedAssertionBody(instance);
    if (failed(body))
      return failure();
    return compileFixedSequence(*body);
  }

  if (auto simple = dyn_cast<semantic::SVSimpleAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(simple);
    if (simple.getIsNull() || children.size() != 1)
      return failure();
    uint64_t repetitions = 1;
    if (simple.getHasRepetition()) {
      if (simple.getRepetitionKind() !=
              semantic::SVSequenceRepetitionKind::Consecutive ||
          simple.getRepetitionIsUnbounded() || !simple.getRepetitionMin() ||
          !simple.getRepetitionMax() ||
          *simple.getRepetitionMin() != *simple.getRepetitionMax() ||
          *simple.getRepetitionMin() <= 0)
        return failure();
      repetitions = static_cast<uint64_t>(*simple.getRepetitionMin());
    }
    FixedSequence nested;
    if (isa<semantic::SVAssertionInstanceExpressionOp>(children.front())) {
      FailureOr<FixedSequence> compiled =
          compileFixedSequence(children.front());
      if (failed(compiled))
        return failure();
      nested = std::move(*compiled);
    } else {
      nested.ages.resize(1);
      nested.ages.front().predicates.push_back(children.front());
    }
    if (nested.ages.empty() || repetitions > 63 / nested.ages.size())
      return failure();
    FixedSequence result;
    result.ages.reserve(repetitions * nested.ages.size());
    for (uint64_t index = 0; index < repetitions; ++index)
      llvm::append_range(result.ages, nested.ages);
    return result;
  }

  if (auto concat = dyn_cast<semantic::SVSequenceConcatExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(concat);
    ArrayAttr delays = concat.getDelays();
    if (children.empty() || delays.size() != children.size())
      return failure();
    FixedSequence result;
    for (auto [index, child] : llvm::enumerate(children)) {
      auto delay = dyn_cast<DictionaryAttr>(delays[index]);
      auto minimum = delay ? delay.getAs<IntegerAttr>("min") : IntegerAttr{};
      auto maximum = delay ? delay.getAs<IntegerAttr>("max") : IntegerAttr{};
      auto unbounded =
          delay ? delay.getAs<BoolAttr>("is_unbounded") : BoolAttr{};
      if (!minimum || !maximum || !unbounded || unbounded.getValue() ||
          minimum.getInt() < 0 || minimum.getInt() != maximum.getInt())
        return failure();
      FailureOr<FixedSequence> nested = compileFixedSequence(child);
      if (failed(nested))
        return failure();
      uint64_t offset = minimum.getInt();
      if (result.ages.empty())
        result.ages.resize(1);
      // A match item at the left endpoint executes before a ##0 successor is
      // evaluated. This fixed-age representation cannot preserve that
      // intra-Observed ordering, so do not flatten it into a conjunction.
      if (offset == 0 && !result.ages.back().matchItems.empty())
        return failure();
      uint64_t start = result.ages.size() - 1 + offset;
      uint64_t required = start + nested->ages.size();
      if (required > 63)
        return failure();
      result.ages.resize(std::max<uint64_t>(result.ages.size(), required));
      for (auto [age, nestedAge] : llvm::enumerate(nested->ages)) {
        llvm::append_range(result.ages[start + age].predicates,
                           nestedAge.predicates);
        llvm::append_range(result.ages[start + age].negatedPredicates,
                           nestedAge.negatedPredicates);
        llvm::append_range(result.ages[start + age].caseGuards,
                           nestedAge.caseGuards);
        llvm::append_range(result.ages[start + age].matchItems,
                           nestedAge.matchItems);
      }
    }
    return result;
  }

  if (auto matched = dyn_cast<semantic::SVSequenceWithMatchExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(matched);
    if (matched.getHasRepetition() || children.empty() ||
        children.size() != 1 + matched.getMatchItemCount())
      return failure();
    FailureOr<FixedSequence> nested = compileFixedSequence(children.front());
    if (failed(nested) || nested->ages.empty())
      return failure();
    llvm::append_range(nested->ages.back().matchItems,
                       ArrayRef<Operation *>(children).drop_front());
    return nested;
  }

  if (auto unary = dyn_cast<semantic::SVUnaryAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(unary);
    if (unary.getOperatorKind() != semantic::SVAssertionUnaryOperator::Not ||
        unary.getHasRange() || children.size() != 1)
      return failure();
    FailureOr<FixedSequence> nested = compileFixedSequence(children.front());
    if (failed(nested) || !isSingleBooleanAge(*nested))
      return failure();
    return negateSingleBooleanSequence(std::move(*nested));
  }

  return failure();
}

/// Compile the leading-##1 LRM multi-clock handoff form used by UVM's
/// conformance sequence into a short sequence of independently clocked stages.
/// This is intentionally narrower than the single-clock compiler: every
/// concatenation delay, including the leading delay, must be exactly ##1 and
/// every maximal clocked subsequence must be a nonempty boolean term. A
/// detached attempt actor can then wait on each clock in order without a
/// runtime temporal interpreter. In particular, a transition to a different
/// clock observes its nearest strictly subsequent tick, as required for ##1.
static FailureOr<MultiClockSequence>
compileMultiClockSequence(Operation *operation, Operation *inheritedClock) {
  if (auto instance =
          dyn_cast<semantic::SVAssertionInstanceExpressionOp>(operation)) {
    FailureOr<Operation *> body = getExpandedAssertionBody(instance);
    if (failed(body))
      return failure();
    return compileMultiClockSequence(*body, inheritedClock);
  }

  if (auto clocking =
          dyn_cast<semantic::SVClockingAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(clocking);
    if (children.size() != 2)
      return failure();
    Operation *clock = children.front();
    FailureOr<MultiClockSequence> nested =
        compileMultiClockSequence(children.back(), clock);
    if (failed(nested))
      return failure();
    nested->changesClock |=
        inheritedClock && !areEquivalentDirectClocks(inheritedClock, clock);
    return nested;
  }

  if (auto simple = dyn_cast<semantic::SVSimpleAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(simple);
    if (!inheritedClock || simple.getIsNull() || simple.getHasRepetition() ||
        children.size() != 1)
      return failure();
    if (isa<semantic::SVAssertionInstanceExpressionOp>(children.front()))
      return compileMultiClockSequence(children.front(), inheritedClock);
    MultiClockSequence result;
    result.stages.push_back({inheritedClock, {children.front()}});
    return result;
  }

  auto concat = dyn_cast<semantic::SVSequenceConcatExprOp>(operation);
  if (!concat || !inheritedClock)
    return failure();
  SmallVector<Operation *> children = getChildren(concat);
  ArrayAttr delays = concat.getDelays();
  if (children.empty() || delays.size() != children.size())
    return failure();

  MultiClockSequence result;
  result.hasLeadingDelay = true;
  Operation *previousClock = inheritedClock;
  for (auto [index, child] : llvm::enumerate(children)) {
    auto delay = dyn_cast<DictionaryAttr>(delays[index]);
    auto minimum = delay ? delay.getAs<IntegerAttr>("min") : IntegerAttr{};
    auto maximum = delay ? delay.getAs<IntegerAttr>("max") : IntegerAttr{};
    auto unbounded = delay ? delay.getAs<BoolAttr>("is_unbounded") : BoolAttr{};
    if (!minimum || !maximum || !unbounded || unbounded.getValue() ||
        minimum.getInt() != 1 || maximum.getInt() != 1)
      return failure();
    FailureOr<MultiClockSequence> nested =
        compileMultiClockSequence(child, inheritedClock);
    if (failed(nested) || nested->stages.empty())
      return failure();
    result.changesClock |= nested->changesClock;
    result.hasLeadingDelay |= nested->hasLeadingDelay;
    for (MultiClockSequenceStage &stage : nested->stages) {
      result.changesClock |=
          !areEquivalentDirectClocks(previousClock, stage.clock);
      previousClock = stage.clock;
      result.stages.push_back(std::move(stage));
    }
  }
  return result;
}

static LogicalResult appendFixedSequence(FixedSequence &result,
                                         const FixedSequence &nested,
                                         uint64_t offset) {
  if (nested.ages.empty())
    return failure();
  if (result.ages.empty())
    result.ages.resize(1);
  if (offset == 0 && !result.ages.back().matchItems.empty())
    return failure();
  uint64_t start = result.ages.size() - 1 + offset;
  uint64_t required = start + nested.ages.size();
  if (required > 63)
    return failure();
  result.ages.resize(std::max<uint64_t>(result.ages.size(), required));
  for (auto [age, nestedAge] : llvm::enumerate(nested.ages)) {
    llvm::append_range(result.ages[start + age].predicates,
                       nestedAge.predicates);
    llvm::append_range(result.ages[start + age].negatedPredicates,
                       nestedAge.negatedPredicates);
    llvm::append_range(result.ages[start + age].caseGuards,
                       nestedAge.caseGuards);
    llvm::append_range(result.ages[start + age].matchItems,
                       nestedAge.matchItems);
  }
  // Multiple independent priority scopes require an explicit nesting/order
  // model. Keep this first executable slice exact by accepting one boundary
  // per trace and rejecting compositions that would conflate scopes.
  if (!result.firstMatchBoundaries.empty() &&
      !nested.firstMatchBoundaries.empty())
    return failure();
  for (FirstMatchBoundary boundary : nested.firstMatchBoundaries) {
    boundary.age += start;
    result.firstMatchBoundaries.push_back(boundary);
  }
  result.vacuousSuccess |= nested.vacuousSuccess;
  return success();
}

/// Conjoin a bounded trace at an absolute clock age. This is used for finite
/// `always` ranges, where one property attempt starts at every age in the
/// range and all of those attempts must succeed. Unlike concatenation, the
/// insertion age is independent of the current endpoint.
static LogicalResult mergeFixedSequenceAt(FixedSequence &result,
                                          const FixedSequence &nested,
                                          uint64_t start) {
  if (nested.ages.empty() || start + nested.ages.size() > 63 ||
      !nested.firstMatchBoundaries.empty() || nested.vacuousSuccess)
    return failure();
  result.ages.resize(
      std::max<uint64_t>(result.ages.size(), start + nested.ages.size()));
  for (auto [age, nestedAge] : llvm::enumerate(nested.ages)) {
    if (!nestedAge.matchItems.empty())
      return failure();
    llvm::append_range(result.ages[start + age].predicates,
                       nestedAge.predicates);
    llvm::append_range(result.ages[start + age].negatedPredicates,
                       nestedAge.negatedPredicates);
    llvm::append_range(result.ages[start + age].caseGuards,
                       nestedAge.caseGuards);
  }
  return success();
}

/// Recognize the high-frequency persistent repetition forms without expanding
/// their unbounded inter-occurrence waits into a finite horizon:
///
///   fixed-prefix ##N boolean[->M:N] [##1 boolean]
///   fixed-prefix ##N boolean[=M:N]  [##1 boolean]
///
/// The generated monitor below keeps aggregate token counts per DFA state, so
/// runtime memory is bounded by N rather than by the number or age of live
/// attempts. Locals, match items, branching terms, and longer continuations
/// require per-token data/correlation and remain deliberately outside this
/// compact representation.
static FailureOr<PersistentRepetitionSequence>
compilePersistentRepetition(Operation *operation) {
  SmallVector<Operation *> children;
  ArrayAttr delays;
  if (auto concat = dyn_cast<semantic::SVSequenceConcatExprOp>(operation)) {
    children = getChildren(concat);
    delays = concat.getDelays();
    if (children.empty() || delays.size() != children.size())
      return failure();
  } else {
    children.push_back(operation);
  }

  auto getFixedDelay = [&](size_t index) -> std::optional<uint64_t> {
    if (!delays)
      return index == 0 ? std::optional<uint64_t>(0) : std::nullopt;
    auto delay = dyn_cast<DictionaryAttr>(delays[index]);
    auto minimum = delay ? delay.getAs<IntegerAttr>("min") : IntegerAttr{};
    auto maximum = delay ? delay.getAs<IntegerAttr>("max") : IntegerAttr{};
    auto unbounded = delay ? delay.getAs<BoolAttr>("is_unbounded") : BoolAttr{};
    if (!minimum || !maximum || !unbounded || unbounded.getValue() ||
        minimum.getInt() < 0 || minimum.getInt() != maximum.getInt())
      return std::nullopt;
    return static_cast<uint64_t>(minimum.getInt());
  };

  size_t repetitionIndex = children.size();
  semantic::SVSimpleAssertionExprOp repetition;
  for (auto [index, child] : llvm::enumerate(children)) {
    auto simple = dyn_cast<semantic::SVSimpleAssertionExprOp>(child);
    if (!simple || !simple.getHasRepetition() ||
        (simple.getRepetitionKind() !=
             semantic::SVSequenceRepetitionKind::Nonconsecutive &&
         simple.getRepetitionKind() !=
             semantic::SVSequenceRepetitionKind::GoTo))
      continue;
    if (repetition)
      return failure();
    repetition = simple;
    repetitionIndex = index;
  }
  if (!repetition || repetition.getRepetitionIsUnbounded() ||
      !repetition.getRepetitionMin() || !repetition.getRepetitionMax() ||
      *repetition.getRepetitionMin() <= 0 ||
      *repetition.getRepetitionMax() < *repetition.getRepetitionMin() ||
      *repetition.getRepetitionMax() > 63)
    return failure();

  PersistentRepetitionSequence result;
  result.kind = *repetition.getRepetitionKind();
  result.minimum = static_cast<uint64_t>(*repetition.getRepetitionMin());
  result.maximum = static_cast<uint64_t>(*repetition.getRepetitionMax());

  SmallVector<Operation *> repeatedChildren = getChildren(repetition);
  if (repeatedChildren.size() != 1)
    return failure();
  if (isa<semantic::SVAssertionInstanceExpressionOp>(
          repeatedChildren.front())) {
    FailureOr<FixedSequence> nested =
        compileFixedSequence(repeatedChildren.front());
    if (failed(nested) || !isSingleBooleanAge(*nested))
      return failure();
    result.term = nested->ages.front();
  } else {
    result.term.predicates.push_back(repeatedChildren.front());
  }

  for (size_t index = 0; index < repetitionIndex; ++index) {
    std::optional<uint64_t> delay = getFixedDelay(index);
    FailureOr<FixedSequence> prefix = compileFixedSequence(children[index]);
    if (!delay || failed(prefix) || prefix->ages.empty() ||
        llvm::any_of(prefix->ages,
                     [](const FixedSequenceAge &age) {
                       return !age.matchItems.empty();
                     }) ||
        failed(appendFixedSequence(result.entry, *prefix, *delay)))
      return failure();
  }

  std::optional<uint64_t> repetitionDelay = getFixedDelay(repetitionIndex);
  FixedSequence entryPoint;
  entryPoint.ages.resize(1);
  if (!repetitionDelay ||
      failed(appendFixedSequence(result.entry, entryPoint, *repetitionDelay)))
    return failure();

  if (repetitionIndex + 1 == children.size())
    return result;
  if (repetitionIndex + 2 != children.size())
    return failure();
  std::optional<uint64_t> terminalDelay = getFixedDelay(repetitionIndex + 1);
  FailureOr<FixedSequence> terminal =
      compileFixedSequence(children[repetitionIndex + 1]);
  if (!terminalDelay || *terminalDelay != 1 || failed(terminal) ||
      !isSingleBooleanAge(*terminal))
    return failure();
  result.terminal = terminal->ages.front();
  result.hasTerminal = true;
  return result;
}

/// Compile the bounded branching sequence forms into a finite set of exact
/// traces. The runtime monitor below shares sampled predicate values and keeps
/// one compact attempt-age word per trace. This deliberately caps expansion:
/// it is a compile-time representation for small static SVA ranges, not a
/// general temporal interpreter.
static FailureOr<FixedSequenceAlternatives>
compileFixedSequenceAlternatives(Operation *operation,
                                 Operation *resolvedClock = nullptr) {
  if (FailureOr<FixedSequence> exact = compileFixedSequence(operation);
      succeeded(exact))
    return FixedSequenceAlternatives{std::move(*exact)};

  if (auto instance =
          dyn_cast<semantic::SVAssertionInstanceExpressionOp>(operation)) {
    FailureOr<Operation *> body = getExpandedAssertionBody(instance);
    if (failed(body))
      return failure();
    return compileFixedSequenceAlternatives(*body, resolvedClock);
  }

  if (auto first = dyn_cast<semantic::SVFirstMatchAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(first);
    if (first.getMatchItemCount() != 0 || children.size() != 1)
      return failure();
    FailureOr<FixedSequenceAlternatives> nested =
        compileFixedSequenceAlternatives(children.front(), resolvedClock);
    if (failed(nested) || nested->empty())
      return failure();
    for (FixedSequence &alternative : *nested) {
      if (alternative.ages.empty() || !alternative.firstMatchBoundaries.empty())
        return failure();
      alternative.firstMatchBoundaries.push_back(
          {first.getOperation(), alternative.ages.size() - 1});
    }
    return nested;
  }

  if (auto simple = dyn_cast<semantic::SVSimpleAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(simple);
    if (simple.getIsNull() || children.size() != 1)
      return failure();
    if (!simple.getHasRepetition() &&
        isa<semantic::SVAssertionInstanceExpressionOp>(children.front()))
      return compileFixedSequenceAlternatives(children.front(), resolvedClock);

    // A finite positive consecutive range is the union of its exact repeat
    // counts. Keep that union in the same bounded exact-trace representation
    // used for ranged ## delays and binary sequence composition. Empty and
    // unbounded repetition require distinct endpoint/termination semantics
    // and deliberately remain outside this compiler.
    if (simple.getHasRepetition() &&
        simple.getRepetitionKind() ==
            semantic::SVSequenceRepetitionKind::Consecutive &&
        !simple.getRepetitionIsUnbounded() && simple.getRepetitionMin() &&
        simple.getRepetitionMax() && *simple.getRepetitionMin() > 0 &&
        *simple.getRepetitionMax() >= *simple.getRepetitionMin()) {
      FixedSequenceAlternatives nested;
      if (isa<semantic::SVAssertionInstanceExpressionOp>(children.front())) {
        FailureOr<FixedSequenceAlternatives> compiled =
            compileFixedSequenceAlternatives(children.front(), resolvedClock);
        if (failed(compiled))
          return failure();
        nested = std::move(*compiled);
      } else {
        FixedSequence term;
        term.ages.resize(1);
        term.ages.front().predicates.push_back(children.front());
        nested.push_back(std::move(term));
      }
      if (nested.empty())
        return failure();

      uint64_t minimum = *simple.getRepetitionMin();
      uint64_t maximum = *simple.getRepetitionMax();
      FixedSequenceAlternatives prefixes(1);
      FixedSequenceAlternatives results;
      for (uint64_t count = 1; count <= maximum; ++count) {
        if (prefixes.size() > maxFixedSequenceAlternatives / nested.size())
          return failure();
        FixedSequenceAlternatives expanded;
        expanded.reserve(prefixes.size() * nested.size());
        for (const FixedSequence &prefix : prefixes) {
          for (const FixedSequence &suffix : nested) {
            FixedSequence combined = prefix;
            uint64_t offset = combined.ages.empty() ? 0 : 1;
            if (failed(appendFixedSequence(combined, suffix, offset)))
              return failure();
            expanded.push_back(std::move(combined));
          }
        }
        prefixes = std::move(expanded);
        if (count < minimum)
          continue;
        if (results.size() > maxFixedSequenceAlternatives - prefixes.size())
          return failure();
        llvm::append_range(results, prefixes);
      }
      return results;
    }
  }

  if (auto clocking =
          dyn_cast<semantic::SVClockingAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(clocking);
    if (children.size() != 2 || !resolvedClock ||
        !areEquivalentDirectClocks(resolvedClock, children.front()))
      return failure();
    return compileFixedSequenceAlternatives(children.back(), resolvedClock);
  }

  if (auto concat = dyn_cast<semantic::SVSequenceConcatExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(concat);
    ArrayAttr delays = concat.getDelays();
    if (children.empty() || delays.size() != children.size())
      return failure();
    FixedSequenceAlternatives results(1);
    for (auto [index, child] : llvm::enumerate(children)) {
      auto delay = dyn_cast<DictionaryAttr>(delays[index]);
      auto minimum = delay ? delay.getAs<IntegerAttr>("min") : IntegerAttr{};
      auto maximum = delay ? delay.getAs<IntegerAttr>("max") : IntegerAttr{};
      auto unbounded =
          delay ? delay.getAs<BoolAttr>("is_unbounded") : BoolAttr{};
      if (!minimum || !maximum || !unbounded || unbounded.getValue() ||
          minimum.getInt() < 0 || maximum.getInt() < minimum.getInt())
        return failure();
      FailureOr<FixedSequenceAlternatives> nested =
          compileFixedSequenceAlternatives(child, resolvedClock);
      if (failed(nested))
        return failure();
      uint64_t offsets =
          static_cast<uint64_t>(maximum.getInt() - minimum.getInt()) + 1;
      if (nested->empty() ||
          results.size() > maxFixedSequenceAlternatives / nested->size() ||
          results.size() * nested->size() >
              maxFixedSequenceAlternatives / offsets)
        return failure();
      FixedSequenceAlternatives expanded;
      expanded.reserve(results.size() * nested->size() * offsets);
      for (const FixedSequence &prefix : results)
        for (const FixedSequence &suffix : *nested)
          for (int64_t offset = minimum.getInt(); offset <= maximum.getInt();
               ++offset) {
            FixedSequence combined = prefix;
            if (failed(appendFixedSequence(combined, suffix,
                                           static_cast<uint64_t>(offset))))
              return failure();
            expanded.push_back(std::move(combined));
          }
      results = std::move(expanded);
    }
    return results;
  }

  if (auto caseProperty =
          dyn_cast<semantic::SVCaseAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(caseProperty);
    if (children.empty())
      return failure();
    Operation *selector = children.front();
    size_t childIndex = 1;
    SmallVector<Operation *> priorLabels;
    FixedSequenceAlternatives results;
    for (Attribute sizeAttr : caseProperty.getItemGroupSizes()) {
      auto size = dyn_cast<IntegerAttr>(sizeAttr);
      if (!size || size.getInt() <= 0)
        return failure();
      size_t labelCount = static_cast<size_t>(size.getInt());
      if (childIndex + labelCount >= children.size())
        return failure();
      ArrayRef<Operation *> labels(children.data() + childIndex, labelCount);
      childIndex += labelCount;
      FailureOr<FixedSequenceAlternatives> body =
          compileFixedSequenceAlternatives(children[childIndex++],
                                           resolvedClock);
      if (failed(body) || body->empty() ||
          body->size() >
              (maxFixedSequenceAlternatives - results.size()) / labelCount)
        return failure();
      for (Operation *label : labels) {
        for (const FixedSequence &bodyAlternative : *body) {
          if (bodyAlternative.ages.empty())
            return failure();
          FixedSequence alternative = bodyAlternative;
          for (Operation *prior : priorLabels)
            alternative.ages.front().caseGuards.push_back(
                {selector, prior, true});
          alternative.ages.front().caseGuards.push_back(
              {selector, label, false});
          results.push_back(std::move(alternative));
        }
      }
      llvm::append_range(priorLabels, labels);
    }

    FixedSequenceAlternatives fallback;
    if (caseProperty.getHasDefault()) {
      if (childIndex >= children.size())
        return failure();
      FailureOr<FixedSequenceAlternatives> compiledDefault =
          compileFixedSequenceAlternatives(children[childIndex++],
                                           resolvedClock);
      if (failed(compiledDefault) || compiledDefault->empty())
        return failure();
      fallback = std::move(*compiledDefault);
    } else {
      FixedSequence vacuous;
      vacuous.ages.resize(1);
      vacuous.vacuousSuccess = true;
      fallback.push_back(std::move(vacuous));
    }
    if (childIndex != children.size() ||
        results.size() > maxFixedSequenceAlternatives - fallback.size())
      return failure();
    for (FixedSequence &alternative : fallback) {
      if (alternative.ages.empty())
        return failure();
      for (Operation *prior : priorLabels)
        alternative.ages.front().caseGuards.push_back({selector, prior, true});
    }
    llvm::append_range(results, std::move(fallback));
    return results;
  }

  if (auto conditional =
          dyn_cast<semantic::SVConditionalAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(conditional);
    size_t expectedChildren = conditional.getHasElse() ? 3 : 2;
    if (children.size() != expectedChildren)
      return failure();

    Operation *condition = children.front();
    FailureOr<FixedSequenceAlternatives> thenAlternatives =
        compileFixedSequenceAlternatives(children[1], resolvedClock);
    if (failed(thenAlternatives) || thenAlternatives->empty())
      return failure();
    for (FixedSequence &alternative : *thenAlternatives) {
      if (alternative.ages.empty())
        return failure();
      alternative.ages.front().predicates.push_back(condition);
    }

    FixedSequenceAlternatives elseAlternatives;
    if (conditional.getHasElse()) {
      FailureOr<FixedSequenceAlternatives> compiledElse =
          compileFixedSequenceAlternatives(children[2], resolvedClock);
      if (failed(compiledElse) || compiledElse->empty())
        return failure();
      elseAlternatives = std::move(*compiledElse);
    } else {
      FixedSequence vacuous;
      vacuous.ages.resize(1);
      vacuous.vacuousSuccess = true;
      elseAlternatives.push_back(std::move(vacuous));
    }
    if (thenAlternatives->size() >
        maxFixedSequenceAlternatives - elseAlternatives.size())
      return failure();
    for (FixedSequence &alternative : elseAlternatives) {
      if (alternative.ages.empty())
        return failure();
      alternative.ages.front().negatedPredicates.push_back(condition);
    }
    llvm::append_range(*thenAlternatives, std::move(elseAlternatives));
    return std::move(*thenAlternatives);
  }

  if (auto unary = dyn_cast<semantic::SVUnaryAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(unary);
    if (children.size() != 1 ||
        unary.getOperatorKind() == semantic::SVAssertionUnaryOperator::Not)
      return failure();

    int64_t minimum = 0;
    int64_t maximum = 0;
    bool nextTime = unary.getOperatorKind() ==
                        semantic::SVAssertionUnaryOperator::NextTime ||
                    unary.getOperatorKind() ==
                        semantic::SVAssertionUnaryOperator::SNextTime;
    bool always =
        unary.getOperatorKind() == semantic::SVAssertionUnaryOperator::Always ||
        unary.getOperatorKind() == semantic::SVAssertionUnaryOperator::SAlways;
    bool eventually = unary.getOperatorKind() ==
                          semantic::SVAssertionUnaryOperator::Eventually ||
                      unary.getOperatorKind() ==
                          semantic::SVAssertionUnaryOperator::SEventually;
    if (!nextTime && !always && !eventually)
      return failure();

    if (!unary.getHasRange()) {
      if (!nextTime)
        return failure();
      minimum = maximum = 1;
    } else {
      if (unary.getRangeIsUnbounded() || !unary.getRangeMin() ||
          !unary.getRangeMax())
        return failure();
      minimum = *unary.getRangeMin();
      maximum = *unary.getRangeMax();
    }
    if (minimum < 0 || maximum < minimum || maximum > 62 ||
        (nextTime && minimum != maximum))
      return failure();

    FailureOr<FixedSequenceAlternatives> nested =
        compileFixedSequenceAlternatives(children.front(), resolvedClock);
    if (failed(nested) || nested->empty())
      return failure();

    if (nextTime) {
      FixedSequenceAlternatives results;
      results.reserve(nested->size());
      for (const FixedSequence &alternative : *nested) {
        FixedSequence shifted;
        if (failed(appendFixedSequence(shifted, alternative,
                                       static_cast<uint64_t>(minimum))))
          return failure();
        results.push_back(std::move(shifted));
      }
      return results;
    }

    uint64_t offsets = static_cast<uint64_t>(maximum - minimum) + 1;
    if (eventually) {
      if (nested->size() > maxFixedSequenceAlternatives / offsets)
        return failure();
      FixedSequenceAlternatives results;
      results.reserve(nested->size() * offsets);
      for (int64_t delay = minimum; delay <= maximum; ++delay) {
        for (const FixedSequence &alternative : *nested) {
          FixedSequence shifted;
          if (failed(appendFixedSequence(shifted, alternative,
                                         static_cast<uint64_t>(delay))))
            return failure();
          results.push_back(std::move(shifted));
        }
      }
      return results;
    }

    FixedSequenceAlternatives results(1);
    for (int64_t delay = minimum; delay <= maximum; ++delay) {
      if (results.size() > maxFixedSequenceAlternatives / nested->size())
        return failure();
      FixedSequenceAlternatives expanded;
      expanded.reserve(results.size() * nested->size());
      for (const FixedSequence &prefix : results) {
        for (const FixedSequence &alternative : *nested) {
          FixedSequence combined = prefix;
          if (failed(mergeFixedSequenceAt(combined, alternative,
                                          static_cast<uint64_t>(delay))))
            return failure();
          expanded.push_back(std::move(combined));
        }
      }
      results = std::move(expanded);
    }
    return results;
  }

  auto binary = dyn_cast<semantic::SVBinaryAssertionExprOp>(operation);
  SmallVector<Operation *> operands =
      binary ? getChildren(binary) : SmallVector<Operation *>{};
  if (!binary || operands.size() != 2)
    return failure();
  FailureOr<FixedSequenceAlternatives> lhs =
      compileFixedSequenceAlternatives(operands.front(), resolvedClock);
  FailureOr<FixedSequenceAlternatives> rhs =
      compileFixedSequenceAlternatives(operands.back(), resolvedClock);
  if (failed(lhs) || failed(rhs) || lhs->empty() || rhs->empty())
    return failure();

  auto hasMatchItems = [](const FixedSequence &sequence) {
    return llvm::any_of(sequence.ages, [](const FixedSequenceAge &age) {
      return !age.matchItems.empty();
    });
  };
  if (llvm::any_of(*lhs, hasMatchItems) || llvm::any_of(*rhs, hasMatchItems))
    return failure();
  auto hasVacuousAlternative = [](const FixedSequence &sequence) {
    return sequence.vacuousSuccess;
  };
  if (llvm::any_of(*lhs, hasVacuousAlternative) ||
      llvm::any_of(*rhs, hasVacuousAlternative))
    return failure();

  switch (binary.getOperatorKind()) {
  case semantic::SVAssertionBinaryOperator::Or: {
    if (lhs->size() > maxFixedSequenceAlternatives - rhs->size())
      return failure();
    llvm::append_range(*lhs, std::move(*rhs));
    return std::move(*lhs);
  }
  case semantic::SVAssertionBinaryOperator::And:
  case semantic::SVAssertionBinaryOperator::Intersect: {
    if (lhs->size() > maxFixedSequenceAlternatives / rhs->size())
      return failure();
    FixedSequenceAlternatives results;
    results.reserve(lhs->size() * rhs->size());
    bool intersect = binary.getOperatorKind() ==
                     semantic::SVAssertionBinaryOperator::Intersect;
    for (const FixedSequence &left : *lhs) {
      for (const FixedSequence &right : *rhs) {
        if (intersect && left.ages.size() != right.ages.size())
          continue;
        FixedSequence combined;
        combined.ages.resize(std::max(left.ages.size(), right.ages.size()));
        combined.vacuousSuccess = left.vacuousSuccess || right.vacuousSuccess;
        for (auto [age, value] : llvm::enumerate(left.ages)) {
          llvm::append_range(combined.ages[age].predicates, value.predicates);
          llvm::append_range(combined.ages[age].negatedPredicates,
                             value.negatedPredicates);
          llvm::append_range(combined.ages[age].caseGuards, value.caseGuards);
        }
        for (auto [age, value] : llvm::enumerate(right.ages)) {
          llvm::append_range(combined.ages[age].predicates, value.predicates);
          llvm::append_range(combined.ages[age].negatedPredicates,
                             value.negatedPredicates);
          llvm::append_range(combined.ages[age].caseGuards, value.caseGuards);
        }
        if (!left.firstMatchBoundaries.empty() &&
            !right.firstMatchBoundaries.empty())
          return failure();
        llvm::append_range(combined.firstMatchBoundaries,
                           left.firstMatchBoundaries);
        llvm::append_range(combined.firstMatchBoundaries,
                           right.firstMatchBoundaries);
        results.push_back(std::move(combined));
      }
    }
    if (results.empty())
      return failure();
    return results;
  }
  case semantic::SVAssertionBinaryOperator::Throughout: {
    if (lhs->size() != 1 || lhs->front().ages.size() != 1)
      return failure();
    if (!lhs->front().firstMatchBoundaries.empty() ||
        lhs->front().vacuousSuccess)
      return failure();
    const FixedSequenceAge &guard = lhs->front().ages.front();
    for (FixedSequence &sequence : *rhs) {
      for (FixedSequenceAge &age : sequence.ages) {
        llvm::append_range(age.predicates, guard.predicates);
        llvm::append_range(age.negatedPredicates, guard.negatedPredicates);
        llvm::append_range(age.caseGuards, guard.caseGuards);
      }
    }
    return std::move(*rhs);
  }
  case semantic::SVAssertionBinaryOperator::Within: {
    FixedSequenceAlternatives results;
    for (const FixedSequence &inner : *lhs) {
      for (const FixedSequence &outer : *rhs) {
        if (inner.ages.size() > outer.ages.size())
          continue;
        size_t placements = outer.ages.size() - inner.ages.size() + 1;
        if (placements > maxFixedSequenceAlternatives ||
            results.size() > maxFixedSequenceAlternatives - placements)
          return failure();
        for (size_t offset = 0; offset < placements; ++offset) {
          FixedSequence combined = outer;
          combined.vacuousSuccess |= inner.vacuousSuccess;
          for (auto [age, value] : llvm::enumerate(inner.ages)) {
            llvm::append_range(combined.ages[offset + age].predicates,
                               value.predicates);
            llvm::append_range(combined.ages[offset + age].negatedPredicates,
                               value.negatedPredicates);
            llvm::append_range(combined.ages[offset + age].caseGuards,
                               value.caseGuards);
          }
          if (!combined.firstMatchBoundaries.empty() &&
              !inner.firstMatchBoundaries.empty())
            return failure();
          for (FirstMatchBoundary boundary : inner.firstMatchBoundaries) {
            boundary.age += offset;
            combined.firstMatchBoundaries.push_back(boundary);
          }
          results.push_back(std::move(combined));
        }
      }
    }
    if (results.empty())
      return failure();
    return results;
  }
  case semantic::SVAssertionBinaryOperator::Iff: {
    if (lhs->size() != 1 || rhs->size() != 1 ||
        !isSingleBooleanAge(lhs->front()) ||
        !isSingleBooleanAge(rhs->front()) || lhs->front().vacuousSuccess ||
        rhs->front().vacuousSuccess)
      return failure();
    FixedSequence bothTrue = lhs->front();
    llvm::append_range(bothTrue.ages.front().predicates,
                       rhs->front().ages.front().predicates);
    llvm::append_range(bothTrue.ages.front().negatedPredicates,
                       rhs->front().ages.front().negatedPredicates);
    FixedSequence bothFalse =
        negateSingleBooleanSequence(std::move(lhs->front()));
    FixedSequence negatedRight =
        negateSingleBooleanSequence(std::move(rhs->front()));
    llvm::append_range(bothFalse.ages.front().predicates,
                       negatedRight.ages.front().predicates);
    llvm::append_range(bothFalse.ages.front().negatedPredicates,
                       negatedRight.ages.front().negatedPredicates);
    return FixedSequenceAlternatives{std::move(bothTrue), std::move(bothFalse)};
  }
  case semantic::SVAssertionBinaryOperator::Implies: {
    if (lhs->size() != 1 || rhs->size() != 1 ||
        !isSingleBooleanAge(lhs->front()) ||
        !isSingleBooleanAge(rhs->front()) || lhs->front().vacuousSuccess ||
        rhs->front().vacuousSuccess)
      return failure();
    FixedSequence antecedentFalse =
        negateSingleBooleanSequence(std::move(lhs->front()));
    antecedentFalse.vacuousSuccess = true;
    return FixedSequenceAlternatives{std::move(antecedentFalse),
                                     std::move(rhs->front())};
  }
  default:
    return failure();
  }
}

static Operation *unwrapAssertionInstance(Operation *operation) {
  while (true) {
    if (auto instance =
            dyn_cast<semantic::SVAssertionInstanceExpressionOp>(operation)) {
      FailureOr<Operation *> body = getExpandedAssertionBody(instance);
      if (failed(body))
        return nullptr;
      operation = *body;
      continue;
    }
    auto simple = dyn_cast<semantic::SVSimpleAssertionExprOp>(operation);
    SmallVector<Operation *> children =
        simple ? getChildren(simple) : SmallVector<Operation *>{};
    if (simple && !simple.getIsNull() && !simple.getHasRepetition() &&
        children.size() == 1 &&
        isa<semantic::SVAssertionInstanceExpressionOp>(children.front())) {
      operation = children.front();
      continue;
    }
    return operation;
  }
}

} // namespace

LogicalResult
UnitLowering::lowerSequenceEndpointMonitor(ArrayRef<Operation *> roots) {
  if (roots.size() != 1)
    return function.emitError(
               "sequence endpoint monitor requires one assertion instance"),
           failure();
  auto instance =
      dyn_cast<semantic::SVAssertionInstanceExpressionOp>(roots.front());
  if (!instance)
    return emitError(getSemanticLocation(roots.front()))
               << "sequence endpoint monitor has no assertion instance",
           failure();
  if (instance.getLocalVariableCount() != 0)
    return emitError(getSemanticLocation(instance))
               << "sequence endpoint monitor does not support local "
                  "variables",
           failure();
  FailureOr<Operation *> expanded = getExpandedAssertionBody(instance);
  if (failed(expanded))
    return emitError(getSemanticLocation(instance))
               << "sequence endpoint monitor requires a nonrecursive "
                  "expanded instance without local variables",
           failure();
  auto clocking = dyn_cast<semantic::SVClockingAssertionExprOp>(*expanded);
  SmallVector<Operation *> clocked =
      clocking ? getChildren(clocking) : SmallVector<Operation *>{};
  if (!clocking || clocked.size() != 2)
    return emitError(getSemanticLocation(*expanded))
               << "sequence endpoint monitor requires an explicit clock",
           failure();
  auto clock = dyn_cast<semantic::SVSignalEventControlOp>(clocked.front());
  if (!clock || clock.getHasIff() || getChildren(clock).size() != 1 ||
      !isAddressableExpression(getChildren(clock).front()))
    return emitError(getSemanticLocation(clocked.front()))
               << "sequence endpoint monitor requires one direct signal "
                  "edge clock without iff",
           failure();
  FailureOr<FixedSequence> compiled = compileFixedSequence(clocked.back());
  if (failed(compiled) || compiled->ages.empty() ||
      compiled->ages.size() > 63 ||
      llvm::any_of(compiled->ages, [](const FixedSequenceAge &age) {
        return !age.matchItems.empty();
      }))
    return emitError(getSemanticLocation(clocked.back()))
               << "sequence endpoint monitor supports boolean terms, fixed "
                  "## delays, and fixed consecutive repetition up to 63 "
                  "cycles",
           failure();

  auto endpointPath =
      function->getAttrOfType<StringAttr>(sequenceEndpointPathAttrName);
  Value endpoint =
      endpointPath ? values.lookup(endpointPath.getValue()) : Value{};
  if (!endpoint || !isa<sim::EventType>(endpoint.getType()))
    return function.emitError(
               "sequence endpoint monitor has no endpoint event capture"),
           failure();
  function->removeAttr(sequenceEndpointMonitorAttrName);
  function->removeAttr(sequenceEndpointPathAttrName);

  Location location = getSemanticLocation(instance);
  function->setAttr("home_region",
                    sim::EventRegionAttr::get(function.getContext(),
                                              sim::EventRegion::Observed));
  Type stateType = builder.getI64Type();
  Value zero = arith::ConstantOp::create(builder, location, stateType,
                                         builder.getI64IntegerAttr(0));
  Value stateStorage;
  if (compiled->ages.size() > 1)
    stateStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);

  Block *wait = addBlock();
  Block *sample = addBlock();
  emitBranch(wait);
  setCurrent(wait);
  if (failed(emitEventSuspend(clock, sample)))
    return failure();
  wait->getTerminator()->setAttr(
      "resume_region", sim::EventRegionAttr::get(function.getContext(),
                                                 sim::EventRegion::Observed));
  setCurrent(sample);

  bool savedSampleAssertionValues = sampleAssertionValues;
  sampleAssertionValues = true;
  Operation *savedSampledClock = activeSampledClock;
  activeSampledClock = clock;
  llvm::scope_exit restoreSampling([&] {
    sampleAssertionValues = savedSampleAssertionValues;
    activeSampledClock = savedSampledClock;
  });

  llvm::DenseMap<Operation *, Value> predicateCache;
  auto evaluateAge = [&](const FixedSequenceAge &age) -> FailureOr<Value> {
    Value result = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    auto evaluatePredicate = [&](Operation *predicate) -> FailureOr<Value> {
      Value truth;
      if (auto found = predicateCache.find(predicate);
          found != predicateCache.end()) {
        truth = found->second;
      } else {
        FailureOr<Value> value = lowerExpression(predicate);
        if (failed(value))
          return failure();
        FailureOr<Value> converted =
            truthValue(*value, getSemanticLocation(predicate));
        if (failed(converted))
          return failure();
        truth = *converted;
        predicateCache[predicate] = truth;
      }
      return truth;
    };
    for (Operation *predicate : age.predicates) {
      FailureOr<Value> truth = evaluatePredicate(predicate);
      if (failed(truth))
        return failure();
      result = arith::AndIOp::create(builder, location, result, *truth);
    }
    for (Operation *predicate : age.negatedPredicates) {
      FailureOr<Value> truth = evaluatePredicate(predicate);
      if (failed(truth))
        return failure();
      Value negated = arith::XOrIOp::create(
          builder, location, *truth,
          arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true)));
      result = arith::AndIOp::create(builder, location, result, negated);
    }
    return result;
  };
  auto triggerIf = [&](Value condition) {
    Block *trigger = addBlock();
    Block *continuation = addBlock();
    cf::CondBranchOp::create(builder, location, condition, trigger,
                             ValueRange{}, continuation, ValueRange{});
    setCurrent(trigger);
    sim::SimEventTriggerOp::create(builder, location, endpoint, Value{},
                                   builder.getBoolAttr(false),
                                   sim::EventSiteAttr{});
    emitBranch(continuation);
    setCurrent(continuation);
  };

  Value state = stateStorage ? sim::SimRefLoadOp::create(
                                   builder, location, stateType, stateStorage)
                             : zero;
  Value nextState = zero;
  for (uint64_t age = 1; age < compiled->ages.size(); ++age) {
    Value mask = arith::ConstantOp::create(
        builder, location, stateType,
        builder.getI64IntegerAttr(uint64_t{1} << age));
    Value presentBits = arith::AndIOp::create(builder, location, state, mask);
    Value active = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, presentBits, zero);
    FailureOr<Value> matches = evaluateAge(compiled->ages[age]);
    if (failed(matches))
      return failure();
    Value advances = arith::AndIOp::create(builder, location, active, *matches);
    if (age + 1 == compiled->ages.size()) {
      triggerIf(advances);
    } else {
      Value nextMask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
      Value advancedBit =
          arith::SelectOp::create(builder, location, advances, nextMask, zero);
      nextState =
          arith::OrIOp::create(builder, location, nextState, advancedBit);
    }
  }

  FailureOr<Value> starts = evaluateAge(compiled->ages.front());
  if (failed(starts))
    return failure();
  if (compiled->ages.size() == 1) {
    triggerIf(*starts);
  } else {
    Value nextMask = arith::ConstantOp::create(builder, location, stateType,
                                               builder.getI64IntegerAttr(2));
    Value started =
        arith::SelectOp::create(builder, location, *starts, nextMask, zero);
    nextState = arith::OrIOp::create(builder, location, nextState, started);
  }
  if (stateStorage)
    sim::SimRefStoreOp::create(builder, location, nextState, stateStorage);
  cf::BranchOp::create(builder, location, wait);
  return success();
}

LogicalResult UnitLowering::lowerConcurrentAssertion(
    semantic::SVConcurrentAssertionStatementOp op) {
  Location location = getSemanticLocation(op);
  if (op->hasAttr("obelisk_sim.default_assertion_failure")) {
    emitDefaultAssertionFailure(location, "concurrent assertion");
    return success();
  }
  SmallVector<Operation *> children = getChildren(op);

  bool expect = op.getAssertionKind() == semantic::SVAssertionKind::Expect;
  bool expectMonitor = op->hasAttr("obelisk_sim.expect_monitor");
  if (expect && !expectMonitor) {
    size_t actionCount = static_cast<size_t>(op.getHasPassAction()) +
                         static_cast<size_t>(op.getHasFailAction());
    if (children.size() <= actionCount)
      return op.emitError("malformed expect statement inventory"), failure();

    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    Value resultStorage = sim::SimRefAllocOp::create(
        builder, location,
        sim::RefType::get(function.getContext(), builder.getI1Type()), zero);
    Value completed = sim::SimEventCreateOp::create(
        builder, location, sim::EventType::get(function.getContext()));

    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    uint64_t occurrence = nextForkOrdinal;
    std::string donePath =
        (function.getSymName() + ".$expect_done." + Twine(node)).str();
    std::string resultPath =
        (function.getSymName() + ".$expect_result." + Twine(node)).str();
    std::string identity = (function.getSymName() + ".$expect_monitor." +
                            Twine(node) + "." + Twine(occurrence))
                               .str();

    Attribute previousCodeUnit = op->getAttr("obelisk_sim.fork_code_unit_id");
    Attribute previousCaptures = op->getAttr(calleeCapturesAttrName);
    op->setAttr("obelisk_sim.fork_code_unit_id",
                builder.getI64IntegerAttr(stableCodeUnitID(identity)));
    op->setAttr("obelisk_sim.expect_monitor", builder.getUnitAttr());
    op->setAttr("obelisk_sim.expect_done_path",
                builder.getStringAttr(donePath));
    op->setAttr("obelisk_sim.expect_result_path",
                builder.getStringAttr(resultPath));
    SmallVector<Attribute> capturePaths;
    if (auto captures = dyn_cast_or_null<ArrayAttr>(previousCaptures))
      llvm::append_range(capturePaths, captures);
    capturePaths.push_back(builder.getStringAttr(donePath));
    capturePaths.push_back(builder.getStringAttr(resultPath));
    op->setAttr(calleeCapturesAttrName, builder.getArrayAttr(capturePaths));
    Value previousDoneValue = values.lookup(donePath);
    Value previousResultLValue = lvalues.lookup(resultPath);
    values[donePath] = completed;
    lvalues[resultPath] = resultStorage;
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> monitor =
        outlineForkBranch(op, node, /*branchIndex=*/16,
                          /*captureReferences=*/true);
    if (previousDoneValue)
      values[donePath] = previousDoneValue;
    else
      values.erase(donePath);
    if (previousResultLValue)
      lvalues[resultPath] = previousResultLValue;
    else
      lvalues.erase(resultPath);
    op->removeAttr("obelisk_sim.expect_monitor");
    op->removeAttr("obelisk_sim.expect_done_path");
    op->removeAttr("obelisk_sim.expect_result_path");
    if (previousCaptures)
      op->setAttr(calleeCapturesAttrName, previousCaptures);
    else
      op->removeAttr(calleeCapturesAttrName);
    if (previousCodeUnit)
      op->setAttr("obelisk_sim.fork_code_unit_id", previousCodeUnit);
    else
      op->removeAttr("obelisk_sim.fork_code_unit_id");
    if (failed(monitor))
      return failure();
    monitor->first->setAttr("obelisk_sim.expect_monitor_actor",
                            builder.getUnitAttr());
    sim::SimSpawnOp::create(builder, location, monitor->first.getSymNameAttr(),
                            monitor->second, ArrayAttr{}, ArrayAttr{});

    Block *completedBlock = addBlock();
    sim::SimSuspendEventOp::create(
        builder, location, completed, ValueRange{}, sim::ContinuationSiteAttr{},
        sim::EventRegionAttr::get(function.getContext(),
                                  sim::EventRegion::Reactive),
        completedBlock);
    setCurrent(completedBlock);
    Value passed = sim::SimRefLoadOp::create(
        builder, location, builder.getI1Type(), resultStorage);
    Block *passBlock = addBlock();
    Block *failBlock = addBlock();
    Block *mergeBlock = addBlock();
    cf::CondBranchOp::create(builder, location, passed, passBlock, ValueRange{},
                             failBlock, ValueRange{});
    size_t actionBase = children.size() - actionCount;
    setCurrent(passBlock);
    if (op.getHasPassAction() && failed(lowerStatement(children[actionBase])))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(failBlock);
    if (op.getHasFailAction()) {
      if (failed(lowerStatement(children[actionBase + op.getHasPassAction()])))
        return failure();
    } else {
      emitDefaultAssertionFailure(location, "expect");
    }
    emitBranch(mergeBlock);
    setCurrent(mergeBlock);
    return success();
  }

  bool cover =
      op.getAssertionKind() == semantic::SVAssertionKind::CoverProperty ||
      op.getAssertionKind() == semantic::SVAssertionKind::CoverSequence;
  bool coverSequence =
      op.getAssertionKind() == semantic::SVAssertionKind::CoverSequence;
  bool assertion = op.getAssertionKind() == semantic::SVAssertionKind::Assert ||
                   op.getAssertionKind() == semantic::SVAssertionKind::Assume;
  bool observable = assertion || cover;
  if (!observable && !expectMonitor &&
      op.getAssertionKind() != semantic::SVAssertionKind::Restrict) {
    emitError(location)
        << "expect statements are not executable by the bounded concurrent "
           "monitor";
    return failure();
  }

  // A source assertion label arrives as a synthetic named block. Preserve its
  // prepared stable identity for future assertion-control selection without
  // turning it into a procedural control activation across clock waits.
  if (auto block =
          dyn_cast_or_null<semantic::SVBlockStatementOp>(op->getParentOp())) {
    if (auto path = block.getBlockPathAttr())
      function->setAttr("obelisk_sim.assertion_path", path);
    if (auto target =
            block->getAttrOfType<IntegerAttr>("obelisk_sim.control_target_id"))
      function->setAttr("obelisk_sim.assertion_target_id", target);
  }

  size_t prefix = 0;
  Operation *disable = nullptr;
  if (op.getHasDefaultDisable()) {
    if (children.empty())
      return op.emitError("missing resolved default disable expression"),
             failure();
    disable = children[prefix++];
  }
  Operation *defaultClock = nullptr;
  if (op.getDefaultClockingSymbolAttr()) {
    if (prefix >= children.size() ||
        !isa<semantic::SVSignalEventControlOp, semantic::SVEventListControlOp>(
            children[prefix]))
      return op.emitError("missing resolved default clock event"), failure();
    defaultClock = children[prefix++];
  }
  size_t expected = prefix + 1 + static_cast<size_t>(op.getHasPassAction()) +
                    static_cast<size_t>(op.getHasFailAction());
  if (children.size() != expected)
    return op.emitError("malformed concurrent assertion inventory"), failure();

  semantic::SVAssertionInstanceExpressionOp localInstance;
  bool multipleLocalInstances = false;
  children[prefix]->walk(
      [&](semantic::SVAssertionInstanceExpressionOp instance) {
        if (instance.getLocalVariableCount() == 0)
          return;
        if (localInstance && localInstance != instance)
          multipleLocalInstances = true;
        else
          localInstance = instance;
      });
  if (multipleLocalInstances)
    return emitError(getSemanticLocation(children[prefix]))
               << "bounded concurrent monitors support one local-variable "
                  "assertion instance",
           failure();

  Operation *property = unwrapAssertionInstance(children[prefix]);
  if (!property)
    return op.emitError("recursive, local-variable, or malformed assertion "
                        "instances are not executable by the bounded AOT "
                        "monitor"),
           failure();

  Operation *clock = defaultClock;
  if (auto clocking = dyn_cast<semantic::SVClockingAssertionExprOp>(property)) {
    SmallVector<Operation *> clocked = getChildren(clocking);
    if (clocked.size() != 2)
      return clocking.emitError("malformed clocked assertion"), failure();
    clock = clocked.front();
    property = unwrapAssertionInstance(clocked.back());
  }
  if (!clock)
    return op.emitError("concurrent assertion has no resolved clock"),
           failure();
  auto clockEvent = dyn_cast<semantic::SVSignalEventControlOp>(clock);
  if (!clockEvent || clockEvent.getHasIff() ||
      getChildren(clockEvent).size() != 1 ||
      !isAddressableExpression(getChildren(clockEvent).front()))
    return emitError(getSemanticLocation(clock))
               << "AOT concurrent monitors currently require one direct "
                  "signal edge clock without iff",
           failure();

  if (auto disabled =
          dyn_cast_or_null<semantic::SVDisableIffAssertionExprOp>(property)) {
    SmallVector<Operation *> nested = getChildren(disabled);
    if (nested.size() != 2)
      return disabled.emitError("malformed disable iff assertion"), failure();
    disable = nested.front();
    property = unwrapAssertionInstance(nested.back());
  }
  if (!property)
    return op.emitError("disable iff wraps an unsupported assertion instance"),
           failure();

  bool firstMatch = false;
  if (auto first = dyn_cast<semantic::SVFirstMatchAssertionExprOp>(property)) {
    SmallVector<Operation *> nested = getChildren(first);
    if (first.getMatchItemCount() != 0 || nested.size() != 1)
      return emitError(getSemanticLocation(first))
                 << "bounded first_match does not yet support match items",
             failure();
    firstMatch = true;
  }

  bool multiClockAttempt =
      op->hasAttr("obelisk_sim.multiclock_sequence_attempt");
  MultiClockSequence multiClockSequence;
  FailureOr<MultiClockSequence> compiledMultiClock =
      compileMultiClockSequence(property, clock);
  if (succeeded(compiledMultiClock) && compiledMultiClock->changesClock &&
      !compiledMultiClock->hasLeadingDelay)
    return emitError(getSemanticLocation(property))
               << "immediate cross-clock sequence terms are not executable "
                  "yet; the supported multi-clock handoff requires a "
                  "leading ##1",
           failure();
  if (succeeded(compiledMultiClock) && compiledMultiClock->changesClock &&
      compiledMultiClock->hasLeadingDelay) {
    multiClockSequence = std::move(*compiledMultiClock);
    if (localInstance || disable || expectMonitor || firstMatch ||
        op.getAssertionKind() == semantic::SVAssertionKind::CoverSequence)
      return emitError(getSemanticLocation(property))
                 << "multi-clock sequence handoff currently requires a plain "
                    "property directive without locals, disable iff, expect, "
                    "or cover-sequence per-match accounting",
             failure();
    for (const MultiClockSequenceStage &stage : multiClockSequence.stages) {
      if (!isStaticDirectClock(stage.clock))
        return emitError(getSemanticLocation(stage.clock))
                   << "multi-clock sequence stages require one direct signal "
                      "edge clock without iff",
               failure();
    }

    if (!multiClockAttempt) {
      auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
      uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
      uint64_t occurrence = nextForkOrdinal;
      std::string identity = (function.getSymName() + ".$multiclock_attempt." +
                              Twine(node) + "." + Twine(occurrence))
                                 .str();
      Attribute previousCodeUnit = op->getAttr("obelisk_sim.fork_code_unit_id");
      op->setAttr("obelisk_sim.fork_code_unit_id",
                  builder.getI64IntegerAttr(stableCodeUnitID(identity)));
      op->setAttr("obelisk_sim.multiclock_sequence_attempt",
                  builder.getUnitAttr());
      FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> attempt =
          outlineForkBranch(op, node, /*branchIndex=*/24,
                            /*captureReferences=*/true);
      op->removeAttr("obelisk_sim.multiclock_sequence_attempt");
      if (previousCodeUnit)
        op->setAttr("obelisk_sim.fork_code_unit_id", previousCodeUnit);
      else
        op->removeAttr("obelisk_sim.fork_code_unit_id");
      if (failed(attempt))
        return failure();

      attempt->first->setAttr("obelisk_sim.multiclock_sequence_attempt_actor",
                              builder.getUnitAttr());
      attempt->first->setAttr("obelisk_sim.detached_controls",
                              builder.getUnitAttr());
      attempt->first->setAttr(
          "home_region", sim::EventRegionAttr::get(function.getContext(),
                                                   sim::EventRegion::Observed));
      attempt->first->setAttr(
          "domain", sim::ExecutionDomainAttr::get(
                        function.getContext(), sim::ExecutionDomain::Design));
      function->setAttr("obelisk_sim.multiclock_sequence_monitor",
                        builder.getUnitAttr());
      function->setAttr("home_region",
                        sim::EventRegionAttr::get(function.getContext(),
                                                  sim::EventRegion::Observed));
      function->setAttr(
          "domain", sim::ExecutionDomainAttr::get(
                        function.getContext(), sim::ExecutionDomain::Design));

      Block *wait = addBlock();
      Block *start = addBlock();
      emitBranch(wait);
      setCurrent(wait);
      if (failed(emitEventSuspend(clock, start)))
        return failure();
      wait->getTerminator()->setAttr(
          "resume_region",
          sim::EventRegionAttr::get(function.getContext(),
                                    sim::EventRegion::Observed));
      setCurrent(start);
      sim::SimSpawnOp::create(builder, location,
                              attempt->first.getSymNameAttr(), attempt->second,
                              ArrayAttr{}, ArrayAttr{});
      cf::BranchOp::create(builder, location, wait);
      return success();
    }
  } else if (multiClockAttempt) {
    return emitError(getSemanticLocation(property))
               << "malformed multi-clock sequence attempt",
           failure();
  }

  semantic::SVBinaryAssertionExprOp implication;
  FixedSequence sequence;
  FixedSequence antecedentSequence;
  FixedSequenceAlternatives sequenceAlternatives;
  PersistentRepetitionSequence persistentRepetition;
  bool hasPersistentRepetition = false;
  bool nonoverlapped = false;
  bool followedBy = false;
  if (multiClockAttempt) {
    // The staged actor below owns temporal evaluation. Keep the ordinary
    // validation path structurally nonempty without constructing bitset
    // monitor state that it will never use.
    sequence.ages.resize(1);
  } else if (auto binary =
                 dyn_cast_or_null<semantic::SVBinaryAssertionExprOp>(property);
             binary &&
             (binary.getOperatorKind() ==
                  semantic::SVAssertionBinaryOperator::OverlappedImplication ||
              binary.getOperatorKind() == semantic::SVAssertionBinaryOperator::
                                              NonOverlappedImplication ||
              binary.getOperatorKind() ==
                  semantic::SVAssertionBinaryOperator::OverlappedFollowedBy ||
              binary.getOperatorKind() == semantic::SVAssertionBinaryOperator::
                                              NonOverlappedFollowedBy)) {
    SmallVector<Operation *> operands = getChildren(binary);
    if (operands.size() != 2)
      return binary.emitError("malformed implication/followed-by property"),
             failure();
    FailureOr<FixedSequence> lhs = compileFixedSequence(operands.front());
    FailureOr<FixedSequence> rhs = compileFixedSequence(operands.back());
    if (failed(lhs) || lhs->ages.empty()) {
      if (diagnoseUnsupportedConcurrentFeature(operands.front(),
                                               /*nested=*/true))
        return failure();
      return binary.emitError(
                 "AOT implication/followed-by antecedent must be one "
                 "deterministic bounded sequence within the 63-cycle "
                 "horizon"),
             failure();
    }
    if (failed(rhs) || rhs->ages.empty()) {
      if (diagnoseUnsupportedConcurrentFeature(operands.back(),
                                               /*nested=*/true))
        return failure();
      return binary.emitError(
                 "AOT implication/followed-by consequent must be one "
                 "deterministic bounded sequence within the 63-cycle "
                 "horizon"),
             failure();
    }
    implication = binary;
    antecedentSequence = std::move(*lhs);
    sequence = std::move(*rhs);
    nonoverlapped =
        binary.getOperatorKind() ==
            semantic::SVAssertionBinaryOperator::NonOverlappedImplication ||
        binary.getOperatorKind() ==
            semantic::SVAssertionBinaryOperator::NonOverlappedFollowedBy;
    followedBy =
        binary.getOperatorKind() ==
            semantic::SVAssertionBinaryOperator::OverlappedFollowedBy ||
        binary.getOperatorKind() ==
            semantic::SVAssertionBinaryOperator::NonOverlappedFollowedBy;
    if (followedBy)
      function->setAttr("obelisk_sim.followed_by_monitor",
                        builder.getUnitAttr());
  } else {
    if (FailureOr<PersistentRepetitionSequence> persistent =
            compilePersistentRepetition(property);
        succeeded(persistent)) {
      persistentRepetition = std::move(*persistent);
      hasPersistentRepetition = true;
      // The persistent monitor owns its own token state below. Keep ordinary
      // bounded validation structurally nonempty without allocating the fixed
      // age bitset.
      sequence.ages.resize(1);
    } else {
      FailureOr<FixedSequenceAlternatives> compiled =
          compileFixedSequenceAlternatives(property, clock);
      if (failed(compiled) || compiled->empty()) {
        if (diagnoseUnsupportedConcurrentFeature(property))
          return failure();
        return emitError(getSemanticLocation(property))
                   << "bounded AOT sequence compilation failed: the property "
                      "either exceeds the 63-cycle or 256-alternative limit, "
                      "or combines bounded operators in a form whose endpoint "
                      "ordering is not executable yet",
               failure();
      }
      if (!cover) {
        if (std::optional<BooleanMinimizationStats> stats =
                minimizeBooleanAlternatives(*compiled)) {
          function->setAttr("obelisk_sim.sva_boolean_solver",
                            builder.getStringAttr(stats->backend));
          function->setAttr("obelisk_sim.sva_boolean_solver_queries",
                            builder.getI64IntegerAttr(stats->solverQueries));
          function->setAttr(
              "obelisk_sim.sva_boolean_alternatives_before",
              builder.getI64IntegerAttr(stats->alternativesBefore));
          function->setAttr(
              "obelisk_sim.sva_boolean_alternatives_after",
              builder.getI64IntegerAttr(stats->alternativesAfter));
          function->setAttr("obelisk_sim.sva_boolean_literals_before",
                            builder.getI64IntegerAttr(stats->literalsBefore));
          function->setAttr("obelisk_sim.sva_boolean_literals_after",
                            builder.getI64IntegerAttr(stats->literalsAfter));
        }
      }
      if (compiled->size() == 1)
        sequence = std::move(compiled->front());
      else
        sequenceAlternatives = std::move(*compiled);
    }
  }
  bool branchingSequence = !sequenceAlternatives.empty();
  bool boundedFirstMatch =
      firstMatch ||
      llvm::any_of(sequenceAlternatives, [](const FixedSequence &alternative) {
        return !alternative.firstMatchBoundaries.empty();
      });
  if (boundedFirstMatch)
    function->setAttr("obelisk_sim.first_match_monitor", builder.getUnitAttr());
  if ((!branchingSequence && sequence.ages.empty()) ||
      llvm::any_of(sequenceAlternatives,
                   [](const FixedSequence &alternative) {
                     return alternative.ages.empty() ||
                            alternative.ages.size() > 63;
                   }) ||
      sequence.ages.size() > 63)
    return op.emitError("concurrent monitor horizon must be 1..63 cycles"),
           failure();
  if (implication && antecedentSequence.ages.size() + sequence.ages.size() > 63)
    return op.emitError(
               "combined implication/followed-by antecedent/consequent state "
               "exceeds the 63-cycle bounded monitor horizon"),
           failure();
  if (implication && localInstance && antecedentSequence.ages.size() != 1)
    return emitError(getSemanticLocation(implication))
               << "multi-cycle implication/followed-by antecedents do not yet "
                  "compose with assertion locals",
           failure();
  if (implication && !localInstance &&
      llvm::any_of(antecedentSequence.ages, [](const FixedSequenceAge &age) {
        return !age.matchItems.empty();
      }))
    return emitError(getSemanticLocation(implication))
               << "implication/followed-by antecedent match items require "
                  "assertion local flow",
           failure();
  if (hasPersistentRepetition && (localInstance || implication || disable ||
                                  expectMonitor || firstMatch || coverSequence))
    return emitError(getSemanticLocation(property))
               << "persistent [->]/[=] repetition currently requires a plain "
                  "assert, assume, cover-property, or restrict directive "
                  "without locals, implication, disable iff, first_match, "
                  "expect, or cover-sequence per-match accounting",
           failure();
  if (branchingSequence &&
      (localInstance || implication || disable || expectMonitor))
    return emitError(getSemanticLocation(property))
               << "branching bounded sequences currently require a plain "
                  "concurrent directive without locals, implication, "
                  "disable iff, or expect",
           failure();
  if (branchingSequence &&
      llvm::any_of(sequenceAlternatives, [](const FixedSequence &alternative) {
        return llvm::any_of(alternative.ages, [](const FixedSequenceAge &age) {
          return !age.matchItems.empty();
        });
      }))
    return emitError(getSemanticLocation(property))
               << "branching bounded sequences do not yet support match "
                  "items",
           failure();
  if (!localInstance &&
      llvm::any_of(sequence.ages, [](const FixedSequenceAge &age) {
        return !age.matchItems.empty();
      }))
    return emitError(getSemanticLocation(property))
               << "assertion match items require a supported local-variable "
                  "assertion instance",
           failure();

  if (expectMonitor) {
    if (localInstance || implication || disable ||
        llvm::any_of(sequence.ages, [](const FixedSequenceAge &age) {
          return !age.matchItems.empty();
        }))
      return emitError(location)
                 << "expect currently requires a fixed sequence without "
                    "locals, implication, disable iff, or match items",
             failure();
    auto donePath =
        op->getAttrOfType<StringAttr>("obelisk_sim.expect_done_path");
    auto resultPath =
        op->getAttrOfType<StringAttr>("obelisk_sim.expect_result_path");
    Value completed = donePath ? values.lookup(donePath.getValue()) : Value{};
    Value resultStorage =
        resultPath ? lvalues.lookup(resultPath.getValue()) : Value{};
    if (!resultStorage && resultPath)
      resultStorage = values.lookup(resultPath.getValue());
    if (!completed || !isa<sim::EventType>(completed.getType()) ||
        !resultStorage || !isa<sim::RefType>(resultStorage.getType()))
      return emitError(location) << "expect monitor has no completion captures",
             failure();

    function->setAttr("home_region",
                      sim::EventRegionAttr::get(function.getContext(),
                                                sim::EventRegion::Observed));
    function->setAttr(
        "domain", sim::ExecutionDomainAttr::get(function.getContext(),
                                                sim::ExecutionDomain::Design));
    Block *successBlock = addBlock();
    Block *failureBlock = addBlock();
    Block *wait = addBlock();
    emitBranch(wait);

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });
    for (auto [age, sequenceAge] : llvm::enumerate(sequence.ages)) {
      Block *sample = addBlock();
      setCurrent(wait);
      if (failed(emitEventSuspend(clock, sample)))
        return failure();
      wait->getTerminator()->setAttr(
          "resume_region",
          sim::EventRegionAttr::get(function.getContext(),
                                    sim::EventRegion::Observed));
      setCurrent(sample);
      Value matches = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      for (Operation *predicate : sequenceAge.predicates) {
        FailureOr<Value> value = lowerExpression(predicate);
        if (failed(value))
          return failure();
        FailureOr<Value> truth =
            truthValue(*value, getSemanticLocation(predicate));
        if (failed(truth))
          return failure();
        matches = arith::AndIOp::create(builder, location, matches, *truth);
      }
      for (Operation *predicate : sequenceAge.negatedPredicates) {
        FailureOr<Value> value = lowerExpression(predicate);
        if (failed(value))
          return failure();
        FailureOr<Value> truth =
            truthValue(*value, getSemanticLocation(predicate));
        if (failed(truth))
          return failure();
        Value negated = arith::XOrIOp::create(
            builder, location, *truth,
            arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                      builder.getBoolAttr(true)));
        matches = arith::AndIOp::create(builder, location, matches, negated);
      }
      Block *matched =
          age + 1 == sequence.ages.size() ? successBlock : addBlock();
      cf::CondBranchOp::create(builder, location, matches, matched,
                               ValueRange{}, failureBlock, ValueRange{});
      wait = matched;
    }
    auto finish = [&](Block *block, bool passed) {
      setCurrent(block);
      Value result = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(passed));
      sim::SimRefStoreOp::create(builder, location, result, resultStorage);
      sim::SimEventTriggerOp::create(builder, location, completed, Value{},
                                     builder.getBoolAttr(false),
                                     sim::EventSiteAttr{});
      sim::SimReturnOp::create(builder, location, ValueRange{});
    };
    finish(successBlock, true);
    finish(failureBlock, false);
    setCurrent(addBlock());
    return success();
  }
  // IEEE 1800 evaluates sampled predicates and monitor state in Observed.
  function->setAttr("home_region",
                    sim::EventRegionAttr::get(function.getContext(),
                                              sim::EventRegion::Observed));

  Type stateType = builder.getI64Type();
  Value zero = arith::ConstantOp::create(builder, location, stateType,
                                         builder.getI64IntegerAttr(0));
  bool needsState =
      disable || (!branchingSequence && sequence.ages.size() > 1) ||
      (implication && (nonoverlapped || antecedentSequence.ages.size() > 1));
  if (implication && antecedentSequence.ages.size() > 1)
    function->setAttr(
        "obelisk_sim.bounded_antecedent_horizon",
        builder.getI64IntegerAttr(antecedentSequence.ages.size()));
  Value stateStorage;
  if (needsState)
    stateStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);

  Value disableEpoch;
  Value initialDisable;
  if (disable) {
    // `disable iff` is unsampled and asynchronous. Bind its two-state truth
    // value as a computed observer: every false-to-true transition wakes a
    // cold Observed actor which clears live attempts and advances an epoch.
    // Reactive reports capture that epoch and become no-ops when cancellation
    // overtakes an already queued callback in the same time slot.
    FailureOr<Value> current = lowerExpression(disable);
    if (failed(current))
      return failure();
    FailureOr<Value> truth = truthValue(*current, getSemanticLocation(disable));
    if (failed(truth))
      return failure();
    initialDisable = *truth;
    FailureOr<Value> observer = bindObserver(disable);
    if (failed(observer))
      return emitError(getSemanticLocation(disable))
                 << "disable iff expression has no asynchronous observer; "
                    "its operands are not executable",
             failure();
    auto observerBinding = observer->getDefiningOp<sim::SimObserverBindOp>();
    if (!observerBinding)
      return emitError(getSemanticLocation(disable))
                 << "disable iff expression has no observer binding",
             failure();
    for (Value capture : observerBinding.getValues())
      if (isa<sim::RefType>(capture.getType()) &&
          !isStaticallyAllocatedOverrideTarget(capture))
        return emitError(getSemanticLocation(disable))
                   << "disable iff cannot asynchronously observe an "
                      "automatic variable",
               failure();
    auto design = function->getParentOfType<sim::SimDesignOp>();
    sim::SimFuncOp observerEvaluator =
        design ? design.lookupSymbol<sim::SimFuncOp>(
                     observerBinding.getEvaluator())
               : sim::SimFuncOp{};
    if (!observerEvaluator)
      return emitError(getSemanticLocation(disable))
                 << "disable iff observer evaluator is missing",
             failure();
    observerEvaluator->setAttr("obelisk_sim.concurrent_cancel_observer",
                               builder.getUnitAttr());
    observerEvaluator->setAttr("obelisk_sim.detached_controls",
                               builder.getUnitAttr());
    disableEpoch = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);

    if (!design)
      return function.emitError(
                 "concurrent disable outlining requires a simulation design"),
             failure();
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    std::string symbol =
        (function.getSymName() + ".$concurrent_cancel." + Twine(node)).str();
    std::string identity =
        (function.getSymName() + ".$concurrent_disable." + Twine(node)).str();
    uint64_t codeUnitID = stableCodeUnitID(identity);
    uint64_t scopeID = 0;
    std::string parentHierarchy = function.getSymName().str();
    uint64_t parentID = function.getCodeUnitId().value_or(0);
    for (sim::SimCodeUnitDeclOp declaration :
         design.getBody().front().getOps<sim::SimCodeUnitDeclOp>()) {
      if (declaration.getId() != parentID)
        continue;
      scopeID = declaration.getScopeId();
      parentHierarchy = declaration.getHierarchicalName().str();
      break;
    }
    std::string hierarchy =
        (Twine(parentHierarchy) + ".$concurrent_disable." + Twine(node)).str();

    Value context = function.getBody().front().getArgument(0);
    SmallVector<Value> captures{context};
    llvm::append_range(captures, observerBinding.getValues());
    unsigned initialDisableIndex = captures.size();
    captures.push_back(initialDisable);
    unsigned stateStorageIndex = captures.size();
    captures.push_back(stateStorage);
    unsigned disableEpochIndex = captures.size();
    captures.push_back(disableEpoch);
    SmallVector<Type> inputs;
    for (Value capture : captures)
      inputs.push_back(capture.getType());
    SmallVector<DictionaryAttr> argumentAttrs;
    for (auto [index, capture] : llvm::enumerate(captures)) {
      SmallVector<NamedAttribute> metadata;
      if (auto argument = dyn_cast<BlockArgument>(capture);
          argument && argument.getOwner() == &function.getBody().front()) {
        if (DictionaryAttr source =
                function.getArgAttrDict(argument.getArgNumber()))
          llvm::append_range(metadata, source);
      }
      if (metadata.empty())
        metadata.push_back(builder.getNamedAttr(
            "obelisk_sim.capture_kind",
            sim::CaptureKindAttr::get(function.getContext(),
                                      index == 0 ? sim::CaptureKind::Context
                                                 : sim::CaptureKind::Formal)));
      if (isa<sim::RefType>(capture.getType()) &&
          !isStaticallyAllocatedOverrideTarget(capture))
        metadata.push_back(builder.getNamedAttr(
            "obelisk_sim.automatic_reference_capture", builder.getUnitAttr()));
      argumentAttrs.push_back(builder.getDictionaryAttr(metadata));
    }

    OpBuilder outlineBuilder(function);
    outlineBuilder.setInsertionPoint(function);
    sim::SimCodeUnitDeclOp::create(
        outlineBuilder, getSemanticLocation(disable), codeUnitID, scopeID,
        sim::EntryKind::Fork, outlineBuilder.getStringAttr(hierarchy),
        outlineBuilder.getStringAttr("concurrent assertion disable observer"),
        outlineBuilder.getUnitAttr());
    SmallVector<NamedAttribute> attributes{
        outlineBuilder.getNamedAttr(bindingsAttrName,
                                    outlineBuilder.getArrayAttr({})),
        outlineBuilder.getNamedAttr(
            "code_unit_id", outlineBuilder.getI64IntegerAttr(codeUnitID)),
        outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(
            "home_region",
            sim::EventRegionAttr::get(function.getContext(),
                                      sim::EventRegion::Reactive)),
        outlineBuilder.getNamedAttr(
            "domain", sim::ExecutionDomainAttr::get(
                          function.getContext(), sim::ExecutionDomain::Design)),
        outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                    outlineBuilder.getStringAttr(hierarchy)),
    };
    sim::SimFuncOp cancel = sim::SimFuncOp::create(
        outlineBuilder, getSemanticLocation(disable), symbol,
        FunctionType::get(function.getContext(), inputs, TypeRange{}),
        sim::EntryKind::Fork, attributes, argumentAttrs);
    SymbolTable::setSymbolVisibility(cancel, SymbolTable::Visibility::Private);
    cancel->setAttr("obelisk_sim.concurrent_cancel", builder.getUnitAttr());
    cancel->setAttr("obelisk_sim.detached_controls", builder.getUnitAttr());
    cancel->setAttr("obelisk_sim.priority_signal_resume",
                    builder.getUnitAttr());

    Block &entry = cancel.getBody().front();
    Block *waitDisable = new Block;
    Block *cancelLiveAttempts = new Block;
    waitDisable->addArgument(builder.getI1Type(), getSemanticLocation(disable));
    cancel.getBody().push_back(waitDisable);
    cancel.getBody().push_back(cancelLiveAttempts);
    OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
    cf::BranchOp::create(entryBuilder, getSemanticLocation(disable),
                         waitDisable,
                         ValueRange{entry.getArgument(initialDisableIndex)});
    OpBuilder waitBuilder = OpBuilder::atBlockEnd(waitDisable);
    SmallVector<Value> reboundValues;
    for (unsigned index = 1; index != initialDisableIndex; ++index)
      reboundValues.push_back(entry.getArgument(index));
    auto reboundObserver = sim::SimObserverBindOp::create(
        waitBuilder, getSemanticLocation(disable),
        observerBinding.getResult().getType(),
        observerBinding.getEvaluatorAttr(), reboundValues,
        observerBinding.getCaptureCountAttr());
    SmallVector<Value> observed{reboundObserver.getResult(),
                                waitDisable->getArgument(0)};
    auto cancelWait = sim::SimSuspendObserveOp::create(
        waitBuilder, getSemanticLocation(disable), observed, 0,
        ArrayRef<int32_t>{static_cast<int32_t>(sim::EdgeKind::Posedge)},
        ArrayRef<int32_t>{-1}, sim::ContinuationSiteAttr{},
        sim::EventRegionAttr::get(function.getContext(),
                                  sim::EventRegion::Reactive),
        cancelLiveAttempts);
    cancelWait->setAttr("obelisk_sim.concurrent_cancel_level_true",
                        builder.getUnitAttr());

    OpBuilder cancelBuilder = OpBuilder::atBlockEnd(cancelLiveAttempts);
    Value cancelZero = arith::ConstantOp::create(
        cancelBuilder, getSemanticLocation(disable), stateType,
        cancelBuilder.getI64IntegerAttr(0));
    sim::SimRefStoreOp::create(cancelBuilder, getSemanticLocation(disable),
                               cancelZero,
                               entry.getArgument(stateStorageIndex));
    Value epoch = sim::SimRefLoadOp::create(
        cancelBuilder, getSemanticLocation(disable), stateType,
        entry.getArgument(disableEpochIndex));
    Value one = arith::ConstantOp::create(
        cancelBuilder, getSemanticLocation(disable), stateType,
        cancelBuilder.getI64IntegerAttr(1));
    Value nextEpoch = arith::AddIOp::create(
        cancelBuilder, getSemanticLocation(disable), epoch, one);
    sim::SimRefStoreOp::create(cancelBuilder, getSemanticLocation(disable),
                               nextEpoch, entry.getArgument(disableEpochIndex));
    Value currentTrue = arith::ConstantOp::create(
        cancelBuilder, getSemanticLocation(disable), builder.getI1Type(),
        builder.getBoolAttr(true));
    cf::BranchOp::create(cancelBuilder, getSemanticLocation(disable),
                         waitDisable, ValueRange{currentTrue});

    sim::SimSpawnOp::create(builder, getSemanticLocation(disable),
                            cancel.getSymNameAttr(), captures, ArrayAttr{},
                            ArrayAttr{});
  }

  struct ReportCallback {
    sim::SimFuncOp function;
    SmallVector<Value> captures;
    Location location;
  };
  std::optional<ReportCallback> passReport;
  std::optional<ReportCallback> failReport;
  auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
  uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
  auto outlineReport =
      [&](bool passed, std::optional<ReportCallback> &report) -> LogicalResult {
    if (!observable || (!passed && cover))
      return success();
    Operation *selected = nullptr;
    bool defaultFailure = false;
    if (passed && op.getHasPassAction())
      selected = children[prefix + 1];
    else if (!passed && op.getHasFailAction())
      selected = children[prefix + 1 + op.getHasPassAction()];
    else if (!passed && assertion)
      defaultFailure = true;
    if (!selected && !defaultFailure)
      return success();
    if (selected && isa<semantic::SVEmptyStatementOp>(selected))
      return success();

    Operation *outlined = defaultFailure ? op.getOperation() : selected;
    unsigned ordinal = passed ? 0 : (defaultFailure ? 2 : 1);
    std::string identity = (function.getSymName() + ".$concurrent_report." +
                            Twine(node) + "." + Twine(ordinal))
                               .str();
    Attribute previousCodeUnit =
        outlined->getAttr("obelisk_sim.fork_code_unit_id");
    outlined->setAttr("obelisk_sim.fork_code_unit_id",
                      builder.getI64IntegerAttr(stableCodeUnitID(identity)));
    if (defaultFailure)
      op->setAttr("obelisk_sim.default_assertion_failure",
                  builder.getUnitAttr());
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
        outlineForkBranch(outlined, node, ordinal,
                          /*captureReferences=*/true);
    if (defaultFailure)
      op->removeAttr("obelisk_sim.default_assertion_failure");
    if (previousCodeUnit)
      outlined->setAttr("obelisk_sim.fork_code_unit_id", previousCodeUnit);
    else
      outlined->removeAttr("obelisk_sim.fork_code_unit_id");
    if (failed(callback))
      return failure();

    callback->first->setAttr(
        "home_region", sim::EventRegionAttr::get(function.getContext(),
                                                 sim::EventRegion::Reactive));
    callback->first->setAttr(
        "domain", sim::ExecutionDomainAttr::get(function.getContext(),
                                                sim::ExecutionDomain::Design));
    callback->first->setAttr("obelisk_sim.concurrent_report",
                             builder.getUnitAttr());
    callback->first->setAttr("obelisk_sim.detached_controls",
                             builder.getUnitAttr());
    if (disableEpoch) {
      sim::SimFuncOp evaluator = callback->first;
      SmallVector<Type> inputTypes(evaluator.getFunctionType().getInputs());
      inputTypes.push_back(disableEpoch.getType());
      inputTypes.push_back(stateType);
      evaluator.setFunctionType(
          FunctionType::get(function.getContext(), inputTypes, TypeRange{}));
      Block &entry = evaluator.getBody().front();
      BlockArgument epochReference =
          entry.addArgument(disableEpoch.getType(), location);
      BlockArgument expectedEpoch = entry.addArgument(stateType, location);
      SmallVector<Attribute> argumentAttrs;
      if (ArrayAttr attrs = evaluator.getArgAttrsAttr())
        llvm::append_range(argumentAttrs, attrs);
      while (argumentAttrs.size() + 2 < inputTypes.size())
        argumentAttrs.push_back(builder.getDictionaryAttr({}));
      argumentAttrs.push_back(builder.getDictionaryAttr({
          builder.getNamedAttr(
              "obelisk_sim.capture_kind",
              sim::CaptureKindAttr::get(function.getContext(),
                                        sim::CaptureKind::Formal)),
          builder.getNamedAttr("obelisk_sim.automatic_reference_capture",
                               builder.getUnitAttr()),
      }));
      argumentAttrs.push_back(builder.getDictionaryAttr({builder.getNamedAttr(
          "obelisk_sim.capture_kind",
          sim::CaptureKindAttr::get(function.getContext(),
                                    sim::CaptureKind::Formal))}));
      evaluator.setArgAttrsAttr(builder.getArrayAttr(argumentAttrs));

      Block *body = entry.splitBlock(entry.begin());
      Block *canceled = new Block;
      evaluator.getBody().push_back(canceled);
      OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
      Value currentEpoch = sim::SimRefLoadOp::create(entryBuilder, location,
                                                     stateType, epochReference);
      Value current = arith::CmpIOp::create(entryBuilder, location,
                                            arith::CmpIPredicate::eq,
                                            currentEpoch, expectedEpoch);
      cf::CondBranchOp::create(entryBuilder, location, current, body, canceled);
      OpBuilder canceledBuilder = OpBuilder::atBlockEnd(canceled);
      sim::SimReturnOp::create(canceledBuilder, location, ValueRange{});
      callback->second.push_back(disableEpoch);
    }
    report.emplace(ReportCallback{callback->first, std::move(callback->second),
                                  getSemanticLocation(outlined)});
    return success();
  };
  if (failed(outlineReport(true, passReport)) ||
      failed(outlineReport(false, failReport)))
    return failure();
  auto scheduleResult = [&](bool passed) {
    std::optional<ReportCallback> &report = passed ? passReport : failReport;
    if (!report)
      return;
    SmallVector<Value> captures(report->captures);
    if (disableEpoch)
      captures.push_back(sim::SimRefLoadOp::create(builder, report->location,
                                                   stateType, disableEpoch));
    sim::SimSpawnOp::create(builder, report->location,
                            report->function.getSymNameAttr(), captures,
                            ArrayAttr{}, ArrayAttr{});
  };

  if (hasPersistentRepetition) {
    function->setAttr("obelisk_sim.persistent_repetition_monitor",
                      builder.getUnitAttr());
    function->setAttr(
        "obelisk_sim.persistent_repetition_kind",
        builder.getStringAttr(semantic::stringifySVSequenceRepetitionKind(
            persistentRepetition.kind)));
    function->setAttr("obelisk_sim.persistent_repetition_min",
                      builder.getI64IntegerAttr(persistentRepetition.minimum));
    function->setAttr("obelisk_sim.persistent_repetition_max",
                      builder.getI64IntegerAttr(persistentRepetition.maximum));
    function->setAttr("obelisk_sim.persistent_repetition_dfa",
                      builder.getUnitAttr());

    struct TokenState {
      uint64_t occurrences = 0;
      bool pending = false;
      Value storage;
    };
    SmallVector<TokenState> tokenStates;
    DenseMap<std::pair<uint64_t, uint8_t>, unsigned> tokenStateIndices;
    auto addTokenState = [&](uint64_t occurrences, bool pending) {
      unsigned index = tokenStates.size();
      tokenStateIndices.try_emplace(
          std::pair<uint64_t, uint8_t>{occurrences,
                                       static_cast<uint8_t>(pending)},
          index);
      tokenStates.push_back(
          {occurrences, pending,
           sim::SimRefAllocOp::create(
               builder, location,
               sim::RefType::get(function.getContext(), stateType), zero)});
    };
    if (!persistentRepetition.hasTerminal) {
      for (uint64_t count = 0; count < persistentRepetition.minimum; ++count)
        addTokenState(count, false);
    } else if (persistentRepetition.kind ==
               semantic::SVSequenceRepetitionKind::Nonconsecutive) {
      for (uint64_t count = 0; count <= persistentRepetition.maximum; ++count)
        addTokenState(count, false);
    } else {
      for (uint64_t count = 0; count < persistentRepetition.maximum; ++count)
        addTokenState(count, false);
      for (uint64_t count = persistentRepetition.minimum;
           count <= persistentRepetition.maximum; ++count)
        addTokenState(count, true);
    }
    function->setAttr("obelisk_sim.persistent_repetition_states",
                      builder.getI64IntegerAttr(tokenStates.size()));
    auto findTokenState = [&](uint64_t occurrences, bool pending) {
      auto found =
          tokenStateIndices.find({occurrences, static_cast<uint8_t>(pending)});
      assert(found != tokenStateIndices.end() &&
             "persistent repetition DFA destination must exist");
      return found->second;
    };

    Value prefixStateStorage;
    if (persistentRepetition.entry.ages.size() > 1)
      prefixStateStorage = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero);

    Block *wait = addBlock();
    Block *sample = addBlock();
    emitBranch(wait);
    setCurrent(wait);
    if (failed(emitEventSuspend(clock, sample)))
      return failure();
    wait->getTerminator()->setAttr(
        "resume_region", sim::EventRegionAttr::get(function.getContext(),
                                                   sim::EventRegion::Observed));
    setCurrent(sample);

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    Value falseValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    Value one = arith::ConstantOp::create(builder, location, stateType,
                                          builder.getI64IntegerAttr(1));
    DenseMap<Operation *, Value> predicateCache;
    auto evaluateAge = [&](const FixedSequenceAge &age) -> FailureOr<Value> {
      Value result = trueValue;
      auto evaluatePredicate = [&](Operation *predicate) -> FailureOr<Value> {
        if (auto found = predicateCache.find(predicate);
            found != predicateCache.end())
          return found->second;
        FailureOr<Value> value = lowerExpression(predicate);
        if (failed(value))
          return failure();
        FailureOr<Value> truth =
            truthValue(*value, getSemanticLocation(predicate));
        if (failed(truth))
          return failure();
        predicateCache[predicate] = *truth;
        return *truth;
      };
      for (Operation *predicate : age.predicates) {
        FailureOr<Value> truth = evaluatePredicate(predicate);
        if (failed(truth))
          return failure();
        result = arith::AndIOp::create(builder, location, result, *truth);
      }
      for (Operation *predicate : age.negatedPredicates) {
        FailureOr<Value> truth = evaluatePredicate(predicate);
        if (failed(truth))
          return failure();
        Value negated =
            arith::XOrIOp::create(builder, location, *truth, trueValue);
        result = arith::AndIOp::create(builder, location, result, negated);
      }
      return result;
    };
    auto negate = [&](Value value) {
      return arith::XOrIOp::create(builder, location, value, trueValue)
          .getResult();
    };
    auto selectCount = [&](Value condition, Value count) {
      return arith::SelectOp::create(builder, location, condition, count, zero)
          .getResult();
    };
    auto addCount = [&](Value &target, Value count) {
      target = arith::AddIOp::create(builder, location, target, count);
    };

    Value prefixState =
        prefixStateStorage
            ? sim::SimRefLoadOp::create(builder, location, stateType,
                                        prefixStateStorage)
                  .getResult()
            : zero;
    Value nextPrefixState = zero;
    Value entryCount = zero;
    Value failureCount = zero;
    for (uint64_t age = 1; age < persistentRepetition.entry.ages.size();
         ++age) {
      Value mask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1} << age));
      Value activeBits =
          arith::AndIOp::create(builder, location, prefixState, mask);
      Value active = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, activeBits, zero);
      FailureOr<Value> matches =
          evaluateAge(persistentRepetition.entry.ages[age]);
      if (failed(matches))
        return failure();
      Value advances =
          arith::AndIOp::create(builder, location, active, *matches);
      Value fails =
          arith::AndIOp::create(builder, location, active, negate(*matches));
      addCount(failureCount, selectCount(fails, one));
      if (age + 1 == persistentRepetition.entry.ages.size()) {
        addCount(entryCount, selectCount(advances, one));
      } else {
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
        nextPrefixState = arith::OrIOp::create(
            builder, location, nextPrefixState,
            arith::SelectOp::create(builder, location, advances, nextMask,
                                    zero));
      }
    }
    FailureOr<Value> starts =
        evaluateAge(persistentRepetition.entry.ages.front());
    if (failed(starts))
      return failure();
    addCount(failureCount, selectCount(negate(*starts), one));
    if (persistentRepetition.entry.ages.size() == 1) {
      addCount(entryCount, selectCount(*starts, one));
    } else {
      Value nextMask = arith::ConstantOp::create(builder, location, stateType,
                                                 builder.getI64IntegerAttr(2));
      nextPrefixState = arith::OrIOp::create(
          builder, location, nextPrefixState,
          arith::SelectOp::create(builder, location, *starts, nextMask, zero));
    }
    if (prefixStateStorage)
      sim::SimRefStoreOp::create(builder, location, nextPrefixState,
                                 prefixStateStorage);

    SmallVector<Value> amounts;
    SmallVector<Value> nextAmounts(tokenStates.size(), zero);
    amounts.reserve(tokenStates.size());
    for (const TokenState &state : tokenStates)
      amounts.push_back(sim::SimRefLoadOp::create(builder, location, stateType,
                                                  state.storage));
    addCount(amounts[findTokenState(0, false)], entryCount);

    FailureOr<Value> repeated = evaluateAge(persistentRepetition.term);
    if (failed(repeated))
      return failure();
    Value notRepeated = negate(*repeated);
    Value terminal = trueValue;
    Value notTerminal = falseValue;
    if (persistentRepetition.hasTerminal) {
      FailureOr<Value> evaluated = evaluateAge(persistentRepetition.terminal);
      if (failed(evaluated))
        return failure();
      terminal = *evaluated;
      notTerminal = negate(terminal);
    }

    Value successCount = zero;
    auto route = [&](unsigned destination, Value amount, Value condition) {
      addCount(nextAmounts[destination], selectCount(condition, amount));
    };
    auto succeed = [&](Value amount, Value condition) {
      addCount(successCount, selectCount(condition, amount));
    };
    auto fail = [&](Value amount, Value condition) {
      addCount(failureCount, selectCount(condition, amount));
    };

    for (auto [index, state] : llvm::enumerate(tokenStates)) {
      Value amount = amounts[index];
      if (!persistentRepetition.hasTerminal) {
        if (state.occurrences + 1 >= persistentRepetition.minimum) {
          succeed(amount, *repeated);
        } else {
          route(findTokenState(state.occurrences + 1, false), amount,
                *repeated);
        }
        route(index, amount, notRepeated);
        continue;
      }

      if (persistentRepetition.kind ==
          semantic::SVSequenceRepetitionKind::Nonconsecutive) {
        Value remaining = trueValue;
        if (state.occurrences >= persistentRepetition.minimum) {
          succeed(amount, terminal);
          remaining = notTerminal;
        }
        Value consumes =
            arith::AndIOp::create(builder, location, remaining, *repeated);
        Value waits =
            arith::AndIOp::create(builder, location, remaining, notRepeated);
        if (state.occurrences == persistentRepetition.maximum)
          fail(amount, consumes);
        else
          route(findTokenState(state.occurrences + 1, false), amount, consumes);
        route(index, amount, waits);
        continue;
      }

      if (!state.pending) {
        uint64_t nextCount = state.occurrences + 1;
        bool nextPending = nextCount >= persistentRepetition.minimum;
        route(findTokenState(nextCount, nextPending), amount, *repeated);
        route(index, amount, notRepeated);
        continue;
      }

      succeed(amount, terminal);
      if (state.occurrences == persistentRepetition.maximum) {
        fail(amount, notTerminal);
        continue;
      }
      Value consumes =
          arith::AndIOp::create(builder, location, notTerminal, *repeated);
      Value waits =
          arith::AndIOp::create(builder, location, notTerminal, notRepeated);
      route(findTokenState(state.occurrences + 1, true), amount, consumes);
      route(findTokenState(state.occurrences, false), amount, waits);
    }

    for (auto [state, nextAmount] : llvm::zip_equal(tokenStates, nextAmounts))
      sim::SimRefStoreOp::create(builder, location, nextAmount, state.storage);

    auto scheduleCount = [&](Value count, bool passed) {
      std::optional<ReportCallback> &report = passed ? passReport : failReport;
      if (!report)
        return;
      Block *loop = addBlock();
      Block *body = addBlock();
      Block *done = addBlock();
      loop->addArgument(stateType, location);
      cf::BranchOp::create(builder, location, loop, ValueRange{count});
      setCurrent(loop);
      Value remaining = loop->getArgument(0);
      Value nonzero = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, remaining, zero);
      cf::CondBranchOp::create(builder, location, nonzero, body, ValueRange{},
                               done, ValueRange{});
      setCurrent(body);
      scheduleResult(passed);
      Value next = arith::SubIOp::create(builder, location, remaining, one);
      cf::BranchOp::create(builder, location, loop, ValueRange{next});
      setCurrent(done);
    };
    scheduleCount(successCount, true);
    scheduleCount(failureCount, false);
    cf::BranchOp::create(builder, location, wait);
    return success();
  }

  if (multiClockAttempt) {
    function->setAttr("home_region",
                      sim::EventRegionAttr::get(function.getContext(),
                                                sim::EventRegion::Observed));
    function->setAttr(
        "domain", sim::ExecutionDomainAttr::get(function.getContext(),
                                                sim::ExecutionDomain::Design));
    Block *successBlock = addBlock();
    Block *failureBlock = addBlock();
    Block *wait = addBlock();
    emitBranch(wait);
    for (auto [index, stage] : llvm::enumerate(multiClockSequence.stages)) {
      Block *sample = addBlock();
      setCurrent(wait);
      if (failed(emitEventSuspend(stage.clock, sample)))
        return failure();
      wait->getTerminator()->setAttr(
          "resume_region",
          sim::EventRegionAttr::get(function.getContext(),
                                    sim::EventRegion::Observed));
      setCurrent(sample);

      bool savedSampleAssertionValues = sampleAssertionValues;
      Operation *savedSampledClock = activeSampledClock;
      sampleAssertionValues = true;
      activeSampledClock = stage.clock;
      Value matches = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      for (Operation *predicate : stage.predicates) {
        FailureOr<Value> value = lowerExpression(predicate);
        if (failed(value)) {
          sampleAssertionValues = savedSampleAssertionValues;
          activeSampledClock = savedSampledClock;
          return failure();
        }
        FailureOr<Value> truth =
            truthValue(*value, getSemanticLocation(predicate));
        if (failed(truth)) {
          sampleAssertionValues = savedSampleAssertionValues;
          activeSampledClock = savedSampledClock;
          return failure();
        }
        matches = arith::AndIOp::create(builder, location, matches, *truth);
      }
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;

      Block *matched = index + 1 == multiClockSequence.stages.size()
                           ? successBlock
                           : addBlock();
      cf::CondBranchOp::create(builder, location, matches, matched,
                               ValueRange{}, failureBlock, ValueRange{});
      wait = matched;
    }
    auto finish = [&](Block *block, bool passed) {
      setCurrent(block);
      scheduleResult(passed);
      sim::SimReturnOp::create(builder, location, ValueRange{});
    };
    finish(successBlock, true);
    finish(failureBlock, false);
    setCurrent(addBlock());
    return success();
  }

  auto cancelDisabledSample = [&](Block *wait) -> LogicalResult {
    if (!disable)
      return success();
    FailureOr<Value> currentDisable = lowerExpression(disable);
    if (failed(currentDisable))
      return failure();
    FailureOr<Value> disabled =
        truthValue(*currentDisable, getSemanticLocation(disable));
    if (failed(disabled))
      return failure();
    Block *cancelSample = addBlock();
    Block *evaluateSample = addBlock();
    cf::CondBranchOp::create(builder, getSemanticLocation(disable), *disabled,
                             cancelSample, ValueRange{}, evaluateSample,
                             ValueRange{});
    setCurrent(cancelSample);
    sim::SimRefStoreOp::create(builder, getSemanticLocation(disable), zero,
                               stateStorage);
    Value epoch = sim::SimRefLoadOp::create(
        builder, getSemanticLocation(disable), stateType, disableEpoch);
    Value one =
        arith::ConstantOp::create(builder, getSemanticLocation(disable),
                                  stateType, builder.getI64IntegerAttr(1));
    Value nextEpoch = arith::AddIOp::create(
        builder, getSemanticLocation(disable), epoch, one);
    sim::SimRefStoreOp::create(builder, getSemanticLocation(disable), nextEpoch,
                               disableEpoch);
    cf::BranchOp::create(builder, getSemanticLocation(disable), wait);
    setCurrent(evaluateSample);
    return success();
  };

  if (branchingSequence) {
    bool perMatchCover = coverSequence;
    function->setAttr("obelisk_sim.branching_sequence_monitor",
                      builder.getUnitAttr());
    function->setAttr("obelisk_sim.branching_sequence_alternatives",
                      builder.getI64IntegerAttr(sequenceAlternatives.size()));
    size_t vacuousAlternatives = llvm::count_if(
        sequenceAlternatives, [](const FixedSequence &alternative) {
          return alternative.vacuousSuccess;
        });
    if (vacuousAlternatives != 0)
      function->setAttr("obelisk_sim.vacuous_sequence_alternatives",
                        builder.getI64IntegerAttr(vacuousAlternatives));
    SmallVector<Value> alternativeStates;
    alternativeStates.reserve(sequenceAlternatives.size());
    for (const FixedSequence &alternative : sequenceAlternatives) {
      if (alternative.ages.size() == 1) {
        alternativeStates.push_back(Value{});
        continue;
      }
      alternativeStates.push_back(sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero));
    }

    Block *wait = addBlock();
    Block *sample = addBlock();
    emitBranch(wait);
    setCurrent(wait);
    if (failed(emitEventSuspend(clock, sample)))
      return failure();
    wait->getTerminator()->setAttr(
        "resume_region", sim::EventRegionAttr::get(function.getContext(),
                                                   sim::EventRegion::Observed));
    setCurrent(sample);

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    Value falseValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    size_t horizon = 0;
    for (const FixedSequence &alternative : sequenceAlternatives)
      horizon = std::max(horizon, alternative.ages.size());
    SmallVector<Value> activeAny(horizon, falseValue);
    SmallVector<Value> successAny(horizon, falseValue);
    SmallVector<Value> nonvacuousSuccessAny(horizon, falseValue);
    SmallVector<Value> continueAny(horizon, falseValue);
    SmallVector<SmallVector<Value>> survives(sequenceAlternatives.size());
    SmallVector<Value> starts(sequenceAlternatives.size(), falseValue);
    SmallVector<Value> nextStates(sequenceAlternatives.size(), zero);
    llvm::DenseMap<Operation *, Value> predicateCache;
    llvm::DenseMap<std::pair<Operation *, Operation *>, Value> caseGuardCache;
    llvm::DenseMap<Operation *, Value> caseSelectorCache;
    auto evaluateAge = [&](const FixedSequenceAge &age) -> FailureOr<Value> {
      Value result = trueValue;
      auto evaluatePredicate = [&](Operation *predicate) -> FailureOr<Value> {
        Value truth;
        if (auto found = predicateCache.find(predicate);
            found != predicateCache.end()) {
          truth = found->second;
        } else {
          FailureOr<Value> value = lowerExpression(predicate);
          if (failed(value))
            return failure();
          FailureOr<Value> converted =
              truthValue(*value, getSemanticLocation(predicate));
          if (failed(converted))
            return failure();
          truth = *converted;
          predicateCache[predicate] = truth;
        }
        return truth;
      };
      for (Operation *predicate : age.predicates) {
        FailureOr<Value> truth = evaluatePredicate(predicate);
        if (failed(truth))
          return failure();
        result = arith::AndIOp::create(builder, location, result, *truth);
      }
      for (Operation *predicate : age.negatedPredicates) {
        FailureOr<Value> truth = evaluatePredicate(predicate);
        if (failed(truth))
          return failure();
        Value negated =
            arith::XOrIOp::create(builder, location, *truth, trueValue);
        result = arith::AndIOp::create(builder, location, result, negated);
      }
      for (const FixedSequenceAge::CaseGuard &guard : age.caseGuards) {
        std::pair<Operation *, Operation *> key{guard.selector, guard.label};
        Value matched;
        if (auto found = caseGuardCache.find(key);
            found != caseGuardCache.end()) {
          matched = found->second;
        } else {
          Value selector;
          if (auto found = caseSelectorCache.find(guard.selector);
              found != caseSelectorCache.end()) {
            selector = found->second;
          } else {
            FailureOr<Value> loweredSelector = lowerExpression(guard.selector);
            if (failed(loweredSelector))
              return failure();
            selector = *loweredSelector;
            caseSelectorCache[guard.selector] = selector;
          }
          FailureOr<Value> comparison =
              lowerCaseLabel(selector, selector.getType(), guard.selector,
                             guard.label, semantic::SVCaseCondition::Normal);
          if (failed(comparison) || !(*comparison).getType().isInteger(1))
            return failure();
          matched = *comparison;
          caseGuardCache[key] = matched;
        }
        if (guard.negated)
          matched =
              arith::XOrIOp::create(builder, location, matched, trueValue);
        result = arith::AndIOp::create(builder, location, result, matched);
      }
      return result;
    };

    for (auto [alternativeIndex, alternative] :
         llvm::enumerate(sequenceAlternatives)) {
      survives[alternativeIndex].resize(alternative.ages.size(), falseValue);
      FailureOr<Value> start = evaluateAge(alternative.ages.front());
      if (failed(start))
        return failure();
      starts[alternativeIndex] = *start;
      if (alternative.ages.size() == 1) {
        successAny[0] =
            arith::OrIOp::create(builder, location, successAny[0], *start);
        if (!alternative.vacuousSuccess)
          nonvacuousSuccessAny[0] = arith::OrIOp::create(
              builder, location, nonvacuousSuccessAny[0], *start);
      } else
        continueAny[0] =
            arith::OrIOp::create(builder, location, continueAny[0], *start);

      if (alternative.ages.size() == 1)
        continue;
      Value state = sim::SimRefLoadOp::create(
          builder, location, stateType, alternativeStates[alternativeIndex]);
      for (uint64_t age = 1; age < alternative.ages.size(); ++age) {
        Value mask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << age));
        Value presentBits =
            arith::AndIOp::create(builder, location, state, mask);
        Value active = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, presentBits, zero);
        activeAny[age] =
            arith::OrIOp::create(builder, location, activeAny[age], active);
        FailureOr<Value> matches = evaluateAge(alternative.ages[age]);
        if (failed(matches))
          return failure();
        Value advances =
            arith::AndIOp::create(builder, location, active, *matches);
        survives[alternativeIndex][age] = advances;
        if (age + 1 == alternative.ages.size()) {
          successAny[age] = arith::OrIOp::create(builder, location,
                                                 successAny[age], advances);
          if (!alternative.vacuousSuccess)
            nonvacuousSuccessAny[age] = arith::OrIOp::create(
                builder, location, nonvacuousSuccessAny[age], advances);
        } else
          continueAny[age] = arith::OrIOp::create(builder, location,
                                                  continueAny[age], advances);
      }
    }

    llvm::DenseMap<Operation *, SmallVector<Value>> firstMatchSuccess;
    for (auto [alternativeIndex, alternative] :
         llvm::enumerate(sequenceAlternatives)) {
      for (const FirstMatchBoundary &boundary :
           alternative.firstMatchBoundaries) {
        auto [entry, inserted] =
            firstMatchSuccess.try_emplace(boundary.group, horizon, falseValue);
        (void)inserted;
        Value matched = boundary.age == 0
                            ? starts[alternativeIndex]
                            : survives[alternativeIndex][boundary.age];
        entry->second[boundary.age] = arith::OrIOp::create(
            builder, location, entry->second[boundary.age], matched);
      }
    }

    auto applyFirstMatchPriority = [&](Value enabled, size_t alternativeIndex,
                                       uint64_t age) -> Value {
      const FixedSequence &alternative = sequenceAlternatives[alternativeIndex];
      for (const FirstMatchBoundary &boundary :
           alternative.firstMatchBoundaries) {
        if (boundary.age < age)
          continue;
        Value groupMatched = firstMatchSuccess[boundary.group][age];
        Value selected = falseValue;
        if (boundary.age == age)
          selected = age == 0 ? starts[alternativeIndex]
                              : survives[alternativeIndex][age];
        Value allowed = arith::OrIOp::create(
            builder, location,
            arith::XOrIOp::create(builder, location, groupMatched, trueValue),
            selected);
        auto gated = arith::AndIOp::create(builder, location, enabled, allowed);
        gated->setAttr("obelisk_sim.first_match_priority",
                       builder.getUnitAttr());
        enabled = gated;
      }
      return enabled;
    };

    for (auto [alternativeIndex, alternative] :
         llvm::enumerate(sequenceAlternatives)) {
      if (alternative.ages.size() == 1)
        continue;
      Value nextState = zero;
      Value startEnabled = applyFirstMatchPriority(starts[alternativeIndex],
                                                   alternativeIndex, 0);
      if (!perMatchCover)
        startEnabled = arith::AndIOp::create(
            builder, location, startEnabled,
            arith::XOrIOp::create(builder, location, successAny[0], trueValue));
      Value firstMask = arith::ConstantOp::create(builder, location, stateType,
                                                  builder.getI64IntegerAttr(2));
      nextState = arith::OrIOp::create(
          builder, location, nextState,
          arith::SelectOp::create(builder, location, startEnabled, firstMask,
                                  zero));
      for (uint64_t age = 1; age + 1 < alternative.ages.size(); ++age) {
        Value enabled = applyFirstMatchPriority(survives[alternativeIndex][age],
                                                alternativeIndex, age);
        if (!perMatchCover)
          enabled = arith::AndIOp::create(
              builder, location, enabled,
              arith::XOrIOp::create(builder, location, successAny[age],
                                    trueValue));
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
        nextState = arith::OrIOp::create(
            builder, location, nextState,
            arith::SelectOp::create(builder, location, enabled, nextMask,
                                    zero));
      }
      nextStates[alternativeIndex] = nextState;
    }

    auto reportWhen = [&](Value condition, bool passed) {
      if (!observable)
        return;
      Block *report = addBlock();
      Block *continuation = addBlock();
      cf::CondBranchOp::create(builder, location, condition, report,
                               ValueRange{}, continuation, ValueRange{});
      setCurrent(report);
      scheduleResult(passed);
      emitBranch(continuation);
      setCurrent(continuation);
    };
    if (perMatchCover) {
      function->setAttr("obelisk_sim.cover_sequence_per_match",
                        builder.getUnitAttr());
      for (auto [alternativeIndex, alternative] :
           llvm::enumerate(sequenceAlternatives)) {
        Value matched = alternative.ages.size() == 1
                            ? starts[alternativeIndex]
                            : survives[alternativeIndex].back();
        reportWhen(matched, true);
      }
    } else {
      for (size_t age = 1; age < horizon; ++age) {
        reportWhen(cover ? nonvacuousSuccessAny[age] : successAny[age], true);
        Value finished = arith::OrIOp::create(
            builder, location, successAny[age], continueAny[age]);
        Value failedAttempt = arith::AndIOp::create(
            builder, location, activeAny[age],
            arith::XOrIOp::create(builder, location, finished, trueValue));
        reportWhen(failedAttempt, false);
      }
      reportWhen(cover ? nonvacuousSuccessAny[0] : successAny[0], true);
      Value startFinished = arith::OrIOp::create(builder, location,
                                                 successAny[0], continueAny[0]);
      reportWhen(
          arith::XOrIOp::create(builder, location, startFinished, trueValue),
          false);
    }

    for (auto [state, nextState] :
         llvm::zip_equal(alternativeStates, nextStates))
      if (state)
        sim::SimRefStoreOp::create(builder, location, nextState, state);
    cf::BranchOp::create(builder, location, wait);
    return success();
  }

  if (localInstance) {
    struct LocalState {
      std::string path;
      Type type;
      Operation *initializer = nullptr;
      SmallVector<Value> ages;
    };
    ArrayAttr typeAttrs =
        localInstance->getAttrOfType<ArrayAttr>(assertionLocalTypesAttrName);
    if (!typeAttrs ||
        typeAttrs.size() != localInstance.getLocalVariableCount() ||
        localInstance.getLocalVariablePaths().size() != typeAttrs.size() ||
        localInstance.getLocalVariableHasInitializer().size() !=
            typeAttrs.size())
      return emitError(getSemanticLocation(localInstance))
                 << "assertion local-variable type inventory is malformed",
             failure();
    SmallVector<Operation *> instanceChildren = getChildren(localInstance);
    size_t initializerIndex = 1 + localInstance.getArgumentCount();
    SmallVector<LocalState> locals;
    llvm::StringMap<unsigned> localIndices;
    for (auto [index, pathAttr, typeAttr, hasInitializer] :
         llvm::enumerate(localInstance.getLocalVariablePaths(), typeAttrs,
                         localInstance.getLocalVariableHasInitializer())) {
      auto path = dyn_cast<StringAttr>(pathAttr);
      auto type = dyn_cast<TypeAttr>(typeAttr);
      if (!path || !type || !sim::getPackedWidth(type.getValue()))
        return emitError(getSemanticLocation(localInstance))
                   << "bounded assertion locals require fixed packed types",
               failure();
      Operation *initializer = nullptr;
      if (hasInitializer != 0) {
        if (initializerIndex >= instanceChildren.size())
          return localInstance.emitError("missing assertion local initializer"),
                 failure();
        initializer = instanceChildren[initializerIndex++];
      }
      localIndices[path.getValue()] = locals.size();
      locals.push_back(
          {path.getValue().str(), type.getValue(), initializer, {}});
    }
    if (initializerIndex != instanceChildren.size())
      return localInstance.emitError(
                 "unexpected assertion instance operands after locals"),
             failure();

    // A deterministic fixed sequence has at most one live attempt at each
    // age. Keep one typed cell per (local, age); processing ages from oldest
    // to youngest below prevents a newly advanced value from overwriting the
    // value consumed by an older attempt in the same Observed region.
    for (LocalState &local : locals) {
      Value initial = createDefaultValue(builder, location, local.type);
      if (!initial)
        return emitError(location)
                   << "cannot materialize assertion local type " << local.type,
               failure();
      for (size_t age = 0; age < sequence.ages.size(); ++age)
        local.ages.push_back(sim::SimRefAllocOp::create(
            builder, location,
            sim::RefType::get(function.getContext(), local.type), initial));
    }

    llvm::StringMap<Value> enclosingValues = values;
    llvm::StringMap<Value> enclosingLValues = lvalues;
    llvm::scope_exit restoreBindings([&] {
      values = std::move(enclosingValues);
      lvalues = std::move(enclosingLValues);
    });
    auto bindLocals = [&](ArrayRef<Value> localValues) {
      for (auto [local, value] : llvm::zip_equal(locals, localValues)) {
        values[local.path] = value;
        lvalues.erase(local.path);
      }
    };
    auto initializeLocals = [&]() -> FailureOr<SmallVector<Value>> {
      SmallVector<Value> result;
      result.reserve(locals.size());
      for (LocalState &local : locals) {
        Value value = createDefaultValue(builder, location, local.type);
        if (!value)
          return failure();
        result.push_back(value);
      }
      for (auto [index, local] : llvm::enumerate(locals)) {
        if (!local.initializer)
          continue;
        bindLocals(result);
        FailureOr<Value> initialized = lowerExpression(local.initializer);
        if (failed(initialized))
          return failure();
        FailureOr<Value> converted =
            convert(*initialized, local.type, isSignedNode(local.initializer),
                    getSemanticLocation(local.initializer));
        if (failed(converted))
          return failure();
        result[index] = *converted;
      }
      return result;
    };
    auto loadLocals = [&](uint64_t age) {
      SmallVector<Value> result;
      for (LocalState &local : locals)
        result.push_back(sim::SimRefLoadOp::create(
            builder, location, local.type, local.ages[age]));
      return result;
    };
    auto storeLocals = [&](uint64_t age, ArrayRef<Value> localValues) {
      for (auto [local, value] : llvm::zip_equal(locals, localValues))
        sim::SimRefStoreOp::create(builder, location, value, local.ages[age]);
    };
    auto evaluatePredicates =
        [&](const FixedSequenceAge &age,
            ArrayRef<Value> localValues) -> FailureOr<Value> {
      bindLocals(localValues);
      Value result = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      auto evaluatePredicate = [&](Operation *predicate) -> FailureOr<Value> {
        FailureOr<Value> value = lowerExpression(predicate);
        if (failed(value))
          return failure();
        return truthValue(*value, getSemanticLocation(predicate));
      };
      for (Operation *predicate : age.predicates) {
        FailureOr<Value> truth = evaluatePredicate(predicate);
        if (failed(truth))
          return failure();
        result = arith::AndIOp::create(builder, location, result, *truth);
      }
      for (Operation *predicate : age.negatedPredicates) {
        FailureOr<Value> truth = evaluatePredicate(predicate);
        if (failed(truth))
          return failure();
        Value negated = arith::XOrIOp::create(
            builder, location, *truth,
            arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                      builder.getBoolAttr(true)));
        result = arith::AndIOp::create(builder, location, result, negated);
      }
      return result;
    };
    auto applyMatchItems =
        [&](ArrayRef<Operation *> items,
            SmallVector<Value> localValues) -> FailureOr<SmallVector<Value>> {
      Operation *secondCall = nullptr;
      unsigned callCount = 0;
      for (Operation *item : items)
        if (isa<semantic::SVCallExpressionOp>(item) && ++callCount == 2) {
          secondCall = item;
          break;
        }
      if (secondCall)
        return emitError(getSemanticLocation(secondCall))
                   << "bounded assertion local flow supports at most one "
                      "subroutine-call match item per endpoint",
               failure();
      for (Operation *item : items) {
        auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(item);
        SmallVector<Operation *> operands = getChildren(item);
        if (!assignment) {
          auto call = dyn_cast<semantic::SVCallExpressionOp>(item);
          if (!call)
            return emitError(getSemanticLocation(item))
                       << "bounded assertion local flow supports assignment "
                          "and subroutine-call match items",
                   failure();
          if (call.getHasOutputArguments() || call.getHasThisClass())
            return emitError(getSemanticLocation(item))
                       << "bounded assertion match calls require value-only "
                          "arguments and no implicit receiver",
                   failure();
          bool unsupportedArgument = false;
          call->walk([&](Operation *nested) {
            StringRef path;
            if (auto named =
                    dyn_cast<semantic::SVNamedValueExpressionOp>(nested))
              path = named.getReferencedPath();
            else if (auto hierarchical =
                         dyn_cast<semantic::SVHierarchicalValueExpressionOp>(
                             nested))
              path = hierarchical.getReferencedPath();
            else if (auto member =
                         dyn_cast<semantic::SVMemberAccessExpressionOp>(nested))
              path = member.getReferencedPath();
            if (!path.empty() && !localIndices.contains(path))
              unsupportedArgument = true;
          });
          if (unsupportedArgument)
            return emitError(getSemanticLocation(item))
                       << "bounded assertion match-call value arguments may "
                          "reference only assertion locals",
                   failure();
          bindLocals(localValues);
          auto itemNode = item->getAttrOfType<IntegerAttr>("node_id");
          uint64_t itemID = itemNode ? itemNode.getValue().getZExtValue() : 0;
          uint64_t occurrence = nextForkOrdinal;
          std::string identity =
              (function.getSymName() + ".$concurrent_match_call." +
               Twine(node) + "." + Twine(itemID) + "." + Twine(occurrence))
                  .str();
          Attribute previousCodeUnit =
              item->getAttr("obelisk_sim.fork_code_unit_id");
          item->setAttr("obelisk_sim.fork_code_unit_id",
                        builder.getI64IntegerAttr(stableCodeUnitID(identity)));
          FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
              outlineForkBranch(item, node,
                                3 + static_cast<unsigned>(occurrence),
                                /*captureReferences=*/false);
          if (previousCodeUnit)
            item->setAttr("obelisk_sim.fork_code_unit_id", previousCodeUnit);
          else
            item->removeAttr("obelisk_sim.fork_code_unit_id");
          if (failed(callback))
            return failure();
          callback->first->setAttr(
              "home_region",
              sim::EventRegionAttr::get(function.getContext(),
                                        sim::EventRegion::Reactive));
          callback->first->setAttr("domain", sim::ExecutionDomainAttr::get(
                                                 function.getContext(),
                                                 sim::ExecutionDomain::Design));
          callback->first->setAttr("obelisk_sim.concurrent_match_call",
                                   builder.getUnitAttr());
          callback->first->setAttr("obelisk_sim.detached_controls",
                                   builder.getUnitAttr());
          sim::SimSpawnOp::create(builder, getSemanticLocation(item),
                                  callback->first.getSymNameAttr(),
                                  callback->second, ArrayAttr{}, ArrayAttr{});
          continue;
        }
        if (assignment.getHasTimingControl() || assignment.getOperatorKind() ||
            assignment.getAssignmentKind() !=
                semantic::SVAssignmentKind::Blocking ||
            operands.size() != 2)
          return emitError(getSemanticLocation(item))
                     << "bounded assertion local flow currently requires "
                        "simple blocking match assignments",
                 failure();
        auto destination =
            dyn_cast<semantic::SVNamedValueExpressionOp>(operands.front());
        auto found = destination
                         ? localIndices.find(destination.getReferencedPath())
                         : localIndices.end();
        if (found == localIndices.end())
          return emitError(getSemanticLocation(item))
                     << "assertion match assignment does not target a local "
                        "variable",
                 failure();
        bindLocals(localValues);
        FailureOr<Value> rhs = lowerExpression(operands.back());
        if (failed(rhs))
          return failure();
        unsigned index = found->second;
        FailureOr<Value> converted =
            convert(*rhs, locals[index].type, isSignedNode(operands.back()),
                    getSemanticLocation(item), isSignedNode(operands.front()));
        if (failed(converted))
          return failure();
        localValues[index] = *converted;
      }
      return localValues;
    };
    auto reportFailure = [&](Value enabled, Value matches) -> LogicalResult {
      Value fails = arith::AndIOp::create(
          builder, location, enabled,
          arith::XOrIOp::create(
              builder, location, matches,
              arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(true))));
      if (!observable)
        return success();
      Block *report = addBlock();
      Block *continuation = addBlock();
      cf::CondBranchOp::create(builder, location, fails, report, ValueRange{},
                               continuation, ValueRange{});
      setCurrent(report);
      scheduleResult(false);
      emitBranch(continuation);
      setCurrent(continuation);
      return success();
    };
    auto reportSuccess = [&] { scheduleResult(true); };

    Block *wait = addBlock();
    Block *sample = addBlock();
    emitBranch(wait);
    setCurrent(wait);
    if (failed(emitEventSuspend(clock, sample)))
      return failure();
    wait->getTerminator()->setAttr(
        "resume_region", sim::EventRegionAttr::get(function.getContext(),
                                                   sim::EventRegion::Observed));
    setCurrent(sample);
    Value state = stateStorage ? sim::SimRefLoadOp::create(
                                     builder, location, stateType, stateStorage)
                               : zero;

    if (failed(cancelDisabledSample(wait)))
      return failure();

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    Value nextState = zero;
    uint64_t firstActiveAge = implication && nonoverlapped ? 0 : 1;
    for (uint64_t cursor = sequence.ages.size(); cursor-- > firstActiveAge;) {
      uint64_t age = cursor;
      Value mask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1} << age));
      Value presentBits = arith::AndIOp::create(builder, location, state, mask);
      Value active = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, presentBits, zero);
      SmallVector<Value> ageLocals = loadLocals(age);
      FailureOr<Value> matches =
          evaluatePredicates(sequence.ages[age], ageLocals);
      if (failed(matches) || failed(reportFailure(active, *matches)))
        return failure();
      Value advances =
          arith::AndIOp::create(builder, location, active, *matches);
      Block *matched = addBlock();
      Block *continued = addBlock();
      cf::CondBranchOp::create(builder, location, advances, matched,
                               ValueRange{}, continued, ValueRange{});
      setCurrent(matched);
      FailureOr<SmallVector<Value>> updated =
          applyMatchItems(sequence.ages[age].matchItems, ageLocals);
      if (failed(updated))
        return failure();
      if (age + 1 == sequence.ages.size())
        reportSuccess();
      else
        storeLocals(age + 1, *updated);
      emitBranch(continued);
      setCurrent(continued);
      if (age + 1 != sequence.ages.size()) {
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
        Value advancedBit = arith::SelectOp::create(builder, location, advances,
                                                    nextMask, zero);
        nextState =
            arith::OrIOp::create(builder, location, nextState, advancedBit);
      }
    }

    FailureOr<SmallVector<Value>> initialLocals = initializeLocals();
    if (failed(initialLocals))
      return failure();
    FailureOr<Value> starts =
        implication ? evaluatePredicates(antecedentSequence.ages.front(),
                                         *initialLocals)
                    : evaluatePredicates(sequence.ages.front(), *initialLocals);
    if (failed(starts))
      return failure();
    Value one = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    if (!implication && failed(reportFailure(one, *starts)))
      return failure();
    if (implication && !cover) {
      if (followedBy) {
        if (failed(reportFailure(one, *starts)))
          return failure();
      } else {
        Value vacuous = arith::XOrIOp::create(builder, location, *starts, one);
        Block *report = addBlock();
        Block *continued = addBlock();
        cf::CondBranchOp::create(builder, location, vacuous, report,
                                 ValueRange{}, continued, ValueRange{});
        setCurrent(report);
        reportSuccess();
        emitBranch(continued);
        setCurrent(continued);
      }
    }

    Block *startMatched = addBlock();
    Block *afterStart = addBlock();
    afterStart->addArgument(stateType, location);
    cf::CondBranchOp::create(builder, location, *starts, startMatched,
                             ValueRange{}, afterStart, ValueRange{nextState});
    setCurrent(startMatched);
    SmallVector<Value> startLocals = *initialLocals;
    if (implication) {
      FailureOr<SmallVector<Value>> updated = applyMatchItems(
          antecedentSequence.ages.front().matchItems, startLocals);
      if (failed(updated))
        return failure();
      startLocals = std::move(*updated);
    }
    if (implication && nonoverlapped) {
      storeLocals(0, startLocals);
      Value startMask = arith::ConstantOp::create(builder, location, stateType,
                                                  builder.getI64IntegerAttr(1));
      Value updatedState =
          arith::OrIOp::create(builder, location, nextState, startMask);
      cf::BranchOp::create(builder, location, afterStart,
                           ValueRange{updatedState});
    } else {
      FailureOr<Value> first =
          implication ? evaluatePredicates(sequence.ages.front(), startLocals)
                      : FailureOr<Value>(*starts);
      if (failed(first) || (implication && failed(reportFailure(one, *first))))
        return failure();
      Block *firstMatched = addBlock();
      Block *afterFirst = addBlock();
      cf::CondBranchOp::create(builder, location, *first, firstMatched,
                               ValueRange{}, afterFirst, ValueRange{});
      setCurrent(firstMatched);
      FailureOr<SmallVector<Value>> updated =
          applyMatchItems(sequence.ages.front().matchItems, startLocals);
      if (failed(updated))
        return failure();
      if (sequence.ages.size() == 1)
        reportSuccess();
      else
        storeLocals(1, *updated);
      emitBranch(afterFirst);
      setCurrent(afterFirst);
      Value updatedState = nextState;
      if (sequence.ages.size() > 1) {
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType, builder.getI64IntegerAttr(2));
        Value started =
            arith::SelectOp::create(builder, location, *first, nextMask, zero);
        updatedState =
            arith::OrIOp::create(builder, location, nextState, started);
      }
      cf::BranchOp::create(builder, location, afterStart,
                           ValueRange{updatedState});
    }
    setCurrent(afterStart);
    nextState = afterStart->getArgument(0);
    if (stateStorage)
      sim::SimRefStoreOp::create(builder, location, nextState, stateStorage);
    cf::BranchOp::create(builder, location, wait);
    return success();
  }

  Block *wait = addBlock();
  Block *sample = addBlock();
  emitBranch(wait);
  setCurrent(wait);
  if (failed(emitEventSuspend(clock, sample)))
    return failure();
  // The clock occurrence samples in Preponed; bounded evaluation and monitor
  // bookkeeping resume in Observed.
  wait->getTerminator()->setAttr(
      "resume_region", sim::EventRegionAttr::get(function.getContext(),
                                                 sim::EventRegion::Observed));
  setCurrent(sample);
  Value state = zero;
  if (stateStorage)
    state =
        sim::SimRefLoadOp::create(builder, location, stateType, stateStorage);

  if (failed(cancelDisabledSample(wait)))
    return failure();

  bool savedSampleAssertionValues = sampleAssertionValues;
  sampleAssertionValues = true;
  Operation *savedSampledClock = activeSampledClock;
  activeSampledClock = clock;
  llvm::scope_exit restoreSampling([&] {
    sampleAssertionValues = savedSampleAssertionValues;
    activeSampledClock = savedSampledClock;
  });

  llvm::DenseMap<Operation *, Value> predicateCache;
  auto conditionalResult = [&](Value condition, bool passed) -> LogicalResult {
    if (!observable)
      return success();
    Block *report = addBlock();
    Block *continuation = addBlock();
    cf::CondBranchOp::create(builder, location, condition, report, ValueRange{},
                             continuation, ValueRange{});
    setCurrent(report);
    scheduleResult(passed);
    emitBranch(continuation);
    setCurrent(continuation);
    return success();
  };
  auto evaluateAge = [&](const FixedSequenceAge &age) -> FailureOr<Value> {
    Value result = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    auto evaluatePredicate = [&](Operation *predicate) -> FailureOr<Value> {
      Value truth;
      auto found = predicateCache.find(predicate);
      if (found != predicateCache.end()) {
        truth = found->second;
      } else {
        FailureOr<Value> value = lowerExpression(predicate);
        if (failed(value))
          return failure();
        FailureOr<Value> converted =
            truthValue(*value, getSemanticLocation(predicate));
        if (failed(converted))
          return failure();
        truth = *converted;
        predicateCache[predicate] = truth;
      }
      return truth;
    };
    for (Operation *predicate : age.predicates) {
      FailureOr<Value> truth = evaluatePredicate(predicate);
      if (failed(truth))
        return failure();
      result = arith::AndIOp::create(builder, location, result, *truth);
    }
    for (Operation *predicate : age.negatedPredicates) {
      FailureOr<Value> truth = evaluatePredicate(predicate);
      if (failed(truth))
        return failure();
      Value negated = arith::XOrIOp::create(
          builder, location, *truth,
          arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true)));
      result = arith::AndIOp::create(builder, location, result, negated);
    }
    return result;
  };

  Value nextState = zero;
  uint64_t firstActiveAge = implication && nonoverlapped ? 0 : 1;
  for (uint64_t age = firstActiveAge; age < sequence.ages.size(); ++age) {
    Value mask = arith::ConstantOp::create(
        builder, location, stateType,
        builder.getI64IntegerAttr(uint64_t{1} << age));
    Value presentBits = arith::AndIOp::create(builder, location, state, mask);
    Value active = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, presentBits, zero);
    FailureOr<Value> matches = evaluateAge(sequence.ages[age]);
    if (failed(matches))
      return failure();
    Value fails = arith::AndIOp::create(
        builder, location, active,
        arith::XOrIOp::create(
            builder, location, *matches,
            arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                      builder.getBoolAttr(true))));
    if (failed(conditionalResult(fails, false)))
      return failure();
    Value advances = arith::AndIOp::create(builder, location, active, *matches);
    if (age + 1 == sequence.ages.size()) {
      if (failed(conditionalResult(advances, true)))
        return failure();
    } else {
      Value nextMask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
      Value advancedBit =
          arith::SelectOp::create(builder, location, advances, nextMask, zero);
      nextState =
          arith::OrIOp::create(builder, location, nextState, advancedBit);
    }
  }

  auto launchConsequent = [&](Value triggered) -> LogicalResult {
    if (nonoverlapped) {
      Value firstMask = arith::ConstantOp::create(builder, location, stateType,
                                                  builder.getI64IntegerAttr(1));
      Value started = arith::SelectOp::create(builder, location, triggered,
                                              firstMask, zero);
      nextState = arith::OrIOp::create(builder, location, nextState, started);
      return success();
    }

    FailureOr<Value> first = evaluateAge(sequence.ages.front());
    if (failed(first))
      return failure();
    Value matched = arith::AndIOp::create(builder, location, triggered, *first);
    Value failedStart = arith::AndIOp::create(
        builder, location, triggered,
        arith::XOrIOp::create(
            builder, location, *first,
            arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                      builder.getBoolAttr(true))));
    if (failed(conditionalResult(failedStart, false)))
      return failure();
    if (sequence.ages.size() == 1)
      return conditionalResult(matched, true);
    Value nextMask = arith::ConstantOp::create(builder, location, stateType,
                                               builder.getI64IntegerAttr(2));
    Value started =
        arith::SelectOp::create(builder, location, matched, nextMask, zero);
    nextState = arith::OrIOp::create(builder, location, nextState, started);
    return success();
  };

  if (implication && antecedentSequence.ages.size() > 1) {
    uint64_t antecedentBase = sequence.ages.size();
    for (uint64_t age = 1; age < antecedentSequence.ages.size(); ++age) {
      Value mask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1} << (antecedentBase + age)));
      Value presentBits = arith::AndIOp::create(builder, location, state, mask);
      Value active = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, presentBits, zero);
      FailureOr<Value> matches = evaluateAge(antecedentSequence.ages[age]);
      if (failed(matches))
        return failure();
      Value advances =
          arith::AndIOp::create(builder, location, active, *matches);
      advances.getDefiningOp()->setAttr("obelisk_sim.implication_antecedent",
                                        builder.getUnitAttr());
      Value notMatches = arith::XOrIOp::create(
          builder, location, *matches,
          arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true)));
      Value vacuous =
          arith::AndIOp::create(builder, location, active, notMatches);
      vacuous.getDefiningOp()->setAttr(
          "obelisk_sim.implication_antecedent_failure", builder.getUnitAttr());
      if (!cover && failed(conditionalResult(vacuous, !followedBy)))
        return failure();
      if (age + 1 == antecedentSequence.ages.size()) {
        if (failed(launchConsequent(advances)))
          return failure();
      } else {
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1}
                                      << (antecedentBase + age + 1)));
        Value advancedBit = arith::SelectOp::create(builder, location, advances,
                                                    nextMask, zero);
        nextState =
            arith::OrIOp::create(builder, location, nextState, advancedBit);
      }
    }
  }

  FailureOr<Value> starts = implication
                                ? evaluateAge(antecedentSequence.ages.front())
                                : evaluateAge(sequence.ages.front());
  if (failed(starts))
    return failure();

  if (implication) {
    Value vacuous = arith::XOrIOp::create(
        builder, location, *starts,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    // Implication succeeds vacuously when its antecedent is false. Followed-by
    // instead requires an antecedent match and therefore fails. A cover
    // directive records neither case as a hit.
    if (!cover && failed(conditionalResult(vacuous, !followedBy)))
      return failure();
    if (antecedentSequence.ages.size() == 1) {
      if (failed(launchConsequent(*starts)))
        return failure();
    } else {
      uint64_t firstAntecedentAge = sequence.ages.size() + 1;
      Value firstMask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1} << firstAntecedentAge));
      Value started =
          arith::SelectOp::create(builder, location, *starts, firstMask, zero);
      nextState = arith::OrIOp::create(builder, location, nextState, started);
    }
  } else {
    // The age-zero attempt is evaluated directly every clock. Older attempts
    // are represented by the state bits handled above.
    Value failedStart = arith::XOrIOp::create(
        builder, location, *starts,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    if (failed(conditionalResult(failedStart, false)))
      return failure();
    if (sequence.ages.size() == 1) {
      if (failed(conditionalResult(*starts, true)))
        return failure();
    } else {
      Value nextMask = arith::ConstantOp::create(builder, location, stateType,
                                                 builder.getI64IntegerAttr(2));
      Value started =
          arith::SelectOp::create(builder, location, *starts, nextMask, zero);
      nextState = arith::OrIOp::create(builder, location, nextState, started);
    }
  }

  if (stateStorage)
    sim::SimRefStoreOp::create(builder, location, nextState, stateStorage);
  cf::BranchOp::create(builder, location, wait);
  return success();
}

} // namespace obelisk::simlowering
