//===- DPIAttrs.h - DPI-C ABI attributes -----------------------*- C++ -*-===//

#ifndef OBELISK_DIALECT_DPI_DPIATTRS_H
#define OBELISK_DIALECT_DPI_DPIATTRS_H

#include "obelisk/Dialect/DPI/DPIDialect.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"

#include "obelisk/Dialect/DPI/DPIEnums.h.inc"

#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/DPI/DPIAttrs.h.inc"

#endif // OBELISK_DIALECT_DPI_DPIATTRS_H
