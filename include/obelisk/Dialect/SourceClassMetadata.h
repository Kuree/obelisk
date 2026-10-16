//===- SourceClassMetadata.h - Unspecialized class declarations -*- C++ -*-===//

#ifndef OBELISK_DIALECT_SOURCECLASSMETADATA_H
#define OBELISK_DIALECT_SOURCECLASSMETADATA_H

#include "mlir/IR/OpDefinition.h"

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
  for (mlir::Operation &sibling : op->getParentOp()->getRegion(0).front())
    if (&sibling != op.getOperation() &&
        mlir::isa<Method, ValueParameter, TypeParameter>(sibling) &&
        sibling.getAttr("name") == op->getAttr("name"))
      return op.emitOpError("duplicates a lexical class declaration name");
  return mlir::success();
}

} // namespace obelisk::sourceclass

#endif
