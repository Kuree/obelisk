add_mlir_library(ObeliskMooreToObelisk
  MooreToObelisk.cpp

  DEPENDS
  ObeliskConversionPassIncGen

  LINK_LIBS PUBLIC
  CIRCTHW
  CIRCTMoore
  CIRCTSim
  ObeliskIR
  MLIRArithDialect
  MLIRIR
  MLIRPass
  MLIRTransformUtils
)
