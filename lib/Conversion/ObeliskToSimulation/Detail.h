//===- Detail.h - Shared semantic-to-simulation lowering helpers -*- C++ -*===//
//
// Helpers shared by the four passes that lower elaborated `obelisk.sv`
// semantic IR to executable `simulation` SSA. Nothing here is part of the
// public conversion interface.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_DETAIL_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_DETAIL_H

#include "SemanticTypeIndex.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Operation.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace obelisk::simlowering {

namespace sim = ::obelisk::sim;
namespace semantic = ::obelisk::ir;

inline constexpr ::mlir::StringLiteral readMemEnumKeyValuesAttrName =
    "readmem_enum_key_values";
inline constexpr ::mlir::StringLiteral readMemEnumElementValuesAttrName =
    "readmem_enum_element_values";
/// Value elaboration folded an expression to, carried over by the frontend.
/// Written by the Slang importer, so the spelling is fixed there as well.
inline constexpr ::mlir::StringLiteral foldedConstantAttrName =
    "folded_constant";
/// Elaboration value of an expression that the language requires to be
/// constant, kept for that use even where executable folds were withdrawn.
/// The two-state importer writes it, so the spelling is fixed there as well.
inline constexpr ::mlir::StringLiteral elaboratedConstantAttrName =
    "elaborated_constant";
/// Finest precision any timeprecision, timeunit or `timescale in the source
/// declares, carried on the semantic root. Written by the Slang importer, so
/// the spelling is fixed there as well.
inline constexpr ::mlir::StringLiteral declaredTimePrecisionAttrName =
    "declared_time_precision_fs";
/// Source array-query order when typedef expansion differs from storage
/// nesting (IEEE 1800-2017 20.7).
inline constexpr ::mlir::StringLiteral arrayQueryDimensionsAttrName =
    "obelisk.array_query_dimensions";
/// Identity a type reference shares with every reference to a matching type
/// (IEEE 1800-2017 6.22.1). Written by the Slang importer, so the spelling is
/// fixed there as well.
inline constexpr ::mlir::StringLiteral typeReferenceIdentityAttrName =
    "type_reference_identity";
/// Ordered post-validation user-defined primitive declaration data.  The
/// frontend stores this dictionary under the unqualified semantic spelling;
/// preparation carries it on the isolated executable unit under the Simulation
/// spelling until unit lowering consumes it.
inline constexpr ::mlir::StringLiteral udpSemanticMetadataAttrName =
    "udp_metadata";
inline constexpr auto observerEventPrimaryAttrName =
    ::obelisk::schedule::Field::EventPrimary;
/// Unit lowering is a parallel nested-function pipeline. Observer users mark
/// their own function and the following serial design pass propagates these
/// requests to the shared evaluator without cross-function mutation.
inline constexpr auto concurrentCancelObserverRequestAttrName =
    ::obelisk::schedule::Field::ConcurrentCancelObserverRequest;
inline constexpr auto concurrentAbortObserverRequestAttrName =
    ::obelisk::schedule::Field::ConcurrentAbortObserverRequest;

/// The frozen direction of a semantic formal argument.
inline sim::FormalDirection
getFormalDirection(semantic::SVArgumentDirection direction) {
  switch (direction) {
  case semantic::SVArgumentDirection::In:
    return sim::FormalDirection::In;
  case semantic::SVArgumentDirection::Out:
    return sim::FormalDirection::Out;
  case semantic::SVArgumentDirection::InOut:
    return sim::FormalDirection::InOut;
  case semantic::SVArgumentDirection::Ref:
    return sim::FormalDirection::Ref;
  }
  llvm_unreachable("unknown argument direction");
}

/// Total, side-effect-free integral system functions encoded by the random
/// program. Unlike user functions, these do not introduce implicit solve
/// priority (IEEE 1800-2023 18.5.11).
inline bool isIntegralRandomSystemFunction(::llvm::StringRef name) {
  return name == "$signed" || name == "$unsigned" || name == "$clog2" ||
         name == "$countbits" || name == "$countones" || name == "$onehot" ||
         name == "$onehot0" || name == "$isunknown";
}

inline bool isRealRandomSystemFunction(::llvm::StringRef name) {
  return name == "$itor" || name == "$rtoi" || name == "$bitstoreal" ||
         name == "$realtobits" || name == "$bitstoshortreal" ||
         name == "$shortrealtobits";
}

