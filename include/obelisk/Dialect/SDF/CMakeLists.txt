set(LLVM_TARGET_DEFINITIONS SDFEnums.td)
mlir_tablegen(SDFEnums.h.inc -gen-enum-decls)
mlir_tablegen(SDFEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(ObeliskSDFEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS SDFAttrs.td)
mlir_tablegen(SDFAttrs.h.inc -gen-attrdef-decls
              -attrdefs-dialect=obelisk_sdf)
mlir_tablegen(SDFAttrs.cpp.inc -gen-attrdef-defs
              -attrdefs-dialect=obelisk_sdf)
add_public_tablegen_target(ObeliskSDFAttrsIncGen)

set(LLVM_TARGET_DEFINITIONS SDFOps.td)
mlir_tablegen(SDFOps.h.inc -gen-op-decls)
mlir_tablegen(SDFOps.cpp.inc -gen-op-defs)
add_public_tablegen_target(ObeliskSDFOpsIncGen)

set(LLVM_TARGET_DEFINITIONS SDFDialect.td)
mlir_tablegen(SDFDialect.h.inc -gen-dialect-decls -dialect=obelisk_sdf)
mlir_tablegen(SDFDialect.cpp.inc -gen-dialect-defs -dialect=obelisk_sdf)
add_public_tablegen_target(ObeliskSDFDialectIncGen)
