//===- SlangTypes.h - Elaborated SystemVerilog types -----------*- C++ -*-===//

#ifndef TALOS_DIALECT_SLANG_SLANGTYPES_H
#define TALOS_DIALECT_SLANG_SLANGTYPES_H

#include "talos/Dialect/Slang/SlangDialect.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/Types.h"

#include "llvm/ADT/TypeSwitch.h"

#include "talos/Dialect/Slang/SlangEnums.h.inc"

#define GET_TYPEDEF_CLASSES
#include "talos/Dialect/Slang/SlangTypes.h.inc"

#endif // TALOS_DIALECT_SLANG_SLANGTYPES_H
