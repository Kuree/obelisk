add_mlir_library(ObeliskRuntimeIR
  RuntimeDialect.cpp

  DEPENDS
  ObeliskRuntimeDialectIncGen
  ObeliskRuntimeEnumsIncGen
  ObeliskRuntimeOpsIncGen
  ObeliskRuntimeTypesIncGen

  LINK_LIBS PUBLIC
  MLIRIR
  MLIRSideEffectInterfaces
)

target_include_directories(ObeliskRuntimeIR PRIVATE
  ${PROJECT_SOURCE_DIR}/runtime/include
)
