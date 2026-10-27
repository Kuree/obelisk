# Canonical generated-file manifest shared by the in-tree native build and the
# standalone host-tools stage. Each consumer defines
# talos_register_generated_file() before including this file.

talos_register_generated_file(
  NAME DesignReflection
  OUTPUT include/talos/Reflection/DesignReflectionLayout.h.inc
  TOOL talos-tblgen
  INPUT include/talos/Reflection/DesignReflection.td
  ARGS -gen-talos-reflection-layout
  DEPENDS utils/talos-tblgen/talos-tblgen.cpp)

talos_register_generated_file(
  NAME VPIObjectModel
  OUTPUT include/talos/Reflection/VPIObjectModel.h.inc
  TOOL talos-tblgen
  INPUT include/talos/Reflection/VPITraversalModel.td
  ARGS -gen-talos-vpi-object-model
  INCLUDE_DIRS include/talos/Reflection
  DEPENDS
    include/talos/Reflection/VPIObjectModel.td
    include/talos/Reflection/VPITraversalEarly.td
    include/talos/Reflection/VPITraversalLate.td
    include/talos/Reflection/VPIValueModel.td
    utils/talos-tblgen/talos-tblgen.cpp)

talos_register_generated_file(
  NAME CoverageFormatDecls
  OUTPUT include/talos/Coverage/CoverageFormatDecls.h.inc
  TOOL talos-tblgen
  INPUT include/talos/Coverage/CoverageDatabase.td
  ARGS -gen-talos-coverage-format-decls
  DEPENDS utils/talos-tblgen/talos-tblgen.cpp)

talos_register_generated_file(
  NAME CoverageFormatParser
  OUTPUT include/talos/Coverage/CoverageFormatParser.h.inc
  TOOL talos-tblgen
  INPUT include/talos/Coverage/CoverageDatabase.td
  ARGS -gen-talos-coverage-format-parser
  DEPENDS utils/talos-tblgen/talos-tblgen.cpp)

talos_register_generated_file(
  NAME CoverageFormatSerializer
  OUTPUT include/talos/Coverage/CoverageFormatSerializer.h.inc
  TOOL talos-tblgen
  INPUT include/talos/Coverage/CoverageDatabase.td
  ARGS -gen-talos-coverage-format-serializer
  DEPENDS utils/talos-tblgen/talos-tblgen.cpp)

talos_register_generated_file(
  NAME ScheduleFields
  OUTPUT include/talos/Dialect/Schedule/ScheduleFields.h.inc
  TOOL talos-tblgen
  INPUT include/talos/Dialect/Schedule/ScheduleFields.td
  ARGS -gen-schedule-fields
  DEPENDS utils/talos-tblgen/talos-tblgen.cpp)
