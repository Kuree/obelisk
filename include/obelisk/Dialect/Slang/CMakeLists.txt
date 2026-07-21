include(${PROJECT_SOURCE_DIR}/cmake/GenerateSlangASTOps.cmake)

set(_slang_ops_td "${CMAKE_CURRENT_BINARY_DIR}/SlangOps.td")
obelisk_generate_slang_ast_ops(slang "${_slang_ops_td}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/include/obelisk/Dialect/Slang/SlangASTNodes.def
)

set(LLVM_TARGET_DEFINITIONS SlangDialect.td)
mlir_tablegen(SlangDialect.h.inc -gen-dialect-decls -dialect=slang)
mlir_tablegen(SlangDialect.cpp.inc -gen-dialect-defs -dialect=slang)
add_public_tablegen_target(SlangDialectIncGen)

set(LLVM_TARGET_DEFINITIONS SlangEnums.td)
mlir_tablegen(SlangEnums.h.inc -gen-enum-decls)
mlir_tablegen(SlangEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(SlangEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS SlangTypes.td)
mlir_tablegen(SlangTypes.h.inc -gen-typedef-decls -typedefs-dialect=slang)
mlir_tablegen(SlangTypes.cpp.inc -gen-typedef-defs -typedefs-dialect=slang)
add_public_tablegen_target(SlangTypesIncGen)

set(LLVM_TARGET_DEFINITIONS "${_slang_ops_td}")
mlir_tablegen(SlangOps.h.inc -gen-op-decls)
mlir_tablegen(SlangOps.cpp.inc -gen-op-defs)
add_public_tablegen_target(SlangOpsIncGen)

add_custom_target(check-slang-ast-inventory
  COMMAND ${CMAKE_COMMAND}
    -DSLANG_AST_VISITOR=${OBELISK_SLANG_INCLUDE_DIR}/slang/ast/ASTVisitor.h
    -DOBELISK_AST_INVENTORY=${PROJECT_SOURCE_DIR}/include/obelisk/Dialect/Slang/SlangASTNodes.def
    -P ${PROJECT_SOURCE_DIR}/cmake/CheckSlangASTInventory.cmake
  VERBATIM
)
