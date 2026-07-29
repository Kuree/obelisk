//===- SimulationToLLVMCoroutine.h - Process coroutine lowering -*- C++ -*-===//

#ifndef OBELISK_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H
#define OBELISK_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Conversion/Passes.h"

#include "mlir/Transforms/DialectConversion.h"

namespace llvm {
class DataLayout;
}

namespace mlir {
class LLVMTypeConverter;
class ModuleOp;
class RewritePatternSet;
} // namespace mlir

namespace obelisk {

/// Normalize packed and suspension-live state, then analyze and construct
/// process ramps, continuation shims, descriptors, and native hooks before the
/// terminal LLVM dialect conversion.
mlir::LogicalResult
prepareSimulationProcessesToLLVMCoroutines(mlir::ModuleOp module,
                                           const llvm::DataLayout &dataLayout);

/// Add the Runtime, function, arithmetic, and control-flow patterns used after
/// coroutine process construction. Packed-value normalization is performed by
/// prepareSimulationProcessesToLLVMCoroutines. Call
/// prepareSimulationProcessesToLLVMCoroutines first when composing the full
/// process conversion outside the registered pass.
void populateSimulationCoroutineToLLVMPatterns(
    const mlir::LLVMTypeConverter &converter,
    mlir::RewritePatternSet &patterns);

} // namespace obelisk

#endif // OBELISK_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H
