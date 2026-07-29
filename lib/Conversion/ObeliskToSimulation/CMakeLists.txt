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
  MaterializeComputeFusion.cpp
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
