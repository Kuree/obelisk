# Build-time support for the host-native Linux target.

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

set(_obelisk_target_triple_explicit FALSE)
if(DEFINED OBELISK_TARGET_TRIPLE AND NOT OBELISK_TARGET_TRIPLE STREQUAL "")
  set(_obelisk_target_triple_explicit TRUE)
endif()

if(NOT _obelisk_target_triple_explicit)
  file(GLOB _obelisk_clang_runtime_dirs LIST_DIRECTORIES TRUE
    "${OBELISK_LLVM_DIST_DIR}/lib/clang/${LLVM_VERSION_MAJOR}/lib/*")
  set(_obelisk_target_candidates)
  foreach(runtime_dir IN LISTS _obelisk_clang_runtime_dirs)
    get_filename_component(candidate "${runtime_dir}" NAME)
    if(EXISTS "${runtime_dir}/libclang_rt.builtins.a" AND
       EXISTS "${OBELISK_LLVM_DIST_DIR}/lib/${candidate}/libc++.a")
      list(APPEND _obelisk_target_candidates "${candidate}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _obelisk_target_candidates)
  list(SORT _obelisk_target_candidates)
  set(_obelisk_all_target_candidates ${_obelisk_target_candidates})
  list(LENGTH _obelisk_target_candidates _obelisk_candidate_count)
  if(_obelisk_candidate_count EQUAL 0)
    message(FATAL_ERROR
      "The LLVM distribution has no complete native runtime triple; set "
      "OBELISK_TARGET_TRIPLE explicitly")
  elseif(_obelisk_candidate_count EQUAL 1)
    list(GET _obelisk_target_candidates 0 OBELISK_TARGET_TRIPLE)
  else()
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _obelisk_host_arch)
    if(_obelisk_host_arch MATCHES "^(x86_64|amd64)$")
      set(_obelisk_arch_pattern "^(x86_64|amd64)-")
    elseif(_obelisk_host_arch MATCHES "^(aarch64|arm64)$")
      set(_obelisk_arch_pattern "^(aarch64|arm64)-")
    else()
      set(_obelisk_arch_pattern "^${_obelisk_host_arch}-")
    endif()
    list(FILTER _obelisk_target_candidates INCLUDE REGEX "${_obelisk_arch_pattern}")
    list(LENGTH _obelisk_target_candidates _obelisk_candidate_count)
    if(NOT _obelisk_candidate_count EQUAL 1)
      string(JOIN ", " _obelisk_candidate_list
        ${_obelisk_all_target_candidates})
      message(FATAL_ERROR
        "The LLVM distribution has ambiguous native runtime triples: "
        "${_obelisk_candidate_list}; set OBELISK_TARGET_TRIPLE explicitly")
    endif()
    list(GET _obelisk_target_candidates 0 OBELISK_TARGET_TRIPLE)
  endif()
endif()
set(OBELISK_TARGET_TRIPLE "${OBELISK_TARGET_TRIPLE}" CACHE STRING
    "Native code-generation target triple")

if(NOT _obelisk_target_triple_explicit)
  string(REGEX MATCH "^[^-]+" _obelisk_target_arch "${OBELISK_TARGET_TRIPLE}")
  string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _obelisk_host_arch)
  string(TOLOWER "${_obelisk_target_arch}" _obelisk_target_arch)
  if((_obelisk_host_arch MATCHES "^(x86_64|amd64)$" AND
      NOT _obelisk_target_arch MATCHES "^(x86_64|amd64)$") OR
     (_obelisk_host_arch MATCHES "^(aarch64|arm64)$" AND
      NOT _obelisk_target_arch MATCHES "^(aarch64|arm64)$") OR
     (NOT _obelisk_host_arch MATCHES "^(x86_64|amd64|aarch64|arm64)$" AND
      NOT _obelisk_target_arch STREQUAL _obelisk_host_arch))
    message(FATAL_ERROR
      "LLVM native runtime triple ${OBELISK_TARGET_TRIPLE} does not match "
      "host processor ${CMAKE_HOST_SYSTEM_PROCESSOR}")
  endif()
endif()

set(OBELISK_NATIVE_SUPPORT_LAYOUT_VERSION "2" CACHE STRING
    "Internal native-support staging layout version")
