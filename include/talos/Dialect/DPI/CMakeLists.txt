set(LLVM_TARGET_DEFINITIONS DPIEnums.td)
mlir_tablegen(DPIEnums.h.inc -gen-enum-decls)
mlir_tablegen(DPIEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(TalosDPIEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS DPIAttrs.td)
mlir_tablegen(DPIAttrs.h.inc -gen-attrdef-decls -attrdefs-dialect=dpi)
mlir_tablegen(DPIAttrs.cpp.inc -gen-attrdef-defs -attrdefs-dialect=dpi)
add_public_tablegen_target(TalosDPIAttrsIncGen)

set(LLVM_TARGET_DEFINITIONS DPIDialect.td)
mlir_tablegen(DPIDialect.h.inc -gen-dialect-decls -dialect=dpi)
mlir_tablegen(DPIDialect.cpp.inc -gen-dialect-defs -dialect=dpi)
add_public_tablegen_target(TalosDPIDialectIncGen)
