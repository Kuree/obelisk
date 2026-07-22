set(LLVM_TARGET_DEFINITIONS RuntimeEnums.td)
mlir_tablegen(RuntimeEnums.h.inc -gen-enum-decls)
mlir_tablegen(RuntimeEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(ObeliskRuntimeEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS RuntimeTypes.td)
mlir_tablegen(RuntimeTypes.h.inc -gen-typedef-decls
              -typedefs-dialect=obelisk_rt)
mlir_tablegen(RuntimeTypes.cpp.inc -gen-typedef-defs
              -typedefs-dialect=obelisk_rt)
add_public_tablegen_target(ObeliskRuntimeTypesIncGen)

set(LLVM_TARGET_DEFINITIONS RuntimeOps.td)
mlir_tablegen(RuntimeOps.h.inc -gen-op-decls)
mlir_tablegen(RuntimeOps.cpp.inc -gen-op-defs)
add_public_tablegen_target(ObeliskRuntimeOpsIncGen)

set(LLVM_TARGET_DEFINITIONS RuntimeDialect.td)
mlir_tablegen(RuntimeDialect.h.inc -gen-dialect-decls -dialect=obelisk_rt)
mlir_tablegen(RuntimeDialect.cpp.inc -gen-dialect-defs -dialect=obelisk_rt)
add_public_tablegen_target(ObeliskRuntimeDialectIncGen)
