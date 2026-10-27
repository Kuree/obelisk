//===- RuntimeTypes.h - Runtime ABI types ---------------------*- C++ -*-===//

#ifndef TALOS_DIALECT_RUNTIME_RUNTIMETYPES_H
#define TALOS_DIALECT_RUNTIME_RUNTIMETYPES_H

#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/Types.h"
#include "talos/Dialect/Runtime/RuntimeDialect.h"

#define GET_TYPEDEF_CLASSES
#include "talos/Dialect/Runtime/RuntimeTypes.h.inc"

#endif // TALOS_DIALECT_RUNTIME_RUNTIMETYPES_H
