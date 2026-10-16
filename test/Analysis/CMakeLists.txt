add_mlir_library(ObeliskAnalysisTestPasses
  AccessFootprintAnalysisTestPass.cpp
  ActivationMemoryAnalysisTestPass.cpp
  ActivationPhaseAnalysisTestPass.cpp
  BytecodeImageTestPass.cpp
  ClockInferenceAnalysisTestPass.cpp
  ClassDispatchAnalysisTestPass.cpp
  CombinationalDependencyAnalysisTestPass.cpp
  CoverageSchemaTestPass.cpp
  ManagedClassLayoutAnalysisTestPass.cpp
  LocalStorageAnalysisTestPass.cpp
  NativeAOTAnalysisTestPass.cpp
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
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskCoverage
  ObeliskSimulationAnalysis
  ObeliskSimulationIR
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

target_include_directories(ObeliskAnalysisTestPasses PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)

include(${PROJECT_SOURCE_DIR}/cmake/ObeliskPrecompiledHeaders.cmake)
obelisk_precompile_headers(ObeliskAnalysisTestPasses
  "${PROJECT_SOURCE_DIR}/include/obelisk/Dialect/Simulation/SimulationOps.h"
  <mlir/Pass/Pass.h>)
