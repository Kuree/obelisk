add_mlir_library(ObeliskSimulationAnalysis
  SimulationAnalysis.cpp
  StateDomainAnalysis.cpp

  DEPENDS
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationIR
  MLIRControlFlowInterfaces
  MLIRIR
)
