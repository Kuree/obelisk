//===- ObeliskOps.h - Obelisk semantic operations --------------*- C++ -*-===//

#ifndef OBELISK_DIALECT_OBELISK_OBELISKOPS_H
#define OBELISK_DIALECT_OBELISK_OBELISKOPS_H

#include "obelisk/Dialect/Obelisk/ObeliskDialect.h"
#include "obelisk/Dialect/Obelisk/ObeliskTypes.h"
#include "obelisk/Dialect/SemanticTraits.h"

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/RegionKindInterface.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Obelisk/ObeliskOps.h.inc"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Obelisk/ObeliskASTOps.h.inc"

#endif // OBELISK_DIALECT_OBELISK_OBELISKOPS_H
