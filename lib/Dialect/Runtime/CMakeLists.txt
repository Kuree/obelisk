add_mlir_library(TalosRuntimeIR
  RuntimeDialect.cpp

  DEPENDS
  TalosRuntimeAttrsIncGen
  TalosRuntimeDialectIncGen
  TalosRuntimeEnumsIncGen
  TalosRuntimeOpsIncGen
  TalosRuntimeTypesIncGen

  LINK_LIBS PUBLIC
  MLIRIR
  MLIRSideEffectInterfaces
)

target_include_directories(TalosRuntimeIR PRIVATE
  ${PROJECT_SOURCE_DIR}/runtime/include
)