string(SHA256 OBELISK_NATIVE_SUPPORT_KEY
  "layout=${OBELISK_NATIVE_SUPPORT_LAYOUT_VERSION};target=${OBELISK_TARGET_TRIPLE};llvm=${OBELISK_LLVM_VERSION}")

foreach(tool clang++ ld.lld llvm-ar llvm-ranlib)
  if(NOT EXISTS "${OBELISK_LLVM_DIST_DIR}/bin/${tool}")
    message(FATAL_ERROR
      "The pinned LLVM distribution is missing target-runtime tool: "
      "${OBELISK_LLVM_DIST_DIR}/bin/${tool}")
  endif()
endforeach()

# Fail at configure time so missing host libc headers cannot silently skip the
# native link-and-run coverage.
set(_obelisk_preflight_key
  "${OBELISK_TARGET_TRIPLE}-${OBELISK_LLVM_VERSION}-${OBELISK_NATIVE_SUPPORT_LAYOUT_VERSION}")
if(NOT "${OBELISK_HOST_C_RUNTIME_PREFLIGHT_KEY}" STREQUAL
       "${_obelisk_preflight_key}")
  set(_obelisk_preflight_source "${CMAKE_BINARY_DIR}/obelisk-host-c-runtime-probe.cpp")
  file(WRITE "${_obelisk_preflight_source}"
    "#include <stdlib.h>\n#include <pthread.h>\nint main() { return 0; }\n")
  execute_process(
    COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/clang++"
      --target=${OBELISK_TARGET_TRIPLE}
      -std=c++17 -fsyntax-only -nostdinc++
      -isystem "${OBELISK_LLVM_DIST_DIR}/include/${OBELISK_TARGET_TRIPLE}/c++/v1"
      -isystem "${OBELISK_LLVM_DIST_DIR}/include/c++/v1"
      -isystem "${OBELISK_LLVM_DIST_DIR}/lib/clang/${LLVM_VERSION_MAJOR}/include"
      "${_obelisk_preflight_source}"
    RESULT_VARIABLE _obelisk_preflight_result
    OUTPUT_VARIABLE _obelisk_preflight_stdout
    ERROR_VARIABLE _obelisk_preflight_stderr)
  if(NOT _obelisk_preflight_result EQUAL 0)
    message(FATAL_ERROR
      "Native target requires the host distribution's libc development "
      "files (common package names: libc6-dev, glibc-devel, or glibc; "
      "musl development files are not a glibc substitute).\n"
      "${_obelisk_preflight_stderr}")
  endif()
  set(OBELISK_HOST_C_RUNTIME_PREFLIGHT_KEY "${_obelisk_preflight_key}"
    CACHE INTERNAL "Successful host C-runtime preflight key" FORCE)
endif()

set(_obelisk_target_runtime_dir "${CMAKE_BINARY_DIR}/target-runtime")
set(OBELISK_TARGET_RUNTIME_ARCHIVE
    "${_obelisk_target_runtime_dir}/libobelisk_rt.a")
set(OBELISK_TARGET_RUNTIME_LTO_ARCHIVE
    "${_obelisk_target_runtime_dir}/libobelisk_rt_lto.a")
set(OBELISK_TARGET_RUNTIME_PRELINKED_OBJECT
    "${_obelisk_target_runtime_dir}/obelisk_rt_prelinked.o")
set(OBELISK_TARGET_RUNTIME_PRELINKED_ARCHIVE
    "${_obelisk_target_runtime_dir}/libobelisk_rt_prelinked.a")
file(GLOB_RECURSE _obelisk_target_runtime_headers CONFIGURE_DEPENDS
  "${_obelisk_runtime_source_dir}/include/*.h"
  "${_obelisk_runtime_source_dir}/lib/*.h")
