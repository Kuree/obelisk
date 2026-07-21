//===- ObeliskTypes.h - Obelisk simulation types ---------------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIM_OBELISKTYPES_H
#define OBELISK_DIALECT_SIM_OBELISKTYPES_H

#include "obelisk/Dialect/Sim/ObeliskDialect.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Types.h"

#include "llvm/ADT/TypeSwitch.h"

#include "obelisk/Dialect/Sim/ObeliskEnums.h.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Sim/ObeliskTypes.h.inc"

#endif // OBELISK_DIALECT_SIM_OBELISKTYPES_H
