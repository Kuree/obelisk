//===- ObeliskTypes.h - Obelisk semantic types -----------------*- C++ -*-===//

#ifndef OBELISK_DIALECT_OBELISK_OBELISKTYPES_H
#define OBELISK_DIALECT_OBELISK_OBELISKTYPES_H

#include "obelisk/Dialect/Obelisk/ObeliskDialect.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Types.h"

#include "llvm/ADT/TypeSwitch.h"

#include "obelisk/Dialect/Obelisk/ObeliskEnums.h.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Obelisk/ObeliskTypes.h.inc"

#endif // OBELISK_DIALECT_OBELISK_OBELISKTYPES_H
