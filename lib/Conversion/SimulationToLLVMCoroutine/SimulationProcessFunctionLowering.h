//===- SimulationProcessFunctionLowering.h - Native function lowering -===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSFUNCTIONLOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSFUNCTIONLOWERING_H

#include "SimulationToLLVMCoroutinePrivate.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"

namespace obelisk::detail {

struct PreparedPlainNativeProcess {
  mlir::ModuleOp module;
  mlir::func::FuncOp body;
  mlir::Location location;
  std::string baseName;
  uint64_t stableID;
  const SimulationProcessFrameAnalysis *analysis;
};

mlir::FailureOr<PreparedPlainNativeProcess> preparePlainNativeProcess(
    sim::SimFuncOp function,
    const SimulationProcessFrameAnalysis &analysis);
mlir::LogicalResult
lowerPreparedPlainNativeProcess(PreparedPlainNativeProcess &process);
mlir::LogicalResult
finishPreparedPlainNativeProcess(PreparedPlainNativeProcess &process);
mlir::LogicalResult lowerPlainNativeProcess(
    sim::SimFuncOp function,
    const SimulationProcessFrameAnalysis &analysis);
mlir::LogicalResult lowerOrdinaryFunction(sim::SimFuncOp function);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSFUNCTIONLOWERING_H
