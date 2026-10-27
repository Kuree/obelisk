# Build-time support for the host-native Linux target.

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

set(_talos_target_triple_explicit FALSE)
if(DEFINED TALOS_TARGET_TRIPLE AND NOT TALOS_TARGET_TRIPLE STREQUAL "")
  set(_talos_target_triple_explicit TRUE)
endif()

if(NOT _talos_target_triple_explicit)
  file(GLOB _talos_clang_runtime_dirs LIST_DIRECTORIES TRUE
    "${TALOS_LLVM_DIST_DIR}/lib/clang/${LLVM_VERSION_MAJOR}/lib/*")
  set(_talos_target_candidates)
  foreach(runtime_dir IN LISTS _talos_clang_runtime_dirs)
    get_filename_component(candidate "${runtime_dir}" NAME)
    if(EXISTS "${runtime_dir}/libclang_rt.builtins.a" AND
       EXISTS "${TALOS_LLVM_DIST_DIR}/lib/${candidate}/libc++.a")
      list(APPEND _talos_target_candidates "${candidate}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _talos_target_candidates)
  list(SORT _talos_target_candidates)
  set(_talos_all_target_candidates ${_talos_target_candidates})
  list(LENGTH _talos_target_candidates _talos_candidate_count)
  if(_talos_candidate_count EQUAL 0)
    message(FATAL_ERROR
      "The LLVM distribution has no complete native runtime triple; set "
      "TALOS_TARGET_TRIPLE explicitly")
  elseif(_talos_candidate_count EQUAL 1)
    list(GET _talos_target_candidates 0 TALOS_TARGET_TRIPLE)
  else()
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _talos_host_arch)
    if(_talos_host_arch MATCHES "^(x86_64|amd64)$")
      set(_talos_arch_pattern "^(x86_64|amd64)-")
    elseif(_talos_host_arch MATCHES "^(aarch64|arm64)$")
      set(_talos_arch_pattern "^(aarch64|arm64)-")
    else()
      set(_talos_arch_pattern "^${_talos_host_arch}-")
    endif()
    list(FILTER _talos_target_candidates INCLUDE REGEX "${_talos_arch_pattern}")
    list(LENGTH _talos_target_candidates _talos_candidate_count)
    if(NOT _talos_candidate_count EQUAL 1)
      string(JOIN ", " _talos_candidate_list
        ${_talos_all_target_candidates})
      message(FATAL_ERROR
        "The LLVM distribution has ambiguous native runtime triples: "
        "${_talos_candidate_list}; set TALOS_TARGET_TRIPLE explicitly")
    endif()
    list(GET _talos_target_candidates 0 TALOS_TARGET_TRIPLE)
  endif()
endif()
set(TALOS_TARGET_TRIPLE "${TALOS_TARGET_TRIPLE}" CACHE STRING
    "Native code-generation target triple")

if(NOT _talos_target_triple_explicit)
  string(REGEX MATCH "^[^-]+" _talos_target_arch "${TALOS_TARGET_TRIPLE}")
  string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _talos_host_arch)
  string(TOLOWER "${_talos_target_arch}" _talos_target_arch)
  if((_talos_host_arch MATCHES "^(x86_64|amd64)$" AND
      NOT _talos_target_arch MATCHES "^(x86_64|amd64)$") OR
     (_talos_host_arch MATCHES "^(aarch64|arm64)$" AND
      NOT _talos_target_arch MATCHES "^(aarch64|arm64)$") OR
     (NOT _talos_host_arch MATCHES "^(x86_64|amd64|aarch64|arm64)$" AND
      NOT _talos_target_arch STREQUAL _talos_host_arch))
    message(FATAL_ERROR
      "LLVM native runtime triple ${TALOS_TARGET_TRIPLE} does not match "
      "host processor ${CMAKE_HOST_SYSTEM_PROCESSOR}")
  endif()
endif()

set(TALOS_NATIVE_SUPPORT_LAYOUT_VERSION "3" CACHE INTERNAL
    "Internal native-support staging layout version" FORCE)
string(SHA256 TALOS_NATIVE_SUPPORT_KEY
  "layout=${TALOS_NATIVE_SUPPORT_LAYOUT_VERSION};target=${TALOS_TARGET_TRIPLE};llvm=${TALOS_LLVM_VERSION}")

