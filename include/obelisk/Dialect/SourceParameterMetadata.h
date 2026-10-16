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

template <typename GenericClass, typename SourceClass, typename ConcreteDefault,
          typename ReferenceDefault, typename UnsupportedDefault,
          typename OtherParameter, typename Parameter>
mlir::LogicalResult verifyTypeParameter(Parameter op) {
  if (!mlir::isa_and_nonnull<GenericClass, SourceClass>(op->getParentOp()))
    return op.emitOpError(
        "source type parameter requires a lexical class owner");
  if (op.getSemanticTypeAttr())
    return op.emitOpError(
        "source type parameter cannot carry an effective type");
  if (!op.getName() || op.getName()->empty())
    return op.emitOpError("source type parameter requires a declaration name");
  for (mlir::Operation &sibling : op->getParentOp()->getRegion(0).front()) {
    if (&sibling == op.getOperation())
      break;
    if (mlir::isa<Parameter, OtherParameter>(sibling) &&
        sibling.getAttr("name") == op->getAttr("name"))
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

template <typename... Defaults, typename Default>
mlir::LogicalResult verifyUniqueOriginalDefault(Default op) {
  auto *owner = op->getParentOp();
  if (!owner || owner->getNumRegions() != 1 || owner->getRegion(0).empty())
    return op.emitOpError("original default requires a declaration owner");
  unsigned count = 0;
  for (mlir::Operation &child : owner->getRegion(0).front())
    count += mlir::isa<Defaults...>(child);
  if (count != 1)
    return op.emitOpError("declaration requires at most one original default");
  return mlir::success();
}

template <typename... Parameters, typename Default>
mlir::LogicalResult verifyDefaultOwner(Default op) {
  if (!mlir::isa_and_nonnull<Parameters...>(op->getParentOp()))
    return op.emitOpError(
        "original type default requires a source type parameter owner");
  if (childCount(op) != 0)
    return op.emitOpError("original type default cannot contain nested nodes");
  return mlir::success();
}

inline bool isLocalParameter(mlir::Operation *parameter) {
  auto local = parameter->getAttr("is_local_param");
  return mlir::isa_and_nonnull<mlir::UnitAttr>(local) ||
         (mlir::isa_and_nonnull<mlir::BoolAttr>(local) &&
          mlir::cast<mlir::BoolAttr>(local).getValue());
}

template <typename Actual, typename... Parameters, typename Reference>
mlir::LogicalResult verifyTypeReference(Reference op,
                                        mlir::SymbolTableCollection &symbols) {
  auto *owner = op->getParentOp();
  auto *target = symbols.lookupNearestSymbolFrom(op, op.getParameterAttr());
  if (mlir::isa_and_nonnull<Actual>(owner)) {
    if (!mlir::isa_and_nonnull<Parameters...>(target) ||
        target->getParentOp() != owner->getParentOp()->getParentOp() ||
        isLocalParameter(target))
      return op.emitOpError("class actual type reference must name a type "
                            "parameter in the enclosing class header");
    return mlir::success();
  }
  if (!mlir::isa_and_nonnull<Parameters...>(owner) ||
      !mlir::isa_and_nonnull<Parameters...>(target) ||
      target->getParentOp() != owner->getParentOp())
    return op.emitOpError("default reference must name a source type parameter "
                          "in the same generic class");
  if (target == owner || !target->isBeforeInBlock(owner))
    return op.emitOpError(
        "default reference must name a preceding source type parameter");
  return mlir::success();
}

template <typename GenericClass, typename SourceClass, typename TypeParameter,
          typename ValueDefault, typename UnsupportedDefault,
          typename Parameter>
mlir::LogicalResult verifyValueParameter(Parameter op) {
  if (!mlir::isa_and_nonnull<GenericClass, SourceClass>(op->getParentOp()))
    return op.emitOpError(
        "source value parameter requires a lexical class owner");
  if (op.getSemanticTypeAttr())
    return op.emitOpError(
        "source value parameter cannot carry an effective type");
  if (op->hasAttr("constant_value"))
    return op.emitOpError(
        "source value parameter cannot carry an effective value");
  if (!op.getName() || op.getName()->empty())
    return op.emitOpError("source value parameter requires a declaration name");
  for (mlir::Operation &sibling : op->getParentOp()->getRegion(0).front()) {
    if (&sibling == op.getOperation())
      break;
    if (mlir::isa<Parameter, TypeParameter>(sibling) &&
        sibling.getAttr("name") == op->getAttr("name"))
      return op.emitOpError("duplicates a lexical parameter declaration name");
  }
  if (childCount(op) > 1 ||
      (childCount(op) == 1 && !mlir::isa<ValueDefault, UnsupportedDefault>(
                                  op.getBody().front().front())))
    return op.emitOpError(
        "source value parameter requires at most one original default");
  return mlir::success();
}

template <typename Root, typename Actual, typename Unary, typename Binary,
          typename Conditional, typename Reference, typename Literal,
          typename Expression>
mlir::LogicalResult verifyValueExpression(Expression op, unsigned operands) {
  if (op->hasAttr("semantic_type"))
    return op.emitOpError(
        "unbound source expression cannot carry an effective type");
  if (!mlir::isa_and_nonnull<Root, Actual, Unary, Binary, Conditional>(
          op->getParentOp()))
    return op.emitOpError(
        "source expression requires an original value default owner");
  if (childCount(op) != operands)
    return op.emitOpError("source expression has an incorrect operand count");
  if (!op.getBody().empty())
    for (mlir::Operation &child : op.getBody().front())
      if (!mlir::isa<Unary, Binary, Conditional, Reference, Literal>(child))
        return op.emitOpError(
            "source expression operand must be a supported typed node");
  return mlir::success();
}

template <typename SourceParameter, typename EffectiveParameter,
          typename Actual, typename Reference>
mlir::LogicalResult verifyValueReference(Reference op,
                                         mlir::SymbolTableCollection &symbols) {
  mlir::Operation *owner = op->getParentOp();
  while (owner &&
         !mlir::isa<SourceParameter, EffectiveParameter, Actual>(owner))
    owner = owner->getParentOp();
  auto *target = symbols.lookupNearestSymbolFrom(op, op.getParameterAttr());
  if (mlir::isa_and_nonnull<Actual>(owner)) {
    if (!mlir::isa_and_nonnull<SourceParameter, EffectiveParameter>(target) ||
        target->getParentOp() != owner->getParentOp()->getParentOp() ||
        isLocalParameter(target))
      return op.emitOpError("class actual value reference must name a value "
                            "parameter in the enclosing class header");
    return mlir::success();
  }
  if (!owner ||
      !mlir::isa_and_nonnull<SourceParameter, EffectiveParameter>(target) ||
      owner->getParentOp() != target->getParentOp())
    return op.emitOpError("default reference must name a value parameter in "
                          "the same declaration scope");
  if (target == owner || !target->isBeforeInBlock(owner))
    return op.emitOpError(
        "default reference must name a preceding value parameter");
  return mlir::success();
}

} // namespace obelisk::sourceparameter

#endif // OBELISK_DIALECT_SOURCEPARAMETERMETADATA_H
