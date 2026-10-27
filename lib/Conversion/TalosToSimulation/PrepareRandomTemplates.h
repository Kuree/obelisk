//===- PrepareRandomTemplates.h - Reusable constraint templates -*- C++ -*-===//

#ifndef TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPARERANDOMTEMPLATES_H
#define TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPARERANDOMTEMPLATES_H

#include "PrepareDeclarations.h"
#include "PrepareTopology.h"

#include "mlir/IR/Builders.h"
#include "mlir/Support/LogicalResult.h"

namespace talos::simlowering {

/// Materialize one reusable constraint template for every exact class whose
/// complete effective constraint set is representable by template dataflow.
/// Classes containing constructs that have not migrated yet retain the
/// existing call-site lowering and do not receive a partial template.
mlir::LogicalResult materializeRandomConstraintTemplates(
    sim::SimDesignOp design, const PreparedClassDeclarations &classes,
    const llvm::StringMap<mlir::Operation *> &semanticSymbols,
    const llvm::StringMap<DescriptorInfo> &descriptors);

} // namespace talos::simlowering

#endif // TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPARERANDOMTEMPLATES_H
