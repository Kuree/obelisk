add_mlir_dialect_library(TalosSimulationTransforms
  AliasPortConnections.cpp
  AnnotateSteadyUnknown.cpp
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
  TalosSimulationPassIncGen

  LINK_LIBS PUBLIC
  TalosSimulationToSchedule
  TalosScheduleTransforms
  TalosIR
  TalosSimulationAnalysis
  TalosSimulationIR
  TalosSolver
  MLIRAnalysis
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRIR
  MLIRPass
  MLIRTransforms
)

target_include_directories(TalosSimulationTransforms PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
target_include_directories(obj.TalosSimulationTransforms PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
