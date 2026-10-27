# Build-time provisioning for the wasm32 target.
#
# The counterpart to TargetNativeSupport.cmake. Emscripten supplies its own
# sysroot, startup files and C++ runtime at link time, so this file only has to
# produce the precompiled wasm runtime archive where the driver expects it.
#
# wasm32 rather than wasm64, because no Safari release implements the
# Memory64 proposal. runtime/lib/ABI.cpp carries literal layout tables for both
# pointer widths, so the archive is correct either way; what selects 32 is that
# it is the only width an iPhone will load.

set(_talos_source_dir "${PROJECT_SOURCE_DIR}")
if(DEFINED TALOS_SOURCE_DIR AND NOT TALOS_SOURCE_DIR STREQUAL "")
  get_filename_component(_talos_source_dir "${TALOS_SOURCE_DIR}" ABSOLUTE)
endif()
set(_talos_runtime_source_dir "${_talos_source_dir}/runtime")
if(DEFINED TALOS_TARGET_RUNTIME_SOURCE_DIR AND
   NOT TALOS_TARGET_RUNTIME_SOURCE_DIR STREQUAL "")
  get_filename_component(_talos_runtime_source_dir
    "${TALOS_TARGET_RUNTIME_SOURCE_DIR}" ABSOLUTE)
endif()

set(TALOS_TARGET_TRIPLE "wasm32-unknown-emscripten" CACHE STRING
    "wasm code-generation target triple")

# Compile the runtime once while assembling the web toolchain. The browser
# linker then consumes ordinary wasm objects instead of rerunning LLVM over
# the entire runtime for every design.
# Ordinary wasm objects do not need to match the embedded LLVM's bitcode
# version, but they must match Emscripten's ABI. Use Emscripten's compiler so
# exception lowering and the libc++/libunwind archives agree exactly.
set(_talos_wasm_cxx "${CMAKE_CXX_COMPILER}")
set(_talos_wasm_ar "${CMAKE_AR}")
foreach(tool _talos_wasm_cxx _talos_wasm_ar)
  if(NOT ${tool} OR NOT EXISTS "${${tool}}")
    message(FATAL_ERROR
      "The wasm toolchain did not provide ${tool}; configure with emcmake so "
      "em++ and emar are selected")
  endif()
endforeach()

set(_talos_target_runtime_dir "${CMAKE_BINARY_DIR}/target-runtime")
set(TALOS_TARGET_RUNTIME_ARCHIVE
    "${_talos_target_runtime_dir}/libtalos_rt.a")
set(_talos_vpi_include_dir
    "${TALOS_SLANG_SOURCE_DIR}/external/ieee1800")

file(GLOB_RECURSE _talos_target_runtime_headers CONFIGURE_DEPENDS
  "${_talos_runtime_source_dir}/include/*.h"
  "${_talos_runtime_source_dir}/lib/*.h"
  "${_talos_vpi_include_dir}/*.h")
set(_talos_target_reflection_include_dir "${CMAKE_BINARY_DIR}/include")
if(DEFINED TALOS_TARGET_REFLECTION_INCLUDE_DIR AND
   NOT TALOS_TARGET_REFLECTION_INCLUDE_DIR STREQUAL "")
  get_filename_component(_talos_target_reflection_include_dir
    "${TALOS_TARGET_REFLECTION_INCLUDE_DIR}" ABSOLUTE)
endif()
set(_talos_target_reflection_headers
  "${_talos_source_dir}/include/talos/Reflection/DesignReflection.h"
  "${_talos_source_dir}/include/talos/Reflection/VPIObjectModel.h"
  "${_talos_target_reflection_include_dir}/talos/Reflection/DesignReflectionLayout.h.inc"
  "${_talos_target_reflection_include_dir}/talos/Reflection/VPIObjectModel.h.inc")
set(_talos_target_coverage_headers
  "${_talos_source_dir}/include/talos/Coverage/CoverageDatabase.h"
  "${_talos_target_reflection_include_dir}/talos/Coverage/CoverageFormatDecls.h.inc"
  "${_talos_target_reflection_include_dir}/talos/Coverage/CoverageFormatParser.h.inc"
  "${_talos_target_reflection_include_dir}/talos/Coverage/CoverageFormatSerializer.h.inc")

set(_talos_target_runtime_definitions
  -DTALOS_RT_IGNORE_EXCEPTIONS=1)
