add_mlir_library(TalosSimulationIR
  SimulationAttributes.cpp
  SimulationCanonicalization.cpp
  SimulationContainerOps.cpp
  SimulationDeclOps.cpp
  SimulationDialect.cpp
  SimulationProcessOps.cpp
  SimulationTimingOps.cpp
  SimulationTypes.cpp
  SimulationVPI.cpp
  SimulationVPIExpressions.cpp
  SimulationVPIReferences.cpp
  SimulationVPIInterfaceDispatch.cpp
  SimulationValueInterfaces.cpp
  SimulationOpDefs0.cpp
  SimulationOpDefs1.cpp
  SimulationOpDefs2.cpp
  SimulationOpDefs3.cpp

  DEPENDS
  TalosCoverageAttrsIncGen
  TalosCoverageDialectIncGen
  TalosDPIAttrsIncGen
  TalosDPIDialectIncGen
  TalosDPIEnumsIncGen
  TalosNativeAttrsIncGen
  TalosNativeDialectIncGen
  TalosScheduleAttrsIncGen
  TalosScheduleOpsIncGen
  TalosScheduleDialectIncGen
  TalosSimulationDialectIncGen
  TalosSimulationEnumsIncGen
  TalosSimulationAttrsIncGen
  TalosSimulationOpsIncGen
  TalosSimulationTypesIncGen
  TalosVPIObjectModelIncGen

  LINK_LIBS PUBLIC
  TalosCoverageIR
  TalosDPIIR
  TalosNativeIR
  TalosRuntimeIR
  MLIRCallInterfaces
  MLIRCastInterfaces
  MLIRArithDialect
  MLIRMathDialect
  MLIRControlFlowInterfaces
  MLIRFunctionInterfaces
  MLIRInferTypeOpInterface
  MLIRInferIntRangeInterface
  MLIRIR
  MLIRMemorySlotInterfaces
  MLIRSideEffectInterfaces
  MLIRViewLikeInterface
)

target_link_libraries(TalosSimulationIR PUBLIC TalosScheduleIR)

target_include_directories(TalosSimulationIR PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)

add_subdirectory(Transforms)
