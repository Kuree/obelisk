//===- SimulationToLLVMCoroutine.h - Process coroutine lowering -*- C++ -*-===//

#ifndef TALOS_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H
#define TALOS_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H

#include "talos/Conversion/Passes.h"

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
class SymbolTableCollection;
} // namespace mlir

namespace talos {

/// Materialize capture-free, scope-specific bridges for every prepared DPI
/// exported function. This must run before bytecode encoding.
mlir::LogicalResult materializeDPIExportBridges(mlir::ModuleOp module);

/// Append each native planning, specialization and lowering pass in ABI order.
void buildSimulationToLLVMCoroutinePipeline(mlir::OpPassManager &manager,
                                            bool nativeWords = true,
                                            bool nativeAggregates = true);
void registerSimulationToLLVMCoroutinePipeline();

/// Lower executable schedule actions after their effect-aware optimization.
/// A caller lowering many functions of one module passes `symbols`, a cache
/// kept coherent with its own symbol changes, so each call does not rebuild
/// the module symbol table.
void materializeNativeScheduleActions(
    mlir::Operation *operation, bool coalesce = true,
    bool preserveReadiness = false,
    mlir::SymbolTableCollection *symbols = nullptr);
/// Materialize actions introduced after the main LLVM conversion, including
/// the integer arithmetic and CFG needed by local readiness regions.
mlir::LogicalResult lowerNativeScheduleActionsToLLVM(
    mlir::Operation *operation, mlir::SymbolTableCollection *symbols = nullptr);

/// Address each native state plane through one invariant base per function
/// that accesses it in several blocks, instead of constant offsets from the
/// plane global. Run last, just before translation to LLVM IR.
mlir::LogicalResult addressNativeStateThroughBases(mlir::ModuleOp module);

/// Add terminal conversion patterns after the native preparation pipeline.
void populateSimulationCoroutineToLLVMPatterns(
    const mlir::LLVMTypeConverter &converter,
    mlir::RewritePatternSet &patterns);

} // namespace talos

#endif // TALOS_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H
