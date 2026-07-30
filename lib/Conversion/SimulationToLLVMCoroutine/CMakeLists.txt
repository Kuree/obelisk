add_mlir_library(ObeliskSimulationToLLVMCoroutine
  SimulationAggregateLowering.cpp
  SimulationControlLowering.cpp
  SimulationDPILowering.cpp
  SimulationManagedLowering.cpp
  SimulationManagedPreparation.cpp
  SimulationToLLVMCoroutine.cpp
  SimulationToLLVMCoroutineUtils.cpp

  DEPENDS
  ObeliskConversionPassIncGen
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationAnalysis
  ObeliskRuntimeIR
  ObeliskRuntimeToLLVM
  ObeliskSimulationIR
  ObeliskSimulationToRuntime
  ObeliskSimulationToStandard
  MLIRAnalysis
  MLIRArithDialect
  MLIRArithToLLVM
  MLIRControlFlowDialect
  MLIRControlFlowToLLVM
  MLIRFuncDialect
  MLIRFuncToLLVM
  MLIRIR
  MLIRLLVMCommonConversion
  MLIRLLVMDialect
  MLIRMathDialect
  MLIRMathToLLVM
  MLIRPass
  MLIRTransforms
)

target_include_directories(ObeliskSimulationToLLVMCoroutine PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
