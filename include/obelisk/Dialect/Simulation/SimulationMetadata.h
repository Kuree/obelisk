//===- SimulationMetadata.h - Shared simulation metadata -------*- C++ -*-===//
//
// Transient metadata names shared by simulation lowering, analysis, and
// optimization. Keep classification here so conservative transformation
// allowlists cannot drift apart.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H

#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace obelisk::sim::metadata {

/// All transient late-lowering metadata is revision-coupled and uses one
/// schema. Consumers reject stale IR instead of maintaining parallel readers.
inline constexpr uint32_t schemaVersion = 1;
inline constexpr uint32_t maxDirectStaticStateBits = 64;

/// Initial process whose same-edge waits have one explicit, storage-backed
/// phase dispatcher. Its entry is cold startup, not an eval activation preamble.
inline constexpr llvm::StringLiteral clockedControl =
    "obelisk_sim.clocked_control";

/// Transient function attribute containing ArgumentBindingAttr,
/// LocalBindingAttr, and ConstantBindingAttr entries.
inline constexpr llvm::StringLiteral bindings = "obelisk_sim.bindings";
inline constexpr llvm::StringLiteral delayScale = "obelisk_sim.delay_scale";
inline constexpr llvm::StringLiteral delayQuantum = "obelisk_sim.delay_quantum";
inline constexpr llvm::StringLiteral captureKind = "obelisk_sim.capture_kind";
inline constexpr llvm::StringLiteral descriptorId = "obelisk_sim.descriptor_id";
inline constexpr llvm::StringLiteral descriptorRootType =
    "obelisk_sim.descriptor_root_type";
inline constexpr llvm::StringLiteral descriptorLow =
    "obelisk_sim.descriptor_low";
inline constexpr llvm::StringLiteral descriptorIndices =
    "obelisk_sim.descriptor_indices";
inline constexpr llvm::StringLiteral descriptorAggregateType =
    "obelisk_sim.descriptor_aggregate_type";
inline constexpr llvm::StringLiteral descriptorPackedLow =
    "obelisk_sim.descriptor_packed_low";
inline constexpr llvm::StringLiteral hierarchicalName =
    "obelisk_sim.hierarchical_name";
/// Exact frontend enum identity retained solely to reconnect a value object
/// to its anonymous enum typespec in the immutable VPI relation image.
inline constexpr llvm::StringLiteral vpiSourceTypeIdentity =
    "obelisk_sim.vpi_source_type_identity";
/// FlatSymbolRefAttr naming the immutable relation-backed anchor that owns the
/// public VPI identity of executable named-event-array storage. The storage
/// remains in the execution layout but is omitted from the VPI object
/// inventory.
inline constexpr llvm::StringLiteral vpiIdentityDelegated =
    "obelisk_sim.vpi_identity_delegated";
/// Marks a storage descriptor that a subroutine owns. Its writers are the
/// subroutine's callers rather than drivers of a design variable.
inline constexpr llvm::StringLiteral subroutineStorage =
    "obelisk_sim.subroutine_storage";
/// Hierarchical path of the variable that holds a function's return value.
inline constexpr llvm::StringLiteral returnVariablePath =
    "obelisk_sim.return_variable_path";
/// Stable base-to-derived index of an effective rand/randc class property.
inline constexpr llvm::StringLiteral randomModeIndex =
    "obelisk_sim.random_mode_index";
/// Marks a non-static rand class-handle field as a recursive object edge.
inline constexpr llvm::StringLiteral randomObjectEdge =
    "obelisk_sim.random_object_edge";
/// Typed RandomVariableKindAttr on a direct packed instance rand property.
inline constexpr llvm::StringLiteral randomVariableKind =
    "obelisk_sim.random_variable_kind";
/// Source signedness of a direct packed random variable.
inline constexpr llvm::StringLiteral randomVariableSigned =
    "obelisk_sim.random_variable_signed";
