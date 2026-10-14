add_mlir_library(ObeliskCoverageIR
  CoverageDialect.cpp

  DEPENDS
  ObeliskCoverageAttrsIncGen
  ObeliskCoverageDialectIncGen

  LINK_LIBS PUBLIC
  MLIRIR
)