inline bool isRandomConstraintSystemFunction(::llvm::StringRef name) {
  return isIntegralRandomSystemFunction(name) ||
         isRealRandomSystemFunction(name);
}

/// Result representation frozen by prepare for computed timing observers.
/// Keep this strongly typed at both ends of the private pass boundary so a
/// malformed integer attribute cannot silently select value semantics.
enum class ObserverResult : uint32_t {
  None,
  Value,
  Truth,
  Event,
};

inline bool isGlobalPastSampledFunction(::llvm::StringRef name) {
  return name == "$past_gclk" || name == "$rose_gclk" || name == "$fell_gclk" ||
         name == "$stable_gclk" || name == "$changed_gclk";
}

inline bool isGlobalFutureSampledFunction(::llvm::StringRef name) {
  return name == "$future_gclk" || name == "$rising_gclk" ||
         name == "$falling_gclk" || name == "$steady_gclk" ||
         name == "$changing_gclk";
}

inline bool isGlobalSampledFunction(::llvm::StringRef name) {
  return isGlobalPastSampledFunction(name) ||
         isGlobalFutureSampledFunction(name);
}

/// Frozen descriptor for an ordinary (non-virtual) clocking-block event.
/// The frontend retains the clocking-block symbol as the expression's normal
/// reference and records the statically resolved event signal separately so
/// preparation can capture the signal without reopening Slang's AST.
inline constexpr ::mlir::StringLiteral clockingBlockEventAttrName =
    "clocking_block_event";
inline constexpr ::mlir::StringLiteral clockingEventEdgeAttrName =
    "clocking_event_edge";
inline constexpr ::mlir::StringLiteral clockingEventHasIffAttrName =
    "clocking_event_has_iff";
inline constexpr ::mlir::StringLiteral clockingEventPathAttrName =
    "clocking_event_path";
inline constexpr ::mlir::StringLiteral clockingEventSymbolAttrName =
    "clocking_event_symbol";
inline constexpr ::mlir::StringLiteral clockingEventRawEdgeAttrName =
    "clocking_event_raw_edge";
inline constexpr ::mlir::StringLiteral clockingEventRawPathAttrName =
    "clocking_event_raw_path";
inline constexpr ::mlir::StringLiteral clockingEventRawSymbolAttrName =
    "clocking_event_raw_symbol";
/// Marks a clocking block whose event needs a private named-event descriptor
/// driven by one shared monitor (for example, an event list or computed event
/// expression).
inline constexpr ::mlir::StringLiteral clockingEventListAttrName =
    "clocking_event_list";
inline constexpr ::mlir::StringLiteral clockingEventMonitorRequiredAttrName =
    "clocking_event_monitor";
inline constexpr ::mlir::StringLiteral clockingVariableAttrName =
    "clocking_variable";
inline constexpr ::mlir::StringLiteral clockingAccessDirectionAttrName =
    "clocking_access_direction";
/// Marks the selection of a built-in base object (`$base`) that converts a
/// derived handle to its semaphore or mailbox base type; a null handle
/// converts to null.
inline constexpr ::mlir::StringLiteral builtinBaseUpcastAttrName =
    "builtin_base_upcast";
/// Path of the private design storage that mirrors the published sample of a
/// clocking input whose changes are observed (IEEE 1800-2023 14.13, 14.15).
inline std::string getClockingSamplePath(::llvm::StringRef clockVariable) {
  return (clockVariable + ".$sampled").str();
}
inline constexpr ::mlir::StringLiteral clockingSourcePathAttrName =
    "clocking_source_path";
inline constexpr ::mlir::StringLiteral clockingSourceSymbolAttrName =
    "clocking_source_symbol";
inline constexpr ::mlir::StringLiteral clockingInputSkewEdgeAttrName =
    "clocking_input_skew_edge";
inline constexpr ::mlir::StringLiteral clockingInputSkewEdgeOnlyAttrName =
    "clocking_input_skew_edge_only";
inline constexpr ::mlir::StringLiteral clockingInputSkewOneStepAttrName =
    "clocking_input_skew_one_step";
inline constexpr ::mlir::StringLiteral clockingInputSkewDelayAttrName =
    "clocking_input_skew_delay";
