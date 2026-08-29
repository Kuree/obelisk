//===- SDFAttrs.h - Exact normalized SDF attributes -----------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SDF_SDFATTRS_H
#define OBELISK_DIALECT_SDF_SDFATTRS_H

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"

#include "obelisk/Dialect/SDF/SDFEnums.h.inc"

#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/SDF/SDFAttrs.h.inc"

#endif // OBELISK_DIALECT_SDF_SDFATTRS_H
