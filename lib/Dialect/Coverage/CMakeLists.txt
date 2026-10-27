add_mlir_library(TalosCoverageIR
  CoverageDialect.cpp

  DEPENDS
  TalosCoverageAttrsIncGen
  TalosCoverageDialectIncGen

  LINK_LIBS PUBLIC
  MLIRIR
)
