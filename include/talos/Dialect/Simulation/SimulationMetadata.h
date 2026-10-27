//===- SimulationMetadata.h - Shared simulation metadata -------*- C++ -*-===//
//
// Transient metadata names shared by simulation lowering, analysis, and
// optimization. Keep classification here so conservative transformation
// allowlists cannot drift apart.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_DIALECT_SIMULATION_SIMULATIONMETADATA_H
#define TALOS_DIALECT_SIMULATION_SIMULATIONMETADATA_H

#include "talos/Dialect/DPI/DPIDialect.h"
#include "talos/Dialect/Schedule/ScheduleFieldEnums.h"
#include "talos/Dialect/Schedule/ScheduleMetadata.h"
#include "talos/Dialect/Simulation/SimulationDialect.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace talos::sim::metadata {

inline bool isKnownBoundary(llvm::StringRef name) {
  if (auto field = schedule::symbolizeField(name))
    return !schedule::metadata::requiresOperationScope(*field);
  return name == CaptureKind::getNameStr() ||
         name == DescriptorId::getNameStr() ||
         name == IntegerRange::getNameStr() ||
         name == DescriptorRootType::getNameStr() ||
         name == DescriptorLow::getNameStr() ||
         name == DescriptorIndices::getNameStr() ||
         name == DescriptorAggregateType::getNameStr() ||
         name == DescriptorPackedLow::getNameStr();
}

inline bool isKnownOperation(llvm::StringRef name) {
  if (auto field = schedule::symbolizeField(name))
    return !schedule::metadata::pinsOperationBoundary(*field);
  return isKnownBoundary(name) || name == Bindings::getNameStr() ||
         name == DelayScale::getNameStr() ||
         name == DelayQuantum::getNameStr() ||
         name == HierarchicalName::getNameStr() ||
         name == ReturnVariablePath::getNameStr() ||
         name == VpiCallSite::getNameStr() ||
         name == Lowered::getNameStr() ||
         name == RandomModeIndex::getNameStr() ||
         name == RandomObjectEdge::getNameStr() ||
         name == RandomVariableKind::getNameStr() ||
         name == RandomVariableSigned::getNameStr() ||
         name == RandomCycleKeyField::getNameStr() ||
         name == RandomCyclePositionField::getNameStr() ||
         name == RandomModeField::getNameStr() ||
         name == ClassBitstreamMember::getNameStr() ||
         name == EmbeddedCovergroup::getNameStr() ||
         name == ClassBitstreamVisibility::getNameStr() ||
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
         name == dpi::ElidedInputs::getNameStr() ||
         name == ResultRanges::getNameStr() ||
         name == ResultExprs::getNameStr() ||
         name == KnownLogicResult::getNameStr() ||
         name == InlineLegalityCache::getNameStr();
}

/// Namespaces whose attributes are compiler-owned facts. A transformation
/// that clones, inlines or erases an operation must understand each such
/// attribute it carries: `isKnownOperation` and `isKnownBoundary` list the
/// ones that survive these transformations.
inline bool isCompilerMetadata(llvm::StringRef name) {
  return name.starts_with("simulation.") || name.starts_with("schedule.") ||
         name.starts_with("dpi.");
}

} // namespace talos::sim::metadata

#endif // TALOS_DIALECT_SIMULATION_SIMULATIONMETADATA_H
