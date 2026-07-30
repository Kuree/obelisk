add_mlir_library(ObeliskSimulationToStandard
  SimulationTimeLowering.cpp
  SimulationToStandard.cpp

  DEPENDS
  ObeliskConversionPassIncGen
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationIR
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRFuncDialect
  MLIRFuncTransforms
  MLIRIR
  MLIRMathDialect
  MLIRPass
  MLIRSCFDialect
  MLIRTransforms
)
