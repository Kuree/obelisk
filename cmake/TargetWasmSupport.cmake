# Build-time provisioning for the wasm32 target.
#
# The counterpart to TargetNativeSupport.cmake. That file cross-compiles the
# runtime with clang against a pinned Debian sysroot and stages glibc, the crt
# objects and libc++ so the driver can link a hermetic ELF. None of that
# applies here: Emscripten supplies its own sysroot, startup files and C++
# runtime at link time, so this file only has to produce the precompiled wasm
# runtime archive where the driver expects to find it.
#
# wasm32 rather than wasm64, because no Safari release implements the
# Memory64 proposal. runtime/lib/ABI.cpp carries literal layout tables for both
# pointer widths, so the archive is correct either way; what selects 32 is that
# it is the only width an iPhone will load.

set(_obelisk_source_dir "${PROJECT_SOURCE_DIR}")
if(DEFINED OBELISK_SOURCE_DIR AND NOT OBELISK_SOURCE_DIR STREQUAL "")
  get_filename_component(_obelisk_source_dir "${OBELISK_SOURCE_DIR}" ABSOLUTE)
endif()
set(_obelisk_runtime_source_dir "${_obelisk_source_dir}/runtime")
if(DEFINED OBELISK_TARGET_RUNTIME_SOURCE_DIR AND
   NOT OBELISK_TARGET_RUNTIME_SOURCE_DIR STREQUAL "")
  get_filename_component(_obelisk_runtime_source_dir
    "${OBELISK_TARGET_RUNTIME_SOURCE_DIR}" ABSOLUTE)
endif()

set(OBELISK_TARGET_TRIPLE "wasm32-unknown-emscripten" CACHE STRING
    "wasm code-generation target triple")

# Compile the runtime once while assembling the web toolchain. The browser
# linker then consumes ordinary wasm objects instead of rerunning LLVM over
# the entire runtime for every design.
# Ordinary wasm objects do not need to match the embedded LLVM's bitcode
# version, but they must match Emscripten's ABI. Use Emscripten's compiler so
# exception lowering and the libc++/libunwind archives agree exactly.
set(_obelisk_wasm_cxx "${CMAKE_CXX_COMPILER}")
set(_obelisk_wasm_ar "${CMAKE_AR}")
foreach(tool _obelisk_wasm_cxx _obelisk_wasm_ar)
  if(NOT ${tool} OR NOT EXISTS "${${tool}}")
    message(FATAL_ERROR
      "The wasm toolchain did not provide ${tool}; configure with emcmake so "
      "em++ and emar are selected")
  endif()
endforeach()

# There is no sysroot to provision, but the rest of the build refers to these,
# and the build-graph regression test includes this file expecting the target
# to exist.
add_custom_target(obelisk_target_sysroot)
set(OBELISK_TARGET_SYSROOT "")
set(OBELISK_TARGET_SYSROOT_KEY "emscripten-${OBELISK_TARGET_TRIPLE}")
if(OBELISK_TARGET_SYSROOT_ONLY)
  return()
endif()

set(_obelisk_target_runtime_dir "${CMAKE_BINARY_DIR}/target-runtime")
set(OBELISK_TARGET_RUNTIME_ARCHIVE
    "${_obelisk_target_runtime_dir}/libobelisk_rt.a")

file(GLOB_RECURSE _obelisk_target_runtime_headers CONFIGURE_DEPENDS
  "${_obelisk_runtime_source_dir}/include/*.h"
  "${_obelisk_runtime_source_dir}/lib/*.h")

set(_obelisk_target_runtime_definitions
  -DOBELISK_RT_IGNORE_EXCEPTIONS=1)
