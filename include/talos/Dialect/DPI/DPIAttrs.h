//===- DPIAttrs.h - DPI-C ABI attributes -----------------------*- C++ -*-===//

#ifndef TALOS_DIALECT_DPI_DPIATTRS_H
#define TALOS_DIALECT_DPI_DPIATTRS_H

#include "talos/Dialect/DPI/DPIDialect.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"

#include "talos/Dialect/DPI/DPIEnums.h.inc"

#define GET_ATTRDEF_CLASSES
#include "talos/Dialect/DPI/DPIAttrs.h.inc"

#endif // TALOS_DIALECT_DPI_DPIATTRS_H
