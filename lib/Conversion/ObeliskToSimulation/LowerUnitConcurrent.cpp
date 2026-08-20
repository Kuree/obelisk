//===- LowerUnitConcurrent.cpp - AOT concurrent assertion monitors -------===//
//
// Compiles the bounded single-clock SVA slice and selected common persistent
// forms into ordinary simulation SSA. Runtime state is a compact bitset or an
// aggregate token DFA carried across clock suspensions; the runtime has no
// temporal interpreter and no solver dependency.
//
//===----------------------------------------------------------------------===//

#include "LowerUnit.h"

#include "obelisk/Runtime/StableHandle.h"
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

Value createPreponedSnapshotEvent(OpBuilder &builder, Location location,
                                  Value context) {
  return sim::SimContextEventOp::create(
      builder, location, sim::EventType::get(builder.getContext()), context,
      builder.getI64IntegerAttr(OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT));
}

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
  /// An intrinsic property completion strength carried by bounded temporal
  /// unary operators. std::nullopt means that an unqualified sequence uses
  /// the enclosing directive's default strength.
  std::optional<bool> intrinsicEndStrong;
  /// True even when Boolean composition makes `intrinsicEndStrong` equal to
  /// the directive-default sentinel. This preserves the fact that a temporal
  /// unary contributed a phase-sensitive completion rule.
  bool hasIntrinsicEndStrength = false;
  /// This trace represents the empty match of a mixed nonoverlapped
  /// antecedent. Its consequent starts on the current tick rather than after
  /// the ordinary nonempty-match handoff. The marker is introduced only after
  /// fixed-sequence compilation, so it cannot leak into general composition.
  bool currentTickConsequentStart = false;
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
/// represented by one bounded warm-up age bitset; all eligible attempts share
/// one exact live count because they observe the same future Boolean value.
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

/// Whether an exact trace is one nonvacuous propositional cube. Such cubes
/// can be complemented and combined without changing a temporal endpoint,
/// action ordering, or match multiplicity.
static bool isOneCycleBooleanCube(const FixedSequence &sequence) {
  if (sequence.ages.size() != 1 || sequence.emptyMatch ||
      sequence.vacuousSuccess || sequence.hasIntrinsicEndStrength ||
      !sequence.firstMatchBoundaries.empty() ||
      !sequence.ages.front().matchItems.empty())
    return false;
  const FixedSequenceAge &age = sequence.ages.front();
  return !age.predicates.empty() || !age.negatedPredicates.empty() ||
         !age.caseGuards.empty();
}

