add_mlir_library(ObeliskToSimulation
  BuildComputeGraph.cpp
  ComputeGraph.cpp
  Finalize.cpp
  LowerUnit.cpp
  Prepare.cpp
  SemanticUtils.cpp
  ThreadSuspension.cpp
  VerifyComputeGraph.cpp

  DEPENDS
  ObeliskConversionPassIncGen
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskIR
  ObeliskSimulationIR
  MLIRAnalysis
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRIR
  MLIRPass
  MLIRTransforms
)
