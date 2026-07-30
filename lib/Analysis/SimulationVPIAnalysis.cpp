//===- SimulationVPIAnalysis.cpp - VPI capability policy ----------------===//

#include "obelisk/Analysis/SimulationVPIAnalysis.h"

#include "llvm/Support/ErrorHandling.h"

namespace obelisk::analysis {

SimulationVPIAnalysis SimulationVPIAnalysis::compute(sim::SimDesignOp design) {
  SimulationVPIAnalysis result;
  sim::ComputeGraphAttr graph = design ? design.getComputeGraphAttr() : nullptr;
  if (graph) {
    result.mode = graph.getVpi();
    result.computeGraph = true;
  }
  return result;
}

SimulationVPIAnalysis SimulationVPIAnalysis::forMode(sim::ComputeVPIMode mode) {
  SimulationVPIAnalysis result;
  result.mode = mode;
  return result;
}

sim::ComputeObservabilityKind SimulationVPIAnalysis::getObservability() const {
  switch (mode) {
  case sim::ComputeVPIMode::Off:
    return sim::ComputeObservabilityKind::Invisible;
  case sim::ComputeVPIMode::Read:
    return sim::ComputeObservabilityKind::SafePoint;
  case sim::ComputeVPIMode::Full:
    return sim::ComputeObservabilityKind::ExternallyWritable;
  }
  llvm_unreachable("unknown VPI mode");
}

bool SimulationVPIAnalysis::allowsRead() const {
  return mode != sim::ComputeVPIMode::Off;
}

bool SimulationVPIAnalysis::allowsWrite() const {
  return mode == sim::ComputeVPIMode::Full;
}

} // namespace obelisk::analysis
