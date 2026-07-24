//===- RuntimeToLLVM.h - Typed runtime ABI to LLVM calls ------*- C++ -*-===//

#ifndef OBELISK_CONVERSION_RUNTIMETOLLVM_H
#define OBELISK_CONVERSION_RUNTIMETOLLVM_H

#include "obelisk/Conversion/Passes.h"

#include "mlir/Support/LogicalResult.h"

namespace mlir {
class LLVMTypeConverter;
class ModuleOp;
class RewritePatternSet;
} // namespace mlir

namespace llvm {
class DataLayout;
} // namespace llvm

namespace obelisk {

/// Materialize the always-present execution descriptor and any encoded
/// simulation bytecode/design database attributes as immutable LLVM globals.
/// The operation is idempotent so composing lowerings may call it safely.
mlir::LogicalResult materializeEmbeddedSimulationDesign(mlir::ModuleOp module);

/// Validate the exact runtime ABI target contract and linear ownership of
/// runtime-owned buffers before composing runtime lowering patterns.
mlir::LogicalResult
validateRuntimeToLLVMPreconditions(mlir::ModuleOp module,
                                   const llvm::DataLayout &dataLayout);

/// Add the one-to-one runtime ABI type mappings to a composing LLVM
/// conversion.
void addRuntimeToLLVMTypeConversions(mlir::LLVMTypeConverter &converter);

/// Add typed runtime materializer and C ABI call lowering patterns to a
/// composing full LLVM conversion.
void populateRuntimeToLLVMPatterns(const mlir::LLVMTypeConverter &converter,
                                   mlir::RewritePatternSet &patterns);

} // namespace obelisk

#endif // OBELISK_CONVERSION_RUNTIMETOLLVM_H