/// Complement a DNF of one-cycle Boolean cubes exactly. De Morgan turns each
/// source cube into a disjunction of inverted literals; distributing those
/// clauses constructs the complemented DNF. The compiler-side minimizer may
/// then use Z3 to remove redundant products before monitor SSA is emitted.
static FailureOr<FixedSequenceAlternatives>
complementOneCycleBooleanDNF(ArrayRef<FixedSequence> alternatives) {
  if (alternatives.empty() ||
      llvm::any_of(alternatives, [](const FixedSequence &alternative) {
        return !isOneCycleBooleanCube(alternative);
      }))
    return failure();

  FixedSequenceAlternatives results(1);
  results.front().ages.resize(1);
  for (const FixedSequence &alternative : alternatives) {
    const FixedSequenceAge &age = alternative.ages.front();
    size_t literalCount = age.predicates.size() + age.negatedPredicates.size() +
                          age.caseGuards.size();
    if (results.size() > maxFixedSequenceAlternatives / literalCount)
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

/// Conjoin two nonvacuous one-cycle Boolean DNFs by Cartesian product. The
/// resulting traces remain nonvacuous.
static FailureOr<FixedSequenceAlternatives>
conjoinOneCycleBooleanDNFs(ArrayRef<FixedSequence> lhs,
                           ArrayRef<FixedSequence> rhs) {
  if (lhs.empty() || rhs.empty() ||
      lhs.size() > maxFixedSequenceAlternatives / rhs.size() ||
      llvm::any_of(lhs,
                   [](const FixedSequence &alternative) {
                     return !isOneCycleBooleanCube(alternative);
                   }) ||
      llvm::any_of(rhs, [](const FixedSequence &alternative) {
        return !isOneCycleBooleanCube(alternative);
      }))
    return failure();

  FixedSequenceAlternatives results;
  results.reserve(lhs.size() * rhs.size());
  for (const FixedSequence &left : lhs) {
    for (const FixedSequence &right : rhs) {
      FixedSequence combined = left;
      const FixedSequenceAge &rightAge = right.ages.front();
      FixedSequenceAge &combinedAge = combined.ages.front();
      llvm::append_range(combinedAge.predicates, rightAge.predicates);
      llvm::append_range(combinedAge.negatedPredicates,
                         rightAge.negatedPredicates);
      llvm::append_range(combinedAge.caseGuards, rightAge.caseGuards);
      combined.vacuousSuccess |= right.vacuousSuccess;
      results.push_back(std::move(combined));
    }
  }
  return results;
}

/// Exact propositional result classes for a one-cycle property. The first
/// index is truth and the second is vacuity. Alternatives inside a class are
/// ordinary nonvacuous Boolean cubes; the class index, rather than each cube,
/// carries the evaluation classification while nested operators are built.
struct OneCyclePropertyDNF {
  FixedSequenceAlternatives alternatives[2][2];
};

static FailureOr<OneCyclePropertyDNF>
compileOneCyclePropertyDNF(Operation *operation,
                           Operation *resolvedClock = nullptr);

/// Return the successful half of an exact property partition in the existing
/// monitor representation. Keeping the two classes separate lets the later
/// Boolean minimizer use Z3 within, but never across, vacuity identity.
static FailureOr<FixedSequenceAlternatives>
flattenOneCyclePropertySuccess(OneCyclePropertyDNF property) {
  FixedSequenceAlternatives &nonvacuous = property.alternatives[true][false];
  FixedSequenceAlternatives &vacuous = property.alternatives[true][true];
  if (nonvacuous.size() > maxFixedSequenceAlternatives - vacuous.size())
    return failure();
  for (FixedSequence &alternative : vacuous)
    alternative.vacuousSuccess = true;
  llvm::append_range(nonvacuous, std::move(vacuous));
  if (nonvacuous.empty())
    return failure();
  return std::move(nonvacuous);
}

/// Conjunction of two bounded property completions. `std::nullopt` denotes
/// the enclosing directive's default sequence strength, so it behaves as the
/// same symbolic Boolean in both operands.
static std::optional<bool>
conjoinIntrinsicEndStrength(std::optional<bool> lhs, std::optional<bool> rhs) {
  if (lhs == true || rhs == true)
    return true;
  if (lhs == false && rhs == false)
    return false;
  return std::nullopt;
}

/// Minimize a same-endpoint property DNF before materializing monitor state.
/// Temporal endpoint identity is deliberately outside the propositional
/// solver: alternatives with different lengths, first_match boundaries, or
/// match-item effects retain their exact source multiplicity and ordering.
static std::optional<BooleanMinimizationStats>
minimizeUniformBooleanAlternatives(FixedSequenceAlternatives &alternatives) {
  if (alternatives.size() < 2)
    return std::nullopt;
  size_t horizon = alternatives.front().ages.size();
  bool vacuousSuccess = alternatives.front().vacuousSuccess;
  std::optional<bool> intrinsicEndStrong =
      alternatives.front().intrinsicEndStrong;
  bool hasIntrinsicEndStrength = alternatives.front().hasIntrinsicEndStrength;
  bool currentTickConsequentStart =
      alternatives.front().currentTickConsequentStart;
  if (horizon == 0 ||
      llvm::any_of(alternatives, [&](const FixedSequence &alternative) {
        return alternative.ages.size() != horizon ||
               alternative.vacuousSuccess != vacuousSuccess ||
               alternative.intrinsicEndStrong != intrinsicEndStrong ||
               alternative.hasIntrinsicEndStrength != hasIntrinsicEndStrength ||
               alternative.currentTickConsequentStart !=
                   currentTickConsequentStart ||
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
    alternative.intrinsicEndStrong = intrinsicEndStrong;
    alternative.hasIntrinsicEndStrength = hasIntrinsicEndStrength;
    alternative.currentTickConsequentStart = currentTickConsequentStart;
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

/// Minimize independently observable-equivalent Boolean groups. Vacuous and
/// nonvacuous successes cannot be merged, nor can traces with different
/// temporal completion metadata, but each same-class DNF can still use Z3.
/// This is especially useful for `implies`, whose false-LHS cubes are vacuous
/// while its true-LHS/RHS cubes are nonvacuous.
static std::optional<BooleanMinimizationStats>
minimizeBooleanAlternatives(FixedSequenceAlternatives &alternatives) {
  if (alternatives.size() < 2)
    return std::nullopt;

  struct Group {
    size_t horizon = 0;
    bool vacuousSuccess = false;
    std::optional<bool> intrinsicEndStrong;
    bool hasIntrinsicEndStrength = false;
    bool currentTickConsequentStart = false;
    FixedSequenceAlternatives alternatives;
  };
  SmallVector<Group, 2> groups;
  auto literalCount = [](const FixedSequence &alternative) {
    size_t count = 0;
    for (const FixedSequenceAge &age : alternative.ages)
      count += age.predicates.size() + age.negatedPredicates.size() +
               age.caseGuards.size();
    return count;
  };

  BooleanMinimizationStats aggregate;
  aggregate.alternativesBefore = alternatives.size();
  for (const FixedSequence &alternative : alternatives) {
    if (alternative.ages.empty() || !alternative.firstMatchBoundaries.empty() ||
        llvm::any_of(alternative.ages, [](const FixedSequenceAge &age) {
          return !age.matchItems.empty();
        }))
      return std::nullopt;
    aggregate.literalsBefore += literalCount(alternative);
    Group *selected = nullptr;
    for (Group &group : groups) {
      if (group.horizon == alternative.ages.size() &&
          group.vacuousSuccess == alternative.vacuousSuccess &&
          group.intrinsicEndStrong == alternative.intrinsicEndStrong &&
          group.hasIntrinsicEndStrength ==
              alternative.hasIntrinsicEndStrength &&
          group.currentTickConsequentStart ==
              alternative.currentTickConsequentStart) {
        selected = &group;
        break;
      }
    }
    if (!selected) {
      groups.push_back({alternative.ages.size(),
                        alternative.vacuousSuccess,
                        alternative.intrinsicEndStrong,
                        alternative.hasIntrinsicEndStrength,
                        alternative.currentTickConsequentStart,
                        {}});
      selected = &groups.back();
    }
    selected->alternatives.push_back(alternative);
  }

  bool minimizedAnyGroup = false;
  FixedSequenceAlternatives minimized;
  minimized.reserve(alternatives.size());
  for (Group &group : groups) {
    if (std::optional<BooleanMinimizationStats> stats =
            minimizeUniformBooleanAlternatives(group.alternatives)) {
      minimizedAnyGroup = true;
      aggregate.solverQueries += stats->solverQueries;
      if (aggregate.backend.empty())
        aggregate.backend = stats->backend;
      else if (aggregate.backend != stats->backend)
        aggregate.backend = "mixed";
    }
    for (FixedSequence &alternative : group.alternatives) {
      aggregate.literalsAfter += literalCount(alternative);
      minimized.push_back(std::move(alternative));
    }
  }
  if (!minimizedAnyGroup)
    return std::nullopt;
  aggregate.alternativesAfter = minimized.size();
  alternatives = std::move(minimized);
  return aggregate;
}

/// Diagnose SVA forms that the bounded monitor compiler intentionally leaves
/// unsupported. Keep these messages tied to the semantic construct instead of
/// reporting the generic fixed-trace compilation failure: users need to know
/// whether a property is malformed, exceeds a bounded implementation limit,
/// or requires a temporal semantic that has not been implemented yet.
static bool diagnoseUnsupportedConcurrentFeature(Operation *operation,
                                                 bool nested = false) {
  if (auto binary = dyn_cast<semantic::SVBinaryAssertionExprOp>(operation);
      binary &&
      (binary.getOperatorKind() == semantic::SVAssertionBinaryOperator::Iff ||
       binary.getOperatorKind() ==
           semantic::SVAssertionBinaryOperator::Implies)) {
    emitError(getSemanticLocation(binary))
        << "SVA property operator '"
        << semantic::stringifySVAssertionBinaryOperator(
               binary.getOperatorKind())
        << "' currently requires one-cycle Boolean property operands without "
           "temporal strength, first_match, or match items, with at most 256 "
           "alternatives in each exact truth/vacuity class and in the emitted "
           "success union";
    return true;
  }

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
            " currently requires a minimum no greater than 63 on one "
            "Boolean term, optionally preceded by a deterministic bounded "
            "prefix and followed by ##1 plus one Boolean term; minimum zero "
            "requires that ##1 Boolean continuation so no empty property "
            "endpoint remains");
      }
      if (kind == semantic::SVSequenceRepetitionKind::Nonconsecutive) {
        return diagnose(
            "nonconsecutive sequence repetition [=] currently requires a "
            "finite range no greater than 63 on one boolean term, "
            "optionally preceded by a deterministic bounded prefix and "
            "followed by ##1 plus one boolean term; minimum zero requires "
            "that continuation so no empty property endpoint remains");
      }
      if (kind == semantic::SVSequenceRepetitionKind::GoTo) {
        return diagnose(
            "goto sequence repetition [->] currently requires a finite range "
            "no greater than 63 on one boolean term, optionally preceded by "
            "a deterministic bounded prefix and followed by ##1 plus one "
            "boolean term; minimum zero requires that continuation so no "
            "empty property endpoint remains");
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
            "bounded one-cycle Boolean property operand whose exact "
            "truth/vacuity-class expansion has at most 256 alternatives and "
            "has no temporal strength, first_match, or match items, or one "
            "deterministic or bounded branching sequence that is multi-cycle "
            "or explicitly qualified by strong/weak, or a supported "
            "aggregate persistent "
            "property (final ##[M:$], positive persistent repetition, "
            "unbounded always/s_eventually, or Boolean until), or an "
            "implication/followed-by with one Boolean antecedent and a "
            "supported bounded or final-##[M:$] consequent");
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
            "match items; an unbounded range minimum must be no greater "
            "than 63");
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
            "' currently requires one-cycle Boolean property operands without "
            "temporal strength, first_match, or match items, with at most 256 "
            "alternatives in each exact truth/vacuity class and in the emitted "
            "success union");
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
          "followed-by, unsupported persistent operators, or "
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

/// Return whether a supported one-cycle property can produce a vacuous result
/// of either truth value. This is deliberately a semantic traversal rather
/// than an operation walk: assertion invocations execute only their expanded
/// body, while retained actual/default and local-initializer children are
/// inventory. False-vacuous results matter even when the existing success-only
/// representation has no vacuous alternative to carry as a marker.
static bool mayHaveVacuousResult(Operation *operation) {
  if (auto instance =
          dyn_cast<semantic::SVAssertionInstanceExpressionOp>(operation)) {
    FailureOr<Operation *> body = getExpandedAssertionBody(instance);
    return failed(body) || mayHaveVacuousResult(*body);
  }

  if (auto simple = dyn_cast<semantic::SVSimpleAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(simple);
    if (!simple.getIsNull() && !simple.getHasRepetition() &&
        children.size() == 1 &&
        isa<semantic::SVAssertionInstanceExpressionOp>(children.front()))
      return mayHaveVacuousResult(children.front());
    return false;
  }

  if (auto conditional =
          dyn_cast<semantic::SVConditionalAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(conditional);
    size_t expectedChildren = conditional.getHasElse() ? 3 : 2;
    if (children.size() != expectedChildren)
      return true;
    return !conditional.getHasElse() || mayHaveVacuousResult(children[1]) ||
           mayHaveVacuousResult(children[2]);
  }

  if (auto caseProperty =
          dyn_cast<semantic::SVCaseAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(caseProperty);
    if (children.empty())
      return true;
    size_t childIndex = 1;
    for (Attribute sizeAttr : caseProperty.getItemGroupSizes()) {
      auto size = dyn_cast<IntegerAttr>(sizeAttr);
      if (!size || size.getInt() <= 0)
        return true;
      size_t labelCount = static_cast<size_t>(size.getInt());
      if (childIndex + labelCount >= children.size())
        return true;
      childIndex += labelCount;
      if (mayHaveVacuousResult(children[childIndex++]))
        return true;
    }
    if (!caseProperty.getHasDefault())
      return true;
    return childIndex >= children.size() ||
           mayHaveVacuousResult(children[childIndex]);
  }

  if (auto unary = dyn_cast<semantic::SVUnaryAssertionExprOp>(operation)) {
    if (unary.getOperatorKind() != semantic::SVAssertionUnaryOperator::Not)
      return false;
    SmallVector<Operation *> children = getChildren(unary);
    return children.size() != 1 || mayHaveVacuousResult(children.front());
  }

  auto binary = dyn_cast<semantic::SVBinaryAssertionExprOp>(operation);
  if (!binary)
    return false;
  SmallVector<Operation *> operands = getChildren(binary);
  if (operands.size() != 2)
    return true;
  bool lhs = mayHaveVacuousResult(operands.front());
  bool rhs = mayHaveVacuousResult(operands.back());
  switch (binary.getOperatorKind()) {
  case semantic::SVAssertionBinaryOperator::Implies:
    // A false antecedent makes `implies` vacuously true even when both
    // operands are otherwise always nonvacuous.
    return true;
  case semantic::SVAssertionBinaryOperator::And:
  case semantic::SVAssertionBinaryOperator::Or:
  case semantic::SVAssertionBinaryOperator::Iff:
    // These results are vacuous only when both operand evaluations are.
    return lhs && rhs;
  default:
    return false;
  }
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
    result.intrinsicEndStrong = nested.intrinsicEndStrong;
    result.hasIntrinsicEndStrength = nested.hasIntrinsicEndStrength;
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
    if (!unary.getRangeIsUnbounded() || !unary.getRangeMin() ||
        *unary.getRangeMin() > 63)
      return failure();
    minimum = *unary.getRangeMin();
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
  result.hasIntrinsicEndStrength |= nested.hasIntrinsicEndStrength;
  if (nested.intrinsicEndStrong) {
    if (result.intrinsicEndStrong &&
        result.intrinsicEndStrong != nested.intrinsicEndStrong)
      return failure();
    result.intrinsicEndStrong = nested.intrinsicEndStrong;
  }
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
  result.hasIntrinsicEndStrength |= nested.hasIntrinsicEndStrength;
  if (nested.intrinsicEndStrong) {
    if (result.intrinsicEndStrong &&
        result.intrinsicEndStrong != nested.intrinsicEndStrong)
      return failure();
    result.intrinsicEndStrong = nested.intrinsicEndStrong;
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

  // A zero-minimum repetition by itself admits an empty match and therefore
  // cannot be used as a sequential property (IEEE 1800-2017 16.12.2 and
  // 16.12.22). A ##1 Boolean continuation eliminates that empty property
  // endpoint; its zero-occurrence case is evaluated from the saturated
  // terminal-eligible state on the repetition entry clock.
  if (repetitionIndex + 1 == children.size()) {
    if (result.minimum == 0)
      return failure();
    return result;
  }
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
      if (!mayHaveVacuousResult(children.front()) &&
          llvm::none_of(*nested, [](const FixedSequence &alternative) {
            return alternative.vacuousSuccess;
          }))
        return complementOneCycleBooleanDNF(*nested);
      FailureOr<OneCyclePropertyDNF> property =
          compileOneCyclePropertyDNF(operation, resolvedClock);
      if (failed(property))
        return failure();
      return flattenOneCyclePropertySuccess(std::move(*property));
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
    bool intrinsicStrong = unary.getOperatorKind() ==
                               semantic::SVAssertionUnaryOperator::SNextTime ||
                           unary.getOperatorKind() ==
                               semantic::SVAssertionUnaryOperator::SAlways ||
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
        shifted.intrinsicEndStrong = intrinsicStrong;
        shifted.hasIntrinsicEndStrength = true;
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
          shifted.intrinsicEndStrong = intrinsicStrong;
          shifted.hasIntrinsicEndStrength = true;
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
    for (FixedSequence &result : results) {
      result.intrinsicEndStrong = intrinsicStrong;
      result.hasIntrinsicEndStrength = true;
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
  bool hasVacuousOperand = llvm::any_of(*lhs, hasVacuousAlternative) ||
                           llvm::any_of(*rhs, hasVacuousAlternative);
  bool lhsMayBeVacuous = mayHaveVacuousResult(operands.front());
  bool rhsMayBeVacuous = mayHaveVacuousResult(operands.back());
  bool hiddenVacuityNeedsExact = false;
  switch (binary.getOperatorKind()) {
  case semantic::SVAssertionBinaryOperator::Implies:
    hiddenVacuityNeedsExact = lhsMayBeVacuous || rhsMayBeVacuous;
    break;
  case semantic::SVAssertionBinaryOperator::And:
  case semantic::SVAssertionBinaryOperator::Or:
  case semantic::SVAssertionBinaryOperator::Iff:
    hiddenVacuityNeedsExact = lhsMayBeVacuous && rhsMayBeVacuous;
    break;
  default:
    break;
  }
  if (hasVacuousOperand || hiddenVacuityNeedsExact) {
    switch (binary.getOperatorKind()) {
    case semantic::SVAssertionBinaryOperator::And:
    case semantic::SVAssertionBinaryOperator::Or:
    case semantic::SVAssertionBinaryOperator::Iff:
    case semantic::SVAssertionBinaryOperator::Implies: {
      FailureOr<OneCyclePropertyDNF> property =
          compileOneCyclePropertyDNF(operation, resolvedClock);
      if (failed(property))
        return failure();
      return flattenOneCyclePropertySuccess(std::move(*property));
    }
    default:
      return failure();
    }
  }

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
        combined.intrinsicEndStrong = conjoinIntrinsicEndStrength(
            left.intrinsicEndStrong, right.intrinsicEndStrong);
        combined.hasIntrinsicEndStrength =
            left.hasIntrinsicEndStrength || right.hasIntrinsicEndStrength;
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
          combined.intrinsicEndStrong = conjoinIntrinsicEndStrength(
              outer.intrinsicEndStrong, inner.intrinsicEndStrong);
          combined.hasIntrinsicEndStrength =
              outer.hasIntrinsicEndStrength || inner.hasIntrinsicEndStrength;
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
    FailureOr<FixedSequenceAlternatives> negatedLhs =
        complementOneCycleBooleanDNF(*lhs);
    FailureOr<FixedSequenceAlternatives> negatedRhs =
        complementOneCycleBooleanDNF(*rhs);
    FailureOr<FixedSequenceAlternatives> bothTrue =
        conjoinOneCycleBooleanDNFs(*lhs, *rhs);
    if (failed(negatedLhs) || failed(negatedRhs) || failed(bothTrue))
      return failure();
    FailureOr<FixedSequenceAlternatives> bothFalse =
        conjoinOneCycleBooleanDNFs(*negatedLhs, *negatedRhs);
    if (failed(bothFalse) ||
        bothTrue->size() > maxFixedSequenceAlternatives - bothFalse->size())
      return failure();
    llvm::append_range(*bothTrue, std::move(*bothFalse));
    return std::move(*bothTrue);
  }
  case semantic::SVAssertionBinaryOperator::Implies: {
    FailureOr<FixedSequenceAlternatives> antecedentFalse =
        complementOneCycleBooleanDNF(*lhs);
    FailureOr<FixedSequenceAlternatives> consequentTrue =
        conjoinOneCycleBooleanDNFs(*lhs, *rhs);
    if (failed(antecedentFalse) || failed(consequentTrue) ||
        antecedentFalse->size() >
            maxFixedSequenceAlternatives - consequentTrue->size())
      return failure();
    for (FixedSequence &alternative : *antecedentFalse)
      alternative.vacuousSuccess = true;
    llvm::append_range(*antecedentFalse, std::move(*consequentTrue));
    return std::move(*antecedentFalse);
  }
  default:
    return failure();
  }
}

static FailureOr<OneCyclePropertyDNF>
compileOneCyclePropertyDNF(Operation *operation, Operation *resolvedClock) {
  auto appendAlternatives = [&](OneCyclePropertyDNF &property, bool truth,
                                bool vacuous,
                                FixedSequenceAlternatives alternatives) {
    FixedSequenceAlternatives &destination =
        property.alternatives[truth][vacuous];
    if (destination.size() > maxFixedSequenceAlternatives - alternatives.size())
      return failure();
    llvm::append_range(destination, std::move(alternatives));
    return success();
  };
  auto gatePredicate = [](OneCyclePropertyDNF &property, Operation *predicate,
                          bool negated) {
    for (bool truth : {false, true}) {
      for (bool vacuous : {false, true}) {
        for (FixedSequence &alternative :
             property.alternatives[truth][vacuous]) {
          if (alternative.ages.size() != 1)
            return failure();
          if (negated)
            alternative.ages.front().negatedPredicates.push_back(predicate);
          else
            alternative.ages.front().predicates.push_back(predicate);
        }
      }
    }
    return success();
  };
  auto gateCase = [](OneCyclePropertyDNF &property, Operation *selector,
                     ArrayRef<Operation *> priorLabels, Operation *label) {
    for (bool truth : {false, true}) {
      for (bool vacuous : {false, true}) {
        for (FixedSequence &alternative :
             property.alternatives[truth][vacuous]) {
          if (alternative.ages.size() != 1)
            return failure();
          for (Operation *prior : priorLabels)
            alternative.ages.front().caseGuards.push_back(
                {selector, prior, true});
          if (label)
            alternative.ages.front().caseGuards.push_back(
                {selector, label, false});
        }
      }
    }
    return success();
  };

  if (auto instance =
          dyn_cast<semantic::SVAssertionInstanceExpressionOp>(operation)) {
    FailureOr<Operation *> body = getExpandedAssertionBody(instance);
    if (failed(body))
      return failure();
    return compileOneCyclePropertyDNF(*body, resolvedClock);
  }

  if (auto simple = dyn_cast<semantic::SVSimpleAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(simple);
    if (!simple.getIsNull() && !simple.getHasRepetition() &&
        children.size() == 1 &&
        isa<semantic::SVAssertionInstanceExpressionOp>(children.front()))
      return compileOneCyclePropertyDNF(children.front(), resolvedClock);
  }

  if (auto conditional =
          dyn_cast<semantic::SVConditionalAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(conditional);
    size_t expectedChildren = conditional.getHasElse() ? 3 : 2;
    if (children.size() != expectedChildren)
      return failure();

    FailureOr<OneCyclePropertyDNF> thenProperty =
        compileOneCyclePropertyDNF(children[1], resolvedClock);
    if (failed(thenProperty) ||
        failed(gatePredicate(*thenProperty, children.front(), false)))
      return failure();
    OneCyclePropertyDNF result = std::move(*thenProperty);

    OneCyclePropertyDNF elseProperty;
    if (conditional.getHasElse()) {
      FailureOr<OneCyclePropertyDNF> compiled =
          compileOneCyclePropertyDNF(children[2], resolvedClock);
      if (failed(compiled))
        return failure();
      elseProperty = std::move(*compiled);
    } else {
      FixedSequence vacuous;
      vacuous.ages.resize(1);
      elseProperty.alternatives[true][true].push_back(std::move(vacuous));
    }
    if (failed(gatePredicate(elseProperty, children.front(), true)))
      return failure();
    for (bool truth : {false, true})
      for (bool vacuous : {false, true})
        if (failed(appendAlternatives(
                result, truth, vacuous,
                std::move(elseProperty.alternatives[truth][vacuous]))))
          return failure();
    return result;
  }

  if (auto caseProperty =
          dyn_cast<semantic::SVCaseAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(caseProperty);
    if (children.empty())
      return failure();
    Operation *selector = children.front();
    size_t childIndex = 1;
    SmallVector<Operation *> priorLabels;
    OneCyclePropertyDNF result;
    for (Attribute sizeAttr : caseProperty.getItemGroupSizes()) {
      auto size = dyn_cast<IntegerAttr>(sizeAttr);
      if (!size || size.getInt() <= 0)
        return failure();
      size_t labelCount = static_cast<size_t>(size.getInt());
      if (childIndex + labelCount >= children.size())
        return failure();
      ArrayRef<Operation *> labels(children.data() + childIndex, labelCount);
      childIndex += labelCount;
      FailureOr<OneCyclePropertyDNF> body =
          compileOneCyclePropertyDNF(children[childIndex++], resolvedClock);
      if (failed(body))
        return failure();
      for (Operation *label : labels) {
        OneCyclePropertyDNF selected = *body;
        if (failed(gateCase(selected, selector, priorLabels, label)))
          return failure();
        for (bool truth : {false, true})
          for (bool vacuous : {false, true})
            if (failed(appendAlternatives(
                    result, truth, vacuous,
                    std::move(selected.alternatives[truth][vacuous]))))
              return failure();
      }
      llvm::append_range(priorLabels, labels);
    }

    OneCyclePropertyDNF fallback;
    if (caseProperty.getHasDefault()) {
      if (childIndex >= children.size())
        return failure();
      FailureOr<OneCyclePropertyDNF> compiled =
          compileOneCyclePropertyDNF(children[childIndex++], resolvedClock);
      if (failed(compiled))
        return failure();
      fallback = std::move(*compiled);
    } else {
      FixedSequence vacuous;
      vacuous.ages.resize(1);
      fallback.alternatives[true][true].push_back(std::move(vacuous));
    }
    if (childIndex != children.size() ||
        failed(gateCase(fallback, selector, priorLabels, nullptr)))
      return failure();
    for (bool truth : {false, true})
      for (bool vacuous : {false, true})
        if (failed(appendAlternatives(
                result, truth, vacuous,
                std::move(fallback.alternatives[truth][vacuous]))))
          return failure();
    return result;
  }

  if (auto unary = dyn_cast<semantic::SVUnaryAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(unary);
    if (unary.getOperatorKind() == semantic::SVAssertionUnaryOperator::Not) {
      if (unary.getHasRange() || children.size() != 1)
        return failure();
      FailureOr<OneCyclePropertyDNF> operand =
          compileOneCyclePropertyDNF(children.front(), resolvedClock);
      if (failed(operand))
        return failure();
      OneCyclePropertyDNF result;
      for (bool truth : {false, true})
        for (bool vacuous : {false, true})
          result.alternatives[!truth][vacuous] =
              std::move(operand->alternatives[truth][vacuous]);
      return result;
    }
  }

  if (auto binary = dyn_cast<semantic::SVBinaryAssertionExprOp>(operation)) {
    semantic::SVAssertionBinaryOperator kind = binary.getOperatorKind();
    bool supported = kind == semantic::SVAssertionBinaryOperator::And ||
                     kind == semantic::SVAssertionBinaryOperator::Or ||
                     kind == semantic::SVAssertionBinaryOperator::Iff ||
                     kind == semantic::SVAssertionBinaryOperator::Implies;
    if (supported) {
      SmallVector<Operation *> operands = getChildren(binary);
      if (operands.size() != 2)
        return failure();
      FailureOr<OneCyclePropertyDNF> lhs =
          compileOneCyclePropertyDNF(operands.front(), resolvedClock);
      FailureOr<OneCyclePropertyDNF> rhs =
          compileOneCyclePropertyDNF(operands.back(), resolvedClock);
      if (failed(lhs) || failed(rhs))
        return failure();

      OneCyclePropertyDNF result;
      for (bool leftTruth : {false, true}) {
        for (bool leftVacuous : {false, true}) {
          ArrayRef<FixedSequence> left =
              lhs->alternatives[leftTruth][leftVacuous];
          if (left.empty())
            continue;
          for (bool rightTruth : {false, true}) {
            for (bool rightVacuous : {false, true}) {
              ArrayRef<FixedSequence> right =
                  rhs->alternatives[rightTruth][rightVacuous];
              if (right.empty())
                continue;
              bool truth = false;
              bool vacuous = false;
              switch (kind) {
              case semantic::SVAssertionBinaryOperator::And:
                truth = leftTruth && rightTruth;
                vacuous = leftVacuous && rightVacuous;
                break;
              case semantic::SVAssertionBinaryOperator::Or:
                truth = leftTruth || rightTruth;
                vacuous = leftVacuous && rightVacuous;
                break;
              case semantic::SVAssertionBinaryOperator::Iff:
                truth = leftTruth == rightTruth;
                vacuous = leftVacuous && rightVacuous;
                break;
              case semantic::SVAssertionBinaryOperator::Implies:
                truth = !leftTruth || rightTruth;
                vacuous = !leftTruth || leftVacuous || rightVacuous;
                break;
              default:
                llvm_unreachable("filtered one-cycle property operator");
              }
              FailureOr<FixedSequenceAlternatives> product =
                  conjoinOneCycleBooleanDNFs(left, right);
              if (failed(product) ||
                  failed(appendAlternatives(result, truth, vacuous,
                                            std::move(*product))))
                return failure();
            }
          }
        }
      }
      return result;
    }
  }

  FailureOr<FixedSequenceAlternatives> successful =
      compileFixedSequenceAlternatives(operation, resolvedClock);
  if (failed(successful) || successful->empty() ||
      llvm::any_of(*successful, [](const FixedSequence &alternative) {
        return !isOneCycleBooleanCube(alternative);
      }))
    return failure();
  FailureOr<FixedSequenceAlternatives> failedProperty =
      complementOneCycleBooleanDNF(*successful);
  if (failed(failedProperty))
    return failure();
  OneCyclePropertyDNF result;
  result.alternatives[true][false] = std::move(*successful);
  result.alternatives[false][false] = std::move(*failedProperty);
  return result;
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

/// Return the uniform intrinsic end strength of a directly bounded temporal
/// property, or std::nullopt when its completion depends on progress phase.
/// A direct temporal unary over an EOS-insensitive one-cycle property is
/// uniform. An and/or tree is uniform only when every leaf has the same rule.
static std::optional<bool>
analyzeUniformIntrinsicEndStrength(Operation *candidate,
                                   Operation *resolvedClock) {
  candidate = unwrapAssertionInstance(candidate);
  if (!candidate)
    return std::nullopt;
  if (auto unary = dyn_cast<semantic::SVUnaryAssertionExprOp>(candidate)) {
    bool strong = unary.getOperatorKind() ==
                      semantic::SVAssertionUnaryOperator::SNextTime ||
                  unary.getOperatorKind() ==
                      semantic::SVAssertionUnaryOperator::SAlways ||
                  unary.getOperatorKind() ==
                      semantic::SVAssertionUnaryOperator::SEventually;
    bool temporal =
        strong ||
        unary.getOperatorKind() ==
            semantic::SVAssertionUnaryOperator::NextTime ||
        unary.getOperatorKind() == semantic::SVAssertionUnaryOperator::Always ||
        unary.getOperatorKind() ==
            semantic::SVAssertionUnaryOperator::Eventually;
    SmallVector<Operation *> children = getChildren(unary);
    if (!temporal || children.size() != 1)
      return std::nullopt;
    FailureOr<FixedSequenceAlternatives> nested =
        compileFixedSequenceAlternatives(children.front(), resolvedClock);
    if (failed(nested) || nested->empty() ||
        llvm::any_of(*nested, [](const FixedSequence &alternative) {
          return alternative.ages.size() != 1 || alternative.emptyMatch ||
                 alternative.vacuousSuccess ||
                 !alternative.firstMatchBoundaries.empty() ||
                 !alternative.ages.front().matchItems.empty() ||
                 alternative.intrinsicEndStrong.has_value() ||
                 alternative.hasIntrinsicEndStrength;
        }))
      return std::nullopt;
    return strong;
  }
  auto binary = dyn_cast<semantic::SVBinaryAssertionExprOp>(candidate);
  if (!binary ||
      (binary.getOperatorKind() != semantic::SVAssertionBinaryOperator::And &&
       binary.getOperatorKind() != semantic::SVAssertionBinaryOperator::Or))
    return std::nullopt;
  SmallVector<Operation *> children = getChildren(binary);
  if (children.size() != 2)
    return std::nullopt;
  std::optional<bool> lhs =
      analyzeUniformIntrinsicEndStrength(children.front(), resolvedClock);
  std::optional<bool> rhs =
      analyzeUniformIntrinsicEndStrength(children.back(), resolvedClock);
  if (!lhs || lhs != rhs)
    return std::nullopt;
  return lhs;
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
  // prepared stable identity for assertion-control selection without turning
  // it into a procedural control activation across clock waits. Unlabeled
  // assertions selected through a module scope carry their prepared identity
  // directly on the directive.
  IntegerAttr assertionControlID =
      op->getAttrOfType<IntegerAttr>("obelisk_sim.assertion_control_target_id");
  if (auto block =
          dyn_cast_or_null<semantic::SVBlockStatementOp>(op->getParentOp())) {
    if (auto path = block.getBlockPathAttr())
      function->setAttr("obelisk_sim.assertion_path", path);
    if (auto target = block->getAttrOfType<IntegerAttr>(
            "obelisk_sim.control_target_id")) {
      function->setAttr("obelisk_sim.assertion_target_id", target);
      if (!assertionControlID)
        assertionControlID = target;
    }
  }
  if (assertionControlID)
    function->setAttr("obelisk_sim.assertion_target_id", assertionControlID);
  bool attemptControlled =
      !expectMonitor && op->hasAttr("obelisk_sim.assertion_controlled");
  bool killControlled =
      !expectMonitor && op->hasAttr("obelisk_sim.assertion_kill_controlled");
  bool actionControlled =
      op->hasAttr("obelisk_sim.assertion_action_controlled");
  if ((attemptControlled || killControlled || actionControlled) &&
      (!assertionControlID ||
       !assertionControlID.getValue().isStrictlyPositive()))
    return emitError(location)
               << "concurrent assertion has no prepared control ID",
           failure();
  auto queryAttemptEnabled = [&]() -> Value {
    if (!attemptControlled)
      return {};
    Value context = function.getBody().front().getArgument(0);
    auto enabled = sim::SimAssertionEnabledOp::create(
        builder, location, builder.getI1Type(), context, assertionControlID);
    enabled->setAttr("obelisk_sim.concurrent_attempt_enable",
                     builder.getUnitAttr());
    return enabled;
  };
  auto gateNewAttempt = [&](Value candidate, Value enabled) -> Value {
    if (!enabled)
      return candidate;
    auto gated = arith::AndIOp::create(builder, location, candidate, enabled);
    gated->setAttr("obelisk_sim.concurrent_attempt_start",
                   builder.getUnitAttr());
    return gated;
  };
  auto queryActionState = [&]() -> Value {
    if (!actionControlled)
      return {};
    Value context = function.getBody().front().getArgument(0);
    auto state = sim::SimAssertionActionStateOp::create(
        builder, location, builder.getI32Type(), context, assertionControlID);
    state->setAttr("obelisk_sim.concurrent_attempt_action_state",
                   builder.getUnitAttr());
    return state;
  };
  auto gateActionResult = [&](Value condition, Value actionState,
                              bool reportedPassed, bool vacuous) -> Value {
    if (!actionState)
      return condition;
    int32_t mask = reportedPassed ? (vacuous ? 2 : 1) : 4;
    Value selected = arith::AndIOp::create(
        builder, location, actionState,
        arith::ConstantOp::create(builder, location, builder.getI32Type(),
                                  builder.getI32IntegerAttr(mask)));
    Value enabled = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, selected,
        arith::ConstantOp::create(builder, location, builder.getI32Type(),
                                  builder.getI32IntegerAttr(0)));
    auto gated = arith::AndIOp::create(builder, location, condition, enabled);
    gated->setAttr("obelisk_sim.concurrent_action_control",
                   builder.getUnitAttr());
    gated->setAttr("obelisk_sim.concurrent_action_class",
                   builder.getStringAttr(
                       reportedPassed
                           ? (vacuous ? "vacuous-pass" : "nonvacuous-pass")
                           : "fail"));
    return gated;
  };

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
  // Distinguish abort(not(P)) from not(abort(P)).  Both retain the same
  // monitor for P, but only the latter negates the result forced by the abort
  // condition itself.
  bool temporalNegationOutsideAbort = false;
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
    semantic::SVAbortAssertionExprOp negatedAbort;
    Operation *negatedAbortCondition = nullptr;
    if (!abort && operand) {
      if (auto nestedAbort =
              dyn_cast<semantic::SVAbortAssertionExprOp>(operand)) {
        SmallVector<Operation *> abortChildren = getChildren(nestedAbort);
        if (abortChildren.size() != 2)
          return nestedAbort.emitError("malformed property abort expression"),
                 failure();
        Operation *abortOperand = unwrapAssertionInstance(abortChildren.back());
        if (!abortOperand)
          return nestedAbort.emitError(
                     "property abort wraps an unsupported assertion instance"),
                 failure();
        negatedAbort = nestedAbort;
        negatedAbortCondition = abortChildren.front();
        operand = abortOperand;
      }
    }
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
    // Keep unqualified one-cycle Boolean negation on the DNF path, where the
    // optional compiler-side solver can minimize the complemented formula.
    // Temporal negation retains the deterministic, branching, implication,
    // or aggregate persistent operand monitor and flips its one property
    // result at the exact clock where that result becomes known.
    if (fixedOperand) {
      bool temporal = false;
      if (auto binary =
              dyn_cast<semantic::SVBinaryAssertionExprOp>(fixedOperand))
        temporal =
            binary.getOperatorKind() ==
                semantic::SVAssertionBinaryOperator::OverlappedImplication ||
            binary.getOperatorKind() ==
                semantic::SVAssertionBinaryOperator::NonOverlappedImplication ||
            binary.getOperatorKind() ==
                semantic::SVAssertionBinaryOperator::OverlappedFollowedBy ||
            binary.getOperatorKind() ==
                semantic::SVAssertionBinaryOperator::NonOverlappedFollowedBy;
      if (!temporal) {
        FailureOr<FixedSequenceAlternatives> fixed =
            compileFixedSequenceAlternatives(fixedOperand, clock);
        bool valid = succeeded(fixed) && !fixed->empty() &&
                     llvm::all_of(*fixed, [](const FixedSequence &alternative) {
                       return !alternative.emptyMatch &&
                              !alternative.vacuousSuccess &&
                              !alternative.ages.empty();
                     });
        temporal = valid &&
                   (negatedAbort || explicitStrength ||
                    llvm::any_of(*fixed, [](const FixedSequence &alternative) {
                      return alternative.ages.size() > 1;
                    }));
      }
      if (!temporal)
        temporal = succeeded(compilePersistentDelay(fixedOperand)) ||
                   succeeded(compilePersistentUnary(fixedOperand)) ||
                   succeeded(compilePersistentUntil(fixedOperand)) ||
                   succeeded(compilePersistentRepetition(fixedOperand));
      if (temporal) {
        temporalNegation = candidate;
        property = operand;
        if (negatedAbort) {
          abort = negatedAbort;
          abortCondition = negatedAbortCondition;
          temporalNegationOutsideAbort = true;
          function->setAttr(
              "obelisk_sim.temporal_property_negation_outside_abort",
              builder.getUnitAttr());
        }
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
  Operation *outerFirstMatchOperation = nullptr;
  if (auto first = dyn_cast<semantic::SVFirstMatchAssertionExprOp>(property)) {
    SmallVector<Operation *> nested = getChildren(first);
    if (first.getMatchItemCount() != 0 || nested.size() != 1)
      return emitError(getSemanticLocation(first))
                 << "bounded first_match does not yet support match items",
             failure();
    firstMatch = true;
    outerFirstMatchOperation = first.getOperation();
  }
  auto hasOnlyOuterFirstMatchBoundary = [&](const FixedSequence &candidate) {
    if (!firstMatch || candidate.firstMatchBoundaries.size() != 1)
      return false;
    const FirstMatchBoundary &boundary = candidate.firstMatchBoundaries.front();
    return boundary.groupPath.size() == 1 &&
           boundary.groupPath.front().operation == outerFirstMatchOperation &&
           boundary.groupPath.front().activation == 0;
  };

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
        killControlled || actionControlled ||
        op.getAssertionKind() == semantic::SVAssertionKind::CoverSequence)
      return emitError(getSemanticLocation(property))
                 << "multi-clock sequence handoff currently requires a plain "
                    "property directive without locals, disable iff, Kill or "
                    "action control, expect, or cover-sequence per-match "
                    "accounting",
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
      if (Value enabled = queryAttemptEnabled()) {
        Block *launch = addBlock();
        cf::CondBranchOp::create(builder, location, enabled, launch,
                                 ValueRange{}, wait, ValueRange{});
        setCurrent(launch);
      }
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
  size_t antecedentAlternativeAdmissionCount = 1;
  size_t consequentAlternativeAdmissionCount = 1;
  bool consequentAlternativesAdmissionEligible = true;
  bool consequentEndStrengthComposable = true;
  std::optional<bool> consequentUniformIntrinsicEndStrong;
  std::optional<bool> expectEndStrong;
  auto recordBooleanMinimization = [&](const BooleanMinimizationStats &stats,
                                       StringRef scope) {
    auto accumulate = [&](StringRef name, uint64_t value) {
      uint64_t existing = 0;
      if (auto attribute = function->getAttrOfType<IntegerAttr>(name))
        existing = attribute.getValue().getZExtValue();
      function->setAttr(name, builder.getI64IntegerAttr(existing + value));
    };
    StringRef aggregateBackend = stats.backend;
    if (auto existing = function->getAttrOfType<StringAttr>(
            "obelisk_sim.sva_boolean_solver");
        existing && existing.getValue() == "z3")
      aggregateBackend = "z3";
    function->setAttr("obelisk_sim.sva_boolean_solver",
                      builder.getStringAttr(aggregateBackend));
    accumulate("obelisk_sim.sva_boolean_solver_queries", stats.solverQueries);
    accumulate("obelisk_sim.sva_boolean_alternatives_before",
               stats.alternativesBefore);
    accumulate("obelisk_sim.sva_boolean_alternatives_after",
               stats.alternativesAfter);
    accumulate("obelisk_sim.sva_boolean_literals_before", stats.literalsBefore);
    accumulate("obelisk_sim.sva_boolean_literals_after", stats.literalsAfter);

    auto scopedName = [&](StringRef suffix) {
      return (Twine("obelisk_sim.sva_boolean_") + scope + "_" + suffix).str();
    };
    function->setAttr(scopedName("solver"),
                      builder.getStringAttr(stats.backend));
    function->setAttr(scopedName("solver_queries"),
                      builder.getI64IntegerAttr(stats.solverQueries));
    function->setAttr(scopedName("alternatives_before"),
                      builder.getI64IntegerAttr(stats.alternativesBefore));
    function->setAttr(scopedName("alternatives_after"),
                      builder.getI64IntegerAttr(stats.alternativesAfter));
    function->setAttr(scopedName("literals_before"),
                      builder.getI64IntegerAttr(stats.literalsBefore));
    function->setAttr(scopedName("literals_after"),
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
    FailureOr<FixedSequenceAlternatives> lhs =
        compileFixedSequenceAlternatives(operands.front(), clock);
    FailureOr<FixedSequenceAlternatives> rhs =
        compileFixedSequenceAlternatives(operands.back(), clock);
    FailureOr<PersistentDelaySequence> delayedRhs =
        compilePersistentDelay(operands.back());
    FailureOr<PersistentUnaryProperty> unaryRhs =
        compilePersistentUnary(operands.back());
    FailureOr<PersistentUntilProperty> untilRhs =
        compilePersistentUntil(operands.back());
    FailureOr<PersistentRepetitionSequence> repetitionRhs =
        compilePersistentRepetition(operands.back());
    if (succeeded(lhs)) {
      bool hasEmpty = llvm::any_of(
          *lhs, [](const FixedSequence &value) { return value.emptyMatch; });
      if (hasEmpty && nonoverlapped) {
        bool allEmpty = llvm::all_of(
            *lhs, [](const FixedSequence &value) { return value.emptyMatch; });
        // An empty nonoverlapped match starts its consequent at the nearest
        // clock tick beginning with the antecedent start. In this singly
        // clocked monitor that is the current tick. Represent each empty match
        // as a predicate-free one-age activation. For an empty-only antecedent
        // the ordinary deterministic path can suppress the global handoff. A
        // mixed antecedent keeps that handoff for its nonempty endpoints and
        // marks the empty channel for current-tick dispatch by the source-age
        // coalescer.
        for (FixedSequence &value : *lhs) {
          if (!value.emptyMatch)
            continue;
          value = FixedSequence{};
          value.ages.resize(1);
          value.currentTickConsequentStart = !allEmpty;
        }
        if (allEmpty) {
          nonoverlapped = false;
          function->setAttr("obelisk_sim.empty_antecedent_nonoverlap",
                            builder.getUnitAttr());
        } else {
          function->setAttr("obelisk_sim.mixed_empty_antecedent_nonoverlap",
                            builder.getUnitAttr());
        }
      } else if (hasEmpty) {
        // Empty matches have no endpoint and therefore do not trigger an
        // overlapped consequent. A mixed antecedent remains nondegenerate by
        // virtue of its retained nonempty alternatives.
        llvm::erase_if(
            *lhs, [](const FixedSequence &value) { return value.emptyMatch; });
        function->setAttr("obelisk_sim.overlapped_empty_matches_ignored",
                          builder.getUnitAttr());
        if (lhs->empty())
          return emitError(getSemanticLocation(operands.front()))
                     << "an overlapping implication/followed-by antecedent "
                        "must admit a nonempty match",
                 failure();
      }
    }
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
         llvm::any_of(
             *rhs,
             [](const FixedSequence &value) { return value.ages.empty(); })) &&
        failed(delayedRhs) && failed(unaryRhs) && failed(untilRhs) &&
        failed(repetitionRhs)) {
      if (diagnoseUnsupportedConcurrentFeature(operands.back(),
                                               /*nested=*/true))
        return failure();
      return binary.emitError(
                 "AOT implication/followed-by consequent must expand to at "
                 "most 256 bounded alternatives within the 63-cycle "
                 "horizon"),
             failure();
    }
    antecedentAlternativeAdmissionCount = lhs->size();
    if (succeeded(rhs)) {
      consequentAlternativeAdmissionCount = rhs->size();
      consequentAlternativesAdmissionEligible =
          llvm::all_of(*rhs, [](const FixedSequence &alternative) {
            return !alternative.ages.empty() && !alternative.vacuousSuccess &&
                   alternative.firstMatchBoundaries.empty() &&
                   llvm::all_of(alternative.ages,
                                [](const FixedSequenceAge &age) {
                                  return age.matchItems.empty();
                                });
          });
      bool hasIntrinsicEndStrength =
          llvm::any_of(*rhs, [](const FixedSequence &alternative) {
            return alternative.hasIntrinsicEndStrength;
          });
      if (hasIntrinsicEndStrength) {
        consequentUniformIntrinsicEndStrong =
            analyzeUniformIntrinsicEndStrength(operands.back(), clock);
        consequentEndStrengthComposable =
            consequentUniformIntrinsicEndStrong.has_value();
      }
    }
    // Once branching antecedent results are coalesced by source attempt,
    // equivalent same-endpoint alternatives are idempotent for both universal
    // implication and existential followed-by evaluation.  Minimize both
    // sides before materializing monitor channels. Cover sequence alone must
    // retain source match multiplicity. The raw admission counts above still
    // enforce the pre-minimization expansion cap.
    if (!coverSequence)
      if (std::optional<BooleanMinimizationStats> stats =
              minimizeBooleanAlternatives(*lhs))
        recordBooleanMinimization(*stats, "antecedent");
    if (!coverSequence && succeeded(rhs))
      if (std::optional<BooleanMinimizationStats> stats =
              minimizeBooleanAlternatives(*rhs))
        recordBooleanMinimization(*stats, "consequent");
    implication = binary;
    bool ordinaryBranchingConsequentAfterMinimization =
        lhs->front().ages.size() == 1 && succeeded(rhs) && rhs->size() > 1;
    bool sourceAttemptCanRemainPending =
        lhs->front().ages.size() > 1 ||
        (succeeded(rhs) &&
         (nonoverlapped ||
          llvm::any_of(*rhs, [](const FixedSequence &alternative) {
            return alternative.ages.size() > 1;
          })));
    bool useSourceAgeCoalescer =
        lhs->size() == 1 && sourceAttemptCanRemainPending &&
        (antecedentAlternativeAdmissionCount > 1 ||
         lhs->front().ages.size() > 1) &&
        !localInstance && !ordinaryBranchingConsequentAfterMinimization;
    if (lhs->size() == 1 && !useSourceAgeCoalescer)
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
    } else if (succeeded(unaryRhs)) {
      persistentUnary = std::move(*unaryRhs);
      hasPersistentUnary = true;
      sequence.ages.resize(1);
    } else if (succeeded(untilRhs)) {
      persistentUntil = std::move(*untilRhs);
      hasPersistentUntil = true;
      sequence.ages.resize(1);
    } else if (succeeded(repetitionRhs)) {
      persistentRepetition = std::move(*repetitionRhs);
      hasPersistentRepetition = true;
      sequence.ages.resize(1);
    } else {
      if (rhs->size() == 1)
        sequence = std::move(rhs->front());
      else
        consequentAlternatives = std::move(*rhs);
    }
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
                      "empty match",
               failure();
      // IEEE 16.12.2 makes strong/weak sequence truth insensitive to an outer
      // first_match. Expect uses strong(sequence) by default and also admits
      // explicit strong/weak, so erase exactly that outer boundary before
      // minimization. Retain any nested boundary for rejection below.
      if (expectMonitor && firstMatch &&
          llvm::all_of(*compiled, hasOnlyOuterFirstMatchBoundary))
        for (FixedSequence &alternative : *compiled)
          alternative.firstMatchBoundaries.clear();
      if (!coverSequence) {
        if (std::optional<BooleanMinimizationStats> stats =
                minimizeBooleanAlternatives(*compiled))
          recordBooleanMinimization(*stats, "property");
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
  auto isOneCycleExpectSequence = [](const FixedSequence &candidate) {
    return candidate.ages.size() == 1 &&
           candidate.firstMatchBoundaries.empty() &&
           candidate.ages.front().matchItems.empty();
  };
  auto isBoundedBranchingExpectSequence = [](const FixedSequence &candidate) {
    return !candidate.ages.empty() && candidate.firstMatchBoundaries.empty() &&
           llvm::all_of(candidate.ages, [](const FixedSequenceAge &age) {
             return age.matchItems.empty();
           });
  };
  bool expectOneCycleBoolean =
      expectMonitor &&
      (branchingSequence
           ? llvm::all_of(sequenceAlternatives, isOneCycleExpectSequence)
           : isOneCycleExpectSequence(sequence));
  bool expectBoundedBranching =
      expectMonitor && branchingSequence &&
      llvm::all_of(sequenceAlternatives, isBoundedBranchingExpectSequence);
  bool expectHasIntrinsicEndStrength =
      expectMonitor &&
      (sequence.hasIntrinsicEndStrength ||
       llvm::any_of(sequenceAlternatives, [](const FixedSequence &alternative) {
         return alternative.hasIntrinsicEndStrength;
       }));
  if (expectMonitor && endStrength) {
    expectEndStrong =
        endStrength.getStrength() == semantic::SVAssertionStrength::Strong;
  } else if (expectHasIntrinsicEndStrength) {
    expectEndStrong = analyzeUniformIntrinsicEndStrength(property, clock);
    if (!expectEndStrong)
      return emitError(getSemanticLocation(property))
                 << "expect over bounded temporal property operators "
                    "currently requires one uniform weak or strong completion "
                    "rule composed from direct operators; mixed or "
                    "phase-sensitive intrinsic strength requires per-progress "
                    "end-of-simulation state",
             failure();
  }
  bool deterministicImplicationNeedsEOS =
      implication && !branchingAntecedent && !branchingConsequent &&
      !hasPersistentDelay && !hasPersistentUnary && !hasPersistentUntil &&
      !hasPersistentRepetition &&
      (antecedentSequence.ages.size() > 1 || nonoverlapped ||
       sequence.ages.size() > 1);
  bool branchingConsequentHasIntrinsicEndStrength = llvm::any_of(
      consequentAlternatives, [](const FixedSequence &alternative) {
        return alternative.hasIntrinsicEndStrength;
      });
  bool rawCombinedBranching = antecedentAlternativeAdmissionCount > 1 &&
                              consequentAlternativeAdmissionCount > 1;
  bool rawCombinedBranchingWithinLimit =
      !rawCombinedBranching ||
      antecedentAlternativeAdmissionCount <=
          maxFixedSequenceAlternatives / consequentAlternativeAdmissionCount;
  bool combinedBranching = branchingAntecedent && branchingConsequent;
  bool combinedBoundedBranching =
      combinedBranching && rawCombinedBranchingWithinLimit &&
      consequentAlternativesAdmissionEligible &&
      llvm::all_of(
          consequentAlternatives, [](const FixedSequence &alternative) {
            return !alternative.ages.empty() && !alternative.vacuousSuccess &&
                   alternative.firstMatchBoundaries.empty() &&
                   llvm::all_of(alternative.ages,
                                [](const FixedSequenceAge &age) {
                                  return age.matchItems.empty();
                                });
          });
  bool combinedBooleanBranching =
      combinedBoundedBranching &&
      llvm::all_of(consequentAlternatives,
                   [](const FixedSequence &alternative) {
                     return alternative.ages.size() == 1;
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
  for (const FixedSequence &alternative : sequenceAlternatives)
    consequentHorizon = std::max(consequentHorizon, alternative.ages.size());
  for (const FixedSequence &alternative : consequentAlternatives)
    consequentHorizon = std::max(consequentHorizon, alternative.ages.size());
  if (implication && antecedentHorizon + consequentHorizon > 63)
    return op.emitError(
               "combined implication/followed-by antecedent/consequent state "
               "exceeds the 63-cycle bounded monitor horizon"),
           failure();
  if (actionControlled &&
      (expectMonitor || abort || localInstance || temporalNegation ||
       hasPersistentDelay || hasPersistentUnary || hasPersistentUntil ||
       hasPersistentRepetition || nonoverlapped ||
       (implication && antecedentHorizon != 1) || consequentHorizon != 1 ||
       (branchingAntecedent && sequence.vacuousSuccess)))
    return emitError(location)
               << "concurrent assertion action control currently requires a "
                  "single-clock one-cycle directive without expect, abort, "
                  "locals, persistent state, nonoverlapped handoff, or a "
                  "vacuous branching-antecedent consequent",
           failure();
  if (implication && localInstance && branchingAntecedent)
    return emitError(getSemanticLocation(implication))
               << "branching implication/followed-by antecedents do not yet "
                  "compose with assertion locals",
           failure();
  auto hasMatchItems = [](const FixedSequence &value) {
    return llvm::any_of(value.ages, [](const FixedSequenceAge &age) {
      return !age.matchItems.empty();
    });
  };
  bool persistentConsequentImplicationEligible =
      implication && !branchingAntecedent && !branchingConsequent &&
      antecedentSequence.ages.size() == 1 &&
      !antecedentSequence.vacuousSuccess &&
      antecedentSequence.firstMatchBoundaries.empty() &&
      antecedentSequence.ages.front().matchItems.empty() &&
      antecedentSequence.ages.front().caseGuards.empty();
  if (temporalNegation && implication && !branchingAntecedent &&
      (antecedentSequence.ages.size() != 1 ||
       antecedentSequence.vacuousSuccess ||
       !antecedentSequence.firstMatchBoundaries.empty() ||
       !antecedentSequence.ages.front().matchItems.empty() ||
       !antecedentSequence.ages.front().caseGuards.empty()))
    return emitError(getSemanticLocation(temporalNegation))
               << "temporal property 'not' over implication/followed-by "
                  "currently requires either a bounded antecedent handled by "
                  "the source-age coalescer or one nonvacuous Boolean or "
                  "guaranteed empty antecedent without first_match, case "
                  "guards, or match items",
           failure();
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
  if (branchingAntecedent && (localInstance || expectMonitor || endStrength))
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
  if (branchingAntecedent && !consequentEndStrengthComposable)
    return emitError(getSemanticLocation(implication))
               << "branching implication/followed-by consequents with "
                  "mixed or phase-sensitive intrinsic temporal strength "
                  "require per-progress end-of-simulation completion "
                  "metadata",
           failure();
  if ((rawCombinedBranching && !rawCombinedBranchingWithinLimit) ||
      (combinedBranching && !combinedBoundedBranching))
    return emitError(getSemanticLocation(implication))
               << "combined branching implication/followed-by currently "
                  "requires bounded consequent alternatives without "
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
  if (branchingConsequent && branchingConsequentHasIntrinsicEndStrength &&
      !consequentUniformIntrinsicEndStrong)
    return emitError(getSemanticLocation(implication))
               << "branching implication/followed-by consequents with "
                  "intrinsic temporal strength require per-progress "
                  "end-of-simulation completion metadata",
           failure();
  if (deterministicImplicationNeedsEOS && sequence.hasIntrinsicEndStrength &&
      !consequentEndStrengthComposable)
    return emitError(getSemanticLocation(implication))
               << "implication/followed-by consequents with composed "
                  "intrinsic temporal strength require per-progress "
                  "end-of-simulation completion metadata",
           failure();
  if (deterministicImplicationNeedsEOS && !endStrengthSource)
    endStrengthSource = implication.getOperation();
  if (combinedBoundedBranching)
    // The branching-antecedent monitor owns the exact consequent truth and
    // per-antecedent match channels. Keep an ordinary placeholder state shape
    // while the source-age coalescer owns all bounded consequent alternatives.
    sequence.ages.resize(1);
  if (combinedBoundedBranching && consequentUniformIntrinsicEndStrong) {
    sequence.intrinsicEndStrong = consequentUniformIntrinsicEndStrong;
    sequence.hasIntrinsicEndStrength = true;
  }
  if (hasPersistentRepetition &&
      (localInstance ||
       (implication && !persistentConsequentImplicationEligible) ||
       expectMonitor || firstMatch || coverSequence))
    return emitError(getSemanticLocation(property))
               << "persistent [*]/[->]/[=] repetition currently requires a "
                  "plain assert, assume, cover-property, or restrict "
                  "directive without locals. Implication/followed-by "
                  "additionally requires one nonvacuous Boolean or "
                  "guaranteed-empty antecedent without case guards or "
                  "first_match; expect and cover-sequence per-match "
                  "accounting remain unsupported",
           failure();
  if (hasPersistentUntil &&
      (localInstance ||
       (implication && !persistentConsequentImplicationEligible) ||
       expectMonitor || firstMatch || coverSequence))
    return emitError(getSemanticLocation(property))
               << "persistent until currently requires a plain assert, "
                  "assume, cover-property, or restrict directive without "
                  "locals. Implication/followed-by additionally requires one "
                  "nonvacuous Boolean or guaranteed-empty antecedent without "
                  "case guards or first_match; expect and cover-sequence "
                  "per-match accounting remain unsupported",
           failure();
  if (hasPersistentUnary &&
      (localInstance ||
       (implication && !persistentConsequentImplicationEligible) ||
       expectMonitor || firstMatch || coverSequence))
    return emitError(getSemanticLocation(property))
               << "persistent property operator '"
               << semantic::stringifySVAssertionUnaryOperator(
                      persistentUnary.kind)
               << "' currently requires one outermost unbounded form over "
                  "a deterministic one-cycle Boolean operand without "
                  "locals or match items. Implication/followed-by additionally "
                  "requires one nonvacuous Boolean or guaranteed-empty "
                  "antecedent without case guards or first_match; expect and "
                  "cover-sequence per-match accounting remain unsupported",
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
    if (localInstance || implication || expectMonitor || firstMatch ||
        coverSequence || branchingSequence || matchItems)
      return emitError(getSemanticLocation(abort))
                 << "SVA property operator '" << spelling
                 << "' currently requires one abort and an otherwise "
                    "deterministic bounded property or supported aggregate "
                    "persistent property, optionally with one directly "
                    "adjacent temporal property negation, without locals, "
                    "match items, "
                    "implication/followed-by, first_match, "
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
  if (branchingSequence && (localInstance || implication ||
                            (expectMonitor && !expectBoundedBranching)))
    return emitError(getSemanticLocation(property))
               << "branching bounded sequences currently require a "
                  "concurrent directive without locals or implication, or "
                  "an expect statement with at most one outer first_match "
                  "and no match items",
           failure();
  if (endStrength && (implication || hasPersistentUntil || hasPersistentUnary ||
                      coverSequence))
    return emitError(getSemanticLocation(endStrengthSource))
               << "SVA '"
               << semantic::stringifySVAssertionStrength(
                      endStrength.getStrength())
               << "' end-of-simulation qualification currently requires "
                  "one outermost bounded sequence property without "
                  "implication/followed-by, unsupported persistent "
                  "operators, or cover-sequence per-match accounting",
           failure();
  if (temporalNegation && coverSequence)
    return emitError(getSemanticLocation(temporalNegation))
               << "temporal property 'not' currently requires one "
                  "deterministic or bounded branching property optionally "
                  "qualified by strong/weak, a supported aggregate "
                  "persistent property, or an implication/followed-by with "
                  "one Boolean antecedent, or a supported expect operand, "
                  "without cover-sequence per-match accounting",
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
        (branchingSequence && !expectBoundedBranching) ||
        !sequence.firstMatchBoundaries.empty() ||
        llvm::any_of(sequence.ages,
                     [](const FixedSequenceAge &age) {
                       return !age.matchItems.empty();
                     }) ||
        llvm::any_of(sequenceAlternatives,
                     [](const FixedSequence &alternative) {
                       return !alternative.firstMatchBoundaries.empty() ||
                              llvm::any_of(alternative.ages,
                                           [](const FixedSequenceAge &age) {
                                             return !age.matchItems.empty();
                                           });
                     }))
      return emitError(location)
                 << "expect currently requires one deterministic fixed "
                    "sequence, optionally with outer first_match, or bounded "
                    "alternatives with at most one outer first_match and "
                    "without locals, implication/followed-by, disable iff, "
                    "or match items",
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
    Type expectStateType = builder.getI64Type();
    Value expectZero;
    SmallVector<Value> expectBranchingStateWords;
    if (expectBoundedBranching && !expectOneCycleBoolean) {
      expectZero = arith::ConstantOp::create(builder, location, expectStateType,
                                             builder.getI64IntegerAttr(0));
      size_t wordCount = (sequenceAlternatives.size() + 63) / 64;
      expectBranchingStateWords.reserve(wordCount);
      for (size_t word = 0; word < wordCount; ++word)
        expectBranchingStateWords.push_back(sim::SimRefAllocOp::create(
            builder, location,
            sim::RefType::get(function.getContext(), expectStateType),
            expectZero));
    }

    // Keep private start/completion bits so a final-phase coordinator can
    // close a started, still-pending one-shot evaluation with its implicit or
    // intrinsic strength without duplicating an ordinary-clock result.
    Value expectNotDone = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    Value expectDoneStorage = sim::SimRefAllocOp::create(
        builder, location,
        sim::RefType::get(function.getContext(), builder.getI1Type()),
        expectNotDone);
    Value expectStartedStorage = sim::SimRefAllocOp::create(
        builder, location,
        sim::RefType::get(function.getContext(), builder.getI1Type()),
        expectNotDone);
    auto design = function->getParentOfType<sim::SimDesignOp>();
    if (!design)
      return function.emitError(
                 "expect end-of-simulation completion requires a design"),
             failure();
    uint64_t expectNode = 0;
    if (auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id"))
      expectNode = nodeAttr.getValue().getZExtValue();
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

    std::string finalSymbol =
        (function.getSymName() + ".$expect_eos." + Twine(expectNode)).str();
    std::string finalIdentity =
        (function.getSymName() + ".$expect_eos_identity." + Twine(expectNode))
            .str();
    uint64_t finalCodeUnitID = stableCodeUnitID(finalIdentity);
    std::string finalHierarchy =
        (Twine(parentHierarchy) + ".$expect_eos." + Twine(expectNode)).str();
    Value context = function.getBody().front().getArgument(0);
    SmallVector<Value> finalCaptures{context, completed, resultStorage,
                                     expectDoneStorage, expectStartedStorage};
    SmallVector<Type> finalInputs;
    SmallVector<DictionaryAttr> finalArgumentAttrs;
    for (auto [index, capture] : llvm::enumerate(finalCaptures)) {
      finalInputs.push_back(capture.getType());
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
          !isStaticallyAllocatedOverrideTarget(capture) &&
          llvm::none_of(metadata, [](NamedAttribute attribute) {
            return attribute.getName() ==
                   "obelisk_sim.automatic_reference_capture";
          }))
        metadata.push_back(builder.getNamedAttr(
            "obelisk_sim.automatic_reference_capture", builder.getUnitAttr()));
      finalArgumentAttrs.push_back(builder.getDictionaryAttr(metadata));
    }

    OpBuilder finalBuilder(function);
    finalBuilder.setInsertionPointAfter(function);
    sim::SimCodeUnitDeclOp::create(
        finalBuilder, location, finalCodeUnitID, scopeID, sim::EntryKind::Final,
        finalBuilder.getStringAttr(finalHierarchy),
        finalBuilder.getStringAttr("expect end-of-simulation coordinator"),
        finalBuilder.getUnitAttr());
    SmallVector<NamedAttribute> finalAttributes{
        finalBuilder.getNamedAttr(bindingsAttrName,
                                  finalBuilder.getArrayAttr({})),
        finalBuilder.getNamedAttr(
            "code_unit_id", finalBuilder.getI64IntegerAttr(finalCodeUnitID)),
        finalBuilder.getNamedAttr("internal", finalBuilder.getUnitAttr()),
        finalBuilder.getNamedAttr(
            "home_region", sim::EventRegionAttr::get(function.getContext(),
                                                     sim::EventRegion::Active)),
        finalBuilder.getNamedAttr(
            "domain", sim::ExecutionDomainAttr::get(
                          function.getContext(), sim::ExecutionDomain::Design)),
        finalBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                  finalBuilder.getStringAttr(finalHierarchy)),
    };
    sim::SimFuncOp finalCoordinator = sim::SimFuncOp::create(
        finalBuilder, location, finalSymbol,
        FunctionType::get(function.getContext(), finalInputs, TypeRange{}),
        sim::EntryKind::Final, finalAttributes, finalArgumentAttrs);
    SymbolTable::setSymbolVisibility(finalCoordinator,
                                     SymbolTable::Visibility::Private);
    finalCoordinator->setAttr("obelisk_sim.expect_eos_coordinator",
                              builder.getUnitAttr());
    bool expectOperandStrong = expectEndStrong.value_or(true);
    bool expectOuterStrong =
        temporalNegation ? !expectOperandStrong : expectOperandStrong;
    if (endStrength || temporalNegation) {
      function->setAttr("obelisk_sim.strong_weak_monitor",
                        builder.getUnitAttr());
      function->setAttr(
          "obelisk_sim.end_of_simulation_strength",
          builder.getStringAttr(expectOuterStrong ? "strong" : "weak"));
    }
    if (temporalNegation) {
      function->setAttr(
          "obelisk_sim.negated_operand_end_of_simulation_strength",
          builder.getStringAttr(expectOperandStrong ? "strong" : "weak"));
    }
    finalCoordinator->setAttr(
        "obelisk_sim.expect_operand_strength",
        builder.getStringAttr(expectOperandStrong ? "strong" : "weak"));
    Block &finalEntry = finalCoordinator.getBody().front();
    Block *checkPending = new Block;
    Block *completePending = new Block;
    Block *alreadyComplete = new Block;
    finalCoordinator.getBody().push_back(checkPending);
    finalCoordinator.getBody().push_back(completePending);
    finalCoordinator.getBody().push_back(alreadyComplete);
    OpBuilder finalEntryBuilder = OpBuilder::atBlockEnd(&finalEntry);
    Value wasCompleted = sim::SimRefLoadOp::create(finalEntryBuilder, location,
                                                   builder.getI1Type(),
                                                   finalEntry.getArgument(3));
    cf::CondBranchOp::create(finalEntryBuilder, location, wasCompleted,
                             alreadyComplete, checkPending);
    OpBuilder checkBuilder = OpBuilder::atBlockEnd(checkPending);
    Value wasStarted = sim::SimRefLoadOp::create(
        checkBuilder, location, builder.getI1Type(), finalEntry.getArgument(4));
    cf::CondBranchOp::create(checkBuilder, location, wasStarted,
                             completePending, alreadyComplete);
    OpBuilder completeBuilder = OpBuilder::atBlockEnd(completePending);
    Value finalResult = arith::ConstantOp::create(
        completeBuilder, location, builder.getI1Type(),
        completeBuilder.getBoolAttr(!expectOuterStrong));
    sim::SimRefStoreOp::create(completeBuilder, location, finalResult,
                               finalEntry.getArgument(2));
    Value nowComplete = arith::ConstantOp::create(
        completeBuilder, location, builder.getI1Type(),
        completeBuilder.getBoolAttr(true));
    sim::SimRefStoreOp::create(completeBuilder, location, nowComplete,
                               finalEntry.getArgument(3));
    sim::SimEventTriggerOp::create(
        completeBuilder, location, finalEntry.getArgument(1), Value{},
        completeBuilder.getBoolAttr(false), sim::EventSiteAttr{});
    sim::SimReturnOp::create(completeBuilder, location, ValueRange{});
    OpBuilder alreadyCompleteBuilder = OpBuilder::atBlockEnd(alreadyComplete);
    sim::SimReturnOp::create(alreadyCompleteBuilder, location, ValueRange{});
    sim::SimSpawnOp::create(builder, location,
                            finalCoordinator.getSymNameAttr(), finalCaptures,
                            ArrayAttr{}, ArrayAttr{});
    emitBranch(wait);

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });
    auto markExpectStarted = [&] {
      Value started = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      sim::SimRefStoreOp::create(builder, location, started,
                                 expectStartedStorage);
    };

    if (expectOneCycleBoolean) {
      if (branchingSequence)
        function->setAttr("obelisk_sim.expect_one_cycle_branching",
                          builder.getUnitAttr());
      function->setAttr(
          "obelisk_sim.expect_one_cycle_alternatives",
          builder.getI64IntegerAttr(
              branchingSequence ? sequenceAlternatives.size() : 1));
      Block *sample = addBlock();
      setCurrent(wait);
      if (failed(emitEventSuspend(clock, sample)))
        return failure();
      wait->getTerminator()->setAttr(
          "resume_region",
          sim::EventRegionAttr::get(function.getContext(),
                                    sim::EventRegion::Observed));
      setCurrent(sample);
      markExpectStarted();
      Value falseValue = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(false));
      Value trueValue = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      DenseMap<Operation *, Value> predicateCache;
      DenseMap<Attribute, Value> symbolicPredicateCache;
      DenseMap<std::pair<Operation *, Operation *>, Value> caseGuardCache;
      DenseMap<Operation *, Value> caseSelectorCache;
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

      Value successAny = falseValue;
      auto accumulateAlternative = [&](const FixedSequence &alternative) {
        FailureOr<Value> matches = evaluateAge(alternative.ages.front());
        if (failed(matches))
          return failure();
        successAny =
            arith::OrIOp::create(builder, location, successAny, *matches);
        return success();
      };
      if (branchingSequence) {
        for (const FixedSequence &alternative : sequenceAlternatives)
          if (failed(accumulateAlternative(alternative)))
            return failure();
      } else if (failed(accumulateAlternative(sequence))) {
        return failure();
      }
      cf::CondBranchOp::create(builder, location, successAny, successBlock,
                               ValueRange{}, failureBlock, ValueRange{});
    } else if (expectBoundedBranching) {
      size_t horizon = 0;
      for (const FixedSequence &alternative : sequenceAlternatives)
        horizon = std::max(horizon, alternative.ages.size());
      function->setAttr("obelisk_sim.expect_bounded_branching",
                        builder.getUnitAttr());
      function->setAttr("obelisk_sim.expect_bounded_alternatives",
                        builder.getI64IntegerAttr(sequenceAlternatives.size()));
      function->setAttr("obelisk_sim.expect_bounded_horizon",
                        builder.getI64IntegerAttr(horizon));
      function->setAttr(
          "obelisk_sim.expect_bounded_state_words",
          builder.getI64IntegerAttr(expectBranchingStateWords.size()));

      Block *ageWait = wait;
      for (size_t age = 0; age < horizon; ++age) {
        Block *sample = addBlock();
        setCurrent(ageWait);
        if (failed(emitEventSuspend(clock, sample)))
          return failure();
        ageWait->getTerminator()->setAttr(
            "resume_region",
            sim::EventRegionAttr::get(function.getContext(),
                                      sim::EventRegion::Observed));
        setCurrent(sample);
        if (age == 0)
          markExpectStarted();

        Value falseValue = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(false));
        Value trueValue = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(true));
        SmallVector<Value> currentWords;
        if (age != 0)
          for (Value storage : expectBranchingStateWords)
            currentWords.push_back(sim::SimRefLoadOp::create(
                builder, location, expectStateType, storage));
        SmallVector<Value> nextWords(expectBranchingStateWords.size(),
                                     expectZero);
        Value successAny = falseValue;
        Value continueAny = falseValue;
        DenseMap<Operation *, Value> predicateCache;
        DenseMap<Attribute, Value> symbolicPredicateCache;
        DenseMap<std::pair<Operation *, Operation *>, Value> caseGuardCache;
        DenseMap<Operation *, Value> caseSelectorCache;
        auto evaluateAge =
            [&](const FixedSequenceAge &sequenceAge) -> FailureOr<Value> {
          Value result = trueValue;
          auto evaluatePredicate =
              [&](Operation *predicate) -> FailureOr<Value> {
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
          for (Operation *predicate : sequenceAge.predicates) {
            FailureOr<Value> truth = evaluatePredicate(predicate);
            if (failed(truth))
              return failure();
            result = arith::AndIOp::create(builder, location, result, *truth);
          }
          for (Operation *predicate : sequenceAge.negatedPredicates) {
            FailureOr<Value> truth = evaluatePredicate(predicate);
            if (failed(truth))
              return failure();
            Value negated =
                arith::XOrIOp::create(builder, location, *truth, trueValue);
            result = arith::AndIOp::create(builder, location, result, negated);
          }
          for (const FixedSequenceAge::CaseGuard &guard :
               sequenceAge.caseGuards) {
            std::pair<Operation *, Operation *> key{guard.selector,
                                                    guard.label};
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
              FailureOr<Value> comparison = lowerCaseLabel(
                  selector, selector.getType(), guard.selector, guard.label,
                  semantic::SVCaseCondition::Normal);
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

        for (auto [alternativeIndex, alternative] :
             llvm::enumerate(sequenceAlternatives)) {
          if (age >= alternative.ages.size())
            continue;
          Value active = trueValue;
          size_t wordIndex = alternativeIndex / 64;
          uint64_t bit = uint64_t{1} << (alternativeIndex % 64);
          Value mask =
              arith::ConstantOp::create(builder, location, expectStateType,
                                        builder.getI64IntegerAttr(bit));
          if (age != 0) {
            Value present = arith::AndIOp::create(
                builder, location, currentWords[wordIndex], mask);
            active = arith::CmpIOp::create(builder, location,
                                           arith::CmpIPredicate::ne, present,
                                           expectZero);
          }
          FailureOr<Value> matches = evaluateAge(alternative.ages[age]);
          if (failed(matches))
            return failure();
          Value advances =
              arith::AndIOp::create(builder, location, active, *matches);
          if (age + 1 == alternative.ages.size()) {
            successAny =
                arith::OrIOp::create(builder, location, successAny, advances);
          } else {
            continueAny =
                arith::OrIOp::create(builder, location, continueAny, advances);
            Value retained = arith::SelectOp::create(
                builder, location, advances, mask, expectZero);
            nextWords[wordIndex] = arith::OrIOp::create(
                builder, location, nextWords[wordIndex], retained);
          }
        }

        if (age + 1 != horizon)
          for (auto [storage, nextWord] :
               llvm::zip_equal(expectBranchingStateWords, nextWords))
            sim::SimRefStoreOp::create(builder, location, nextWord, storage);

        if (age + 1 == horizon) {
          cf::CondBranchOp::create(builder, location, successAny, successBlock,
                                   ValueRange{}, failureBlock, ValueRange{});
          continue;
        }
        Block *notSuccessful = addBlock();
        Block *nextWait = addBlock();
        cf::CondBranchOp::create(builder, location, successAny, successBlock,
                                 ValueRange{}, notSuccessful, ValueRange{});
        setCurrent(notSuccessful);
        cf::CondBranchOp::create(builder, location, continueAny, nextWait,
                                 ValueRange{}, failureBlock, ValueRange{});
        ageWait = nextWait;
      }
    } else {
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
        if (age == 0)
          markExpectStarted();
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
    }
    auto finish = [&](Block *block, bool passed) {
      setCurrent(block);
      Value result = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(passed));
      sim::SimRefStoreOp::create(builder, location, result, resultStorage);
      Value done = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      sim::SimRefStoreOp::create(builder, location, done, expectDoneStorage);
      sim::SimEventTriggerOp::create(builder, location, completed, Value{},
                                     builder.getBoolAttr(false),
                                     sim::EventSiteAttr{});
      sim::SimReturnOp::create(builder, location, ValueRange{});
    };
    finish(successBlock, !temporalNegation);
    finish(failureBlock, temporalNegation);
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
  Value killEpochStorage;
  if (killControlled) {
    Value context = function.getBody().front().getArgument(0);
    auto initialKillEpoch = sim::SimAssertionKillEpochOp::create(
        builder, location, stateType, context, assertionControlID);
    initialKillEpoch->setAttr("obelisk_sim.concurrent_kill_epoch",
                              builder.getUnitAttr());
    killEpochStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        initialKillEpoch);
    killEpochStorage.getDefiningOp()->setAttr(
        "obelisk_sim.concurrent_kill_epoch_storage", builder.getUnitAttr());
  }
  auto countNewAttempt = [&](Value enabled) -> Value {
    Value one = arith::ConstantOp::create(builder, location, stateType,
                                          builder.getI64IntegerAttr(1));
    if (!enabled)
      return one;
    auto count = arith::SelectOp::create(builder, location, enabled, one, zero);
    count->setAttr("obelisk_sim.concurrent_attempt_start",
                   builder.getUnitAttr());
    return count;
  };
  bool persistentStateOwner = hasPersistentDelay || hasPersistentUnary ||
                              hasPersistentUntil || hasPersistentRepetition;
  bool needsState =
      (disable && !persistentStateOwner && !branchingSequence &&
       !branchingAntecedent && !branchingConsequent) ||
      (abort && !abort.getIsSynchronous() && !persistentStateOwner) ||
      (!branchingSequence && sequence.ages.size() > 1) ||
      (implication && !branchingAntecedent && !branchingConsequent &&
       !hasPersistentDelay && !hasPersistentUnary && !hasPersistentUntil &&
       !hasPersistentRepetition &&
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
  auto guardReactiveCallback =
      [&](sim::SimFuncOp evaluator, SmallVectorImpl<Value> &captures,
          Location callbackLocation, StringRef killEpochAttr) {
        auto appendEpochArguments = [&](Value epochStorage,
                                        bool useControlEpoch) {
          SmallVector<Type> inputTypes(evaluator.getFunctionType().getInputs());
          inputTypes.push_back(epochStorage.getType());
          inputTypes.push_back(stateType);
          evaluator.setFunctionType(FunctionType::get(
              function.getContext(), inputTypes, TypeRange{}));
          Block &entry = evaluator.getBody().front();
          BlockArgument epochReference =
              entry.addArgument(epochStorage.getType(), callbackLocation);
          BlockArgument expectedEpoch =
              entry.addArgument(stateType, callbackLocation);
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
          argumentAttrs.push_back(builder.getDictionaryAttr(
              {builder.getNamedAttr(
                  "obelisk_sim.capture_kind",
                  sim::CaptureKindAttr::get(function.getContext(),
                                            sim::CaptureKind::Formal))}));
          evaluator.setArgAttrsAttr(builder.getArrayAttr(argumentAttrs));

          Block *body = entry.splitBlock(entry.begin());
          Block *canceled = new Block;
          evaluator.getBody().push_back(canceled);
          OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
          Value currentEpoch;
          if (useControlEpoch) {
            auto current = sim::SimAssertionKillEpochOp::create(
                entryBuilder, callbackLocation, stateType,
                entry.getArgument(0), assertionControlID);
            current->setAttr(killEpochAttr, builder.getUnitAttr());
            currentEpoch = current;
          } else {
            currentEpoch = sim::SimRefLoadOp::create(
                entryBuilder, callbackLocation, stateType, epochReference);
          }
          Value current = arith::CmpIOp::create(
              entryBuilder, callbackLocation, arith::CmpIPredicate::eq,
              currentEpoch, expectedEpoch);
          cf::CondBranchOp::create(entryBuilder, callbackLocation, current,
                                   body, canceled);
          OpBuilder canceledBuilder = OpBuilder::atBlockEnd(canceled);
          sim::SimReturnOp::create(canceledBuilder, callbackLocation,
                                   ValueRange{});
          captures.push_back(epochStorage);
        };

        if (disableEpoch)
          appendEpochArguments(disableEpoch, /*useControlEpoch=*/false);
        if (killEpochStorage)
          appendEpochArguments(killEpochStorage, /*useControlEpoch=*/true);
      };
  auto materializeReactiveCallbackCaptures =
      [&](ArrayRef<Value> callbackCaptures,
          Location callbackLocation) -> SmallVector<Value> {
    SmallVector<Value> captures;
    for (Value capture : callbackCaptures) {
      captures.push_back(capture);
      if (capture == disableEpoch)
        captures.push_back(sim::SimRefLoadOp::create(
            builder, callbackLocation, stateType, disableEpoch));
      if (capture == killEpochStorage)
        captures.push_back(sim::SimRefLoadOp::create(
            builder, callbackLocation, stateType, killEpochStorage));
    }
    return captures;
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
    guardReactiveCallback(callback->first, callback->second, location,
                          "obelisk_sim.concurrent_report_kill_epoch");
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

  auto cancelKilledSample =
      [&](ArrayRef<Value> stateStorages) -> LogicalResult {
    if (!killEpochStorage)
      return success();
    Value context = function.getBody().front().getArgument(0);
    auto currentEpoch = sim::SimAssertionKillEpochOp::create(
        builder, location, stateType, context, assertionControlID);
    currentEpoch->setAttr("obelisk_sim.concurrent_kill_epoch_check",
                          builder.getUnitAttr());
    Value seenEpoch = sim::SimRefLoadOp::create(builder, location, stateType,
                                                killEpochStorage);
    Value current = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, currentEpoch, seenEpoch);
    Block *cancelLive = addBlock();
    Block *continueSample = addBlock();
    cf::CondBranchOp::create(builder, location, current, continueSample,
                             ValueRange{}, cancelLive, ValueRange{});
    setCurrent(cancelLive);
    for (Value storage : stateStorages)
      if (storage)
        sim::SimRefStoreOp::create(builder, location, zero, storage);
    sim::SimRefStoreOp::create(builder, location, currentEpoch,
                               killEpochStorage);
    cf::BranchOp::create(builder, location, continueSample);
    setCurrent(continueSample);
    return success();
  };

  if (disable && !persistentStateOwner && !branchingSequence &&
      !branchingAntecedent && !branchingConsequent &&
      failed(outlineDisableObserver({stateStorage})))
    return failure();

  auto scheduleReportedResult = [&](bool passed) {
    std::optional<ReportCallback> &report = passed ? passReport : failReport;
    if (!report)
      return;
    SmallVector<Value> captures = materializeReactiveCallbackCaptures(
        report->captures, report->location);
    sim::SimSpawnOp::create(builder, report->location,
                            report->function.getSymNameAttr(), captures,
                            ArrayAttr{}, ArrayAttr{});
  };
  auto scheduleResult = [&](bool passed) {
    scheduleReportedResult(temporalNegation ? !passed : passed);
  };
  auto shouldScheduleReportedResult = [&](bool passed) {
    return (passed ? passReport : failReport).has_value();
  };
  auto shouldScheduleResult = [&](bool operandPassed) {
    bool reportedPassed = temporalNegation ? !operandPassed : operandPassed;
    // A cover-property pass action executes for both vacuous and nonvacuous
    // successful attempts.  Query the actual outlined callback as well, so
    // silent pass/fail directions do not materialize empty conditional CFG.
    return shouldScheduleReportedResult(reportedPassed);
  };
  auto scheduleCount = [&](Value count, bool passed) {
    bool reportedPassed = temporalNegation ? !passed : passed;
    std::optional<ReportCallback> &report =
        reportedPassed ? passReport : failReport;
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

  // Compute the number of new attempts entering an aggregate persistent
  // consequent on this clock. A missing antecedent denotes a standalone
  // property. Nonoverlapped composition shifts a true antecedent through one
  // explicit handoff cell; a false antecedent completes at once.
  auto computePersistentAttemptCount = [&](Value attemptEnabled,
                                           Value antecedentTruth,
                                           Value handoffStorage) -> Value {
    if (!antecedentTruth)
      return countNewAttempt(attemptEnabled);

    Value activeAntecedent = gateNewAttempt(antecedentTruth, attemptEnabled);
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    Value antecedentFailed =
        arith::XOrIOp::create(builder, location, antecedentTruth, trueValue);
    Value activeAntecedentFailure =
        gateNewAttempt(antecedentFailed, attemptEnabled);
    Value one = arith::ConstantOp::create(builder, location, stateType,
                                          builder.getI64IntegerAttr(1));
    Value activation =
        arith::SelectOp::create(builder, location, activeAntecedent, one, zero);
    activation.getDefiningOp()->setAttr(
        "obelisk_sim.persistent_implication_activation", builder.getUnitAttr());
    Value antecedentResultCount = arith::SelectOp::create(
        builder, location, activeAntecedentFailure, one, zero);
    antecedentResultCount.getDefiningOp()->setAttr(
        "obelisk_sim.persistent_implication_antecedent_result",
        builder.getUnitAttr());
    scheduleCount(antecedentResultCount, !followedBy);

    if (!handoffStorage)
      return activation;
    Value handoffActivation =
        sim::SimRefLoadOp::create(builder, location, stateType, handoffStorage);
    sim::SimRefStoreOp::create(builder, location, activation, handoffStorage);
    return handoffActivation;
  };

  auto outlineCountedEndOfSimulation = [&](ArrayRef<Value> countStorages,
                                           ArrayRef<Value> bitsetStorages,
                                           bool passed, StringRef identityTag,
                                           Operation *source) -> LogicalResult {
    if (temporalNegation)
      passed = !passed;
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
    for (auto [capture, index] :
         llvm::zip_equal(report->captures, reportCaptureIndices)) {
      Value argument = entry.getArgument(index);
      reportOperands.push_back(argument);
      if (capture == disableEpoch || capture == killEpochStorage)
        reportOperands.push_back(sim::SimRefLoadOp::create(
            bodyBuilder, report->location, stateType, argument));
    }
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
    bool reportedPassed = temporalNegationOutsideAbort ? !accepted : accepted;
    std::optional<ReportCallback> *selectedReport =
        reportedPassed ? &passReport : &failReport;
    bool emitReports = selectedReport->has_value();

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
      for (auto [capture, index] :
           llvm::zip_equal((*selectedReport)->captures, reportCaptureIndices)) {
        Value argument = entry.getArgument(index);
        reportOperands.push_back(argument);
        if (capture == disableEpoch || capture == killEpochStorage)
          reportOperands.push_back(sim::SimRefLoadOp::create(
              bodyBuilder, (*selectedReport)->location, stateType, argument));
      }
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

    // IEEE 1800-2017 16.12.14 evaluates an asynchronous abort condition from
    // sampled values at every simulation time step. The private runtime event
    // is published immediately after the Preponed plane is captured. Static
    // signal dependencies are deliberately omitted: their raw changes cannot
    // change the sampled condition until the next event and would only cause
    // redundant evaluator work later in the same slot.
    Value preponedEvent = createPreponedSnapshotEvent(
        builder, getSemanticLocation(abortCondition), context);
    FailureOr<Value> observer =
        bindObserver(abortCondition, preponedEvent,
                     /*includeStaticDependencies=*/false);
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
    actor.getBody().push_back(waitAbort);
    actor.getBody().push_back(abortLiveAttempts);
    OpBuilder actorEntryBuilder = OpBuilder::atBlockEnd(&actorEntry);
    cf::BranchOp::create(actorEntryBuilder, getSemanticLocation(abort),
                         waitAbort);

    OpBuilder waitBuilder = OpBuilder::atBlockEnd(waitAbort);
    SmallVector<Value> reboundValues;
    for (unsigned index : observerIndices)
      reboundValues.push_back(actorEntry.getArgument(index));
    auto reboundObserver = sim::SimObserverBindOp::create(
        waitBuilder, getSemanticLocation(abortCondition),
        observerBinding.getResult().getType(),
        observerBinding.getEvaluatorAttr(), reboundValues,
        observerBinding.getCaptureCountAttr());
    Value initialFalse = arith::ConstantOp::create(
        waitBuilder, getSemanticLocation(abortCondition),
        waitBuilder.getI1Type(), waitBuilder.getBoolAttr(false));
    SmallVector<Value> observed{reboundObserver.getResult(), initialFalse};
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
    cf::BranchOp::create(abortBuilder, getSemanticLocation(abort), waitAbort);

    sim::SimSpawnOp::create(builder, getSemanticLocation(abort),
                            actor.getSymNameAttr(), actorCaptures, ArrayAttr{},
                            ArrayAttr{});
    return std::optional<PersistentAbortPlan>{std::move(plan)};
  };

  auto abortPersistentSample =
      [&](Block *wait, const std::optional<PersistentAbortPlan> &plan,
          Value currentAttemptCount) -> LogicalResult {
    if (!plan)
      return success();
    // Both synchronous and asynchronous abort conditions use the current
    // clock occurrence's Preponed sample. The asynchronous observer covers
    // every intervening time slot through the private snapshot event.
    FailureOr<Value> condition = lowerExpression(abortCondition);
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
    dispatcherOperands.push_back(currentAttemptCount);
    sim::SimCallOp::create(builder, getSemanticLocation(abort), TypeRange{},
                           plan->dispatcher, dispatcherOperands, ArrayAttr{},
                           ArrayAttr{});
    cf::BranchOp::create(builder, getSemanticLocation(abort), wait);
    setCurrent(evaluate);
    return success();
  };

  auto outlineEndOfSimulationReports =
      [&](ArrayRef<Value> liveStateStorages, size_t horizon,
          size_t firstLiveAge,
          std::optional<bool> operandStrongOverride,
          std::optional<bool> completionPassedOverride = std::nullopt,
          StringRef identitySuffix = {}) -> LogicalResult {
    if (!endStrengthSource)
      return success();
    bool operandStrong = operandStrongOverride.value_or(
        endStrength
            ? endStrength.getStrength() == semantic::SVAssertionStrength::Strong
            // A sequence is weak by default only as the property of
            // assert/assume. Cover-property and restrict use strong
            // sequence semantics.
            : !assertion);
    bool outerStrong = temporalNegation ? !operandStrong : operandStrong;
    semantic::SVAssertionStrength outerStrength =
        outerStrong ? semantic::SVAssertionStrength::Strong
                    : semantic::SVAssertionStrength::Weak;
    if (!completionPassedOverride) {
      function->setAttr("obelisk_sim.strong_weak_monitor",
                        builder.getUnitAttr());
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
    }

    // A one-cycle property has no incomplete attempt at end of simulation.
    // For longer traces each age bit denotes one distinct property attempt.
    // Branching alternatives may carry the same attempt in several state
    // words, so the final coordinator first unions those words and then emits
    // exactly one completion per live age, oldest-first.
    //
    // A successful cover-property attempt executes its pass action regardless
    // of whether separate operator-specific vacuity accounting classifies it
    // as vacuous. In particular, IEEE 16.14.8 classifies sequence properties
    // and explicit strong/weak sequence properties as nonvacuous.
    bool operandPassed = !operandStrong;
    bool completionPassed = completionPassedOverride.value_or(operandPassed);
    if (temporalNegation)
      completionPassed = !completionPassed;
    std::optional<ReportCallback> *selectedReport =
        completionPassed ? &passReport : &failReport;
    if (horizon > firstLiveAge && !liveStateStorages.empty() &&
        selectedReport && *selectedReport) {
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
      StringRef spelling = completionPassedOverride
                               ? (completionPassed ? "pass" : "fail")
                               : semantic::stringifySVAssertionStrength(
                                     outerStrength);
      std::string suffix = identitySuffix.empty()
                               ? std::string{}
                               : (Twine(".") + identitySuffix).str();
      std::string reportSymbol =
          (function.getSymName() + ".$concurrent_eos_report." + Twine(node) +
           "." + spelling + suffix)
              .str();
      std::string reportIdentity =
          (function.getSymName() + ".$concurrent_eos_report_identity." +
           Twine(node) + "." + spelling + suffix)
              .str();
      uint64_t reportCodeUnitID = stableCodeUnitID(reportIdentity);
      std::string reportHierarchy =
          (Twine(parentHierarchy) + ".$concurrent_eos_report." + Twine(node) +
           "." + spelling + suffix)
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
           spelling + suffix)
              .str();
      std::string coordinatorIdentity =
          (function.getSymName() + ".$concurrent_eos_identity." + Twine(node) +
           "." + spelling + suffix)
              .str();
      uint64_t coordinatorCodeUnitID = stableCodeUnitID(coordinatorIdentity);
      std::string coordinatorHierarchy =
          (Twine(parentHierarchy) + ".$concurrent_eos." + Twine(node) + "." +
           spelling + suffix)
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
      if (completionPassedOverride) {
        coordinator->setAttr("obelisk_sim.concurrent_eos_forced_completion",
                             builder.getUnitAttr());
        coordinator->setAttr("obelisk_sim.concurrent_eos_vacuous",
                             builder.getUnitAttr());
      }
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
      for (uint64_t age = horizon; age-- > firstLiveAge;) {
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
        for (auto [capture, index] :
             llvm::zip_equal(report.captures, reportCaptureIndices)) {
          Value argument = coordinator.getBody().front().getArgument(index);
          reportOperands.push_back(argument);
          if (capture == disableEpoch || capture == killEpochStorage)
            reportOperands.push_back(sim::SimRefLoadOp::create(
                reportBuilder, report.location, stateType, argument));
        }
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

  std::optional<bool> intrinsicOperandStrengthOverride;
  if (temporalNegation && hasPersistentUnary)
    intrinsicOperandStrengthOverride = persistentUnary.eventually;
  else if (temporalNegation && hasPersistentUntil)
    intrinsicOperandStrengthOverride = persistentUntil.strong;
  else if (deterministicImplicationNeedsEOS &&
           sequence.intrinsicEndStrong.has_value())
    intrinsicOperandStrengthOverride = sequence.intrinsicEndStrong;
  // Aggregate monitors own their live state and final dispatcher. Still run
  // the shared setup to record switched strength metadata, but make its
  // bounded-state age range empty so it cannot capture their placeholder
  // stateStorage value.
  size_t firstEndOfSimulationAge = persistentStateOwner ? sequence.ages.size()
                                   : implication && nonoverlapped ? 0
                                                                  : 1;
  if (!branchingSequence && !branchingAntecedent && !branchingConsequent &&
      failed(outlineEndOfSimulationReports({stateStorage}, sequence.ages.size(),
                                           firstEndOfSimulationAge,
                                           intrinsicOperandStrengthOverride)))
    return failure();
  if (localInstance && implication && antecedentSequence.ages.size() > 1 &&
      failed(outlineEndOfSimulationReports(
          {stateStorage}, sequence.ages.size() + antecedentSequence.ages.size(),
          sequence.ages.size() + 1,
          /*operandStrongOverride=*/std::nullopt,
          /*completionPassedOverride=*/!followedBy,
          /*identitySuffix=*/"antecedent_no_match")))
    return failure();

  if (abort && !abort.getIsSynchronous() && !persistentStateOwner) {
    // Asynchronous aborts need a persistent observer in addition to the
    // clocked monitor. Its evaluator reads only the once-per-slot Preponed
    // plane and is invoked by the private snapshot event; the clocked path
    // below handles the attempt beginning on a clock where that sampled
    // condition is already true.
    Value context = function.getBody().front().getArgument(0);
    Value preponedEvent = createPreponedSnapshotEvent(
        builder, getSemanticLocation(abortCondition), context);
    FailureOr<Value> observer =
        bindObserver(abortCondition, preponedEvent,
                     /*includeStaticDependencies=*/false);
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

    bool accepted =
        abort.getAction() == semantic::SVAssertionAbortAction::Accept;
    bool reportedPassed = temporalNegationOutsideAbort ? !accepted : accepted;
    std::optional<ReportCallback> *selectedReport =
        reportedPassed ? &passReport : &failReport;

    SmallVector<Value> captures{context};
    unsigned observerCaptureBegin = captures.size();
    llvm::append_range(captures, observerBinding.getValues());
    unsigned stateStorageIndex = captures.size();
    captures.push_back(stateStorage);
    SmallVector<unsigned> selectedCaptureIndices;
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
    appendReportCaptures(*selectedReport, selectedCaptureIndices);
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
    actor.getBody().push_back(waitAbort);
    actor.getBody().push_back(abortLiveAttempts);
    OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
    cf::BranchOp::create(entryBuilder, getSemanticLocation(abort), waitAbort);

    OpBuilder waitBuilder = OpBuilder::atBlockEnd(waitAbort);
    SmallVector<Value> reboundValues;
    for (unsigned index = observerCaptureBegin; index != stateStorageIndex;
         ++index)
      reboundValues.push_back(entry.getArgument(index));
    auto reboundObserver = sim::SimObserverBindOp::create(
        waitBuilder, getSemanticLocation(abortCondition),
        observerBinding.getResult().getType(),
        observerBinding.getEvaluatorAttr(), reboundValues,
        observerBinding.getCaptureCountAttr());
    Value initialFalse = arith::ConstantOp::create(
        waitBuilder, getSemanticLocation(abortCondition),
        waitBuilder.getI1Type(), waitBuilder.getBoolAttr(false));
    SmallVector<Value> observed{reboundObserver.getResult(), initialFalse};
    auto abortWait = sim::SimSuspendObserveOp::create(
        waitBuilder, getSemanticLocation(abortCondition), observed, 0,
        ArrayRef<int32_t>{static_cast<int32_t>(sim::EdgeKind::Posedge)},
        ArrayRef<int32_t>{-1}, sim::ContinuationSiteAttr{},
        sim::EventRegionAttr::get(function.getContext(),
                                  sim::EventRegion::Reactive),
        abortLiveAttempts);
    abortWait->setAttr("obelisk_sim.concurrent_abort_level_true",
                       builder.getUnitAttr());

    Block *currentBlock = abortLiveAttempts;
    Value liveState;
    {
      OpBuilder abortBuilder = OpBuilder::atBlockEnd(currentBlock);
      liveState = sim::SimRefLoadOp::create(
          abortBuilder, getSemanticLocation(abort), stateType,
          entry.getArgument(stateStorageIndex));
    }
    // Oldest attempts complete first. The abort result is vacuous; an outer
    // temporal negation flips its truth while preserving that classification.
    // Every resulting cover-property success still executes the pass action.
    if (*selectedReport) {
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
        for (auto [capture, index] : llvm::zip_equal(
                 (*selectedReport)->captures, selectedCaptureIndices)) {
          Value argument = entry.getArgument(index);
          reportCaptures.push_back(argument);
          if (capture == disableEpoch || capture == killEpochStorage)
            reportCaptures.push_back(sim::SimRefLoadOp::create(
                reportBuilder, (*selectedReport)->location, stateType,
                argument));
        }
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
    cf::BranchOp::create(finishBuilder, getSemanticLocation(abort), waitAbort);

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
    // other directives are strong. A successful cover-property completion
    // executes its pass action. Cover-sequence match accounting stays separate.
    if (!weakCompletion || !coverSequence) {
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
    if (failed(cancelKilledSample(delayStateStorages)))
      return failure();

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    Value attemptEnabled = queryAttemptEnabled();
    Value currentAttemptCount = countNewAttempt(attemptEnabled);
    if (failed(
            abortPersistentSample(wait, *persistentAbort, currentAttemptCount)))
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
    Value prefixActive = attemptEnabled ? attemptEnabled : trueValue;
    Value antecedentResultCount = zero;
    if (implication) {
      FailureOr<Value> antecedent =
          evaluateAge(antecedentSequence.ages.front());
      if (failed(antecedent))
        return failure();
      Value antecedentFails =
          arith::XOrIOp::create(builder, location, *antecedent, trueValue);
      Value activeAntecedent = gateNewAttempt(*antecedent, attemptEnabled);
      Value activeAntecedentFails =
          gateNewAttempt(antecedentFails, attemptEnabled);
      antecedentResultCount = selectCount(activeAntecedentFails, one);
      Value triggered = selectCount(activeAntecedent, one);
      if (delayedActivationStorage) {
        Value delayedActivation = sim::SimRefLoadOp::create(
            builder, location, stateType, delayedActivationStorage);
        prefixActive =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  delayedActivation, zero);
        sim::SimRefStoreOp::create(builder, location, triggered,
                                   delayedActivationStorage);
      } else {
        prefixActive = activeAntecedent;
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
    Value advances =
        arith::AndIOp::create(builder, location, *starts, prefixActive);
    Value failedStart = arith::AndIOp::create(
        builder, location,
        arith::XOrIOp::create(builder, location, *starts, trueValue),
        prefixActive);
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
      // A false followed-by antecedent is an operand failure. Under temporal
      // negation it becomes a vacuous outer success, whose cover pass action
      // still executes even though vacuity is accounted separately.
      if (shouldScheduleResult(!followedBy))
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
    if (implication) {
      function->setAttr("obelisk_sim.persistent_unary_implication",
                        builder.getUnitAttr());
      if (nonoverlapped)
        function->setAttr("obelisk_sim.persistent_unary_nonoverlapped",
                          builder.getUnitAttr());
    }

    Value eligibleStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);
    Value immatureStorage;
    if (persistentUnary.minimum != 0)
      immatureStorage = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero);
    Value handoffStorage;
    if (implication && nonoverlapped) {
      handoffStorage = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero);
      handoffStorage.getDefiningOp()->setAttr(
          "obelisk_sim.persistent_implication_handoff",
          builder.getUnitAttr());
    }

    SmallVector<Value> unaryStateStorages{eligibleStorage};
    if (immatureStorage)
      unaryStateStorages.push_back(immatureStorage);
    if (handoffStorage)
      unaryStateStorages.push_back(handoffStorage);
    if (failed(outlineDisableObserver(unaryStateStorages)))
      return failure();
    SmallVector<Value> unaryBitsetStorages;
    if (immatureStorage)
      unaryBitsetStorages.push_back(immatureStorage);
    if (handoffStorage)
      unaryBitsetStorages.push_back(handoffStorage);
    FailureOr<std::optional<PersistentAbortPlan>> persistentAbort =
        preparePersistentAbort({eligibleStorage}, unaryBitsetStorages);
    if (failed(persistentAbort))
      return failure();

    SmallVector<Value> endCounts{eligibleStorage};
    SmallVector<Value> endBitsets;
    // An attempt younger than M remains vacuous through property negation,
    // but if the resulting cover property succeeds its pass action still
    // executes. Future counters retain the eligible/immature distinction.
    if (immatureStorage)
      endBitsets.push_back(immatureStorage);
    if (handoffStorage)
      endBitsets.push_back(handoffStorage);
    if (failed(outlineCountedEndOfSimulation(
            endCounts, endBitsets,
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
    if (failed(cancelKilledSample(unaryStateStorages)))
      return failure();

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    DenseMap<Operation *, Value> predicateCache;
    DenseMap<Attribute, Value> symbolicPredicateCache;
    auto evaluatePredicate = [&](Operation *predicate) -> FailureOr<Value> {
      if (auto found = predicateCache.find(predicate);
          found != predicateCache.end())
        return found->second;
      Attribute referencedSymbol;
      if (isa<semantic::SVNamedValueExpressionOp,
              semantic::SVHierarchicalValueExpressionOp>(predicate))
        referencedSymbol = predicate->getAttr("referenced_symbol");
      // Direct references to the same static symbol observe one Preponed
      // snapshot, even when the antecedent and consequent own distinct AST
      // nodes. Share their assertion-truth conversion within this transition.
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
      FailureOr<Value> result =
          truthValue(*value, getSemanticLocation(predicate));
      if (failed(result))
        return failure();
      predicateCache[predicate] = *result;
      if (referencedSymbol)
        symbolicPredicateCache[referencedSymbol] = *result;
      return *result;
    };
    auto evaluateAge = [&](const FixedSequenceAge &age) -> FailureOr<Value> {
      Value result = trueValue;
      for (Operation *predicate : age.predicates) {
        FailureOr<Value> value = evaluatePredicate(predicate);
        if (failed(value))
          return failure();
        result = arith::AndIOp::create(builder, location, result, *value);
      }
      for (Operation *predicate : age.negatedPredicates) {
        FailureOr<Value> value = evaluatePredicate(predicate);
        if (failed(value))
          return failure();
        Value negated =
            arith::XOrIOp::create(builder, location, *value, trueValue);
        result = arith::AndIOp::create(builder, location, result, negated);
      }
      return result;
    };
    FailureOr<Value> truth = evaluateAge(persistentUnary.operand);
    if (failed(truth))
      return failure();

    Value attemptEnabled = queryAttemptEnabled();
    Value antecedentTruth;
    if (implication) {
      FailureOr<Value> antecedent =
          evaluateAge(antecedentSequence.ages.front());
      if (failed(antecedent))
        return failure();
      antecedentTruth = *antecedent;
    }
    Value currentAttemptCount = computePersistentAttemptCount(
        attemptEnabled, antecedentTruth, handoffStorage);
    if (failed(
            abortPersistentSample(wait, *persistentAbort, currentAttemptCount)))
      return failure();

    Value one = arith::ConstantOp::create(builder, location, stateType,
                                          builder.getI64IntegerAttr(1));
    Value eligible = sim::SimRefLoadOp::create(builder, location, stateType,
                                               eligibleStorage);
    Value eligibleNow;
    if (!immatureStorage) {
      // The attempt beginning on this tick is immediately eligible.
      eligibleNow = arith::AddIOp::create(builder, location, eligible,
                                          currentAttemptCount);
    } else {
      // Preserve holes created while assertion checking is Off. A scalar
      // immature count is sufficient only when one attempt begins on every
      // clock; the age bitset keeps each enabled start's exact M-tick delay.
      Value immature = sim::SimRefLoadOp::create(builder, location, stateType,
                                                 immatureStorage);
      Value matureMask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1}
                                    << (persistentUnary.minimum - 1)));
      Value matureBit =
          arith::AndIOp::create(builder, location, immature, matureMask);
      Value isMature = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, matureBit, zero);
      Value matured =
          arith::SelectOp::create(builder, location, isMature, one, zero);
      Value shifted = arith::ShLIOp::create(builder, location, immature, one);
      uint64_t queueMask =
          (uint64_t{1} << persistentUnary.minimum) - uint64_t{1};
      Value retained = arith::AndIOp::create(
          builder, location, shifted,
          arith::ConstantOp::create(builder, location, stateType,
                                    builder.getI64IntegerAttr(queueMask)));
      Value nextImmature = arith::OrIOp::create(builder, location, retained,
                                                currentAttemptCount);
      sim::SimRefStoreOp::create(builder, location, nextImmature,
                                 immatureStorage);
      eligibleNow = arith::AddIOp::create(builder, location, eligible, matured);
    }

    Value terminalCount;
    Value nextEligible;
    if (persistentUnary.eventually) {
      terminalCount =
          arith::SelectOp::create(builder, location, *truth, eligibleNow, zero);
      nextEligible =
          arith::SelectOp::create(builder, location, *truth, zero, eligibleNow);
    } else {
      terminalCount =
          arith::SelectOp::create(builder, location, *truth, zero, eligibleNow);
      nextEligible =
          arith::SelectOp::create(builder, location, *truth, eligibleNow, zero);
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
    if (implication) {
      function->setAttr("obelisk_sim.persistent_until_implication",
                        builder.getUnitAttr());
      if (nonoverlapped)
        function->setAttr("obelisk_sim.persistent_until_nonoverlapped",
                          builder.getUnitAttr());
    }

    Value liveStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);
    SmallVector<Value> untilStateStorages{liveStorage};
    Value handoffStorage;
    if (implication && nonoverlapped) {
      handoffStorage = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero);
      handoffStorage.getDefiningOp()->setAttr(
          "obelisk_sim.persistent_implication_handoff", builder.getUnitAttr());
      untilStateStorages.push_back(handoffStorage);
    }
    if (failed(outlineDisableObserver(untilStateStorages)))
      return failure();
    FailureOr<std::optional<PersistentAbortPlan>> persistentAbort =
        preparePersistentAbort({liveStorage},
                               handoffStorage ? ArrayRef<Value>{handoffStorage}
                                              : ArrayRef<Value>{});
    if (failed(persistentAbort))
      return failure();
    SmallVector<Value> endCounts;
    if (persistentUntil.strong || !coverSequence)
      endCounts.push_back(liveStorage);
    if (failed(outlineCountedEndOfSimulation(
            endCounts,
            handoffStorage ? ArrayRef<Value>{handoffStorage}
                           : ArrayRef<Value>{},
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
    if (failed(cancelKilledSample(untilStateStorages)))
      return failure();

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
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
    auto negate = [&](Value value) {
      return arith::XOrIOp::create(builder, location, value, trueValue)
          .getResult();
    };

    Value attemptEnabled = queryAttemptEnabled();
    Value antecedentTruth;
    if (implication) {
      FailureOr<Value> antecedent =
          evaluateAge(antecedentSequence.ages.front());
      if (failed(antecedent))
        return failure();
      antecedentTruth = *antecedent;
    }
    Value currentAttemptCount = computePersistentAttemptCount(
        attemptEnabled, antecedentTruth, handoffStorage);
    if (failed(
            abortPersistentSample(wait, *persistentAbort, currentAttemptCount)))
      return failure();

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

    Value live =
        sim::SimRefLoadOp::create(builder, location, stateType, liveStorage);
    Value attempts =
        arith::AddIOp::create(builder, location, live, currentAttemptCount);
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
    if (implication) {
      function->setAttr("obelisk_sim.persistent_repetition_implication",
                        builder.getUnitAttr());
      if (nonoverlapped)
        function->setAttr("obelisk_sim.persistent_repetition_nonoverlapped",
                          builder.getUnitAttr());
    }

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
    Value handoffStorage;
    if (implication && nonoverlapped) {
      handoffStorage = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero);
      handoffStorage.getDefiningOp()->setAttr(
          "obelisk_sim.persistent_implication_handoff", builder.getUnitAttr());
    }

    SmallVector<Value> repetitionStateStorages;
    for (const TokenState &state : tokenStates)
      repetitionStateStorages.push_back(state.storage);
    if (prefixStateStorage)
      repetitionStateStorages.push_back(prefixStateStorage);
    if (handoffStorage)
      repetitionStateStorages.push_back(handoffStorage);
    if (failed(outlineDisableObserver(repetitionStateStorages)))
      return failure();

    SmallVector<Value> abortCounts;
    for (const TokenState &state : tokenStates)
      abortCounts.push_back(state.storage);
    SmallVector<Value> abortBitsets;
    if (prefixStateStorage)
      abortBitsets.push_back(prefixStateStorage);
    if (handoffStorage)
      abortBitsets.push_back(handoffStorage);
    FailureOr<std::optional<PersistentAbortPlan>> persistentAbort =
        preparePersistentAbort(abortCounts, abortBitsets);
    if (failed(persistentAbort))
      return failure();

    bool weakCompletion = endStrength ? endStrength.getStrength() ==
                                            semantic::SVAssertionStrength::Weak
                                      : assertion;
    SmallVector<Value> endCounts;
    SmallVector<Value> endBitsets;
    if (!weakCompletion || !coverSequence) {
      for (const TokenState &state : tokenStates)
        endCounts.push_back(state.storage);
      if (prefixStateStorage)
        endBitsets.push_back(prefixStateStorage);
      if (handoffStorage)
        endBitsets.push_back(handoffStorage);
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
    if (failed(cancelKilledSample(repetitionStateStorages)))
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
    Value one = arith::ConstantOp::create(builder, location, stateType,
                                          builder.getI64IntegerAttr(1));
    DenseMap<Operation *, Value> predicateCache;
    DenseMap<Operation *, Value> predicateValueCache;
    DenseMap<Attribute, Value> symbolicPredicateCache;
    DenseMap<Attribute, Value> symbolicPredicateValueCache;
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
          predicateValueCache[predicate] =
              symbolicPredicateValueCache.lookup(referencedSymbol);
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
      predicateValueCache[predicate] = *value;
      if (referencedSymbol) {
        symbolicPredicateCache[referencedSymbol] = *truth;
        symbolicPredicateValueCache[referencedSymbol] = *value;
      }
      return *truth;
    };
    auto evaluateAge = [&](const FixedSequenceAge &age) -> FailureOr<Value> {
      Value result = trueValue;
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
    auto evaluateStrictFalse =
        [&](const FixedSequenceAge &age) -> FailureOr<Value> {
      assert(age.predicates.size() + age.negatedPredicates.size() == 1 &&
             "persistent repetition term must be one Boolean literal");
      if (!age.negatedPredicates.empty())
        return evaluatePredicate(age.negatedPredicates.front());

      Operation *predicate = age.predicates.front();
      FailureOr<Value> truth = evaluatePredicate(predicate);
      if (failed(truth))
        return failure();
      Value value = predicateValueCache.lookup(predicate);
      FailureOr<Value> scalar =
          toPackedScalar(value, getSemanticLocation(predicate));
      if (failed(scalar))
        return failure();
      if (auto logic = dyn_cast<sim::LogicType>((*scalar).getType())) {
        IntegerType bits = builder.getIntegerType(logic.getWidth());
        Value zeroLogic = sim::SimLogicConstantOp::create(
            builder, location, logic, builder.getIntegerAttr(bits, 0),
            builder.getIntegerAttr(bits, 0));
        return sim::SimLogicCompareOp::create(
                   builder, location, builder.getI1Type(),
                   sim::CompareKind::CaseEq, *scalar, zeroLogic)
            .getResult();
      }
      return negate(*truth);
    };
    auto selectCount = [&](Value condition, Value count) {
      return arith::SelectOp::create(builder, location, condition, count, zero)
          .getResult();
    };
    auto addCount = [&](Value &target, Value count) {
      target = arith::AddIOp::create(builder, location, target, count);
    };

    Value attemptEnabled = queryAttemptEnabled();
    Value antecedentTruth;
    if (implication) {
      FailureOr<Value> antecedent =
          evaluateAge(antecedentSequence.ages.front());
      if (failed(antecedent))
        return failure();
      antecedentTruth = *antecedent;
    }
    Value currentAttemptCount = computePersistentAttemptCount(
        attemptEnabled, antecedentTruth, handoffStorage);
    if (failed(
            abortPersistentSample(wait, *persistentAbort, currentAttemptCount)))
      return failure();
    Value currentAttemptActive = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, currentAttemptCount, zero);

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
    Value activeStart =
        arith::AndIOp::create(builder, location, *starts, currentAttemptActive);
    Value failedStart = arith::AndIOp::create(
        builder, location, negate(*starts), currentAttemptActive);
    addCount(failureCount, selectCount(failedStart, one));
    if (persistentRepetition.entry.ages.size() == 1) {
      addCount(entryCount, selectCount(activeStart, one));
    } else {
      Value nextMask = arith::ConstantOp::create(builder, location, stateType,
                                                 builder.getI64IntegerAttr(2));
      nextPrefixState = arith::OrIOp::create(
          builder, location, nextPrefixState,
          arith::SelectOp::create(builder, location, activeStart, nextMask,
                                  zero));
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
    bool entryPending = persistentRepetition.minimum == 0 &&
                        persistentRepetition.hasTerminal &&
                        persistentRepetition.kind !=
                            semantic::SVSequenceRepetitionKind::Nonconsecutive;
    addCount(amounts[findTokenState(0, entryPending)], entryCount);

    FailureOr<Value> repeated = evaluateAge(persistentRepetition.term);
    if (failed(repeated))
      return failure();
    Value notRepeated = negate(*repeated);
    Value repeatedUnknown = falseValue;
    if (persistentRepetition.kind !=
        semantic::SVSequenceRepetitionKind::Consecutive) {
      // The derived goto/nonconsecutive gap is !term[*0:$]. Unlike ordinary
      // sequence non-match, X/Z does not satisfy that logical negation: only
      // a known zero may wait, while an unknown value kills the trace.
      FailureOr<Value> strictFalse =
          evaluateStrictFalse(persistentRepetition.term);
      if (failed(strictFalse))
        return failure();
      notRepeated = *strictFalse;
      Value repeatedKnown =
          arith::OrIOp::create(builder, location, *repeated, notRepeated);
      repeatedUnknown = negate(repeatedKnown);
    }
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
        else {
          route(index, amount, notRepeated);
          fail(amount, repeatedUnknown);
        }
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
            Value unknown = arith::AndIOp::create(builder, location,
                                                  notTerminal, repeatedUnknown);
            route(index, amount, consumes);
            route(findTokenState(persistentRepetition.minimum, false), amount,
                  waits);
            fail(amount, unknown);
            break;
          }
          uint64_t nextCount =
              std::min(state.occurrences + 1, persistentRepetition.minimum);
          route(findTokenState(nextCount,
                               nextCount >= persistentRepetition.minimum),
                amount, *repeated);
          route(index, amount, notRepeated);
          fail(amount, repeatedUnknown);
          break;
        }
        case semantic::SVSequenceRepetitionKind::Nonconsecutive: {
          if (state.occurrences >= persistentRepetition.minimum) {
            succeed(amount, terminal);
            Value remainsKnown = arith::AndIOp::create(
                builder, location, notTerminal,
                arith::OrIOp::create(builder, location, *repeated,
                                     notRepeated));
            Value unknown = arith::AndIOp::create(builder, location,
                                                  notTerminal, repeatedUnknown);
            route(index, amount, remainsKnown);
            fail(amount, unknown);
            break;
          }
          route(findTokenState(state.occurrences + 1, false), amount,
                *repeated);
          route(index, amount, notRepeated);
          fail(amount, repeatedUnknown);
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
        Value unknown = arith::AndIOp::create(builder, location, remaining,
                                              repeatedUnknown);
        if (state.occurrences == persistentRepetition.maximum)
          fail(amount, consumes);
        else
          route(findTokenState(state.occurrences + 1, false), amount, consumes);
        route(index, amount, waits);
        fail(amount, unknown);
        continue;
      }

      if (!state.pending) {
        uint64_t nextCount = state.occurrences + 1;
        bool nextPending = nextCount >= persistentRepetition.minimum;
        route(findTokenState(nextCount, nextPending), amount, *repeated);
        route(index, amount, notRepeated);
        fail(amount, repeatedUnknown);
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
      Value unknown = arith::AndIOp::create(builder, location, notTerminal,
                                            repeatedUnknown);
      route(findTokenState(state.occurrences + 1, true), amount, consumes);
      route(findTokenState(state.occurrences, false), amount, waits);
      fail(amount, unknown);
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
    function->setAttr("obelisk_sim.branching_antecedent_result_coalescer",
                      builder.getUnitAttr());
    function->setAttr("obelisk_sim.branching_antecedent_alternatives",
                      builder.getI64IntegerAttr(antecedentAlternatives.size()));
    function->setAttr("obelisk_sim.branching_antecedent_match_channels",
                      builder.getI64IntegerAttr(antecedentAlternatives.size()));
    size_t currentTickChannels = llvm::count_if(
        antecedentAlternatives, [](const FixedSequence &alternative) {
          return alternative.currentTickConsequentStart;
        });
    if (currentTickChannels != 0) {
      function->setAttr(
          "obelisk_sim.mixed_empty_antecedent_current_tick_channels",
          builder.getI64IntegerAttr(currentTickChannels));
      function->setAttr(
          "obelisk_sim.mixed_empty_antecedent_handoff_channels",
          builder.getI64IntegerAttr(antecedentAlternatives.size() -
                                    currentTickChannels));
    }
    if (combinedBoundedBranching) {
      function->setAttr(
          "obelisk_sim.branching_consequent_alternatives",
          builder.getI64IntegerAttr(consequentAlternatives.size()));
      function->setAttr(
          "obelisk_sim.combined_bounded_branching_pairs",
          builder.getI64IntegerAttr(antecedentAlternatives.size() *
                                    consequentAlternatives.size()));
      function->setAttr(
          "obelisk_sim.combined_bounded_branching_pairs_before_minimization",
          builder.getI64IntegerAttr(antecedentAlternativeAdmissionCount *
                                    consequentAlternativeAdmissionCount));
      function->setAttr("obelisk_sim.combined_bounded_branching_monitor",
                        builder.getUnitAttr());
      if (combinedBooleanBranching) {
        function->setAttr("obelisk_sim.combined_boolean_branching_monitor",
                          builder.getUnitAttr());
        function->setAttr(
            "obelisk_sim.combined_boolean_branching_pairs",
            builder.getI64IntegerAttr(antecedentAlternatives.size() *
                                      consequentAlternatives.size()));
        function->setAttr(
            "obelisk_sim.combined_boolean_branching_pairs_before_minimization",
            builder.getI64IntegerAttr(antecedentAlternativeAdmissionCount *
                                      consequentAlternativeAdmissionCount));
      }
    }

    size_t consequentAlternativeCount =
        combinedBoundedBranching && !combinedBooleanBranching
            ? consequentAlternatives.size()
            : 1;
    auto getConsequentAlternative = [&](size_t index) -> const FixedSequence & {
      return combinedBoundedBranching && !combinedBooleanBranching
                 ? consequentAlternatives[index]
                 : sequence;
    };

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
    auto channelIsNonoverlapped = [&](size_t channel) {
      return nonoverlapped &&
             !antecedentAlternatives[channel].currentTickConsequentStart;
    };
    // One source property attempt may produce several antecedent matches.
    // Their consequent evaluations are obligations of that one attempt, not
    // independent property results.  Retain a saw-match bit by original
    // source age until all antecedent paths and consequent obligations for the
    // attempt have resolved.  The last possible result age is the last
    // antecedent end point followed by the consequent horizon (plus the
    // nonoverlap handoff).
    size_t sourceAttemptHorizon = 1;
    for (auto [channel, alternative] : llvm::enumerate(antecedentAlternatives))
      for (size_t consequentIndex = 0;
           consequentIndex < consequentAlternativeCount; ++consequentIndex)
        sourceAttemptHorizon =
            std::max(sourceAttemptHorizon,
                     alternative.ages.size() - 1 +
                         (channelIsNonoverlapped(channel) ? 1 : 0) +
                         getConsequentAlternative(consequentIndex).ages.size());
    function->setAttr("obelisk_sim.branching_antecedent_result_horizon",
                      builder.getI64IntegerAttr(sourceAttemptHorizon));
    Value matchedState;
    if (sourceAttemptHorizon > 1)
      matchedState = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), stateType), zero);

    auto consequentStateIndex = [&](size_t channel, size_t consequentIndex) {
      return channel * consequentAlternativeCount + consequentIndex;
    };
    bool consequentNeedsState = false;
    SmallVector<Value> consequentStates(antecedentAlternatives.size() *
                                        consequentAlternativeCount);
    for (size_t channel = 0; channel < antecedentAlternatives.size(); ++channel)
      for (size_t consequentIndex = 0;
           consequentIndex < consequentAlternativeCount; ++consequentIndex) {
        const FixedSequence &consequent =
            getConsequentAlternative(consequentIndex);
        if (consequent.ages.size() == 1 && !channelIsNonoverlapped(channel))
          continue;
        consequentNeedsState = true;
        consequentStates[consequentStateIndex(channel, consequentIndex)] =
            sim::SimRefAllocOp::create(
                builder, location,
                sim::RefType::get(function.getContext(), stateType), zero);
      }

    SmallVector<Value> branchingStateStorages;
    for (Value storage : alternativeStates)
      if (storage)
        branchingStateStorages.push_back(storage);
    if (matchedState)
      branchingStateStorages.push_back(matchedState);
    if (consequentNeedsState)
      for (Value storage : consequentStates)
        if (storage)
          branchingStateStorages.push_back(storage);
    if (failed(outlineDisableObserver(branchingStateStorages)))
      return failure();

    // At end of simulation, no pending antecedent path can produce another
    // match. A pending consequent completes according to intrinsic temporal
    // unary strength when present, otherwise the directive's default sequence
    // strength (weak for assert/assume, strong for cover/restrict).
    // Finalize those states by original source-attempt age, rather than by
    // antecedent alternative or consequent channel, so an evaluation still
    // produces at most one action callback.
    auto outlineBranchingAntecedentEndOfSimulation = [&]() -> LogicalResult {
      if (sourceAttemptHorizon <= 1 || branchingStateStorages.empty())
        return success();

      bool operandStrong = sequence.intrinsicEndStrong.value_or(!assertion);
      bool antecedentCanRemainPending = llvm::any_of(
          alternativeStates, [](Value storage) { return bool(storage); });
      bool operandPassPossible =
          followedBy ? !operandStrong
                     : !operandStrong || antecedentCanRemainPending;
      bool operandFailPossible =
          followedBy ? operandStrong || antecedentCanRemainPending
                     : operandStrong;
      bool passPossible =
          temporalNegation ? operandFailPossible : operandPassPossible;
      bool failPossible =
          temporalNegation ? operandPassPossible : operandFailPossible;
      bool emitPass = passPossible && passReport.has_value();
      bool emitFail = failPossible && failReport.has_value();
      if (!emitPass && !emitFail)
        return success();

      auto design = function->getParentOfType<sim::SimDesignOp>();
      if (!design)
        return function.emitError(
                   "branching antecedent finalization requires a simulation "
                   "design"),
               failure();

      Location finalLocation = getSemanticLocation(implication);
      StringRef strengthSpelling = operandStrong ? "strong" : "weak";
      function->setAttr("obelisk_sim.branching_antecedent_eos_coalescer",
                        builder.getUnitAttr());
      function->setAttr(
          "obelisk_sim.branching_antecedent_consequent_eos_strength",
          builder.getStringAttr(strengthSpelling));

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
      auto captureValue = [&](Value value) {
        auto [entry, inserted] =
            captureIndices.try_emplace(value, captures.size());
        if (inserted)
          captures.push_back(value);
        return entry->second;
      };

      SmallVector<std::optional<unsigned>> alternativeCaptureIndices;
      alternativeCaptureIndices.reserve(alternativeStates.size());
      for (Value storage : alternativeStates)
        alternativeCaptureIndices.push_back(
            storage ? std::optional<unsigned>(captureValue(storage))
                    : std::nullopt);
      std::optional<unsigned> matchedCaptureIndex;
      if (matchedState)
        matchedCaptureIndex = captureValue(matchedState);
      SmallVector<std::optional<unsigned>> consequentCaptureIndices;
      consequentCaptureIndices.reserve(consequentStates.size());
      for (Value storage : consequentStates)
        consequentCaptureIndices.push_back(
            storage ? std::optional<unsigned>(captureValue(storage))
                    : std::nullopt);

      struct FinalReportInfo {
        ReportCallback *report = nullptr;
        sim::SimFuncOp function;
        SmallVector<unsigned> captureIndices;
      };
      std::optional<FinalReportInfo> finalPass;
      std::optional<FinalReportInfo> finalFail;
      auto prepareReportCaptures = [&](ReportCallback &report) {
        FinalReportInfo info;
        info.report = &report;
        for (Value capture : report.captures)
          info.captureIndices.push_back(captureValue(capture));
        return info;
      };
      if (emitPass)
        finalPass = prepareReportCaptures(*passReport);
      if (emitFail)
        finalFail = prepareReportCaptures(*failReport);

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

      OpBuilder outlineBuilder(function);
      outlineBuilder.setInsertionPoint(function);
      auto outlineFinalReport = [&](FinalReportInfo &info,
                                    StringRef resultSpelling) {
        std::string symbol =
            (function.getSymName() + ".$concurrent_eos_branch_report." +
             Twine(node) + "." + resultSpelling)
                .str();
        std::string identity = (function.getSymName() +
                                ".$concurrent_eos_branch_report_identity." +
                                Twine(node) + "." + resultSpelling)
                                   .str();
        uint64_t codeUnitID = stableCodeUnitID(identity);
        std::string hierarchy =
            (Twine(parentHierarchy) + ".$concurrent_eos_branch_report." +
             Twine(node) + "." + resultSpelling)
                .str();
        sim::SimCodeUnitDeclOp::create(
            outlineBuilder, finalLocation, codeUnitID, scopeID,
            sim::EntryKind::Fork, outlineBuilder.getStringAttr(hierarchy),
            outlineBuilder.getStringAttr(
                "branching antecedent end-of-simulation report"),
            outlineBuilder.getUnitAttr());
        Operation *clonedReport = info.report->function->clone();
        info.function = cast<sim::SimFuncOp>(clonedReport);
        info.function.setSymName(symbol);
        info.function->setAttr("entry_kind",
                               sim::EntryKindAttr::get(function.getContext(),
                                                       sim::EntryKind::Fork));
        info.function->setAttr("home_region", sim::EventRegionAttr::get(
                                                  function.getContext(),
                                                  sim::EventRegion::Reactive));
        info.function->setAttr(
            "domain", sim::ExecutionDomainAttr::get(
                          function.getContext(), sim::ExecutionDomain::Design));
        info.function->setAttr("code_unit_id",
                               outlineBuilder.getI64IntegerAttr(codeUnitID));
        info.function->setAttr(sim::metadata::hierarchicalName,
                               outlineBuilder.getStringAttr(hierarchy));
        info.function->setAttr("obelisk_sim.concurrent_eos_report",
                               outlineBuilder.getUnitAttr());
        info.function->setAttr("obelisk_sim.branching_antecedent_eos_report",
                               outlineBuilder.getUnitAttr());
        outlineBuilder.insert(clonedReport);
      };
      if (finalPass)
        outlineFinalReport(*finalPass, "pass");
      if (finalFail)
        outlineFinalReport(*finalFail, "fail");

      std::string coordinatorSymbol =
          (function.getSymName() + ".$concurrent_eos_branch." + Twine(node))
              .str();
      std::string coordinatorIdentity =
          (function.getSymName() + ".$concurrent_eos_branch_identity." +
           Twine(node))
              .str();
      uint64_t coordinatorCodeUnitID = stableCodeUnitID(coordinatorIdentity);
      std::string coordinatorHierarchy =
          (Twine(parentHierarchy) + ".$concurrent_eos_branch." + Twine(node))
              .str();
      sim::SimCodeUnitDeclOp::create(
          outlineBuilder, finalLocation, coordinatorCodeUnitID, scopeID,
          sim::EntryKind::Final,
          outlineBuilder.getStringAttr(coordinatorHierarchy),
          outlineBuilder.getStringAttr(
              "branching antecedent end-of-simulation coordinator"),
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
          outlineBuilder, finalLocation, coordinatorSymbol,
          FunctionType::get(function.getContext(), inputs, TypeRange{}),
          sim::EntryKind::Final, attributes, argumentAttrs);
      SymbolTable::setSymbolVisibility(coordinator,
                                       SymbolTable::Visibility::Private);
      coordinator->setAttr("obelisk_sim.concurrent_eos_coordinator",
                           builder.getUnitAttr());
      coordinator->setAttr("obelisk_sim.branching_antecedent_eos_coalescer",
                           builder.getUnitAttr());
      coordinator->setAttr("obelisk_sim.detached_controls",
                           builder.getUnitAttr());

      Block *current = &coordinator.getBody().front();
      OpBuilder finalBuilder = OpBuilder::atBlockEnd(current);
      Value finalZero =
          arith::ConstantOp::create(finalBuilder, finalLocation, stateType,
                                    finalBuilder.getI64IntegerAttr(0));
      Value finalFalse = arith::ConstantOp::create(
          finalBuilder, finalLocation, finalBuilder.getI1Type(),
          finalBuilder.getBoolAttr(false));
      Value finalTrue = arith::ConstantOp::create(
          finalBuilder, finalLocation, finalBuilder.getI1Type(),
          finalBuilder.getBoolAttr(true));
      auto loadCapturedState = [&](std::optional<unsigned> index) -> Value {
        if (!index)
          return {};
        return sim::SimRefLoadOp::create(
            finalBuilder, finalLocation, stateType,
            coordinator.getBody().front().getArgument(*index));
      };
      SmallVector<Value> finalAlternativeStates;
      for (std::optional<unsigned> index : alternativeCaptureIndices)
        finalAlternativeStates.push_back(loadCapturedState(index));
      Value finalMatchedState = loadCapturedState(matchedCaptureIndex);
      SmallVector<Value> finalConsequentStates;
      for (std::optional<unsigned> index : consequentCaptureIndices)
        finalConsequentStates.push_back(loadCapturedState(index));

      auto bitIsSet = [&](Value state, uint64_t bit) -> Value {
        Value mask = arith::ConstantOp::create(
            finalBuilder, finalLocation, stateType,
            finalBuilder.getI64IntegerAttr(uint64_t{1} << bit));
        Value present =
            arith::AndIOp::create(finalBuilder, finalLocation, state, mask);
        return arith::CmpIOp::create(finalBuilder, finalLocation,
                                     arith::CmpIPredicate::ne, present,
                                     finalZero);
      };
      auto emitFinalReportWhen = [&](Value condition, FinalReportInfo &info,
                                     StringRef resultSpelling,
                                     uint64_t sourceAge) {
        Block *reportBlock = new Block;
        Block *continuation = new Block;
        coordinator.getBody().push_back(reportBlock);
        coordinator.getBody().push_back(continuation);
        finalBuilder.setInsertionPointToEnd(current);
        auto dispatch = cf::CondBranchOp::create(
            finalBuilder, finalLocation, condition, reportBlock, ValueRange{},
            continuation, ValueRange{});
        dispatch->setAttr("obelisk_sim.branching_antecedent_eos_result",
                          builder.getStringAttr(resultSpelling));
        dispatch->setAttr("obelisk_sim.branching_antecedent_eos_source_age",
                          builder.getI64IntegerAttr(sourceAge));

        OpBuilder reportBuilder = OpBuilder::atBlockEnd(reportBlock);
        SmallVector<Value> operands;
        for (auto [capture, index] :
             llvm::zip_equal(info.report->captures, info.captureIndices)) {
          Value argument = coordinator.getBody().front().getArgument(index);
          operands.push_back(argument);
          if (capture == disableEpoch || capture == killEpochStorage)
            operands.push_back(sim::SimRefLoadOp::create(
                reportBuilder, info.report->location, stateType, argument));
        }
        auto spawn = sim::SimSpawnOp::create(
            reportBuilder, info.report->location,
            info.function.getSymNameAttr(), operands, ArrayAttr{}, ArrayAttr{});
        spawn->setAttr("obelisk_sim.branching_antecedent_eos_result",
                       builder.getStringAttr(resultSpelling));
        spawn->setAttr("obelisk_sim.branching_antecedent_eos_source_age",
                       builder.getI64IntegerAttr(sourceAge));
        cf::BranchOp::create(reportBuilder, finalLocation, continuation);
        current = continuation;
      };

      // Stored state is already shifted for the next clock. Therefore a bit's
      // index is the source-attempt age seen by this phase-final process.
      for (uint64_t sourceAge = sourceAttemptHorizon; sourceAge-- > 1;) {
        finalBuilder.setInsertionPointToEnd(current);
        Value antecedentPending = finalFalse;
        for (auto [index, alternative] :
             llvm::enumerate(antecedentAlternatives)) {
          if (!finalAlternativeStates[index] ||
              sourceAge >= alternative.ages.size())
            continue;
          antecedentPending = arith::OrIOp::create(
              finalBuilder, finalLocation, antecedentPending,
              bitIsSet(finalAlternativeStates[index], sourceAge));
        }

        Value consequentPending = finalFalse;
        for (size_t channel = 0; channel < antecedentAlternatives.size();
             ++channel)
          for (size_t consequentIndex = 0;
               consequentIndex < consequentAlternativeCount;
               ++consequentIndex) {
            Value state = finalConsequentStates[consequentStateIndex(
                channel, consequentIndex)];
            if (!state)
              continue;
            uint64_t antecedentEndAge =
                antecedentAlternatives[channel].ages.size() - 1;
            bool channelNonoverlapped = channelIsNonoverlapped(channel);
            uint64_t sourceOffset =
                antecedentEndAge + (channelNonoverlapped ? 1 : 0);
            if (sourceAge < sourceOffset)
              continue;
            uint64_t bit = sourceAge - sourceOffset;
            uint64_t firstBit = channelNonoverlapped ? 0 : 1;
            if (bit < firstBit ||
                bit >= getConsequentAlternative(consequentIndex).ages.size())
              continue;
            consequentPending =
                arith::OrIOp::create(finalBuilder, finalLocation,
                                     consequentPending, bitIsSet(state, bit));
          }

        Value matched = finalMatchedState
                            ? bitIsSet(finalMatchedState, sourceAge)
                            : finalFalse;
        Value active = arith::OrIOp::create(
            finalBuilder, finalLocation, antecedentPending,
            arith::OrIOp::create(finalBuilder, finalLocation, consequentPending,
                                 matched));
        Value operandPassed;
        if (followedBy)
          operandPassed = operandStrong ? finalFalse : consequentPending;
        else
          operandPassed =
              operandStrong
                  ? arith::XOrIOp::create(finalBuilder, finalLocation,
                                          consequentPending, finalTrue)
                        .getResult()
                  : finalTrue;
        Value operandFailed = arith::XOrIOp::create(finalBuilder, finalLocation,
                                                    operandPassed, finalTrue);
        Value resultPassed = temporalNegation ? operandFailed : operandPassed;
        Value resultFailed = temporalNegation ? operandPassed : operandFailed;
        Value passCondition = arith::AndIOp::create(finalBuilder, finalLocation,
                                                    active, resultPassed);
        Value failCondition = arith::AndIOp::create(finalBuilder, finalLocation,
                                                    active, resultFailed);
        if (finalPass)
          emitFinalReportWhen(passCondition, *finalPass, "pass", sourceAge);
        if (finalFail)
          emitFinalReportWhen(failCondition, *finalFail, "fail", sourceAge);
      }
      finalBuilder.setInsertionPointToEnd(current);
      sim::SimReturnOp::create(finalBuilder, finalLocation, ValueRange{});

      sim::SimSpawnOp::create(builder, finalLocation,
                              coordinator.getSymNameAttr(), captures,
                              ArrayAttr{}, ArrayAttr{});
      return success();
    };
    if (failed(outlineBranchingAntecedentEndOfSimulation()))
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
    if (failed(cancelKilledSample(branchingStateStorages)))
      return failure();

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    Value attemptEnabled = queryAttemptEnabled();
    Value currentActionState = queryActionState();
    Value falseValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    Value newAttemptActive = attemptEnabled ? attemptEnabled : trueValue;
    SmallVector<Value> antecedentPendingNext(sourceAttemptHorizon, falseValue);
    SmallVector<Value> consequentActive(sourceAttemptHorizon, falseValue);
    SmallVector<Value> consequentPendingNext(sourceAttemptHorizon, falseValue);
    SmallVector<Value> consequentSucceeded(sourceAttemptHorizon, falseValue);
    SmallVector<Value> consequentFailed(sourceAttemptHorizon, falseValue);
    SmallVector<SmallVector<Value>> consequentChannelSucceeded(
        antecedentAlternatives.size(),
        SmallVector<Value>(sourceAttemptHorizon, falseValue));
    SmallVector<Value> antecedentMatched(sourceAttemptHorizon, falseValue);
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
    auto evaluateConsequentAge = [&](size_t consequentIndex,
                                     uint64_t age) -> FailureOr<Value> {
      if (combinedBooleanBranching) {
        assert(consequentIndex == 0 && age == 0 &&
               "combined Boolean consequent has one shared age");
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
      }
      const FixedSequence &consequent =
          getConsequentAlternative(consequentIndex);
      assert(age < consequent.ages.size() && "consequent age out of range");
      return evaluateAge(consequent.ages[age]);
    };
    auto reportWhen = [&](Value condition, bool passed, bool vacuous = false) {
      if (!observable)
        return;
      bool reportedPassed = temporalNegation ? !passed : passed;
      condition = gateActionResult(condition, currentActionState,
                                   reportedPassed, vacuous);
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
      starts[alternativeIndex] = gateNewAttempt(*start, attemptEnabled);
      if (alternative.ages.size() == 1)
        terminalMatches[alternativeIndex] = starts[alternativeIndex];

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

    for (auto [alternativeIndex, alternative] :
         llvm::enumerate(antecedentAlternatives)) {
      if (alternative.ages.size() == 1)
        continue;
      Value nextState = zero;
      Value enabled = applyFirstMatchPriority(starts[alternativeIndex],
                                              alternativeIndex, 0);
      antecedentPendingNext[0] = arith::OrIOp::create(
          builder, location, antecedentPendingNext[0], enabled);
      Value firstMask = arith::ConstantOp::create(builder, location, stateType,
                                                  builder.getI64IntegerAttr(2));
      nextState = arith::OrIOp::create(
          builder, location, nextState,
          arith::SelectOp::create(builder, location, enabled, firstMask, zero));
      for (uint64_t age = 1; age + 1 < alternative.ages.size(); ++age) {
        enabled = applyFirstMatchPriority(survives[alternativeIndex][age],
                                          alternativeIndex, age);
        antecedentPendingNext[age] = arith::OrIOp::create(
            builder, location, antecedentPendingNext[age], enabled);
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

    SmallVector<Value> nextConsequentStates(consequentStates.size(), zero);
    for (size_t channel = 0; channel < antecedentAlternatives.size();
         ++channel) {
      uint64_t antecedentEndAge =
          antecedentAlternatives[channel].ages.size() - 1;
      bool channelNonoverlapped = channelIsNonoverlapped(channel);
      SmallVector<Value> channelActive(sourceAttemptHorizon, falseValue);
      SmallVector<Value> channelSucceeded(sourceAttemptHorizon, falseValue);
      SmallVector<Value> channelPending(sourceAttemptHorizon, falseValue);
      for (size_t consequentIndex = 0;
           consequentIndex < consequentAlternativeCount; ++consequentIndex) {
        size_t stateIndex = consequentStateIndex(channel, consequentIndex);
        if (!consequentStates[stateIndex])
          continue;
        const FixedSequence &consequent =
            getConsequentAlternative(consequentIndex);
        Value state = sim::SimRefLoadOp::create(builder, location, stateType,
                                                consequentStates[stateIndex]);
        uint64_t firstAge = channelNonoverlapped ? 0 : 1;
        for (uint64_t age = firstAge; age < consequent.ages.size(); ++age) {
          uint64_t sourceAge =
              antecedentEndAge + (channelNonoverlapped ? 1 : 0) + age;
          assert(sourceAge < sourceAttemptHorizon);
          Value mask = arith::ConstantOp::create(
              builder, location, stateType,
              builder.getI64IntegerAttr(uint64_t{1} << age));
          Value present = arith::AndIOp::create(builder, location, state, mask);
          Value active = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::ne, present, zero);
          channelActive[sourceAge] = arith::OrIOp::create(
              builder, location, channelActive[sourceAge], active);
          FailureOr<Value> matches =
              evaluateConsequentAge(consequentIndex, age);
          if (failed(matches))
            return failure();
          Value advances =
              arith::AndIOp::create(builder, location, active, *matches);
          if (age + 1 == consequent.ages.size()) {
            channelSucceeded[sourceAge] = arith::OrIOp::create(
                builder, location, channelSucceeded[sourceAge], advances);
          } else {
            channelPending[sourceAge] = arith::OrIOp::create(
                builder, location, channelPending[sourceAge], advances);
            Value nextMask = arith::ConstantOp::create(
                builder, location, stateType,
                builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
            nextConsequentStates[stateIndex] = arith::OrIOp::create(
                builder, location, nextConsequentStates[stateIndex],
                arith::SelectOp::create(builder, location, advances, nextMask,
                                        zero));
          }
        }
      }
      for (uint64_t sourceAge = 0; sourceAge < sourceAttemptHorizon;
           ++sourceAge) {
        consequentChannelSucceeded[channel][sourceAge] =
            channelSucceeded[sourceAge];
        Value channelNotSucceeded = arith::XOrIOp::create(
            builder, location, channelSucceeded[sourceAge], trueValue);
        Value effectivePending = arith::AndIOp::create(
            builder, location, channelPending[sourceAge], channelNotSucceeded);
        consequentActive[sourceAge] =
            arith::OrIOp::create(builder, location, consequentActive[sourceAge],
                                 channelActive[sourceAge]);
        consequentSucceeded[sourceAge] = arith::OrIOp::create(
            builder, location, consequentSucceeded[sourceAge],
            channelSucceeded[sourceAge]);
        consequentPendingNext[sourceAge] = arith::OrIOp::create(
            builder, location, consequentPendingNext[sourceAge],
            effectivePending);
        Value channelResolved = arith::OrIOp::create(
            builder, location, channelSucceeded[sourceAge], effectivePending);
        Value channelFailed = arith::AndIOp::create(
            builder, location, channelActive[sourceAge],
            arith::XOrIOp::create(builder, location, channelResolved,
                                  trueValue));
        consequentFailed[sourceAge] = arith::OrIOp::create(
            builder, location, consequentFailed[sourceAge], channelFailed);
      }
    }

    auto markConsequentTrigger = [&](Operation *operation, size_t channel,
                                     size_t consequentIndex) {
      operation->setAttr("obelisk_sim.branching_antecedent_consequent_trigger",
                         builder.getUnitAttr());
      operation->setAttr("obelisk_sim.branching_antecedent_channel",
                         builder.getI64IntegerAttr(channel));
      operation->setAttr("obelisk_sim.branching_consequent_alternative",
                         builder.getI64IntegerAttr(consequentIndex));
    };
    for (auto [channel, triggered] : llvm::enumerate(terminalMatches)) {
      uint64_t sourceAge = antecedentAlternatives[channel].ages.size() - 1;
      antecedentMatched[sourceAge] = arith::OrIOp::create(
          builder, location, antecedentMatched[sourceAge], triggered);
      if (channelIsNonoverlapped(channel)) {
        consequentPendingNext[sourceAge] = arith::OrIOp::create(
            builder, location, consequentPendingNext[sourceAge], triggered);
        Value firstMask = arith::ConstantOp::create(
            builder, location, stateType, builder.getI64IntegerAttr(1));
        for (size_t consequentIndex = 0;
             consequentIndex < consequentAlternativeCount; ++consequentIndex) {
          size_t stateIndex = consequentStateIndex(channel, consequentIndex);
          auto launched = arith::SelectOp::create(builder, location, triggered,
                                                  firstMask, zero);
          markConsequentTrigger(launched, channel, consequentIndex);
          nextConsequentStates[stateIndex] = arith::OrIOp::create(
              builder, location, nextConsequentStates[stateIndex], launched);
        }
        continue;
      }
      Value startSucceeded = falseValue;
      Value startPending = falseValue;
      for (size_t consequentIndex = 0;
           consequentIndex < consequentAlternativeCount; ++consequentIndex) {
        const FixedSequence &consequent =
            getConsequentAlternative(consequentIndex);
        FailureOr<Value> consequentStart =
            evaluateConsequentAge(consequentIndex, 0);
        if (failed(consequentStart))
          return failure();
        Value matches = arith::AndIOp::create(builder, location, triggered,
                                              *consequentStart);
        markConsequentTrigger(matches.getDefiningOp(), channel,
                              consequentIndex);
        if (consequent.ages.size() == 1) {
          startSucceeded =
              arith::OrIOp::create(builder, location, startSucceeded, matches);
        } else {
          startPending =
              arith::OrIOp::create(builder, location, startPending, matches);
          Value nextMask = arith::ConstantOp::create(
              builder, location, stateType, builder.getI64IntegerAttr(2));
          size_t stateIndex = consequentStateIndex(channel, consequentIndex);
          nextConsequentStates[stateIndex] = arith::OrIOp::create(
              builder, location, nextConsequentStates[stateIndex],
              arith::SelectOp::create(builder, location, matches, nextMask,
                                      zero));
        }
      }
      consequentSucceeded[sourceAge] = arith::OrIOp::create(
          builder, location, consequentSucceeded[sourceAge], startSucceeded);
      consequentChannelSucceeded[channel][sourceAge] = startSucceeded;
      Value startNotSucceeded =
          arith::XOrIOp::create(builder, location, startSucceeded, trueValue);
      startPending = arith::AndIOp::create(builder, location, startPending,
                                           startNotSucceeded);
      consequentPendingNext[sourceAge] = arith::OrIOp::create(
          builder, location, consequentPendingNext[sourceAge], startPending);
      Value startResolved =
          arith::OrIOp::create(builder, location, startSucceeded, startPending);
      Value startFailed = arith::AndIOp::create(
          builder, location, triggered,
          arith::XOrIOp::create(builder, location, startResolved, trueValue));
      consequentFailed[sourceAge] = arith::OrIOp::create(
          builder, location, consequentFailed[sourceAge], startFailed);
    }

    Value priorMatchedState =
        matchedState ? sim::SimRefLoadOp::create(builder, location, stateType,
                                                 matchedState)
                     : zero;
    Value nextMatchedState = zero;
    SmallVector<Value> resultSucceeded(sourceAttemptHorizon, falseValue);
    SmallVector<Value> resultFailed(sourceAttemptHorizon, falseValue);
    SmallVector<Value> resultResolved(sourceAttemptHorizon, falseValue);
    SmallVector<Value> resultVacuous(sourceAttemptHorizon, falseValue);
    for (uint64_t age = 0; age < sourceAttemptHorizon; ++age) {
      Value priorMatched = falseValue;
      if (age != 0) {
        Value mask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << age));
        Value present =
            arith::AndIOp::create(builder, location, priorMatchedState, mask);
        priorMatched = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, present, zero);
      }
      Value active = age == 0 ? newAttemptActive : priorMatched;
      if (age < activeAny.size())
        active =
            arith::OrIOp::create(builder, location, active, activeAny[age]);
      active = arith::OrIOp::create(builder, location, active,
                                    consequentActive[age]);
      Value matched = arith::OrIOp::create(builder, location, priorMatched,
                                           antecedentMatched[age]);
      Value pending =
          arith::OrIOp::create(builder, location, antecedentPendingNext[age],
                               consequentPendingNext[age]);
      Value finished =
          arith::XOrIOp::create(builder, location, pending, trueValue);
      Value completed =
          arith::AndIOp::create(builder, location, active, finished);
      Value unmatched =
          arith::XOrIOp::create(builder, location, matched, trueValue);
      Value noAntecedentMatch =
          arith::AndIOp::create(builder, location, completed, unmatched);
      resultVacuous[age] = noAntecedentMatch;
      noAntecedentMatch.getDefiningOp()->setAttr(
          "obelisk_sim.branching_antecedent_vacuity", builder.getUnitAttr());

      if (followedBy) {
        // Followed-by is existential across all antecedent matches.  The
        // first successful consequent resolves the source attempt; failure is
        // known only when neither antecedent nor consequent can progress.
        resultSucceeded[age] = arith::AndIOp::create(builder, location, active,
                                                     consequentSucceeded[age]);
        Value notSucceeded = arith::XOrIOp::create(
            builder, location, consequentSucceeded[age], trueValue);
        Value matchedCompletion =
            arith::AndIOp::create(builder, location, completed, matched);
        Value matchedFailure = arith::AndIOp::create(
            builder, location, matchedCompletion, notSucceeded);
        resultFailed[age] = arith::OrIOp::create(
            builder, location, noAntecedentMatch, matchedFailure);
      } else {
        // Implication is universal across all antecedent matches.  One failed
        // consequent resolves the source attempt immediately; success is
        // known only after every possible match and obligation has finished.
        resultFailed[age] = arith::AndIOp::create(builder, location, active,
                                                  consequentFailed[age]);
        Value notFailed = arith::XOrIOp::create(
            builder, location, consequentFailed[age], trueValue);
        Value matchedCompletion =
            arith::AndIOp::create(builder, location, completed, matched);
        Value matchedSuccess = arith::AndIOp::create(
            builder, location, matchedCompletion, notFailed);
        resultSucceeded[age] = arith::OrIOp::create(
            builder, location, noAntecedentMatch, matchedSuccess);
      }
      resultSucceeded[age].getDefiningOp()->setAttr(
          followedBy ? "obelisk_sim.branching_antecedent_existential_success"
                     : "obelisk_sim.branching_antecedent_universal_success",
          builder.getUnitAttr());
      resultFailed[age].getDefiningOp()->setAttr(
          followedBy ? "obelisk_sim.branching_antecedent_existential_failure"
                     : "obelisk_sim.branching_antecedent_universal_failure",
          builder.getUnitAttr());
      resultResolved[age] = arith::OrIOp::create(
          builder, location, resultSucceeded[age], resultFailed[age]);

      if (age + 1 < sourceAttemptHorizon) {
        Value decisive =
            followedBy ? consequentSucceeded[age] : consequentFailed[age];
        Value notDecisive =
            arith::XOrIOp::create(builder, location, decisive, trueValue);
        Value retain = arith::AndIOp::create(
            builder, location, matched,
            arith::AndIOp::create(builder, location, pending, notDecisive));
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

    // Several source attempts of different ages may resolve on one clock.
    // Dispatch each once, oldest first, independently of the antecedent
    // alternative or consequent channel that produced the decisive result.
    for (uint64_t age = sourceAttemptHorizon; age-- > 0;) {
      if (shouldScheduleResult(false))
        reportWhen(resultFailed[age], false);
      if (shouldScheduleResult(true)) {
        if (actionControlled && !followedBy) {
          Value nonvacuous = arith::AndIOp::create(
              builder, location, resultSucceeded[age],
              arith::XOrIOp::create(builder, location, resultVacuous[age],
                                    trueValue));
          reportWhen(nonvacuous, true, /*vacuous=*/false);
          reportWhen(resultVacuous[age], true, /*vacuous=*/true);
        } else {
          reportWhen(resultSucceeded[age], true);
        }
      }
    }

    auto clearBitWhen = [&](Value state, uint64_t bit, Value condition,
                            StringRef reason) {
      assert(bit < 64);
      Value keepMask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(
              static_cast<int64_t>(~(uint64_t{1} << bit))));
      Value cleared = arith::AndIOp::create(builder, location, state, keepMask);
      cleared.getDefiningOp()->setAttr(reason, builder.getUnitAttr());
      auto selected =
          arith::SelectOp::create(builder, location, condition, cleared, state);
      selected->setAttr(reason, builder.getUnitAttr());
      return selected.getResult();
    };
    auto clearResolvedBit = [&](Value state, uint64_t bit, uint64_t sourceAge) {
      assert(sourceAge < sourceAttemptHorizon);
      return clearBitWhen(state, bit, resultResolved[sourceAge],
                          "obelisk_sim.branching_antecedent_result_cancel");
    };

    for (auto [index, storage] : llvm::enumerate(alternativeStates)) {
      if (!storage)
        continue;
      Value nextState = nextAlternativeStates[index];
      for (uint64_t age = 0;
           age + 1 < antecedentAlternatives[index].ages.size(); ++age)
        nextState = clearResolvedBit(nextState, age + 1, age);
      sim::SimRefStoreOp::create(builder, location, nextState, storage);
    }
    if (matchedState)
      sim::SimRefStoreOp::create(builder, location, nextMatchedState,
                                 matchedState);
    if (consequentNeedsState) {
      for (size_t channel = 0; channel < antecedentAlternatives.size();
           ++channel)
        for (size_t consequentIndex = 0;
             consequentIndex < consequentAlternativeCount; ++consequentIndex) {
          size_t stateIndex = consequentStateIndex(channel, consequentIndex);
          Value storage = consequentStates[stateIndex];
          if (!storage)
            continue;
          Value nextState = nextConsequentStates[stateIndex];
          uint64_t antecedentEndAge =
              antecedentAlternatives[channel].ages.size() - 1;
          bool channelNonoverlapped = channelIsNonoverlapped(channel);
          uint64_t firstBit = channelNonoverlapped ? 0 : 1;
          const FixedSequence &consequent =
              getConsequentAlternative(consequentIndex);
          for (uint64_t bit = firstBit; bit < consequent.ages.size(); ++bit) {
            uint64_t sourceAge =
                antecedentEndAge + (channelNonoverlapped ? 1 : 0) + bit - 1;
            nextState = clearResolvedBit(nextState, bit, sourceAge);
            nextState = clearBitWhen(
                nextState, bit, consequentChannelSucceeded[channel][sourceAge],
                "obelisk_sim.branching_consequent_alternative_cancel");
          }
          sim::SimRefStoreOp::create(builder, location, nextState, storage);
        }
    }
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
    size_t vacuousAlternatives =
        llvm::count_if(alternatives, [](const FixedSequence &alternative) {
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
    if (branchingConsequent) {
      // A sequence consequent is weak by default for assert/assume and strong
      // for cover/restrict.  Use the implication as the semantic source when
      // no explicit outer strength/negation already supplied one so the
      // shared finalizer unions alternative words by relative source age.
      if (!endStrengthSource)
        endStrengthSource = implication.getOperation();
      function->setAttr("obelisk_sim.branching_consequent_eos_coalescer",
                        builder.getUnitAttr());
      if (consequentUniformIntrinsicEndStrong)
        function->setAttr(
            "obelisk_sim.branching_consequent_intrinsic_eos_strength",
            builder.getStringAttr(
                *consequentUniformIntrinsicEndStrong ? "strong" : "weak"));
    }
    if ((branchingSequence || branchingConsequent) &&
        failed(outlineEndOfSimulationReports(
            branchingStateStorages, horizon,
            branchingConsequent && nonoverlapped ? 0 : 1,
            branchingConsequent ? consequentUniformIntrinsicEndStrong
                                : std::nullopt)))
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
    if (failed(cancelKilledSample(branchingStateStorages)))
      return failure();

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    Value attemptEnabled = queryAttemptEnabled();
    Value currentActionState = queryActionState();
    Value falseValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    Value newAttemptActive = attemptEnabled ? attemptEnabled : trueValue;
    SmallVector<Value> activeAny(horizon, falseValue);
    SmallVector<Value> successAny(horizon, falseValue);
    SmallVector<Value> nonvacuousSuccessAny(horizon, falseValue);
    SmallVector<Value> vacuousSuccessAny(horizon, falseValue);
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

    Value rawAntecedentTrigger = trueValue;
    Value antecedentTrigger = trueValue;
    if (branchingConsequent) {
      FailureOr<Value> trigger = evaluateAge(antecedentSequence.ages.front());
      if (failed(trigger))
        return failure();
      rawAntecedentTrigger = *trigger;
      antecedentTrigger = gateNewAttempt(*trigger, attemptEnabled);
    }

    for (auto [alternativeIndex, alternative] : llvm::enumerate(alternatives)) {
      survives[alternativeIndex].resize(alternative.ages.size(), falseValue);
      uint64_t firstActiveAge = branchingConsequent && nonoverlapped ? 0 : 1;
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
        } else
          enabled = gateNewAttempt(enabled, attemptEnabled);
        starts[alternativeIndex] = enabled;
        if (alternative.ages.size() == 1) {
          successAny[0] =
              arith::OrIOp::create(builder, location, successAny[0], enabled);
          SmallVector<Value> &classified = alternative.vacuousSuccess
                                               ? vacuousSuccessAny
                                               : nonvacuousSuccessAny;
          classified[0] =
              arith::OrIOp::create(builder, location, classified[0], enabled);
        } else
          continueAny[0] =
              arith::OrIOp::create(builder, location, continueAny[0], enabled);
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
          SmallVector<Value> &classified = alternative.vacuousSuccess
                                               ? vacuousSuccessAny
                                               : nonvacuousSuccessAny;
          classified[age] = arith::OrIOp::create(builder, location,
                                                 classified[age], advances);
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
    for (auto [alternativeIndex, alternative] : llvm::enumerate(alternatives)) {
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

    for (auto [alternativeIndex, alternative] : llvm::enumerate(alternatives)) {
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

    auto reportWhen = [&](Value condition, bool passed, bool vacuous = false) {
      if (!observable)
        return;
      bool reportedPassed = temporalNegation ? !passed : passed;
      condition = gateActionResult(condition, currentActionState,
                                   reportedPassed, vacuous);
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
        reportWhen(matched, true, alternative.vacuousSuccess);
      }
    } else {
      if (branchingConsequent && shouldScheduleResult(!followedBy)) {
        Value noAntecedent = arith::XOrIOp::create(
            builder, location, rawAntecedentTrigger, trueValue);
        noAntecedent = gateNewAttempt(noAntecedent, attemptEnabled);
        reportWhen(noAntecedent, !followedBy, /*vacuous=*/true);
      }
      for (size_t age = 1; age < horizon; ++age) {
        reportWhen(successAny[age], true);
        Value finished = arith::OrIOp::create(
            builder, location, successAny[age], continueAny[age]);
        Value failedAttempt = arith::AndIOp::create(
            builder, location, activeAny[age],
            arith::XOrIOp::create(builder, location, finished, trueValue));
        reportWhen(failedAttempt, false);
      }
      if (actionControlled) {
        reportWhen(nonvacuousSuccessAny[0], true,
                   /*vacuous=*/false);
        Value onlyVacuous = arith::AndIOp::create(
            builder, location, vacuousSuccessAny[0],
            arith::XOrIOp::create(builder, location, nonvacuousSuccessAny[0],
                                  trueValue));
        reportWhen(onlyVacuous, true, /*vacuous=*/true);
      } else {
        reportWhen(successAny[0], true);
      }
      Value startFinished = arith::OrIOp::create(builder, location,
                                                 successAny[0], continueAny[0]);
      Value startActive =
          branchingConsequent
              ? (nonoverlapped ? activeAny[0] : antecedentTrigger)
              : newAttemptActive;
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
    // age. Keep one typed cell per (local, consequent age) and per live
    // antecedent age after age zero. Processing ages from oldest to youngest
    // below prevents a newly advanced value from overwriting the value
    // consumed by an older attempt in the same Observed region.
    size_t consequentLocalAgeCount = sequence.ages.size();
    size_t antecedentLocalAgeCount =
        implication ? antecedentSequence.ages.size() - 1 : 0;
    size_t localAgeCount = consequentLocalAgeCount + antecedentLocalAgeCount;
    auto antecedentLocalAge = [&](uint64_t age) {
      assert(implication && age > 0 && age < antecedentSequence.ages.size());
      return consequentLocalAgeCount + age - 1;
    };
    for (LocalState &local : locals) {
      Value initial = createDefaultValue(builder, location, local.type);
      if (!initial)
        return emitError(location)
                   << "cannot materialize assertion local type " << local.type,
               failure();
      for (size_t age = 0; age < localAgeCount; ++age)
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
          guardReactiveCallback(
              callback->first, callback->second, getSemanticLocation(item),
              "obelisk_sim.concurrent_match_call_kill_epoch");
          SmallVector<Value> captures = materializeReactiveCallbackCaptures(
              callback->second, getSemanticLocation(item));
          sim::SimSpawnOp::create(builder, getSemanticLocation(item),
                                  callback->first.getSymNameAttr(),
                                  captures, ArrayAttr{}, ArrayAttr{});
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
    auto reportAntecedentFailure =
        [&](Value enabled, Value matches) -> LogicalResult {
      if (!shouldScheduleResult(!followedBy))
        return success();
      Value fails = arith::AndIOp::create(
          builder, location, enabled,
          arith::XOrIOp::create(
              builder, location, matches,
              arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(true))));
      fails.getDefiningOp()->setAttr(
          "obelisk_sim.implication_antecedent_failure",
          builder.getUnitAttr());
      if (!observable)
        return success();
      Block *report = addBlock();
      Block *continuation = addBlock();
      cf::CondBranchOp::create(builder, location, fails, report, ValueRange{},
                               continuation, ValueRange{});
      setCurrent(report);
      scheduleResult(!followedBy);
      emitBranch(continuation);
      setCurrent(continuation);
      return success();
    };

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
    Value state;
    if (!killEpochStorage)
      state = stateStorage ? sim::SimRefLoadOp::create(builder, location,
                                                       stateType, stateStorage)
                           : zero;
    if (failed(cancelDisabledSample(wait, {stateStorage})))
      return failure();
    if (failed(cancelKilledSample({stateStorage})))
      return failure();
    if (killEpochStorage)
      state = stateStorage ? sim::SimRefLoadOp::create(builder, location,
                                                       stateType, stateStorage)
                           : zero;

    bool savedSampleAssertionValues = sampleAssertionValues;
    sampleAssertionValues = true;
    Operation *savedSampledClock = activeSampledClock;
    activeSampledClock = clock;
    llvm::scope_exit restoreSampling([&] {
      sampleAssertionValues = savedSampleAssertionValues;
      activeSampledClock = savedSampledClock;
    });

    Value nextState = zero;
    auto launchConsequent =
        [&](Value triggered, SmallVector<Value> localValues,
            Value baseState) -> FailureOr<Value> {
      if (nonoverlapped) {
        storeLocals(0, localValues);
        Value startMask = arith::ConstantOp::create(
            builder, location, stateType, builder.getI64IntegerAttr(1));
        Value started = arith::SelectOp::create(builder, location, triggered,
                                                startMask, zero);
        return arith::OrIOp::create(builder, location, baseState, started)
            .getResult();
      }

      FailureOr<Value> first =
          evaluatePredicates(sequence.ages.front(), localValues);
      if (failed(first) || failed(reportFailure(triggered, *first)))
        return failure();
      Value matched =
          arith::AndIOp::create(builder, location, triggered, *first);
      Block *firstMatched = addBlock();
      Block *afterFirst = addBlock();
      afterFirst->addArgument(stateType, location);
      cf::CondBranchOp::create(builder, location, matched, firstMatched,
                               ValueRange{}, afterFirst,
                               ValueRange{baseState});
      setCurrent(firstMatched);
      FailureOr<SmallVector<Value>> updated =
          applyMatchItems(sequence.ages.front().matchItems, localValues);
      if (failed(updated))
        return failure();
      Value matchedState = baseState;
      if (sequence.ages.size() == 1) {
        reportSuccess();
      } else {
        storeLocals(1, *updated);
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType, builder.getI64IntegerAttr(2));
        matchedState =
            arith::OrIOp::create(builder, location, baseState, nextMask);
      }
      cf::BranchOp::create(builder, location, afterFirst,
                           ValueRange{matchedState});
      setCurrent(afterFirst);
      return afterFirst->getArgument(0);
    };

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

    // Antecedent attempts use the high portion of the same state word as the
    // ordinary deterministic monitor. Local values travel in parallel typed
    // cells. Process the oldest antecedent first so a same-clock terminal can
    // launch the consequent without clobbering an older consequent attempt.
    if (implication && antecedentSequence.ages.size() > 1) {
      uint64_t antecedentBase = sequence.ages.size();
      for (uint64_t cursor = antecedentSequence.ages.size(); cursor-- > 1;) {
        uint64_t age = cursor;
        Value mask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << (antecedentBase + age)));
        Value presentBits =
            arith::AndIOp::create(builder, location, state, mask);
        Value active = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, presentBits, zero);
        SmallVector<Value> ageLocals = loadLocals(antecedentLocalAge(age));
        FailureOr<Value> matches =
            evaluatePredicates(antecedentSequence.ages[age], ageLocals);
        if (failed(matches) ||
            failed(reportAntecedentFailure(active, *matches)))
          return failure();
        Value advances =
            arith::AndIOp::create(builder, location, active, *matches);
        advances.getDefiningOp()->setAttr("obelisk_sim.implication_antecedent",
                                          builder.getUnitAttr());
        Block *matched = addBlock();
        Block *continued = addBlock();
        continued->addArgument(stateType, location);
        cf::CondBranchOp::create(builder, location, advances, matched,
                                 ValueRange{}, continued,
                                 ValueRange{nextState});
        setCurrent(matched);
        FailureOr<SmallVector<Value>> updated = applyMatchItems(
            antecedentSequence.ages[age].matchItems, ageLocals);
        if (failed(updated))
          return failure();
        Value advancedState = nextState;
        if (age + 1 == antecedentSequence.ages.size()) {
          FailureOr<Value> launched =
              launchConsequent(advances, *updated, nextState);
          if (failed(launched))
            return failure();
          advancedState = *launched;
        } else {
          storeLocals(antecedentLocalAge(age + 1), *updated);
          Value nextMask = arith::ConstantOp::create(
              builder, location, stateType,
              builder.getI64IntegerAttr(uint64_t{1}
                                        << (antecedentBase + age + 1)));
          advancedState =
              arith::OrIOp::create(builder, location, nextState, nextMask);
        }
        cf::BranchOp::create(builder, location, continued,
                             ValueRange{advancedState});
        setCurrent(continued);
        nextState = continued->getArgument(0);
      }
    }

    Block *afterStart = addBlock();
    afterStart->addArgument(stateType, location);
    if (Value enabled = queryAttemptEnabled()) {
      Block *evaluateStart = addBlock();
      cf::CondBranchOp::create(builder, location, enabled, evaluateStart,
                               ValueRange{}, afterStart, ValueRange{nextState});
      setCurrent(evaluateStart);
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
    if ((!implication && failed(reportFailure(one, *starts))) ||
        (implication && failed(reportAntecedentFailure(one, *starts))))
      return failure();

    Block *startMatched = addBlock();
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
    Value startedState = nextState;
    if (implication && antecedentSequence.ages.size() > 1) {
      storeLocals(antecedentLocalAge(1), startLocals);
      uint64_t antecedentBit = sequence.ages.size() + 1;
      Value startMask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1} << antecedentBit));
      startedState =
          arith::OrIOp::create(builder, location, nextState, startMask);
    } else if (implication) {
      FailureOr<Value> launched =
          launchConsequent(*starts, startLocals, nextState);
      if (failed(launched))
        return failure();
      startedState = *launched;
    } else {
      FailureOr<SmallVector<Value>> updated =
          applyMatchItems(sequence.ages.front().matchItems, startLocals);
      if (failed(updated))
        return failure();
      if (sequence.ages.size() == 1) {
        reportSuccess();
      } else {
        storeLocals(1, *updated);
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType, builder.getI64IntegerAttr(2));
        startedState =
            arith::OrIOp::create(builder, location, nextState, nextMask);
      }
    }
    cf::BranchOp::create(builder, location, afterStart,
                         ValueRange{startedState});
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
  if (stateStorage && !killEpochStorage)
    state =
        sim::SimRefLoadOp::create(builder, location, stateType, stateStorage);
  if (failed(cancelDisabledSample(wait, {stateStorage})))
    return failure();
  if (failed(cancelKilledSample({stateStorage})))
    return failure();
  if (stateStorage && killEpochStorage)
    state =
        sim::SimRefLoadOp::create(builder, location, stateType, stateStorage);

  bool savedSampleAssertionValues = sampleAssertionValues;
  sampleAssertionValues = true;
  Operation *savedSampledClock = activeSampledClock;
  activeSampledClock = clock;
  llvm::scope_exit restoreSampling([&] {
    sampleAssertionValues = savedSampleAssertionValues;
    activeSampledClock = savedSampledClock;
  });

  Value attemptEnabled = queryAttemptEnabled();
  Value currentActionState = queryActionState();
  llvm::DenseMap<Operation *, Value> predicateCache;
  auto conditionalResult = [&](Value condition, bool passed,
                               bool alreadyReported = false,
                               bool vacuous = false) -> LogicalResult {
    if (!observable)
      return success();
    bool reportedPassed =
        alreadyReported ? passed : (temporalNegation ? !passed : passed);
    condition = gateActionResult(condition, currentActionState, reportedPassed,
                                 vacuous);
    Block *report = addBlock();
    Block *continuation = addBlock();
    cf::CondBranchOp::create(builder, location, condition, report, ValueRange{},
                             continuation, ValueRange{});
    setCurrent(report);
    if (alreadyReported)
      scheduleReportedResult(passed);
    else
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
    // All four abort operators use the ordinary assertion-sampled value of
    // their Boolean condition. Async versus sync controls whether additional
    // time slots are observed, not the value domain at a property clock.
    FailureOr<Value> condition = lowerExpression(abortCondition);
    if (failed(condition))
      return failure();
    FailureOr<Value> sampled =
        truthValue(*condition, getSemanticLocation(abortCondition));
    if (failed(sampled))
      return failure();
    Value aborts = *sampled;
    bool accepted =
        abort.getAction() == semantic::SVAssertionAbortAction::Accept;
    bool reportedPassed = temporalNegationOutsideAbort ? !accepted : accepted;
    Block *aborted = addBlock();
    Block *evaluate = addBlock();
    cf::CondBranchOp::create(builder, location, aborts, aborted, ValueRange{},
                             evaluate, ValueRange{});
    setCurrent(aborted);
    // Every live state bit represents one independent property attempt. Abort
    // each exactly once, then include the attempt that starts on this clock.
    // The abort result is vacuous and an enclosing temporal negation flips
    // its truth. Cover-property pass actions execute for every resulting
    // success, including the attempt starting now.
    if (shouldScheduleReportedResult(reportedPassed)) {
      for (uint64_t age = 1; age < sequence.ages.size(); ++age) {
        Value mask = arith::ConstantOp::create(
            builder, location, stateType,
            builder.getI64IntegerAttr(uint64_t{1} << age));
        Value presentBits =
            arith::AndIOp::create(builder, location, state, mask);
        Value active = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, presentBits, zero);
        if (failed(conditionalResult(active, reportedPassed,
                                     /*alreadyReported=*/true,
                                     /*vacuous=*/true)))
          return failure();
      }
      Value current =
          attemptEnabled ? attemptEnabled
                         : arith::ConstantOp::create(builder, location,
                                                     builder.getI1Type(),
                                                     builder.getBoolAttr(true));
      if (failed(conditionalResult(current, reportedPassed,
                                   /*alreadyReported=*/true,
                                   /*vacuous=*/true)))
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
      if (failed(conditionalResult(advances, true,
                                   /*alreadyReported=*/false,
                                   sequence.vacuousSuccess)))
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
      return conditionalResult(matched, true,
                               /*alreadyReported=*/false,
                               sequence.vacuousSuccess);
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
      if (shouldScheduleResult(!followedBy) &&
          failed(conditionalResult(vacuous, !followedBy,
                                   /*alreadyReported=*/false,
                                   /*vacuous=*/true)))
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
  Value activeStart = gateNewAttempt(*starts, attemptEnabled);

  if (implication) {
    Value vacuous = arith::XOrIOp::create(
        builder, location, *starts,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    vacuous = gateNewAttempt(vacuous, attemptEnabled);
    // Implication succeeds vacuously when its antecedent is false. Followed-by
    // instead fails vacuously; temporal negation turns that result into a
    // vacuous success whose cover pass action must still execute.
    if (shouldScheduleResult(!followedBy) &&
        failed(conditionalResult(vacuous, !followedBy,
                                 /*alreadyReported=*/false,
                                 /*vacuous=*/true)))
      return failure();
    if (antecedentSequence.ages.size() == 1) {
      if (failed(launchConsequent(activeStart)))
        return failure();
    } else {
      uint64_t firstAntecedentAge = sequence.ages.size() + 1;
      Value firstMask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1} << firstAntecedentAge));
      Value started = arith::SelectOp::create(builder, location, activeStart,
                                              firstMask, zero);
      nextState = arith::OrIOp::create(builder, location, nextState, started);
    }
  } else {
    // The age-zero attempt is evaluated directly every clock. Older attempts
    // are represented by the state bits handled above.
    Value failedStart = arith::XOrIOp::create(
        builder, location, *starts,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    failedStart = gateNewAttempt(failedStart, attemptEnabled);
    if (failed(conditionalResult(failedStart, false)))
      return failure();
    if (sequence.ages.size() == 1) {
      if (failed(conditionalResult(activeStart, true,
                                   /*alreadyReported=*/false,
                                   sequence.vacuousSuccess)))
        return failure();
    } else {
      Value nextMask = arith::ConstantOp::create(builder, location, stateType,
                                                 builder.getI64IntegerAttr(2));
      Value started = arith::SelectOp::create(builder, location, activeStart,
                                              nextMask, zero);
      nextState = arith::OrIOp::create(builder, location, nextState, started);
    }
  }

  if (stateStorage)
    sim::SimRefStoreOp::create(builder, location, nextState, stateStorage);
  cf::BranchOp::create(builder, location, wait);
  return success();
}

} // namespace obelisk::simlowering
