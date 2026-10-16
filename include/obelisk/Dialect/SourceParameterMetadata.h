//===- SourceParameterMetadata.h - Unresolved parameter syntax -*- C++ -*-===//

#ifndef OBELISK_DIALECT_SOURCEPARAMETERMETADATA_H
#define OBELISK_DIALECT_SOURCEPARAMETERMETADATA_H

#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"

namespace obelisk::sourceparameter {

inline size_t childCount(mlir::Operation *op) {
  return op->getRegion(0).empty()
             ? 0
             : op->getRegion(0).front().getOperations().size();
}

template <typename GenericClass, typename ConcreteDefault,
          typename ReferenceDefault, typename UnsupportedDefault,
          typename Parameter>
mlir::LogicalResult verifyTypeParameter(Parameter op) {
  if (!mlir::isa_and_nonnull<GenericClass>(op->getParentOp()))
    return op.emitOpError(
        "source type parameter requires a generic class owner");
  if (op.getSemanticTypeAttr())
    return op.emitOpError(
        "source type parameter cannot carry an effective type");
  if (!op.getName() || op.getName()->empty())
    return op.emitOpError("source type parameter requires a declaration name");
  for (mlir::Operation &sibling : op->getParentOp()->getRegion(0).front()) {
    if (&sibling == op.getOperation())
      break;
    if (auto other = mlir::dyn_cast<Parameter>(&sibling);
        other && other.getName() == op.getName())
      return op.emitOpError(
          "duplicates a lexical type parameter declaration name");
  }
  if (childCount(op) > 1)
    return op.emitOpError(
        "source type parameter has at most one original default");
  if (childCount(op) == 1 &&
      !mlir::isa<ConcreteDefault, ReferenceDefault, UnsupportedDefault>(
          op.getBody().front().front()))
    return op.emitOpError(
        "source type parameter requires a typed default node");
  return mlir::success();
}

template <typename Parameter, typename Default>
mlir::LogicalResult verifyDefaultOwner(Default op) {
  if (!mlir::isa_and_nonnull<Parameter>(op->getParentOp()))
    return op.emitOpError(
        "original type default requires a source type parameter owner");
  if (childCount(op) != 0)
    return op.emitOpError("original type default cannot contain nested nodes");
  return mlir::success();
}

template <typename Parameter, typename Reference>
mlir::LogicalResult verifyTypeReference(Reference op,
                                        mlir::SymbolTableCollection &symbols) {
  auto owner = mlir::dyn_cast_or_null<Parameter>(op->getParentOp());
  auto target = symbols.template lookupNearestSymbolFrom<Parameter>(
      op, op.getParameterAttr());
  if (!owner || !target || target->getParentOp() != owner->getParentOp())
    return op.emitOpError("default reference must name a source type parameter "
                          "in the same generic class");
  if (target == owner || !target->isBeforeInBlock(owner))
    return op.emitOpError(
        "default reference must name a preceding source type parameter");
  return mlir::success();
}

} // namespace obelisk::sourceparameter

#endif // OBELISK_DIALECT_SOURCEPARAMETERMETADATA_H
