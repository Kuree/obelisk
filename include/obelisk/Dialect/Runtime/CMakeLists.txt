set(LLVM_TARGET_DEFINITIONS RuntimeAttrs.td)
mlir_tablegen(RuntimeAttrs.h.inc -gen-attrdef-decls -attrdefs-dialect=runtime)
mlir_tablegen(RuntimeAttrs.cpp.inc -gen-attrdef-defs -attrdefs-dialect=runtime)
add_public_tablegen_target(ObeliskRuntimeAttrsIncGen)

set(LLVM_TARGET_DEFINITIONS RuntimeEnums.td)
mlir_tablegen(RuntimeEnums.h.inc -gen-enum-decls)
mlir_tablegen(RuntimeEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(ObeliskRuntimeEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS RuntimeTypes.td)
mlir_tablegen(RuntimeTypes.h.inc -gen-typedef-decls
              -typedefs-dialect=runtime)
mlir_tablegen(RuntimeTypes.cpp.inc -gen-typedef-defs
              -typedefs-dialect=runtime)
add_public_tablegen_target(ObeliskRuntimeTypesIncGen)

set(LLVM_TARGET_DEFINITIONS RuntimeOps.td)
mlir_tablegen(RuntimeOps.h.inc -gen-op-decls)
mlir_tablegen(RuntimeOps.cpp.inc -gen-op-defs)
add_public_tablegen_target(ObeliskRuntimeOpsIncGen)

set(LLVM_TARGET_DEFINITIONS RuntimeDialect.td)
mlir_tablegen(RuntimeDialect.h.inc -gen-dialect-decls -dialect=runtime)
mlir_tablegen(RuntimeDialect.cpp.inc -gen-dialect-defs -dialect=runtime)
add_public_tablegen_target(ObeliskRuntimeDialectIncGen)
