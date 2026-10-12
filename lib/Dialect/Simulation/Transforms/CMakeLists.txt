add_mlir_dialect_library(ObeliskSimulationTransforms
  AliasPortConnections.cpp
  CompileRandomPlans.cpp
  DevirtualizeClassCalls.cpp
  EliminateDeadBoundaries.cpp
  EliminateDeadCaptures.cpp
  InsertGCPolls.cpp
  Inline.cpp
  PropagateInitializedStorage.cpp
  RefineContinuousSensitivity.cpp
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
  ObeliskSolver
  MLIRAnalysis
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRIR
  MLIRPass
  MLIRTransforms
)

target_include_directories(ObeliskSimulationTransforms PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
target_include_directories(obj.ObeliskSimulationTransforms PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
