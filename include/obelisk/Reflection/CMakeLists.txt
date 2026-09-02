set(OBELISK_GENERATED_DIR "" CACHE PATH
  "Host-generated files consumed by a cross build")

function(obelisk_register_generated_file)
  cmake_parse_arguments(ARG "" "NAME;OUTPUT;TOOL;INPUT"
    "ARGS;DEPENDS;INCLUDE_DIRS" ${ARGN})
  if(ARG_UNPARSED_ARGUMENTS OR ARG_KEYWORDS_MISSING_VALUES OR
      NOT ARG_NAME OR NOT ARG_OUTPUT OR NOT ARG_TOOL OR NOT ARG_INPUT)
    message(FATAL_ERROR
      "invalid generated-file manifest entry ${ARG_NAME}")
  endif()
  get_property(registered_names GLOBAL PROPERTY OBELISK_GENERATED_NAMES)
  get_property(registered_outputs GLOBAL PROPERTY OBELISK_GENERATED_OUTPUTS)
  if(ARG_NAME IN_LIST registered_names OR ARG_OUTPUT IN_LIST registered_outputs)
    message(FATAL_ERROR
      "duplicate generated-file NAME or OUTPUT for ${ARG_NAME}")
  endif()
  set_property(GLOBAL APPEND PROPERTY OBELISK_GENERATED_NAMES "${ARG_NAME}")
  set_property(GLOBAL APPEND PROPERTY OBELISK_GENERATED_OUTPUTS "${ARG_OUTPUT}")

  set(local_output "${CMAKE_BINARY_DIR}/${ARG_OUTPUT}")
  get_filename_component(local_directory "${local_output}" DIRECTORY)
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
    set(include_args)
    foreach(include_dir IN LISTS ARG_INCLUDE_DIRS)
      list(APPEND include_args -I "${PROJECT_SOURCE_DIR}/${include_dir}")
    endforeach()
    add_custom_command(
      OUTPUT "${local_output}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${local_directory}"
      COMMAND "$<TARGET_FILE:${ARG_TOOL}>" ${ARG_ARGS} ${include_args} "${input}"
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
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${local_directory}"
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
