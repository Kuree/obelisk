//===- SimulationVPIAnalysis.h - VPI capability policy --------*- C++ -*-===//
//
// Read-only analysis of the VPI capabilities recorded in a simulation design.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_ANALYSIS_SIMULATIONVPIANALYSIS_H
#define OBELISK_ANALYSIS_SIMULATIONVPIANALYSIS_H

#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Simulation/SimulationEnums.h"

namespace obelisk::sim {
class SimDesignOp;
}

namespace obelisk::analysis {

/// Semantic VPI capabilities for one simulation design.
///
/// Consumers ask capability questions instead of interpreting
/// schedule::ComputeVPIMode themselves. This keeps native lowering, bytecode
/// encoding, and schedule specialization on the same policy.
class SimulationVPIAnalysis {
public:
  static SimulationVPIAnalysis compute(sim::SimDesignOp design);
  static SimulationVPIAnalysis forMode(schedule::ComputeVPIMode mode);

  bool hasComputeGraph() const { return computeGraph; }
  schedule::ComputeVPIMode getMode() const { return mode; }

  schedule::ComputeObservabilityKind getObservability() const;
  bool allowsRead() const;
  bool allowsWrite() const;

  /// Whether external access preserves the compiler's closed-world dependency
  /// inventory. Read-only observation does; external writes do not.
  bool preservesStaticDependencies() const {
    return computeGraph && !allowsWrite();
  }

private:
  schedule::ComputeVPIMode mode = schedule::ComputeVPIMode::Off;
  bool computeGraph = false;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_SIMULATIONVPIANALYSIS_H
