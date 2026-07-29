//===- SimulationScheduleAnalysis.h - Shared schedule ranks ----*- C++ -*-===//
//
// Read-only process and continuation ordering derived from the compute graph.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_ANALYSIS_SIMULATIONSCHEDULEANALYSIS_H
#define OBELISK_ANALYSIS_SIMULATIONSCHEDULEANALYSIS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"

#include <cstdint>
#include <optional>

namespace obelisk::analysis {

/// Resolve a compute-graph block ordinal, excluding transient observer capture
/// bridge blocks that are not part of the graph ABI.
mlir::Block *lookupComputeGraphBlock(sim::SimFuncOp function, uint32_t ordinal);

/// Deterministic scheduler ranks shared by native and bytecode execution.
class SimulationScheduleAnalysis {
public:
  static mlir::FailureOr<SimulationScheduleAnalysis>
  compute(mlir::ModuleOp module);
  static mlir::FailureOr<SimulationScheduleAnalysis>
  compute(sim::SimDesignOp design);

  std::optional<uint32_t> getEntryRank(mlir::Operation *function) const;
  std::optional<uint32_t> getBlockRank(mlir::Block *block) const;

  const llvm::DenseMap<mlir::Operation *, uint32_t> &getEntryRanks() const {
    return entryRanks;
  }
  const llvm::DenseMap<mlir::Block *, uint32_t> &getBlockRanks() const {
    return blockRanks;
  }

private:
  llvm::DenseMap<mlir::Operation *, uint32_t> entryRanks;
  llvm::DenseMap<mlir::Block *, uint32_t> blockRanks;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_SIMULATIONSCHEDULEANALYSIS_H
