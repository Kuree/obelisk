add_mlir_library(ObeliskIR
  ObeliskDialect.cpp

  DEPENDS
  ObeliskDialectIncGen
  ObeliskEnumsIncGen
  ObeliskOpsIncGen
  ObeliskTypesIncGen

  LINK_LIBS PUBLIC
  CIRCTHW
  MLIRBytecodeOpInterface
  MLIRIR
  MLIRSideEffectInterfaces
)
