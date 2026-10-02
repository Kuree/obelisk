//===- SimulationMetadata.h - Shared simulation metadata -------*- C++ -*-===//
//
// Transient metadata names shared by simulation lowering, analysis, and
// optimization. Keep classification here so conservative transformation
// allowlists cannot drift apart.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H

#include "obelisk/Dialect/Schedule/ScheduleFieldEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace obelisk::sim::metadata {


/// Transient function attribute containing ArgumentBindingAttr,
/// LocalBindingAttr, and ConstantBindingAttr entries.
inline constexpr llvm::StringLiteral bindings = "simulation.bindings";
/// Frozen by the bytecode encoder from complete call-closure effect analysis.
/// Recomputed whenever the bytecode image is rebuilt.
inline constexpr llvm::StringLiteral bytecodeReadOnly =
    "simulation.bytecode_read_only";
/// Immutable canonical-frame certificate and module checkpoint inventory.
/// Valid only at the Frames boundary, before derived native specialization.
inline constexpr llvm::StringLiteral nativeFrameInputs =
    "simulation.native_frame_inputs";
inline constexpr llvm::StringLiteral nativeFrameCheckpoint =
    "simulation.native_frame_checkpoint";
inline constexpr llvm::StringLiteral delayScale = "simulation.delay_scale";
inline constexpr llvm::StringLiteral delayQuantum = "simulation.delay_quantum";
inline constexpr llvm::StringLiteral captureKind = "simulation.capture_kind";
inline constexpr llvm::StringLiteral descriptorId = "simulation.descriptor_id";
inline constexpr llvm::StringLiteral descriptorRootType =
    "simulation.descriptor_root_type";
inline constexpr llvm::StringLiteral descriptorLow =
    "simulation.descriptor_low";
inline constexpr llvm::StringLiteral descriptorIndices =
    "simulation.descriptor_indices";
inline constexpr llvm::StringLiteral descriptorAggregateType =
    "simulation.descriptor_aggregate_type";
inline constexpr llvm::StringLiteral descriptorPackedLow =
    "simulation.descriptor_packed_low";
inline constexpr llvm::StringLiteral hierarchicalName =
    "simulation.hierarchical_name";
/// Exact frontend enum identity retained solely to reconnect a value object
/// to its anonymous enum typespec in the immutable VPI relation image.
inline constexpr llvm::StringLiteral vpiSourceTypeIdentity =
    "simulation.vpi_source_type_identity";
/// FlatSymbolRefAttr naming the immutable relation-backed anchor that owns the
/// public VPI identity of executable named-event-array storage. The storage
/// remains in the execution layout but is omitted from the VPI object
/// inventory.
inline constexpr llvm::StringLiteral vpiIdentityDelegated =
    "simulation.vpi_identity_delegated";
/// Marks a storage descriptor that a subroutine owns. Its writers are the
/// subroutine's callers rather than drivers of a design variable.
inline constexpr llvm::StringLiteral subroutineStorage =
    "simulation.subroutine_storage";
/// Hierarchical path of the variable that holds a function's return value.
inline constexpr llvm::StringLiteral returnVariablePath =
    "simulation.return_variable_path";
/// Stable base-to-derived index of an effective rand/randc class property.
inline constexpr llvm::StringLiteral randomModeIndex =
    "simulation.random_mode_index";
/// Marks a non-static rand class-handle field as a recursive object edge.
inline constexpr llvm::StringLiteral randomObjectEdge =
    "simulation.random_object_edge";
/// Typed RandomVariableKindAttr on a direct packed instance rand property.
inline constexpr llvm::StringLiteral randomVariableKind =
    "simulation.random_variable_kind";
/// Source signedness of a direct packed random variable.
inline constexpr llvm::StringLiteral randomVariableSigned =
    "simulation.random_variable_signed";
/// Hidden i64 fields that carry one randc property's permutation state.
inline constexpr llvm::StringLiteral randomCycleKeyField =
    "simulation.random_cycle_key_field";
inline constexpr llvm::StringLiteral randomCyclePositionField =
    "simulation.random_cycle_position_field";
/// Root-class field containing the 64-bit disabled-property mask.
inline constexpr llvm::StringLiteral randomModeField =
    "simulation.random_mode_field";
/// Marks an executable class field that corresponds to a source-declared
/// instance property. Compiler-owned fields and static properties are absent
/// from the object bit-stream inventory.
inline constexpr llvm::StringLiteral classBitstreamMember =
    "simulation.class_bitstream_member";
/// Source member visibility retained for recursive class bit-stream legality.
inline constexpr llvm::StringLiteral classBitstreamVisibility =
    "simulation.class_bitstream_visibility";
