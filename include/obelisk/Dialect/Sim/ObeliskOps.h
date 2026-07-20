//===- ObeliskOps.h - Obelisk simulation operations ------------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIM_OBELISKOPS_H
#define OBELISK_DIALECT_SIM_OBELISKOPS_H

#include "obelisk/Dialect/Sim/ObeliskDialect.h"
#include "obelisk/Dialect/Sim/ObeliskTypes.h"

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/TypeSwitch.h"

#include "obelisk/Dialect/Sim/ObeliskEnums.h.inc"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Sim/ObeliskOps.h.inc"

#endif // OBELISK_DIALECT_SIM_OBELISKOPS_H
