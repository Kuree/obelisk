set(_obelisk_reflection_relative
  "include/obelisk/Reflection/DesignReflectionLayout.h.inc")
set(_obelisk_reflection_output
  "${CMAKE_CURRENT_BINARY_DIR}/DesignReflectionLayout.h.inc")
set(_obelisk_reflection_staged
  "${CMAKE_BINARY_DIR}/generated/${_obelisk_reflection_relative}")

if(NOT CMAKE_CROSSCOMPILING)
  set(LLVM_TARGET_DEFINITIONS DesignReflection.td)
  tablegen(OBELISK DesignReflectionLayout.h.inc
    -gen-obelisk-reflection-layout)
  add_public_tablegen_target(ObeliskDesignReflectionGenerated)
  add_custom_command(
    OUTPUT "${_obelisk_reflection_staged}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
      "${CMAKE_BINARY_DIR}/generated/include/obelisk/Reflection"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${_obelisk_reflection_output}" "${_obelisk_reflection_staged}"
    DEPENDS ObeliskDesignReflectionGenerated "${_obelisk_reflection_output}"
    COMMENT "Staging generated reflection tables")
  set(_obelisk_reflection_target_outputs "${_obelisk_reflection_staged}")
else()
  set(OBELISK_GENERATED_DIR "" CACHE PATH
    "Host-generated files consumed by a cross build")
  set(_obelisk_reflection_input
    "${OBELISK_GENERATED_DIR}/${_obelisk_reflection_relative}")
  if(NOT OBELISK_GENERATED_DIR OR NOT EXISTS "${_obelisk_reflection_input}")
    message(FATAL_ERROR
      "Cross builds require ${_obelisk_reflection_relative} from the native "
      "host-tools stage; set OBELISK_GENERATED_DIR to that stage's output")
  endif()
  add_custom_command(
    OUTPUT "${_obelisk_reflection_output}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${_obelisk_reflection_input}" "${_obelisk_reflection_output}"
    DEPENDS "${_obelisk_reflection_input}"
    COMMENT "Importing host-generated reflection tables")
  set(_obelisk_reflection_target_outputs "${_obelisk_reflection_output}")
endif()

add_custom_target(ObeliskDesignReflectionIncGen
  DEPENDS ${_obelisk_reflection_target_outputs})
add_dependencies(obelisk-generated-files ObeliskDesignReflectionIncGen)
