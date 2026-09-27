add_mlir_library(ObeliskScheduleIR
  Export.cpp
  ScheduleDialect.cpp
  ScheduleAttributes.cpp
  ScheduleFields.cpp
  DEPENDS
  ObeliskScheduleFieldsIncGen
  ObeliskScheduleDialectIncGen
  ObeliskScheduleAttrsIncGen
  ObeliskScheduleEnumsIncGen
  LINK_LIBS PUBLIC
  MLIRIR
)
add_subdirectory(Transforms)
