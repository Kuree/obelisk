//===- SimulationPackedLowering.h - Packed simulation conversion ------===//

#ifndef TALOS_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPACKEDLOWERING_H
#define TALOS_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPACKEDLOWERING_H

#include "SimulationNBALowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"

namespace mlir {
class Pass;
}

namespace talos::detail {

std::unique_ptr<mlir::Pass> createNativeFunctionFinalizationPass();
std::unique_ptr<mlir::Pass> createPublishNativeFunctionProofsPass();
mlir::LogicalResult
prepareNativeProofPublicationInputs(mlir::ModuleOp module,
                                    mlir::AnalysisManager manager);

mlir::LogicalResult
prepareNativeFunctionFinalizationInputs(mlir::ModuleOp module,
                                        mlir::AnalysisManager manager);

mlir::LogicalResult lowerPackedSimulationOperations(
    mlir::ModuleOp module, const llvm::DataLayout &dataLayout,
    const NativeStateLayout &stateLayout, bool enableDirectStaticState,
    const NativeStaticNBAPlan *staticNBAPlan, bool vpiAllowsWrite,
    bool experimentalTwoState,
    llvm::function_ref<mlir::LogicalResult()> threadStatuses);

} // namespace talos::detail

#endif // TALOS_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPACKEDLOWERING_H
