//===- SlangOps.h - Elaborated slang AST operations ------------*- C++ -*-===//

#ifndef TALOS_DIALECT_SLANG_SLANGOPS_H
#define TALOS_DIALECT_SLANG_SLANGOPS_H

#include "talos/Dialect/SemanticTraits.h"
#include "talos/Dialect/Slang/SlangDialect.h"
#include "talos/Dialect/Slang/SlangTypes.h"

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"

#define GET_OP_CLASSES
#include "talos/Dialect/Slang/SlangOps.h.inc"

#endif // TALOS_DIALECT_SLANG_SLANGOPS_H