set(_obelisk_target_runtime_objects)
set(_obelisk_target_runtime_lto_objects)
set(_obelisk_target_runtime_definitions)
if(OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  list(APPEND _obelisk_target_runtime_definitions
    -DOBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()
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
    ContainerBitstreamBytecode ClassBitstream ClassBitstreamBytecode DPIExport
    DPIExportBytecode DPIOpenArray DPIAggregate)
foreach(source IN LISTS _obelisk_target_runtime_common_sources
                        _obelisk_target_runtime_cold_tail_sources)
  set(object "${_obelisk_target_runtime_dir}/${source}.o")
  set(lto_object "${_obelisk_target_runtime_dir}/${source}.bc")
  set(source_dependencies
      "${_obelisk_runtime_source_dir}/lib/${source}.cpp")
  if(source STREQUAL "RecursiveBitstream")
    list(APPEND source_dependencies
      "${_obelisk_runtime_source_dir}/lib/ContainerBitstream.cpp")
  endif()
  list(APPEND _obelisk_target_runtime_objects "${object}")
  list(APPEND _obelisk_target_runtime_lto_objects "${lto_object}")
  add_custom_command(
    OUTPUT "${object}" "${lto_object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${_obelisk_target_runtime_dir}"
    COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/clang++"
      --target=${OBELISK_TARGET_TRIPLE}
      -std=c++17 -O3 -fPIC -fvisibility=hidden
      ${_obelisk_target_runtime_definitions}
      -ffunction-sections -fdata-sections
      "-ffile-prefix-map=${_obelisk_runtime_source_dir}=/obelisk/runtime"
      "-fmacro-prefix-map=${_obelisk_runtime_source_dir}=/obelisk/runtime"
      -nostdinc++
      -isystem "${OBELISK_LLVM_DIST_DIR}/include/${OBELISK_TARGET_TRIPLE}/c++/v1"
      -isystem "${OBELISK_LLVM_DIST_DIR}/include/c++/v1"
      -isystem "${OBELISK_LLVM_DIST_DIR}/lib/clang/${LLVM_VERSION_MAJOR}/include"
      -I "${_obelisk_runtime_source_dir}/include"
      -I "${_obelisk_runtime_source_dir}/lib"
      -c "${_obelisk_runtime_source_dir}/lib/${source}.cpp" -o "${object}"
    COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/clang++"
      --target=${OBELISK_TARGET_TRIPLE}
      -std=c++17 -O3 -flto=full -funified-lto -fPIC -fvisibility=hidden
      ${_obelisk_target_runtime_definitions}
      -ffunction-sections -fdata-sections
      "-ffile-prefix-map=${_obelisk_runtime_source_dir}=/obelisk/runtime"
      "-fmacro-prefix-map=${_obelisk_runtime_source_dir}=/obelisk/runtime"
      -nostdinc++
      -isystem "${OBELISK_LLVM_DIST_DIR}/include/${OBELISK_TARGET_TRIPLE}/c++/v1"
      -isystem "${OBELISK_LLVM_DIST_DIR}/include/c++/v1"
      -isystem "${OBELISK_LLVM_DIST_DIR}/lib/clang/${LLVM_VERSION_MAJOR}/include"
      -I "${_obelisk_runtime_source_dir}/include"
      -I "${_obelisk_runtime_source_dir}/lib"
      -c "${_obelisk_runtime_source_dir}/lib/${source}.cpp" -o "${lto_object}"
    DEPENDS
      ${source_dependencies}
      ${_obelisk_target_runtime_headers}
    COMMENT "Building native and Full-LTO target runtime ${source}.cpp"
    VERBATIM)
endforeach()
add_custom_command(
  OUTPUT "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
  COMMAND "${CMAKE_COMMAND}" -E rm -f
          "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llvm-ar" rcs
          "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
          ${_obelisk_target_runtime_objects}
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llvm-ranlib"
          "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
  DEPENDS ${_obelisk_target_runtime_objects}
  COMMENT "Archiving target libobelisk_rt.a with llvm-ar"
  VERBATIM)
add_custom_command(
  OUTPUT "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
  COMMAND "${CMAKE_COMMAND}" -E rm -f
          "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llvm-ar" rcs
          "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
          ${_obelisk_target_runtime_lto_objects}
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llvm-ranlib"
          "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
  DEPENDS ${_obelisk_target_runtime_lto_objects}
  COMMENT "Archiving target libobelisk_rt_lto.a with llvm-ar"
  VERBATIM)
add_custom_command(
  OUTPUT "${OBELISK_TARGET_RUNTIME_PRELINKED_OBJECT}"
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/ld.lld"
          -r --lto=full --lto-O3 --lto-CGO3
          --whole-archive "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
          --no-whole-archive
          -o "${OBELISK_TARGET_RUNTIME_PRELINKED_OBJECT}"
  DEPENDS "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
  COMMENT "Prelinking the target runtime with Full LTO"
  VERBATIM)
add_custom_command(
  OUTPUT "${OBELISK_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
  COMMAND "${CMAKE_COMMAND}" -E rm -f
          "${OBELISK_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llvm-ar" rcs
          "${OBELISK_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
          "${OBELISK_TARGET_RUNTIME_PRELINKED_OBJECT}"
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llvm-ranlib"
          "${OBELISK_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
  DEPENDS "${OBELISK_TARGET_RUNTIME_PRELINKED_OBJECT}"
  COMMENT "Archiving the prelinked target runtime"
  VERBATIM)
add_custom_target(obelisk_target_runtime
  DEPENDS
    "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
    "${OBELISK_TARGET_RUNTIME_PRELINKED_ARCHIVE}")
set(OBELISK_NATIVE_SUPPORT_DIR
    "${CMAKE_BINARY_DIR}/lib/obelisk/targets/${OBELISK_TARGET_TRIPLE}")
set(OBELISK_NATIVE_SUPPORT_STAMP
    "${CMAKE_BINARY_DIR}/native-support-${OBELISK_NATIVE_SUPPORT_KEY}.complete")
set(_obelisk_native_support_byproducts
  "${OBELISK_NATIVE_SUPPORT_DIR}/.complete"
  "${OBELISK_NATIVE_SUPPORT_DIR}/README.txt"
  "${OBELISK_NATIVE_SUPPORT_DIR}/BUILD_PATH_PREFIXES.txt"
  "${OBELISK_NATIVE_SUPPORT_DIR}/clang_rt.crtbegin.o"
  "${OBELISK_NATIVE_SUPPORT_DIR}/clang_rt.crtend.o"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libclang_rt.builtins.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libc++.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libc++abi.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libunwind.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libobelisk_rt.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libobelisk_rt_lto.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/libobelisk_rt_prelinked.a"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/obelisk/LICENSE"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/llvm/LICENSE.TXT"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/llvm/Apache-2.0.txt"
  "${OBELISK_NATIVE_SUPPORT_DIR}/licenses/llvm/LLVM-exception.txt")
add_custom_command(
  OUTPUT "${OBELISK_NATIVE_SUPPORT_STAMP}"
  BYPRODUCTS ${_obelisk_native_support_byproducts}
  COMMAND "${CMAKE_COMMAND}"
    "-DSTAMP=${OBELISK_NATIVE_SUPPORT_STAMP}"
    "-DDESTINATION=${OBELISK_NATIVE_SUPPORT_DIR}"
    "-DRUNTIME_ARCHIVE=${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "-DRUNTIME_LTO_ARCHIVE=${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
    "-DRUNTIME_PRELINKED_ARCHIVE=${OBELISK_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
    "-DLLVM_DIST=${OBELISK_LLVM_DIST_DIR}"
    "-DSOURCE_DIR=${_obelisk_source_dir}"
    "-DSTAGE_KEY=${OBELISK_NATIVE_SUPPORT_KEY}"
    "-DTARGET_TRIPLE=${OBELISK_TARGET_TRIPLE}"
    "-DLLVM_VERSION_MAJOR=${LLVM_VERSION_MAJOR}"
    -P "${_obelisk_source_dir}/cmake/StageNativeSupport.cmake"
  DEPENDS
    "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
    "${OBELISK_TARGET_RUNTIME_PRELINKED_ARCHIVE}"
    "${_obelisk_source_dir}/cmake/StageNativeSupport.cmake"
    "${_obelisk_source_dir}/LICENSE"
    "${_obelisk_source_dir}/docs/third-party/licenses/Apache-2.0.txt"
    "${_obelisk_source_dir}/docs/third-party/licenses/LLVM-exception.txt"
  COMMENT "Staging local Obelisk native-link support"
  VERBATIM)
add_custom_target(obelisk_native_support
  DEPENDS "${OBELISK_NATIVE_SUPPORT_STAMP}")
add_dependencies(obelisk_native_support obelisk_target_runtime)

message(STATUS "Native target triple: ${OBELISK_TARGET_TRIPLE}")
message(STATUS "Native support key: ${OBELISK_NATIVE_SUPPORT_KEY}")
