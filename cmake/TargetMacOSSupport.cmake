# Xcode/Command Line Tools are build dependencies. Generated simulations link
# in process against the staged runtime and SDK text stubs, without xcrun, a
# compiler subprocess, or an installed SDK on the user's machine.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET MATCHES "^[0-9]+(\\.[0-9]+)*$" OR
   CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "11.0")
  message(FATAL_ERROR "Set CMAKE_OSX_DEPLOYMENT_TARGET to macOS 11.0 or later")
endif()
set(TALOS_TARGET_TRIPLE
  "aarch64-apple-macosx${CMAKE_OSX_DEPLOYMENT_TARGET}" CACHE STRING
  "Native code-generation target triple")
if(NOT TALOS_TARGET_TRIPLE MATCHES
   "^(aarch64|arm64)-apple-(macosx[0-9.]*|darwin[0-9.]*)$")
  message(FATAL_ERROR "The macOS native backend requires an ARM64 Apple target")
endif()
if(CMAKE_OSX_SYSROOT AND IS_DIRECTORY "${CMAKE_OSX_SYSROOT}")
  set(_talos_macos_sdk "${CMAKE_OSX_SYSROOT}")
else()
  set(_talos_macos_sdk_name macosx)
  if(CMAKE_OSX_SYSROOT)
    set(_talos_macos_sdk_name "${CMAKE_OSX_SYSROOT}")
  endif()
  execute_process(COMMAND xcrun --sdk "${_talos_macos_sdk_name}" --show-sdk-path
    RESULT_VARIABLE _talos_sdk_status
    OUTPUT_VARIABLE _talos_macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _talos_sdk_status EQUAL 0)
    message(FATAL_ERROR "Building Talos requires a macOS SDK")
  endif()
endif()
execute_process(COMMAND xcrun --sdk "${_talos_macos_sdk}" --show-sdk-version
  RESULT_VARIABLE _talos_sdk_version_status
  OUTPUT_VARIABLE TALOS_MACOS_SDK_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _talos_sdk_version_status EQUAL 0 OR
   NOT TALOS_MACOS_SDK_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
  message(FATAL_ERROR "Could not determine the macOS SDK version")
endif()
execute_process(COMMAND xcrun --sdk macosx --find clang++
  RESULT_VARIABLE _talos_clang_status
  OUTPUT_VARIABLE _talos_apple_clang OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _talos_clang_status EQUAL 0)
  message(FATAL_ERROR "Could not locate the Apple C++ toolchain")
endif()
get_filename_component(_talos_apple_bin "${_talos_apple_clang}" DIRECTORY)
# Match the system libc++ ABI. The LLVM SDK's newer libc++ headers can refer to
# symbols which do not exist in the macOS system runtime.
find_path(_talos_macos_cxx_headers string
  PATHS "${_talos_macos_sdk}/usr/include/c++/v1"
        "${_talos_apple_bin}/../include/c++/v1"
  NO_DEFAULT_PATH REQUIRED)
set(TALOS_MACOS_SYSROOT "${_talos_macos_sdk}")
set(TALOS_MACOS_CXX_HEADERS "${_talos_macos_cxx_headers}")

set(_talos_macos_runtime_dir "${CMAKE_BINARY_DIR}/target-runtime")
file(MAKE_DIRECTORY "${_talos_macos_runtime_dir}")
set(_talos_macos_builtins
  "${TALOS_LLVM_DIST_DIR}/lib/clang/${LLVM_VERSION_MAJOR}/lib/darwin/libclang_rt.osx.a")
if(NOT EXISTS "${_talos_macos_builtins}")
  message(FATAL_ERROR "The LLVM SDK is missing ${_talos_macos_builtins}")
endif()
execute_process(
  COMMAND "${TALOS_LLVM_TOOLS_DIR}/llvm-lipo" -archs "${_talos_macos_builtins}"
  RESULT_VARIABLE _talos_lipo_status
  OUTPUT_VARIABLE _talos_builtin_arches OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _talos_lipo_status EQUAL 0 OR
   NOT _talos_builtin_arches MATCHES "(^| )arm64($| )")
  message(FATAL_ERROR "The LLVM SDK has no ARM64 macOS compiler-rt archive")
endif()
set(TALOS_COMPILER_RT_BUILTINS "${_talos_macos_builtins}")
# llvm-ar flattens only plain archives, and the SDK ships a universal file
# even when it holds just the arm64 slice. Thin every fat input.
file(READ "${_talos_macos_builtins}" _talos_builtins_magic LIMIT 4 HEX)
if(_talos_builtins_magic MATCHES "^cafebab[ef]$")
  set(TALOS_COMPILER_RT_BUILTINS
    "${_talos_macos_runtime_dir}/libclang_rt.osx.a")
  execute_process(
    COMMAND "${TALOS_LLVM_TOOLS_DIR}/llvm-lipo"
      "${_talos_macos_builtins}" -thin arm64
      -output "${TALOS_COMPILER_RT_BUILTINS}.tmp"
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${TALOS_COMPILER_RT_BUILTINS}.tmp" "${TALOS_COMPILER_RT_BUILTINS}"
    COMMAND_ERROR_IS_FATAL ANY)
  file(REMOVE "${TALOS_COMPILER_RT_BUILTINS}.tmp")
endif()

set(TALOS_TARGET_RUNTIME_ARCHIVE
  "${_talos_macos_runtime_dir}/libtalos_rt.a")
set(TALOS_TARGET_RUNTIME_LTO_ARCHIVE
  "${_talos_macos_runtime_dir}/libtalos_rt_lto.a")
set(TALOS_NATIVE_SUPPORT_DIR
  "${CMAKE_BINARY_DIR}/lib/talos/targets/${TALOS_TARGET_TRIPLE}")
set(_talos_macos_runtime_sources
  ABI Bytecode Containers Coverage CoverageBlockEvents CoverageDatabase
  DesignBytecode DesignBytecodeImage DesignBytecodeIntrinsics DesignBytecodeLogic
  DesignBytecodeNets DesignBytecodeObservers DesignBytecodeRoots DesignDatabase
  DPI FileIO Format ManagedHeap Plusargs Process ProcessAllocation ProcessAOT
  ProcessNativeState ProcessNBA ProcessObservers ProcessSignals ProcessState
  ProcessTable ProcessTransitions ProcessValidation Random RandSolve RandSamplePlan
  RandReal RandSolveWide Runtime Sampled StochasticQueue System VCD VPI VPIValueLayout
  ScanFormat DynamicScanBytecode ContainerBitstream RecursiveBitstream
  ContainerBitstreamBytecode ClassBitstream ClassBitstreamBytecode DPIExport
  DPIExportBytecode DPIOpenArray DPIAggregate)
set(_talos_macos_runtime_headers
  "${CMAKE_BINARY_DIR}/include/talos/Reflection/DesignReflectionLayout.h.inc"
  "${CMAKE_BINARY_DIR}/include/talos/Reflection/VPIObjectModel.h.inc"
  "${CMAKE_BINARY_DIR}/include/talos/Coverage/CoverageFormatDecls.h.inc"
  "${CMAKE_BINARY_DIR}/include/talos/Coverage/CoverageFormatParser.h.inc"
  "${CMAKE_BINARY_DIR}/include/talos/Coverage/CoverageFormatSerializer.h.inc")
set(_talos_macos_runtime_definitions)
if(TALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  list(APPEND _talos_macos_runtime_definitions
    -DTALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()
foreach(flavor IN ITEMS native lto)
  set(objects)
  set(lto_flags)
  if(flavor STREQUAL "lto")
    set(lto_flags -flto=full -funified-lto)
  endif()
  foreach(source IN LISTS _talos_macos_runtime_sources)
    set(object "${_talos_macos_runtime_dir}/${flavor}/${source}.o")
    list(APPEND objects "${object}")
    add_custom_command(
      OUTPUT "${object}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${_talos_macos_runtime_dir}/${flavor}"
      COMMAND ${CMAKE_CXX_COMPILER_LAUNCHER}
        "${TALOS_LLVM_TOOLS_DIR}/clang++"
        --target=${TALOS_TARGET_TRIPLE} -std=c++17 -O3
        -isysroot "${_talos_macos_sdk}" -nostdinc++
        -isystem "${_talos_macos_cxx_headers}"
        -fPIC -fvisibility=hidden ${lto_flags}
        ${_talos_macos_runtime_definitions}
        "-ffile-prefix-map=${PROJECT_SOURCE_DIR}=/talos"
        -I "${PROJECT_SOURCE_DIR}/runtime/include"
        -I "${PROJECT_SOURCE_DIR}/include"
        -I "${CMAKE_BINARY_DIR}/include"
        -isystem "${TALOS_SLANG_SOURCE_DIR}/external/ieee1800"
        -MD -MF "${object}.d" -MQ "${object}"
        -c "${PROJECT_SOURCE_DIR}/runtime/lib/${source}.cpp" -o "${object}"
      DEPFILE "${object}.d"
      DEPENDS "${PROJECT_SOURCE_DIR}/runtime/lib/${source}.cpp"
        ${_talos_macos_runtime_headers}
      VERBATIM)
  endforeach()
  if(flavor STREQUAL "lto")
    set(archive "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}")
  else()
    set(archive "${TALOS_TARGET_RUNTIME_ARCHIVE}")
  endif()
  add_custom_command(
    OUTPUT "${archive}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${archive}"
    COMMAND "${TALOS_LLVM_TOOLS_DIR}/llvm-ar" rcs "${archive}" ${objects}
    COMMAND "${TALOS_LLVM_TOOLS_DIR}/llvm-ar" qLs
      "${archive}" "${TALOS_COMPILER_RT_BUILTINS}"
    DEPENDS ${objects} "${TALOS_COMPILER_RT_BUILTINS}"
    VERBATIM)
endforeach()
add_custom_target(talos_target_runtime
  DEPENDS "${TALOS_TARGET_RUNTIME_ARCHIVE}"
    "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}")
set(_talos_macos_support_stamp "${CMAKE_BINARY_DIR}/macos-native-support.complete")
add_custom_command(
  OUTPUT "${_talos_macos_support_stamp}"
  BYPRODUCTS "${TALOS_NATIVE_SUPPORT_DIR}/.complete"
    "${TALOS_NATIVE_SUPPORT_DIR}/libtalos_rt.a"
    "${TALOS_NATIVE_SUPPORT_DIR}/libtalos_rt_lto.a"
    "${TALOS_NATIVE_SUPPORT_DIR}/weak-imports.txt"
    "${TALOS_NATIVE_SUPPORT_DIR}/weak-imports.rsp"
  COMMAND "${CMAKE_COMMAND}"
    "-DSDK=${_talos_macos_sdk}"
    "-DDESTINATION=${TALOS_NATIVE_SUPPORT_DIR}"
    "-DRUNTIME=${TALOS_TARGET_RUNTIME_ARCHIVE}"
    "-DRUNTIME_LTO=${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}"
    "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DLLVM_NM=${TALOS_LLVM_TOOLS_DIR}/llvm-nm"
    -P "${PROJECT_SOURCE_DIR}/cmake/StageMacOSSupport.cmake"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_talos_macos_support_stamp}"
  DEPENDS "${TALOS_TARGET_RUNTIME_ARCHIVE}"
    "${TALOS_TARGET_RUNTIME_LTO_ARCHIVE}"
    "${PROJECT_SOURCE_DIR}/cmake/StageMacOSSupport.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/MacOSWeakImports.cmake"
    "${PROJECT_SOURCE_DIR}/LICENSE"
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-LICENSE.TXT"
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-CREDITS.TXT"
  VERBATIM)
add_custom_target(talos_native_support DEPENDS "${_talos_macos_support_stamp}")
add_dependencies(talos_native_support talos_target_runtime)
message(STATUS "macOS target triple: ${TALOS_TARGET_TRIPLE}")
