add_mlir_library(ObeliskSimulationToLLVMCoroutine
  SimulationToLLVMCoroutine.cpp

  DEPENDS
  ObeliskConversionPassIncGen
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskRuntimeIR
  ObeliskRuntimeToLLVM
  ObeliskSimulationIR
  ObeliskSimulationToRuntime
  ObeliskSimulationToStandard
  MLIRArithDialect
  MLIRArithToLLVM
  MLIRControlFlowDialect
  MLIRControlFlowToLLVM
  MLIRFuncDialect
  MLIRFuncToLLVM
  MLIRIR
  MLIRLLVMCommonConversion
  MLIRLLVMDialect
  MLIRPass
  MLIRTransforms
)
