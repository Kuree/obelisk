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
