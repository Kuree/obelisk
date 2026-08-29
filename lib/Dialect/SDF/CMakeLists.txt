add_mlir_library(ObeliskSDFIR
  SDFDialect.cpp

  DEPENDS
  ObeliskSDFAttrsIncGen
  ObeliskSDFDialectIncGen
  ObeliskSDFEnumsIncGen
  ObeliskSDFOpsIncGen

  LINK_LIBS PUBLIC
  MLIRIR
)
