add_mlir_library(ObeliskToSimulation
  BuildComputeGraph.cpp
  ComputeFusion.cpp
  ComputeGraph.cpp
  EliminateDeadBoundaries.cpp
  EliminateDeadCaptures.cpp
  Finalize.cpp
  FuseComputeFragments.cpp
  Inline.cpp
  LowerUnit.cpp
  LowerUnitCalls.cpp
  LowerUnitConditionals.cpp
  LowerUnitContainers.cpp
  LowerUnitControlFlow.cpp
  LowerUnitCovergroups.cpp
  LowerUnitExpressions.cpp
  LowerUnitLValues.cpp
  LowerUnitOperators.cpp
  LowerUnitSystemCalls.cpp
  LowerUnitTiming.cpp
  MaterializeComputeFusion.cpp
  PlanStaticSuperstep.cpp
  Prepare.cpp
  SCCP.cpp
  SemanticUtils.cpp
  SpecializeStaticStateNBA.cpp
  ThreadSuspension.cpp
  VerifyComputeGraph.cpp

  DEPENDS
  ObeliskConversionPassIncGen
  ObeliskSimulationOpsIncGen

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

target_include_directories(ObeliskToSimulation PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
