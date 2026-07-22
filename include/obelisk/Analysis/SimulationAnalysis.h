//===- SimulationAnalysis.h - Simulation analysis utilities ----*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_SIMULATIONANALYSIS_H
#define OBELISK_ANALYSIS_SIMULATIONANALYSIS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Value.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"

#include <cstdint>
#include <optional>

namespace obelisk::analysis {

/// Concrete descriptor provenance recomputed from executable SSA and CFG.
/// Absence from a map means no fact has reached the value; a present unknown
/// fact means analysis proved that the value cannot retain concrete identity.
struct DescriptorProvenance {
  sim::ComputeResourceKind resource = sim::ComputeResourceKind::Unknown;
  std::optional<uint64_t> descriptor;
  std::optional<unsigned> formal;
  uint64_t low = 0;
  uint64_t width = 0;
  uint64_t rootWidth = 0;
  bool dynamic = false;

  bool operator==(const DescriptorProvenance &other) const {
    return resource == other.resource && descriptor == other.descriptor &&
           formal == other.formal && low == other.low && width == other.width &&
           rootWidth == other.rootWidth && dynamic == other.dynamic;
  }
};

using DescriptorProvenanceMap =
    llvm::DenseMap<mlir::Value, DescriptorProvenance>;

/// Derive stable descriptor roots and ranges for all handle-typed values in a
/// defined simulation function. Driver handles are normalized to their net.
DescriptorProvenanceMap deriveDescriptorProvenance(sim::SimFuncOp function);

/// Weighted cost shared by IPO growth accounting and compute-graph lane
/// balancing. Terminators are free, ordinary operations cost one, state access
/// costs three, and a remaining direct call costs five.
uint64_t getSimulationOperationCost(mlir::Operation &operation);

/// Sum weighted operation cost recursively below an operation or region.
uint64_t getSimulationOperationCost(mlir::Operation *operation);
uint64_t getSimulationRegionCost(mlir::Region &region);

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_SIMULATIONANALYSIS_H