foreach(tool clang++ ld.lld llvm-ar llvm-ranlib)
  if(NOT EXISTS "${TALOS_LLVM_DIST_DIR}/bin/${tool}")
    message(FATAL_ERROR
      "The pinned LLVM distribution is missing target-runtime tool: "
      "${TALOS_LLVM_DIST_DIR}/bin/${tool}")
  endif()
endforeach()

# Fail at configure time so missing host libc headers cannot silently skip the
# native link-and-run coverage.
set(_talos_preflight_key
  "${TALOS_TARGET_TRIPLE}-${TALOS_LLVM_VERSION}-${TALOS_NATIVE_SUPPORT_LAYOUT_VERSION}")
if(NOT "${TALOS_HOST_C_RUNTIME_PREFLIGHT_KEY}" STREQUAL
       "${_talos_preflight_key}")
  set(_talos_preflight_source "${CMAKE_BINARY_DIR}/talos-host-c-runtime-probe.cpp")
  file(WRITE "${_talos_preflight_source}"
    "#include <stdlib.h>\n#include <pthread.h>\nint main() { return 0; }\n")
  execute_process(
    COMMAND "${TALOS_LLVM_DIST_DIR}/bin/clang++"
      --target=${TALOS_TARGET_TRIPLE}
      -std=c++17 -fsyntax-only -nostdinc++
      -isystem "${TALOS_LLVM_DIST_DIR}/include/${TALOS_TARGET_TRIPLE}/c++/v1"
      -isystem "${TALOS_LLVM_DIST_DIR}/include/c++/v1"
      -isystem "${TALOS_LLVM_DIST_DIR}/lib/clang/${LLVM_VERSION_MAJOR}/include"
      "${_talos_preflight_source}"
    RESULT_VARIABLE _talos_preflight_result
    OUTPUT_VARIABLE _talos_preflight_stdout
    ERROR_VARIABLE _talos_preflight_stderr)
  if(NOT _talos_preflight_result EQUAL 0)
    message(FATAL_ERROR
      "Native target requires the host distribution's libc development "
      "files (common package names: libc6-dev, glibc-devel, or glibc; "
      "musl development files are not a glibc substitute).\n"
      "${_talos_preflight_stderr}")
  endif()
  set(TALOS_HOST_C_RUNTIME_PREFLIGHT_KEY "${_talos_preflight_key}"
    CACHE INTERNAL "Successful host C-runtime preflight key" FORCE)
endif()

set(_talos_target_runtime_dir "${CMAKE_BINARY_DIR}/target-runtime")
set(TALOS_COMPILER_RT_BUILTINS
    "${TALOS_LLVM_DIST_DIR}/lib/clang/${LLVM_VERSION_MAJOR}/lib/${TALOS_TARGET_TRIPLE}/libclang_rt.builtins.a")
if(NOT EXISTS "${TALOS_COMPILER_RT_BUILTINS}")
  message(FATAL_ERROR "The SDK is missing ${TALOS_COMPILER_RT_BUILTINS}")
endif()
set(TALOS_TARGET_RUNTIME_ARCHIVE
    "${_talos_target_runtime_dir}/libtalos_rt.a")
set(TALOS_TARGET_RUNTIME_LTO_ARCHIVE
    "${_talos_target_runtime_dir}/libtalos_rt_lto.a")
set(TALOS_TARGET_RUNTIME_PRELINKED_OBJECT
    "${_talos_target_runtime_dir}/talos_rt_prelinked.o")
set(TALOS_TARGET_RUNTIME_PRELINKED_ARCHIVE
    "${_talos_target_runtime_dir}/libtalos_rt_prelinked.a")
set(_talos_target_runtime_vpi_include_dir
    "${TALOS_SLANG_SOURCE_DIR}/external/ieee1800")
set(_talos_target_runtime_vpi_headers
    "${_talos_target_runtime_vpi_include_dir}/vpi_user.h"
    "${_talos_target_runtime_vpi_include_dir}/sv_vpi_user.h"
    "${_talos_target_runtime_vpi_include_dir}/vpi_compatibility.h")
set(_talos_target_reflection_include_dir "${CMAKE_BINARY_DIR}/include")
if(DEFINED TALOS_TARGET_REFLECTION_INCLUDE_DIR AND
   NOT TALOS_TARGET_REFLECTION_INCLUDE_DIR STREQUAL "")
  get_filename_component(_talos_target_reflection_include_dir
    "${TALOS_TARGET_REFLECTION_INCLUDE_DIR}" ABSOLUTE)
