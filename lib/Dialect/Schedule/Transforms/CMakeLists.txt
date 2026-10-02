add_mlir_library(ObeliskScheduleTransforms
  PARTIAL_SOURCES_INTENDED
  ComputeFusion.cpp
  FuseComputeFragments.cpp
  MaterializeComputeFusion.cpp
  MaterializeGraphRegions.cpp
  PlanStaticSuperstep.cpp
  SimulationNativeRegionOptimization.cpp
  SpecializeStaticStateNBA.cpp
  VerifyComputeGraph.cpp

  DEPENDS
  ObeliskSchedulePassIncGen
  LINK_LIBS PUBLIC
  ObeliskSimulationToSchedule
  ObeliskSimulationAnalysis
  ObeliskSimulationIR
  ObeliskScheduleIR
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRPass
  MLIRTransforms
)

add_mlir_library(ObeliskScheduleNativeTransforms
  PARTIAL_SOURCES_INTENDED
  NativeSpecializationPasses.cpp
  NativeTransfers.cpp
  MarkCleanStaticNBA.cpp
  NativeEvalSpecialization.cpp
  SpecializeNativeCaptures.cpp
  CoalesceNativeReady.cpp
  CachePureCones.cpp
  DEPENDS
  ObeliskSchedulePassIncGen
  LINK_LIBS PUBLIC
  ObeliskSimulationIR
  ObeliskSimulationAnalysis
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRLLVMDialect
  MLIRTransforms
)
target_include_directories(ObeliskScheduleNativeTransforms PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include")
