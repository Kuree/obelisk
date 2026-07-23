//===- EliminateDeadBoundaries.h - Simulation boundary pruning -*- C++ -*-===//

#ifndef OBELISK_CONVERSION_OBELISKTOSIMULATION_ELIMINATEDEADBOUNDARIES_H
#define OBELISK_CONVERSION_OBELISKTOSIMULATION_ELIMINATEDEADBOUNDARIES_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Pass/Pass.h"

namespace obelisk {

/// Optional pass-owned counters populated by the shared boundary eliminator.
struct EliminationStatistics {
  mlir::Pass::Statistic *functionsConsidered = nullptr;
  mlir::Pass::Statistic *abiPinnedFunctions = nullptr;
  mlir::Pass::Statistic *functionsPruned = nullptr;
  mlir::Pass::Statistic *argumentsRemoved = nullptr;
  mlir::Pass::Statistic *resultsRemoved = nullptr;
  mlir::Pass::Statistic *callOperandsRemoved = nullptr;
  mlir::Pass::Statistic *spawnOperandsRemoved = nullptr;
  mlir::Pass::Statistic *returnOperandsRemoved = nullptr;
  mlir::Pass::Statistic *callsRebuilt = nullptr;
  mlir::Pass::Statistic *pureCallsErased = nullptr;
};

/// Run the shared whole-design boundary analysis and serial mutation.
mlir::LogicalResult
eliminateDeadSimulationBoundaries(sim::SimDesignOp design,
                                  bool eliminateResults, bool missedRemarks,
                                  EliminationStatistics statistics);

} // namespace obelisk

#endif // OBELISK_CONVERSION_OBELISKTOSIMULATION_ELIMINATEDEADBOUNDARIES_H
