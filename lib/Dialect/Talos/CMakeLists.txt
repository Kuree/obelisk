add_mlir_library(TalosIR
  TalosDialect.cpp
  TalosASTOpDefs0.cpp
  TalosASTOpDefs1.cpp
  TalosASTOpDefs2.cpp
  TalosASTOpDefs3.cpp

  DEPENDS
  TalosCoverageAttrsIncGen
  TalosCoverageDialectIncGen
  TalosDialectIncGen
  TalosASTOpsIncGen
  TalosEnumsIncGen
  TalosOpsIncGen
  TalosTypesIncGen

  LINK_LIBS PUBLIC
  MLIRBytecodeOpInterface
  MLIRIR
  MLIRSideEffectInterfaces
)
