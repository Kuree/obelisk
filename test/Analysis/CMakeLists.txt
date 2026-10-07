add_mlir_library(ObeliskAnalysisTestPasses
  AccessFootprintAnalysisTestPass.cpp
  ActivationMemoryAnalysisTestPass.cpp
  ClockInferenceAnalysisTestPass.cpp
  CombinationalDependencyAnalysisTestPass.cpp
  ManagedClassLayoutAnalysisTestPass.cpp
  NativeAOTAnalysisTestPass.cpp
  NativeStateLayoutAnalysisTestPass.cpp
  SimulationProcessFrameAnalysisTestPass.cpp
  SimulationScheduleAnalysisTestPass.cpp
  SimulationVPIAnalysisTestPass.cpp
  StaticSpecializationAnalysisTestPass.cpp
  StateDomainTestPass.cpp
  StorageWriteAnalysisTestPass.cpp
  ValueInterfacesTestPass.cpp

  EXCLUDE_FROM_LIBMLIR
  DISABLE_INSTALL

  DEPENDS
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationAnalysis
  ObeliskSimulationIR
  MLIRIR
  MLIRPass
  LLVMCore
)
