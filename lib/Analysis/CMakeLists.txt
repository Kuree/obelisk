add_mlir_library(ObeliskSimulationAnalysis
  NativeAOTAnalysis.cpp
  NetConnectivityAnalysis.cpp
  SimulationAnalysis.cpp
  StaticSpecializationAnalysis.cpp
  StateDomainAnalysis.cpp

  DEPENDS
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationIR
  MLIRControlFlowDialect
  MLIRControlFlowInterfaces
  MLIRIR
)
