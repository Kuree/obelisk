//===- PrepareRandomIteration.h - Frozen iterative constraints -*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARERANDOMITERATION_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARERANDOMITERATION_H

#include "mlir/IR/Builders.h"
#include "llvm/ADT/ArrayRef.h"

namespace obelisk::simlowering {

/// Expand fixed foreach dimensions in declaration order before constraint
/// dependency analysis. Each iterator becomes a typed constant in its cloned
/// body; omitted dimensions do not iterate. Runtime dimensions remain intact.
mlir::LogicalResult expandStaticRandomForeachConstraints(
    llvm::MutableArrayRef<mlir::Operation *> roots, mlir::Builder &builder);

/// Replace fixed unpacked array and slice operands of unique constraints with
/// their leaf selections. Singular operands remain singular; duplicate leaves
/// retain their identity and consequently make the constraint unsatisfiable.
mlir::LogicalResult expandStaticRandomUniquenessConstraints(
    llvm::MutableArrayRef<mlir::Operation *> roots, mlir::Builder &builder);

/// Descend through fixed unpacked array members of an inside set until each
/// member is singular (11.4.13). Preserve declared coordinates and expression
/// types; ordinary membership lowering supplies the comparison context.
mlir::LogicalResult expandStaticRandomInsideExpressions(
    llvm::MutableArrayRef<mlir::Operation *> roots, mlir::Builder &builder);

} // namespace obelisk::simlowering

#endif
