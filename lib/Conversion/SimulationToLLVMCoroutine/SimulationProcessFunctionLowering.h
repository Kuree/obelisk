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

struct PreparedOrdinaryNativeFunction {
  mlir::func::FuncOp body;
  bool observer;
  mlir::IntegerAttr observerWidth;
  mlir::BoolAttr observerFourState;
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
mlir::FailureOr<PreparedOrdinaryNativeFunction>
prepareOrdinaryFunction(sim::SimFuncOp function);
mlir::LogicalResult
lowerPreparedOrdinaryFunction(PreparedOrdinaryNativeFunction &function);
mlir::LogicalResult lowerOrdinaryFunction(sim::SimFuncOp function);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSFUNCTIONLOWERING_H
