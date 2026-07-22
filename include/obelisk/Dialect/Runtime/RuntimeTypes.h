//===- RuntimeTypes.h - Runtime ABI types ---------------------*- C++ -*-===//

#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMETYPES_H
#define OBELISK_DIALECT_RUNTIME_RUNTIMETYPES_H

#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/Types.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Runtime/RuntimeTypes.h.inc"

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMETYPES_H
