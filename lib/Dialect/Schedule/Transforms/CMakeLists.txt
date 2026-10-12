add_mlir_library(ObeliskScheduleTransforms
  PARTIAL_SOURCES_INTENDED
  ComputeFusion.cpp
  FuseClockedKernels.cpp
  FuseComputeFragments.cpp
  LowerConditionalReferenceAccesses.cpp
  MaterializeComputeFusion.cpp
  MaterializeGraphRegions.cpp
  PlanCombinationalStatements.cpp
  PlanStaticSuperstep.cpp
  SimulationNativeRegionOptimization.cpp
  SpecializeStaticStateNBA.cpp
  SpecializeReferenceJoins.cpp
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
  MLIRSCFDialect
  MLIRPass
  MLIRTransforms
)

add_mlir_library(ObeliskScheduleNativeTransforms
  PARTIAL_SOURCES_INTENDED
  NativeSpecializationPasses.cpp
  NativeWords.cpp
  NativeAggregates.cpp
  NativeWordPredication.cpp
  NativeTransfers.cpp
  MarkCleanStaticNBA.cpp
  NativeEvalSpecialization.cpp
  SpecializeNativeCaptures.cpp
  CoalesceNativeReady.cpp
  CachePureCones.cpp
  VersionKnownRegions.cpp
  DEPENDS
  ObeliskSchedulePassIncGen
  LINK_LIBS PUBLIC
  ObeliskSimulationIR
  ObeliskSimulationAnalysis
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRLLVMDialect
  MLIRSCFDialect
  MLIRUBDialect
  MLIRTransforms
)
target_include_directories(ObeliskScheduleNativeTransforms PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include")