/// Marks an explicit class-containing bit-stream conversion whose source is
/// exactly the enclosing method's current-instance `this`.  This exception is
/// semantic: aliases and handles reached through another expression never
/// inherit it.
inline constexpr llvm::StringLiteral classBitstreamAllowHiddenRoot =
    "simulation.class_bitstream_allow_hidden_root";
/// Dense nonzero identifier of a materialized class bit-stream cast site.
inline constexpr llvm::StringLiteral classBitstreamSiteID =
    "simulation.class_bitstream_site_id";
inline constexpr llvm::StringLiteral classBitstreamBytecodeFunction =
    "simulation.class_bitstream_bytecode_function";
inline constexpr llvm::StringLiteral classBitstreamBytecodeSite =
    "simulation.class_bitstream_bytecode_site";
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
    "simulation.coverage.functional.with_expression_ids";
inline constexpr llvm::StringLiteral coverageFunctionalWithExpressionOrdinals =
    "simulation.coverage.functional.with_expression_ordinals";
inline constexpr llvm::StringLiteral coverageFunctionalWithCandidateValues =
    "simulation.coverage.functional.with_candidate_values";
inline constexpr llvm::StringLiteral coverageFunctionalWithIteratorPath =
    "simulation.coverage.functional.with_iterator_path";
/// Constructor-time cross-selector predicates are evaluated once for every
/// tuple in the finite Cartesian product of their cross targets. The values
/// are flattened tuple-major (the final target varies fastest); target paths
/// identify the semantic iterator bindings restored around each evaluation.
inline constexpr llvm::StringLiteral
    coverageFunctionalCrossWithCandidateValues =
        "simulation.coverage.functional.cross_with_candidate_values";
inline constexpr llvm::StringLiteral coverageFunctionalCrossWithTargetPaths =
    "simulation.coverage.functional.cross_with_target_paths";
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
inline constexpr llvm::StringLiteral thisArgument = "simulation.this_argument";
inline constexpr llvm::StringLiteral lowered = "simulation.lowered";

inline constexpr llvm::StringLiteral dpiElidedInputs =
    "simulation.dpi_elided_inputs";
/// Native-only annotation for a closed-world activation whose state and NBA
/// accesses may use the actor-boundary clean-specialization proof.
/// Marks an AOT region body whose bytecode fallback may be frozen before
/// native-only next-state and publication rewrites consume the annotation.
/// Stable semantic partition assigned before native lowering.  The value is
/// independent of worker count and is retained on generated LLVM functions so
/// object emission and incremental caches share one ownership boundary.
inline constexpr llvm::StringLiteral nativePartition =
    "obelisk.native.partition";
/// The driver is lowering a complete executable and may omit unreferenced
/// implementation helpers. Object and IR emission keep their public helpers.
inline constexpr llvm::StringLiteral nativeClosedExecutable =
    "obelisk.native.closed_executable";
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


inline bool isKnownBoundary(llvm::StringRef name) {
  if (auto field = schedule::symbolizeField(name))
    return !schedule::metadata::requiresOperationScope(*field);
  return name == captureKind || name == descriptorId ||
         name == descriptorRootType || name == descriptorLow ||
         name == descriptorIndices || name == descriptorAggregateType ||
         name == descriptorPackedLow;
}

inline bool isKnownOperation(llvm::StringRef name) {
  if (auto field = schedule::symbolizeField(name))
    return !schedule::metadata::pinsOperationBoundary(*field);
  return isKnownBoundary(name) || name == bindings || name == delayScale ||
         name == delayQuantum || name == hierarchicalName ||
         name == returnVariablePath || name == lowered ||
         name == randomModeIndex || name == randomObjectEdge ||
         name == randomVariableKind || name == randomVariableSigned ||
         name == randomCycleKeyField || name == randomCyclePositionField ||
         name == randomModeField || name == classBitstreamMember ||
         name == classBitstreamVisibility ||
         name == schedule::metadata::staticBodyFusion ||
         name == schedule::metadata::evalDiscardableStore ||
         name == schedule::metadata::staticFusion ||
         name == schedule::metadata::computeKernels ||
         name == schedule::metadata::threeTierSchedule ||
         name == schedule::metadata::staticSpecialization ||
         name == schedule::metadata::staticSuperstep ||
         name == schedule::metadata::topLevelWildcardWait ||
         name == schedule::metadata::proceduralEventWait ||
         name == schedule::metadata::repeatingAlwaysWait ||
         name == dpiElidedInputs;
}

} // namespace obelisk::sim::metadata

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H
