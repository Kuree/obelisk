include(${PROJECT_SOURCE_DIR}/cmake/GenerateSlangASTOps.cmake)

set(_talos_ast_ops_td "${CMAKE_CURRENT_BINARY_DIR}/TalosASTOps.td")
talos_generate_slang_ast_ops(talos "${_talos_ast_ops_td}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/include/talos/Dialect/Slang/SlangASTNodes.def
)

set(LLVM_TARGET_DEFINITIONS TalosEnums.td)
mlir_tablegen(TalosEnums.h.inc -gen-enum-decls)
mlir_tablegen(TalosEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(TalosEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS TalosTypes.td)
mlir_tablegen(TalosTypes.h.inc -gen-typedef-decls
              -typedefs-dialect=talos)
mlir_tablegen(TalosTypes.cpp.inc -gen-typedef-defs
              -typedefs-dialect=talos)
add_public_tablegen_target(TalosTypesIncGen)

set(LLVM_TARGET_DEFINITIONS TalosOps.td)
mlir_tablegen(TalosOps.h.inc -gen-op-decls)
mlir_tablegen(TalosOps.cpp.inc -gen-op-defs)
add_public_tablegen_target(TalosOpsIncGen)

set(LLVM_TARGET_DEFINITIONS "${_talos_ast_ops_td}")
# The AST node inventory generates hundreds of op classes. Sharding emits
# their definitions in slices that compile as parallel translation units,
# plus the registerTalosDialectOperations hook the dialect calls instead of
# expanding a GET_OP_LIST.
mlir_tablegen(TalosASTOps.h.inc -gen-op-decls
              --op-shard-count=${TALOS_OP_SHARD_COUNT})
mlir_tablegen(TalosASTOpDefs.cpp.inc -gen-op-defs
              --op-shard-count=${TALOS_OP_SHARD_COUNT})
add_public_tablegen_target(TalosASTOpsIncGen)

set(LLVM_TARGET_DEFINITIONS TalosDialect.td)
mlir_tablegen(TalosDialect.h.inc -gen-dialect-decls
              -dialect=talos)
mlir_tablegen(TalosDialect.cpp.inc -gen-dialect-defs
              -dialect=talos)
add_public_tablegen_target(TalosDialectIncGen)
