add_mlir_library(ObeliskAnalysisTestPasses
  NativeAOTAnalysisTestPass.cpp
  SimulationScheduleAnalysisTestPass.cpp
  StateDomainTestPass.cpp

  EXCLUDE_FROM_LIBMLIR
  DISABLE_INSTALL

  DEPENDS
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationAnalysis
  ObeliskSimulationIR
  MLIRIR
  MLIRPass
)