/// Hidden i64 fields that carry one randc property's permutation state.
inline constexpr llvm::StringLiteral randomCycleKeyField =
    "obelisk_sim.random_cycle_key_field";
inline constexpr llvm::StringLiteral randomCyclePositionField =
    "obelisk_sim.random_cycle_position_field";
/// Root-class field containing the 64-bit disabled-property mask.
inline constexpr llvm::StringLiteral randomModeField =
    "obelisk_sim.random_mode_field";
/// Marks an executable class field that corresponds to a source-declared
/// instance property. Compiler-owned fields and static properties are absent
/// from the object bit-stream inventory.
inline constexpr llvm::StringLiteral classBitstreamMember =
    "obelisk_sim.class_bitstream_member";
/// Source member visibility retained for recursive class bit-stream legality.
inline constexpr llvm::StringLiteral classBitstreamVisibility =
    "obelisk_sim.class_bitstream_visibility";
/// Marks an explicit class-containing bit-stream conversion whose source is
/// exactly the enclosing method's current-instance `this`.  This exception is
/// semantic: aliases and handles reached through another expression never
/// inherit it.
inline constexpr llvm::StringLiteral classBitstreamAllowHiddenRoot =
    "obelisk_sim.class_bitstream_allow_hidden_root";
/// Dense nonzero identifier of a materialized class bit-stream cast site.
inline constexpr llvm::StringLiteral classBitstreamSiteID =
    "obelisk_sim.class_bitstream_site_id";
inline constexpr llvm::StringLiteral classBitstreamBytecodeFunction =
    "obelisk_sim.class_bitstream_bytecode_function";
inline constexpr llvm::StringLiteral classBitstreamBytecodeSite =
    "obelisk_sim.class_bitstream_bytecode_site";
/// Module-level canonical pointer-free class schema/group/site blob.
inline constexpr llvm::StringLiteral classBitstreamBlob =
    "obelisk.execution.class_bitstream_blob";
/// Module-level schema-only native coverage database embedded in the v1
/// execution extension. Runtime run data is never written back into this
/// attribute.
inline constexpr llvm::StringLiteral coverageSchemaBlob =
    "obelisk.execution.coverage_schema_blob";
/// Frontend-selected IEEE 1800 revision used for revision-sensitive coverage
/// computation and persisted with every functional type.
inline constexpr llvm::StringLiteral coverageLanguageVersion =
    "obelisk.coverage.language_version";
/// Stable FunctionalExpression identity assigned while the schema and helper
/// contract are built together. Unit lowering must copy this exact identity
/// to covergroup.sample; it must never synthesize an execution-local ordinal.
inline constexpr llvm::StringLiteral coverageFunctionalExpressionId =
    "obelisk.coverage.functional_expression_id";
inline constexpr llvm::StringLiteral coverageFunctionalExpressionOrdinal =
    "obelisk.coverage.functional_expression_ordinal";
inline constexpr llvm::StringLiteral coverageFunctionalExpressionKind =
    "obelisk.coverage.functional_expression_kind";
inline constexpr llvm::StringLiteral coverageFunctionalExpressionBitWidth =
    "obelisk.coverage.functional_expression_bit_width";
inline constexpr llvm::StringLiteral coverageFunctionalExpressionSignedness =
    "obelisk.coverage.functional_expression_signedness";
inline constexpr llvm::StringLiteral coverageFunctionalExpressionPhase =
    "obelisk.coverage.functional_expression_phase";
inline constexpr llvm::StringLiteral coverageFunctionalExpressionRole =
    "obelisk.coverage.functional_expression_role";
/// Constructor-time `with` predicates are evaluated once for every candidate
/// occurrence. These transient arrays keep that one-to-many helper contract
/// on the semantic predicate without introducing another runtime ABI shape.
inline constexpr llvm::StringLiteral coverageFunctionalWithExpressionIds =
    "obelisk_sim.coverage.functional.with_expression_ids";
