//===- PrepareDeclarations.h - Executable declaration planning -*- C++ -*-===//
//
// Private declaration materialization interface for the semantic preparation
// pass.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPAREDECLARATIONS_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPAREDECLARATIONS_H

#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"

namespace obelisk::simlowering {

struct PreparedClassDeclarations {
  mlir::SmallVector<ir::SVClassTypeOp> sources;
  llvm::DenseMap<mlir::Operation *, sim::SimClassDeclOp> declarations;
  llvm::DenseMap<mlir::Operation *, mlir::StringAttr> symbols;
  llvm::DenseMap<mlir::Operation *, mlir::FlatSymbolRefAttr> fieldSymbols;
  llvm::DenseMap<mlir::Operation *, mlir::FlatSymbolRefAttr>
      randcKeyFieldSymbols;
  llvm::DenseMap<mlir::Operation *, mlir::FlatSymbolRefAttr>
      randcPositionFieldSymbols;
  llvm::DenseMap<mlir::Operation *, mlir::FlatSymbolRefAttr> methodSymbols;
  llvm::DenseMap<mlir::Operation *, mlir::FlatSymbolRefAttr>
      implicitConstructorSymbols;
  llvm::DenseMap<mlir::Operation *, uint64_t> virtualMethodSlots;
  llvm::DenseMap<mlir::Operation *, uint64_t> virtualMethodSignatures;
  llvm::DenseMap<mlir::Operation *, uint64_t> interfaceMethodOrdinals;
  llvm::StringMap<ir::SVClassTypeOp> semanticClasses;
};

struct PreparedScopeDeclarations {
  llvm::DenseMap<mlir::Operation *, uint64_t> ids;
  mlir::SmallVector<sim::SimScopeDeclOp> declarations;

  uint64_t lookup(mlir::Operation *operation) const;
};

/// Return the executable method behind either a direct method or prototype.
ir::SVSubroutineSymbolOp getClassMethod(mlir::Operation *member);

/// Materialize deterministic covergroup declarations into `design`.
mlir::LogicalResult
materializeCovergroupDeclarations(ir::SVRootSymbolOp semanticRoot,
                                  mlir::OpBuilder &builder);

/// Materialize the complete executable class inventory into `design`.
mlir::FailureOr<PreparedClassDeclarations> materializeClassDeclarations(
    mlir::ModuleOp module, sim::SimDesignOp design,
    ir::SVRootSymbolOp semanticRoot, mlir::OpBuilder &builder,
    const llvm::StringMap<mlir::Operation *> &semanticSymbols);

/// Materialize hierarchical scopes and their DPI time-scale metadata.
mlir::FailureOr<PreparedScopeDeclarations> materializeScopeDeclarations(
    ir::SVRootSymbolOp semanticRoot, mlir::ArrayRef<mlir::Operation *> units,
    uint64_t designPrecisionFemtoseconds, mlir::OpBuilder &builder);

} // namespace obelisk::simlowering

#endif // OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPAREDECLARATIONS_H
