add_mlir_library(ObeliskSlangIR
  SlangDialect.cpp
  SlangOpDefs0.cpp
  SlangOpDefs1.cpp
  SlangOpDefs2.cpp
  SlangOpDefs3.cpp

  DEPENDS
  ObeliskCoverageAttrsIncGen
  ObeliskCoverageDialectIncGen
  SlangDialectIncGen
  SlangEnumsIncGen
  SlangTypesIncGen
  SlangOpsIncGen
  check-slang-ast-inventory

  LINK_LIBS PUBLIC
  MLIRIR
)