inline constexpr llvm::StringLiteral coverageFunctionalWithExpressionOrdinals =
    "obelisk_sim.coverage.functional.with_expression_ordinals";
inline constexpr llvm::StringLiteral coverageFunctionalWithCandidateValues =
    "obelisk_sim.coverage.functional.with_candidate_values";
inline constexpr llvm::StringLiteral coverageFunctionalWithIteratorPath =
    "obelisk_sim.coverage.functional.with_iterator_path";
/// Constructor-time cross-selector predicates are evaluated once for every
/// tuple in the finite Cartesian product of their cross targets. The values
/// are flattened tuple-major (the final target varies fastest); target paths
/// identify the semantic iterator bindings restored around each evaluation.
inline constexpr llvm::StringLiteral
    coverageFunctionalCrossWithCandidateValues =
        "obelisk_sim.coverage.functional.cross_with_candidate_values";
inline constexpr llvm::StringLiteral coverageFunctionalCrossWithTargetPaths =
    "obelisk_sim.coverage.functional.cross_with_target_paths";
inline constexpr llvm::StringLiteral coverageFunctionalFormalId =
    "obelisk.coverage.functional_formal_id";
/// Stable FunctionalItem identity assigned to its semantic declaration.
inline constexpr llvm::StringLiteral coverageFunctionalItemId =
    "obelisk.coverage.functional_item_id";
/// Transient dense counter index assigned by coverage preparation. Stable
/// entity identity remains in the native schema and never depends on this
/// physical index.
inline constexpr llvm::StringLiteral coverageLinePointIndex =
    "obelisk.coverage.line_point_index";
/// Number of line counters expected by the embedded schema.
inline constexpr llvm::StringLiteral coverageLinePointCount =
    "obelisk.coverage.line_point_count";
/// Symbol uses that keep code units required by coverage instrumentation alive
/// through the post-inventory devirtualization and SymbolDCE pipeline. This
/// includes code units owning line obligations and callees referenced by
/// functional constructor/sample expressions before their helpers exist. The
/// references are physical compiler metadata, not schema or merge identities.
inline constexpr llvm::StringLiteral coverageRetainedCodeUnits =
    "obelisk.coverage.retained_code_units";
/// Ordered flat-toggle bindings attached to source-visible declarations.
/// Each dictionary contains base, low, and width i64 fields; aliases may bind
/// multiple coverage ranges to one canonical state range.
inline constexpr llvm::StringLiteral coverageToggleBindings =
    "obelisk.coverage.toggle_bindings";
/// Marks canonical state whose committed transitions are coverage-observable.
inline constexpr llvm::StringLiteral coverageToggleObservable =
    "obelisk.coverage.toggle_observable";
/// Number of flattened toggle bits expected by the embedded schema.
inline constexpr llvm::StringLiteral coverageToggleBitCount =
    "obelisk.coverage.toggle_bit_count";
/// Marks a design-lifetime declaration that directly represents a
/// source-authored SystemVerilog object. Coverage preparation deliberately
/// requires this marker so compiler support state never becomes an obligation.
inline constexpr llvm::StringLiteral coverageSourceAuthored =
    "obelisk.coverage.source_authored";
/// Transient source type retained through coverage preparation when
/// normalization intentionally erases reportable identity such as an enum
/// name. This is not a runtime ABI attribute.
inline constexpr llvm::StringLiteral coverageSourceType =
    "obelisk.coverage.source_type";
/// Exact flattened toggle shadow at the language-defined initial state.
inline constexpr llvm::StringLiteral coverageToggleInitialValue =
    "obelisk.coverage.toggle_initial_value";
inline constexpr llvm::StringLiteral coverageToggleInitialUnknown =
    "obelisk.coverage.toggle_initial_unknown";
/// Preparation-time feature marker used to keep whole-design class planning
/// off ordinary compilation paths.
inline constexpr llvm::StringLiteral classBitstreamSourceFeature =
    "obelisk.feature.class_bitstream_source";
