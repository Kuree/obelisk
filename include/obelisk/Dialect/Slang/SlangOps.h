//===- SlangOps.h - Elaborated slang AST operations ------------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SLANG_SLANGOPS_H
#define OBELISK_DIALECT_SLANG_SLANGOPS_H

#include "obelisk/Dialect/SemanticTraits.h"
#include "obelisk/Dialect/Slang/SlangDialect.h"
#include "obelisk/Dialect/Slang/SlangTypes.h"

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Slang/SlangOps.h.inc"

#endif // OBELISK_DIALECT_SLANG_SLANGOPS_H
