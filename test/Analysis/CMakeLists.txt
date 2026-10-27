add_mlir_library(TalosAnalysisTestPasses
  AccessFootprintAnalysisTestPass.cpp
  ActivationMemoryAnalysisTestPass.cpp
  ActivationPhaseAnalysisTestPass.cpp
  BytecodeImageTestPass.cpp
  ClockInferenceAnalysisTestPass.cpp
  DPIEffectAnalysisTestPass.cpp
  ClassDispatchAnalysisTestPass.cpp
  CombinationalDependencyAnalysisTestPass.cpp
  CoverageSchemaTestPass.cpp
  ManagedClassLayoutAnalysisTestPass.cpp
  LocalStorageAnalysisTestPass.cpp
  LocalUnknownAnalysisTestPass.cpp
  SteadyUnknownAnalysisTestPass.cpp
  NativeAOTAnalysisTestPass.cpp
  NativeSizeCensusTestPass.cpp
  NativeStateLayoutAnalysisTestPass.cpp
  RandomProgramTestPass.cpp
  SimulationProcessFrameAnalysisTestPass.cpp
  SimulationScheduleAnalysisTestPass.cpp
  SimulationVPIAnalysisTestPass.cpp
  StaticSpecializationAnalysisTestPass.cpp
  StateDomainTestPass.cpp
  StorageWriteAnalysisTestPass.cpp
  ModuleTestPasses.cpp
  NativeBackendTestPipeline.cpp
  ValueInterfacesTestPass.cpp

  EXCLUDE_FROM_LIBMLIR
  DISABLE_INSTALL

  DEPENDS
  TalosSimulationOpsIncGen

  LINK_LIBS PUBLIC
  TalosCoverage
  TalosSimulationAnalysis
  TalosScheduleTransforms
  TalosSimulationIR
  MLIRArithDialect
  MLIRAsmParser
  MLIRIR
  MLIRLLVMDialect
  MLIRPass
  MLIRSCFDialect
  MLIRSCFUtils
  MLIRUBDialect
  LLVMCore
)

target_include_directories(TalosAnalysisTestPasses PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)

include(${PROJECT_SOURCE_DIR}/cmake/TalosPrecompiledHeaders.cmake)
talos_precompile_headers(TalosAnalysisTestPasses
  "${PROJECT_SOURCE_DIR}/include/talos/Dialect/Simulation/SimulationOps.h"
  <mlir/Pass/Pass.h>)
