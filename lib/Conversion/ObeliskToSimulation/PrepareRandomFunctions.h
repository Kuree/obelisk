//===- PrepareRandomFunctions.h - Frozen constraint functions -*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARERANDOMFUNCTIONS_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARERANDOMFUNCTIONS_H

#include "mlir/IR/Builders.h"
#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringMap.h"

#include <optional>

namespace obelisk::simlowering {

struct RandomConstraintPath {
  unsigned property;
  uint64_t offset;
  unsigned width;
  bool precise;
  bool isState;
};

/// Logical bit-stream width of packed integral values and fixed unpacked
/// arrays of those values. Non-integral leaves require a different solver.
std::optional<unsigned> getIntegralRandomWidth(mlir::Type type);

/// Width of a property's sampling coordinates. IEEE properties use numeric
/// ranks; their expression types and stored values retain IEEE precision.
std::optional<unsigned> getRandomPropertyWidth(mlir::Type type);

struct FixedRandomArrayLayout {
  unsigned elementWidth;
  unsigned elementCount;
};

/// The mode inventory follows integral leaves in logical bit-stream order.
std::optional<FixedRandomArrayLayout>
getFixedRandomArrayLayout(mlir::Type type);

/// Preserve fixed unpacked dimensions while replacing each integral leaf by
/// one cyclic history value. Scalars have one history of stateType.
mlir::Type getRandomCyclicStateType(mlir::Type type, mlir::Type stateType);

struct QualifiedRandomMember {
  // Declaration-order indices into the root unpacked structure.
  llvm::SmallVector<int64_t> path;
  mlir::Type semanticType;
  mlir::Type type;
  bool isRandC;
};

/// IEEE 1800-2023 18.4 randomizes only members whose declarations carry a
/// rand/randc qualifier. Ordinary members need no integral representation.
mlir::FailureOr<llvm::SmallVector<QualifiedRandomMember>>
collectQualifiedRandomStructMembers(mlir::Type type, mlir::Location location,
                                    mlir::Operation *scope);

mlir::FailureOr<std::optional<RandomConstraintPath>> getRandomConstraintPath(
    mlir::Operation *expression, llvm::ArrayRef<uint64_t> propertyWidths,
    llvm::function_ref<std::optional<unsigned>(mlir::Operation *)>
        propertyForExpression);

/// Preserve source dependencies before function expansion or simplification.
mlir::LogicalResult freezeRandomConstraintPriorityInputs(
    mlir::Operation *root, llvm::ArrayRef<uint64_t> propertyOffsets,
    uint64_t totalWidth, mlir::Builder &builder,
    llvm::function_ref<
        mlir::FailureOr<std::optional<RandomConstraintPath>>(mlir::Operation *)>
        getPath);

/// Expand validated pure expression calls. Actual arguments remain constraint
/// variables; non-argument random reads introduced by a body become state.
mlir::LogicalResult expandRandomConstraintFunctions(
    llvm::MutableArrayRef<mlir::Operation *> roots,
    const llvm::StringMap<mlir::Operation *> &symbols, mlir::Builder &builder,
    const llvm::DenseMap<mlir::Operation *, unsigned> &randomIndices,
    llvm::function_ref<bool(ir::SVCallExpressionOp)> isAdmittedCall,
    llvm::function_ref<
        mlir::FailureOr<ir::SVSubroutineSymbolOp>(ir::SVCallExpressionOp)>
        resolveFunction);

} // namespace obelisk::simlowering

#endif
