//===- SimulationProcessCoroutineLowering.h - Native process lowering -===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H

#include "SimulationToLLVMCoroutinePrivate.h"

namespace obelisk::detail {

struct PreparedSuspendableProcess {
  mlir::ModuleOp module;
  mlir::LLVM::LLVMFuncOp ramp;
  mlir::Location location;
  std::string baseName;
  uint64_t stableID;
  const SimulationProcessFrameAnalysis *analysis;
};

mlir::FailureOr<PreparedSuspendableProcess> prepareSuspendableProcess(
    sim::SimFuncOp function,
    const SimulationProcessFrameAnalysis &analysis);
mlir::LogicalResult
lowerPreparedSuspendableProcess(PreparedSuspendableProcess &process);
mlir::LogicalResult
finishPreparedSuspendableProcess(PreparedSuspendableProcess &process);
mlir::LogicalResult lowerSuspendableProcess(
    sim::SimFuncOp function,
    const SimulationProcessFrameAnalysis &analysis);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H
