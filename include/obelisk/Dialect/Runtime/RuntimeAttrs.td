//===- RuntimeAttrs.td - Runtime ABI enum attributes ----------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMEATTRS_TD
#define OBELISK_DIALECT_RUNTIME_RUNTIMEATTRS_TD

include "obelisk/Dialect/Runtime/RuntimeEnums.td"

// ABI enums deliberately use MLIR's I32EnumAttr constraints and therefore
// serialize as builtin i32 attributes on operations. Their generated C++ enum
// classes remain compiler-visible and are statically checked against Runtime.h;
// a dialect-owned storage wrapper would add syntax without adding semantics.

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMEATTRS_TD
