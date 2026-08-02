//===- SimulationProcessWrapperLowering.h - Native process wrappers ---===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSWRAPPERLOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSWRAPPERLOWERING_H

#include "SimulationToLLVMCoroutinePrivate.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"

namespace obelisk::detail {

void publishAction(mlir::OpBuilder &builder, mlir::Location location,
                   mlir::Value instance, uint32_t actionKind,
                   uint32_t suspendKind, uint32_t continuation,
                   uint32_t flags, mlir::Value payload, uint64_t auxiliary);
mlir::LogicalResult makeNativeWrappers(mlir::ModuleOp module,
                                       mlir::LLVM::LLVMFuncOp ramp,
                                       llvm::StringRef baseName);
mlir::LogicalResult makePlainNativeWrappers(
    mlir::ModuleOp module, mlir::func::FuncOp body, llvm::StringRef baseName,
    const SimulationProcessFrameAnalysis &analysis);
mlir::LogicalResult makeDirectFragmentWrapper(
    mlir::ModuleOp module, sim::SimFuncOp body, sim::SimFuncOp actor,
    llvm::StringRef wrapperName, uint32_t actorSlot, uint32_t continuation,
    const SimulationProcessFrameAnalysis &analysis);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSWRAPPERLOWERING_H
