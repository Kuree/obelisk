add_mlir_library(ObeliskSimulationAnalysis
  BoundedLoopAnalysis.cpp
  ScopedIntegerRangeAnalysis.cpp
  ClassBitstreamAnalysis.cpp
  ClassBitstreamPlan.cpp
  ClassDispatchAnalysis.cpp
  ClockInferenceAnalysis.cpp
  DemandedBitAnalysis.cpp
  HandleDataflowAnalysis.cpp
  LogicBitAnalysis.cpp
  ManagedClassLayoutAnalysis.cpp
  NativeAOTAnalysis.cpp
  NativeStateLayoutAnalysis.cpp
  NetConnectivityAnalysis.cpp
  PrivateStorageAnalysis.cpp
  SimulationCopyProcessAnalysis.cpp
  SimulationEffectAnalysis.cpp
  SimulationProcessFrameAnalysis.cpp
  SimulationScheduleAnalysis.cpp
  SimulationAnalysis.cpp
  SimulationStorageAnalysis.cpp
  SimulationVPIAnalysis.cpp
  StaticSpecializationAnalysis.cpp
  SSAValueAnalysis.cpp
  SourceMemoryAnalysis.cpp
  StateDomainAnalysis.cpp
  StorageWriteAnalysis.cpp

  DEPENDS
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationIR
  MLIRAnalysis
  MLIRControlFlowDialect
  MLIRControlFlowInterfaces
  MLIRIR
  LLVMCore
)

target_include_directories(ObeliskSimulationAnalysis PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
