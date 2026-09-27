add_mlir_library(ObeliskScheduleIR
  ScheduleOps.cpp
  Export.cpp
  ScheduleDialect.cpp
  ScheduleAttributes.cpp
  ScheduleFields.cpp
  DEPENDS
  ObeliskScheduleOpsIncGen
  ObeliskScheduleFieldsIncGen
  ObeliskScheduleDialectIncGen
  ObeliskScheduleAttrsIncGen
  ObeliskScheduleEnumsIncGen
  LINK_LIBS PUBLIC
  MLIRControlFlowInterfaces
  MLIRFunctionInterfaces
  MLIRLLVMDialect
  MLIRSideEffectInterfaces
  MLIRIR
)
# These dialects share typed scheduling sites and native process operands.
# Record their mutual linkage on the static archives, not on the independent
# object-library compilation targets created by add_mlir_library.
target_link_libraries(ObeliskScheduleIR PUBLIC ObeliskSimulationIR)
add_subdirectory(Transforms)