if(OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  list(APPEND _obelisk_target_runtime_definitions
    -DOBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
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
set(_obelisk_wasm_flags
  -std=c++17 -O3
  -fno-exceptions
  -matomics -mbulk-memory
  -fvisibility=hidden
  -ffunction-sections -fdata-sections
  "-ffile-prefix-map=${_obelisk_runtime_source_dir}=/obelisk/runtime"
  "-fmacro-prefix-map=${_obelisk_runtime_source_dir}=/obelisk/runtime"
  ${_obelisk_target_runtime_definitions}
  -I "${_obelisk_runtime_source_dir}/include"
  -I "${_obelisk_runtime_source_dir}/lib")

set(_obelisk_target_runtime_common_sources
    ABI Bytecode Containers Coverage DesignBytecode DesignBytecodeImage
    DesignBytecodeIntrinsics DesignBytecodeLogic DesignBytecodeNets
    DesignBytecodeObservers DesignBytecodeRoots DesignDatabase DPI FileIO
    Format ManagedHeap Plusargs Process ProcessAllocation ProcessAOT
    ProcessNativeState ProcessNBA ProcessObservers ProcessSignals ProcessState
    ProcessTransitions ProcessValidation Random RandSolve RandSolveWide Runtime
    Sampled StochasticQueue System VCD VPI)
set(_obelisk_target_runtime_cold_tail_sources
    ScanFormat DynamicScanBytecode ContainerBitstream RecursiveBitstream
    ContainerBitstreamBytecode DPIExport DPIExportBytecode)
set(_obelisk_target_runtime_objects)
foreach(source IN LISTS _obelisk_target_runtime_common_sources
                        _obelisk_target_runtime_cold_tail_sources)
  set(object "${_obelisk_target_runtime_dir}/${source}.o")
  set(source_dependencies
      "${_obelisk_runtime_source_dir}/lib/${source}.cpp")
  if(source STREQUAL "RecursiveBitstream")
    list(APPEND source_dependencies
      "${_obelisk_runtime_source_dir}/lib/ContainerBitstream.cpp")
  endif()
  list(APPEND _obelisk_target_runtime_objects "${object}")
  add_custom_command(
    OUTPUT "${object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${_obelisk_target_runtime_dir}"
    COMMAND "${_obelisk_wasm_cxx}" ${_obelisk_wasm_flags}
      -c "${_obelisk_runtime_source_dir}/lib/${source}.cpp" -o "${object}"
    DEPENDS
      ${source_dependencies}
      ${_obelisk_target_runtime_headers}
    COMMENT "Building wasm32 target runtime ${source}.cpp"
    VERBATIM)
endforeach()

add_custom_command(
  OUTPUT "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
  COMMAND "${_obelisk_wasm_ar}" rcs "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
          ${_obelisk_target_runtime_objects}
  DEPENDS ${_obelisk_target_runtime_objects}
  COMMENT "Archiving wasm32 libobelisk_rt.a"
  VERBATIM)
add_custom_target(obelisk_target_runtime
  DEPENDS "${OBELISK_TARGET_RUNTIME_ARCHIVE}")

# Staged where the driver looks for target support, matching the native
# layout so the lookup in tools/driver/NativeBackend.cpp needs no special case.
# Only the precompiled runtime archive is staged: Emscripten owns everything
# else a wasm link needs, and it is on PATH rather than staged into the build
# tree.
set(OBELISK_NATIVE_SUPPORT_DIR
    "${CMAKE_BINARY_DIR}/lib/obelisk/targets/${OBELISK_TARGET_TRIPLE}")
set(OBELISK_NATIVE_SUPPORT_STAMP
    "${CMAKE_BINARY_DIR}/wasm-support-${OBELISK_TARGET_TRIPLE}.complete")
add_custom_command(
  OUTPUT "${OBELISK_NATIVE_SUPPORT_STAMP}"
  BYPRODUCTS
    "${OBELISK_NATIVE_SUPPORT_DIR}/libobelisk_rt.a"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${OBELISK_NATIVE_SUPPORT_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
          "${OBELISK_NATIVE_SUPPORT_DIR}/libobelisk_rt.a"
  COMMAND "${CMAKE_COMMAND}" -E touch "${OBELISK_NATIVE_SUPPORT_STAMP}"
  DEPENDS "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
  COMMENT "Staging wasm32 target-link support"
  VERBATIM)
add_custom_target(obelisk_native_support
  DEPENDS "${OBELISK_NATIVE_SUPPORT_STAMP}")
add_dependencies(obelisk_native_support obelisk_target_runtime)

message(STATUS "wasm target triple: ${OBELISK_TARGET_TRIPLE}")
message(STATUS "wasm target runtime: ${_obelisk_target_runtime_dir}")
