add_mlir_library(ObeliskSimulationToStandard
  SimulationTimeLowering.cpp
  SimulationToStandard.cpp
  NativeWordLowering.cpp

  DEPENDS
  ObeliskConversionPassIncGen
  ObeliskSimulationOpsIncGen
  ObeliskScheduleOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationAnalysis
  ObeliskSimulationIR
  ObeliskScheduleIR
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRFuncDialect
  MLIRFuncTransforms
  MLIRIR
  MLIRMathDialect
  MLIRLLVMDialect
  MLIRPass
  MLIRSCFDialect
  MLIRTransforms
)
