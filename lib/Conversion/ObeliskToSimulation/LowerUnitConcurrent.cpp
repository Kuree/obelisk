//===- LowerUnitConcurrent.cpp - AOT concurrent assertion monitors -------===//
//
// Compiles the bounded single-clock SVA slice and selected common persistent
// forms into ordinary simulation SSA. Runtime state is a compact bitset or an
// aggregate token DFA carried across clock suspensions; the runtime has no
// temporal interpreter and no solver dependency.
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

struct FirstMatchGroupComponent {
  Operation *operation = nullptr;
  uint64_t activation = 0;

  bool operator==(const FirstMatchGroupComponent &other) const {
    return operation == other.operation && activation == other.activation;
  }
};

/// A successful trace crossing `age` selects this first_match group and
/// cancels the group's alternatives that would end later. `groupPath` starts
/// with the first_match operation, records enclosing expanded assertion
/// invocation sites, and adds deterministic Cartesian activation components
/// when a ranged prefix can start the same suffix more than once. This keeps
/// both distinct syntax scopes and distinct dynamic activations independent
/// without relying on allocator ordering or runtime temporal objects.
struct FirstMatchBoundary {
  SmallVector<FirstMatchGroupComponent, 4> groupPath;
  uint64_t age = 0;
};

struct FixedSequence {
  SmallVector<FixedSequenceAge, 8> ages;
  SmallVector<FirstMatchBoundary, 2> firstMatchBoundaries;
  /// This trace matches over zero clock ticks. It may participate in the
  /// positive-delay concatenation rewrites from IEEE 1800 empty-match rules,
  /// but it cannot itself be promoted to a property.
  bool emptyMatch = false;
  bool vacuousSuccess = false;
};

static void appendObserverRequest(sim::SimFuncOp function, StringRef name,
                                  FlatSymbolRefAttr evaluator) {
  SmallVector<Attribute, 2> requests;
  if (auto existing = function->getAttrOfType<ArrayAttr>(name))
    llvm::append_range(requests, existing);
  if (!llvm::is_contained(requests, evaluator))
    requests.push_back(evaluator);
  function->setAttr(name, ArrayAttr::get(function.getContext(), requests));
}

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
  bool unbounded = false;
  bool hasTerminal = false;
};

/// A common unbounded property-until form. Every clock starts a new property
/// attempt, but for one-cycle boolean operands all live attempts have the same
/// future transition. The generated monitor can therefore retain one exact
/// token count instead of one process or one state bit per attempt.
struct PersistentUntilProperty {
  FixedSequenceAge left;
  FixedSequenceAge right;
  semantic::SVAssertionBinaryOperator kind =
      semantic::SVAssertionBinaryOperator::Until;
  bool inclusive = false;
  bool strong = false;
};

/// A common unbounded unary property over one Boolean clock sample. Every
/// clock starts one evaluation attempt. Attempts younger than `minimum` are
/// represented by one bounded warm-up count; all eligible attempts share one
/// exact live count because they observe the same future Boolean value.
struct PersistentUnaryProperty {
  FixedSequenceAge operand;
  semantic::SVAssertionUnaryOperator kind =
      semantic::SVAssertionUnaryOperator::Always;
  uint64_t minimum = 0;
  bool eventually = false;
};

/// A deterministic bounded prefix followed by one unbounded delay and one
/// Boolean terminal. Prefix attempts remain a compact age bitset, the last M
/// delay slots are another bitset, and every older attempt shares one exact
/// eligible count. This preserves cover-sequence match multiplicity without
/// expanding an infinite family of traces.
struct PersistentDelaySequence {
  FixedSequence prefix;
  FixedSequenceAge terminal;
  uint64_t minimum = 0;
};

using FixedSequenceAlternatives = SmallVector<FixedSequence, 4>;

static constexpr size_t maxFixedSequenceAlternatives = 256;

static LogicalResult appendFixedSequence(FixedSequence &result,
                                         const FixedSequence &nested,
                                         uint64_t offset);

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
      StringRef spelling;
      switch (kind) {
      case semantic::SVSequenceRepetitionKind::Consecutive:
        spelling = "[*]";
        break;
      case semantic::SVSequenceRepetitionKind::Nonconsecutive:
        spelling = "[=]";
        break;
      case semantic::SVSequenceRepetitionKind::GoTo:
        spelling = "[->]";
        break;
      }
      if (nested && (unbounded ||
                     kind != semantic::SVSequenceRepetitionKind::Consecutive)) {
        return diagnose(
            Twine("persistent sequence repetition ") + spelling +
            " does not yet compose with an implication antecedent or "
            "consequent");
      }
      if (unbounded) {
        return diagnose(
            Twine("unbounded sequence repetition ") + spelling +
            " currently requires a positive minimum no greater than 63 "
            "on one Boolean term, optionally preceded by a deterministic "
            "bounded prefix and followed by ##1 plus one Boolean term");
      }
      if (kind == semantic::SVSequenceRepetitionKind::Nonconsecutive) {
        return diagnose(
            "nonconsecutive sequence repetition [=] currently requires a "
            "positive finite range no greater than 63 on one boolean term, "
            "optionally preceded by a deterministic bounded prefix and "
            "followed by ##1 plus one boolean term");
      }
      if (kind == semantic::SVSequenceRepetitionKind::GoTo) {
        return diagnose(
            "goto sequence repetition [->] currently requires a positive "
            "finite range no greater than 63 on one boolean term, optionally "
            "preceded by a deterministic bounded prefix and followed by ##1 "
            "plus one boolean term");
      }
      if (minimum && *minimum == 0)
        return diagnose(
            "empty consecutive repetition [*0...] currently requires "
            "positive-delay concatenation that eliminates every empty "
            "endpoint; standalone empty matches and ##0 empty fusion are "
            "not executable properties");
      return WalkResult::advance();
    };

    if (auto simple = dyn_cast<semantic::SVSimpleAssertionExprOp>(current);
        simple && simple.getHasRepetition()) {
      if (!simple.getRepetitionKind())
        return diagnose("sequence repetition is missing its repetition kind");
      bool repeatsFirstMatch = false;
      simple->walk([&](semantic::SVFirstMatchAssertionExprOp) {
        repeatsFirstMatch = true;
      });
      if (repeatsFirstMatch)
        return diagnose(
            "repetition of a sequence containing first_match is not "
            "executable yet; each repetition occurrence requires an "
            "independent priority scope");
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
      bool repeatsFirstMatch = false;
      matched->walk([&](semantic::SVFirstMatchAssertionExprOp) {
        repeatsFirstMatch = true;
      });
      if (repeatsFirstMatch)
        return diagnose(
            "repetition of a sequence containing first_match is not "
            "executable yet; each repetition occurrence requires an "
            "independent priority scope");
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
          return diagnose(
              "unbounded sequence delay ##[M:$] currently requires one "
              "final delay with M no greater than 63, a deterministic "
              "bounded prefix, and one Boolean terminal without locals or "
              "match items");
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
            "SVA property operator 'not' currently requires either a "
            "bounded one-cycle boolean operand whose pre-minimization exact "
            "complement expansion has at most 256 alternatives and has no "
            "vacuity, first_match, or match items, or one deterministic "
            "bounded multi-cycle sequence optionally qualified by "
            "strong/weak");
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
              semantic::SVAssertionUnaryOperator::SEventually)
        return diagnose(
            Twine("SVA property operator '") +
            semantic::stringifySVAssertionUnaryOperator(
                unary.getOperatorKind()) +
            "' currently requires its unbounded/no-range form over one "
            "deterministic one-cycle Boolean operand without locals or "
            "match items");
      if (unary.getOperatorKind() ==
              semantic::SVAssertionUnaryOperator::SAlways ||
          unary.getOperatorKind() ==
              semantic::SVAssertionUnaryOperator::Eventually)
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
      case semantic::SVAssertionBinaryOperator::Until:
      case semantic::SVAssertionBinaryOperator::SUntil:
      case semantic::SVAssertionBinaryOperator::UntilWith:
      case semantic::SVAssertionBinaryOperator::SUntilWith:
        return diagnose(
            Twine("SVA property operator '") +
            semantic::stringifySVAssertionBinaryOperator(
                binary.getOperatorKind()) +
            "' currently requires two deterministic one-cycle boolean "
            "operands without locals, match items, disable iff, or nested "
            "property composition");
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
          "' end-of-simulation qualification currently requires one "
          "outermost bounded sequence property without implication/"
          "followed-by, unsupported persistent operators, expect, or "
          "cover-sequence per-match accounting");
    if (auto abort = dyn_cast<semantic::SVAbortAssertionExprOp>(current)) {
      std::string spelling =
          (Twine(abort.getIsSynchronous() ? "sync_" : "") +
           semantic::stringifySVAssertionAbortAction(abort.getAction()) + "_on")
              .str();
      return diagnose(Twine("SVA property operator '") + spelling +
                      "' currently requires one outermost abort around a "
                      "plain deterministic bounded property without locals, "
                      "match items, implication/followed-by, disable iff, "
                      "first_match, expect, or cover-sequence per-match "
                      "accounting");
    }
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
          *simple.getRepetitionMin() != *simple.getRepetitionMax())
        return failure();
      if (*simple.getRepetitionMin() == 0) {
        FixedSequence empty;
        empty.emptyMatch = true;
        return empty;
      }
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
    if (nested.emptyMatch)
      return nested;
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
      if (failed(nested) ||
          failed(appendFixedSequence(result, *nested,
                                     static_cast<uint64_t>(minimum.getInt()))))
        return failure();
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

/// Recognize the high-frequency Boolean forms of `until`, `s_until`,
/// `until_with`, and `s_until_with`. General temporal operands need correlated
/// per-attempt state and are intentionally diagnosed rather than silently
/// approximated by this aggregate monitor.
static FailureOr<PersistentUntilProperty>
compilePersistentUntil(Operation *operation) {
  auto binary = dyn_cast<semantic::SVBinaryAssertionExprOp>(operation);
  if (!binary)
    return failure();
  switch (binary.getOperatorKind()) {
  case semantic::SVAssertionBinaryOperator::Until:
  case semantic::SVAssertionBinaryOperator::SUntil:
  case semantic::SVAssertionBinaryOperator::UntilWith:
  case semantic::SVAssertionBinaryOperator::SUntilWith:
    break;
  default:
    return failure();
  }

  SmallVector<Operation *> children = getChildren(binary);
  if (children.size() != 2)
    return failure();
  FailureOr<FixedSequence> left = compileFixedSequence(children.front());
  FailureOr<FixedSequence> right = compileFixedSequence(children.back());
  if (failed(left) || failed(right) || !isSingleBooleanAge(*left) ||
      !isSingleBooleanAge(*right) || left->vacuousSuccess ||
      right->vacuousSuccess)
    return failure();

  PersistentUntilProperty result;
  result.left = std::move(left->ages.front());
  result.right = std::move(right->ages.front());
  result.kind = binary.getOperatorKind();
  result.inclusive =
      result.kind == semantic::SVAssertionBinaryOperator::UntilWith ||
      result.kind == semantic::SVAssertionBinaryOperator::SUntilWith;
  result.strong =
      result.kind == semantic::SVAssertionBinaryOperator::SUntil ||
      result.kind == semantic::SVAssertionBinaryOperator::SUntilWith;
  return result;
}

/// Recognize the LRM's common genuinely unbounded unary forms:
///
///   always property_expr
///   always [M:$] property_expr
///   s_eventually property_expr
///   s_eventually [M:$] property_expr
///
/// Weak eventually is required to have a bounded range, and strong always is
/// required to have a bounded range, so those remain in the fixed compiler.
static FailureOr<PersistentUnaryProperty>
compilePersistentUnary(Operation *operation) {
  auto unary = dyn_cast<semantic::SVUnaryAssertionExprOp>(operation);
  if (!unary)
    return failure();
  bool always =
      unary.getOperatorKind() == semantic::SVAssertionUnaryOperator::Always;
  bool eventually = unary.getOperatorKind() ==
                    semantic::SVAssertionUnaryOperator::SEventually;
  if (!always && !eventually)
    return failure();

  uint64_t minimum = 0;
  if (unary.getHasRange()) {
    if (!unary.getRangeIsUnbounded() || !unary.getRangeMin())
      return failure();
    minimum = static_cast<uint64_t>(*unary.getRangeMin());
  }

  SmallVector<Operation *> children = getChildren(unary);
  if (children.size() != 1)
    return failure();
  FailureOr<FixedSequence> operand = compileFixedSequence(children.front());
  if (failed(operand) || !isSingleBooleanAge(*operand) ||
      operand->vacuousSuccess)
    return failure();

  PersistentUnaryProperty result;
  result.operand = std::move(operand->ages.front());
  result.kind = unary.getOperatorKind();
  result.minimum = minimum;
  result.eventually = eventually;
  return result;
}

/// Recognize one final finite-but-unbounded delay without enumerating its
/// infinite endpoint set:
///
///   deterministic-bounded-prefix ##[M:$] boolean
///   ##[M:$] boolean
///
/// The prefix stays within the ordinary 63-cycle representation and M uses a
/// 63-bit delay queue. Older live attempts are observationally equivalent and
/// merge into one count until the terminal becomes true.
static FailureOr<PersistentDelaySequence>
compilePersistentDelay(Operation *operation) {
  auto concat = dyn_cast<semantic::SVSequenceConcatExprOp>(operation);
  if (!concat)
    return failure();
  SmallVector<Operation *> children = getChildren(concat);
  ArrayAttr delays = concat.getDelays();
  if (children.empty() || delays.size() != children.size())
    return failure();

  size_t unboundedIndex = children.size();
  uint64_t minimum = 0;
  for (auto [index, delayAttr] : llvm::enumerate(delays)) {
    auto delay = dyn_cast<DictionaryAttr>(delayAttr);
    auto isUnbounded =
        delay ? delay.getAs<BoolAttr>("is_unbounded") : BoolAttr{};
    auto lower = delay ? delay.getAs<IntegerAttr>("min") : IntegerAttr{};
    if (!delay || !isUnbounded || !lower || lower.getInt() < 0)
      return failure();
    if (!isUnbounded.getValue())
      continue;
    if (unboundedIndex != children.size())
      return failure();
    unboundedIndex = index;
    minimum = static_cast<uint64_t>(lower.getInt());
  }
  if (unboundedIndex + 1 != children.size() || minimum > 63)
    return failure();

  PersistentDelaySequence result;
  result.minimum = minimum;
  if (unboundedIndex == 0) {
    // A leading delay has an implicit true sequence at the attempt clock.
    result.prefix.ages.resize(1);
  } else {
    for (size_t index = 0; index < unboundedIndex; ++index) {
      auto delay = dyn_cast<DictionaryAttr>(delays[index]);
      auto lower = delay ? delay.getAs<IntegerAttr>("min") : IntegerAttr{};
      auto upper = delay ? delay.getAs<IntegerAttr>("max") : IntegerAttr{};
      auto isUnbounded =
          delay ? delay.getAs<BoolAttr>("is_unbounded") : BoolAttr{};
      if (!lower || !upper || !isUnbounded || isUnbounded.getValue() ||
          lower.getInt() < 0 || lower.getInt() != upper.getInt())
        return failure();
      FailureOr<FixedSequence> prefix = compileFixedSequence(children[index]);
      if (failed(prefix) || prefix->emptyMatch || prefix->vacuousSuccess ||
          !prefix->firstMatchBoundaries.empty() ||
          llvm::any_of(prefix->ages,
                       [](const FixedSequenceAge &age) {
                         return !age.matchItems.empty() ||
                                !age.caseGuards.empty();
                       }) ||
          failed(appendFixedSequence(result.prefix, *prefix,
                                     static_cast<uint64_t>(lower.getInt()))))
        return failure();
    }
  }
  if (result.prefix.ages.empty() || result.prefix.ages.size() > 63)
    return failure();

  FailureOr<FixedSequence> terminal =
      compileFixedSequence(children[unboundedIndex]);
  if (failed(terminal) || !isSingleBooleanAge(*terminal) ||
      terminal->vacuousSuccess)
    return failure();
  result.terminal = std::move(terminal->ages.front());
  return result;
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
  if (nested.emptyMatch) {
    if (!nested.ages.empty() || !nested.firstMatchBoundaries.empty() ||
        nested.vacuousSuccess)
      return failure();
    if (result.emptyMatch || (result.ages.empty() && offset != 0))
      return failure();
    if (result.ages.empty()) {
      result.emptyMatch = true;
      return success();
    }
    // (seq ##n empty), n>0, is (seq ##(n-1) true). An empty ##0
    // fusion never matches and remains outside the executable property subset.
    if (offset == 0 || result.ages.size() + offset - 1 > 63)
      return failure();
    result.ages.resize(result.ages.size() + offset - 1);
    return success();
  }
  if (nested.ages.empty())
    return failure();
  if (result.emptyMatch) {
    // (empty ##n seq), n>0, is (##(n-1) seq).
    if (offset == 0)
      return failure();
    result = FixedSequence{};
    return appendFixedSequence(result, nested, offset - 1);
  }
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
  for (FirstMatchBoundary boundary : nested.firstMatchBoundaries) {
    boundary.age += start;
    result.firstMatchBoundaries.push_back(boundary);
  }
  result.vacuousSuccess |= nested.vacuousSuccess;
  return success();
}

/// Give every first_match scope in `sequence` a deterministic identity for
/// one Cartesian activation. Alternatives belonging to the same activation
/// receive the same component, while a ranged prefix or placement receives a
/// different component. The source operation keeps unrelated composition
/// sites disjoint.
static void qualifyFirstMatchGroups(FixedSequence &sequence, Operation *owner,
                                    uint64_t activation) {
  for (FirstMatchBoundary &boundary : sequence.firstMatchBoundaries)
    boundary.groupPath.push_back({owner, activation});
}

/// Conjoin a bounded trace at an absolute clock age. This is used for finite
/// `always` ranges, where one property attempt starts at every age in the
/// range and all of those attempts must succeed. Unlike concatenation, the
/// insertion age is independent of the current endpoint.
static LogicalResult mergeFixedSequenceAt(FixedSequence &result,
                                          const FixedSequence &nested,
                                          uint64_t start) {
  if (nested.ages.empty() || start + nested.ages.size() > 63 ||
      nested.vacuousSuccess)
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
  for (FirstMatchBoundary boundary : nested.firstMatchBoundaries) {
    boundary.age += start;
    result.firstMatchBoundaries.push_back(std::move(boundary));
  }
  return success();
}

