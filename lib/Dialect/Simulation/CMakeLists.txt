add_mlir_library(ObeliskSimulationIR
  SimulationDialect.cpp

  DEPENDS
  ObeliskSimulationDialectIncGen
  ObeliskSimulationEnumsIncGen
  ObeliskSimulationOpsIncGen
  ObeliskSimulationTypesIncGen

  LINK_LIBS PUBLIC
  MLIRCallInterfaces
  MLIRControlFlowInterfaces
  MLIRFunctionInterfaces
  MLIRIR
  MLIRMemorySlotInterfaces
  MLIRSideEffectInterfaces
)
