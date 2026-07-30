//===- SimulationProcessCoroutineLowering.h - Native process lowering -===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H

#include "SimulationToLLVMCoroutinePrivate.h"

namespace obelisk::detail {

mlir::LogicalResult lowerSuspendableProcess(
    sim::SimFuncOp function,
    const SimulationProcessFrameAnalysis &analysis);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H
