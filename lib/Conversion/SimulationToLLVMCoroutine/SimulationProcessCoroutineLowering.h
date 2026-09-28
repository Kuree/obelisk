//===- SimulationProcessCoroutineLowering.h - Native process lowering -===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H

#include "SimulationProcessWrapperLowering.h"
#include "SimulationTableProcess.h"
#include "SimulationToLLVMCoroutinePrivate.h"

namespace obelisk::detail {

struct PreparedSuspendableProcess {
  mlir::ModuleOp module;
  mlir::LLVM::LLVMFuncOp ramp;
  mlir::Location location;
  std::string baseName;
  uint64_t stableID;
  const SimulationProcessFrameAnalysis *analysis;
  bool unmanagedNative;
  bool directActivation;
  bool copyActivation;
  CopyKernelBinding copyKernel;
  std::optional<NativeTableProcess> tableProcess;
};

void materializeCopyKernels(
    mlir::ModuleOp module,
    llvm::MutableArrayRef<PreparedSuspendableProcess> processes);

mlir::FailureOr<PreparedSuspendableProcess>
prepareSuspendableProcess(sim::SimFuncOp function,
                          const SimulationProcessFrameAnalysis &analysis,
                          bool copyActivation = false,
                          std::optional<NativeTableProcess> tableProcess = {});
mlir::LogicalResult
lowerPreparedSuspendableProcess(PreparedSuspendableProcess &process);
mlir::LogicalResult
finishPreparedSuspendableProcess(PreparedSuspendableProcess &process,
                                 const mlir::SymbolTable &embeddedSymbols);
mlir::LogicalResult
lowerSuspendableProcess(sim::SimFuncOp function,
                        const SimulationProcessFrameAnalysis &analysis);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSCOROUTINELOWERING_H