/// Recognize the high-frequency persistent repetition forms without expanding
/// their unbounded inter-occurrence waits into a finite horizon:
///
///   fixed-prefix ##N boolean[*M:$]  [##1 boolean]
///   fixed-prefix ##N boolean[->M:N] [##1 boolean]
///   fixed-prefix ##N boolean[=M:N]  [##1 boolean]
///
/// The goto/nonconsecutive upper bound may also be `$`. An unbounded DFA
/// saturates its occurrence count at M: once the lower bound is met, larger
/// counts have identical future behavior. Consecutive repetition uses the
/// same saturation for its eligible run. This keeps runtime state O(M).
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
    if (!simple || !simple.getHasRepetition() || !simple.getRepetitionKind())
      continue;
    bool persistentKind =
        simple.getRepetitionIsUnbounded() ||
        *simple.getRepetitionKind() ==
            semantic::SVSequenceRepetitionKind::Nonconsecutive ||
        *simple.getRepetitionKind() == semantic::SVSequenceRepetitionKind::GoTo;
    if (!persistentKind)
      continue;
    if (repetition)
      return failure();
    repetition = simple;
    repetitionIndex = index;
  }
  if (!repetition || !repetition.getRepetitionMin() ||
      *repetition.getRepetitionMin() <= 0 ||
      *repetition.getRepetitionMin() > 63)
    return failure();

  bool unbounded = repetition.getRepetitionIsUnbounded();
  if (!unbounded &&
      (!repetition.getRepetitionMax() ||
       *repetition.getRepetitionMax() < *repetition.getRepetitionMin() ||
       *repetition.getRepetitionMax() > 63))
    return failure();

  PersistentRepetitionSequence result;
  result.kind = *repetition.getRepetitionKind();
  result.minimum = static_cast<uint64_t>(*repetition.getRepetitionMin());
  result.unbounded = unbounded;
  result.maximum = unbounded
                       ? result.minimum
                       : static_cast<uint64_t>(*repetition.getRepetitionMax());

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
    FailureOr<FixedSequenceAlternatives> nested =
        compileFixedSequenceAlternatives(*body, resolvedClock);
    if (failed(nested))
      return failure();
    for (FixedSequence &alternative : *nested)
      for (FirstMatchBoundary &boundary : alternative.firstMatchBoundaries)
        boundary.groupPath.push_back({instance.getOperation(), 0});
    return nested;
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
      if (alternative.ages.empty())
        return failure();
      alternative.firstMatchBoundaries.push_back(
          {{{first.getOperation(), 0}}, alternative.ages.size() - 1});
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

    // A finite consecutive range is the union of its exact repeat counts.
    // Count zero is retained as an empty trace so surrounding positive-delay
    // concatenation can apply the LRM empty-match rewrite exactly.
    if (simple.getHasRepetition() &&
        simple.getRepetitionKind() ==
            semantic::SVSequenceRepetitionKind::Consecutive &&
        !simple.getRepetitionIsUnbounded() && simple.getRepetitionMin() &&
        simple.getRepetitionMax() &&
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
      if (nested.empty() ||
          llvm::any_of(nested, [](const FixedSequence &alternative) {
            return alternative.emptyMatch || alternative.ages.empty() ||
                   !alternative.firstMatchBoundaries.empty();
          }))
        return failure();

      uint64_t minimum = *simple.getRepetitionMin();
      uint64_t maximum = *simple.getRepetitionMax();
      FixedSequenceAlternatives prefixes(1);
      FixedSequenceAlternatives results;
      if (minimum == 0) {
        FixedSequence empty;
        empty.emptyMatch = true;
        results.push_back(std::move(empty));
      }
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
      for (auto [prefixIndex, prefix] : llvm::enumerate(results))
        for (const FixedSequence &suffix : *nested)
          for (int64_t offset = minimum.getInt(); offset <= maximum.getInt();
               ++offset) {
            FixedSequence qualifiedSuffix = suffix;
            uint64_t activation =
                static_cast<uint64_t>(prefixIndex) * offsets +
                static_cast<uint64_t>(offset - minimum.getInt()) + 1;
            qualifyFirstMatchGroups(qualifiedSuffix, concat.getOperation(),
                                    activation);
            FixedSequence combined = prefix;
            if (failed(appendFixedSequence(combined, qualifiedSuffix,
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
    if (children.size() != 1)
      return failure();

    if (unary.getOperatorKind() == semantic::SVAssertionUnaryOperator::Not) {
      FailureOr<FixedSequenceAlternatives> nested =
          compileFixedSequenceAlternatives(children.front(), resolvedClock);
      if (failed(nested) || nested->empty())
        return failure();

      // A one-cycle property alternative is a Boolean cube. Complement the
      // union of those cubes with De Morgan distribution, leaving the
      // resulting DNF to the compiler-side Boolean minimizer before monitor
      // SSA is materialized. Temporal endpoints, vacuity, first_match, and
      // match-item effects are deliberately excluded from this transform.
      FixedSequenceAlternatives results(1);
      results.front().ages.resize(1);
      for (const FixedSequence &alternative : *nested) {
        if (alternative.ages.size() != 1 || alternative.emptyMatch ||
            alternative.vacuousSuccess ||
            !alternative.firstMatchBoundaries.empty() ||
            !alternative.ages.front().matchItems.empty())
          return failure();

        const FixedSequenceAge &age = alternative.ages.front();
        size_t literalCount = age.predicates.size() +
                              age.negatedPredicates.size() +
                              age.caseGuards.size();
        if (literalCount == 0 ||
            results.size() > maxFixedSequenceAlternatives / literalCount)
          return failure();

        FixedSequenceAlternatives expanded;
        expanded.reserve(results.size() * literalCount);
        for (const FixedSequence &prefix : results) {
          for (Operation *predicate : age.predicates) {
            FixedSequence result = prefix;
            result.ages.front().negatedPredicates.push_back(predicate);
            expanded.push_back(std::move(result));
          }
          for (Operation *predicate : age.negatedPredicates) {
            FixedSequence result = prefix;
            result.ages.front().predicates.push_back(predicate);
            expanded.push_back(std::move(result));
          }
          for (FixedSequenceAge::CaseGuard guard : age.caseGuards) {
            FixedSequence result = prefix;
            guard.negated = !guard.negated;
            result.ages.front().caseGuards.push_back(guard);
            expanded.push_back(std::move(result));
          }
        }
        results = std::move(expanded);
      }
      return results;
    }

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
          FixedSequence qualifiedAlternative = alternative;
          qualifyFirstMatchGroups(qualifiedAlternative, unary.getOperation(),
                                  static_cast<uint64_t>(delay - minimum) + 1);
          FixedSequence shifted;
          if (failed(appendFixedSequence(shifted, qualifiedAlternative,
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
          FixedSequence qualifiedAlternative = alternative;
          qualifyFirstMatchGroups(qualifiedAlternative, unary.getOperation(),
                                  static_cast<uint64_t>(delay - minimum) + 1);
          FixedSequence combined = prefix;
          if (failed(mergeFixedSequenceAt(combined, qualifiedAlternative,
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
      for (auto [outerIndex, outer] : llvm::enumerate(*rhs)) {
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
          for (FirstMatchBoundary boundary : inner.firstMatchBoundaries) {
            boundary.groupPath.push_back(
                {binary.getOperation(),
                 static_cast<uint64_t>(outerIndex) * 64 + offset + 1});
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
  SmallVector<Operation *> clockChildren =
      clockEvent ? getChildren(clockEvent) : SmallVector<Operation *>{};
  size_t expectedClockChildren = clockEvent && clockEvent.getHasIff() ? 2 : 1;
  if (!clockEvent || clockChildren.size() != expectedClockChildren ||
      !isAddressableExpression(clockChildren.front()))
    return emitError(getSemanticLocation(clock))
               << "AOT concurrent monitors currently require one direct "
                  "signal edge clock with an optional executable iff "
                  "condition",
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

  semantic::SVAbortAssertionExprOp abort;
  Operation *abortCondition = nullptr;
  if (auto candidate =
          dyn_cast_or_null<semantic::SVAbortAssertionExprOp>(property)) {
    SmallVector<Operation *> nested = getChildren(candidate);
    if (nested.size() != 2)
      return candidate.emitError("malformed property abort expression"),
             failure();
    abort = candidate;
    abortCondition = nested.front();
    property = unwrapAssertionInstance(nested.back());
    if (!property)
      return candidate.emitError(
                 "property abort wraps an unsupported assertion instance"),
             failure();
  }

  semantic::SVUnaryAssertionExprOp temporalNegation;
  if (auto candidate =
          dyn_cast_or_null<semantic::SVUnaryAssertionExprOp>(property);
      candidate &&
      candidate.getOperatorKind() == semantic::SVAssertionUnaryOperator::Not) {
    SmallVector<Operation *> nested = getChildren(candidate);
    if (candidate.getHasRange() || nested.size() != 1)
      return candidate.emitError("malformed property negation"), failure();
    Operation *operand = unwrapAssertionInstance(nested.front());
    Operation *fixedOperand = operand;
    bool explicitStrength = false;
    if (auto strength =
            dyn_cast_or_null<semantic::SVStrongWeakAssertionExprOp>(operand)) {
      explicitStrength = true;
      SmallVector<Operation *> strengthChildren = getChildren(strength);
      if (strengthChildren.size() != 1)
        return strength.emitError("malformed strong/weak property"), failure();
      fixedOperand = unwrapAssertionInstance(strengthChildren.front());
    }
    // Keep one-cycle Boolean negation on the DNF path, where the optional
    // compiler-side solver can minimize the complemented formula. Temporal
    // negation retains the operand monitor and flips its completed result at
    // the exact clock where that result becomes known.
    if (fixedOperand) {
      FailureOr<FixedSequence> fixed = compileFixedSequence(fixedOperand);
      if (succeeded(fixed) && !fixed->emptyMatch && !fixed->vacuousSuccess &&
          (fixed->ages.size() > 1 || explicitStrength)) {
        temporalNegation = candidate;
        property = operand;
        function->setAttr("obelisk_sim.temporal_property_negation",
                          builder.getUnitAttr());
      }
    }
  }

  semantic::SVStrongWeakAssertionExprOp endStrength;
  if (auto candidate =
          dyn_cast_or_null<semantic::SVStrongWeakAssertionExprOp>(property)) {
    SmallVector<Operation *> nested = getChildren(candidate);
    if (nested.size() != 1)
      return candidate.emitError("malformed strong/weak property"), failure();
    endStrength = candidate;
    property = unwrapAssertionInstance(nested.front());
    if (!property)
      return candidate.emitError(
                 "strong/weak wraps an unsupported assertion instance"),
             failure();
  }
  Operation *endStrengthSource =
      endStrength
          ? endStrength.getOperation()
          : (temporalNegation ? temporalNegation.getOperation() : nullptr);

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
  FixedSequenceAlternatives antecedentAlternatives;
  FixedSequenceAlternatives consequentAlternatives;
  FixedSequenceAlternatives sequenceAlternatives;
  PersistentRepetitionSequence persistentRepetition;
  bool hasPersistentRepetition = false;
  PersistentUntilProperty persistentUntil;
  bool hasPersistentUntil = false;
  PersistentUnaryProperty persistentUnary;
  bool hasPersistentUnary = false;
  PersistentDelaySequence persistentDelay;
  bool hasPersistentDelay = false;
  bool nonoverlapped = false;
  bool followedBy = false;
  size_t consequentAlternativeAdmissionCount = 1;
  bool consequentAlternativesAdmissionEligible = true;
  auto recordBooleanMinimization = [&](const BooleanMinimizationStats &stats) {
    function->setAttr("obelisk_sim.sva_boolean_solver",
                      builder.getStringAttr(stats.backend));
    function->setAttr("obelisk_sim.sva_boolean_solver_queries",
                      builder.getI64IntegerAttr(stats.solverQueries));
    function->setAttr("obelisk_sim.sva_boolean_alternatives_before",
                      builder.getI64IntegerAttr(stats.alternativesBefore));
    function->setAttr("obelisk_sim.sva_boolean_alternatives_after",
                      builder.getI64IntegerAttr(stats.alternativesAfter));
    function->setAttr("obelisk_sim.sva_boolean_literals_before",
                      builder.getI64IntegerAttr(stats.literalsBefore));
    function->setAttr("obelisk_sim.sva_boolean_literals_after",
                      builder.getI64IntegerAttr(stats.literalsAfter));
  };
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
    FailureOr<FixedSequenceAlternatives> lhs =
        compileFixedSequenceAlternatives(operands.front(), clock);
    FailureOr<FixedSequenceAlternatives> rhs =
        compileFixedSequenceAlternatives(operands.back(), clock);
    FailureOr<PersistentDelaySequence> delayedRhs =
        compilePersistentDelay(operands.back());
    if (succeeded(lhs) && llvm::any_of(*lhs, [](const FixedSequence &value) {
          return value.emptyMatch;
        }))
      return emitError(getSemanticLocation(operands.front()))
                 << "empty-match implication/followed-by antecedents are not "
                    "executable yet; overlapping implication requires a "
                    "nondegenerate antecedent and nonoverlapping empty "
                    "antecedents require their distinct LRM start semantics",
             failure();
    if (succeeded(rhs) && llvm::any_of(*rhs, [](const FixedSequence &value) {
          return value.emptyMatch;
        }))
      return emitError(getSemanticLocation(operands.back()))
                 << "an empty-match sequence cannot be used as an "
                    "implication/followed-by consequent property",
             failure();
    if (failed(lhs) || lhs->empty() ||
        llvm::any_of(*lhs, [](const FixedSequence &value) {
          return value.ages.empty();
        })) {
      if (diagnoseUnsupportedConcurrentFeature(operands.front(),
                                               /*nested=*/true))
        return failure();
      return binary.emitError(
                 "AOT implication/followed-by antecedent must expand to at "
                 "most 256 bounded alternatives within the 63-cycle "
                 "horizon"),
             failure();
    }
    if ((failed(rhs) || rhs->empty() ||
         llvm::any_of(*rhs, [](const FixedSequence &value) {
           return value.ages.empty();
         })) &&
        failed(delayedRhs)) {
      if (diagnoseUnsupportedConcurrentFeature(operands.back(),
                                               /*nested=*/true))
        return failure();
      return binary.emitError(
                 "AOT implication/followed-by consequent must expand to at "
                 "most 256 bounded alternatives within the 63-cycle "
                 "horizon"),
             failure();
    }
    if (succeeded(rhs)) {
      consequentAlternativeAdmissionCount = rhs->size();
      consequentAlternativesAdmissionEligible =
          llvm::all_of(*rhs, [](const FixedSequence &alternative) {
            return alternative.ages.size() == 1 &&
                   !alternative.vacuousSuccess &&
                   alternative.firstMatchBoundaries.empty() &&
                   alternative.ages.front().matchItems.empty();
          });
    }
    if (!cover && succeeded(rhs))
      if (std::optional<BooleanMinimizationStats> stats =
              minimizeBooleanAlternatives(*rhs))
        recordBooleanMinimization(*stats);
    implication = binary;
    if (lhs->size() == 1)
      antecedentSequence = std::move(lhs->front());
    else
      antecedentAlternatives = std::move(*lhs);
    if (succeeded(delayedRhs)) {
      persistentDelay = std::move(*delayedRhs);
      hasPersistentDelay = true;
      // The compact implication path below feeds each antecedent match into
      // the deterministic bounded-prefix pipeline. Prefix attempts remain
      // one bit per age because at most one new antecedent match starts on a
      // clock tick; successful prefixes then merge into the delay aggregate.
      sequence.ages.resize(1);
    } else {
      if (rhs->size() == 1)
        sequence = std::move(rhs->front());
      else
        consequentAlternatives = std::move(*rhs);
    }
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
    if (FailureOr<PersistentDelaySequence> delay =
            compilePersistentDelay(property);
        succeeded(delay)) {
      persistentDelay = std::move(*delay);
      hasPersistentDelay = true;
      sequence.ages.resize(1);
    } else if (FailureOr<PersistentUnaryProperty> unary =
                   compilePersistentUnary(property);
               succeeded(unary)) {
      persistentUnary = std::move(*unary);
      hasPersistentUnary = true;
      // The aggregate monitor owns its warm-up and eligible token counts.
      // Keep ordinary bounded validation structurally nonempty.
      sequence.ages.resize(1);
    } else if (FailureOr<PersistentUntilProperty> until =
                   compilePersistentUntil(property);
               succeeded(until)) {
      persistentUntil = std::move(*until);
      hasPersistentUntil = true;
      // The persistent monitor owns one aggregate live-attempt counter below.
      // Keep ordinary bounded validation structurally nonempty.
      sequence.ages.resize(1);
    } else if (FailureOr<PersistentRepetitionSequence> persistent =
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
      if (llvm::any_of(*compiled, [](const FixedSequence &alternative) {
            return alternative.emptyMatch;
          }))
        return emitError(getSemanticLocation(property))
                   << (coverSequence
                           ? "standalone empty-match cover-sequence accounting "
                             "is not executable yet"
                           : "a sequence used as a property cannot admit an "
                             "empty match")
                   << "; use positive-delay concatenation to eliminate the "
                      "empty endpoint",
               failure();
      if (!cover) {
        if (std::optional<BooleanMinimizationStats> stats =
                minimizeBooleanAlternatives(*compiled))
          recordBooleanMinimization(*stats);
      }
      if (compiled->size() == 1)
        sequence = std::move(compiled->front());
      else
        sequenceAlternatives = std::move(*compiled);
    }
  }
  bool branchingSequence = !sequenceAlternatives.empty();
  bool branchingAntecedent = !antecedentAlternatives.empty();
  bool branchingConsequent = !consequentAlternatives.empty();
  bool combinedBranchingRequested =
      branchingAntecedent && consequentAlternativeAdmissionCount > 1;
  bool combinedBranchingWithinLimit =
      !combinedBranchingRequested ||
      antecedentAlternatives.size() <=
          maxFixedSequenceAlternatives /
              consequentAlternativeAdmissionCount;
  bool combinedBooleanBranching =
      branchingAntecedent && branchingConsequent &&
      combinedBranchingWithinLimit &&
      consequentAlternativesAdmissionEligible &&
      llvm::all_of(consequentAlternatives,
                   [](const FixedSequence &alternative) {
                     return alternative.ages.size() == 1 &&
                            !alternative.vacuousSuccess &&
                            alternative.firstMatchBoundaries.empty() &&
                            alternative.ages.front().matchItems.empty();
                   });
  bool boundedFirstMatch =
      firstMatch ||
      llvm::any_of(sequenceAlternatives,
                   [](const FixedSequence &alternative) {
                     return !alternative.firstMatchBoundaries.empty();
                   }) ||
      llvm::any_of(antecedentAlternatives,
                   [](const FixedSequence &alternative) {
                     return !alternative.firstMatchBoundaries.empty();
                   }) ||
      llvm::any_of(consequentAlternatives,
                   [](const FixedSequence &alternative) {
                     return !alternative.firstMatchBoundaries.empty();
                   });
  if (boundedFirstMatch)
    function->setAttr("obelisk_sim.first_match_monitor", builder.getUnitAttr());
  if ((!branchingSequence && !branchingConsequent && sequence.ages.empty()) ||
      llvm::any_of(sequenceAlternatives,
                   [](const FixedSequence &alternative) {
                     return alternative.ages.empty() ||
                            alternative.ages.size() > 63;
                   }) ||
      sequence.ages.size() > 63)
    return op.emitError("concurrent monitor horizon must be 1..63 cycles"),
           failure();
  size_t antecedentHorizon = antecedentSequence.ages.size();
  for (const FixedSequence &alternative : antecedentAlternatives)
    antecedentHorizon = std::max(antecedentHorizon, alternative.ages.size());
  size_t consequentHorizon = sequence.ages.size();
  for (const FixedSequence &alternative : consequentAlternatives)
    consequentHorizon = std::max(consequentHorizon, alternative.ages.size());
  if (implication && antecedentHorizon + consequentHorizon > 63)
    return op.emitError(
               "combined implication/followed-by antecedent/consequent state "
               "exceeds the 63-cycle bounded monitor horizon"),
           failure();
  if (implication && localInstance &&
      (branchingAntecedent || antecedentSequence.ages.size() != 1))
    return emitError(getSemanticLocation(implication))
               << "multi-cycle implication/followed-by antecedents do not yet "
                  "compose with assertion locals",
           failure();
  auto hasMatchItems = [](const FixedSequence &value) {
    return llvm::any_of(value.ages, [](const FixedSequenceAge &age) {
      return !age.matchItems.empty();
    });
  };
  if (implication && !localInstance &&
      (hasMatchItems(antecedentSequence) ||
       llvm::any_of(antecedentAlternatives, hasMatchItems)))
    return emitError(getSemanticLocation(implication))
               << "implication/followed-by antecedent match items require "
                  "assertion local flow",
           failure();
  if (hasPersistentDelay && (localInstance || expectMonitor || firstMatch))
    return emitError(getSemanticLocation(property))
               << "unbounded sequence delay ##[M:$] currently requires one "
                  "deterministic sequence without locals, first_match, or "
                  "expect",
           failure();
  if (hasPersistentDelay && implication &&
      (branchingAntecedent || antecedentSequence.ages.size() != 1 ||
       !antecedentSequence.ages.front().matchItems.empty() ||
       !antecedentSequence.ages.front().caseGuards.empty()))
    return emitError(getSemanticLocation(implication))
               << "unbounded implication/followed-by delay currently "
                  "requires one Boolean antecedent and a deterministic "
                  "bounded consequent prefix before the final ##[M:$] "
                  "Boolean terminal",
           failure();
  if (branchingAntecedent &&
      (localInstance || expectMonitor || endStrength))
    return emitError(getSemanticLocation(implication))
               << "branching implication/followed-by antecedents currently "
                  "require a concurrent directive without locals, expect, "
                  "or outer strong/weak qualification",
           failure();
  if (branchingAntecedent && llvm::any_of(antecedentAlternatives,
                                          [](const FixedSequence &alternative) {
                                            return alternative.vacuousSuccess;
                                          }))
    return emitError(getSemanticLocation(implication))
               << "branching implication/followed-by antecedents cannot "
                  "contain vacuous property alternatives",
           failure();
  if ((combinedBranchingRequested &&
       (!combinedBranchingWithinLimit ||
        !consequentAlternativesAdmissionEligible)) ||
      (branchingAntecedent && branchingConsequent &&
       !combinedBooleanBranching))
    return emitError(getSemanticLocation(implication))
               << "combined branching implication/followed-by currently "
                  "requires a one-cycle bounded consequent without "
                  "first_match, vacuous alternatives, or match items and at "
                  "most 256 antecedent/consequent alternative pairs",
           failure();
  if (branchingConsequent &&
      ((!branchingAntecedent && antecedentSequence.ages.size() != 1) ||
       localInstance || expectMonitor || endStrength))
    return emitError(getSemanticLocation(implication))
               << "branching implication/followed-by consequents currently "
                  "require one Boolean antecedent in a concurrent directive "
                  "without locals, expect, or outer "
                  "strong/weak qualification",
           failure();
  if (branchingConsequent &&
      llvm::any_of(consequentAlternatives, hasMatchItems))
    return emitError(getSemanticLocation(implication))
               << "branching implication/followed-by consequents do not yet "
                  "support match items or assertion-local flow",
           failure();
  if (combinedBooleanBranching)
    // The branching-antecedent monitor owns the exact consequent truth and
    // per-antecedent match channels. Keep its existing one-age state shape.
    sequence.ages.resize(1);
  if (hasPersistentRepetition && (localInstance || implication ||
                                  expectMonitor || firstMatch || coverSequence))
    return emitError(getSemanticLocation(property))
               << "persistent [*]/[->]/[=] repetition currently requires a "
                  "plain "
                  "assert, assume, cover-property, or restrict directive "
                  "without locals, implication, first_match, "
                  "expect, or cover-sequence per-match accounting",
           failure();
  if (hasPersistentUntil && (localInstance || implication || expectMonitor ||
                             firstMatch || coverSequence))
    return emitError(getSemanticLocation(property))
               << "persistent until currently requires a plain assert, "
                  "assume, cover-property, or restrict directive without "
                  "locals, implication/followed-by, first_match, expect, or "
                  "cover-sequence per-match "
                  "accounting",
           failure();
  if (hasPersistentUnary && (localInstance || implication || expectMonitor ||
                             firstMatch || coverSequence))
    return emitError(getSemanticLocation(property))
               << "persistent property operator '"
               << semantic::stringifySVAssertionUnaryOperator(
                      persistentUnary.kind)
               << "' currently requires one outermost unbounded form over "
                  "a deterministic one-cycle Boolean operand without "
                  "locals, match items, implication/followed-by, first_match, "
                  "expect, or cover-sequence "
                  "per-match accounting",
           failure();
  if (abort) {
    std::string spelling =
        (Twine(abort.getIsSynchronous() ? "sync_" : "") +
         semantic::stringifySVAssertionAbortAction(abort.getAction()) + "_on")
            .str();
    bool matchItems =
        llvm::any_of(sequence.ages, [](const FixedSequenceAge &age) {
          return !age.matchItems.empty();
        });
    if (temporalNegation)
      return emitError(getSemanticLocation(abort))
                 << "SVA property operator '" << spelling
                 << "' does not yet compose with bounded temporal property "
                    "negation",
             failure();
    if (localInstance || implication || disable || expectMonitor ||
        firstMatch || coverSequence || branchingSequence || matchItems)
      return emitError(getSemanticLocation(abort))
                 << "SVA property operator '" << spelling
                 << "' currently requires one outermost abort around a "
                    "deterministic bounded property or supported aggregate "
                    "persistent property without locals, match items, "
                    "implication/followed-by, disable iff, first_match, "
                    "expect, or cover-sequence per-match accounting",
             failure();
    function->setAttr(abort.getIsSynchronous()
                          ? "obelisk_sim.synchronous_property_abort"
                          : "obelisk_sim.asynchronous_property_abort",
                      builder.getUnitAttr());
    function->setAttr(
        "obelisk_sim.property_abort_action",
        builder.getStringAttr(
            semantic::stringifySVAssertionAbortAction(abort.getAction())));
  }
  if (branchingSequence && (localInstance || implication || expectMonitor))
    return emitError(getSemanticLocation(property))
               << "branching bounded sequences currently require a "
                  "concurrent directive without locals, implication, or "
                  "expect",
           failure();
  if (endStrength && (implication || hasPersistentUntil || hasPersistentUnary ||
                      expectMonitor || coverSequence))
    return emitError(getSemanticLocation(endStrengthSource))
               << "SVA '"
               << semantic::stringifySVAssertionStrength(
                      endStrength.getStrength())
               << "' end-of-simulation qualification currently requires "
                  "one outermost bounded sequence property without "
                  "implication/followed-by, unsupported persistent "
                  "operators, expect, or "
                  "cover-sequence per-match accounting",
           failure();
  if (temporalNegation &&
      (implication || branchingSequence || hasPersistentDelay ||
       hasPersistentRepetition || hasPersistentUntil || hasPersistentUnary ||
       firstMatch || expectMonitor || coverSequence))
    return emitError(getSemanticLocation(temporalNegation))
               << "temporal property 'not' currently requires one "
                  "deterministic bounded sequence, optionally qualified by "
                  "strong/weak, without implication/followed-by, persistent "
                  "operators, first_match, expect, or cover-sequence "
                  "per-match accounting",
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
  bool persistentStateOwner = hasPersistentDelay || hasPersistentUnary ||
                              hasPersistentUntil || hasPersistentRepetition;
  bool needsState =
      (disable && !persistentStateOwner && !branchingSequence &&
       !branchingAntecedent && !branchingConsequent) ||
      (abort && !abort.getIsSynchronous() && !persistentStateOwner) ||
      (!branchingSequence && sequence.ages.size() > 1) ||
      (implication && !branchingAntecedent && !branchingConsequent &&
       !hasPersistentDelay &&
       (nonoverlapped || antecedentSequence.ages.size() > 1));
  if (implication && antecedentHorizon > 1)
    function->setAttr("obelisk_sim.bounded_antecedent_horizon",
                      builder.getI64IntegerAttr(antecedentHorizon));
  Value stateStorage;
  if (needsState)
    stateStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);

  Value disableEpoch;
  Value initialDisable;
  sim::SimObserverBindOp disableObserverBinding;
  sim::SimDesignOp disableDesign;
  if (disable) {
    // `disable iff` is unsampled and asynchronous. Bind its two-state truth
    // value as a computed observer: every false-to-true transition wakes a
    // cold Reactive actor which clears live attempts and advances an epoch.
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
    disableObserverBinding = observer->getDefiningOp<sim::SimObserverBindOp>();
    if (!disableObserverBinding)
      return emitError(getSemanticLocation(disable))
                 << "disable iff expression has no observer binding",
             failure();
    for (Value capture : disableObserverBinding.getValues())
      if (isa<sim::RefType>(capture.getType()) &&
          !isStaticallyAllocatedOverrideTarget(capture))
        return emitError(getSemanticLocation(disable))
                   << "disable iff cannot asynchronously observe an "
                      "automatic variable",
               failure();
    disableDesign = function->getParentOfType<sim::SimDesignOp>();
    appendObserverRequest(function, concurrentCancelObserverRequestAttrName,
                          disableObserverBinding.getEvaluatorAttr());
    disableEpoch = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);
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

  auto outlineDisableObserver =
      [&](ArrayRef<Value> stateStorages) -> LogicalResult {
    if (!disable)
      return success();
    if (!disableDesign || !disableObserverBinding)
      return function.emitError(
                 "concurrent disable outlining requires a design and "
                 "observer"),
             failure();

    std::string symbol =
        (function.getSymName() + ".$concurrent_cancel." + Twine(node)).str();
    std::string identity =
        (function.getSymName() + ".$concurrent_disable." + Twine(node)).str();
    uint64_t codeUnitID = stableCodeUnitID(identity);
    uint64_t scopeID = 0;
    std::string parentHierarchy = function.getSymName().str();
    uint64_t parentID = function.getCodeUnitId().value_or(0);
    for (sim::SimCodeUnitDeclOp declaration :
         disableDesign.getBody().front().getOps<sim::SimCodeUnitDeclOp>()) {
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
    llvm::append_range(captures, disableObserverBinding.getValues());
    unsigned initialDisableIndex = captures.size();
    captures.push_back(initialDisable);
    unsigned stateStorageBegin = captures.size();
    llvm::append_range(captures, stateStorages);
    unsigned stateStorageEnd = captures.size();
    unsigned disableEpochIndex = captures.size();
    captures.push_back(disableEpoch);

    SmallVector<Type> inputs;
    SmallVector<DictionaryAttr> argumentAttrs;
    for (auto [index, capture] : llvm::enumerate(captures)) {
      inputs.push_back(capture.getType());
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
        disableObserverBinding.getResult().getType(),
        disableObserverBinding.getEvaluatorAttr(), reboundValues,
        disableObserverBinding.getCaptureCountAttr());
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
    for (unsigned index = stateStorageBegin; index != stateStorageEnd; ++index)
      sim::SimRefStoreOp::create(cancelBuilder, getSemanticLocation(disable),
                                 cancelZero, entry.getArgument(index));
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
    return success();
  };

  auto cancelDisabledSample =
      [&](Block *wait, ArrayRef<Value> stateStorages) -> LogicalResult {
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
    for (Value storage : stateStorages)
      sim::SimRefStoreOp::create(builder, getSemanticLocation(disable), zero,
                                 storage);
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

  if (disable && !persistentStateOwner && !branchingSequence &&
      !branchingAntecedent && !branchingConsequent &&
      failed(outlineDisableObserver({stateStorage})))
    return failure();

  auto scheduleResult = [&](bool passed) {
    if (temporalNegation)
      passed = !passed;
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
  auto scheduleCount = [&](Value count, bool passed) {
    std::optional<ReportCallback> &report = passed ? passReport : failReport;
    if (!report)
      return;
    Value one = arith::ConstantOp::create(builder, location, stateType,
                                          builder.getI64IntegerAttr(1));
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

  auto outlineCountedEndOfSimulation = [&](ArrayRef<Value> countStorages,
                                           ArrayRef<Value> bitsetStorages,
                                           bool passed, StringRef identityTag,
                                           Operation *source) -> LogicalResult {
    std::optional<ReportCallback> &report = passed ? passReport : failReport;
    if ((countStorages.empty() && bitsetStorages.empty()) || !report)
      return success();

    auto design = function->getParentOfType<sim::SimDesignOp>();
    if (!design)
      return function.emitError(
                 "counted end-of-simulation completion requires a "
                 "simulation design"),
             failure();

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

    Value context = function.getBody().front().getArgument(0);
    SmallVector<Value> captures{context};
    DenseMap<Value, unsigned> captureIndices;
    captureIndices.try_emplace(context, 0);
    SmallVector<unsigned> countIndices;
    for (Value storage : countStorages) {
      auto [entry, inserted] =
          captureIndices.try_emplace(storage, captures.size());
      if (inserted)
        captures.push_back(storage);
      countIndices.push_back(entry->second);
    }
    SmallVector<unsigned> bitsetIndices;
    for (Value storage : bitsetStorages) {
      auto [entry, inserted] =
          captureIndices.try_emplace(storage, captures.size());
      if (inserted)
        captures.push_back(storage);
      bitsetIndices.push_back(entry->second);
    }
    SmallVector<unsigned> reportCaptureIndices;
    for (Value capture : report->captures) {
      auto [entry, inserted] =
          captureIndices.try_emplace(capture, captures.size());
      if (inserted)
        captures.push_back(capture);
      reportCaptureIndices.push_back(entry->second);
    }
    std::optional<unsigned> disableEpochIndex;
    if (disableEpoch) {
      auto found = captureIndices.find(disableEpoch);
      if (found == captureIndices.end())
        return function.emitError(
                   "counted end-of-simulation report is missing its disable "
                   "epoch capture"),
               failure();
      disableEpochIndex = found->second;
    }

    SmallVector<Type> inputTypes;
    SmallVector<DictionaryAttr> argumentAttrs;
    for (auto [index, capture] : llvm::enumerate(captures)) {
      inputTypes.push_back(capture.getType());
      SmallVector<NamedAttribute> metadata;
      if (auto argument = dyn_cast<BlockArgument>(capture);
          argument && argument.getOwner() == &function.getBody().front()) {
        if (DictionaryAttr sourceAttrs =
                function.getArgAttrDict(argument.getArgNumber()))
          llvm::append_range(metadata, sourceAttrs);
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

    std::string symbol = (function.getSymName() + ".$concurrent_eos_count." +
                          Twine(node) + "." + identityTag)
                             .str();
    std::string identity =
        (function.getSymName() + ".$concurrent_eos_count_identity." +
         Twine(node) + "." + identityTag)
            .str();
    uint64_t codeUnitID = stableCodeUnitID(identity);
    std::string hierarchy =
        (Twine(parentHierarchy) + ".$concurrent_eos_count." + Twine(node) +
         "." + identityTag)
            .str();
    OpBuilder outlineBuilder(function);
    outlineBuilder.setInsertionPoint(function);
    sim::SimCodeUnitDeclOp::create(
        outlineBuilder, getSemanticLocation(source), codeUnitID, scopeID,
        sim::EntryKind::Final, outlineBuilder.getStringAttr(hierarchy),
        outlineBuilder.getStringAttr(
            "counted concurrent assertion end-of-simulation coordinator"),
        outlineBuilder.getUnitAttr());
    SmallVector<NamedAttribute> attributes{
        outlineBuilder.getNamedAttr(bindingsAttrName,
                                    outlineBuilder.getArrayAttr({})),
        outlineBuilder.getNamedAttr(
            "code_unit_id", outlineBuilder.getI64IntegerAttr(codeUnitID)),
        outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(
            "home_region", sim::EventRegionAttr::get(function.getContext(),
                                                     sim::EventRegion::Active)),
        outlineBuilder.getNamedAttr(
            "domain", sim::ExecutionDomainAttr::get(
                          function.getContext(), sim::ExecutionDomain::Design)),
        outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                    outlineBuilder.getStringAttr(hierarchy)),
    };
    sim::SimFuncOp coordinator = sim::SimFuncOp::create(
        outlineBuilder, getSemanticLocation(source), symbol,
        FunctionType::get(function.getContext(), inputTypes, TypeRange{}),
        sim::EntryKind::Final, attributes, argumentAttrs);
    SymbolTable::setSymbolVisibility(coordinator,
                                     SymbolTable::Visibility::Private);
    coordinator->setAttr("obelisk_sim.concurrent_eos_coordinator",
                         builder.getUnitAttr());
    coordinator->setAttr("obelisk_sim.concurrent_eos_counted",
                         builder.getUnitAttr());
    coordinator->setAttr("obelisk_sim.detached_controls",
                         builder.getUnitAttr());

    Block &entry = coordinator.getBody().front();
    OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
    Value count =
        arith::ConstantOp::create(entryBuilder, getSemanticLocation(source),
                                  stateType, entryBuilder.getI64IntegerAttr(0));
    for (unsigned index : countIndices) {
      Value amount =
          sim::SimRefLoadOp::create(entryBuilder, getSemanticLocation(source),
                                    stateType, entry.getArgument(index));
      count = arith::AddIOp::create(entryBuilder, getSemanticLocation(source),
                                    count, amount);
    }
    Value one =
        arith::ConstantOp::create(entryBuilder, getSemanticLocation(source),
                                  stateType, entryBuilder.getI64IntegerAttr(1));
    Block *current = &entry;
    for (unsigned index : bitsetIndices) {
      OpBuilder currentBuilder = OpBuilder::atBlockEnd(current);
      Value bits =
          sim::SimRefLoadOp::create(currentBuilder, getSemanticLocation(source),
                                    stateType, entry.getArgument(index));
      Block *bitLoop = new Block;
      Block *bitBody = new Block;
      Block *bitDone = new Block;
      bitLoop->addArgument(stateType, getSemanticLocation(source));
      bitLoop->addArgument(stateType, getSemanticLocation(source));
      bitDone->addArgument(stateType, getSemanticLocation(source));
      coordinator.getBody().push_back(bitLoop);
      coordinator.getBody().push_back(bitBody);
      coordinator.getBody().push_back(bitDone);
      cf::BranchOp::create(currentBuilder, getSemanticLocation(source), bitLoop,
                           ValueRange{bits, count});

      OpBuilder bitLoopBuilder = OpBuilder::atBlockEnd(bitLoop);
      Value remainingBits = bitLoop->getArgument(0);
      Value accumulated = bitLoop->getArgument(1);
      Value hasBits = arith::CmpIOp::create(
          bitLoopBuilder, getSemanticLocation(source), arith::CmpIPredicate::ne,
          remainingBits,
          arith::ConstantOp::create(bitLoopBuilder, getSemanticLocation(source),
                                    stateType,
                                    bitLoopBuilder.getI64IntegerAttr(0)));
      cf::CondBranchOp::create(bitLoopBuilder, getSemanticLocation(source),
                               hasBits, bitBody, ValueRange{}, bitDone,
                               ValueRange{accumulated});

      OpBuilder bitBodyBuilder = OpBuilder::atBlockEnd(bitBody);
      Value decremented = arith::SubIOp::create(
          bitBodyBuilder, getSemanticLocation(source), remainingBits, one);
      Value nextBits =
          arith::AndIOp::create(bitBodyBuilder, getSemanticLocation(source),
                                remainingBits, decremented);
      Value nextAccumulated = arith::AddIOp::create(
          bitBodyBuilder, getSemanticLocation(source), accumulated, one);
      cf::BranchOp::create(bitBodyBuilder, getSemanticLocation(source), bitLoop,
                           ValueRange{nextBits, nextAccumulated});
      current = bitDone;
      count = bitDone->getArgument(0);
    }
    Block *loop = new Block;
    Block *body = new Block;
    Block *done = new Block;
    loop->addArgument(stateType, getSemanticLocation(source));
    coordinator.getBody().push_back(loop);
    coordinator.getBody().push_back(body);
    coordinator.getBody().push_back(done);
    OpBuilder currentBuilder = OpBuilder::atBlockEnd(current);
    cf::BranchOp::create(currentBuilder, getSemanticLocation(source), loop,
                         ValueRange{count});

    OpBuilder loopBuilder = OpBuilder::atBlockEnd(loop);
    Value remaining = loop->getArgument(0);
    Value nonzero = arith::CmpIOp::create(
        loopBuilder, getSemanticLocation(source), arith::CmpIPredicate::ne,
        remaining,
        arith::ConstantOp::create(loopBuilder, getSemanticLocation(source),
                                  stateType, loopBuilder.getI64IntegerAttr(0)));
    cf::CondBranchOp::create(loopBuilder, getSemanticLocation(source), nonzero,
                             body, ValueRange{}, done, ValueRange{});

    OpBuilder bodyBuilder = OpBuilder::atBlockEnd(body);
    SmallVector<Value> reportOperands;
    for (unsigned index : reportCaptureIndices)
      reportOperands.push_back(entry.getArgument(index));
    if (disableEpochIndex)
      reportOperands.push_back(
          sim::SimRefLoadOp::create(bodyBuilder, report->location, stateType,
                                    entry.getArgument(*disableEpochIndex)));
    sim::SimSpawnOp::create(bodyBuilder, report->location,
                            report->function.getSymNameAttr(), reportOperands,
                            ArrayAttr{}, ArrayAttr{});
    Value next = arith::SubIOp::create(bodyBuilder, getSemanticLocation(source),
                                       remaining, one);
    cf::BranchOp::create(bodyBuilder, getSemanticLocation(source), loop,
                         ValueRange{next});

    OpBuilder doneBuilder = OpBuilder::atBlockEnd(done);
    sim::SimReturnOp::create(doneBuilder, getSemanticLocation(source),
                             ValueRange{});
    sim::SimSpawnOp::create(builder, getSemanticLocation(source),
                            coordinator.getSymNameAttr(), captures, ArrayAttr{},
                            ArrayAttr{});
    return success();
  };

  struct PersistentAbortPlan {
    FlatSymbolRefAttr dispatcher;
    SmallVector<Value> operands;
  };
  auto preparePersistentAbort = [&](ArrayRef<Value> countStorages,
                                    ArrayRef<Value> bitsetStorages)
      -> FailureOr<std::optional<PersistentAbortPlan>> {
    if (!abort)
      return std::optional<PersistentAbortPlan>{};
    if (countStorages.empty() && bitsetStorages.empty())
      return function.emitError(
                 "persistent property abort requires aggregate monitor state"),
             failure();

    auto design = function->getParentOfType<sim::SimDesignOp>();
    if (!design)
      return function.emitError(
                 "persistent property abort requires a simulation design"),
             failure();

    bool accepted =
        abort.getAction() == semantic::SVAssertionAbortAction::Accept;
    std::optional<ReportCallback> *selectedReport =
        accepted ? &passReport : &failReport;
    bool emitReports = *selectedReport && !(accepted && cover);

    Value context = function.getBody().front().getArgument(0);
    SmallVector<Value> captures{context};
    DenseMap<Value, unsigned> captureIndices;
    captureIndices.try_emplace(context, 0);
    auto appendCapture = [&](Value value) {
      auto [entry, inserted] =
          captureIndices.try_emplace(value, captures.size());
      if (inserted)
        captures.push_back(value);
      return entry->second;
    };
    SmallVector<unsigned> countIndices;
    for (Value storage : countStorages)
      countIndices.push_back(appendCapture(storage));
    SmallVector<unsigned> bitsetIndices;
    for (Value storage : bitsetStorages)
      bitsetIndices.push_back(appendCapture(storage));
    SmallVector<unsigned> reportCaptureIndices;
    if (emitReports)
      for (Value capture : (*selectedReport)->captures)
        reportCaptureIndices.push_back(appendCapture(capture));

    SmallVector<Type> inputTypes;
    SmallVector<DictionaryAttr> argumentAttrs;
    for (auto [index, capture] : llvm::enumerate(captures)) {
      inputTypes.push_back(capture.getType());
      SmallVector<NamedAttribute> metadata;
      if (auto argument = dyn_cast<BlockArgument>(capture);
          argument && argument.getOwner() == &function.getBody().front()) {
        if (DictionaryAttr sourceAttrs =
                function.getArgAttrDict(argument.getArgNumber()))
          llvm::append_range(metadata, sourceAttrs);
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
    unsigned extraCountIndex = inputTypes.size();
    inputTypes.push_back(stateType);
    argumentAttrs.push_back(builder.getDictionaryAttr({builder.getNamedAttr(
        "obelisk_sim.capture_kind",
        sim::CaptureKindAttr::get(function.getContext(),
                                  sim::CaptureKind::Formal))}));

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

    StringRef action = accepted ? "accept" : "reject";
    std::string symbol = (function.getSymName() + ".$concurrent_abort_count." +
                          Twine(node) + "." + action)
                             .str();
    std::string identity =
        (function.getSymName() + ".$concurrent_abort_count_identity." +
         Twine(node) + "." + action)
            .str();
    uint64_t codeUnitID = stableCodeUnitID(identity);
    std::string hierarchy =
        (Twine(parentHierarchy) + ".$concurrent_abort_count." + Twine(node) +
         "." + action)
            .str();
    OpBuilder outlineBuilder(function);
    outlineBuilder.setInsertionPoint(function);
    sim::SimCodeUnitDeclOp::create(
        outlineBuilder, getSemanticLocation(abort), codeUnitID, scopeID,
        sim::EntryKind::Function, outlineBuilder.getStringAttr(hierarchy),
        outlineBuilder.getStringAttr(
            "counted persistent concurrent assertion abort dispatcher"),
        outlineBuilder.getUnitAttr());
    SmallVector<NamedAttribute> attributes{
        outlineBuilder.getNamedAttr(bindingsAttrName,
                                    outlineBuilder.getArrayAttr({})),
        outlineBuilder.getNamedAttr(
            "code_unit_id", outlineBuilder.getI64IntegerAttr(codeUnitID)),
        outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(
            "home_region", sim::EventRegionAttr::get(function.getContext(),
                                                     sim::EventRegion::Active)),
        outlineBuilder.getNamedAttr(
            "domain", sim::ExecutionDomainAttr::get(
                          function.getContext(), sim::ExecutionDomain::Design)),
        outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                    outlineBuilder.getStringAttr(hierarchy)),
    };
    sim::SimFuncOp dispatcher = sim::SimFuncOp::create(
        outlineBuilder, getSemanticLocation(abort), symbol,
        FunctionType::get(function.getContext(), inputTypes, TypeRange{}),
        sim::EntryKind::Function, attributes, argumentAttrs);
    SymbolTable::setSymbolVisibility(dispatcher,
                                     SymbolTable::Visibility::Private);
    dispatcher->setAttr("obelisk_sim.concurrent_abort_counted",
                        builder.getUnitAttr());

    Block &entry = dispatcher.getBody().front();
    OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
    Value dispatchZero =
        arith::ConstantOp::create(entryBuilder, getSemanticLocation(abort),
                                  stateType, entryBuilder.getI64IntegerAttr(0));
    Value total = entry.getArgument(extraCountIndex);
    for (unsigned index : countIndices) {
      Value amount =
          sim::SimRefLoadOp::create(entryBuilder, getSemanticLocation(abort),
                                    stateType, entry.getArgument(index));
      total = arith::AddIOp::create(entryBuilder, getSemanticLocation(abort),
                                    total, amount);
      sim::SimRefStoreOp::create(entryBuilder, getSemanticLocation(abort),
                                 dispatchZero, entry.getArgument(index));
    }

    // Count every live attempt represented by a bitset without introducing a
    // Math dialect dependency into design bytecode. Kernighan's recurrence
    // clears the least-significant set bit on each trip, so the generated CFG
    // is proportional to the number of live attempts rather than the fixed
    // state width. Each completed loop carries the accumulated total into the
    // next bitset.
    Value one;
    if (!bitsetIndices.empty() || emitReports)
      one = arith::ConstantOp::create(entryBuilder, getSemanticLocation(abort),
                                      stateType,
                                      entryBuilder.getI64IntegerAttr(1));
    Block *currentBlock = &entry;
    for (unsigned index : bitsetIndices) {
      OpBuilder currentBuilder = OpBuilder::atBlockEnd(currentBlock);
      Value bits =
          sim::SimRefLoadOp::create(currentBuilder, getSemanticLocation(abort),
                                    stateType, entry.getArgument(index));
      sim::SimRefStoreOp::create(currentBuilder, getSemanticLocation(abort),
                                 dispatchZero, entry.getArgument(index));

      Block *bitLoop = new Block;
      Block *bitBody = new Block;
      Block *bitDone = new Block;
      bitLoop->addArgument(stateType, getSemanticLocation(abort));
      bitLoop->addArgument(stateType, getSemanticLocation(abort));
      bitDone->addArgument(stateType, getSemanticLocation(abort));
      dispatcher.getBody().push_back(bitLoop);
      dispatcher.getBody().push_back(bitBody);
      dispatcher.getBody().push_back(bitDone);
      cf::BranchOp::create(currentBuilder, getSemanticLocation(abort), bitLoop,
                           ValueRange{bits, total});

      OpBuilder loopBuilder = OpBuilder::atBlockEnd(bitLoop);
      Value remainingBits = bitLoop->getArgument(0);
      Value accumulated = bitLoop->getArgument(1);
      Value nonzero = arith::CmpIOp::create(
          loopBuilder, getSemanticLocation(abort), arith::CmpIPredicate::ne,
          remainingBits, dispatchZero);
      cf::CondBranchOp::create(loopBuilder, getSemanticLocation(abort), nonzero,
                               bitBody, ValueRange{}, bitDone,
                               ValueRange{accumulated});

      OpBuilder bodyBuilder = OpBuilder::atBlockEnd(bitBody);
      Value decremented = arith::SubIOp::create(
          bodyBuilder, getSemanticLocation(abort), remainingBits, one);
      Value nextBits = arith::AndIOp::create(
          bodyBuilder, getSemanticLocation(abort), remainingBits, decremented);
      Value nextAccumulated = arith::AddIOp::create(
          bodyBuilder, getSemanticLocation(abort), accumulated, one);
      cf::BranchOp::create(bodyBuilder, getSemanticLocation(abort), bitLoop,
                           ValueRange{nextBits, nextAccumulated});

      currentBlock = bitDone;
      total = bitDone->getArgument(0);
    }

    if (emitReports) {
      OpBuilder currentBuilder = OpBuilder::atBlockEnd(currentBlock);
      Block *loop = new Block;
      Block *body = new Block;
      Block *done = new Block;
      loop->addArgument(stateType, getSemanticLocation(abort));
      dispatcher.getBody().push_back(loop);
      dispatcher.getBody().push_back(body);
      dispatcher.getBody().push_back(done);
      cf::BranchOp::create(currentBuilder, getSemanticLocation(abort), loop,
                           ValueRange{total});

      OpBuilder loopBuilder = OpBuilder::atBlockEnd(loop);
      Value remaining = loop->getArgument(0);
      Value nonzero = arith::CmpIOp::create(
          loopBuilder, getSemanticLocation(abort), arith::CmpIPredicate::ne,
          remaining, dispatchZero);
      cf::CondBranchOp::create(loopBuilder, getSemanticLocation(abort), nonzero,
                               body, ValueRange{}, done, ValueRange{});

      OpBuilder bodyBuilder = OpBuilder::atBlockEnd(body);
      SmallVector<Value> reportOperands;
      for (unsigned index : reportCaptureIndices)
        reportOperands.push_back(entry.getArgument(index));
      sim::SimSpawnOp::create(bodyBuilder, (*selectedReport)->location,
                              (*selectedReport)->function.getSymNameAttr(),
                              reportOperands, ArrayAttr{}, ArrayAttr{});
      Value next = arith::SubIOp::create(
          bodyBuilder, getSemanticLocation(abort), remaining, one);
      cf::BranchOp::create(bodyBuilder, getSemanticLocation(abort), loop,
                           ValueRange{next});

      OpBuilder doneBuilder = OpBuilder::atBlockEnd(done);
      sim::SimReturnOp::create(doneBuilder, getSemanticLocation(abort),
                               ValueRange{});
    } else {
      OpBuilder currentBuilder = OpBuilder::atBlockEnd(currentBlock);
      sim::SimReturnOp::create(currentBuilder, getSemanticLocation(abort),
                               ValueRange{});
    }

    PersistentAbortPlan plan{
        FlatSymbolRefAttr::get(function.getContext(), dispatcher.getSymName()),
        captures};
    if (abort.getIsSynchronous())
      return std::optional<PersistentAbortPlan>{std::move(plan)};

    FailureOr<Value> current = lowerExpression(abortCondition);
    if (failed(current))
      return failure();
    FailureOr<Value> truth =
        truthValue(*current, getSemanticLocation(abortCondition));
    if (failed(truth))
      return failure();
    FailureOr<Value> observer = bindObserver(abortCondition);
    if (failed(observer))
      return emitError(getSemanticLocation(abortCondition))
                 << "asynchronous property abort expression has no observer; "
                    "its operands are not executable",
             failure();
    auto observerBinding = observer->getDefiningOp<sim::SimObserverBindOp>();
    if (!observerBinding)
      return emitError(getSemanticLocation(abortCondition))
                 << "asynchronous property abort expression has no observer "
                    "binding",
             failure();
    for (Value capture : observerBinding.getValues())
      if (isa<sim::RefType>(capture.getType()) &&
          !isStaticallyAllocatedOverrideTarget(capture))
        return emitError(getSemanticLocation(abortCondition))
                   << "asynchronous property abort cannot observe an "
                      "automatic variable",
               failure();
    appendObserverRequest(function, concurrentAbortObserverRequestAttrName,
                          observerBinding.getEvaluatorAttr());

    SmallVector<Value> actorCaptures{context};
    DenseMap<Value, unsigned> actorCaptureIndices;
    actorCaptureIndices.try_emplace(context, 0);
    auto appendActorCapture = [&](Value value) {
      auto [entry, inserted] =
          actorCaptureIndices.try_emplace(value, actorCaptures.size());
      if (inserted)
        actorCaptures.push_back(value);
      return entry->second;
    };
    SmallVector<unsigned> observerIndices;
    for (Value capture : observerBinding.getValues())
      observerIndices.push_back(appendActorCapture(capture));
    unsigned initialConditionIndex = appendActorCapture(*truth);
    SmallVector<unsigned> dispatcherIndices;
    for (Value operand : plan.operands)
      dispatcherIndices.push_back(appendActorCapture(operand));

    SmallVector<Type> actorInputs;
    SmallVector<DictionaryAttr> actorArgumentAttrs;
    for (auto [index, capture] : llvm::enumerate(actorCaptures)) {
      actorInputs.push_back(capture.getType());
      SmallVector<NamedAttribute> metadata;
      if (auto argument = dyn_cast<BlockArgument>(capture);
          argument && argument.getOwner() == &function.getBody().front()) {
        if (DictionaryAttr sourceAttrs =
                function.getArgAttrDict(argument.getArgNumber()))
          llvm::append_range(metadata, sourceAttrs);
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
      actorArgumentAttrs.push_back(builder.getDictionaryAttr(metadata));
    }

    std::string actorSymbol =
        (function.getSymName() + ".$concurrent_abort." + Twine(node)).str();
    std::string actorIdentity =
        (function.getSymName() + ".$concurrent_abort_observer." + Twine(node))
            .str();
    uint64_t actorCodeUnitID = stableCodeUnitID(actorIdentity);
    std::string actorHierarchy =
        (Twine(parentHierarchy) + ".$concurrent_abort." + Twine(node)).str();
    sim::SimCodeUnitDeclOp::create(
        outlineBuilder, getSemanticLocation(abort), actorCodeUnitID, scopeID,
        sim::EntryKind::Fork, outlineBuilder.getStringAttr(actorHierarchy),
        outlineBuilder.getStringAttr("concurrent assertion abort observer"),
        outlineBuilder.getUnitAttr());
    SmallVector<NamedAttribute> actorAttributes{
        outlineBuilder.getNamedAttr(bindingsAttrName,
                                    outlineBuilder.getArrayAttr({})),
        outlineBuilder.getNamedAttr(
            "code_unit_id", outlineBuilder.getI64IntegerAttr(actorCodeUnitID)),
        outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(
            "home_region",
            sim::EventRegionAttr::get(function.getContext(),
                                      sim::EventRegion::Reactive)),
        outlineBuilder.getNamedAttr(
            "domain", sim::ExecutionDomainAttr::get(
                          function.getContext(), sim::ExecutionDomain::Design)),
        outlineBuilder.getNamedAttr(
            sim::metadata::hierarchicalName,
            outlineBuilder.getStringAttr(actorHierarchy)),
    };
    sim::SimFuncOp actor = sim::SimFuncOp::create(
        outlineBuilder, getSemanticLocation(abort), actorSymbol,
        FunctionType::get(function.getContext(), actorInputs, TypeRange{}),
        sim::EntryKind::Fork, actorAttributes, actorArgumentAttrs);
    SymbolTable::setSymbolVisibility(actor, SymbolTable::Visibility::Private);
    actor->setAttr("obelisk_sim.concurrent_abort", builder.getUnitAttr());
    actor->setAttr("obelisk_sim.concurrent_abort_counted",
                   builder.getUnitAttr());
    actor->setAttr("obelisk_sim.detached_controls", builder.getUnitAttr());
    actor->setAttr("obelisk_sim.priority_signal_resume", builder.getUnitAttr());

    Block &actorEntry = actor.getBody().front();
    Block *waitAbort = new Block;
    Block *abortLiveAttempts = new Block;
    waitAbort->addArgument(builder.getI1Type(),
                           getSemanticLocation(abortCondition));
    actor.getBody().push_back(waitAbort);
    actor.getBody().push_back(abortLiveAttempts);
    OpBuilder actorEntryBuilder = OpBuilder::atBlockEnd(&actorEntry);
    cf::BranchOp::create(
        actorEntryBuilder, getSemanticLocation(abort), waitAbort,
        ValueRange{actorEntry.getArgument(initialConditionIndex)});

    OpBuilder waitBuilder = OpBuilder::atBlockEnd(waitAbort);
    SmallVector<Value> reboundValues;
    for (unsigned index : observerIndices)
      reboundValues.push_back(actorEntry.getArgument(index));
    auto reboundObserver = sim::SimObserverBindOp::create(
        waitBuilder, getSemanticLocation(abortCondition),
        observerBinding.getResult().getType(),
        observerBinding.getEvaluatorAttr(), reboundValues,
        observerBinding.getCaptureCountAttr());
    SmallVector<Value> observed{reboundObserver.getResult(),
                                waitAbort->getArgument(0)};
    auto abortWait = sim::SimSuspendObserveOp::create(
        waitBuilder, getSemanticLocation(abortCondition), observed, 0,
        ArrayRef<int32_t>{static_cast<int32_t>(sim::EdgeKind::Posedge)},
        ArrayRef<int32_t>{-1}, sim::ContinuationSiteAttr{},
        sim::EventRegionAttr::get(function.getContext(),
                                  sim::EventRegion::Reactive),
        abortLiveAttempts);
    abortWait->setAttr("obelisk_sim.concurrent_abort_level_true",
                       builder.getUnitAttr());

    OpBuilder abortBuilder = OpBuilder::atBlockEnd(abortLiveAttempts);
    SmallVector<Value> dispatcherOperands;
    for (unsigned index : dispatcherIndices)
      dispatcherOperands.push_back(actorEntry.getArgument(index));
    dispatcherOperands.push_back(arith::ConstantOp::create(
        abortBuilder, getSemanticLocation(abort), stateType,
        abortBuilder.getI64IntegerAttr(0)));
    sim::SimCallOp::create(abortBuilder, getSemanticLocation(abort),
                           TypeRange{}, plan.dispatcher, dispatcherOperands,
                           ArrayAttr{}, ArrayAttr{});
    Value currentTrue = arith::ConstantOp::create(
        abortBuilder, getSemanticLocation(abort), builder.getI1Type(),
        builder.getBoolAttr(true));
    cf::BranchOp::create(abortBuilder, getSemanticLocation(abort), waitAbort,
                         ValueRange{currentTrue});

    sim::SimSpawnOp::create(builder, getSemanticLocation(abort),
                            actor.getSymNameAttr(), actorCaptures, ArrayAttr{},
                            ArrayAttr{});
    return std::optional<PersistentAbortPlan>{std::move(plan)};
  };

  auto abortPersistentSample =
      [&](Block *wait,
          const std::optional<PersistentAbortPlan> &plan) -> LogicalResult {
    if (!plan)
      return success();
    FailureOr<Value> condition = [&]() -> FailureOr<Value> {
      if (abort.getIsSynchronous())
        return lowerExpression(abortCondition);
      bool savedSampleAssertionValues = sampleAssertionValues;
      Operation *savedSampledClock = activeSampledClock;
      sampleAssertionValues = false;
      activeSampledClock = nullptr;
      llvm::scope_exit restore([&] {
        sampleAssertionValues = savedSampleAssertionValues;
        activeSampledClock = savedSampledClock;
      });
      return lowerExpression(abortCondition);
    }();
    if (failed(condition))
      return failure();
    FailureOr<Value> aborts =
        truthValue(*condition, getSemanticLocation(abortCondition));
    if (failed(aborts))
      return failure();

    Block *aborted = addBlock();
    Block *evaluate = addBlock();
    cf::CondBranchOp::create(builder, getSemanticLocation(abort), *aborts,
                             aborted, ValueRange{}, evaluate, ValueRange{});
    setCurrent(aborted);
    SmallVector<Value> dispatcherOperands(plan->operands);
    dispatcherOperands.push_back(
        arith::ConstantOp::create(builder, getSemanticLocation(abort),
                                  stateType, builder.getI64IntegerAttr(1)));
    sim::SimCallOp::create(builder, getSemanticLocation(abort), TypeRange{},
                           plan->dispatcher, dispatcherOperands, ArrayAttr{},
                           ArrayAttr{});
    cf::BranchOp::create(builder, getSemanticLocation(abort), wait);
    setCurrent(evaluate);
    return success();
  };

  auto outlineEndOfSimulationReports = [&](ArrayRef<Value> liveStateStorages,
                                           size_t horizon) -> LogicalResult {
    if (!endStrengthSource)
      return success();
    bool operandStrong = endStrength ? endStrength.getStrength() ==
                                           semantic::SVAssertionStrength::Strong
                                     : cover;
    bool outerStrong = temporalNegation ? !operandStrong : operandStrong;
    semantic::SVAssertionStrength outerStrength =
        outerStrong ? semantic::SVAssertionStrength::Strong
                    : semantic::SVAssertionStrength::Weak;
    function->setAttr("obelisk_sim.strong_weak_monitor", builder.getUnitAttr());
    function->setAttr(
        "obelisk_sim.end_of_simulation_strength",
        builder.getStringAttr(
            semantic::stringifySVAssertionStrength(outerStrength)));
    if (temporalNegation)
      function->setAttr(
          "obelisk_sim.negated_operand_end_of_simulation_strength",
          builder.getStringAttr(semantic::stringifySVAssertionStrength(
              operandStrong ? semantic::SVAssertionStrength::Strong
                            : semantic::SVAssertionStrength::Weak)));

    // A one-cycle property has no incomplete attempt at end of simulation.
    // For longer traces each age bit denotes one distinct property attempt.
    // Branching alternatives may carry the same attempt in several state
    // words, so the final coordinator first unions those words and then emits
    // exactly one completion per live age, oldest-first.
    //
    // A weak operand completion is vacuous and remains invisible to cover;
    // negating a strong operand failure instead produces a nonvacuous hit.
    bool operandPassed = !operandStrong;
    bool completionPassed = temporalNegation ? !operandPassed : operandPassed;
    bool vacuousCompletion = !operandStrong;
    std::optional<ReportCallback> *selectedReport =
        completionPassed ? (cover && vacuousCompletion ? nullptr : &passReport)
                         : &failReport;
    if (horizon > 1 && !liveStateStorages.empty() && selectedReport &&
        *selectedReport) {
      auto design = function->getParentOfType<sim::SimDesignOp>();
      if (!design)
        return function.emitError(
                   "strong/weak finalization requires a simulation design"),
               failure();

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

      ReportCallback &report = **selectedReport;
      StringRef spelling =
          semantic::stringifySVAssertionStrength(outerStrength);
      std::string reportSymbol =
          (function.getSymName() + ".$concurrent_eos_report." + Twine(node) +
           "." + spelling)
              .str();
      std::string reportIdentity =
          (function.getSymName() + ".$concurrent_eos_report_identity." +
           Twine(node) + "." + spelling)
              .str();
      uint64_t reportCodeUnitID = stableCodeUnitID(reportIdentity);
      std::string reportHierarchy =
          (Twine(parentHierarchy) + ".$concurrent_eos_report." + Twine(node) +
           "." + spelling)
              .str();

      OpBuilder outlineBuilder(function);
      outlineBuilder.setInsertionPoint(function);
      sim::SimCodeUnitDeclOp::create(
          outlineBuilder, getSemanticLocation(endStrengthSource),
          reportCodeUnitID, scopeID, sim::EntryKind::Fork,
          outlineBuilder.getStringAttr(reportHierarchy),
          outlineBuilder.getStringAttr(
              "concurrent assertion end-of-simulation report"),
          outlineBuilder.getUnitAttr());
      Operation *clonedReport = report.function->clone();
      auto finalReport = cast<sim::SimFuncOp>(clonedReport);
      finalReport.setSymName(reportSymbol);
      finalReport->setAttr(
          "entry_kind",
          sim::EntryKindAttr::get(function.getContext(), sim::EntryKind::Fork));
      finalReport->setAttr(
          "home_region", sim::EventRegionAttr::get(function.getContext(),
                                                   sim::EventRegion::Reactive));
      finalReport->setAttr(
          "domain", sim::ExecutionDomainAttr::get(
                        function.getContext(), sim::ExecutionDomain::Design));
      finalReport->setAttr("code_unit_id",
                           outlineBuilder.getI64IntegerAttr(reportCodeUnitID));
      finalReport->setAttr(sim::metadata::hierarchicalName,
                           outlineBuilder.getStringAttr(reportHierarchy));
      finalReport->setAttr("obelisk_sim.concurrent_eos_report",
                           outlineBuilder.getUnitAttr());
      outlineBuilder.insert(clonedReport);

      Value context = function.getBody().front().getArgument(0);
      SmallVector<Value> captures{context};
      DenseMap<Value, unsigned> captureIndices;
      captureIndices.try_emplace(context, 0);
      SmallVector<unsigned> stateCaptureIndices;
      stateCaptureIndices.reserve(liveStateStorages.size());
      for (Value storage : liveStateStorages) {
        auto [entry, inserted] =
            captureIndices.try_emplace(storage, captures.size());
        if (inserted)
          captures.push_back(storage);
        stateCaptureIndices.push_back(entry->second);
      }
      SmallVector<unsigned> reportCaptureIndices;
      reportCaptureIndices.reserve(report.captures.size());
      for (Value capture : report.captures) {
        auto [entry, inserted] =
            captureIndices.try_emplace(capture, captures.size());
        if (inserted)
          captures.push_back(capture);
        reportCaptureIndices.push_back(entry->second);
      }
      std::optional<unsigned> disableEpochCaptureIndex;
      if (disableEpoch) {
        auto found = captureIndices.find(disableEpoch);
        if (found == captureIndices.end())
          return function.emitError(
                     "strong/weak report is missing its disable epoch"),
                 failure();
        disableEpochCaptureIndex = found->second;
      }

      SmallVector<Type> inputs;
      SmallVector<DictionaryAttr> argumentAttrs;
      for (auto [index, capture] : llvm::enumerate(captures)) {
        inputs.push_back(capture.getType());
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
                                        index == 0
                                            ? sim::CaptureKind::Context
                                            : sim::CaptureKind::Formal)));
        if (isa<sim::RefType>(capture.getType()) &&
            !isStaticallyAllocatedOverrideTarget(capture))
          metadata.push_back(
              builder.getNamedAttr("obelisk_sim.automatic_reference_capture",
                                   builder.getUnitAttr()));
        argumentAttrs.push_back(builder.getDictionaryAttr(metadata));
      }

      std::string coordinatorSymbol =
          (function.getSymName() + ".$concurrent_eos." + Twine(node) + "." +
           spelling)
              .str();
      std::string coordinatorIdentity =
          (function.getSymName() + ".$concurrent_eos_identity." + Twine(node) +
           "." + spelling)
              .str();
      uint64_t coordinatorCodeUnitID = stableCodeUnitID(coordinatorIdentity);
      std::string coordinatorHierarchy =
          (Twine(parentHierarchy) + ".$concurrent_eos." + Twine(node) + "." +
           spelling)
              .str();
      sim::SimCodeUnitDeclOp::create(
          outlineBuilder, getSemanticLocation(endStrengthSource),
          coordinatorCodeUnitID, scopeID, sim::EntryKind::Final,
          outlineBuilder.getStringAttr(coordinatorHierarchy),
          outlineBuilder.getStringAttr(
              "concurrent assertion end-of-simulation coordinator"),
          outlineBuilder.getUnitAttr());
      SmallVector<NamedAttribute> attributes{
          outlineBuilder.getNamedAttr(bindingsAttrName,
                                      outlineBuilder.getArrayAttr({})),
          outlineBuilder.getNamedAttr(
              "code_unit_id",
              outlineBuilder.getI64IntegerAttr(coordinatorCodeUnitID)),
          outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
          outlineBuilder.getNamedAttr(
              "home_region",
              sim::EventRegionAttr::get(function.getContext(),
                                        sim::EventRegion::Active)),
          outlineBuilder.getNamedAttr(
              "domain",
              sim::ExecutionDomainAttr::get(function.getContext(),
                                            sim::ExecutionDomain::Design)),
          outlineBuilder.getNamedAttr(
              sim::metadata::hierarchicalName,
              outlineBuilder.getStringAttr(coordinatorHierarchy)),
      };
      sim::SimFuncOp coordinator = sim::SimFuncOp::create(
          outlineBuilder, getSemanticLocation(endStrengthSource),
          coordinatorSymbol,
          FunctionType::get(function.getContext(), inputs, TypeRange{}),
          sim::EntryKind::Final, attributes, argumentAttrs);
      SymbolTable::setSymbolVisibility(coordinator,
                                       SymbolTable::Visibility::Private);
      coordinator->setAttr("obelisk_sim.concurrent_eos_coordinator",
                           builder.getUnitAttr());
      coordinator->setAttr("obelisk_sim.detached_controls",
                           builder.getUnitAttr());

      Block *current = &coordinator.getBody().front();
      OpBuilder coordinatorBuilder = OpBuilder::atBlockEnd(current);
      Value finalZero = arith::ConstantOp::create(
          coordinatorBuilder, getSemanticLocation(endStrengthSource), stateType,
          coordinatorBuilder.getI64IntegerAttr(0));
      Value live = finalZero;
      for (unsigned index : stateCaptureIndices) {
        Value alternative = sim::SimRefLoadOp::create(
            coordinatorBuilder, getSemanticLocation(endStrengthSource),
            stateType, current->getArgument(index));
        live = arith::OrIOp::create(coordinatorBuilder,
                                    getSemanticLocation(endStrengthSource),
                                    live, alternative);
      }
      for (uint64_t age = horizon; age-- > 1;) {
        Block *reportBlock = new Block;
        Block *continuation = new Block;
        coordinator.getBody().push_back(reportBlock);
        coordinator.getBody().push_back(continuation);
        coordinatorBuilder.setInsertionPointToEnd(current);
        Value mask = arith::ConstantOp::create(
            coordinatorBuilder, getSemanticLocation(endStrengthSource),
            stateType,
            coordinatorBuilder.getI64IntegerAttr(uint64_t{1} << age));
        Value present = arith::AndIOp::create(
            coordinatorBuilder, getSemanticLocation(endStrengthSource), live,
            mask);
        Value active = arith::CmpIOp::create(
            coordinatorBuilder, getSemanticLocation(endStrengthSource),
            arith::CmpIPredicate::ne, present, finalZero);
        cf::CondBranchOp::create(
            coordinatorBuilder, getSemanticLocation(endStrengthSource), active,
            reportBlock, ValueRange{}, continuation, ValueRange{});

        OpBuilder reportBuilder = OpBuilder::atBlockEnd(reportBlock);
        SmallVector<Value> reportOperands;
        reportOperands.reserve(reportCaptureIndices.size() +
                               static_cast<size_t>(disableEpoch != nullptr));
        for (unsigned index : reportCaptureIndices)
          reportOperands.push_back(
              coordinator.getBody().front().getArgument(index));
        if (disableEpochCaptureIndex)
          reportOperands.push_back(sim::SimRefLoadOp::create(
              reportBuilder, report.location, stateType,
              coordinator.getBody().front().getArgument(
                  *disableEpochCaptureIndex)));
        sim::SimSpawnOp::create(reportBuilder, report.location,
                                finalReport.getSymNameAttr(), reportOperands,
                                ArrayAttr{}, ArrayAttr{});
        cf::BranchOp::create(reportBuilder,
                             getSemanticLocation(endStrengthSource),
                             continuation);
        current = continuation;
      }
      OpBuilder returnBuilder = OpBuilder::atBlockEnd(current);
      sim::SimReturnOp::create(
          returnBuilder, getSemanticLocation(endStrengthSource), ValueRange{});

      sim::SimSpawnOp::create(builder, getSemanticLocation(endStrengthSource),
                              coordinator.getSymNameAttr(), captures,
                              ArrayAttr{}, ArrayAttr{});
    }
    return success();
  };

  if (!branchingSequence && failed(outlineEndOfSimulationReports(
                                {stateStorage}, sequence.ages.size())))
    return failure();

  if (abort && !abort.getIsSynchronous() && !persistentStateOwner) {
    // Asynchronous aborts need a persistent observer in addition to the
    // clocked monitor. The observer completes only attempts represented by
    // live state bits; the clocked path below handles the attempt beginning on
    // a clock where the abort condition is already true.
    FailureOr<Value> current = lowerExpression(abortCondition);
    if (failed(current))
      return failure();
    FailureOr<Value> truth =
        truthValue(*current, getSemanticLocation(abortCondition));
    if (failed(truth))
      return failure();
    FailureOr<Value> observer = bindObserver(abortCondition);
    if (failed(observer))
      return emitError(getSemanticLocation(abortCondition))
                 << "asynchronous property abort expression has no observer; "
                    "its operands are not executable",
             failure();
    auto observerBinding = observer->getDefiningOp<sim::SimObserverBindOp>();
    if (!observerBinding)
      return emitError(getSemanticLocation(abortCondition))
                 << "asynchronous property abort expression has no observer "
                    "binding",
             failure();
    for (Value capture : observerBinding.getValues())
      if (isa<sim::RefType>(capture.getType()) &&
          !isStaticallyAllocatedOverrideTarget(capture))
        return emitError(getSemanticLocation(abortCondition))
                   << "asynchronous property abort cannot observe an "
                      "automatic variable",
               failure();

    auto design = function->getParentOfType<sim::SimDesignOp>();
    appendObserverRequest(function, concurrentAbortObserverRequestAttrName,
                          observerBinding.getEvaluatorAttr());
    if (!design || !stateStorage)
      return function.emitError(
                 "asynchronous property abort outlining requires a design "
                 "and monitor state"),
             failure();

    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    std::string symbol =
        (function.getSymName() + ".$concurrent_abort." + Twine(node)).str();
    std::string identity =
        (function.getSymName() + ".$concurrent_abort_observer." + Twine(node))
            .str();
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
        (Twine(parentHierarchy) + ".$concurrent_abort." + Twine(node)).str();

    Value context = function.getBody().front().getArgument(0);
    SmallVector<Value> captures{context};
    unsigned observerCaptureBegin = captures.size();
    llvm::append_range(captures, observerBinding.getValues());
    unsigned initialConditionIndex = captures.size();
    captures.push_back(*truth);
    unsigned stateStorageIndex = captures.size();
    captures.push_back(stateStorage);
    SmallVector<unsigned> passCaptureIndices;
    SmallVector<unsigned> failCaptureIndices;
    auto appendReportCaptures = [&](const std::optional<ReportCallback> &report,
                                    SmallVector<unsigned> &indices) {
      if (!report)
        return;
      for (Value capture : report->captures) {
        if (isa<sim::ContextType>(capture.getType())) {
          indices.push_back(0);
          continue;
        }
        indices.push_back(captures.size());
        captures.push_back(capture);
      }
    };
    appendReportCaptures(passReport, passCaptureIndices);
    appendReportCaptures(failReport, failCaptureIndices);

    SmallVector<Type> inputs;
    SmallVector<DictionaryAttr> argumentAttrs;
    for (Value capture : captures) {
      inputs.push_back(capture.getType());
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
                                      sim::CaptureKind::Formal)));
      if (isa<sim::RefType>(capture.getType()) &&
          !isStaticallyAllocatedOverrideTarget(capture))
        metadata.push_back(builder.getNamedAttr(
            "obelisk_sim.automatic_reference_capture", builder.getUnitAttr()));
      argumentAttrs.push_back(builder.getDictionaryAttr(metadata));
    }

    OpBuilder outlineBuilder(function);
    outlineBuilder.setInsertionPoint(function);
    sim::SimCodeUnitDeclOp::create(
        outlineBuilder, getSemanticLocation(abort), codeUnitID, scopeID,
        sim::EntryKind::Fork, outlineBuilder.getStringAttr(hierarchy),
        outlineBuilder.getStringAttr("concurrent assertion abort observer"),
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
    sim::SimFuncOp actor = sim::SimFuncOp::create(
        outlineBuilder, getSemanticLocation(abort), symbol,
        FunctionType::get(function.getContext(), inputs, TypeRange{}),
        sim::EntryKind::Fork, attributes, argumentAttrs);
    SymbolTable::setSymbolVisibility(actor, SymbolTable::Visibility::Private);
    actor->setAttr("obelisk_sim.concurrent_abort", builder.getUnitAttr());
    actor->setAttr("obelisk_sim.detached_controls", builder.getUnitAttr());
    actor->setAttr("obelisk_sim.priority_signal_resume", builder.getUnitAttr());

    Block &entry = actor.getBody().front();
    Block *waitAbort = new Block;
    Block *abortLiveAttempts = new Block;
    waitAbort->addArgument(builder.getI1Type(),
                           getSemanticLocation(abortCondition));
    actor.getBody().push_back(waitAbort);
    actor.getBody().push_back(abortLiveAttempts);
    OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
    cf::BranchOp::create(entryBuilder, getSemanticLocation(abort), waitAbort,
                         ValueRange{entry.getArgument(initialConditionIndex)});

    OpBuilder waitBuilder = OpBuilder::atBlockEnd(waitAbort);
    SmallVector<Value> reboundValues;
    for (unsigned index = observerCaptureBegin; index != initialConditionIndex;
         ++index)
      reboundValues.push_back(entry.getArgument(index));
    auto reboundObserver = sim::SimObserverBindOp::create(
        waitBuilder, getSemanticLocation(abortCondition),
        observerBinding.getResult().getType(),
        observerBinding.getEvaluatorAttr(), reboundValues,
        observerBinding.getCaptureCountAttr());
    SmallVector<Value> observed{reboundObserver.getResult(),
                                waitAbort->getArgument(0)};
    auto abortWait = sim::SimSuspendObserveOp::create(
        waitBuilder, getSemanticLocation(abortCondition), observed, 0,
        ArrayRef<int32_t>{static_cast<int32_t>(sim::EdgeKind::Posedge)},
        ArrayRef<int32_t>{-1}, sim::ContinuationSiteAttr{},
        sim::EventRegionAttr::get(function.getContext(),
                                  sim::EventRegion::Reactive),
        abortLiveAttempts);
    abortWait->setAttr("obelisk_sim.concurrent_abort_level_true",
                       builder.getUnitAttr());

    bool accepted =
        abort.getAction() == semantic::SVAssertionAbortAction::Accept;
    std::optional<ReportCallback> *selectedReport =
        accepted ? &passReport : &failReport;
    ArrayRef<unsigned> selectedIndices =
        accepted ? ArrayRef<unsigned>(passCaptureIndices)
                 : ArrayRef<unsigned>(failCaptureIndices);
    Block *currentBlock = abortLiveAttempts;
    Value liveState;
    {
      OpBuilder abortBuilder = OpBuilder::atBlockEnd(currentBlock);
      liveState = sim::SimRefLoadOp::create(
          abortBuilder, getSemanticLocation(abort), stateType,
          entry.getArgument(stateStorageIndex));
    }
    // Oldest attempts complete first. Accepted aborts are vacuous and do not
    // create cover-property hits.
    if (*selectedReport && !(accepted && cover)) {
      for (uint64_t age = sequence.ages.size(); age-- > 1;) {
        Block *reportBlock = new Block;
        Block *continuation = new Block;
        actor.getBody().push_back(reportBlock);
        actor.getBody().push_back(continuation);
        OpBuilder currentBuilder = OpBuilder::atBlockEnd(currentBlock);
        Value mask = arith::ConstantOp::create(
            currentBuilder, getSemanticLocation(abort), stateType,
            currentBuilder.getI64IntegerAttr(uint64_t{1} << age));
        Value present = arith::AndIOp::create(
            currentBuilder, getSemanticLocation(abort), liveState, mask);
        Value active = arith::CmpIOp::create(
            currentBuilder, getSemanticLocation(abort),
            arith::CmpIPredicate::ne, present,
            arith::ConstantOp::create(currentBuilder,
                                      getSemanticLocation(abort), stateType,
                                      currentBuilder.getI64IntegerAttr(0)));
        cf::CondBranchOp::create(currentBuilder, getSemanticLocation(abort),
                                 active, reportBlock, ValueRange{},
                                 continuation, ValueRange{});
        OpBuilder reportBuilder = OpBuilder::atBlockEnd(reportBlock);
        SmallVector<Value> reportCaptures;
        for (unsigned index : selectedIndices)
          reportCaptures.push_back(entry.getArgument(index));
        sim::SimSpawnOp::create(reportBuilder, (*selectedReport)->location,
                                (*selectedReport)->function.getSymNameAttr(),
                                reportCaptures, ArrayAttr{}, ArrayAttr{});
        cf::BranchOp::create(reportBuilder, getSemanticLocation(abort),
                             continuation);
        currentBlock = continuation;
      }
    }
    OpBuilder finishBuilder = OpBuilder::atBlockEnd(currentBlock);
    Value abortZero = arith::ConstantOp::create(
        finishBuilder, getSemanticLocation(abort), stateType,
        finishBuilder.getI64IntegerAttr(0));
    sim::SimRefStoreOp::create(finishBuilder, getSemanticLocation(abort),
                               abortZero, entry.getArgument(stateStorageIndex));
    Value currentTrue = arith::ConstantOp::create(
        finishBuilder, getSemanticLocation(abort), builder.getI1Type(),
        builder.getBoolAttr(true));
    cf::BranchOp::create(finishBuilder, getSemanticLocation(abort), waitAbort,
                         ValueRange{currentTrue});

    sim::SimSpawnOp::create(builder, getSemanticLocation(abort),
                            actor.getSymNameAttr(), captures, ArrayAttr{},
                            ArrayAttr{});
  }

  if (hasPersistentDelay) {
    function->setAttr("obelisk_sim.persistent_delay_monitor",
                      builder.getUnitAttr());
    function->setAttr("obelisk_sim.persistent_delay_minimum",
                      builder.getI64IntegerAttr(persistentDelay.minimum));
    function->setAttr(
        "obelisk_sim.persistent_delay_prefix_horizon",
        builder.getI64IntegerAttr(persistentDelay.prefix.ages.size()));
    function->setAttr("obelisk_sim.persistent_delay_aggregate_tokens",
                      builder.getUnitAttr());
    function->setAttr("obelisk_sim.sva_transition_normal_form",
                      builder.getStringAttr("canonical-minimal"));
    if (implication) {
      function->setAttr("obelisk_sim.persistent_delay_implication",
                        builder.getUnitAttr());
      if (nonoverlapped)
        function->setAttr("obelisk_sim.persistent_delay_nonoverlapped",
                          builder.getUnitAttr());
    }
    if (coverSequence)
      function->setAttr("obelisk_sim.persistent_delay_all_matches",
                        builder.getUnitAttr());

    Value prefixStateStorage;
    if (persistentDelay.prefix.ages.size() > 1)
      prefixStateStorage = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero);
    Value warmupStorage;
    if (persistentDelay.minimum != 0)
      warmupStorage = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero);
    Value delayedActivationStorage;
    if (implication && nonoverlapped)
      delayedActivationStorage = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero);
    Value eligibleStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);

    SmallVector<Value> delayStateStorages;
    if (prefixStateStorage)
      delayStateStorages.push_back(prefixStateStorage);
    if (warmupStorage)
      delayStateStorages.push_back(warmupStorage);
    if (delayedActivationStorage)
      delayStateStorages.push_back(delayedActivationStorage);
    delayStateStorages.push_back(eligibleStorage);
    if (failed(outlineDisableObserver(delayStateStorages)))
      return failure();

    SmallVector<Value> abortCounts{eligibleStorage};
    SmallVector<Value> abortBitsets;
    if (prefixStateStorage)
      abortBitsets.push_back(prefixStateStorage);
    if (warmupStorage)
      abortBitsets.push_back(warmupStorage);
    if (delayedActivationStorage)
      abortBitsets.push_back(delayedActivationStorage);
    FailureOr<std::optional<PersistentAbortPlan>> persistentAbort =
        preparePersistentAbort(abortCounts, abortBitsets);
    if (failed(persistentAbort))
      return failure();

    bool weakCompletion = endStrength ? endStrength.getStrength() ==
                                            semantic::SVAssertionStrength::Weak
                                      : assertion;
    SmallVector<Value> endCounts;
    SmallVector<Value> endBitsets;
    // A sequence used by assert/assume is weak unless explicitly qualified;
    // other directives are strong. A weak finite-end completion is vacuous,
    // so it never creates a cover hit.
    if (!weakCompletion || !cover) {
      endCounts.push_back(eligibleStorage);
      if (prefixStateStorage)
        endBitsets.push_back(prefixStateStorage);
      if (warmupStorage)
        endBitsets.push_back(warmupStorage);
      if (delayedActivationStorage)
        endBitsets.push_back(delayedActivationStorage);
    }
    StringRef completionTag = weakCompletion ? "delay_weak" : "delay_strong";
    Operation *completionSource =
        endStrength ? endStrength.getOperation() : property;
    if (failed(outlineCountedEndOfSimulation(endCounts, endBitsets,
                                             weakCompletion, completionTag,
                                             completionSource)))
      return failure();

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

    if (failed(cancelDisabledSample(wait, delayStateStorages)))
      return failure();

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    if (failed(abortPersistentSample(wait, *persistentAbort)))
      return failure();

    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    Value one = arith::ConstantOp::create(builder, location, stateType,
                                          builder.getI64IntegerAttr(1));
    DenseMap<Operation *, Value> predicateCache;
    DenseMap<Attribute, Value> symbolicPredicateCache;
    auto evaluateAge = [&](const FixedSequenceAge &age) -> FailureOr<Value> {
      Value result = trueValue;
      auto evaluatePredicate = [&](Operation *predicate) -> FailureOr<Value> {
        if (auto found = predicateCache.find(predicate);
            found != predicateCache.end())
          return found->second;
        Attribute referencedSymbol;
        if (isa<semantic::SVNamedValueExpressionOp,
                semantic::SVHierarchicalValueExpressionOp>(predicate))
          referencedSymbol = predicate->getAttr("referenced_symbol");
        if (referencedSymbol) {
          if (auto found = symbolicPredicateCache.find(referencedSymbol);
              found != symbolicPredicateCache.end()) {
            predicateCache[predicate] = found->second;
            return found->second;
          }
        }
        FailureOr<Value> value = lowerExpression(predicate);
        if (failed(value))
          return failure();
        FailureOr<Value> truth =
            truthValue(*value, getSemanticLocation(predicate));
        if (failed(truth))
          return failure();
        predicateCache[predicate] = *truth;
        if (referencedSymbol)
          symbolicPredicateCache[referencedSymbol] = *truth;
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
    Value completedPrefix = zero;
    Value failedPrefix = zero;
    Value prefixActive = trueValue;
    Value antecedentResultCount = zero;
    if (implication) {
      FailureOr<Value> antecedent =
          evaluateAge(antecedentSequence.ages.front());
      if (failed(antecedent))
        return failure();
      Value antecedentFails =
          arith::XOrIOp::create(builder, location, *antecedent, trueValue);
      antecedentResultCount = selectCount(antecedentFails, one);
      Value triggered = selectCount(*antecedent, one);
      if (delayedActivationStorage) {
        Value delayedActivation = sim::SimRefLoadOp::create(
            builder, location, stateType, delayedActivationStorage);
        prefixActive = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, delayedActivation,
            zero);
        sim::SimRefStoreOp::create(builder, location, triggered,
                                   delayedActivationStorage);
      } else {
        prefixActive = *antecedent;
      }
    }

    for (uint64_t age = 1; age < persistentDelay.prefix.ages.size(); ++age) {
      Value mask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1} << age));
      Value present =
          arith::AndIOp::create(builder, location, prefixState, mask);
      Value active = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, present, zero);
      FailureOr<Value> matches = evaluateAge(persistentDelay.prefix.ages[age]);
      if (failed(matches))
        return failure();
      Value advances =
          arith::AndIOp::create(builder, location, active, *matches);
      Value notMatches =
          arith::XOrIOp::create(builder, location, *matches, trueValue);
      Value fails =
          arith::AndIOp::create(builder, location, active, notMatches);
      addCount(failedPrefix, selectCount(fails, one));
      if (age + 1 == persistentDelay.prefix.ages.size()) {
        addCount(completedPrefix, selectCount(advances, one));
      } else {
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
        Value advanced = selectCount(advances, nextMask);
        nextPrefixState =
            arith::OrIOp::create(builder, location, nextPrefixState, advanced);
      }
    }

    FailureOr<Value> starts = evaluateAge(persistentDelay.prefix.ages.front());
    if (failed(starts))
      return failure();
    Value advances = *starts;
    Value failedStart =
        arith::XOrIOp::create(builder, location, *starts, trueValue);
    if (implication) {
      advances = arith::AndIOp::create(builder, location, advances,
                                       prefixActive);
      failedStart = arith::AndIOp::create(builder, location, failedStart,
                                          prefixActive);
    }
    addCount(failedPrefix, selectCount(failedStart, one));
    if (persistentDelay.prefix.ages.size() == 1) {
      addCount(completedPrefix, selectCount(advances, one));
    } else {
      Value firstMask = arith::ConstantOp::create(builder, location, stateType,
                                                  builder.getI64IntegerAttr(2));
      Value started = selectCount(advances, firstMask);
      nextPrefixState =
          arith::OrIOp::create(builder, location, nextPrefixState, started);
      sim::SimRefStoreOp::create(builder, location, nextPrefixState,
                                 prefixStateStorage);
    }

    Value matured = completedPrefix;
    if (warmupStorage) {
      Value warmup = sim::SimRefLoadOp::create(builder, location, stateType,
                                               warmupStorage);
      Value matureMask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1}
                                    << (persistentDelay.minimum - 1)));
      Value matureBit =
          arith::AndIOp::create(builder, location, warmup, matureMask);
      Value isMature = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, matureBit, zero);
      matured = selectCount(isMature, one);
      Value shifted = arith::ShLIOp::create(builder, location, warmup, one);
      uint64_t queueMask =
          (uint64_t{1} << persistentDelay.minimum) - uint64_t{1};
      Value mask = arith::ConstantOp::create(
          builder, location, stateType, builder.getI64IntegerAttr(queueMask));
      Value retained = arith::AndIOp::create(builder, location, shifted, mask);
      Value entered = selectCount(
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                completedPrefix, zero),
          one);
      Value nextWarmup =
          arith::OrIOp::create(builder, location, retained, entered);
      sim::SimRefStoreOp::create(builder, location, nextWarmup, warmupStorage);
    }

    Value eligible = sim::SimRefLoadOp::create(builder, location, stateType,
                                               eligibleStorage);
    Value eligibleNow =
        arith::AddIOp::create(builder, location, eligible, matured);
    FailureOr<Value> terminal = evaluateAge(persistentDelay.terminal);
    if (failed(terminal))
      return failure();
    Value successCount = selectCount(*terminal, eligibleNow);
    Value nextEligible =
        coverSequence ? eligibleNow
                      : arith::SelectOp::create(builder, location, *terminal,
                                                zero, eligibleNow)
                            .getResult();
    sim::SimRefStoreOp::create(builder, location, nextEligible,
                               eligibleStorage);
    scheduleCount(successCount, true);
    if (implication) {
      scheduleCount(failedPrefix, false);
      if (!cover)
        scheduleCount(antecedentResultCount, !followedBy);
    } else {
      scheduleCount(failedPrefix, false);
    }
    cf::BranchOp::create(builder, location, wait);
    return success();
  }

  if (hasPersistentUnary) {
    StringRef spelling =
        semantic::stringifySVAssertionUnaryOperator(persistentUnary.kind);
    function->setAttr("obelisk_sim.persistent_unary_monitor",
                      builder.getUnitAttr());
    function->setAttr("obelisk_sim.persistent_unary_kind",
                      builder.getStringAttr(spelling));
    function->setAttr("obelisk_sim.persistent_unary_minimum",
                      builder.getI64IntegerAttr(persistentUnary.minimum));
    function->setAttr("obelisk_sim.persistent_unary_aggregate_tokens",
                      builder.getUnitAttr());
    function->setAttr("obelisk_sim.sva_transition_normal_form",
                      builder.getStringAttr("canonical-minimal"));

    Value eligibleStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);
    Value immatureStorage;
    if (persistentUnary.minimum != 0)
      immatureStorage = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero);

    SmallVector<Value> unaryStateStorages{eligibleStorage};
    if (immatureStorage)
      unaryStateStorages.push_back(immatureStorage);
    if (failed(outlineDisableObserver(unaryStateStorages)))
      return failure();
    FailureOr<std::optional<PersistentAbortPlan>> persistentAbort =
        preparePersistentAbort(unaryStateStorages, /*bitsetStorages=*/{});
    if (failed(persistentAbort))
      return failure();

    SmallVector<Value> endCounts{eligibleStorage};
    // Strong eventually fails every outstanding attempt. Weak always succeeds
    // all outstanding attempts at finite end of simulation, but cover counts
    // only eligible attempts that actually evaluated its Boolean operand.
    if (immatureStorage && (persistentUnary.eventually || !cover))
      endCounts.push_back(immatureStorage);
    if (failed(outlineCountedEndOfSimulation(
            endCounts, /*bitsetStorages=*/{},
            /*passed=*/!persistentUnary.eventually, spelling, property)))
      return failure();

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

    if (failed(cancelDisabledSample(wait, unaryStateStorages)))
      return failure();

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    if (failed(abortPersistentSample(wait, *persistentAbort)))
      return failure();

    Value truth = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    auto evaluatePredicate = [&](Operation *predicate) -> FailureOr<Value> {
      FailureOr<Value> value = lowerExpression(predicate);
      if (failed(value))
        return failure();
      return truthValue(*value, getSemanticLocation(predicate));
    };
    for (Operation *predicate : persistentUnary.operand.predicates) {
      FailureOr<Value> value = evaluatePredicate(predicate);
      if (failed(value))
        return failure();
      truth = arith::AndIOp::create(builder, location, truth, *value);
    }
    for (Operation *predicate : persistentUnary.operand.negatedPredicates) {
      FailureOr<Value> value = evaluatePredicate(predicate);
      if (failed(value))
        return failure();
      Value negated = arith::XOrIOp::create(
          builder, location, *value,
          arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true)));
      truth = arith::AndIOp::create(builder, location, truth, negated);
    }

    Value one = arith::ConstantOp::create(builder, location, stateType,
                                          builder.getI64IntegerAttr(1));
    Value eligible = sim::SimRefLoadOp::create(builder, location, stateType,
                                               eligibleStorage);
    Value eligibleNow;
    if (!immatureStorage) {
      // The attempt beginning on this tick is immediately eligible.
      eligibleNow = arith::AddIOp::create(builder, location, eligible, one);
    } else {
      Value immature = sim::SimRefLoadOp::create(builder, location, stateType,
                                                 immatureStorage);
      Value minimum = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(persistentUnary.minimum));
      Value atCapacity = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, immature, minimum);
      Value matured =
          arith::SelectOp::create(builder, location, atCapacity, one, zero);
      Value incremented =
          arith::AddIOp::create(builder, location, immature, one);
      Value nextImmature = arith::SelectOp::create(
          builder, location, atCapacity, immature, incremented);
      sim::SimRefStoreOp::create(builder, location, nextImmature,
                                 immatureStorage);
      eligibleNow = arith::AddIOp::create(builder, location, eligible, matured);
    }

    Value terminalCount;
    Value nextEligible;
    if (persistentUnary.eventually) {
      terminalCount =
          arith::SelectOp::create(builder, location, truth, eligibleNow, zero);
      nextEligible =
          arith::SelectOp::create(builder, location, truth, zero, eligibleNow);
    } else {
      terminalCount =
          arith::SelectOp::create(builder, location, truth, zero, eligibleNow);
      nextEligible =
          arith::SelectOp::create(builder, location, truth, eligibleNow, zero);
    }
    scheduleCount(terminalCount, persistentUnary.eventually);
    sim::SimRefStoreOp::create(builder, location, nextEligible,
                               eligibleStorage);
    cf::BranchOp::create(builder, location, wait);
    return success();
  }

  if (hasPersistentUntil) {
    function->setAttr("obelisk_sim.persistent_until_monitor",
                      builder.getUnitAttr());
    function->setAttr(
        "obelisk_sim.persistent_until_kind",
        builder.getStringAttr(semantic::stringifySVAssertionBinaryOperator(
            persistentUntil.kind)));
    if (persistentUntil.inclusive)
      function->setAttr("obelisk_sim.persistent_until_inclusive",
                        builder.getUnitAttr());
    if (persistentUntil.strong)
      function->setAttr("obelisk_sim.persistent_until_strong",
                        builder.getUnitAttr());
    function->setAttr("obelisk_sim.persistent_until_aggregate_tokens",
                      builder.getUnitAttr());
    function->setAttr("obelisk_sim.sva_transition_normal_form",
                      builder.getStringAttr("canonical-minimal"));

    Value liveStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);
    SmallVector<Value> untilStateStorages{liveStorage};
    if (failed(outlineDisableObserver(untilStateStorages)))
      return failure();
    FailureOr<std::optional<PersistentAbortPlan>> persistentAbort =
        preparePersistentAbort(untilStateStorages, /*bitsetStorages=*/{});
    if (failed(persistentAbort))
      return failure();
    SmallVector<Value> endCounts;
    if (persistentUntil.strong || !cover)
      endCounts.push_back(liveStorage);
    if (failed(outlineCountedEndOfSimulation(
            endCounts, /*bitsetStorages=*/{},
            /*passed=*/!persistentUntil.strong,
            persistentUntil.strong ? "until_strong" : "until_weak", property)))
      return failure();
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

    if (failed(cancelDisabledSample(wait, untilStateStorages)))
      return failure();

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    if (failed(abortPersistentSample(wait, *persistentAbort)))
      return failure();

    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
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

    FailureOr<Value> left = evaluateAge(persistentUntil.left);
    FailureOr<Value> right = evaluateAge(persistentUntil.right);
    if (failed(left) || failed(right))
      return failure();
    Value notLeft = negate(*left);
    Value notRight = negate(*right);
    Value succeeds;
    Value fails;
    Value continues;
    if (persistentUntil.inclusive) {
      // until_with requires the left property through and including the clock
      // on which the right property succeeds.
      succeeds = arith::AndIOp::create(builder, location, *left, *right);
      fails = notLeft;
      continues = arith::AndIOp::create(builder, location, *left, notRight);
    } else {
      // Plain until does not require the left property on the terminal clock.
      succeeds = *right;
      fails = arith::AndIOp::create(builder, location, notRight, notLeft);
      continues = arith::AndIOp::create(builder, location, notRight, *left);
    }

    Value one = arith::ConstantOp::create(builder, location, stateType,
                                          builder.getI64IntegerAttr(1));
    Value live =
        sim::SimRefLoadOp::create(builder, location, stateType, liveStorage);
    Value attempts = arith::AddIOp::create(builder, location, live, one);
    Value nextLive =
        arith::SelectOp::create(builder, location, continues, attempts, zero);
    sim::SimRefStoreOp::create(builder, location, nextLive, liveStorage);
    Value successCount =
        arith::SelectOp::create(builder, location, succeeds, attempts, zero);
    Value failureCount =
        arith::SelectOp::create(builder, location, fails, attempts, zero);
    scheduleCount(successCount, true);
    scheduleCount(failureCount, false);
    cf::BranchOp::create(builder, location, wait);
    return success();
  }

  if (hasPersistentRepetition) {
    function->setAttr("obelisk_sim.persistent_repetition_monitor",
                      builder.getUnitAttr());
    function->setAttr(
        "obelisk_sim.persistent_repetition_kind",
        builder.getStringAttr(semantic::stringifySVSequenceRepetitionKind(
            persistentRepetition.kind)));
    function->setAttr("obelisk_sim.persistent_repetition_min",
                      builder.getI64IntegerAttr(persistentRepetition.minimum));
    if (persistentRepetition.unbounded)
      function->setAttr("obelisk_sim.persistent_repetition_unbounded",
                        builder.getUnitAttr());
    else
      function->setAttr(
          "obelisk_sim.persistent_repetition_max",
          builder.getI64IntegerAttr(persistentRepetition.maximum));
    function->setAttr("obelisk_sim.persistent_repetition_dfa",
                      builder.getUnitAttr());
    function->setAttr("obelisk_sim.sva_transition_normal_form",
                      builder.getStringAttr("canonical-minimal"));

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
    if (persistentRepetition.unbounded && persistentRepetition.hasTerminal) {
      switch (persistentRepetition.kind) {
      case semantic::SVSequenceRepetitionKind::Consecutive:
        for (uint64_t count = 0; count < persistentRepetition.minimum; ++count)
          addTokenState(count, false);
        addTokenState(persistentRepetition.minimum, true);
        break;
      case semantic::SVSequenceRepetitionKind::GoTo:
        for (uint64_t count = 0; count < persistentRepetition.minimum; ++count)
          addTokenState(count, false);
        addTokenState(persistentRepetition.minimum, false);
        addTokenState(persistentRepetition.minimum, true);
        break;
      case semantic::SVSequenceRepetitionKind::Nonconsecutive:
        for (uint64_t count = 0; count <= persistentRepetition.minimum; ++count)
          addTokenState(count, false);
        break;
      }
    } else if (!persistentRepetition.hasTerminal) {
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

    SmallVector<Value> repetitionStateStorages;
    for (const TokenState &state : tokenStates)
      repetitionStateStorages.push_back(state.storage);
    if (prefixStateStorage)
      repetitionStateStorages.push_back(prefixStateStorage);
    if (failed(outlineDisableObserver(repetitionStateStorages)))
      return failure();

    SmallVector<Value> abortCounts;
    for (const TokenState &state : tokenStates)
      abortCounts.push_back(state.storage);
    SmallVector<Value> abortBitsets;
    if (prefixStateStorage)
      abortBitsets.push_back(prefixStateStorage);
    FailureOr<std::optional<PersistentAbortPlan>> persistentAbort =
        preparePersistentAbort(abortCounts, abortBitsets);
    if (failed(persistentAbort))
      return failure();

    bool weakCompletion = endStrength ? endStrength.getStrength() ==
                                            semantic::SVAssertionStrength::Weak
                                      : assertion;
    SmallVector<Value> endCounts;
    SmallVector<Value> endBitsets;
    if (!weakCompletion || !cover) {
      for (const TokenState &state : tokenStates)
        endCounts.push_back(state.storage);
      if (prefixStateStorage)
        endBitsets.push_back(prefixStateStorage);
    }
    StringRef completionTag =
        weakCompletion ? "repetition_weak" : "repetition_strong";
    Operation *completionSource =
        endStrength ? endStrength.getOperation() : property;
    if (failed(outlineCountedEndOfSimulation(endCounts, endBitsets,
                                             weakCompletion, completionTag,
                                             completionSource)))
      return failure();

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

    if (failed(cancelDisabledSample(wait, repetitionStateStorages)))
      return failure();

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    if (failed(abortPersistentSample(wait, *persistentAbort)))
      return failure();

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
        if (persistentRepetition.kind ==
            semantic::SVSequenceRepetitionKind::Consecutive)
          fail(amount, notRepeated);
        else
          route(index, amount, notRepeated);
        continue;
      }

      if (persistentRepetition.unbounded) {
        switch (persistentRepetition.kind) {
        case semantic::SVSequenceRepetitionKind::Consecutive: {
          if (!state.pending) {
            uint64_t nextCount = state.occurrences + 1;
            route(findTokenState(nextCount,
                                 nextCount >= persistentRepetition.minimum),
                  amount, *repeated);
            fail(amount, notRepeated);
            break;
          }
          succeed(amount, terminal);
          Value continues =
              arith::AndIOp::create(builder, location, notTerminal, *repeated);
          Value stops = arith::AndIOp::create(builder, location, notTerminal,
                                              notRepeated);
          route(index, amount, continues);
          fail(amount, stops);
          break;
        }
        case semantic::SVSequenceRepetitionKind::GoTo: {
          if (state.pending) {
            succeed(amount, terminal);
            Value consumes = arith::AndIOp::create(builder, location,
                                                   notTerminal, *repeated);
            Value waits = arith::AndIOp::create(builder, location, notTerminal,
                                                notRepeated);
            route(index, amount, consumes);
            route(findTokenState(persistentRepetition.minimum, false), amount,
                  waits);
            break;
          }
          uint64_t nextCount =
              std::min(state.occurrences + 1, persistentRepetition.minimum);
          route(findTokenState(nextCount,
                               nextCount >= persistentRepetition.minimum),
                amount, *repeated);
          route(index, amount, notRepeated);
          break;
        }
        case semantic::SVSequenceRepetitionKind::Nonconsecutive: {
          if (state.occurrences >= persistentRepetition.minimum) {
            succeed(amount, terminal);
            route(index, amount, notTerminal);
            break;
          }
          route(findTokenState(state.occurrences + 1, false), amount,
                *repeated);
          route(index, amount, notRepeated);
          break;
        }
        }
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

  if (branchingAntecedent) {
    function->setAttr("obelisk_sim.branching_antecedent_monitor",
                      builder.getUnitAttr());
    function->setAttr("obelisk_sim.branching_antecedent_alternatives",
                      builder.getI64IntegerAttr(antecedentAlternatives.size()));
    function->setAttr("obelisk_sim.branching_antecedent_match_channels",
                      builder.getI64IntegerAttr(antecedentAlternatives.size()));
    if (combinedBooleanBranching) {
      function->setAttr("obelisk_sim.branching_consequent_alternatives",
                        builder.getI64IntegerAttr(
                            consequentAlternatives.size()));
      function->setAttr(
          "obelisk_sim.combined_boolean_branching_pairs",
          builder.getI64IntegerAttr(antecedentAlternatives.size() *
                                    consequentAlternativeAdmissionCount));
      function->setAttr("obelisk_sim.combined_boolean_branching_monitor",
                        builder.getUnitAttr());
    }

    SmallVector<Value> alternativeStates;
    alternativeStates.reserve(antecedentAlternatives.size());
    for (const FixedSequence &alternative : antecedentAlternatives) {
      if (alternative.ages.size() == 1) {
        alternativeStates.push_back(Value{});
        continue;
      }
      alternativeStates.push_back(sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero));
    }
    Value matchedState;
    if (antecedentHorizon > 1)
      matchedState = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero);

    bool consequentNeedsState = nonoverlapped || sequence.ages.size() > 1;
    SmallVector<Value> consequentStates(antecedentAlternatives.size());
    if (consequentNeedsState)
      for (Value &storage : consequentStates)
        storage = sim::SimRefAllocOp::create(
            builder, location,
            sim::RefType::get(function.getContext(), stateType), zero);

    SmallVector<Value> branchingStateStorages;
    for (Value storage : alternativeStates)
      if (storage)
        branchingStateStorages.push_back(storage);
    if (matchedState)
      branchingStateStorages.push_back(matchedState);
    if (consequentNeedsState)
      llvm::append_range(branchingStateStorages, consequentStates);
    if (failed(outlineDisableObserver(branchingStateStorages)))
      return failure();

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

    if (failed(cancelDisabledSample(wait, branchingStateStorages)))
      return failure();

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
    llvm::DenseMap<Operation *, Value> predicateCache;
    llvm::DenseMap<std::pair<Operation *, Operation *>, Value> caseGuardCache;
    llvm::DenseMap<Operation *, Value> caseSelectorCache;
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
            FailureOr<Value> lowered = lowerExpression(guard.selector);
            if (failed(lowered))
              return failure();
            selector = *lowered;
            caseSelectorCache[guard.selector] = selector;
          }
          FailureOr<Value> comparison =
              lowerCaseLabel(selector, selector.getType(), guard.selector,
                             guard.label, semantic::SVCaseCondition::Normal);
          if (failed(comparison) || !comparison->getType().isInteger(1))
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
    std::optional<Value> combinedConsequentTruth;
    auto evaluateConsequentAge = [&](uint64_t age) -> FailureOr<Value> {
      if (!combinedBooleanBranching)
        return evaluateAge(sequence.ages[age]);
      assert(age == 0 && "combined Boolean consequent has one age");
      if (combinedConsequentTruth)
        return *combinedConsequentTruth;
      Value result = falseValue;
      for (const FixedSequence &alternative : consequentAlternatives) {
        FailureOr<Value> matched = evaluateAge(alternative.ages.front());
        if (failed(matched))
          return failure();
        result = arith::OrIOp::create(builder, location, result, *matched);
      }
      combinedConsequentTruth = result;
      return result;
    };
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

    SmallVector<Value> activeAny(antecedentHorizon, falseValue);
    SmallVector<Value> successAny(antecedentHorizon, falseValue);
    SmallVector<SmallVector<Value>> survives(antecedentAlternatives.size());
    SmallVector<Value> starts(antecedentAlternatives.size(), falseValue);
    SmallVector<Value> terminalMatches(antecedentAlternatives.size(),
                                       falseValue);
    SmallVector<Value> nextAlternativeStates(antecedentAlternatives.size(),
                                             zero);
    for (auto [alternativeIndex, alternative] :
         llvm::enumerate(antecedentAlternatives)) {
      survives[alternativeIndex].resize(alternative.ages.size(), falseValue);
      FailureOr<Value> start = evaluateAge(alternative.ages.front());
      if (failed(start))
        return failure();
      starts[alternativeIndex] = *start;
      if (alternative.ages.size() == 1) {
        terminalMatches[alternativeIndex] = *start;
        successAny[0] =
            arith::OrIOp::create(builder, location, successAny[0], *start);
      }

      if (alternative.ages.size() == 1)
        continue;
      Value state = sim::SimRefLoadOp::create(
          builder, location, stateType, alternativeStates[alternativeIndex]);
      for (uint64_t age = 1; age < alternative.ages.size(); ++age) {
        Value mask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << age));
        Value present = arith::AndIOp::create(builder, location, state, mask);
        Value active = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, present, zero);
        activeAny[age] =
            arith::OrIOp::create(builder, location, activeAny[age], active);
        FailureOr<Value> matches = evaluateAge(alternative.ages[age]);
        if (failed(matches))
          return failure();
        Value advances =
            arith::AndIOp::create(builder, location, active, *matches);
        survives[alternativeIndex][age] = advances;
        if (age + 1 == alternative.ages.size())
          successAny[age] = arith::OrIOp::create(builder, location,
                                                 successAny[age], advances);
      }
      terminalMatches[alternativeIndex] = survives[alternativeIndex].back();
    }

    struct FirstMatchGroupState {
      SmallVector<FirstMatchGroupComponent, 4> path;
      SmallVector<Value> success;
    };
    SmallVector<FirstMatchGroupState> firstMatchGroups;
    auto getFirstMatchSuccess =
        [&](ArrayRef<FirstMatchGroupComponent> path) -> SmallVector<Value> & {
      for (FirstMatchGroupState &group : firstMatchGroups)
        if (ArrayRef<FirstMatchGroupComponent>(group.path) == path)
          return group.success;
      FirstMatchGroupState group;
      llvm::append_range(group.path, path);
      group.success.assign(antecedentHorizon, falseValue);
      firstMatchGroups.push_back(std::move(group));
      return firstMatchGroups.back().success;
    };
    for (auto [alternativeIndex, alternative] :
         llvm::enumerate(antecedentAlternatives))
      for (const FirstMatchBoundary &boundary :
           alternative.firstMatchBoundaries) {
        SmallVector<Value> &success = getFirstMatchSuccess(boundary.groupPath);
        Value matched = boundary.age == 0
                            ? starts[alternativeIndex]
                            : survives[alternativeIndex][boundary.age];
        success[boundary.age] = arith::OrIOp::create(
            builder, location, success[boundary.age], matched);
      }
    if (!firstMatchGroups.empty())
      function->setAttr("obelisk_sim.first_match_priority_groups",
                        builder.getI64IntegerAttr(firstMatchGroups.size()));

    auto applyFirstMatchPriority = [&](Value enabled, size_t alternativeIndex,
                                       uint64_t age) -> Value {
      for (const FirstMatchBoundary &boundary :
           antecedentAlternatives[alternativeIndex].firstMatchBoundaries) {
        if (boundary.age < age)
          continue;
        Value groupMatched = getFirstMatchSuccess(boundary.groupPath)[age];
        Value selected = falseValue;
        if (boundary.age == age)
          selected = age == 0 ? starts[alternativeIndex]
                              : survives[alternativeIndex][age];
        Value allowed = arith::OrIOp::create(
            builder, location,
            arith::XOrIOp::create(builder, location, groupMatched, trueValue),
            selected);
        enabled = arith::AndIOp::create(builder, location, enabled, allowed);
        enabled.getDefiningOp()->setAttr("obelisk_sim.first_match_priority",
                                         builder.getUnitAttr());
      }
      return enabled;
    };

    SmallVector<Value> enabledContinueAny(antecedentHorizon, falseValue);
    for (auto [alternativeIndex, alternative] :
         llvm::enumerate(antecedentAlternatives)) {
      if (alternative.ages.size() == 1)
        continue;
      Value nextState = zero;
      Value enabled = applyFirstMatchPriority(starts[alternativeIndex],
                                              alternativeIndex, 0);
      enabledContinueAny[0] = arith::OrIOp::create(
          builder, location, enabledContinueAny[0], enabled);
      Value firstMask = arith::ConstantOp::create(builder, location, stateType,
                                                  builder.getI64IntegerAttr(2));
      nextState = arith::OrIOp::create(
          builder, location, nextState,
          arith::SelectOp::create(builder, location, enabled, firstMask, zero));
      for (uint64_t age = 1; age + 1 < alternative.ages.size(); ++age) {
        enabled = applyFirstMatchPriority(survives[alternativeIndex][age],
                                          alternativeIndex, age);
        enabledContinueAny[age] = arith::OrIOp::create(
            builder, location, enabledContinueAny[age], enabled);
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
        nextState = arith::OrIOp::create(
            builder, location, nextState,
            arith::SelectOp::create(builder, location, enabled, nextMask,
                                    zero));
      }
      nextAlternativeStates[alternativeIndex] = nextState;
    }

    SmallVector<Value> nextConsequentStates(antecedentAlternatives.size(),
                                            zero);
    for (size_t channel = 0; channel < antecedentAlternatives.size();
         ++channel) {
      if (!consequentNeedsState)
        break;
      Value state = sim::SimRefLoadOp::create(builder, location, stateType,
                                              consequentStates[channel]);
      uint64_t firstAge = nonoverlapped ? 0 : 1;
      for (uint64_t age = firstAge; age < sequence.ages.size(); ++age) {
        Value mask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << age));
        Value present = arith::AndIOp::create(builder, location, state, mask);
        Value active = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, present, zero);
        FailureOr<Value> matches = evaluateConsequentAge(age);
        if (failed(matches))
          return failure();
        Value advances =
            arith::AndIOp::create(builder, location, active, *matches);
        if (!cover) {
          Value fails = arith::AndIOp::create(
              builder, location, active,
              arith::XOrIOp::create(builder, location, *matches, trueValue));
          reportWhen(fails, false);
        }
        if (age + 1 == sequence.ages.size()) {
          reportWhen(advances, true);
        } else {
          Value nextMask = arith::ConstantOp::create(
              builder, location, stateType,
              builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
          nextConsequentStates[channel] = arith::OrIOp::create(
              builder, location, nextConsequentStates[channel],
              arith::SelectOp::create(builder, location, advances, nextMask,
                                      zero));
        }
      }
    }

    FailureOr<Value> consequentStart = evaluateConsequentAge(0);
    if (failed(consequentStart))
      return failure();
    auto markConsequentTrigger = [&](Operation *operation, size_t channel) {
      operation->setAttr("obelisk_sim.branching_antecedent_consequent_trigger",
                         builder.getUnitAttr());
      operation->setAttr("obelisk_sim.branching_antecedent_channel",
                         builder.getI64IntegerAttr(channel));
    };
    for (auto [channel, triggered] : llvm::enumerate(terminalMatches)) {
      if (nonoverlapped) {
        Value firstMask = arith::ConstantOp::create(
            builder, location, stateType, builder.getI64IntegerAttr(1));
        auto launched = arith::SelectOp::create(builder, location, triggered,
                                                firstMask, zero);
        markConsequentTrigger(launched, channel);
        nextConsequentStates[channel] = arith::OrIOp::create(
            builder, location, nextConsequentStates[channel], launched);
        continue;
      }
      Value matches =
          arith::AndIOp::create(builder, location, triggered, *consequentStart);
      markConsequentTrigger(matches.getDefiningOp(), channel);
      if (!cover) {
        Value fails = arith::AndIOp::create(
            builder, location, triggered,
            arith::XOrIOp::create(builder, location, *consequentStart,
                                  trueValue));
        reportWhen(fails, false);
      }
      if (sequence.ages.size() == 1) {
        reportWhen(matches, true);
      } else {
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType, builder.getI64IntegerAttr(2));
        nextConsequentStates[channel] = arith::OrIOp::create(
            builder, location, nextConsequentStates[channel],
            arith::SelectOp::create(builder, location, matches, nextMask,
                                    zero));
      }
    }

    Value priorMatchedState =
        matchedState ? sim::SimRefLoadOp::create(builder, location, stateType,
                                                 matchedState)
                     : zero;
    Value nextMatchedState = zero;
    for (uint64_t age = 0; age < antecedentHorizon; ++age) {
      Value active = trueValue;
      Value priorMatched = falseValue;
      if (age != 0) {
        active = activeAny[age];
        Value mask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << age));
        Value present =
            arith::AndIOp::create(builder, location, priorMatchedState, mask);
        priorMatched = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, present, zero);
      }
      Value matched = arith::OrIOp::create(builder, location, priorMatched,
                                           successAny[age]);
      Value finished = arith::XOrIOp::create(
          builder, location, enabledContinueAny[age], trueValue);
      Value unmatched =
          arith::XOrIOp::create(builder, location, matched, trueValue);
      Value noAntecedentMatch = arith::AndIOp::create(
          builder, location, active,
          arith::AndIOp::create(builder, location, finished, unmatched));
      noAntecedentMatch.getDefiningOp()->setAttr(
          "obelisk_sim.branching_antecedent_vacuity", builder.getUnitAttr());
      if (!cover)
        reportWhen(noAntecedentMatch, !followedBy);
      if (age + 1 < antecedentHorizon) {
        Value retain = arith::AndIOp::create(builder, location,
                                             enabledContinueAny[age], matched);
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
        auto retained =
            arith::SelectOp::create(builder, location, retain, nextMask, zero);
        retained->setAttr("obelisk_sim.branching_antecedent_matched_history",
                          builder.getUnitAttr());
        nextMatchedState =
            arith::OrIOp::create(builder, location, nextMatchedState, retained);
      }
    }

    for (auto [storage, nextState] :
         llvm::zip_equal(alternativeStates, nextAlternativeStates))
      if (storage)
        sim::SimRefStoreOp::create(builder, location, nextState, storage);
    if (matchedState)
      sim::SimRefStoreOp::create(builder, location, nextMatchedState,
                                 matchedState);
    if (consequentNeedsState)
      for (auto [storage, nextState] :
           llvm::zip_equal(consequentStates, nextConsequentStates))
        sim::SimRefStoreOp::create(builder, location, nextState, storage);
    auto backedge = cf::BranchOp::create(builder, location, wait);
    backedge->setAttr("obelisk_sim.branching_antecedent_backedge",
                      builder.getUnitAttr());
    return success();
  }

  if (branchingSequence || branchingConsequent) {
    ArrayRef<FixedSequence> alternatives =
        branchingConsequent ? ArrayRef<FixedSequence>(consequentAlternatives)
                            : ArrayRef<FixedSequence>(sequenceAlternatives);
    bool perMatchCover = coverSequence;
    function->setAttr(branchingConsequent
                          ? "obelisk_sim.branching_consequent_monitor"
                          : "obelisk_sim.branching_sequence_monitor",
                      builder.getUnitAttr());
    function->setAttr(branchingConsequent
                          ? "obelisk_sim.branching_consequent_alternatives"
                          : "obelisk_sim.branching_sequence_alternatives",
                      builder.getI64IntegerAttr(alternatives.size()));
    if (branchingConsequent && nonoverlapped)
      function->setAttr("obelisk_sim.branching_consequent_nonoverlapped",
                        builder.getUnitAttr());
    size_t vacuousAlternatives = llvm::count_if(
        alternatives, [](const FixedSequence &alternative) {
          return alternative.vacuousSuccess;
        });
    if (vacuousAlternatives != 0)
      function->setAttr("obelisk_sim.vacuous_sequence_alternatives",
                        builder.getI64IntegerAttr(vacuousAlternatives));
    SmallVector<Value> alternativeStates;
    alternativeStates.reserve(alternatives.size());
    size_t horizon = 0;
    for (const FixedSequence &alternative : alternatives) {
      horizon = std::max(horizon, alternative.ages.size());
      if (alternative.ages.size() == 1 &&
          !(branchingConsequent && nonoverlapped)) {
        alternativeStates.push_back(Value{});
        continue;
      }
      alternativeStates.push_back(sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero));
    }
    SmallVector<Value> branchingStateStorages;
    for (Value storage : alternativeStates)
      if (storage)
        branchingStateStorages.push_back(storage);
    if (failed(outlineDisableObserver(branchingStateStorages)))
      return failure();
    if (branchingSequence &&
        failed(outlineEndOfSimulationReports(branchingStateStorages, horizon)))
      return failure();

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

    if (failed(cancelDisabledSample(wait, branchingStateStorages)))
      return failure();

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
    SmallVector<Value> activeAny(horizon, falseValue);
    SmallVector<Value> successAny(horizon, falseValue);
    SmallVector<Value> nonvacuousSuccessAny(horizon, falseValue);
    SmallVector<Value> continueAny(horizon, falseValue);
    SmallVector<SmallVector<Value>> survives(alternatives.size());
    SmallVector<Value> starts(alternatives.size(), falseValue);
    SmallVector<Value> nextStates(alternatives.size(), zero);
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

    Value antecedentTrigger = trueValue;
    if (branchingConsequent) {
      FailureOr<Value> trigger =
          evaluateAge(antecedentSequence.ages.front());
      if (failed(trigger))
        return failure();
      antecedentTrigger = *trigger;
    }

    for (auto [alternativeIndex, alternative] :
         llvm::enumerate(alternatives)) {
      survives[alternativeIndex].resize(alternative.ages.size(), falseValue);
      uint64_t firstActiveAge =
          branchingConsequent && nonoverlapped ? 0 : 1;
      if (!(branchingConsequent && nonoverlapped)) {
        FailureOr<Value> start = evaluateAge(alternative.ages.front());
        if (failed(start))
          return failure();
        Value enabled = *start;
        if (branchingConsequent) {
          auto gated = arith::AndIOp::create(builder, location,
                                             antecedentTrigger, enabled);
          gated->setAttr("obelisk_sim.branching_consequent_trigger",
                         builder.getUnitAttr());
          enabled = gated;
        }
        starts[alternativeIndex] = enabled;
        if (alternative.ages.size() == 1) {
          successAny[0] = arith::OrIOp::create(
              builder, location, successAny[0], enabled);
          if (!alternative.vacuousSuccess)
            nonvacuousSuccessAny[0] = arith::OrIOp::create(
                builder, location, nonvacuousSuccessAny[0], enabled);
        } else
          continueAny[0] = arith::OrIOp::create(
              builder, location, continueAny[0], enabled);
      }

      if (alternative.ages.size() == 1 && firstActiveAge != 0)
        continue;
      Value state = sim::SimRefLoadOp::create(
          builder, location, stateType, alternativeStates[alternativeIndex]);
      for (uint64_t age = firstActiveAge; age < alternative.ages.size();
           ++age) {
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

    struct FirstMatchGroupState {
      SmallVector<FirstMatchGroupComponent, 4> path;
      SmallVector<Value> success;
    };
    SmallVector<FirstMatchGroupState> firstMatchGroups;
    auto getFirstMatchSuccess =
        [&](ArrayRef<FirstMatchGroupComponent> path) -> SmallVector<Value> & {
      for (FirstMatchGroupState &group : firstMatchGroups)
        if (ArrayRef<FirstMatchGroupComponent>(group.path) == path)
          return group.success;
      FirstMatchGroupState group;
      llvm::append_range(group.path, path);
      group.success.assign(horizon, falseValue);
      firstMatchGroups.push_back(std::move(group));
      return firstMatchGroups.back().success;
    };
    auto matchAt = [&](size_t alternativeIndex, uint64_t age) -> Value {
      if (branchingConsequent && nonoverlapped)
        return survives[alternativeIndex][age];
      return age == 0 ? starts[alternativeIndex]
                      : survives[alternativeIndex][age];
    };
    for (auto [alternativeIndex, alternative] :
         llvm::enumerate(alternatives)) {
      for (const FirstMatchBoundary &boundary :
           alternative.firstMatchBoundaries) {
        SmallVector<Value> &success = getFirstMatchSuccess(boundary.groupPath);
        Value matched = matchAt(alternativeIndex, boundary.age);
        success[boundary.age] = arith::OrIOp::create(
            builder, location, success[boundary.age], matched);
      }
    }
    if (!firstMatchGroups.empty())
      function->setAttr("obelisk_sim.first_match_priority_groups",
                        builder.getI64IntegerAttr(firstMatchGroups.size()));

    auto applyFirstMatchPriority = [&](Value enabled, size_t alternativeIndex,
                                       uint64_t age) -> Value {
      const FixedSequence &alternative = alternatives[alternativeIndex];
      for (const FirstMatchBoundary &boundary :
           alternative.firstMatchBoundaries) {
        if (boundary.age < age)
          continue;
        Value groupMatched = getFirstMatchSuccess(boundary.groupPath)[age];
        Value selected = falseValue;
        if (boundary.age == age)
          selected = matchAt(alternativeIndex, age);
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
         llvm::enumerate(alternatives)) {
      if (branchingConsequent && nonoverlapped) {
        Value firstMask = arith::ConstantOp::create(
            builder, location, stateType, builder.getI64IntegerAttr(1));
        auto launch = arith::SelectOp::create(
            builder, location, antecedentTrigger, firstMask, zero);
        launch->setAttr("obelisk_sim.branching_consequent_trigger",
                        builder.getUnitAttr());
        Value nextState = launch;
        for (uint64_t age = 0; age + 1 < alternative.ages.size(); ++age) {
          Value enabled = applyFirstMatchPriority(
              survives[alternativeIndex][age], alternativeIndex, age);
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
        continue;
      }
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
           llvm::enumerate(alternatives)) {
        Value matched = alternative.ages.size() == 1
                            ? starts[alternativeIndex]
                            : survives[alternativeIndex].back();
        reportWhen(matched, true);
      }
    } else {
      if (branchingConsequent && !cover) {
        Value noAntecedent = arith::XOrIOp::create(
            builder, location, antecedentTrigger, trueValue);
        reportWhen(noAntecedent, !followedBy);
      }
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
      Value startActive =
          branchingConsequent
              ? (nonoverlapped ? activeAny[0] : antecedentTrigger)
              : trueValue;
      Value startFailed = arith::AndIOp::create(
          builder, location, startActive,
          arith::XOrIOp::create(builder, location, startFinished, trueValue));
      reportWhen(startFailed, false);
    }

    for (auto [state, nextState] :
         llvm::zip_equal(alternativeStates, nextStates))
      if (state)
        sim::SimRefStoreOp::create(builder, location, nextState, state);
    auto backedge = cf::BranchOp::create(builder, location, wait);
    if (branchingConsequent)
      backedge->setAttr("obelisk_sim.branching_consequent_backedge",
                        builder.getUnitAttr());
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

    if (failed(cancelDisabledSample(wait, {stateStorage})))
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

  if (failed(cancelDisabledSample(wait, {stateStorage})))
    return failure();

  Value asynchronousAbortAtClock;
  if (abort && !abort.getIsSynchronous()) {
    // Unlike sync_* aborts, the condition is unsampled. Read its current value
    // before enabling sampled predicate lowering for the wrapped property.
    FailureOr<Value> condition = lowerExpression(abortCondition);
    if (failed(condition))
      return failure();
    FailureOr<Value> aborts =
        truthValue(*condition, getSemanticLocation(abortCondition));
    if (failed(aborts))
      return failure();
    asynchronousAbortAtClock = *aborts;
  }

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

  if (abort) {
    Value aborts = asynchronousAbortAtClock;
    if (abort.getIsSynchronous()) {
      FailureOr<Value> condition = lowerExpression(abortCondition);
      if (failed(condition))
        return failure();
      FailureOr<Value> sampled =
          truthValue(*condition, getSemanticLocation(abortCondition));
      if (failed(sampled))
        return failure();
      aborts = *sampled;
    }
    if (!aborts)
      return emitError(getSemanticLocation(abortCondition))
                 << "property abort condition could not be evaluated",
             failure();
    bool accepted =
        abort.getAction() == semantic::SVAssertionAbortAction::Accept;
    Block *aborted = addBlock();
    Block *evaluate = addBlock();
    cf::CondBranchOp::create(builder, location, aborts, aborted, ValueRange{},
                             evaluate, ValueRange{});
    setCurrent(aborted);
    // Every live state bit represents one independent property attempt. Abort
    // each exactly once, then include the attempt that starts on this clock.
    // An accepted abort is vacuous, so it does not create a cover-property hit.
    if (!(accepted && cover)) {
      for (uint64_t age = 1; age < sequence.ages.size(); ++age) {
        Value mask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << age));
        Value presentBits =
            arith::AndIOp::create(builder, location, state, mask);
        Value active = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, presentBits, zero);
        if (failed(conditionalResult(active, accepted)))
          return failure();
      }
      Value current = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      if (failed(conditionalResult(current, accepted)))
        return failure();
    }
    if (stateStorage)
      sim::SimRefStoreOp::create(builder, location, zero, stateStorage);
    cf::BranchOp::create(builder, location, wait);
    setCurrent(evaluate);
  }

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
