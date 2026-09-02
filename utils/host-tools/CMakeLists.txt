cmake_minimum_required(VERSION 3.20)
project(obelisk-host-tools LANGUAGES C CXX)

find_package(LLVM REQUIRED CONFIG)
find_package(MLIR REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}" "${MLIR_CMAKE_DIR}")
include(AddLLVM)
include(HandleLLVMOptions)

include_directories(${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})

get_filename_component(OBELISK_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.."
  ABSOLUTE)
set(OBELISK_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated" CACHE PATH
  "Output tree shared with target builds")

set(LLVM_LINK_COMPONENTS TableGen)
add_llvm_executable(obelisk-tblgen DISABLE_LLVM_LINK_LLVM_DYLIB
  "${OBELISK_SOURCE_DIR}/utils/obelisk-tblgen/obelisk-tblgen.cpp")
llvm_update_compile_flags(obelisk-tblgen)
target_link_libraries(obelisk-tblgen PRIVATE MLIRTblgenLib)
set_target_properties(obelisk-tblgen PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(_obelisk_generated_files)
function(obelisk_add_generated_file relative_output)
  cmake_parse_arguments(ARG "" "TOOL;INPUT" "ARGS;DEPENDS" ${ARGN})
  if(NOT ARG_TOOL OR NOT ARG_INPUT)
    message(FATAL_ERROR
      "obelisk_add_generated_file requires TOOL and INPUT")
  endif()
  set(output "${OBELISK_GENERATED_DIR}/${relative_output}")
  get_filename_component(output_directory "${output}" DIRECTORY)
  add_custom_command(
    OUTPUT "${output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
    COMMAND "$<TARGET_FILE:${ARG_TOOL}>" ${ARG_ARGS} "${ARG_INPUT}"
      --write-if-changed -o "${output}" -d "${output}.d"
    DEPENDS ${ARG_TOOL} "${ARG_INPUT}" ${ARG_DEPENDS}
    DEPFILE "${output}.d"
    VERBATIM
    COMMENT "Generating ${relative_output}")
  set_property(GLOBAL APPEND PROPERTY OBELISK_HOST_GENERATED_FILES "${output}")
endfunction()

function(obelisk_register_generated_file)
  cmake_parse_arguments(ARG "" "NAME;OUTPUT;TOOL;INPUT" "ARGS;DEPENDS" ${ARGN})
  if(NOT ARG_NAME OR NOT ARG_OUTPUT OR NOT ARG_TOOL OR NOT ARG_INPUT)
    message(FATAL_ERROR
      "generated-file manifest entries need NAME, OUTPUT, TOOL, and INPUT")
  endif()
  set(depends)
  foreach(dependency IN LISTS ARG_DEPENDS)
    list(APPEND depends "${OBELISK_SOURCE_DIR}/${dependency}")
  endforeach()
  obelisk_add_generated_file("${ARG_OUTPUT}"
    TOOL "${ARG_TOOL}"
    INPUT "${OBELISK_SOURCE_DIR}/${ARG_INPUT}"
    ARGS ${ARG_ARGS}
    DEPENDS ${depends})
endfunction()

include("${OBELISK_SOURCE_DIR}/cmake/ObeliskGeneratedFiles.cmake")

get_property(_obelisk_generated_files GLOBAL
  PROPERTY OBELISK_HOST_GENERATED_FILES)
add_custom_target(obelisk-generated-files ALL
  DEPENDS ${_obelisk_generated_files})
