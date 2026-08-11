add_mlir_library(ObeliskSimulationIR
  SimulationAttributes.cpp
  SimulationCanonicalization.cpp
  SimulationContainerOps.cpp
  SimulationDeclOps.cpp
  SimulationDialect.cpp
  SimulationProcessOps.cpp
  SimulationTimingOps.cpp
  SimulationTypes.cpp
  SimulationOpDefs0.cpp
  SimulationOpDefs1.cpp
  SimulationOpDefs2.cpp
  SimulationOpDefs3.cpp

  DEPENDS
  ObeliskSimulationDialectIncGen
  ObeliskSimulationEnumsIncGen
  ObeliskSimulationAttrsIncGen
  ObeliskSimulationOpsIncGen
  ObeliskSimulationTypesIncGen

  LINK_LIBS PUBLIC
  ObeliskRuntimeIR
  MLIRCallInterfaces
  MLIRArithDialect
  MLIRControlFlowInterfaces
  MLIRFunctionInterfaces
  MLIRInferTypeOpInterface
  MLIRIR
  MLIRMemorySlotInterfaces
  MLIRSideEffectInterfaces
  MLIRViewLikeInterface
)

target_include_directories(ObeliskSimulationIR PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
