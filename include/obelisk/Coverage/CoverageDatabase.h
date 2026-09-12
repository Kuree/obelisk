//===- CoverageDatabase.h - native Obelisk coverage database ---*- C++ -*-===//

#ifndef OBELISK_COVERAGE_COVERAGEDATABASE_H
#define OBELISK_COVERAGE_COVERAGEDATABASE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "obelisk/Coverage/CoverageFormatDecls.h.inc"

namespace obelisk::coverage {

using Digest = std::array<uint8_t, 32>;
using UUID = std::array<uint8_t, 16>;

inline constexpr uint8_t Magic[8] = {'O', 'B', 'C', 'O', 'V', '\r', '\n', 0x1a};

/// Maximum finite source-occurrence domain materialized for one Clause 19.5.1
/// `with` predicate. The compiler and runtime share this exact v1 resource
/// bound so constructor evaluation cannot create an unbounded expression
/// batch.
inline constexpr uint32_t MaxFunctionalWithCandidates = 4096;

/// Maximum value-tuple domain materialized for one Clause 19.6.1.2 cross
/// selector `with` predicate. This is an implementation resource bound in the
/// sole mutable v1 model, not a format revision boundary.
inline constexpr uint32_t MaxFunctionalCrossWithCandidates = 4096;

enum class Status : uint32_t {
  Ok,
  InvalidArgument,
  IoError,
  OutOfMemory,
  LimitExceeded,
  BadMagic,
  VersionMismatch,
  Truncated,
  IntegerOverflow,
  Misaligned,
  SectionOverlap,
  MissingSection,
  DuplicateSection,
  BadRecordSize,
  BadCount,
  InvalidEnum,
  InvalidUtf8,
  InvalidReference,
  UnsortedOrDuplicate,
  ChecksumMismatch,
  FingerprintMismatch,
  SchemaMismatch,
  DuplicateRun,
  InvalidDatabase,
};

struct Diagnostic {
  Status status = Status::Ok;
  uint64_t offset = 0;
  SectionKind section = SectionKind::Strings;
  uint64_t record = UINT64_MAX;
  const char *field = nullptr;
  std::string detail;
};

struct ParseLimits {
  uint64_t maxImageBytes = UINT64_C(1) << 34;
  uint64_t maxSectionBytes = UINT64_C(1) << 32;
  uint64_t maxRecords = UINT64_C(1) << 28;
  uint64_t maxStrings = UINT64_C(1) << 26;
  uint64_t maxStringBytes = UINT64_C(1) << 30;
};

/// Return whether a database text field is canonical UTF-8. This uses the
/// exact validation accepted by the v1 parser and handwritten validator.
bool isValidUtf8(const std::string &text);

struct SourceFile {
  uint64_t id = 0;
  std::string path;
  Digest digest{};
};

struct Scope {
  uint64_t id = 0;
  uint64_t parent = 0;
  std::string name;
  uint32_t kind = 0;
  std::string definition;
};

struct LinePoint {
  uint64_t id = 0;
  uint64_t file = 0;
  uint64_t endFile = 0;
  uint64_t scope = 0;
  std::string macroName;
  uint32_t line = 0;
  uint32_t column = 0;
  uint32_t endLine = 0;
  uint32_t endColumn = 0;
  uint32_t semanticPhase = 0;
  uint32_t flags = 0;
};

struct ToggleObject {
  uint64_t id = 0;
  uint64_t scope = 0;
  uint64_t file = 0;
  std::string name;
  uint32_t typeRoot = UINT32_MAX;
  uint64_t bitWidth = 0;
  uint32_t line = 0;
  uint32_t column = 0;
  uint32_t endLine = 0;
  uint32_t endColumn = 0;
  uint32_t flags = 0;
};

inline constexpr uint32_t ToggleObjectFourState = uint32_t{1} << 0;

struct ToggleDimension {
  uint64_t object = 0;
  uint32_t parent = UINT32_MAX;
  ToggleDimensionKind kind = ToggleDimensionKind::Root;
  /// The first descendant and total number of descendant records in canonical
  /// preorder. Direct children are recovered from their parent fields.
  uint32_t firstChild = UINT32_MAX;
  uint32_t childCount = 0;
  int64_t left = 0;
  int64_t right = 0;
  uint64_t bitOffset = 0;
  uint64_t bitWidth = 0;
  std::string name;
  uint32_t flags = 0;
};

struct FunctionalType {
  uint64_t id = 0;
  uint64_t scope = 0;
  std::string name;
  uint32_t flags = 0;
  uint32_t languageVersion = 2017;
  std::string hierarchy;
};

struct FunctionalItem {
  uint64_t id = 0;
  uint64_t type = 0;
  std::string name;
  FunctionalItemKind kind = FunctionalItemKind::Coverpoint;
  uint32_t flags = 0;
  uint32_t goal = 100;
  uint32_t weight = 1;
  uint32_t ordinal = 0;
  std::string hierarchy;
};

/// Retained only as a sampling target for an inherited cross after a derived
/// coverpoint with the same name replaces it in group aggregation.
inline constexpr uint32_t FunctionalItemNonAggregating = uint32_t{1} << 0;

struct FunctionalBin {
  uint64_t id = 0;
  uint64_t item = 0;
  std::string name;
  FunctionalBinKind kind = FunctionalBinKind::State;
  uint32_t flags = 0;
  uint64_t atLeast = 1;
  uint32_t ordinal = 0;
  std::string hierarchy;
};

inline constexpr uint32_t FunctionalBinDefault = uint32_t{1} << 0;
inline constexpr uint32_t FunctionalBinDefaultSequence = uint32_t{1} << 1;
inline constexpr uint32_t FunctionalBinIgnore = uint32_t{1} << 2;
inline constexpr uint32_t FunctionalBinIllegal = uint32_t{1} << 3;
inline constexpr uint32_t FunctionalBinWildcard = uint32_t{1} << 4;
inline constexpr uint32_t FunctionalBinAutomatic = uint32_t{1} << 6;
inline constexpr uint32_t FunctionalBinEmpty = uint32_t{1} << 7;

struct FunctionalValueSet {
  uint64_t id = 0;
  uint64_t item = 0;
  uint32_t firstAtom = 0;
  uint32_t atomCount = 0;
  uint32_t bitWidth = 0;
  FunctionalValueSetKind kind = FunctionalValueSetKind::Integral;
  uint32_t flags = 0;
  CoverageSignedness signedness = CoverageSignedness::Unsigned;
  /// Whole-set constructor expression when the atom list itself is deferred.
  uint64_t setExpression = 0;
};

inline constexpr uint32_t FunctionalValueSetNeedsResolution = uint32_t{1} << 0;

struct FunctionalValueAtom {
  uint64_t valueSet = 0;
  uint64_t realLowBits = 0;
  uint64_t realHighBits = 0;
  uint32_t firstLimb = 0;
  uint32_t limbCount = 0;
  uint32_t ordinal = 0;
  FunctionalValueAtomKind kind = FunctionalValueAtomKind::IntegralValue;
  uint32_t flags = 0;
  uint64_t lowerExpression = 0;
  uint64_t upperExpression = 0;
};

inline constexpr uint32_t FunctionalValueAtomLowerInclusive = uint32_t{1} << 0;
inline constexpr uint32_t FunctionalValueAtomUpperInclusive = uint32_t{1} << 1;
/// The unresolved real interval stores a center in lowerExpression and an
/// absolute or percentage tolerance in upperExpression.  Resolution expands
/// the pair to ordinary inclusive endpoints and clears these definition-only
/// flags before persisting the resolved schema.
inline constexpr uint32_t FunctionalValueAtomAbsoluteTolerance = uint32_t{1}
                                                                 << 2;
inline constexpr uint32_t FunctionalValueAtomRelativeTolerance = uint32_t{1}
                                                                 << 3;
/// IEEE 1800 permits `$` as either endpoint of a covergroup value range.  An
/// unresolved integral range omits the corresponding expression and is
/// materialized to the effective type limit when an instance is constructed;
/// resolved integral atoms therefore clear these flags.  A real range keeps
/// the corresponding bit field at its canonical zero value and carries the
/// unbounded meaning in both definition and resolved records.
inline constexpr uint32_t FunctionalValueAtomLowerUnbounded = uint32_t{1} << 4;
inline constexpr uint32_t FunctionalValueAtomUpperUnbounded = uint32_t{1} << 5;
/// Preserve whether a real atom was written as a range (including a tolerance
/// range), rather than as an individual value.  IEEE 1800-2023 gives these two
/// forms different names when an open bin array is resolved.
inline constexpr uint32_t FunctionalValueAtomRealRange = uint32_t{1} << 6;

/// One little-endian 64-bit limb of a resolved integral value or range.
/// The aval/bval encoding distinguishes 0=(0,0), 1=(1,0), X=(0,1), and
/// Z=(1,1). wildcardMask marks definition bits that match either 0 or 1.
struct FunctionalValueLimb {
  uint64_t valueSet = 0;
  uint32_t atomOrdinal = 0;
  uint32_t ordinal = 0;
  uint64_t lowAval = 0;
  uint64_t lowBval = 0;
  uint64_t highAval = 0;
  uint64_t highBval = 0;
  uint64_t wildcardMask = 0;
};

struct FunctionalExpression {
  /// This is a compiler-evaluated interface descriptor, not a serialized AST.
  /// The generated constructor/sample/event/option helper identified by
  /// evaluationPhase and resultOrdinal evaluates the expression. The database
  /// stores only its stable semantic identity and the canonical resolved
  /// result. Formal declarations and default-expression identities are stored
  /// in Database::functionalFormals. Captured argument values remain in typed
  /// MLIR/helper ABI data, and inherited plans are flattened here with their
  /// inherited stable member IDs.
  uint64_t id = 0;
  uint64_t owner = 0;
  FunctionalExpressionOwnerKind ownerKind = FunctionalExpressionOwnerKind::Type;
  FunctionalExpressionRole role = FunctionalExpressionRole::SamplingEvent;
  FunctionalExpressionResultKind resultKind =
      FunctionalExpressionResultKind::Boolean;
  uint32_t flags = 0;
  Digest semanticDigest{};
  uint32_t bitWidth = 0;
  CoverageSignedness signedness = CoverageSignedness::NotApplicable;
  uint32_t ownerOrdinal = 0;
  uint32_t ownerSubordinal = 0;
  FunctionalExpressionEvaluationPhase evaluationPhase =
      FunctionalExpressionEvaluationPhase::Constructor;
  uint32_t resultOrdinal = 0;
};

/// A Set result's managed container carries one four-state element plane.
inline constexpr uint32_t FunctionalExpressionSetElementFourState = uint32_t{1}
                                                                    << 0;
/// Sampling-event expressions use these bits to distinguish block boundaries
/// from ordinary clocking events. End is only valid together with Block.
inline constexpr uint32_t FunctionalExpressionSamplingEventBlock = uint32_t{1}
                                                                   << 1;
inline constexpr uint32_t FunctionalExpressionSamplingEventEnd = uint32_t{1}
                                                                 << 2;

struct FunctionalFormal {
  /// Constructor input actuals are captured when new executes; constructor
  /// refs remain read-only live references. Sample formals are evaluated for
  /// each sample call. The direction enum deliberately cannot encode the
  /// illegal output and inout directions.
  uint64_t id = 0;
  uint64_t type = 0;
  std::string name;
  FunctionalFormalKind kind = FunctionalFormalKind::Constructor;
  FunctionalFormalDirection direction = FunctionalFormalDirection::Input;
  FunctionalExpressionResultKind resultKind =
      FunctionalExpressionResultKind::Boolean;
  uint32_t flags = 0;
  uint32_t bitWidth = 0;
  CoverageSignedness signedness = CoverageSignedness::NotApplicable;
  uint32_t ordinal = 0;
  uint64_t defaultExpression = 0;
};

inline constexpr uint32_t FunctionalFormalHasDefault = uint32_t{1} << 0;

struct TransitionProgram {
  uint64_t bin = 0;
  uint64_t item = 0;
  uint32_t firstAlternative = 0;
  uint32_t alternativeCount = 0;
  uint32_t flags = 0;
};

struct TransitionAlternative {
  uint64_t bin = 0;
  uint64_t terminalValueSet = 0;
  uint32_t firstStep = 0;
  uint32_t stepCount = 0;
  uint32_t ordinal = 0;
  uint32_t flags = 0;
};

struct TransitionStep {
  uint64_t bin = 0;
  uint64_t valueSet = 0;
  uint64_t lowerExpression = 0;
  uint64_t upperExpression = 0;
  uint64_t lowerBound = 1;
  uint64_t upperBound = 1;
  uint32_t alternativeOrdinal = 0;
  uint32_t ordinal = 0;
  TransitionRepetitionKind repetition = TransitionRepetitionKind::Once;
  uint32_t flags = 0;
};

inline constexpr uint32_t TransitionStepNeedsResolution = uint32_t{1} << 0;
inline constexpr uint64_t TransitionUnbounded = UINT64_MAX;

struct CrossPlan {
  uint64_t item = 0;
  uint32_t firstTarget = 0;
  uint32_t targetCount = 0;
  uint32_t firstBin = 0;
  uint32_t binCount = 0;
  CrossRetainAutoPolicy retainAutoPolicy = CrossRetainAutoPolicy::Deferred;
  uint32_t flags = 0;
  uint64_t iffExpression = 0;
  /// Exact managed-container element identity and structural provenance span
  /// of the implicit CrossValType. Every cross carries this layout so schema
  /// validity does not depend on selector reachability.
  uint64_t tupleElementType = 0;
  uint64_t tupleProvenanceSpan = 0;
  uint32_t tupleFlags = 0;
};

inline constexpr uint32_t CrossTupleFourState = uint32_t{1} << 0;

struct CrossTarget {
  uint64_t cross = 0;
  uint64_t target = 0;
  uint32_t ordinal = 0;
  /// Physical field layout inside the enclosing cross's implicit
  /// CrossValType. Every target carries its exact field layout.
  uint64_t tupleBitOffset = 0;
  uint32_t tupleBitWidth = 0;
  FunctionalExpressionResultKind tupleResultKind =
      FunctionalExpressionResultKind::Boolean;
  CoverageSignedness tupleSignedness = CoverageSignedness::NotApplicable;
  uint32_t tupleFlags = 0;
};

inline constexpr uint32_t CrossTupleFieldFourState = uint32_t{1} << 0;
/// The coverpoint's source value domain includes X and Z. This is distinct
/// from the CrossValType field representation, which may be four-state even
/// when an implicit conversion can only produce known values.
inline constexpr uint32_t CrossTargetDomainFourState = uint32_t{1} << 1;

struct CrossBinPlan {
  uint64_t bin = 0;
  uint64_t cross = 0;
  uint64_t rootSelector = 0;
  uint32_t flags = 0;
};

struct CrossSelectorNode {
  uint64_t id = 0;
  uint64_t cross = 0;
  uint64_t target = 0;
  uint64_t bin = 0;
  uint64_t valueSet = 0;
  uint64_t withExpression = 0;
  uint64_t constructionExpression = 0;
  uint64_t tupleSet = 0;
  uint64_t matchesExpression = 0;
  uint32_t firstOperand = 0;
  uint32_t operandCount = 0;
  CrossSelectorKind kind = CrossSelectorKind::Binsof;
  uint32_t ordinal = 0;
  uint32_t flags = 0;
  CrossMatchesPolicy matchesPolicy = CrossMatchesPolicy::None;
  uint64_t matchesCount = 0;
};

struct CrossSelectorOperand {
  uint64_t node = 0;
  uint64_t operand = 0;
  uint32_t ordinal = 0;
};

struct FunctionalConfiguration {
  uint64_t type = 0;
  Digest configuration{};
  uint32_t flags = 0;
};

struct FunctionalBinPlan {
  uint64_t bin = 0;
  uint64_t valueSet = 0;
  uint64_t iffExpression = 0;
  uint64_t cardinalityExpression = 0;
  uint64_t arrayCardinality = 0;
  FunctionalBinArrayMode arrayMode = FunctionalBinArrayMode::Scalar;
  FunctionalBinDistributionKind distribution =
      FunctionalBinDistributionKind::None;
  uint32_t flags = 0;
};

struct FunctionalTupleSet {
  uint64_t id = 0;
  uint64_t cross = 0;
  uint64_t selector = 0;
  uint32_t firstTuple = 0;
  uint32_t tupleCount = 0;
  FunctionalTupleElementMode elementMode = FunctionalTupleElementMode::BinTuple;
  uint32_t flags = 0;
};

struct FunctionalTuple {
  uint64_t tupleSet = 0;
  uint64_t id = 0;
  uint32_t firstComponent = 0;
  uint32_t componentCount = 0;
  uint32_t ordinal = 0;
  uint32_t flags = 0;
};

struct FunctionalTupleComponent {
  uint64_t tuple = 0;
  uint64_t target = 0;
  uint64_t bin = 0;
  uint64_t valueSet = 0;
  uint32_t ordinal = 0;
  uint32_t flags = 0;
};

struct FunctionalOptionPlan {
  uint64_t owner = 0;
  uint64_t expression = 0;
  FunctionalConfigurationOptionOwnerKind ownerKind =
      FunctionalConfigurationOptionOwnerKind::Group;
  FunctionalOptionScopeKind scope = FunctionalOptionScopeKind::Instance;
  FunctionalConfigurationOptionKind option =
      FunctionalConfigurationOptionKind::Goal;
  uint32_t ordinal = 0;
  uint32_t flags = 0;
};

struct FunctionalConfigurationOption {
  uint64_t type = 0;
  Digest configuration{};
  uint64_t owner = 0;
  FunctionalConfigurationOptionOwnerKind ownerKind =
      FunctionalConfigurationOptionOwnerKind::Group;
  FunctionalOptionScopeKind scope = FunctionalOptionScopeKind::Instance;
  FunctionalConfigurationOptionKind option =
      FunctionalConfigurationOptionKind::Goal;
  FunctionalConfigurationValueKind valueKind =
      FunctionalConfigurationValueKind::Unsigned;
  uint32_t flags = 0;
  uint64_t value = 0;
  std::string stringValue;
};

struct ResolvedFunctionalItem {
  uint64_t type = 0;
  Digest configuration{};
  uint64_t id = 0;
  uint64_t templateItem = 0;
  std::string name;
  FunctionalItemKind kind = FunctionalItemKind::Coverpoint;
  uint32_t flags = 0;
  uint32_t goal = 100;
  uint32_t weight = 1;
  uint32_t ordinal = 0;
  std::string hierarchy;
};

struct ResolvedFunctionalBin {
  uint64_t type = 0;
  Digest configuration{};
  uint64_t id = 0;
  uint64_t templateBin = 0;
  uint64_t item = 0;
  std::string name;
  FunctionalBinKind kind = FunctionalBinKind::State;
  uint32_t flags = 0;
  uint32_t ordinal = 0;
  uint32_t expansionOrdinal = 0;
  uint64_t atLeast = 1;
  std::string hierarchy;
};

struct ResolvedTransitionStep {
  uint64_t type = 0;
  Digest configuration{};
  uint64_t bin = 0;
  uint64_t alternative = 0;
  uint64_t valueSet = 0;
  uint64_t lowerBound = 1;
  uint64_t upperBound = 1;
  /// Dense within the resolved alternative and equal to the template step
  /// ordinal; repetition is represented by bounds rather than unrolled rows.
  uint32_t ordinal = 0;
  uint32_t flags = 0;
};

struct ResolvedTransitionAlternative {
  /// Concrete post-constructor/distribution sequence before ignore/illegal
  /// subtraction. A resolved bin may therefore retain this sequence while
  /// carrying FunctionalBinEmpty after unconditional subtraction.
  uint64_t type = 0;
  Digest configuration{};
  uint64_t id = 0;
  uint64_t bin = 0;
  uint32_t templateAlternativeOrdinal = 0;
  uint32_t expansionOrdinal = 0;
  uint32_t firstStep = 0;
  uint32_t stepCount = 0;
  uint32_t ordinal = 0;
  uint32_t flags = 0;
};

/// Lossless constructor-resolution inventory for each static transition
/// alternative, including alternatives that expand to zero concrete sequences.
struct ResolvedTransitionExpansionGroup {
  uint64_t type = 0;
  Digest configuration{};
  uint64_t item = 0;
  uint64_t templateBin = 0;
  uint32_t templateAlternativeOrdinal = 0;
  uint32_t firstAlternative = 0;
  uint32_t alternativeCount = 0;
  uint32_t flags = 0;
  uint64_t expansionCount = 0;
};

struct ResolvedCrossPlan {
  uint64_t type = 0;
  Digest configuration{};
  uint64_t cross = 0;
  CrossRetainAutoPolicy retainAutoPolicy = CrossRetainAutoPolicy::Deferred;
  uint32_t flags = 0;
  /// Range into Database::resolvedCrossAutomaticBinCountLimbs. The limbs are
  /// a canonical little-endian unsigned integer for the constructor-resolved
  /// LRM 19.11.2 automatic denominator (Bc). Zero has an empty range.
  uint32_t firstAutomaticBinCountLimb = 0;
  uint32_t automaticBinCountLimbCount = 0;
  /// Root of the canonical reduced decision graph for the remaining
  /// automatic cross-bin set. Zero denotes the empty set.
  uint64_t rootNode = 0;
};

/// A node in a reduced ordered multi-valued decision graph. Each level is one
/// cross target, in declaration order. Missing bin edges reject the tuple.
struct ResolvedCrossAutomaticNode {
  uint64_t type = 0;
  Digest configuration{};
  uint64_t cross = 0;
  uint64_t id = 0;
  uint32_t targetOrdinal = 0;
  uint32_t firstEdge = 0;
  uint32_t edgeCount = 0;
  uint32_t flags = 0;
};

struct ResolvedCrossAutomaticEdge {
  uint64_t node = 0;
  uint64_t bin = 0;
  /// Zero accepts after the final target. Nonzero advances exactly one level.
  uint64_t child = 0;
  uint32_t ordinal = 0;
  uint32_t flags = 0;
};

struct ResolvedCrossSelectorBinding {
  uint64_t type = 0;
  Digest configuration{};
  uint64_t cross = 0;
  uint64_t node = 0;
  uint64_t valueSet = 0;
  uint64_t withExpression = 0;
  uint64_t tupleSet = 0;
  CrossMatchesPolicy matchesPolicy = CrossMatchesPolicy::None;
  uint32_t flags = 0;
  uint64_t matchesCount = 0;
};

struct ResolvedFunctionalValueSet {
  /// The concrete language after constructor evaluation, effective-type casts,
  /// `with` filtering, and bin-array distribution, but before ignore/illegal
  /// subtraction. `FunctionalBinEmpty` records only a globally unsatisfiable
  /// result after unconditional subtraction; guarded exclusions are runtime
  /// state and do not make a schema bin empty.
  uint64_t type = 0;
  Digest configuration{};
  uint64_t id = 0;
  uint64_t templateValueSet = 0;
  uint64_t item = 0;
  uint32_t firstAtom = 0;
  uint32_t atomCount = 0;
  uint32_t bitWidth = 0;
  FunctionalValueSetKind kind = FunctionalValueSetKind::Integral;
  uint32_t flags = 0;
  CoverageSignedness signedness = CoverageSignedness::Unsigned;
  uint64_t ownerBin = 0;
  uint64_t ownerSelector = 0;
  /// StateBin: expansion ordinal,0. TransitionStep: resolved alternative
  /// ordinal,step ordinal. SelectorIntersection: 0,0. TupleComponent: tuple
  /// ordinal,component ordinal; tuple sets are unique per selector/config.
  uint32_t ownerOrdinal = 0;
  uint32_t ownerSubordinal = 0;
  ResolvedFunctionalValueSetRole role =
      ResolvedFunctionalValueSetRole::StateBin;
};

/// Resolved records deliberately use distinct owned types and stable-ID
/// namespaces. Physical ordinals below are layout only and never merge keys.
struct ResolvedFunctionalValueAtom {
  uint64_t valueSet = 0;
  uint64_t realLowBits = 0;
  uint64_t realHighBits = 0;
  uint32_t firstLimb = 0;
  uint32_t limbCount = 0;
  uint32_t ordinal = 0;
  FunctionalValueAtomKind kind = FunctionalValueAtomKind::IntegralValue;
  uint32_t flags = 0;
};

struct ResolvedFunctionalValueLimb {
  uint64_t valueSet = 0;
  uint32_t atomOrdinal = 0;
  uint32_t ordinal = 0;
  uint64_t lowAval = 0;
  uint64_t lowBval = 0;
  uint64_t highAval = 0;
  uint64_t highBval = 0;
  uint64_t wildcardMask = 0;
};

struct ResolvedFunctionalBinPlan {
  uint64_t type = 0;
  Digest configuration{};
  uint64_t bin = 0;
  uint64_t valueSet = 0;
  uint64_t iffExpression = 0;
  uint64_t cardinalityExpression = 0;
  uint64_t arrayCardinality = 0;
  FunctionalBinArrayMode arrayMode = FunctionalBinArrayMode::Scalar;
  FunctionalBinDistributionKind distribution =
      FunctionalBinDistributionKind::None;
  uint32_t flags = 0;
};

/// Canonical resolved bin-array inventory. The physical bin range is excluded
/// from logical configuration identity; zero-bin unsized groups remain
/// explicit so omission cannot masquerade as a resolved cardinality of zero.
struct ResolvedFunctionalBinGroup {
  uint64_t type = 0;
  Digest configuration{};
  uint64_t item = 0;
  uint64_t templateBin = 0;
  uint32_t firstBin = 0;
  uint32_t binCount = 0;
  uint64_t arrayCardinality = 0;
  FunctionalBinArrayMode arrayMode = FunctionalBinArrayMode::Scalar;
  FunctionalBinDistributionKind distribution =
      FunctionalBinDistributionKind::None;
  uint32_t flags = 0;
  FunctionalBinKind kind = FunctionalBinKind::State;
};

struct ResolvedFunctionalTupleSet {
  uint64_t type = 0;
  Digest configuration{};
  uint64_t id = 0;
  uint64_t templateTupleSet = 0;
  uint64_t cross = 0;
  uint64_t selector = 0;
  uint32_t firstTuple = 0;
  uint32_t tupleCount = 0;
  FunctionalTupleElementMode elementMode = FunctionalTupleElementMode::BinTuple;
  uint32_t flags = 0;
};

struct ResolvedFunctionalTuple {
  uint64_t tupleSet = 0;
  uint64_t id = 0;
  uint32_t firstComponent = 0;
  uint32_t componentCount = 0;
  uint32_t ordinal = 0;
  uint32_t flags = 0;
};

struct ResolvedFunctionalTupleComponent {
  uint64_t tuple = 0;
  uint64_t target = 0;
  uint64_t bin = 0;
  uint64_t valueSet = 0;
  uint32_t ordinal = 0;
  uint32_t flags = 0;
};

struct FunctionalSourceRange {
  uint64_t bin = 0;
  FunctionalSourceRole role = FunctionalSourceRole::Expanded;
  /// Zero for expanded/original source; macro stack ordinal otherwise.
  uint32_t ordinal = 0;
  uint64_t file = 0;
  uint64_t endFile = 0;
  std::string macroName;
  uint32_t line = 0;
  uint32_t column = 0;
  uint32_t endLine = 0;
  uint32_t endColumn = 0;
  uint32_t flags = 0;
};

struct Exclusion {
  uint64_t entity = 0;
  MetricKind metric = MetricKind::Line;
  std::string reason;
  uint32_t flags = 0;
};

struct Run {
  UUID uuid{};
  std::string name;
  uint32_t status = 0;
  uint64_t simulationTime = 0;
  uint64_t timestamp = 0;
  uint64_t seed = 0;
  uint64_t flags = 0;
  std::vector<std::pair<std::string, std::string>> tags;
};

inline constexpr uint64_t RunContainsLine = uint64_t{1} << 0;
inline constexpr uint64_t RunContainsToggle = uint64_t{1} << 1;
inline constexpr uint64_t RunContainsFunctional = uint64_t{1} << 2;
inline constexpr uint64_t RunContainsCoverageMask =
    RunContainsLine | RunContainsToggle | RunContainsFunctional;

struct Counter {
  UUID run{};
  MetricKind metric = MetricKind::Line;
  uint64_t entity = 0;
  /// Resolved covergroup instance ID for functional counters; zero otherwise.
  uint64_t instance = 0;
  uint32_t subindex = 0;
  uint32_t flags = 0;
  uint64_t value = 0;
};

struct ResolvedInstance {
  UUID run{};
  uint64_t type = 0;
  uint64_t id = 0;
  std::string name;
  uint32_t flags = 0;
  Digest configuration{};
};

/// One mutable, run-local functional option. A nonzero `instance` identifies
/// an instance option; its `owner` is zero for the group or a stable template
/// FunctionalItem. A zero `instance` identifies a type option; its `owner` is
/// the stable FunctionalType (group owner) or template FunctionalItem. These
/// rows are execution data and never participate in the schema fingerprint.
struct ResolvedInstanceOption {
  UUID run{};
  uint64_t instance = 0;
  uint64_t owner = 0;
  FunctionalConfigurationOptionOwnerKind ownerKind =
      FunctionalConfigurationOptionOwnerKind::Group;
  FunctionalConfigurationOptionKind option =
      FunctionalConfigurationOptionKind::Weight;
  FunctionalConfigurationValueKind valueKind =
      FunctionalConfigurationValueKind::Unsigned;
  std::string stringValue;
  uint64_t value = 0;
  uint32_t flags = 0;
};

/// The persisted display name was synthesized from the run-local instance
/// identity rather than set through option.name or set_inst_name(). Generated
/// names satisfy the LRM uniqueness requirement but remain run-local during
/// report and aggregate-only grouping.
inline constexpr uint32_t ResolvedInstanceGeneratedName = uint32_t{1} << 0;

struct IllegalBinDiagnostic {
  UUID run{};
  uint64_t bin = 0;
  uint64_t instance = 0;
  uint64_t simulationTime = 0;
  uint64_t count = 0;
  std::string message;
  uint32_t flags = 0;
};

inline constexpr uint32_t IllegalBinDiagnosticOverflow = uint32_t{1} << 0;

struct SparseCrossTuple {
  UUID run{};
  uint64_t instance = 0;
  uint64_t cross = 0;
  uint32_t firstComponent = 0;
  uint32_t componentCount = 0;
  uint64_t hits = 0;
  uint32_t flags = 0;
};

inline constexpr uint32_t SparseCrossTupleOverflow = uint32_t{1} << 0;

struct Database {
  uint32_t flags = 0;
  std::string producer;
  Digest schemaFingerprint{};
  std::vector<SourceFile> sourceFiles;
  std::vector<Scope> scopes;
  std::vector<LinePoint> linePoints;
  std::vector<ToggleObject> toggleObjects;
  std::vector<ToggleDimension> toggleDimensions;
  std::vector<FunctionalType> functionalTypes;
  std::vector<FunctionalItem> functionalItems;
  std::vector<FunctionalBin> functionalBins;
  std::vector<FunctionalValueSet> functionalValueSets;
  std::vector<FunctionalValueAtom> functionalValueAtoms;
  std::vector<FunctionalValueLimb> functionalValueLimbs;
  std::vector<FunctionalExpression> functionalExpressions;
  std::vector<FunctionalFormal> functionalFormals;
  std::vector<FunctionalBinPlan> functionalBinPlans;
  std::vector<FunctionalTupleSet> functionalTupleSets;
  std::vector<FunctionalTuple> functionalTupleSetTuples;
  std::vector<FunctionalTupleComponent> functionalTupleSetComponents;
  std::vector<FunctionalOptionPlan> functionalOptionPlans;
  std::vector<TransitionProgram> transitionPrograms;
  std::vector<TransitionAlternative> transitionAlternatives;
  std::vector<TransitionStep> transitionSteps;
  std::vector<CrossPlan> crossPlans;
  std::vector<CrossTarget> crossTargets;
  std::vector<CrossBinPlan> crossBins;
  std::vector<CrossSelectorNode> crossSelectorNodes;
  std::vector<CrossSelectorOperand> crossSelectorOperands;
  std::vector<FunctionalSourceRange> functionalSourceRanges;
  std::vector<Exclusion> exclusions;
  std::vector<Run> runs;
  std::vector<ResolvedInstance> resolvedInstances;
  std::vector<ResolvedInstanceOption> resolvedInstanceOptions;
  std::vector<Counter> counters;
  std::vector<IllegalBinDiagnostic> illegalBinDiagnostics;
  std::vector<FunctionalConfiguration> functionalConfigurations;
  std::vector<FunctionalConfigurationOption> functionalConfigurationOptions;
  std::vector<ResolvedFunctionalItem> resolvedFunctionalItems;
  std::vector<ResolvedFunctionalBin> resolvedFunctionalBins;
  std::vector<ResolvedTransitionAlternative> resolvedTransitionAlternatives;
  std::vector<ResolvedTransitionExpansionGroup>
      resolvedTransitionExpansionGroups;
  std::vector<ResolvedTransitionStep> resolvedTransitionSteps;
  std::vector<ResolvedCrossPlan> resolvedCrossPlans;
  std::vector<uint64_t> resolvedCrossAutomaticBinCountLimbs;
  std::vector<ResolvedCrossAutomaticNode> resolvedCrossAutomaticNodes;
  std::vector<ResolvedCrossAutomaticEdge> resolvedCrossAutomaticEdges;
  std::vector<ResolvedCrossSelectorBinding> resolvedCrossSelectorBindings;
  std::vector<ResolvedFunctionalValueSet> resolvedFunctionalValueSets;
  std::vector<ResolvedFunctionalValueAtom> resolvedFunctionalValueAtoms;
  std::vector<ResolvedFunctionalValueLimb> resolvedFunctionalValueLimbs;
  std::vector<ResolvedFunctionalBinPlan> resolvedFunctionalBinPlans;
  std::vector<ResolvedFunctionalBinGroup> resolvedFunctionalBinGroups;
  std::vector<ResolvedFunctionalTupleSet> resolvedFunctionalTupleSets;
  std::vector<ResolvedFunctionalTuple> resolvedFunctionalTupleSetTuples;
  std::vector<ResolvedFunctionalTupleComponent>
      resolvedFunctionalTupleSetComponents;
  std::vector<SparseCrossTuple> sparseCrossTuples;
  std::vector<uint64_t> sparseCrossTupleComponents;
};

using WriteCallback = bool (*)(const uint8_t *data, size_t size, void *user);

Status parse(const uint8_t *data, size_t size, Database &result,
             const ParseLimits &limits = {}, Diagnostic *diagnostic = nullptr);
Status serialize(const Database &database, WriteCallback callback, void *user,
                 Diagnostic *diagnostic = nullptr);
Status serialize(const Database &database, std::vector<uint8_t> &result,
                 Diagnostic *diagnostic = nullptr);
Status validate(const Database &database, Diagnostic *diagnostic = nullptr);
/// Returns true when the database contains only compiler-owned static schema.
/// Embedded execution images must not carry run-time resolved state.
bool isSchemaOnly(const Database &database);
Digest computeSchemaFingerprint(const Database &database);
/// Computes the canonical resolved semantics digest for one temporary or final
/// `(type, configurationKey)` bundle. The key itself is not hashed.
Digest computeFunctionalConfigurationFingerprint(
    const Database &database, uint64_t type, const Digest &configurationKey);
Digest sha256(const uint8_t *data, size_t size);

Status merge(Database &destination, const Database &source,
             bool discardTestDetail = false, Diagnostic *diagnostic = nullptr);
/// Transactionally replaces all retained runs with one canonical aggregate
/// run while preserving the static schema and resolved configuration bundles.
Status discardTestDetail(Database &database, Diagnostic *diagnostic = nullptr);
Status readFile(const std::string &path, Database &result,
                const ParseLimits &limits = {},
                Diagnostic *diagnostic = nullptr);
Status writeFileAtomically(const std::string &path, const Database &database,
                           Diagnostic *diagnostic = nullptr);

const char *statusName(Status status);
const char *metricName(MetricKind metric);
bool parseMetric(const std::string &name, MetricKind &metric);
std::string formatUUID(const UUID &uuid);
bool parseUUID(const std::string &text, UUID &uuid);

} // namespace obelisk::coverage

#endif
