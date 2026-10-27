//===- SourceParameterMetadata.h - Unresolved parameter syntax -*- C++ -*-===//

#ifndef TALOS_DIALECT_SOURCEPARAMETERMETADATA_H
#define TALOS_DIALECT_SOURCEPARAMETERMETADATA_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"

namespace talos::sourceparameter {

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
          typename... ExtraOwners, typename Expression>
mlir::LogicalResult verifyValueExpression(Expression op, unsigned operands) {
  if (op->hasAttr("semantic_type"))
    return op.emitOpError(
        "unbound source expression cannot carry an effective type");
  if (!mlir::isa_and_nonnull<Root, Actual, Unary, Binary, Conditional,
                             ExtraOwners...>(op->getParentOp()))
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

// An explicit scalar-instance actual is owned by the instantiating body. The
// target is a full module-root reference into one direct child instance.
template <typename InstanceBody, typename TypeDefault, typename TypeReference,
          typename Unary, typename Binary, typename Conditional,
          typename Reference, typename Literal, typename Actual>
mlir::LogicalResult verifyInstanceActual(Actual op) {
  if (!mlir::isa_and_nonnull<InstanceBody>(op->getParentOp()) ||
      op->hasAttr("semantic_type"))
    return op.emitOpError("parameter actual requires an instantiating body "
                          "and no effective type");
  if (op.getOrdinal() < 0 || uint64_t(op.getOrdinal()) > UINT32_MAX)
    return op.emitOpError("parameter actual ordinal exceeds the compact range");
  bool omitted = bool(op.getUseDefaultAttr());
  bool unavailable = bool(op.getUnavailableRhsAttr());
  if ((omitted && (!op.getIsNamedAttr() || unavailable)) ||
      (unavailable && op.getUnavailableRhs()->empty()) ||
      childCount(op) != unsigned(!omitted && !unavailable))
    return op.emitOpError("parameter actual requires one original RHS, named "
                          "empty syntax, or explicit unavailability");
  if (childCount(op) &&
      !mlir::isa<TypeDefault, TypeReference, Unary, Binary, Conditional,
                 Reference, Literal>(op.getBody().front().front()))
    return op.emitOpError("parameter actual has an unsupported RHS node");
  uint64_t expected = 0;
  for (mlir::Operation &sibling : op->getParentOp()->getRegion(0).front()) {
    if (&sibling == op.getOperation())
      break;
    if (auto previous = mlir::dyn_cast<Actual>(sibling)) {
      ++expected;
      if (previous.getParameterAttr() == op.getParameterAttr())
        return op.emitOpError("duplicates an explicit parameter target");
    }
  }
  if (uint64_t(op.getOrdinal()) != expected)
    return op.emitOpError(
        "parameter actual ordinals must be dense source order");
  return mlir::success();
}

template <typename Instance, typename InstanceBody, typename Parameter,
          typename TypeParameter, typename TypeDefault, typename TypeReference,
          typename Actual>
mlir::LogicalResult
verifyInstanceActualTarget(Actual op, mlir::SymbolTableCollection &symbols) {
  auto module = op->template getParentOfType<mlir::ModuleOp>();
  auto *target =
      module ? symbols.lookupSymbolIn(module, op.getParameterAttr()) : nullptr;
  if (!mlir::isa_and_nonnull<Parameter, TypeParameter>(target) ||
      isLocalParameter(target))
    return op.emitOpError("parameter actual must name a nonlocal parameter");
  auto body = mlir::dyn_cast_or_null<InstanceBody>(target->getParentOp());
  auto instance =
      body ? mlir::dyn_cast_or_null<Instance>(body->getParentOp()) : Instance{};
  if (!instance || instance->getParentOp() != op->getParentOp())
    return op.emitOpError("parameter actual target must belong to a direct "
                          "child instance");
  if (childCount(op)) {
    bool type =
        mlir::isa<TypeDefault, TypeReference>(op.getBody().front().front());
    if (type != mlir::isa<TypeParameter>(target))
      return op.emitOpError("parameter actual RHS and target kinds disagree");
  }
  return mlir::success();
}

template <typename Actual, typename Parameter, typename Reference>
mlir::LogicalResult
verifyInstanceActualLeaf(Reference op, mlir::SymbolTableCollection &symbols) {
  auto actual = op->template getParentOfType<Actual>();
  auto *target = symbols.lookupNearestSymbolFrom(op, op.getParameterAttr());
  if (!actual || !mlir::isa_and_nonnull<Parameter>(target) ||
      target->getParentOp() != actual->getParentOp())
    return op.emitOpError("parameter actual reference requires an exact "
                          "parameter in the instantiating body");
  return mlir::success();
}

} // namespace talos::sourceparameter

#endif // TALOS_DIALECT_SOURCEPARAMETERMETADATA_H
