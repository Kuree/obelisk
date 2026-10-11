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

} // namespace obelisk::simlowering

#endif
