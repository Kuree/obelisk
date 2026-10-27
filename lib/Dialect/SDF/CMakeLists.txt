add_mlir_library(TalosSDFIR
  SDFDialect.cpp

  DEPENDS
  TalosSDFAttrsIncGen
  TalosSDFDialectIncGen
  TalosSDFEnumsIncGen
  TalosSDFOpsIncGen

  LINK_LIBS PUBLIC
  MLIRIR
)
