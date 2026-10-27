add_mlir_library(TalosSlangToTalos
  SlangToTalos.cpp

  DEPENDS
  TalosConversionPassIncGen
  TalosASTOpsIncGen
  SlangOpsIncGen

  LINK_LIBS PUBLIC
  TalosIR
  TalosSlangIR
  MLIRIR
  MLIRPass
  MLIRTransformUtils
)
