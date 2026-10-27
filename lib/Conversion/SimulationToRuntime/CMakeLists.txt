add_mlir_library(TalosSimulationToRuntime
  SimulationToRuntime.cpp

  DEPENDS
  TalosConversionPassIncGen
  TalosRuntimeOpsIncGen
  TalosSimulationOpsIncGen

  LINK_LIBS PUBLIC
  TalosRuntimeIR
  TalosSimulationIR
  TalosSimulationToStandard
  MLIRArithDialect
  MLIRIR
  MLIRPass
  MLIRTransforms
)

target_include_directories(TalosSimulationToRuntime PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
