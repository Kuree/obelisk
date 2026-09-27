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
  MarkCleanStaticNBA.cpp
  NativeEvalSpecialization.cpp
  SpecializeNativeCaptures.cpp
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
