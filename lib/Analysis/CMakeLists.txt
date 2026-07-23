add_mlir_library(ObeliskSimulationAnalysis
  NetConnectivityAnalysis.cpp
  SimulationAnalysis.cpp
  StateDomainAnalysis.cpp

  DEPENDS
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationIR
  MLIRControlFlowInterfaces
  MLIRIR
)
