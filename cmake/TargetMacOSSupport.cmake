# Xcode/Command Line Tools are build dependencies. Generated simulations link
# in process against the staged runtime and SDK text stubs, without xcrun, a
# compiler subprocess, or an installed SDK on the user's machine.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET MATCHES "^[0-9]+(\\.[0-9]+)*$" OR
   CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "11.0")
  message(FATAL_ERROR "Set CMAKE_OSX_DEPLOYMENT_TARGET to macOS 11.0 or later")
endif()
set(OBELISK_TARGET_TRIPLE
  "aarch64-apple-macosx${CMAKE_OSX_DEPLOYMENT_TARGET}" CACHE STRING
  "Native code-generation target triple")
if(NOT OBELISK_TARGET_TRIPLE MATCHES
   "^(aarch64|arm64)-apple-(macosx[0-9.]*|darwin[0-9.]*)$")
  message(FATAL_ERROR "The macOS native backend requires an ARM64 Apple target")
endif()
if(CMAKE_OSX_SYSROOT AND IS_DIRECTORY "${CMAKE_OSX_SYSROOT}")
  set(_obelisk_macos_sdk "${CMAKE_OSX_SYSROOT}")
else()
  set(_obelisk_macos_sdk_name macosx)
  if(CMAKE_OSX_SYSROOT)
    set(_obelisk_macos_sdk_name "${CMAKE_OSX_SYSROOT}")
  endif()
  execute_process(COMMAND xcrun --sdk "${_obelisk_macos_sdk_name}" --show-sdk-path
    RESULT_VARIABLE _obelisk_sdk_status
    OUTPUT_VARIABLE _obelisk_macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _obelisk_sdk_status EQUAL 0)
    message(FATAL_ERROR "Building Obelisk requires a macOS SDK")
  endif()
