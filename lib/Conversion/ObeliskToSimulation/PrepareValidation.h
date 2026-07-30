//===- PrepareValidation.h - Semantic preparation validation ----*- C++ -*-===//
//
// Private preflight interface for the semantic-to-simulation prepare pass.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPAREVALIDATION_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPAREVALIDATION_H

#include "obelisk/Dialect/Obelisk/ObeliskOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/StringMap.h"

namespace obelisk::simlowering {

/// Frozen semantic namespace required by the remaining preparation phases.
struct ValidatedSemanticDesign {
  ir::SVRootSymbolOp root;
  llvm::StringMap<mlir::Operation *> symbols;
};

/// Validate the complete elaborated semantic tree before target IR is built.
mlir::FailureOr<ValidatedSemanticDesign>
validateSemanticDesign(mlir::ModuleOp module);

} // namespace obelisk::simlowering

#endif // OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPAREVALIDATION_H
