//===- SDFAttrs.h - Exact normalized SDF attributes -----------*- C++ -*-===//

#ifndef TALOS_DIALECT_SDF_SDFATTRS_H
#define TALOS_DIALECT_SDF_SDFATTRS_H

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"

#include "talos/Dialect/SDF/SDFEnums.h.inc"

#define GET_ATTRDEF_CLASSES
#include "talos/Dialect/SDF/SDFAttrs.h.inc"

#endif // TALOS_DIALECT_SDF_SDFATTRS_H
