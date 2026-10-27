//===- SDFOps.h - Transient normalized SDF operations ---------*- C++ -*-===//

#ifndef TALOS_DIALECT_SDF_SDFOPS_H
#define TALOS_DIALECT_SDF_SDFOPS_H

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "talos/Dialect/SDF/SDFAttrs.h"
#include "talos/Dialect/SDF/SDFDialect.h"

#define GET_OP_CLASSES
#include "talos/Dialect/SDF/SDFOps.h.inc"

#endif // TALOS_DIALECT_SDF_SDFOPS_H
