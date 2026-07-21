add_mlir_library(ObeliskToSimulation
  Finalize.cpp
  LowerUnit.cpp
  Prepare.cpp
  SemanticUtils.cpp
  ThreadSuspension.cpp

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
