//===- SDF.h - Transient SDF importer -------------------------*- C++ -*-===//
//
// Public frontend entry points for IEEE 1800-2017 Clause 32 data.  Imported
// operations are a normalized, transient exchange format: a production
// SystemVerilog import must consume them before Simulation IR is formed.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_FRONTEND_SDF_H
#define OBELISK_FRONTEND_SDF_H

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringRef.h"

namespace mlir {
class MLIRContext;
}

namespace obelisk::frontend {

/// Parse one SDF DELAYFILE into a verified module of typed obelisk_sdf ops.
/// Decimal tokens are retained exactly; this API performs no timescale
/// conversion or destination-precision rounding.
mlir::FailureOr<mlir::OwningOpRef<mlir::ModuleOp>>
importSDF(llvm::StringRef sourceName, llvm::StringRef contents,
          mlir::MLIRContext &context, bool verifyIR = true);

} // namespace obelisk::frontend

#endif // OBELISK_FRONTEND_SDF_H
