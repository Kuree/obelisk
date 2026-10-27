add_mlir_library(TalosScheduleIR
  ScheduleOps.cpp
  Export.cpp
  ScheduleDialect.cpp
  ScheduleAttributes.cpp
  ScheduleFields.cpp
  DEPENDS
  TalosScheduleOpsIncGen
  TalosScheduleFieldsIncGen
  TalosScheduleDialectIncGen
  TalosScheduleAttrsIncGen
  TalosScheduleEnumsIncGen
  # ScheduleOps.cpp includes the Simulation ops it schedules.
  TalosSimulationDialectIncGen
  TalosSimulationEnumsIncGen
  TalosSimulationAttrsIncGen
  TalosSimulationOpsIncGen
  TalosSimulationTypesIncGen
  LINK_LIBS PUBLIC
  MLIRArithDialect
  MLIRControlFlowInterfaces
  MLIRFunctionInterfaces
  MLIRLLVMDialect
  MLIRSideEffectInterfaces
  MLIRViewLikeInterface
  MLIRTransformUtils
  MLIRIR
)
# These dialects share typed scheduling sites and native process operands.
# Record their mutual linkage on the static archives, not on the independent
# object-library compilation targets created by add_mlir_library.
target_link_libraries(TalosScheduleIR PUBLIC TalosSimulationIR)
add_subdirectory(Transforms)