inline constexpr ::mlir::StringLiteral clockingInputSkewDelayIsRealAttrName =
    "clocking_input_skew_delay_is_real";
inline constexpr ::mlir::StringLiteral clockingOutputSkewEdgeAttrName =
    "clocking_output_skew_edge";
inline constexpr ::mlir::StringLiteral clockingOutputSkewEdgeOnlyAttrName =
    "clocking_output_skew_edge_only";
inline constexpr ::mlir::StringLiteral clockingOutputSkewOneStepAttrName =
    "clocking_output_skew_one_step";
inline constexpr ::mlir::StringLiteral clockingOutputSkewDelayAttrName =
    "clocking_output_skew_delay";
inline constexpr ::mlir::StringLiteral clockingOutputSkewDelayIsRealAttrName =
    "clocking_output_skew_delay_is_real";
inline constexpr ::mlir::StringLiteral clockingTimeUnitAttrName =
    "clocking_time_unit_fs";
inline constexpr ::mlir::StringLiteral clockingTimePrecisionAttrName =
    "clocking_time_precision_fs";

inline std::optional<ObserverResult>
parseObserverResult(::mlir::IntegerAttr attribute) {
  if (!attribute)
    return std::nullopt;
  switch (attribute.getValue().getZExtValue()) {
  case static_cast<uint32_t>(ObserverResult::Value):
    return ObserverResult::Value;
  case static_cast<uint32_t>(ObserverResult::Truth):
    return ObserverResult::Truth;
  case static_cast<uint32_t>(ObserverResult::Event):
    return ObserverResult::Event;
  default:
    return std::nullopt;
  }
}

/// True for any operation in the elaborated semantic dialect.
bool isSemanticOp(::mlir::Operation *op);

/// True for the semantic symbols that become one `simulation.func`.
bool isCodeUnit(::mlir::Operation *op);

/// True for an elaborated design member nested in Slang's synthetic instance
/// used only to describe a parameterized virtual-interface type. Declarations
/// nested in an actual runtime type (for example a class) remain executable.
bool isCompileTimeOnlyInstanceMember(::mlir::Operation *op);

/// Source location of a semantic node, falling back to its MLIR location.
::mlir::Location getSemanticLocation(::mlir::Operation *op);

/// Operations in the first block of an AST node's inventory region.
::mlir::SmallVector<::mlir::Operation *> getChildren(::mlir::Operation *op);

/// Whether an expression is a packed selection or method result applied to a
/// queue, dynamic array, associative array, or string, possibly through
/// unpacked elements in between. IEEE 1800-2023 16.5.1 samples such a value
/// in Preponed, where the state snapshot holds no container contents.
bool readsDynamicContainerValue(::mlir::Operation *expression);

/// Lossless view of the heterogeneous direct children of a coverage-bin
/// symbol.  The frontend records source roles and transition grouping in
/// attributes because the generic AST region itself has no operand segments.
struct CoverageTransitionRangeChildren {
  ::mlir::SmallVector<::mlir::Operation *> items;
  ::mlir::Operation *repeatFrom = nullptr;
  ::mlir::Operation *repeatTo = nullptr;
  semantic::SVCoverageTransitionRepeatKind repeatKind =
      semantic::SVCoverageTransitionRepeatKind::None;
};

struct CoverageTransitionSetChildren {
  ::mlir::SmallVector<CoverageTransitionRangeChildren> ranges;
};

struct CoverageBinChildren {
  ::mlir::Operation *iff = nullptr;
  ::mlir::Operation *numberOfBins = nullptr;
  ::mlir::Operation *setCoverage = nullptr;
  ::mlir::Operation *with = nullptr;
  ::mlir::Operation *crossSelect = nullptr;
  ::mlir::SmallVector<::mlir::Operation *> values;
  ::mlir::SmallVector<CoverageTransitionSetChildren> transitions;
};

/// Decode and validate the frontend's coverage-bin child inventory.  On
/// failure this emits a diagnostic at the bin and returns failure without a
/// partial view.
::mlir::FailureOr<CoverageBinChildren>
decodeCoverageBinChildren(semantic::SVCoverageBinSymbolOp bin);

/// The value expression, if any, in a net declaration. Delay-control children
/// describe either the declaration assignment or the net itself and are not
/// initializer expressions.
::mlir::SmallVector<::mlir::Operation *>
getNetInitializerExpressions(::mlir::Operation *op);

