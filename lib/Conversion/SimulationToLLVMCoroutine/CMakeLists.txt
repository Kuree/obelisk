add_mlir_library(ObeliskSimulationToLLVMCoroutine
  SimulationAggregateLowering.cpp
  SimulationControlLowering.cpp
  SimulationDPILowering.cpp
  SimulationEventLowering.cpp
  SimulationFunctionBodyLowering.cpp
  SimulationFunctionTypeLowering.cpp
  SimulationHandleLowering.cpp
  SimulationManagedLowering.cpp
  SimulationManagedPreparation.cpp
  SimulationManagedRootInstrumentation.cpp
  SimulationManagedRootSupport.cpp
  SimulationObserverLowering.cpp
  SimulationOverrideLowering.cpp
  SimulationProcessDescriptorLowering.cpp
  SimulationProcessCFGThreading.cpp
  SimulationReferenceLifetimeLowering.cpp
  SimulationRuntimeStatusThreading.cpp
  SimulationSuspensionTypeLowering.cpp
  SimulationToLLVMCoroutine.cpp
  SimulationToLLVMCoroutineUtils.cpp
  SimulationWaitLowering.cpp

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
