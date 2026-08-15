add_mlir_library(ObeliskSimulationToRuntime
  SimulationToRuntime.cpp

  DEPENDS
  ObeliskConversionPassIncGen
  ObeliskRuntimeOpsIncGen
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskRuntimeIR
  ObeliskSimulationIR
  ObeliskSimulationToStandard
  MLIRArithDialect
  MLIRIR
  MLIRPass
  MLIRTransforms
)

target_include_directories(ObeliskSimulationToRuntime PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
