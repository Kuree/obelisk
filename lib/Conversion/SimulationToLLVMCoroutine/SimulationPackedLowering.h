//===- SimulationPackedLowering.h - Packed simulation conversion ------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPACKEDLOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPACKEDLOWERING_H

#include "SimulationNBALowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"

namespace obelisk::detail {

mlir::LogicalResult lowerPackedSimulationOperations(
    mlir::ModuleOp module, const llvm::DataLayout &dataLayout,
    const NativeStateLayout &stateLayout, bool enableDirectStaticState,
    const NativeStaticNBAPlan *staticNBAPlan, bool vpiAllowsWrite,
    bool experimentalTwoState);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPACKEDLOWERING_H