endif()
execute_process(COMMAND xcrun --sdk "${_obelisk_macos_sdk}" --show-sdk-version
  RESULT_VARIABLE _obelisk_sdk_version_status
  OUTPUT_VARIABLE OBELISK_MACOS_SDK_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _obelisk_sdk_version_status EQUAL 0 OR
   NOT OBELISK_MACOS_SDK_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
  message(FATAL_ERROR "Could not determine the macOS SDK version")
endif()
execute_process(COMMAND xcrun --sdk macosx --find clang++
  RESULT_VARIABLE _obelisk_clang_status
  OUTPUT_VARIABLE _obelisk_apple_clang OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _obelisk_clang_status EQUAL 0)
  message(FATAL_ERROR "Could not locate the Apple C++ toolchain")
endif()
get_filename_component(_obelisk_apple_bin "${_obelisk_apple_clang}" DIRECTORY)
# Match the system libc++ ABI. The LLVM SDK's newer libc++ headers can refer to
# symbols which do not exist in the macOS system runtime.
find_path(_obelisk_macos_cxx_headers string
  PATHS "${_obelisk_macos_sdk}/usr/include/c++/v1"
        "${_obelisk_apple_bin}/../include/c++/v1"
  NO_DEFAULT_PATH REQUIRED)
set(OBELISK_MACOS_SYSROOT "${_obelisk_macos_sdk}")
set(OBELISK_MACOS_CXX_HEADERS "${_obelisk_macos_cxx_headers}")

set(_obelisk_macos_runtime_dir "${CMAKE_BINARY_DIR}/target-runtime")
file(MAKE_DIRECTORY "${_obelisk_macos_runtime_dir}")
set(_obelisk_macos_builtins
  "${OBELISK_LLVM_DIST_DIR}/lib/clang/${LLVM_VERSION_MAJOR}/lib/darwin/libclang_rt.osx.a")
if(NOT EXISTS "${_obelisk_macos_builtins}")
  message(FATAL_ERROR "The LLVM SDK is missing ${_obelisk_macos_builtins}")
endif()
execute_process(
  COMMAND "${OBELISK_LLVM_TOOLS_DIR}/llvm-lipo" -archs "${_obelisk_macos_builtins}"
  RESULT_VARIABLE _obelisk_lipo_status
  OUTPUT_VARIABLE _obelisk_builtin_arches OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _obelisk_lipo_status EQUAL 0 OR
   NOT _obelisk_builtin_arches MATCHES "(^| )arm64($| )")
  message(FATAL_ERROR "The LLVM SDK has no ARM64 macOS compiler-rt archive")
endif()
set(OBELISK_COMPILER_RT_BUILTINS "${_obelisk_macos_builtins}")
# llvm-ar flattens only plain archives, and the SDK ships a universal file
# even when it holds just the arm64 slice. Thin every fat input.
file(READ "${_obelisk_macos_builtins}" _obelisk_builtins_magic LIMIT 4 HEX)
if(_obelisk_builtins_magic MATCHES "^cafebab[ef]$")
  set(OBELISK_COMPILER_RT_BUILTINS
    "${_obelisk_macos_runtime_dir}/libclang_rt.osx.a")
  execute_process(
    COMMAND "${OBELISK_LLVM_TOOLS_DIR}/llvm-lipo"
      "${_obelisk_macos_builtins}" -thin arm64
      -output "${OBELISK_COMPILER_RT_BUILTINS}.tmp"
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${OBELISK_COMPILER_RT_BUILTINS}.tmp" "${OBELISK_COMPILER_RT_BUILTINS}"
    COMMAND_ERROR_IS_FATAL ANY)
  file(REMOVE "${OBELISK_COMPILER_RT_BUILTINS}.tmp")
endif()

set(OBELISK_TARGET_RUNTIME_ARCHIVE
  "${_obelisk_macos_runtime_dir}/libobelisk_rt.a")
set(OBELISK_TARGET_RUNTIME_LTO_ARCHIVE
  "${_obelisk_macos_runtime_dir}/libobelisk_rt_lto.a")
set(OBELISK_NATIVE_SUPPORT_DIR
  "${CMAKE_BINARY_DIR}/lib/obelisk/targets/${OBELISK_TARGET_TRIPLE}")
set(_obelisk_macos_runtime_sources
  ABI Bytecode Containers Coverage CoverageBlockEvents CoverageDatabase
  DesignBytecode DesignBytecodeImage DesignBytecodeIntrinsics DesignBytecodeLogic
  DesignBytecodeNets DesignBytecodeObservers DesignBytecodeRoots DesignDatabase
  DPI FileIO Format ManagedHeap Plusargs Process ProcessAllocation ProcessAOT
  ProcessNativeState ProcessNBA ProcessObservers ProcessSignals ProcessState
  ProcessTable ProcessTransitions ProcessValidation Random RandSolve RandSamplePlan
  RandReal RandSolveWide Runtime Sampled StochasticQueue System VCD VPI
  ScanFormat DynamicScanBytecode ContainerBitstream RecursiveBitstream
  ContainerBitstreamBytecode ClassBitstream ClassBitstreamBytecode DPIExport
  DPIExportBytecode DPIOpenArray DPIAggregate)
set(_obelisk_macos_runtime_headers
  "${CMAKE_BINARY_DIR}/include/obelisk/Reflection/DesignReflectionLayout.h.inc"
  "${CMAKE_BINARY_DIR}/include/obelisk/Reflection/VPIObjectModel.h.inc"
  "${CMAKE_BINARY_DIR}/include/obelisk/Coverage/CoverageFormatDecls.h.inc"
  "${CMAKE_BINARY_DIR}/include/obelisk/Coverage/CoverageFormatParser.h.inc"
  "${CMAKE_BINARY_DIR}/include/obelisk/Coverage/CoverageFormatSerializer.h.inc")
set(_obelisk_macos_runtime_definitions)
if(OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS)
  list(APPEND _obelisk_macos_runtime_definitions
    -DOBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS=1)
endif()
foreach(flavor IN ITEMS native lto)
  set(objects)
  set(lto_flags)
  if(flavor STREQUAL "lto")
    set(lto_flags -flto=full -funified-lto)
  endif()
  foreach(source IN LISTS _obelisk_macos_runtime_sources)
    set(object "${_obelisk_macos_runtime_dir}/${flavor}/${source}.o")
    list(APPEND objects "${object}")
    add_custom_command(
      OUTPUT "${object}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${_obelisk_macos_runtime_dir}/${flavor}"
      COMMAND ${CMAKE_CXX_COMPILER_LAUNCHER}
        "${OBELISK_LLVM_TOOLS_DIR}/clang++"
        --target=${OBELISK_TARGET_TRIPLE} -std=c++17 -O3
        -isysroot "${_obelisk_macos_sdk}" -nostdinc++
        -isystem "${_obelisk_macos_cxx_headers}"
        -fPIC -fvisibility=hidden ${lto_flags}
        ${_obelisk_macos_runtime_definitions}
        "-ffile-prefix-map=${PROJECT_SOURCE_DIR}=/obelisk"
        -I "${PROJECT_SOURCE_DIR}/runtime/include"
        -I "${PROJECT_SOURCE_DIR}/include"
        -I "${CMAKE_BINARY_DIR}/include"
        -isystem "${OBELISK_SLANG_SOURCE_DIR}/external/ieee1800"
        -MD -MF "${object}.d" -MQ "${object}"
        -c "${PROJECT_SOURCE_DIR}/runtime/lib/${source}.cpp" -o "${object}"
      DEPFILE "${object}.d"
      DEPENDS "${PROJECT_SOURCE_DIR}/runtime/lib/${source}.cpp"
        ${_obelisk_macos_runtime_headers}
      VERBATIM)
  endforeach()
  if(flavor STREQUAL "lto")
    set(archive "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}")
  else()
    set(archive "${OBELISK_TARGET_RUNTIME_ARCHIVE}")
  endif()
  add_custom_command(
    OUTPUT "${archive}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${archive}"
    COMMAND "${OBELISK_LLVM_TOOLS_DIR}/llvm-ar" rcs "${archive}" ${objects}
    COMMAND "${OBELISK_LLVM_TOOLS_DIR}/llvm-ar" qLs
      "${archive}" "${OBELISK_COMPILER_RT_BUILTINS}"
    DEPENDS ${objects} "${OBELISK_COMPILER_RT_BUILTINS}"
    VERBATIM)
endforeach()
add_custom_target(obelisk_target_runtime
  DEPENDS "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}")
set(_obelisk_macos_support_stamp "${CMAKE_BINARY_DIR}/macos-native-support.complete")
add_custom_command(
  OUTPUT "${_obelisk_macos_support_stamp}"
  BYPRODUCTS "${OBELISK_NATIVE_SUPPORT_DIR}/.complete"
    "${OBELISK_NATIVE_SUPPORT_DIR}/libobelisk_rt.a"
    "${OBELISK_NATIVE_SUPPORT_DIR}/libobelisk_rt_lto.a"
    "${OBELISK_NATIVE_SUPPORT_DIR}/weak-imports.txt"
    "${OBELISK_NATIVE_SUPPORT_DIR}/weak-imports.rsp"
  COMMAND "${CMAKE_COMMAND}"
    "-DSDK=${_obelisk_macos_sdk}"
    "-DDESTINATION=${OBELISK_NATIVE_SUPPORT_DIR}"
    "-DRUNTIME=${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "-DRUNTIME_LTO=${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
    "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DLLVM_NM=${OBELISK_LLVM_TOOLS_DIR}/llvm-nm"
    -P "${PROJECT_SOURCE_DIR}/cmake/StageMacOSSupport.cmake"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_obelisk_macos_support_stamp}"
  DEPENDS "${OBELISK_TARGET_RUNTIME_ARCHIVE}"
    "${OBELISK_TARGET_RUNTIME_LTO_ARCHIVE}"
    "${PROJECT_SOURCE_DIR}/cmake/StageMacOSSupport.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/MacOSWeakImports.cmake"
    "${PROJECT_SOURCE_DIR}/LICENSE"
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-LICENSE.TXT"
    "${PROJECT_SOURCE_DIR}/docs/third-party/licenses/compiler-rt-CREDITS.TXT"
  VERBATIM)
add_custom_target(obelisk_native_support DEPENDS "${_obelisk_macos_support_stamp}")
add_dependencies(obelisk_native_support obelisk_target_runtime)
message(STATUS "macOS target triple: ${OBELISK_TARGET_TRIPLE}")
