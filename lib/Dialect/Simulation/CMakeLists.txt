add_mlir_library(ObeliskSimulationIR
  SimulationAttributes.cpp
  SimulationCanonicalization.cpp
  SimulationContainerOps.cpp
  SimulationDeclOps.cpp
  SimulationDialect.cpp
  SimulationProcessOps.cpp
  SimulationTimingOps.cpp
  SimulationTypes.cpp
  SimulationVPI.cpp
  SimulationValueInterfaces.cpp
  SimulationOpDefs0.cpp
  SimulationOpDefs1.cpp
  SimulationOpDefs2.cpp
  SimulationOpDefs3.cpp

  DEPENDS
  ObeliskCoverageAttrsIncGen
  ObeliskCoverageDialectIncGen
  ObeliskDPIAttrsIncGen
  ObeliskDPIDialectIncGen
  ObeliskDPIEnumsIncGen
  ObeliskNativeAttrsIncGen
  ObeliskNativeDialectIncGen
  ObeliskScheduleAttrsIncGen
  ObeliskScheduleOpsIncGen
  ObeliskScheduleDialectIncGen
  ObeliskSimulationDialectIncGen
  ObeliskSimulationEnumsIncGen
  ObeliskSimulationAttrsIncGen
  ObeliskSimulationOpsIncGen
  ObeliskSimulationTypesIncGen
  ObeliskVPIObjectModelIncGen

  LINK_LIBS PUBLIC
  ObeliskCoverageIR
  ObeliskDPIIR
  ObeliskNativeIR
  ObeliskRuntimeIR
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

target_link_libraries(ObeliskSimulationIR PUBLIC ObeliskScheduleIR)

target_include_directories(ObeliskSimulationIR PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)

add_subdirectory(Transforms)
