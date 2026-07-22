add_mlir_library(ObeliskToSimulation
  BuildComputeGraph.cpp
  ComputeGraph.cpp
  Finalize.cpp
  Inline.cpp
  LowerUnit.cpp
  Prepare.cpp
  SCCP.cpp
  SemanticUtils.cpp
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