inline constexpr llvm::StringLiteral thisArgument = "obelisk_sim.this_argument";
inline constexpr llvm::StringLiteral lowered = "obelisk_sim.lowered";
inline constexpr llvm::StringLiteral staticBodyFusion =
    "obelisk_sim.static_body_fusion";
inline constexpr llvm::StringLiteral staticFusion = "obelisk_sim.static_fusion";
inline constexpr llvm::StringLiteral computeKernels =
    "obelisk_sim.compute_kernels";
inline constexpr llvm::StringLiteral threeTierSchedule =
    "obelisk_sim.three_tier_schedule";
inline constexpr llvm::StringLiteral staticSpecialization =
    "obelisk_sim.static_specialization";
inline constexpr llvm::StringLiteral staticSuperstep =
    "obelisk_sim.static_superstep";
/// Marks the outer implicit wait of an `always @*` process.
inline constexpr llvm::StringLiteral topLevelWildcardWait =
    "obelisk_sim.top_level_wildcard_wait";
/// Marks a source-language procedural event control. Its controlled statement
/// executes after the wait and cannot reactivate that same wait from within
/// the active logical process.
inline constexpr llvm::StringLiteral proceduralEventWait =
    "obelisk_sim.procedural_event_wait";
/// Marks an outer explicit event control of a general-purpose `always`
/// procedure. The procedure returns to this wait after every iteration, so an
/// event enabled by its body can enqueue the next iteration.
inline constexpr llvm::StringLiteral repeatingAlwaysWait =
    "obelisk_sim.repeating_always_wait";
/// Logical DPI output-formal indices whose unused internal copy-in operands
/// were removed while preserving the externally visible DPI signature.
inline constexpr llvm::StringLiteral dpiElidedInputs =
    "obelisk_sim.dpi_elided_inputs";
/// Native-only annotation for a closed-world activation whose state and NBA
/// accesses may use the actor-boundary clean-specialization proof.
inline constexpr llvm::StringLiteral nativeGuardedSpecializationBody =
    "obelisk.native.guarded_specialization_body";
/// Marks an AOT region body whose bytecode fallback may be frozen before
/// native-only next-state and publication rewrites consume the annotation.
inline constexpr llvm::StringLiteral nativeRegionBody =
    "obelisk.native.region_body";
/// Stable semantic partition assigned before native lowering.  The value is
/// independent of worker count and is retained on generated LLVM functions so
/// object emission and incremental caches share one ownership boundary.
inline constexpr llvm::StringLiteral nativePartition =
    "obelisk.native.partition";
/// Deterministic per-design inventory of native partitions, their members,
/// imports, exports, and dependencies.
inline constexpr llvm::StringLiteral nativePartitionManifest =
    "obelisk.native.partition_manifest";
/// Module-level, design-keyed copies of native partition manifests.  Design
/// operations are erased during LLVM lowering, so later object emission and
/// cache planning consume this preserved inventory.
inline constexpr llvm::StringLiteral nativePartitionManifests =
    "obelisk.native.partition_manifests";
/// Link-complete post-lowering inventory of physical LLVM symbols and their
/// cross-partition references. Object emission consumes this manifest rather
/// than the pre-lowering semantic inventory.
inline constexpr llvm::StringLiteral nativePhysicalPartitionManifest =
    "obelisk.native.physical_partition_manifest";

// Revision-coupled eval facts shared by planning and LLVM materialization.
// These affect scheduling correctness and must not drift as ad-hoc strings
// between producer and consumer modules.
inline constexpr llvm::StringLiteral evalTier2Convergence =
    "obelisk.eval.tier2_convergence";
inline constexpr llvm::StringLiteral evalMayTerminate =
    "obelisk.eval.may_terminate";
inline constexpr llvm::StringLiteral evalInfallible = "obelisk.eval.infallible";
/// A status-returning owner whose nonzero result is a fractured cold
/// checkpoint. The periodic prefix may call it directly when it checks that
/// status before running any downstream owner.
inline constexpr llvm::StringLiteral evalCheckpointSafe =
    "obelisk.eval.checkpoint_safe";