/// Literal spelling of an integer node, including constants frozen by the
/// prepare pass after their defining symbol is no longer in scope.
std::optional<::mlir::StringRef>
getConstantSpelling(::mlir::Operation *operation);

/// Source-language constant needed for elaboration-only counts and bounds.
/// In two-state mode this may differ from evaluating the expression at runtime.
///
/// The accessor a client uses decides its --two-state semantics. A structural
/// quantity (bound, count, key, ordinal, history depth, topology index) takes
/// this elaborated spelling: it is fixed at elaboration and X/Z in it is an
/// error. An executable value (condition, label, operand, delay, data) uses
/// getConstantSpelling, which omits Slang's four-state fold under --two-state
/// so the expression is folded again in its runtime domain.
std::optional<::mlir::StringRef>
getElaboratedConstantSpelling(::mlir::Operation *operation);

/// Copy a parameter, enum value or specparam symbol's executable constant and
/// any retained two-state pattern onto an expression that references it.
void copyConstantSymbolValue(::mlir::Operation *reference,
                             ::mlir::Operation *symbol);

/// Original X/Z pattern retained when a literal was projected to two-state.
std::optional<::mlir::StringRef>
getTwoStatePatternSpelling(::mlir::Operation *operation);

/// Lower a retained wildcard pattern to an integer mask comparison. Returns
/// no value when ordinary expression lowering should handle the comparison.
::mlir::FailureOr<std::optional<::mlir::Value>>
lowerTwoStatePatternComparison(::mlir::OpBuilder &builder, ::mlir::Value value,
                               ::mlir::Operation *pattern,
                               sim::CompareKind kind);

/// Self-determined width of an unsized based literal whose most significant
/// four-state bit is unknown, or nothing for any other node. IEEE 1800-2017
/// 5.7.1 fills that bit through every wider containing expression instead of
/// padding the literal with zeros, so a widening of such a literal -- and any
/// fold computed from one -- has to follow that rule rather than an ordinary
/// conversion.
std::optional<unsigned>
getUnsizedUnknownFillWidth(::mlir::Operation *operation);

/// Matching-type identity of a type-reference node, or nothing when the node
/// is not a type reference the importer numbered.
std::optional<int64_t> getTypeReferenceIdentity(::mlir::Operation *operation);

/// Fold a pure, already-lowered SSA chain without rewriting its surrounding
/// CFG. This also follows constants frozen from elaborated parameters.
::mlir::Attribute foldConstantValue(::mlir::Value value);
std::optional<bool> foldConstantTruth(::mlir::Value value);

/// Whether an expression denotes stable storage that the scheduler can watch
/// directly, without a computed observer.
bool isAddressableExpression(::mlir::Operation *operation);

/// Return the expression whose LSB is sampled for a frozen Clause 31.7
/// predicate. Implicit context conversions are semantic scaffolding rather
/// than part of the condition grammar and are peeled here so direct handles
/// retain the compact clock-wait path.
::mlir::Operation *getTimingConditionOperand(::mlir::Operation *operation,
                                             int32_t predicate);

/// Whether a semantic endpoint is the unbounded `$` literal, ignoring
/// source-level conversion wrappers.
bool isUnboundedEndpoint(::mlir::Operation *operation);

/// Deterministic nonzero identifier for an outlined semantic code unit.
uint64_t stableCodeUnitID(::mlir::StringRef key);

/// Canonical source-authored identity for a semantic coverage entity. This
/// excludes importer node numbers and generated symbol leaves so unrelated IR
/// insertion cannot perturb coverage identities.
std::string functionalSemanticIdentity(::mlir::Operation *operation);

/// Stable nonzero v1 identity of a covergroup type.
uint64_t stableFunctionalTypeID(semantic::SVCovergroupTypeOp covergroup);

/// Whether overriding a captured reference mutates design/static storage
/// rather than activation-local automatic storage.
bool isStaticallyAllocatedOverrideTarget(::mlir::Value value);

/// Materialize deferred automatic covergroup clocking samplers at the design
/// anchor. Per-function lowering records plans but never mutates sibling
/// symbols, keeping the nested pass safe under MLIR's parallel pass manager.
::mlir::LogicalResult materializeCovergroupClockingSamplers(
    sim::SimDesignOp design, const class UnitLoweringInputs &loweringInputs);

/// Whether a packed semantic type is signed.
bool isSignedSemanticType(::mlir::Type type);

/// Packed width and four-state domain of a source semantic type.  These
/// queries are non-diagnostic so speculative lowering can decline a construct
/// without poisoning an otherwise supported fallback path.
std::optional<uint64_t> getSemanticPackedWidth(::mlir::Type type);
bool isFourStateSemanticType(::mlir::Type type);

/// Fixed SystemVerilog bitstream width of a semantic type.
///
/// Unlike the packed-width query used for SSA normalization, this includes
/// fixed unpacked arrays and aggregates, as required by `$bits`. Dynamic
/// bitstreams have no type-only width and return std::nullopt.
std::optional<uint64_t> getSemanticBitstreamWidth(::mlir::Type type);

/// One source-level dimension of an elaborated SystemVerilog type.
///
/// Dimensions are ordered outermost first, matching the one-based dimension
/// index accepted by the array query system functions. Fixed dimensions retain
/// their declared direction and endpoints. The remaining kinds require an
/// object value (or an open-array descriptor) to answer range queries.
enum class SemanticDimensionKind : uint8_t {
  Fixed,
  String,
  DynamicArray,
  Queue,
  AssociativeArray,
  OpenArray,
};

struct SemanticDimension {
  SemanticDimensionKind kind = SemanticDimensionKind::Fixed;
  bool unpacked = false;
  int64_t left = 0;
  int64_t right = 0;
  ::mlir::Type indexType;

  bool isFixed() const { return kind == SemanticDimensionKind::Fixed; }
};

/// Source-level dimensions of a semantic type, ordered outermost first.
///
/// This is a shape query only: it never evaluates an object. Dynamic container
/// dimensions are represented explicitly so callers cannot accidentally fold
/// a value-dependent query as if it were a fixed range.
::mlir::SmallVector<SemanticDimension> getSemanticDimensions(::mlir::Type type);

struct SemanticSelectionLayout {
  int64_t left;
  int64_t right;
  unsigned elementWidth;
};

/// Declared coordinates and logical element width of a fixed integral select.
/// The rightmost declared element occupies the least-significant bits.
std::optional<SemanticSelectionLayout>
getSemanticSelectionLayout(::mlir::Type type);

/// Normalized type of a semantic node's `semantic_type` attribute.
::mlir::FailureOr<::mlir::Type>
getNormalizedSemanticType(::mlir::Operation *op);
::mlir::FailureOr<::mlir::Type> normalizeSemanticType(::mlir::Type type,
                                                      ::mlir::Location location,
                                                      ::mlir::Operation *scope);
/// Lossless source-semantic type inventory used only by VPI reflection.
::mlir::FailureOr<::obelisk::sim::VPITypeSemanticsAttr>
makeVPITypeSemantics(::mlir::Type type, ::mlir::Location location,
                     ::mlir::ArrayAttr typedefLayers = {},
                     ::mlir::Operation *semanticOwner = nullptr);

inline constexpr ::llvm::StringLiteral vpiTypedefLayersAttrName =
    "vpi_typedef_layers";
inline constexpr ::llvm::StringLiteral vpiSourceTypeIdentityAttrName =
    "vpi_source_type_identity";
::mlir::FailureOr<dpi::AggregateABIAttr>
makeDPIAggregateABI(::mlir::Type semanticType, ::mlir::Type normalizedType,
                    ::mlir::Location location, ::mlir::Builder &builder,
                    bool compactTransport = false);

/// Whether a timing condition is decided by its watched storage being nonzero.
/// A `wait` on such an expression can suspend on the storage directly; anything
/// else -- a handle, whose truth is being non-null, or a real, whose truth is a
/// floating-point comparison -- has to re-evaluate the condition instead.
bool storageDecidesTruth(::mlir::Operation *expression);

/// Stable flat executable symbol for a semantic class reference. Semantic
/// paths may be nested and coexist with the generated design between passes;
/// executable descriptors therefore use their own collision-free namespace.
::mlir::StringAttr
getSimulationClassSymbol(::mlir::SymbolRefAttr semanticClass);
::mlir::StringAttr
getSimulationCovergroupSymbol(::mlir::SymbolRefAttr semanticCovergroup);

