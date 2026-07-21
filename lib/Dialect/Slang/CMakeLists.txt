add_mlir_library(ObeliskSlangIR
  SlangDialect.cpp

  DEPENDS
  SlangDialectIncGen
  SlangEnumsIncGen
  SlangTypesIncGen
  SlangOpsIncGen
  check-slang-ast-inventory

  LINK_LIBS PUBLIC
  MLIRIR
)
