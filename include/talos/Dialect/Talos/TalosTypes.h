//===- TalosTypes.h - Talos semantic types -----------------*- C++ -*-===//

#ifndef TALOS_DIALECT_TALOS_TALOSTYPES_H
#define TALOS_DIALECT_TALOS_TALOSTYPES_H

#include "talos/Dialect/Talos/TalosDialect.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Types.h"

#include "llvm/ADT/TypeSwitch.h"

#include "talos/Dialect/Talos/TalosEnums.h.inc"

#define GET_TYPEDEF_CLASSES
#include "talos/Dialect/Talos/TalosTypes.h.inc"

#endif // TALOS_DIALECT_TALOS_TALOSTYPES_H
