//===- ForeachLoopMetadata.h - Foreach metadata schema ---------*- C++ -*-===//
//
// Shared validation and field names for the transient foreach-loop metadata
// carried by the Slang and Obelisk semantic dialects.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_DIALECT_FOREACHLOOPMETADATA_H
#define OBELISK_DIALECT_FOREACHLOOPMETADATA_H

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"

namespace obelisk::foreach_metadata {

inline constexpr llvm::StringLiteral hasStaticRange = "has_static_range";
inline constexpr llvm::StringLiteral left = "left";
inline constexpr llvm::StringLiteral right = "right";
inline constexpr llvm::StringLiteral hasIterator = "has_iterator";
inline constexpr llvm::StringLiteral iteratorSymbol = "iterator_symbol";
inline constexpr llvm::StringLiteral iteratorPath = "iterator_path";
inline constexpr llvm::StringLiteral iteratorType = "iterator_type";

inline mlir::LogicalResult
verify(mlir::ArrayAttr dimensions,
       llvm::function_ref<mlir::InFlightDiagnostic()> emitError) {
  llvm::StringSet<> paths;
  for (auto [index, attribute] : llvm::enumerate(dimensions)) {
    auto dimension = mlir::dyn_cast<mlir::DictionaryAttr>(attribute);
    if (!dimension)
      return emitError() << "loop_dimensions entry #" << index
                         << " must be a dictionary";

    auto staticRange = dimension.getAs<mlir::BoolAttr>(hasStaticRange);
    auto rangeLeft = dimension.getAs<mlir::IntegerAttr>(left);
    auto rangeRight = dimension.getAs<mlir::IntegerAttr>(right);
    if (!staticRange)
      return emitError() << "loop_dimensions entry #" << index
                         << " requires a has_static_range boolean";
    if (staticRange.getValue()) {
      if (!rangeLeft || !rangeRight ||
          !rangeLeft.getType().isSignlessInteger(64) ||
          !rangeRight.getType().isSignlessInteger(64))
        return emitError() << "static loop_dimensions entry #" << index
                           << " requires i64 left and right bounds";
    } else if (dimension.contains(left) || dimension.contains(right)) {
      return emitError() << "dynamic loop_dimensions entry #" << index
                         << " cannot carry static bounds";
    }

    auto iterator = dimension.getAs<mlir::BoolAttr>(hasIterator);
    auto symbol = dimension.getAs<mlir::SymbolRefAttr>(iteratorSymbol);
    auto path = dimension.getAs<mlir::StringAttr>(iteratorPath);
    auto type = dimension.getAs<mlir::TypeAttr>(iteratorType);
    if (!iterator)
      return emitError() << "loop_dimensions entry #" << index
                         << " requires a has_iterator boolean";
    if (!iterator.getValue()) {
      if (dimension.contains(iteratorSymbol) ||
          dimension.contains(iteratorPath) || dimension.contains(iteratorType))
        return emitError() << "skipped loop_dimensions entry #" << index
                           << " cannot carry iterator metadata";
      continue;
    }
    if (!symbol || !path || path.getValue().empty() || !type)
      return emitError() << "iterator loop_dimensions entry #" << index
                         << " requires symbol, nonempty path, and type";
    if (!paths.insert(path.getValue()).second)
      return emitError() << "foreach iterator paths must be unique";
  }
  return mlir::success();
}

inline bool hasRuntimeDimension(mlir::ArrayAttr dimensions) {
  return llvm::any_of(dimensions, [](mlir::Attribute attribute) {
    auto dimension = mlir::dyn_cast<mlir::DictionaryAttr>(attribute);
    auto staticRange = dimension
                           ? dimension.getAs<mlir::BoolAttr>(hasStaticRange)
                           : mlir::BoolAttr{};
    auto iterator = dimension ? dimension.getAs<mlir::BoolAttr>(hasIterator)
                              : mlir::BoolAttr{};
    return staticRange && iterator && iterator.getValue() &&
           !staticRange.getValue();
  });
}

} // namespace obelisk::foreach_metadata

#endif // OBELISK_DIALECT_FOREACHLOOPMETADATA_H
