//===- SimulationCopyProcessAnalysis.h - Copy admission ---------*- C++ -*-===//

#ifndef TALOS_ANALYSIS_SIMULATIONCOPYPROCESSANALYSIS_H
#define TALOS_ANALYSIS_SIMULATIONCOPYPROCESSANALYSIS_H

#include <optional>

namespace talos::sim {
class SimFuncOp;
}

namespace talos::analysis {

struct CaptureCopyProcess {
  unsigned source;
  unsigned destination;
};

/// Admit an Active port/continuous process consisting of an immediate packed
/// storage copy followed by a change wait on that same source. Both references
/// must be entry storage captures; no conversion, driver resolution, dynamic
/// selector, carried value, or other effect is admitted. Call before state
/// threading and packed lowering obscure this source shape.
bool isCaptureCopyProcess(sim::SimFuncOp function);
std::optional<CaptureCopyProcess>
getCaptureCopyProcess(sim::SimFuncOp function);

} // namespace talos::analysis

#endif
