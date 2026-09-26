add_mlir_dialect_library(ObeliskSimulationTransforms
  BuildComputeGraph.cpp
  ComputeFusion.cpp
  ComputeGraph.cpp
  DevirtualizeClassCalls.cpp
  EliminateDeadBoundaries.cpp
  EliminateDeadCaptures.cpp
  FuseComputeFragments.cpp
  Inline.cpp
  MaterializeClockedControl.cpp
  MaterializeComputeFusion.cpp
  MaterializeGraphRegions.cpp
  PlanStaticSuperstep.cpp
  PropagateInitializedStorage.cpp
  SCCP.cpp
  SimulationNativeRegionOptimization.cpp
  SpecializeStaticStateNBA.cpp
  ThreadSuspension.cpp
  UnrollBoundedLoops.cpp
  Utils.cpp
  VerifyComputeGraph.cpp

  DEPENDS
  ObeliskSimulationPassIncGen

  LINK_LIBS PUBLIC
  ObeliskIR
  ObeliskSimulationAnalysis
  ObeliskSimulationIR
  MLIRAnalysis
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRIR
  MLIRPass
  MLIRTransforms
)
