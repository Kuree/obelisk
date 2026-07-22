add_mlir_library(ObeliskSimulationAnalysis
  StateDomainAnalysis.cpp

  DEPENDS
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationIR
  MLIRControlFlowInterfaces
  MLIRIR
)
