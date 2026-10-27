add_mlir_library(TalosDPIIR
  DPIDialect.cpp

  DEPENDS
  TalosDPIAttrsIncGen
  TalosDPIDialectIncGen
  TalosDPIEnumsIncGen

  LINK_LIBS PUBLIC
  MLIRIR
)

target_include_directories(TalosDPIIR PRIVATE
  ${PROJECT_SOURCE_DIR}/runtime/include
)
