add_mlir_library(TalosSimulationToStandard
  SimulationTimeLowering.cpp
  SimulationToStandard.cpp
  NativeWordLowering.cpp

  DEPENDS
  TalosConversionPassIncGen
  TalosSimulationOpsIncGen
  TalosScheduleOpsIncGen

  LINK_LIBS PUBLIC
  TalosSimulationAnalysis
  TalosSimulationIR
  TalosScheduleIR
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
