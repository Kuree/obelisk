//===- SlangTypes.h - Elaborated SystemVerilog types -----------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SLANG_SLANGTYPES_H
#define OBELISK_DIALECT_SLANG_SLANGTYPES_H

#include "obelisk/Dialect/Slang/SlangDialect.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/Types.h"

#include "llvm/ADT/TypeSwitch.h"

#include "obelisk/Dialect/Slang/SlangEnums.h.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Slang/SlangTypes.h.inc"

#endif // OBELISK_DIALECT_SLANG_SLANGTYPES_H