endif()
set(_talos_target_runtime_reflection_headers
    "${_talos_source_dir}/include/talos/Reflection/DesignReflection.h"
    "${_talos_target_reflection_include_dir}/talos/Reflection/DesignReflectionLayout.h.inc"
    "${_talos_source_dir}/include/talos/Reflection/VPIObjectModel.h"
    "${_talos_target_reflection_include_dir}/talos/Reflection/VPIObjectModel.h.inc")
set(_talos_target_runtime_coverage_headers
    "${_talos_source_dir}/include/talos/Coverage/CoverageDatabase.h"
    "${_talos_target_reflection_include_dir}/talos/Coverage/CoverageFormatDecls.h.inc"
    "${_talos_target_reflection_include_dir}/talos/Coverage/CoverageFormatParser.h.inc"
    "${_talos_target_reflection_include_dir}/talos/Coverage/CoverageFormatSerializer.h.inc")
set(_talos_target_runtime_objects)
set(_talos_target_runtime_lto_objects)
set(_talos_target_runtime_definitions)
if(TALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  list(APPEND _talos_target_runtime_definitions
    -DTALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()
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
    DPIExportBytecode DPIOpenArray DPIAggregate)
foreach(source IN LISTS _talos_target_runtime_common_sources
                        _talos_target_runtime_cold_tail_sources)
  set(object "${_talos_target_runtime_dir}/${source}.o")
  set(lto_object "${_talos_target_runtime_dir}/${source}.bc")
  set(source_dependencies
      "${_talos_runtime_source_dir}/lib/${source}.cpp")
  if(source STREQUAL "CoverageDatabase")
    list(APPEND source_dependencies
      "${_talos_source_dir}/lib/Coverage/CoverageFileSupport.h"
      "${_talos_source_dir}/lib/Coverage/CoverageIndex.h"
      "${_talos_source_dir}/lib/Coverage/CoverageDatabase.cpp")
  endif()
  if(source STREQUAL "RecursiveBitstream")
    list(APPEND source_dependencies
      "${_talos_runtime_source_dir}/lib/ContainerBitstream.cpp")
  endif()
  if(source STREQUAL "DesignDatabase" OR source STREQUAL "VPI")
    list(APPEND source_dependencies
      ${_talos_target_runtime_reflection_headers})
  endif()
  list(APPEND _talos_target_runtime_objects "${object}")
  list(APPEND _talos_target_runtime_lto_objects "${lto_object}")
  # Track actual includes instead of rebuilding the entire runtime for every
  # private-header edit. Separate native and LTO edges can run independently.
  foreach(output IN ITEMS "${object}" "${lto_object}")
    set(lto_flags)
    if(output STREQUAL lto_object)
      set(lto_flags -flto=full -funified-lto)
    endif()
    get_filename_component(output_name "${output}" NAME)
    add_custom_command(
      OUTPUT "${output}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
              "${_talos_target_runtime_dir}"
      COMMAND ${CMAKE_CXX_COMPILER_LAUNCHER}
        "${TALOS_LLVM_DIST_DIR}/bin/clang++"
        --target=${TALOS_TARGET_TRIPLE}
        -std=c++17 -O3 ${lto_flags} -fPIC -fvisibility=hidden
        ${_talos_target_runtime_definitions}
        -ffunction-sections -fdata-sections
        "-ffile-prefix-map=${_talos_runtime_source_dir}=/talos/runtime"
        "-fmacro-prefix-map=${_talos_runtime_source_dir}=/talos/runtime"
        -nostdinc++
        -isystem "${TALOS_LLVM_DIST_DIR}/include/${TALOS_TARGET_TRIPLE}/c++/v1"
        -isystem "${TALOS_LLVM_DIST_DIR}/include/c++/v1"
        -isystem "${TALOS_LLVM_DIST_DIR}/lib/clang/${LLVM_VERSION_MAJOR}/include"
        -I "${_talos_runtime_source_dir}/include"
        -I "${_talos_runtime_source_dir}/lib"
        -I "${_talos_source_dir}/include"
        -I "${_talos_target_reflection_include_dir}"
        -isystem "${_talos_target_runtime_vpi_include_dir}"
        -MD -MF "${output}.d" -MQ "${output}"
        -c "${_talos_runtime_source_dir}/lib/${source}.cpp" -o "${output}"
      DEPFILE "${output}.d"
      DEPENDS
        ${source_dependencies}
        ${_talos_target_runtime_coverage_headers}
        ${_talos_target_runtime_vpi_headers}
      COMMENT "Building target runtime ${output_name}"
      VERBATIM)
  endforeach()
endforeach()
add_custom_command(
  OUTPUT "${TALOS_TARGET_RUNTIME_ARCHIVE}"
  COMMAND "${CMAKE_COMMAND}" -E rm -f
          "${TALOS_TARGET_RUNTIME_ARCHIVE}"
  COMMAND "${TALOS_LLVM_DIST_DIR}/bin/llvm-ar" rcs
          "${TALOS_TARGET_RUNTIME_ARCHIVE}"
          ${_talos_target_runtime_objects}
  COMMAND "${TALOS_LLVM_DIST_DIR}/bin/llvm-ar" qL
          "${TALOS_TARGET_RUNTIME_ARCHIVE}" "${TALOS_COMPILER_RT_BUILTINS}"
  COMMAND "${TALOS_LLVM_DIST_DIR}/bin/llvm-ranlib"
          "${TALOS_TARGET_RUNTIME_ARCHIVE}"
  DEPENDS ${_talos_target_runtime_objects} "${TALOS_COMPILER_RT_BUILTINS}"
  COMMENT "Archiving target libtalos_rt.a with llvm-ar"
  VERBATIM)
add_custom_command(
  OUTPUT "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}"
  COMMAND "${CMAKE_COMMAND}" -E rm -f
          "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}"
  COMMAND "${TALOS_LLVM_DIST_DIR}/bin/llvm-ar" rcs
          "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}"
          ${_talos_target_runtime_lto_objects}
  COMMAND "${TALOS_LLVM_DIST_DIR}/bin/llvm-ar" qL
          "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}" "${TALOS_COMPILER_RT_BUILTINS}"
  COMMAND "${TALOS_LLVM_DIST_DIR}/bin/llvm-ranlib"
          "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}"
  DEPENDS ${_talos_target_runtime_lto_objects} "${TALOS_COMPILER_RT_BUILTINS}"
  COMMENT "Archiving target libtalos_rt_lto.a with llvm-ar"
  VERBATIM)
add_custom_command(
  OUTPUT "${TALOS_TARGET_RUNTIME_PRELINKED_OBJECT}"
  COMMAND "${TALOS_LLVM_DIST_DIR}/bin/ld.lld"
          -r --lto=full --lto-O3 --lto-CGO3
          ${_talos_target_runtime_lto_objects}
          -o "${TALOS_TARGET_RUNTIME_PRELINKED_OBJECT}"
  DEPENDS ${_talos_target_runtime_lto_objects}
  COMMENT "Prelinking the target runtime with Full LTO"
  VERBATIM)
add_custom_command(
  OUTPUT "${TALOS_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
  COMMAND "${CMAKE_COMMAND}" -E rm -f
          "${TALOS_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
  COMMAND "${TALOS_LLVM_DIST_DIR}/bin/llvm-ar" rcs
          "${TALOS_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
          "${TALOS_TARGET_RUNTIME_PRELINKED_OBJECT}"
  COMMAND "${TALOS_LLVM_DIST_DIR}/bin/llvm-ar" qL
          "${TALOS_TARGET_RUNTIME_PRELINKED_ARCHIVE}" "${TALOS_COMPILER_RT_BUILTINS}"
  COMMAND "${TALOS_LLVM_DIST_DIR}/bin/llvm-ranlib"
          "${TALOS_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
  DEPENDS "${TALOS_TARGET_RUNTIME_PRELINKED_OBJECT}" "${TALOS_COMPILER_RT_BUILTINS}"
  COMMENT "Archiving the prelinked target runtime"
  VERBATIM)
add_custom_target(talos_target_runtime
  DEPENDS
    "${TALOS_TARGET_RUNTIME_ARCHIVE}"
    "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}"
    "${TALOS_TARGET_RUNTIME_PRELINKED_ARCHIVE}")
set(TALOS_NATIVE_SUPPORT_DIR
    "${CMAKE_BINARY_DIR}/lib/talos/targets/${TALOS_TARGET_TRIPLE}")
set(TALOS_NATIVE_SUPPORT_STAMP
    "${CMAKE_BINARY_DIR}/native-support-${TALOS_NATIVE_SUPPORT_KEY}.complete")
set(_talos_native_support_byproducts
  "${TALOS_NATIVE_SUPPORT_DIR}/.complete"
  "${TALOS_NATIVE_SUPPORT_DIR}/README.txt"
  "${TALOS_NATIVE_SUPPORT_DIR}/BUILD_PATH_PREFIXES.txt"
  "${TALOS_NATIVE_SUPPORT_DIR}/clang_rt.crtbegin.o"
  "${TALOS_NATIVE_SUPPORT_DIR}/clang_rt.crtend.o"
  "${TALOS_NATIVE_SUPPORT_DIR}/libc++.a"
  "${TALOS_NATIVE_SUPPORT_DIR}/libc++abi.a"
  "${TALOS_NATIVE_SUPPORT_DIR}/libunwind.a"
  "${TALOS_NATIVE_SUPPORT_DIR}/libtalos_rt.a"
  "${TALOS_NATIVE_SUPPORT_DIR}/libtalos_rt_lto.a"
  "${TALOS_NATIVE_SUPPORT_DIR}/libtalos_rt_prelinked.a"
  "${TALOS_NATIVE_SUPPORT_DIR}/licenses/talos/LICENSE"
  "${TALOS_NATIVE_SUPPORT_DIR}/licenses/llvm/LICENSE.TXT"
  "${TALOS_NATIVE_SUPPORT_DIR}/licenses/llvm/Apache-2.0.txt"
  "${TALOS_NATIVE_SUPPORT_DIR}/licenses/llvm/LLVM-exception.txt")
list(APPEND _talos_native_support_byproducts
  "${TALOS_NATIVE_SUPPORT_DIR}/licenses/compiler-rt/LICENSE.TXT"
  "${TALOS_NATIVE_SUPPORT_DIR}/licenses/compiler-rt/CREDITS.TXT")
add_custom_command(
  OUTPUT "${TALOS_NATIVE_SUPPORT_STAMP}"
  BYPRODUCTS ${_talos_native_support_byproducts}
  COMMAND "${CMAKE_COMMAND}"
    "-DSTAMP=${TALOS_NATIVE_SUPPORT_STAMP}"
    "-DDESTINATION=${TALOS_NATIVE_SUPPORT_DIR}"
    "-DRUNTIME_ARCHIVE=${TALOS_TARGET_RUNTIME_ARCHIVE}"
    "-DRUNTIME_LTO_ARCHIVE=${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}"
    "-DRUNTIME_PRELINKED_ARCHIVE=${TALOS_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
    "-DLLVM_DIST=${TALOS_LLVM_DIST_DIR}"
    "-DSOURCE_DIR=${_talos_source_dir}"
    "-DSTAGE_KEY=${TALOS_NATIVE_SUPPORT_KEY}"
    "-DTARGET_TRIPLE=${TALOS_TARGET_TRIPLE}"
    "-DLLVM_VERSION_MAJOR=${LLVM_VERSION_MAJOR}"
    -P "${_talos_source_dir}/cmake/StageNativeSupport.cmake"
  DEPENDS
    "${TALOS_TARGET_RUNTIME_ARCHIVE}"
    "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}"
    "${TALOS_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
    "${_talos_source_dir}/cmake/StageNativeSupport.cmake"
    "${_talos_source_dir}/LICENSE"
    "${_talos_source_dir}/docs/third-party/licenses/Apache-2.0.txt"
    "${_talos_source_dir}/docs/third-party/licenses/LLVM-exception.txt"
    "${_talos_source_dir}/docs/third-party/licenses/compiler-rt-LICENSE.TXT"
    "${_talos_source_dir}/docs/third-party/licenses/compiler-rt-CREDITS.TXT"
  COMMENT "Staging local Talos native-link support"
  VERBATIM)
add_custom_target(talos_native_support
  DEPENDS "${TALOS_NATIVE_SUPPORT_STAMP}")
add_dependencies(talos_native_support talos_target_runtime)

message(STATUS "Native target triple: ${TALOS_TARGET_TRIPLE}")
message(STATUS "Native support key: ${TALOS_NATIVE_SUPPORT_KEY}")
