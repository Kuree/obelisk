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
inline constexpr llvm::StringLiteral thisArgument =
    "obelisk_sim.this_argument";
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
/// Marks the outer implicit wait of an `always @*` process. Publications from
/// the active process itself cannot satisfy the wait it will reach next.
inline constexpr llvm::StringLiteral topLevelWildcardWait =
    "obelisk_sim.top_level_wildcard_wait";
/// Native-only annotation for a closed-world activation whose state and NBA
/// accesses may use the actor-boundary clean-specialization proof.
inline constexpr llvm::StringLiteral nativeGuardedSpecializationBody =
    "obelisk.native.guarded_specialization_body";
/// Marks an AOT region body whose bytecode fallback may be frozen before
/// native-only next-state and publication rewrites consume the annotation.
inline constexpr llvm::StringLiteral nativeRegionBody =
    "obelisk.native.region_body";

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

inline bool isKnownBoundary(llvm::StringRef name) {
  return name == captureKind || name == descriptorId ||
         name == descriptorRootType || name == descriptorLow ||
         name == descriptorIndices || name == descriptorAggregateType ||
         name == descriptorPackedLow;
}

inline bool isKnownOperation(llvm::StringRef name) {
  return isKnownBoundary(name) || name == bindings || name == delayScale ||
         name == delayQuantum || name == hierarchicalName || name == lowered ||
         name == staticBodyFusion || name == staticFusion ||
         name == computeKernels || name == threeTierSchedule ||
         name == staticSpecialization || name == staticSuperstep ||
         name == topLevelWildcardWait;
}

} // namespace obelisk::sim::metadata

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H
