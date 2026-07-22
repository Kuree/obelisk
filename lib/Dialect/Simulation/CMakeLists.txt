add_mlir_library(ObeliskSimulationIR
  SimulationDialect.cpp

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
