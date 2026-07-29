//===- ComputeFusion.h - Static process-body fusion helpers -----*- C++ -*-===//

#ifndef OBELISK_CONVERSION_OBELISKTOSIMULATION_COMPUTEFUSION_H
#define OBELISK_CONVERSION_OBELISKTOSIMULATION_COMPUTEFUSION_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

namespace obelisk {

/// Return true when a process body can be merged without combining
/// actor-local state or admitting behavior outside the static digital subset.
bool isComputeBodyFusionEligible(sim::SimFuncOp function);

/// Return continuation targets that can coexist in the Active ready set when
/// the given sensitivity awakens. A constant-delay continuation is excluded
/// only when graph activation edges prove it is the unique producer currently
/// publishing the sensitivity; independent deadlines remain barriers.
mlir::SmallVector<uint32_t>
getComputeFusionReadyTargets(sim::ComputeGraphAttr graph,
                             sim::ComputeEffectAttr sensitivity);

} // namespace obelisk

#endif // OBELISK_CONVERSION_OBELISKTOSIMULATION_COMPUTEFUSION_H
