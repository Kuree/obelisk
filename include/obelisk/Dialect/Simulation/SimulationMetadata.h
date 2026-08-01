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
inline constexpr llvm::StringLiteral lowered = "obelisk_sim.lowered";
inline constexpr llvm::StringLiteral staticBodyFusion =
    "obelisk_sim.static_body_fusion";
inline constexpr llvm::StringLiteral staticFusion = "obelisk_sim.static_fusion";
inline constexpr llvm::StringLiteral computeKernels =
    "obelisk_sim.compute_kernels";
inline constexpr llvm::StringLiteral staticSpecialization =
    "obelisk_sim.static_specialization";
inline constexpr llvm::StringLiteral staticSuperstep =
    "obelisk_sim.static_superstep";
/// Native-only annotation for a closed-world activation whose state and NBA
/// accesses may use the actor-boundary clean-specialization proof.
inline constexpr llvm::StringLiteral nativeGuardedSpecializationBody =
    "obelisk.native.guarded_specialization_body";

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
         name == computeKernels ||
         name == staticSpecialization || name == staticSuperstep;
}

} // namespace obelisk::sim::metadata

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H
