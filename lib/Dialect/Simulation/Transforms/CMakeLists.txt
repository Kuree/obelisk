add_mlir_dialect_library(ObeliskSimulationTransforms
  DevirtualizeClassCalls.cpp
  EliminateDeadBoundaries.cpp
  EliminateDeadCaptures.cpp
  Inline.cpp
  PropagateInitializedStorage.cpp
  SCCP.cpp
  SimplifyBodies.cpp
  ThreadSuspension.cpp
  UnrollBoundedLoops.cpp

  DEPENDS
  ObeliskSimulationPassIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationToSchedule
  ObeliskScheduleTransforms
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
