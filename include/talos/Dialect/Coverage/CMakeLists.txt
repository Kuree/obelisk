set(LLVM_TARGET_DEFINITIONS CoverageAttrs.td)
mlir_tablegen(CoverageAttrs.h.inc -gen-attrdef-decls -attrdefs-dialect=coverage)
mlir_tablegen(CoverageAttrs.cpp.inc -gen-attrdef-defs -attrdefs-dialect=coverage)
add_public_tablegen_target(TalosCoverageAttrsIncGen)

set(LLVM_TARGET_DEFINITIONS CoverageDialect.td)
mlir_tablegen(CoverageDialect.h.inc -gen-dialect-decls -dialect=coverage)
mlir_tablegen(CoverageDialect.cpp.inc -gen-dialect-defs -dialect=coverage)
add_public_tablegen_target(TalosCoverageDialectIncGen)
