//===- SourceClassMetadata.h - Unspecialized class declarations -*- C++ -*-===//

#ifndef OBELISK_DIALECT_SOURCECLASSMETADATA_H
#define OBELISK_DIALECT_SOURCECLASSMETADATA_H

#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"

namespace obelisk::sourceclass {

template <typename GenericClass, typename ValueParameter,
          typename TypeParameter, typename Method>
mlir::LogicalResult verifyMethod(Method op, bool isTask) {
  if (!mlir::isa_and_nonnull<GenericClass>(op->getParentOp()))
    return op.emitOpError("source method requires a generic class owner");
  if (op.getSemanticTypeAttr())
    return op.emitOpError("source method cannot carry an effective signature");
  if (!op.getName() || op.getName()->empty())
    return op.emitOpError("source method requires a declaration name");
  if (!op.getBody().empty() && !op.getBody().front().empty())
    return op.emitOpError("source method cannot contain an elaborated body");
  if (op.getIsConstructorAttr() &&
      (isTask || *op.getName() != "new" || op.getIsStaticAttr() ||
       op.getIsPureAttr() || op.getIsDeclaredVirtualAttr()))
    return op.emitOpError(
        "constructor requires a nonstatic, nonvirtual function named new");
  if (op.getIsStaticAttr() &&
      (op.getIsDeclaredVirtualAttr() || op.getIsPureAttr()))
    return op.emitOpError("static method cannot be virtual or pure");
  if (op.getIsPureAttr() && !op.getIsDeclaredVirtualAttr())
    return op.emitOpError("pure method must be declared virtual");
  if (auto virtuality = op.getSourceVirtualityAttr()) {
    if ((virtuality.getValue() &&
         (op.getIsStaticAttr() || op.getIsConstructorAttr())) ||
        (!virtuality.getValue() &&
         (op.getIsDeclaredVirtualAttr() || op.getIsPureAttr())))
      return op.emitOpError(
          "source virtuality conflicts with declaration modifiers");
  }
  for (mlir::Operation &sibling : op->getParentOp()->getRegion(0).front())
    if (&sibling != op.getOperation() &&
        mlir::isa<Method, ValueParameter, TypeParameter>(sibling) &&
        sibling.getAttr("name") == op->getAttr("name"))
      return op.emitOpError("duplicates a lexical class declaration name");
  return mlir::success();
}

template <typename GenericClass, typename ConcreteType, typename ReferenceType,
          typename UnavailableType, typename... Declarations,
          typename Declaration>
mlir::LogicalResult verifyDataDeclaration(Declaration op) {
  if (!mlir::isa_and_nonnull<GenericClass>(op->getParentOp()))
    return op.emitOpError("source declaration requires a generic class owner");
  if (op.getSemanticTypeAttr() || op->hasAttr("constant_value"))
    return op.emitOpError(
        "source declaration cannot carry an effective payload");
  if (!op.getName() || op.getName()->empty())
    return op.emitOpError("source declaration requires a declaration name");
  if (op.getBody().empty() || !llvm::hasSingleElement(op.getBody().front()) ||
      !mlir::isa<ConcreteType, ReferenceType, UnavailableType>(
          op.getBody().front().front()))
    return op.emitOpError(
        "source declaration requires exactly one declared-type node");
  for (mlir::Operation &sibling : op->getParentOp()->getRegion(0).front())
    if (&sibling != op.getOperation() && mlir::isa<Declarations...>(sibling) &&
        sibling.getAttr("name") == op->getAttr("name"))
      return op.emitOpError("duplicates a lexical class declaration name");
  return mlir::success();
}

template <typename Property, typename Alias, typename TypeNode>
mlir::LogicalResult verifyDeclaredTypeOwner(TypeNode op, bool concrete) {
  if (!mlir::isa_and_nonnull<Property, Alias>(op->getParentOp()))
    return op.emitOpError(
        "declared type requires a source property or typedef owner");
  if (!op.getBody().empty() && !op.getBody().front().empty())
    return op.emitOpError("declared type cannot contain elaborated nodes");
  if (!concrete && op->hasAttr("semantic_type"))
    return op.emitOpError(
        "unbound declared type cannot carry an effective type");
  return mlir::success();
}

template <typename Parameter, typename Alias, typename Reference>
mlir::LogicalResult
verifyDeclaredTypeReference(Reference op,
                            mlir::SymbolTableCollection &symbols) {
  auto *owner = op->getParentOp();
  auto *target = symbols.lookupNearestSymbolFrom(op, op.getDeclarationAttr());
  if (!mlir::isa_and_nonnull<Parameter, Alias>(target) ||
      target->getParentOp() != owner->getParentOp())
    return op.emitOpError("declared type reference must name a lexical type in "
                          "the same generic class");
  if (target == owner || !target->isBeforeInBlock(owner))
    return op.emitOpError(
        "declared type reference must name a preceding lexical type");
  return mlir::success();
}

} // namespace obelisk::sourceclass

#endif
