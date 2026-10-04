//===- SimulationToLLVMCoroutine.h - Process coroutine lowering -*- C++ -*-===//

#ifndef OBELISK_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H
#define OBELISK_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H

#include "obelisk/Conversion/Passes.h"

#include "mlir/Transforms/DialectConversion.h"

namespace llvm {
class DataLayout;
}

namespace mlir {
class LLVMTypeConverter;
class ModuleOp;
class OpPassManager;
class RewritePatternSet;
class Operation;
} // namespace mlir

namespace obelisk {

/// Materialize capture-free, scope-specific bridges for every prepared DPI
/// exported function. This must run before bytecode encoding.
mlir::LogicalResult materializeDPIExportBridges(mlir::ModuleOp module);

/// Append each native planning, specialization and lowering pass in ABI order.
void buildSimulationToLLVMCoroutinePipeline(mlir::OpPassManager &manager);
void registerSimulationToLLVMCoroutinePipeline();

/// Lower executable schedule actions after their effect-aware optimization.
void materializeNativeScheduleActions(mlir::Operation *operation,
                                      bool coalesce = true,
                                      bool preserveReadiness = false);
/// Materialize actions introduced after the main LLVM conversion, including
/// the integer arithmetic and CFG needed by local readiness regions.
mlir::LogicalResult
lowerNativeScheduleActionsToLLVM(mlir::Operation *operation);

/// Add terminal conversion patterns after the native preparation pipeline.
void populateSimulationCoroutineToLLVMPatterns(
    const mlir::LLVMTypeConverter &converter,
    mlir::RewritePatternSet &patterns);

} // namespace obelisk

#endif // OBELISK_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H
