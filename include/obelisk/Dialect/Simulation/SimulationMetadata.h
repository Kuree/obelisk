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

namespace obelisk::sim::metadata {

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

inline bool isKnownBoundary(llvm::StringRef name) {
  return name == captureKind || name == descriptorId ||
         name == descriptorRootType || name == descriptorLow ||
         name == descriptorIndices || name == descriptorAggregateType ||
         name == descriptorPackedLow;
}

inline bool isKnownOperation(llvm::StringRef name) {
  return isKnownBoundary(name) || name == bindings || name == delayScale ||
         name == delayQuantum || name == hierarchicalName || name == lowered;
}

} // namespace obelisk::sim::metadata

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H
