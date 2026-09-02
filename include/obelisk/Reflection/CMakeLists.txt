set(OBELISK_GENERATED_DIR "" CACHE PATH
  "Host-generated files consumed by a cross build")

function(obelisk_register_generated_file)
  cmake_parse_arguments(ARG "" "NAME;OUTPUT;TOOL;INPUT" "ARGS;DEPENDS" ${ARGN})
  if(NOT ARG_NAME OR NOT ARG_OUTPUT OR NOT ARG_TOOL OR NOT ARG_INPUT)
    message(FATAL_ERROR
      "generated-file manifest entries need NAME, OUTPUT, TOOL, and INPUT")
  endif()

  get_filename_component(output_name "${ARG_OUTPUT}" NAME)
  set(local_output "${CMAKE_CURRENT_BINARY_DIR}/${output_name}")
  set(staged_output "${CMAKE_BINARY_DIR}/generated/${ARG_OUTPUT}")
  get_filename_component(staged_directory "${staged_output}" DIRECTORY)
  set(generated_target "Obelisk${ARG_NAME}Generated")
  set(import_target "Obelisk${ARG_NAME}IncGen")

  if(NOT CMAKE_CROSSCOMPILING)
    set(input "${PROJECT_SOURCE_DIR}/${ARG_INPUT}")
    set(depends)
    foreach(dependency IN LISTS ARG_DEPENDS)
      list(APPEND depends "${PROJECT_SOURCE_DIR}/${dependency}")
    endforeach()
    add_custom_command(
      OUTPUT "${local_output}"
      COMMAND "$<TARGET_FILE:${ARG_TOOL}>" ${ARG_ARGS} "${input}"
        --write-if-changed -o "${local_output}" -d "${local_output}.d"
      DEPENDS "${ARG_TOOL}" "${input}" ${depends}
      DEPFILE "${local_output}.d"
      VERBATIM
      COMMENT "Generating ${ARG_NAME}")
    add_custom_target("${generated_target}" DEPENDS "${local_output}")
    add_custom_command(
      OUTPUT "${staged_output}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${staged_directory}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${local_output}" "${staged_output}"
      DEPENDS "${generated_target}" "${local_output}"
      COMMENT "Staging generated ${ARG_NAME}")
    set(target_output "${staged_output}")
  else()
    set(host_output "${OBELISK_GENERATED_DIR}/${ARG_OUTPUT}")
    if(NOT OBELISK_GENERATED_DIR OR NOT EXISTS "${host_output}")
      message(FATAL_ERROR
        "Cross builds require ${ARG_OUTPUT} from the native host-tools stage; "
        "set OBELISK_GENERATED_DIR to that stage's output")
    endif()
    add_custom_command(
      OUTPUT "${local_output}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${host_output}" "${local_output}"
      DEPENDS "${host_output}"
      COMMENT "Importing host-generated ${ARG_NAME}")
    set(target_output "${local_output}")
  endif()

  add_custom_target("${import_target}" DEPENDS "${target_output}")
  add_dependencies(obelisk-generated-files "${import_target}")
endfunction()

include("${PROJECT_SOURCE_DIR}/cmake/ObeliskGeneratedFiles.cmake")
