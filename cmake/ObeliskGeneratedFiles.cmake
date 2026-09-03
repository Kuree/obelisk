# Canonical generated-file manifest shared by the in-tree native build and the
# standalone host-tools stage. Each consumer defines
# obelisk_register_generated_file() before including this file.

obelisk_register_generated_file(
  NAME DesignReflection
  OUTPUT include/obelisk/Reflection/DesignReflectionLayout.h.inc
  TOOL obelisk-tblgen
  INPUT include/obelisk/Reflection/DesignReflection.td
  ARGS -gen-obelisk-reflection-layout
  DEPENDS utils/obelisk-tblgen/obelisk-tblgen.cpp)

obelisk_register_generated_file(
  NAME VPIObjectModel
  OUTPUT include/obelisk/Reflection/VPIObjectModel.h.inc
  TOOL obelisk-tblgen
  INPUT include/obelisk/Reflection/VPITraversalModel.td
  ARGS -gen-obelisk-vpi-object-model
  INCLUDE_DIRS include/obelisk/Reflection
  DEPENDS
    include/obelisk/Reflection/VPIObjectModel.td
    include/obelisk/Reflection/VPITraversalEarly.td
    include/obelisk/Reflection/VPITraversalLate.td
    include/obelisk/Reflection/VPIValueModel.td
    utils/obelisk-tblgen/obelisk-tblgen.cpp)
