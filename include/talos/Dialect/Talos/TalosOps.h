//===- TalosOps.h - Talos semantic operations --------------*- C++ -*-===//

#ifndef TALOS_DIALECT_TALOS_TALOSOPS_H
#define TALOS_DIALECT_TALOS_TALOSOPS_H

#include "talos/Dialect/Talos/TalosDialect.h"
#include "talos/Dialect/Talos/TalosTypes.h"
#include "talos/Dialect/SemanticTraits.h"

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
#include "talos/Dialect/Talos/TalosOps.h.inc"

#define GET_OP_CLASSES
#include "talos/Dialect/Talos/TalosASTOps.h.inc"

#endif // TALOS_DIALECT_TALOS_TALOSOPS_H
