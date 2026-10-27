//===- PrepareStatements.h - Procedural statement inventory ----------------===//

#ifndef TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPARESTATEMENTS_H
#define TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPARESTATEMENTS_H

#include "PrepareUnits.h"

namespace talos::simlowering {
/// Freeze source statement occurrences before executable outlining/rewriting.
/// Unknown source forms remain unbound rather than acquiring a guessed origin.
mlir::LogicalResult materializeProceduralStatements(
    sim::SimDesignOp design, const PreparedUnits &units,
    const llvm::StringMap<mlir::Operation *> &semanticSymbols,
    mlir::OpBuilder &builder);
/// Preserve every source call occurrence, independently of statement shape.
mlir::LogicalResult materializeProceduralCalls(
    sim::SimDesignOp design, const PreparedUnits &units,
    const llvm::StringMap<mlir::Operation *> &semanticSymbols,
    mlir::OpBuilder &builder);
/// Freeze declaration-owned nominal interface members and dispatch contracts.
mlir::LogicalResult materializeVPIInterfaceDispatch(
    sim::SimDesignOp design, ir::SVRootSymbolOp semanticRoot,
    const PreparedUnits &units, const PreparedScopeDeclarations &scopes,
    mlir::OpBuilder &builder);
} // namespace talos::simlowering
#endif