/// Stable flat reflection symbol for one elaborated virtual-interface
/// specialization and optional modport view. Unlike the opaque executable
/// specialization key, this names an immutable SimVPITypespecDeclOp.
::mlir::StringAttr getSimulationVirtualInterfaceTypespecSymbol(
    ::mlir::SymbolRefAttr semanticInterface, ::mlir::StringAttr modport);

/// Classify a canonical source-semantic type for the initial DPI-C ABI.
/// Diagnostics are issued at `location` for unsupported categories.
::mlir::FailureOr<DPIABIKind> getDPIABIKind(::mlir::Type type,
                                            ::mlir::Location location);

/// Elaborated hierarchical path of a semantic symbol, or its plain name.
::mlir::StringRef getHierarchyName(::mlir::Operation *op);

/// Declared name of a semantic symbol, for diagnostics and debug info.
::mlir::StringRef getDebugName(::mlir::Operation *op);

/// Creates semantic operations with fresh node IDs.
class SemanticOpFactory {
public:
  explicit SemanticOpFactory(::mlir::ModuleOp module)
      : builder(module.getContext()) {
    module.walk([&](::mlir::Operation *operation) {
      if (auto node = operation->getAttrOfType<::mlir::IntegerAttr>("node_id"))
        nextNode = std::max(nextNode, node.getInt() + 1);
    });
  }

  ::mlir::Operation *
  create(::mlir::OpBuilder &at, ::mlir::Location location,
         ::llvm::StringRef name,
         ::llvm::ArrayRef<::mlir::NamedAttribute> attributes) {
    ::mlir::OperationState state(location, name);
    state.addAttributes(attributes);
    state.addAttribute("node_id", builder.getI64IntegerAttr(nextNode++));
    state.addRegion();
    ::mlir::Operation *operation = at.create(state);
    operation->getRegion(0).push_back(new ::mlir::Block);
    return operation;
  }

  /// Gives every node of a cloned semantic tree a fresh identity.
  void renumber(::mlir::Operation *root) {
    root->walk([&](::mlir::Operation *operation) {
      if (operation->hasAttr("node_id"))
        operation->setAttr("node_id", builder.getI64IntegerAttr(nextNode++));
    });
  }

  ::mlir::Builder builder;

private:
  int64_t nextNode = 0;
};

/// Value and unknown planes of a parsed SystemVerilog integer literal.
struct ParsedConstant {
  ::llvm::APInt value;
  ::llvm::APInt unknown;
};

/// Parse a SystemVerilog integer literal into `width` bits. Explicitly sized
/// literals are first truncated or padded to their declared size. Unsized
/// based literals first acquire their self-determined width (at least 32 bits).
/// The resulting literal is then resized to `width` according to its
/// signedness and the unsized-X/Z extension rule.
::mlir::FailureOr<ParsedConstant> parseSVInteger(::mlir::StringRef spelling,
                                                 unsigned width,
                                                 ::mlir::Location location);

/// Freeze the elaborated value and normalized type of a constant symbol.
/// The symbol must carry semantic_type and constant_value attributes.
::mlir::FailureOr<sim::FrozenConstantAttr>
freezeSemanticConstant(::mlir::Operation *symbol);
/// Freezes an elaborated constant spelling of `source`, normalized to
/// `normalized`, into a typed simulation constant.
::mlir::FailureOr<sim::FrozenConstantAttr>
freezeConstantSpelling(::mlir::Type normalized, ::mlir::Type source,
                       ::llvm::StringRef spelling, ::mlir::Location location);

/// The all-unknown (or zero) initial value of a normalized type.
::mlir::Value createDefaultValue(::mlir::OpBuilder &builder,
                                 ::mlir::Location location, ::mlir::Type type);

/// Argument metadata describing how an entry argument is bound.
::mlir::DictionaryAttr
captureMetadata(::mlir::OpBuilder &builder, sim::CaptureKind kind,
                std::optional<uint64_t> descriptorId = std::nullopt);

/// The bit-stream width of a fixed bit-stream type: a packed value's own
/// width, or, for a fixed unpacked array or structure, the sum of its
/// elements' widths as IEEE 1800-2017 11.4.14 concatenates them. Empty for
/// anything whose width the compiler cannot know.
::std::optional<uint64_t> fixedBitStreamWidth(::mlir::Type type);

} // namespace obelisk::simlowering

#endif // OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_DETAIL_H