inline constexpr llvm::StringLiteral evalTwoStateVariant =
    "obelisk.eval.two_state_variant";
inline constexpr llvm::StringLiteral evalPathGuardedTwoState =
    "obelisk.eval.path_guarded_two_state";
/// A path-guarded owner whose complete persistent state closure is known-
/// preserving. Once its recorded promotion ranges are known, the dispatcher
/// only needs to retain the checkpoint-path probe.
inline constexpr llvm::StringLiteral evalPathGuardedKnownPreserving =
    "obelisk.eval.path_guarded_known_preserving";
/// An owner whose route probe was declined, so no path predicate guards its
/// runtime leaf. Its four-state body calls the runtime inline and must stay
/// runtime-owned.
inline constexpr llvm::StringLiteral evalUnsupportedCheckpointOwner =
    "obelisk.eval.unsupported_checkpoint_owner";
inline constexpr llvm::StringLiteral evalCallClosureRoot =
    "obelisk.eval.call_closure_root";
inline constexpr llvm::StringLiteral evalTrustedTwoStateCoordinator =
    "obelisk.eval.trusted_two_state_coordinator";
inline constexpr llvm::StringLiteral evalCheckpointRoutes =
    "obelisk.eval.checkpoint_routes";
/// Producer certificate for a generated region activation that reconstructs
/// actor-side continuation arguments from canonical state on every entry.
inline constexpr llvm::StringLiteral evalReconstructsContinuationArgs =
    "obelisk.eval.reconstructs_continuation_args";
/// Per-NBA conversion certificate that the selected generated owner may use
/// its fixed root/region metadata.  Attach this before dialect conversion;
/// conversion patterns must not rediscover the fact from a parent function
/// that another pattern may already have replaced.
inline constexpr llvm::StringLiteral evalCompactNBAMetadata =
    "obelisk.eval.compact_nba_metadata";
/// Stable logical process identity attached to operations cloned into a
/// fused eval body.  The inliner propagates a call-site identity through
/// helper bodies so active-self suppression does not depend on the physical
/// coordinator that happens to contain the operation.
inline constexpr llvm::StringLiteral evalSourceOwner =
    "obelisk.eval.source_owner";
/// Inter-pass proof marker for a read-observable canonical store that a
/// dormant Tier-1 eval specialization may omit.  MaterializeComputeFusion
/// attaches it only after proving private dominating-store promotion;
/// SimulationToLLVMCoroutine consumes it while cloning the eval-private call
/// closure and removes it before dialect lowering.
inline constexpr llvm::StringLiteral evalDiscardableStore =
    "obelisk.eval.discardable_store";

inline bool isKnownBoundary(llvm::StringRef name) {
  return name == captureKind || name == descriptorId ||
         name == descriptorRootType || name == descriptorLow ||
         name == descriptorIndices || name == descriptorAggregateType ||
         name == descriptorPackedLow;
}

inline bool isKnownOperation(llvm::StringRef name) {
  return isKnownBoundary(name) || name == bindings || name == delayScale ||
         name == delayQuantum || name == hierarchicalName ||
         name == returnVariablePath || name == lowered ||
         name == randomModeIndex || name == randomObjectEdge ||
         name == randomVariableKind || name == randomVariableSigned ||
         name == randomCycleKeyField || name == randomCyclePositionField ||
         name == randomModeField || name == classBitstreamMember ||
         name == classBitstreamVisibility || name == staticBodyFusion ||
         name == evalDiscardableStore || name == staticFusion ||
         name == computeKernels || name == threeTierSchedule ||
         name == staticSpecialization || name == staticSuperstep ||
         name == topLevelWildcardWait || name == proceduralEventWait ||
         name == repeatingAlwaysWait || name == dpiElidedInputs;
}

} // namespace obelisk::sim::metadata

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H
