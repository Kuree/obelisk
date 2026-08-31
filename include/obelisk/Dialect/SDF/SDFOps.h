//===- SDFOps.h - Transient normalized SDF operations ---------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SDF_SDFOPS_H
#define OBELISK_DIALECT_SDF_SDFOPS_H

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "obelisk/Dialect/SDF/SDFAttrs.h"
#include "obelisk/Dialect/SDF/SDFDialect.h"

#define GET_OP_CLASSES
#include "obelisk/Dialect/SDF/SDFOps.h.inc"

#endif // OBELISK_DIALECT_SDF_SDFOPS_H
