add_mlir_library(ObeliskDPIIR
  DPIDialect.cpp

  DEPENDS
  ObeliskDPIAttrsIncGen
  ObeliskDPIDialectIncGen
  ObeliskDPIEnumsIncGen

  LINK_LIBS PUBLIC
  MLIRIR
)

target_include_directories(ObeliskDPIIR PRIVATE
  ${PROJECT_SOURCE_DIR}/runtime/include
)
