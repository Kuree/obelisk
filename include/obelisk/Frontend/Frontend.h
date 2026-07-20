//===- Frontend.h - Obelisk SystemVerilog frontend support ------*- C++ -*-===//

#ifndef OBELISK_FRONTEND_FRONTEND_H
#define OBELISK_FRONTEND_FRONTEND_H

#include "circt/Conversion/ImportVerilog.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/ArrayRef.h"

#include <string>

namespace mlir {
class MLIRContext;
class TimingScope;
} // namespace mlir

namespace obelisk::frontend {

/// Import one SystemVerilog compilation from the given primary source files.
/// Frontend search paths, macros, libraries, and language settings are supplied
/// through `options`. The returned module has passed the MLIR verifier when
/// `verifyIR` is true.
mlir::FailureOr<mlir::OwningOpRef<mlir::ModuleOp>>
importSystemVerilog(llvm::ArrayRef<std::string> inputFilenames,
                    mlir::MLIRContext &context, mlir::TimingScope &timingScope,
                    const circt::ImportVerilogOptions &options,
                    bool verifyIR = true);

} // namespace obelisk::frontend

#endif // OBELISK_FRONTEND_FRONTEND_H
