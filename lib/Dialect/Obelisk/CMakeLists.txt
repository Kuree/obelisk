add_mlir_library(ObeliskIR
  ObeliskDialect.cpp
  ObeliskASTOpDefs0.cpp
  ObeliskASTOpDefs1.cpp
  ObeliskASTOpDefs2.cpp
  ObeliskASTOpDefs3.cpp

  DEPENDS
  ObeliskDialectIncGen
  ObeliskASTOpsIncGen
  ObeliskEnumsIncGen
  ObeliskOpsIncGen
  ObeliskTypesIncGen

  LINK_LIBS PUBLIC
  MLIRBytecodeOpInterface
  MLIRIR
  MLIRSideEffectInterfaces
)
