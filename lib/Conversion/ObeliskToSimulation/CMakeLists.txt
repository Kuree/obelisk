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
  LowerUnitArrayQuerySystemCalls.cpp
  LowerUnitCalls.cpp
  LowerUnitConditionals.cpp
  LowerUnitContainers.cpp
  LowerUnitControlFlow.cpp
  LowerUnitCovergroups.cpp
  LowerUnitDisplaySystemCalls.cpp
  LowerUnitExpressions.cpp
  LowerUnitFileSystemCalls.cpp
  LowerUnitLValues.cpp
  LowerUnitOperators.cpp
  LowerUnitRealConversionSystemCalls.cpp
  LowerUnitRealMathSystemCalls.cpp
  LowerUnitSystemCalls.cpp
  LowerUnitTiming.cpp
  MaterializeComputeFusion.cpp
  PlanStaticSuperstep.cpp
  Prepare.cpp
  PrepareDeclarations.cpp
  PrepareNetTopology.cpp
  PrepareTopology.cpp
  PrepareValidation.cpp
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
