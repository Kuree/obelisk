add_mlir_library(ObeliskSimulationAnalysis
  ClassBitstreamAnalysis.cpp
  ClassBitstreamPlan.cpp
  ClassDispatchAnalysis.cpp
  ManagedClassLayoutAnalysis.cpp
  NativeAOTAnalysis.cpp
  NativeStateLayoutAnalysis.cpp
  NetConnectivityAnalysis.cpp
  SimulationCopyProcessAnalysis.cpp
  SimulationProcessFrameAnalysis.cpp
  SimulationScheduleAnalysis.cpp
  SimulationAnalysis.cpp
  SimulationStorageAnalysis.cpp
  SimulationVPIAnalysis.cpp
  StaticSpecializationAnalysis.cpp
  StateDomainAnalysis.cpp

  DEPENDS
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationIR
  MLIRControlFlowDialect
  MLIRControlFlowInterfaces
  MLIRIR
  LLVMCore
)

target_include_directories(ObeliskSimulationAnalysis PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
