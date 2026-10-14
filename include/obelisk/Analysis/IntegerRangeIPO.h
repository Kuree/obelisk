#ifndef OBELISK_ANALYSIS_INTEGERRANGEIPO_H
#define OBELISK_ANALYSIS_INTEGERRANGEIPO_H
#include "mlir/Interfaces/InferIntRangeInterface.h"
#include "llvm/ADT/StringRef.h"
#include <optional>
namespace mlir {
class Operation;
class Attribute;
class Value;
} // namespace mlir
namespace obelisk::analysis {
/// Reject malformed, empty or mismatched-width certificates.
std::optional<mlir::ConstantIntRanges>
decodeIntegerRange(mlir::Attribute attr,
                   std::optional<unsigned> expectedWidth = std::nullopt);
std::optional<mlir::ConstantIntRanges> getIntegerResultRange(mlir::Value value);
/// Return only a structurally valid, bounded expression for this result.
mlir::Attribute getIntegerResultExpression(mlir::Value value);
/// A module barrier publishes conservative call/entry facts into each worker's
/// owned IR. Each round analyzes functions independently against the previous
/// immutable round. Recursive cycles begin unconstrained; bounded refinement
/// cannot invent a circular proof. Public/escaping entry arguments stay free.
void prepareIntegerRangeIPO(mlir::Operation *scope);
} // namespace obelisk::analysis
#endif
