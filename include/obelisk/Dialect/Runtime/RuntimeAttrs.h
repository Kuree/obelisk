//===- RuntimeAttrs.h - Runtime ABI attributes ----------------*- C++ -*-===//

#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMEATTRS_H
#define OBELISK_DIALECT_RUNTIME_RUNTIMEATTRS_H

#include "mlir/IR/BuiltinAttributes.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"

#include "obelisk/Dialect/Runtime/RuntimeEnums.h.inc"

#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/Runtime/RuntimeAttrs.h.inc"

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMEATTRS_H
