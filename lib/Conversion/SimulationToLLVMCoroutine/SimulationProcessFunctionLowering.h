//===- SimulationProcessFunctionLowering.h - Native function lowering -===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSFUNCTIONLOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSFUNCTIONLOWERING_H

#include "SimulationToLLVMCoroutinePrivate.h"

namespace obelisk::detail {

mlir::LogicalResult lowerPlainNativeProcess(
    sim::SimFuncOp function,
    const SimulationProcessFrameAnalysis &analysis);
mlir::LogicalResult lowerOrdinaryFunction(sim::SimFuncOp function);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSFUNCTIONLOWERING_H

