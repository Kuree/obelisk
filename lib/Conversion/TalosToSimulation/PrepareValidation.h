//===- PrepareValidation.h - Semantic preparation validation ----*- C++ -*-===//
//
// Private preflight interface for the semantic-to-simulation prepare pass.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPAREVALIDATION_H
#define TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPAREVALIDATION_H

#include "talos/Dialect/Talos/TalosOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"

namespace talos::simlowering {

/// Frozen semantic namespace required by the remaining preparation phases.
struct ValidatedSemanticDesign {
  ir::SVRootSymbolOp root;
  llvm::StringMap<mlir::Operation *> symbols;
  llvm::DenseSet<mlir::Type> unusedEmbeddedCovergroupTypes;
};

/// Validate the complete elaborated semantic tree before target IR is built.
mlir::FailureOr<ValidatedSemanticDesign>
validateSemanticDesign(mlir::ModuleOp module, bool pruneUnusedCoverage);

} // namespace talos::simlowering

#endif // TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPAREVALIDATION_H
