//===- SimulationCopyProcessAnalysis.h - Copy admission ---------*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_SIMULATIONCOPYPROCESSANALYSIS_H
#define OBELISK_ANALYSIS_SIMULATIONCOPYPROCESSANALYSIS_H

namespace obelisk::sim {
class SimFuncOp;
}

namespace obelisk::analysis {

/// Admit an Active port/continuous process consisting of an immediate packed
/// storage copy followed by a change wait on that same source. Both references
/// must be entry storage captures; no conversion, driver resolution, dynamic
/// selector, carried value, or other effect is admitted. Call before state
/// threading and packed lowering obscure this source shape.
bool isCaptureCopyProcess(sim::SimFuncOp function);

} // namespace obelisk::analysis

#endif
