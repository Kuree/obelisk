set(LLVM_TARGET_DEFINITIONS SDFEnums.td)
mlir_tablegen(SDFEnums.h.inc -gen-enum-decls)
mlir_tablegen(SDFEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(TalosSDFEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS SDFAttrs.td)
mlir_tablegen(SDFAttrs.h.inc -gen-attrdef-decls
              -attrdefs-dialect=talos_sdf)
mlir_tablegen(SDFAttrs.cpp.inc -gen-attrdef-defs
              -attrdefs-dialect=talos_sdf)
add_public_tablegen_target(TalosSDFAttrsIncGen)

set(LLVM_TARGET_DEFINITIONS SDFOps.td)
mlir_tablegen(SDFOps.h.inc -gen-op-decls)
mlir_tablegen(SDFOps.cpp.inc -gen-op-defs)
add_public_tablegen_target(TalosSDFOpsIncGen)

set(LLVM_TARGET_DEFINITIONS SDFDialect.td)
mlir_tablegen(SDFDialect.h.inc -gen-dialect-decls -dialect=talos_sdf)
mlir_tablegen(SDFDialect.cpp.inc -gen-dialect-defs -dialect=talos_sdf)
add_public_tablegen_target(TalosSDFDialectIncGen)
