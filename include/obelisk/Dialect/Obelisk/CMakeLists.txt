include(${PROJECT_SOURCE_DIR}/cmake/GenerateSlangASTOps.cmake)

set(_obelisk_ast_ops_td "${CMAKE_CURRENT_BINARY_DIR}/ObeliskASTOps.td")
obelisk_generate_slang_ast_ops(obelisk "${_obelisk_ast_ops_td}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/include/obelisk/Dialect/Slang/SlangASTNodes.def
)

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

set(LLVM_TARGET_DEFINITIONS "${_obelisk_ast_ops_td}")
# The AST node inventory generates hundreds of op classes. Sharding emits
# their definitions in slices that compile as parallel translation units,
# plus the registerObeliskDialectOperations hook the dialect calls instead of
# expanding a GET_OP_LIST.
mlir_tablegen(ObeliskASTOps.h.inc -gen-op-decls
              --op-shard-count=${OBELISK_OP_SHARD_COUNT})
mlir_tablegen(ObeliskASTOpDefs.cpp.inc -gen-op-defs
              --op-shard-count=${OBELISK_OP_SHARD_COUNT})
add_public_tablegen_target(ObeliskASTOpsIncGen)

set(LLVM_TARGET_DEFINITIONS ObeliskDialect.td)
mlir_tablegen(ObeliskDialect.h.inc -gen-dialect-decls
              -dialect=obelisk)
mlir_tablegen(ObeliskDialect.cpp.inc -gen-dialect-defs
              -dialect=obelisk)
add_public_tablegen_target(ObeliskDialectIncGen)
