//===- SimulationVPIAnalysis.cpp - VPI capability policy ----------------===//

#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "llvm/Support/ErrorHandling.h"

namespace obelisk::analysis {

SimulationVPIAnalysis SimulationVPIAnalysis::compute(sim::SimDesignOp design) {
  SimulationVPIAnalysis result;
  schedule::ComputeGraphAttr graph =
      design ? design.getComputeGraphAttr() : nullptr;
  if (graph) {
    result.mode = graph.getVpi();
    result.computeGraph = true;
  }
  return result;
}

SimulationVPIAnalysis
SimulationVPIAnalysis::forMode(schedule::ComputeVPIMode mode) {
  SimulationVPIAnalysis result;
  result.mode = mode;
  return result;
}

schedule::ComputeObservabilityKind
SimulationVPIAnalysis::getObservability() const {
  switch (mode) {
  case schedule::ComputeVPIMode::Off:
    return schedule::ComputeObservabilityKind::Invisible;
  case schedule::ComputeVPIMode::Read:
    return schedule::ComputeObservabilityKind::SafePoint;
  case schedule::ComputeVPIMode::Full:
    return schedule::ComputeObservabilityKind::ExternallyWritable;
  }
  llvm_unreachable("unknown VPI mode");
}

bool SimulationVPIAnalysis::allowsRead() const {
  return mode != schedule::ComputeVPIMode::Off;
}

bool SimulationVPIAnalysis::allowsWrite() const {
  return mode == schedule::ComputeVPIMode::Full;
}

} // namespace obelisk::analysis
