set(LLVM_TARGET_DEFINITIONS ObeliskEnums.td)
mlir_tablegen(ObeliskEnums.h.inc -gen-enum-decls)
mlir_tablegen(ObeliskEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(ObeliskEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS ObeliskTypes.td)
mlir_tablegen(ObeliskTypes.h.inc -gen-typedef-decls
              -typedefs-dialect=obelisk)
mlir_tablegen(ObeliskTypes.cpp.inc -gen-typedef-defs
              -typedefs-dialect=obelisk)
add_public_tablegen_target(ObeliskTypesIncGen)

set(LLVM_TARGET_DEFINITIONS ObeliskOps.td)
mlir_tablegen(ObeliskOps.h.inc -gen-op-decls)
mlir_tablegen(ObeliskOps.cpp.inc -gen-op-defs)
add_public_tablegen_target(ObeliskOpsIncGen)

set(LLVM_TARGET_DEFINITIONS ObeliskDialect.td)
mlir_tablegen(ObeliskDialect.h.inc -gen-dialect-decls
              -dialect=obelisk)
mlir_tablegen(ObeliskDialect.cpp.inc -gen-dialect-defs
              -dialect=obelisk)
add_public_tablegen_target(ObeliskDialectIncGen)