if(TALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  list(APPEND _talos_target_runtime_definitions
    -DTALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()

# Build the wasm runtime without exception support while retaining the
# source-level guards used by native builds at C ABI boundaries. The runtime's
# portability layer compiles those guards away under this strict mode, and the
# explicit definition above selects fatal handling for locally detected
# allocation overflow. Native builds continue to translate allocation and
# library failures into the public status codes documented by the runtime API.
#
# +atomics,+bulk-memory are required for the archive to link against an
# emscripten runtime built with shared memory; without them wasm-ld rejects
# the objects outright. They are harmless in a single-threaded link.
set(_talos_wasm_flags
  -std=c++17 -O3
  -fno-exceptions
  -matomics -mbulk-memory
  -fvisibility=hidden
  -ffunction-sections -fdata-sections
  "-ffile-prefix-map=${_talos_runtime_source_dir}=/talos/runtime"
  "-fmacro-prefix-map=${_talos_runtime_source_dir}=/talos/runtime"
  ${_talos_target_runtime_definitions}
  -I "${_talos_source_dir}/include"
  -I "${_talos_target_reflection_include_dir}"
  -I "${_talos_runtime_source_dir}/include"
  -I "${_talos_runtime_source_dir}/lib"
  -I "${_talos_vpi_include_dir}")

set(_talos_target_runtime_common_sources
    ABI Bytecode Containers Coverage CoverageBlockEvents CoverageDatabase DesignBytecode DesignBytecodeImage
    DesignBytecodeIntrinsics DesignBytecodeLogic DesignBytecodeNets
    DesignBytecodeObservers DesignBytecodeRoots DesignDatabase DPI FileIO
    Format ManagedHeap Plusargs Process ProcessAllocation ProcessAOT
    ProcessNativeState ProcessNBA ProcessObservers ProcessSignals ProcessState
    ProcessTable ProcessTransitions ProcessValidation Random RandSolve RandSamplePlan RandReal RandSolveWide Runtime
    Sampled StochasticQueue System VCD VPI VPIValueLayout)
set(_talos_target_runtime_cold_tail_sources
    ScanFormat DynamicScanBytecode ContainerBitstream RecursiveBitstream
    ContainerBitstreamBytecode ClassBitstream ClassBitstreamBytecode DPIExport
    DPIExportBytecode)
set(_talos_target_runtime_check_sources
    DesignReflectionABI VPIObjectModelABI)
set(_talos_target_runtime_objects)
set(_talos_target_runtime_check_objects)
foreach(source IN LISTS _talos_target_runtime_common_sources
                        _talos_target_runtime_cold_tail_sources
                        _talos_target_runtime_check_sources)
  set(object "${_talos_target_runtime_dir}/${source}.o")
  set(source_dependencies
      "${_talos_runtime_source_dir}/lib/${source}.cpp")
  if(source STREQUAL "CoverageDatabase")
    list(APPEND source_dependencies
      "${_talos_source_dir}/lib/Coverage/CoverageFileSupport.h"
      "${_talos_source_dir}/lib/Coverage/CoverageDatabase.cpp")
  endif()
  if(source STREQUAL "RecursiveBitstream")
    list(APPEND source_dependencies
      "${_talos_runtime_source_dir}/lib/ContainerBitstream.cpp")
  endif()
  if(source STREQUAL "DesignDatabase" OR source STREQUAL "VPI" OR
     source IN_LIST _talos_target_runtime_check_sources)
    list(APPEND source_dependencies ${_talos_target_reflection_headers})
  endif()
  if(source IN_LIST _talos_target_runtime_check_sources)
    list(APPEND _talos_target_runtime_check_objects "${object}")
  else()
    list(APPEND _talos_target_runtime_objects "${object}")
  endif()
  add_custom_command(
    OUTPUT "${object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${_talos_target_runtime_dir}"
    COMMAND ${CMAKE_CXX_COMPILER_LAUNCHER}
      "${_talos_wasm_cxx}" ${_talos_wasm_flags}
      -c "${_talos_runtime_source_dir}/lib/${source}.cpp" -o "${object}"
    DEPENDS
      ${source_dependencies}
      ${_talos_target_runtime_headers}
      ${_talos_target_coverage_headers}
    COMMENT "Building wasm32 target runtime ${source}.cpp"
    VERBATIM)
endforeach()

add_custom_command(
  OUTPUT "${TALOS_TARGET_RUNTIME_ARCHIVE}"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${TALOS_TARGET_RUNTIME_ARCHIVE}"
  COMMAND "${_talos_wasm_ar}" rcs "${TALOS_TARGET_RUNTIME_ARCHIVE}"
          ${_talos_target_runtime_objects}
  DEPENDS ${_talos_target_runtime_objects}
  COMMENT "Archiving wasm32 libtalos_rt.a"
  VERBATIM)
add_custom_target(talos_target_runtime
  DEPENDS "${TALOS_TARGET_RUNTIME_ARCHIVE}")
add_custom_target(talos_target_runtime_abi_checks
  DEPENDS ${_talos_target_runtime_check_objects})
add_dependencies(talos_target_runtime talos_target_runtime_abi_checks)

# Staged where the driver looks for target support, matching the native
# layout so the lookup in tools/driver/NativeBackend.cpp needs no special case.
# Only the precompiled runtime archive is staged: Emscripten owns everything
# else a wasm link needs, and it is on PATH rather than staged into the build
# tree.
set(TALOS_NATIVE_SUPPORT_DIR
    "${CMAKE_BINARY_DIR}/lib/talos/targets/${TALOS_TARGET_TRIPLE}")
set(TALOS_NATIVE_SUPPORT_STAMP
    "${CMAKE_BINARY_DIR}/wasm-support-${TALOS_TARGET_TRIPLE}.complete")
add_custom_command(
  OUTPUT "${TALOS_NATIVE_SUPPORT_STAMP}"
  BYPRODUCTS
    "${TALOS_NATIVE_SUPPORT_DIR}/libtalos_rt.a"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${TALOS_NATIVE_SUPPORT_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${TALOS_TARGET_RUNTIME_ARCHIVE}"
          "${TALOS_NATIVE_SUPPORT_DIR}/libtalos_rt.a"
  COMMAND "${CMAKE_COMMAND}" -E touch "${TALOS_NATIVE_SUPPORT_STAMP}"
  DEPENDS "${TALOS_TARGET_RUNTIME_ARCHIVE}"
  COMMENT "Staging wasm32 target-link support"
  VERBATIM)
add_custom_target(talos_native_support
  DEPENDS "${TALOS_NATIVE_SUPPORT_STAMP}")
add_dependencies(talos_native_support talos_target_runtime)

message(STATUS "wasm target triple: ${TALOS_TARGET_TRIPLE}")
message(STATUS "wasm target runtime: ${_talos_target_runtime_dir}")
