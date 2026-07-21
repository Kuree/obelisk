add_mlir_library(ObeliskIR
  ObeliskDialect.cpp

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
