//===- SimulationProcessCoroutineLowering.h - Native process lowering -===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H

#include "SimulationToLLVMCoroutinePrivate.h"

namespace obelisk::detail {

mlir::LogicalResult lowerPlainNativeProcess(
    sim::SimFuncOp function,
    const SimulationProcessFrameAnalysis &analysis);
mlir::LogicalResult lowerSuspendableProcess(
    sim::SimFuncOp function,
    const SimulationProcessFrameAnalysis &analysis);
mlir::LogicalResult lowerOrdinaryFunction(sim::SimFuncOp function);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H

