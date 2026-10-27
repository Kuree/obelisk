cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# FetchContent's base directory contains configured object files in addition to
# downloaded sources. Pointing it into another CMake build tree lets independent
# Ninja processes write the same objects while maintaining different dependency
# databases, which corrupts incremental-build state. A standalone external
# cache remains valid; reject only a base nested in a different configured tree.
cmake_path(ABSOLUTE_PATH FETCHCONTENT_BASE_DIR NORMALIZE
  OUTPUT_VARIABLE _talos_fetchcontent_base_dir)
cmake_path(ABSOLUTE_PATH CMAKE_BINARY_DIR NORMALIZE
  OUTPUT_VARIABLE _talos_binary_dir)
set(_talos_fetchcontent_probe "${_talos_fetchcontent_base_dir}")
while(TRUE)
  if("${_talos_fetchcontent_probe}" STREQUAL "${_talos_binary_dir}")
    break()
  endif()
  if(EXISTS "${_talos_fetchcontent_probe}/CMakeCache.txt")
    message(FATAL_ERROR
      "FETCHCONTENT_BASE_DIR (${_talos_fetchcontent_base_dir}) is inside "
      "another CMake build tree (${_talos_fetchcontent_probe}). Use this "
      "build's own ${CMAKE_BINARY_DIR}/_deps directory or a standalone cache.")
  endif()
  cmake_path(GET _talos_fetchcontent_probe PARENT_PATH
    _talos_fetchcontent_parent)
  if("${_talos_fetchcontent_parent}" STREQUAL
     "${_talos_fetchcontent_probe}")
    break()
  endif()
  set(_talos_fetchcontent_probe "${_talos_fetchcontent_parent}")
endwhile()
unset(_talos_fetchcontent_parent)
unset(_talos_fetchcontent_probe)
unset(_talos_binary_dir)
unset(_talos_fetchcontent_base_dir)

option(TALOS_INCLUDE_TESTS "Build the Talos test suites" ON)
option(TALOS_RT_BYTECODE_VALIDATION_DIAGNOSTICS
  "Report failed bytecode image validation checks" OFF)
option(TALOS_ENABLE_Z3
  "Use compiler-side Z3 analysis for constrained-random planning" ON)
set(TALOS_Z3_SOURCE_DIR "" CACHE PATH
  "Extracted Z3 source tree used instead of the pinned archive")
set(TALOS_Z3_INSTALL_DIR "" CACHE PATH
  "Existing pinned static Z3 installation for the compiler host")

# Op definitions generated from the SystemVerilog AST inventory dominate the
# build's critical path when one translation unit compiles a whole dialect.
# TableGen splits them into this many shards, each its own translation unit.
set(TALOS_OP_SHARD_COUNT "4" CACHE STRING
  "Translation units used for each dialect's generated op definitions")

#===------------------------------------------------------------------------===
# Toolchain: a CMake-managed prebuilt LLVM + MLIR release SDK.
#
# We deliberately do not build LLVM or MLIR from source. The selected binary
# SDK must contain matching
# LLVM and MLIR headers, libraries, TableGen tools, and CMake package files.
#
# The official archive is downloaded and extracted below CMAKE_BINARY_DIR. A
# local file:// URL provides a fully offline path while keeping the extracted
# distribution owned by the build tree.
#
# This happens before project() because the SDK's clang is the default host
# compiler, and project() is where CMake selects it.
#===------------------------------------------------------------------------===
set(TALOS_LLVM_VERSION "22.1.6")

# CMAKE_HOST_SYSTEM_PROCESSOR is only set by project().
cmake_host_system_information(RESULT _talos_host_processor
  QUERY OS_PLATFORM)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND
   DEFINED ENV{PROCESSOR_ARCHITEW6432})
  # An emulated shell may report its own architecture instead of the host's.
  set(_talos_host_processor "$ENV{PROCESSOR_ARCHITEW6432}")
endif()
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND
   _talos_host_processor MATCHES "^(aarch64|arm64|ARM64)$")
  # The first conda-forge Windows ARM64 MLIR SDK is 22.1.8.
  set(TALOS_LLVM_VERSION "22.1.8")
endif()

set(TALOS_LLVM_INSTALL_DIR "" CACHE PATH
  "Existing matching LLVM + MLIR + Clang + LLD binary SDK")
if(TALOS_LLVM_INSTALL_DIR)
  get_filename_component(TALOS_LLVM_DIST_DIR
    "${TALOS_LLVM_INSTALL_DIR}" ABSOLUTE)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND
       _talos_host_processor MATCHES "^(x86_64|amd64|AMD64|aarch64|arm64|ARM64)$" AND
       NOT TALOS_LLVM_PREBUILT_SHA256)
  # LLVM's official Windows archive omits MLIR. Use one pinned set of
  # upstream conda-forge binaries, including the LLD development libraries.
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/WindowsPrebuiltLLVM.cmake")
endif()

if(NOT TALOS_LLVM_DIST_DIR)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
   _talos_host_processor MATCHES "^(x86_64|amd64|AMD64)$")
  set(_talos_llvm_archive_name
      "LLVM-${TALOS_LLVM_VERSION}-Linux-X64.tar.xz")
  set(_talos_llvm_archive_sha256
      "c5ac8ef89ca39d30cb32e9b83772f995dd891c685ebc188d593c943a64d5f8b5")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
       _talos_host_processor MATCHES "^(aarch64|arm64|ARM64)$")
  set(_talos_llvm_archive_name
      "LLVM-${TALOS_LLVM_VERSION}-Linux-ARM64.tar.xz")
  set(_talos_llvm_archive_sha256
      "b67817634e8e1c2632dfc056af14d61b94f8e6502f4e557560eea227aa22ce37")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
       _talos_host_processor MATCHES "^(aarch64|arm64|ARM64)$")
  set(_talos_llvm_archive_name
      "LLVM-${TALOS_LLVM_VERSION}-macOS-ARM64.tar.xz")
  set(_talos_llvm_archive_sha256
      "8059d9d9eeb059c30d812b4a37291888f8dcba04d2b5ace61fd12d2904eaa0e9")
else()
  set(_talos_llvm_archive_name "")
  set(_talos_llvm_archive_sha256 "")
endif()

set(TALOS_LLVM_PREBUILT_URL
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-${TALOS_LLVM_VERSION}/${_talos_llvm_archive_name}"
    CACHE STRING "Official or local file:// LLVM + MLIR prebuilt archive")
set(TALOS_LLVM_PREBUILT_SHA256 "${_talos_llvm_archive_sha256}"
    CACHE STRING "SHA-256 of TALOS_LLVM_PREBUILT_URL")

if(NOT TALOS_LLVM_PREBUILT_URL OR NOT TALOS_LLVM_PREBUILT_SHA256)
  message(FATAL_ERROR
    "No official LLVM + MLIR v${TALOS_LLVM_VERSION} archive is known for "
    "${CMAKE_HOST_SYSTEM_NAME}/${_talos_host_processor}; set "
    "TALOS_LLVM_INSTALL_DIR to a matching binary SDK, or set "
    "TALOS_LLVM_PREBUILT_URL and TALOS_LLVM_PREBUILT_SHA256")
endif()

if(NOT _talos_llvm_archive_name)
  set(_talos_llvm_archive_name "llvm-mlir-prebuilt.archive")
endif()

set(_talos_llvm_download_dir "${CMAKE_BINARY_DIR}/_downloads")
set(_talos_llvm_extract_dir "${CMAKE_BINARY_DIR}/llvm-mlir")
set(_talos_llvm_extract_stamp
    "${_talos_llvm_extract_dir}/.talos-archive-sha256")
set(_talos_llvm_archive
    "${_talos_llvm_download_dir}/${_talos_llvm_archive_name}")
file(MAKE_DIRECTORY "${_talos_llvm_download_dir}")
file(DOWNLOAD "${TALOS_LLVM_PREBUILT_URL}" "${_talos_llvm_archive}"
  EXPECTED_HASH "SHA256=${TALOS_LLVM_PREBUILT_SHA256}"
  STATUS _talos_llvm_download_status
  SHOW_PROGRESS
  TLS_VERIFY ON)
list(GET _talos_llvm_download_status 0 _talos_llvm_download_code)
list(GET _talos_llvm_download_status 1 _talos_llvm_download_message)
if(NOT _talos_llvm_download_code EQUAL 0)
  message(FATAL_ERROR
    "Failed to download the LLVM + MLIR prebuilt archive: "
    "${_talos_llvm_download_message}")
endif()

file(GLOB_RECURSE _talos_llvm_configs_before
  LIST_DIRECTORIES FALSE
  "${_talos_llvm_extract_dir}/LLVMConfig.cmake")
set(_talos_llvm_extracted_sha256 "")
if(EXISTS "${_talos_llvm_extract_stamp}")
  file(READ "${_talos_llvm_extract_stamp}"
       _talos_llvm_extracted_sha256)
  string(STRIP "${_talos_llvm_extracted_sha256}"
         _talos_llvm_extracted_sha256)
endif()
if(NOT _talos_llvm_configs_before OR
   NOT _talos_llvm_extracted_sha256 STREQUAL
       TALOS_LLVM_PREBUILT_SHA256)
  file(REMOVE_RECURSE "${_talos_llvm_extract_dir}")
  file(MAKE_DIRECTORY "${_talos_llvm_extract_dir}")
  file(ARCHIVE_EXTRACT INPUT "${_talos_llvm_archive}"
    DESTINATION "${_talos_llvm_extract_dir}")
  file(WRITE "${_talos_llvm_extract_stamp}"
       "${TALOS_LLVM_PREBUILT_SHA256}\n")
endif()

file(GLOB_RECURSE _talos_llvm_configs
  LIST_DIRECTORIES FALSE
  "${_talos_llvm_extract_dir}/LLVMConfig.cmake")
set(_talos_llvm_package_config "")
foreach(config_file IN LISTS _talos_llvm_configs)
  if(config_file MATCHES "/lib/cmake/llvm/LLVMConfig\\.cmake$")
    set(_talos_llvm_package_config "${config_file}")
    break()
  endif()
endforeach()
if(NOT _talos_llvm_package_config)
  message(FATAL_ERROR
    "The downloaded prebuilt archive contains no LLVM CMake package")
endif()
get_filename_component(_talos_llvm_cmake_dir
  "${_talos_llvm_package_config}" DIRECTORY)
get_filename_component(TALOS_LLVM_DIST_DIR
  "${_talos_llvm_cmake_dir}/../../.." ABSOLUTE)
endif()

if(NOT EXISTS "${TALOS_LLVM_DIST_DIR}/lib/cmake/llvm/LLVMConfig.cmake" OR
   NOT EXISTS "${TALOS_LLVM_DIST_DIR}/lib/cmake/mlir/MLIRConfig.cmake")
  message(FATAL_ERROR
    "The prebuilt distribution at ${TALOS_LLVM_DIST_DIR} does not "
    "contain both LLVM and MLIR CMake packages")
endif()

# Compile with the SDK's clang unless a compiler was chosen with
# CMAKE_<LANG>_COMPILER, CC/CXX or a toolchain file. GCC takes about twice as
# long per translation unit on this code base. An existing build directory
# keeps the compiler it was first configured with, since CMake caches it.
option(TALOS_USE_SDK_CLANG
  "Compile with the prebuilt LLVM distribution's clang unless a compiler is chosen"
  ON)
set(_talos_host_executable_suffix "")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(_talos_host_executable_suffix ".exe")
endif()
set(_talos_sdk_clang "${TALOS_LLVM_DIST_DIR}/bin/clang")
set(_talos_sdk_cxx "${_talos_sdk_clang}++${_talos_host_executable_suffix}")
set(_talos_probe_flags)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND
     NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
      "Minimum macOS version for the compiler and generated simulations")
  endif()
  # The SDK's libraries are ThinLTO bitcode that only its own LLD can link,
  # and its Clang neither asks xcrun for an SDK nor, since CMake 4, receives
  # one by default. Its bundled libc++ headers also assume a newer libc++ than
  # macOS provides, so host code uses the SDK's system C++ headers instead.
  set(_talos_macos_sdk "${CMAKE_OSX_SYSROOT}")
  if(NOT IS_DIRECTORY "${_talos_macos_sdk}")
    set(_talos_sdk_name macosx)
    if(CMAKE_OSX_SYSROOT)
      set(_talos_sdk_name "${CMAKE_OSX_SYSROOT}")
    endif()
    execute_process(COMMAND xcrun --sdk "${_talos_sdk_name}" --show-sdk-path
      RESULT_VARIABLE _talos_xcrun_result
      OUTPUT_VARIABLE _talos_macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _talos_xcrun_result EQUAL 0 OR
       NOT IS_DIRECTORY "${_talos_macos_sdk}")
      message(FATAL_ERROR
        "Building Talos on macOS requires Xcode or the Command Line Tools")
    endif()
    set(CMAKE_OSX_SYSROOT "${_talos_macos_sdk}" CACHE PATH "macOS SDK")
  endif()
  set(TALOS_HOST_CXX_STDLIB_FLAGS
    "-stdlib++-isystem ${_talos_macos_sdk}/usr/include/c++/v1")
  set(_talos_probe_flags -isysroot "${_talos_macos_sdk}"
    "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}"
    -stdlib++-isystem "${_talos_macos_sdk}/usr/include/c++/v1")
endif()
if(TALOS_USE_SDK_CLANG AND
   CMAKE_HOST_SYSTEM_NAME MATCHES "^(Linux|Windows|Darwin)$" AND
   NOT CMAKE_TOOLCHAIN_FILE AND
   NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED CMAKE_CXX_COMPILER AND
   NOT DEFINED ENV{CC} AND NOT DEFINED ENV{CXX} AND
   EXISTS "${_talos_sdk_cxx}")
  # The SDK's Clang uses the host's C++ library and C runtime. Verify that
  # their headers and libraries are installed before selecting the compiler.
  set(_talos_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/TalosSDKClangProbe")
  file(MAKE_DIRECTORY "${_talos_probe_dir}")
  file(WRITE "${_talos_probe_dir}/probe.cpp"
    "#include <string>\n"
    "int main() { return static_cast<int>(std::string(\"a\").size()) - 1; }\n")
  execute_process(
    COMMAND "${_talos_sdk_cxx}" ${_talos_probe_flags}
      "${_talos_probe_dir}/probe.cpp"
      -o "${_talos_probe_dir}/probe${_talos_host_executable_suffix}"
    RESULT_VARIABLE _talos_probe_result
    OUTPUT_QUIET
    ERROR_VARIABLE _talos_probe_error)
  if(_talos_probe_result EQUAL 0)
    set(CMAKE_C_COMPILER "${_talos_sdk_clang}${_talos_host_executable_suffix}"
      CACHE FILEPATH "C compiler")
    set(CMAKE_CXX_COMPILER "${_talos_sdk_cxx}" CACHE FILEPATH
      "C++ compiler")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR
      "The SDK's Clang cannot build against this host's C++ library. "
      "Install Visual Studio C++ build tools and a Windows SDK:\n${_talos_probe_error}")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR
      "The SDK's Clang cannot build against the macOS SDK:\n${_talos_probe_error}")
  else()
    message(STATUS
      "The SDK's clang cannot build against this host's C++ library; using "
      "the default compiler instead:\n${_talos_probe_error}")
  endif()
  unset(_talos_probe_dir)
  unset(_talos_probe_result)
  unset(_talos_probe_error)
endif()
unset(_talos_sdk_clang)
unset(_talos_sdk_cxx)
unset(_talos_probe_flags)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_LINKER "${TALOS_LLVM_DIST_DIR}/bin/lld-link.exe"
    CACHE FILEPATH "SDK COFF linker")
endif()

project(talos
  VERSION 0.1.0
  DESCRIPTION "An AOT SystemVerilog simulator targeting IEEE 1800 conformance and real UVM"
  LANGUAGES C CXX)

set(MLIR_DIR "${TALOS_LLVM_DIST_DIR}/lib/cmake/mlir")
set(LLVM_DIR "${TALOS_LLVM_DIST_DIR}/lib/cmake/llvm")
list(PREPEND CMAKE_PREFIX_PATH "${TALOS_LLVM_DIST_DIR}")
if(WIN32 AND NOT DEFINED ENV{VSINSTALLDIR})
  find_program(_talos_vswhere vswhere
    HINTS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer")
  if(_talos_vswhere)
    execute_process(COMMAND "${_talos_vswhere}" -latest -products "*"
      -property installationPath
      OUTPUT_VARIABLE _talos_visual_studio OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_talos_visual_studio)
      set(ENV{VSINSTALLDIR} "${_talos_visual_studio}")
    endif()
  endif()
endif()

find_package(LLVM REQUIRED CONFIG)
find_package(MLIR REQUIRED CONFIG)
set(LLD_DIR "${TALOS_LLVM_DIST_DIR}/lib/cmake/lld")
find_package(LLD REQUIRED CONFIG)
if(NOT LLVM_PACKAGE_VERSION VERSION_EQUAL TALOS_LLVM_VERSION)
  message(FATAL_ERROR
    "The binary SDK must provide LLVM ${TALOS_LLVM_VERSION}, got ${LLVM_PACKAGE_VERSION}")
endif()
if(WIN32)
  set(TALOS_TARGET_TRIPLE "${LLVM_HOST_TRIPLE}" CACHE STRING
    "Native code-generation target triple")
  include("${PROJECT_SOURCE_DIR}/cmake/WindowsTarget.cmake")
  talos_windows_target("${LLVM_HOST_TRIPLE}" TALOS_WINDOWS_HOST)
  talos_windows_target("${TALOS_TARGET_TRIPLE}" TALOS_WINDOWS_TARGET)
  if(NOT TALOS_WINDOWS_HOST_COFF_MACHINE STREQUAL
         TALOS_WINDOWS_TARGET_COFF_MACHINE)
    message(FATAL_ERROR
      "Native target ${TALOS_TARGET_TRIPLE} must match the SDK host ${LLVM_HOST_TRIPLE}")
  endif()
  execute_process(
    COMMAND "${TALOS_LLVM_DIST_DIR}/bin/clang.exe"
      --target=${TALOS_TARGET_TRIPLE} --rtlib=compiler-rt -print-libgcc-file-name
    RESULT_VARIABLE _talos_builtins_status
    OUTPUT_VARIABLE TALOS_COMPILER_RT_BUILTINS
    ERROR_VARIABLE _talos_builtins_diagnostic OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _talos_builtins_status EQUAL 0 OR
     NOT EXISTS "${TALOS_COMPILER_RT_BUILTINS}")
    message(FATAL_ERROR
      "The SDK has no compiler-rt builtins for ${TALOS_TARGET_TRIPLE}: "
      "${TALOS_COMPILER_RT_BUILTINS} ${_talos_builtins_diagnostic}")
  endif()
endif()

message(STATUS "Using prebuilt LLVM ${LLVM_PACKAGE_VERSION} + MLIR")
message(STATUS "  LLVM package = ${LLVM_DIR}")
message(STATUS "  MLIR package = ${MLIR_DIR}")
message(STATUS "  LLD package = ${LLD_DIR}")

if(NOT EXISTS "${TALOS_LLVM_DIST_DIR}/bin/llvm-tblgen${_talos_host_executable_suffix}" OR
   NOT EXISTS "${TALOS_LLVM_DIST_DIR}/bin/mlir-tblgen${_talos_host_executable_suffix}")
  message(FATAL_ERROR
    "The prebuilt LLVM + MLIR distribution is missing TableGen tools")
endif()
set(TALOS_LLVM_TOOLS_DIR "${TALOS_LLVM_DIST_DIR}/bin" CACHE INTERNAL
    "Matching prebuilt LLVM/MLIR tool directory" FORCE)
if(WIN32)
  include("${PROJECT_SOURCE_DIR}/cmake/WindowsSDKSupport.cmake")
endif()

# Keep generators and libraries on the same prebuilt distribution even when a
# build directory was previously configured against another SDK.
set(LLVM_TABLEGEN_EXE "${TALOS_LLVM_TOOLS_DIR}/llvm-tblgen${_talos_host_executable_suffix}"
    CACHE FILEPATH "Matching LLVM TableGen executable" FORCE)
set(MLIR_TABLEGEN_EXE "${TALOS_LLVM_TOOLS_DIR}/mlir-tblgen${_talos_host_executable_suffix}"
    CACHE FILEPATH "Matching MLIR TableGen executable" FORCE)

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")

# Declare this before either platform's linker selection. Fresh macOS builds
# must see the default so they enable the shared host ThinLTO cache.
option(TALOS_USE_SDK_LLD
  "Link host executables with the prebuilt LLVM distribution's lld" ON)

# The macOS SDK ships its libraries as ThinLTO bitcode, which only its own LLD
# can link. Select it before LLVM's options detect the linker, and share one
# ThinLTO cache across the host executables.
set(_talos_use_macos_sdk_lld FALSE)
if(APPLE AND TALOS_USE_SDK_LLD AND NOT LLVM_USE_LINKER AND
   EXISTS "${TALOS_LLVM_TOOLS_DIR}/ld64.lld")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(LLVM_USE_LINKER lld)
  else()
    set(LLVM_USE_LINKER "${TALOS_LLVM_TOOLS_DIR}/ld64.lld")
  endif()
  set(_talos_use_macos_sdk_lld TRUE)
  message(STATUS "Host linker: ${TALOS_LLVM_TOOLS_DIR}/ld64.lld")
endif()
# Upstream Clang would otherwise use the SDK's bundled libc++ headers.
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
   TALOS_HOST_CXX_STDLIB_FLAGS)
  string(APPEND CMAKE_CXX_FLAGS " ${TALOS_HOST_CXX_STDLIB_FLAGS}")
endif()

include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(HandleLLVMOptions)

# HandleLLVMOptions first probes the compiler's standard library, then applies
# -fuse-ld. Keep LLD-only cache flags out of those initial probes, which still
# use Apple's default linker and otherwise report a misleading library error.
if(_talos_use_macos_sdk_lld)
  set(TALOS_HOST_LTO_CACHE_DIR "${CMAKE_BINARY_DIR}/lto.cache" CACHE PATH
    "ThinLTO cache for host links of the macOS SDK's bitcode libraries")
  set(TALOS_HOST_LTO_CACHE_POLICY "" CACHE STRING
    "Optional pruning policy for the host ThinLTO cache")
  foreach(flags CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS
                CMAKE_MODULE_LINKER_FLAGS)
    string(APPEND ${flags} " -Wl,-cache_path_lto,${TALOS_HOST_LTO_CACHE_DIR}")
    if(TALOS_HOST_LTO_CACHE_POLICY)
      string(APPEND ${flags}
        " -Wl,--thinlto-cache-policy=${TALOS_HOST_LTO_CACHE_POLICY}")
    endif()
  endforeach()
endif()
unset(_talos_use_macos_sdk_lld)

if(WIN32 AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC" AND NOT MSVC)
  # LLVM 22's add_link_opts assumes non-MSVC compiler drivers use ELF. The
  # SDK's clang++ has GNU command-line syntax but targets the MSVC COFF ABI.
  # Supply COFF dead stripping and keep AddLLVM from adding --gc-sections.
  set(LLVM_NO_DEAD_STRIP ON)
  add_link_options("$<$<NOT:$<CONFIG:Debug>>:LINKER:/OPT:REF>")
endif()

# The compiler tools statically link LLVM, MLIR, slang and Z3 with debug info
# into 1.3-1.5 GB executables. GNU ld takes ~15 s for each, longer than a
# one-file library edit takes to compile; the SDK's lld takes ~2 s. An explicit
# LLVM_USE_LINKER, handled by HandleLLVMOptions above, takes precedence.
if(TALOS_USE_SDK_LLD AND NOT LLVM_USE_LINKER AND NOT EMSCRIPTEN AND
   CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   EXISTS "${TALOS_LLVM_TOOLS_DIR}/ld.lld")
  set(_talos_lld_flags "")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC's -fuse-ld=lld runs the first ld.lld on its program search path. -B
    # prepends a directory holding only the SDK's ld.lld, so GCC still finds
    # its own assembler and collect2.
    set(_talos_lld_dir "${CMAKE_BINARY_DIR}/host-linker")
    file(MAKE_DIRECTORY "${_talos_lld_dir}")
    file(CREATE_LINK "${TALOS_LLVM_TOOLS_DIR}/ld.lld"
      "${_talos_lld_dir}/ld.lld" SYMBOLIC)
    set(_talos_lld_flags "-fuse-ld=lld -B${_talos_lld_dir}/")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(_talos_lld_flags "--ld-path=${TALOS_LLVM_TOOLS_DIR}/ld.lld")
  endif()
  if(_talos_lld_flags)
    foreach(flags CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS
                  CMAKE_MODULE_LINKER_FLAGS)
      string(APPEND ${flags} " ${_talos_lld_flags}")
    endforeach()
    message(STATUS "Host linker: ${TALOS_LLVM_TOOLS_DIR}/ld.lld")
  endif()
  unset(_talos_lld_flags)
  unset(_talos_lld_dir)
endif()
if(WIN32)
  # CMake already links GNU-style Clang on Windows through lld-link. Match the
  # usual Linux stack reserve: the Windows 1 MiB default is too small for
  # nested semantic lowering and assertion expression analysis.
  add_link_options(LINKER:/STACK:8388608 LINKER:/debug:symtab
                   LINKER:/OPT:NOICF)
endif()

if(EMSCRIPTEN)
  # wasm32, not wasm64. No Safari release implements the Memory64 proposal,
  # so a MEMORY64 module cannot run on any iPhone or Mac; runtime/lib/ABI.cpp
  # carries layout tables for both pointer widths, so 32-bit costs nothing but
  # the 4 GB address-space ceiling.
  #
  # The exception mode is the second half of running on Safari and is just as
  # load-bearing as the pointer width. Native wasm exceptions would pick a
  # Safari generation: 26 implements the standardised exnref encoding and
  # earlier releases only the superseded one, so either choice excludes half the
  # installed base. -fexceptions implements the same C++ semantics in JavaScript
  # and emits no wasm exception opcodes at all, so nothing can reject it. The
  # runtime only ever throws std::bad_alloc, so the slower throw path costs
  # nothing outside an out-of-memory condition. Applied to every translation
  # unit because the mode has to match across the whole link.
  foreach(flags CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS
                CMAKE_SHARED_LINKER_FLAGS)
    if(NOT ${flags} MATCHES "fexceptions")
      set(${flags} "${${flags}} -fexceptions")
    endif()
  endforeach()
  set(_talos_configure_wasm_support TRUE)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  set(TALOS_NATIVE_PLATFORM "linux")
  set(_talos_configure_native_support TRUE)
elseif(APPLE)
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" OR
     (CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"))
    message(FATAL_ERROR "The macOS native backend requires an ARM64 host and build")
  endif()
  set(TALOS_NATIVE_PLATFORM "macos")
  set(_talos_configure_macos_support TRUE)
elseif(WIN32)
  set(TALOS_NATIVE_PLATFORM "windows")
  set(_talos_configure_windows_support TRUE)
else()
  message(FATAL_ERROR
    "native executable generation requires a Linux host, or "
    "configure with emcmake to target wasm32")
endif()

#===------------------------------------------------------------------------===
# Upstream slang semantic-AST frontend.
#
# Build the latest upstream release as part of Talos instead of using a
# compiler SDK's private copy. Offline builds can point
# TALOS_SLANG_SOURCE_DIR at an extracted copy of this exact release.
#===------------------------------------------------------------------------===
set(TALOS_SLANG_VERSION "11.0")
set(TALOS_SLANG_ARCHIVE_SHA256
    "50676d5a9adbefb97d266a4b174e6b0513901afd5ac57a6cdfea0a61149c3704")
set(TALOS_SLANG_SOURCE_DIR "" CACHE PATH
    "Extracted slang v${TALOS_SLANG_VERSION} source tree (offline builds)")
set(TALOS_SLANG_PREBUILT_BUILD_DIR "" CACHE PATH
    "Existing Talos CMake build providing compiled static Slang and fmt")

# Z3 is a host-side compiler dependency only. Build a pinned static library
# from source so the compiler does not inherit the glibc floor of Z3's release
# binaries, and keep the runtime dependency-free. The source override supports
# fully offline builds without weakening the archive checksum path.
if(TALOS_ENABLE_Z3)
  include(ExternalProject)
  set(TALOS_Z3_VERSION "4.13.4")
  set(TALOS_Z3_ARCHIVE_SHA256
      "4071977e66e9f3d239b7b098ceddfe62ffdf3c71e345e9524a4a5001d1f4adf3")
  set(_talos_z3_library_name "libz3.a")
  if(WIN32)
    set(_talos_z3_library_name "z3.lib")
  endif()
  set(_talos_z3_install "${CMAKE_BINARY_DIR}/z3-${TALOS_Z3_VERSION}")
  if(EMSCRIPTEN)
    set(_talos_z3_single_threaded ON)
  else()
    set(_talos_z3_single_threaded OFF)
  endif()
  set(_talos_z3_cmake_args
      -DCMAKE_BUILD_TYPE=Release
      "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DZ3_BUILD_LIBZ3_SHARED=OFF
      -DZ3_BUILD_EXECUTABLE=OFF
      -DZ3_BUILD_TEST_EXECUTABLES=OFF
      -DZ3_ENABLE_EXAMPLE_TARGETS=OFF
      -DZ3_BUILD_DOCUMENTATION=OFF
      -DZ3_BUILD_PYTHON_BINDINGS=OFF
      -DZ3_BUILD_DOTNET_BINDINGS=OFF
      -DZ3_BUILD_JAVA_BINDINGS=OFF
      -DZ3_BUILD_JULIA_BINDINGS=OFF
      -DZ3_INCLUDE_GIT_HASH=OFF
      -DZ3_INCLUDE_GIT_DESCRIBE=OFF
      -DZ3_LINK_TIME_OPTIMIZATION=OFF
      -DZ3_SINGLE_THREADED=${_talos_z3_single_threaded}
      -DZ3_POLLING_TIMER=ON)
  # Preserve cross-compilation, including Emscripten, across the isolated
  # nested configure. Native compilers use Z3's thread-safe build so independent
  # MLIR workers can solve concurrently. The wasm compiler retains
  # single-threaded polling mode because it has no pthread runtime.
  if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND _talos_z3_cmake_args
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
  endif()
  if(EMSCRIPTEN)
    # The toolchain selects em++, but it does not carry the outer project's
    # exception ABI flags into ExternalProject's isolated cache. Z3 is linked
    # into the wasm compiler, so its objects must match.
    list(APPEND _talos_z3_cmake_args
      "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
      "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}")
  endif()
  if(CMAKE_CXX_COMPILER_TARGET)
    list(APPEND _talos_z3_cmake_args
      "-DCMAKE_CXX_COMPILER_TARGET=${CMAKE_CXX_COMPILER_TARGET}")
  endif()
  # Build Z3 for the outer macOS target. Its own default is the SDK version,
  # which makes the linker warn about every Z3 object.
  if(APPLE)
    foreach(variable IN ITEMS CMAKE_OSX_DEPLOYMENT_TARGET
                              CMAKE_OSX_ARCHITECTURES CMAKE_OSX_SYSROOT)
      if(${variable})
        list(APPEND _talos_z3_cmake_args "-D${variable}=${${variable}}")
      endif()
    endforeach()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND TALOS_HOST_CXX_STDLIB_FLAGS)
      list(APPEND _talos_z3_cmake_args
        "-DCMAKE_CXX_FLAGS=${TALOS_HOST_CXX_STDLIB_FLAGS}")
    endif()
  endif()
  # Z3 is by far the largest nested build, so a compiler cache the outer build
  # opted into has to reach it too; the isolated configure would otherwise miss
  # it silently. Purely a pass-through: unset by default, so ordinary builds
  # are unaffected. Z3 declares LANGUAGES CXX, so only the C++ launcher applies.
  if(CMAKE_CXX_COMPILER_LAUNCHER)
    list(APPEND _talos_z3_cmake_args
      "-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")
  endif()
  if(TALOS_Z3_INSTALL_DIR)
    set(_talos_z3_install "${TALOS_Z3_INSTALL_DIR}")
    if(NOT EXISTS "${_talos_z3_install}/lib/${_talos_z3_library_name}" OR
       NOT EXISTS "${_talos_z3_install}/include/z3.h" OR
       NOT EXISTS "${_talos_z3_install}/include/z3_version.h")
      message(FATAL_ERROR
        "TALOS_Z3_INSTALL_DIR must contain pinned static Z3 headers and lib/libz3.a")
    endif()
    file(STRINGS "${_talos_z3_install}/include/z3_version.h"
      _talos_z3_installed_version
      REGEX "^#define Z3_FULL_VERSION[ \t]+\"")
    string(REGEX REPLACE "^#define Z3_FULL_VERSION[ \t]+\"([^\"]+)\"$" "\\1"
      _talos_z3_installed_version "${_talos_z3_installed_version}")
    if(NOT _talos_z3_installed_version STREQUAL "${TALOS_Z3_VERSION}.0")
      message(FATAL_ERROR
        "TALOS_Z3_INSTALL_DIR must contain Z3 ${TALOS_Z3_VERSION}.0")
    endif()
    # Keep the dependency used by the solver's object library without running
    # a second ExternalProject against the shared installation.
    add_custom_target(talos_z3_project)
  elseif(TALOS_Z3_SOURCE_DIR)
    file(STRINGS "${TALOS_Z3_SOURCE_DIR}/CMakeLists.txt"
      _talos_z3_project_line
      REGEX "^project\\(Z3 VERSION ${TALOS_Z3_VERSION}\\.0 LANGUAGES CXX\\)$")
    if(NOT _talos_z3_project_line)
      message(FATAL_ERROR
        "TALOS_Z3_SOURCE_DIR must contain Z3 ${TALOS_Z3_VERSION}")
    endif()
    ExternalProject_Add(talos_z3_project
      SOURCE_DIR "${TALOS_Z3_SOURCE_DIR}"
      DOWNLOAD_COMMAND ""
      EXCLUDE_FROM_ALL TRUE
      CMAKE_ARGS ${_talos_z3_cmake_args}
      INSTALL_DIR "${_talos_z3_install}"
      BUILD_BYPRODUCTS "${_talos_z3_install}/lib/${_talos_z3_library_name}")
  else()
    ExternalProject_Add(talos_z3_project
      URL "https://github.com/Z3Prover/z3/archive/refs/tags/z3-${TALOS_Z3_VERSION}.tar.gz"
      URL_HASH "SHA256=${TALOS_Z3_ARCHIVE_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL TRUE
      CMAKE_ARGS ${_talos_z3_cmake_args}
      INSTALL_DIR "${_talos_z3_install}"
      BUILD_BYPRODUCTS "${_talos_z3_install}/lib/${_talos_z3_library_name}")
  endif()
  if(NOT TALOS_Z3_INSTALL_DIR)
    file(MAKE_DIRECTORY "${_talos_z3_install}/include")
  endif()
  add_library(TalosZ3 STATIC IMPORTED GLOBAL)
  set_target_properties(TalosZ3 PROPERTIES
    IMPORTED_LOCATION "${_talos_z3_install}/lib/${_talos_z3_library_name}"
    INTERFACE_INCLUDE_DIRECTORIES "${_talos_z3_install}/include")
  if(EMSCRIPTEN)
    # The solver benefits from a growable compiler heap. Exception handling is
    # already fixed globally to -fexceptions, which is what Z3 needs; it throws
    # far more than the runtime does.
    target_link_options(TalosZ3 INTERFACE
      "SHELL:-s ALLOW_MEMORY_GROWTH=1")
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(TalosZ3 INTERFACE Threads::Threads)
  endif()
  if(TALOS_Z3_INSTALL_DIR)
    message(STATUS "Reusing compiler-only Z3 ${TALOS_Z3_VERSION} at ${_talos_z3_install}")
  else()
    add_dependencies(TalosZ3 talos_z3_project)
    message(STATUS "Building compiler-only Z3 ${TALOS_Z3_VERSION} from source")
  endif()
endif()

find_package(Python 3.8 COMPONENTS Interpreter REQUIRED)
if(TALOS_SLANG_PREBUILT_BUILD_DIR)
  include("${PROJECT_SOURCE_DIR}/cmake/PrebuiltSlang.cmake")
  talos_import_prebuilt_slang()
else()
  # slang v11.0 declares these dependencies by git tag. Declare checksummed
  # release archives first (FetchContent's first declaration wins) so Talos's
  # dependency graph is immutable and works without git.
  FetchContent_Declare(fmt
    URL "https://github.com/fmtlib/fmt/archive/refs/tags/12.1.0.tar.gz"
    URL_HASH
      "SHA256=ea7de4299689e12b6dddd392f9896f08fb0777ac7168897a244a6d6085043fea"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  if(TALOS_SLANG_SOURCE_DIR)
    FetchContent_Declare(talos_slang
      SOURCE_DIR "${TALOS_SLANG_SOURCE_DIR}")
  else()
    FetchContent_Declare(talos_slang
      URL "https://github.com/MikePopoloski/slang/archive/refs/tags/v${TALOS_SLANG_VERSION}.tar.gz"
      URL_HASH "SHA256=${TALOS_SLANG_ARCHIVE_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  endif()

  # Only the slang library is needed; keep third-party build surface minimal.
  set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
  set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
  set(SLANG_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
  set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "" FORCE)
  set(SLANG_INCLUDE_INSTALL OFF CACHE BOOL "" FORCE)
  set(SLANG_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
  set(SLANG_USE_MIMALLOC OFF CACHE BOOL "" FORCE)
  # slang derives its version from `git describe`, which walks up from the
  # extracted source into Talos's checkout: Talos tags garble or break the
  # version and every Talos commit reruns CMake. Pre-empt slang's gitversion
  # module (by its include guard) to report what a release tarball built outside
  # any repository reports. The check below fails configure if a slang bump stops
  # honoring the override.
  set(__get_git_version YES)
  function(get_git_version _patch _prerelease _hash)
    set(${_patch} 0 PARENT_SCOPE)
    set(${_prerelease} "" PARENT_SCOPE)
    set(${_hash} 0 PARENT_SCOPE)
  endfunction()
  set(SLANG_VERSION_PRERELEASE "" CACHE STRING "" FORCE)
  FetchContent_MakeAvailable(talos_slang)
  get_directory_property(_talos_slang_version_hash
    DIRECTORY "${talos_slang_SOURCE_DIR}" DEFINITION SLANG_VERSION_HASH)
  if(NOT _talos_slang_version_hash STREQUAL "0")
    message(FATAL_ERROR
      "slang no longer uses Talos's get_git_version override (hash "
      "'${_talos_slang_version_hash}'); update it for slang's gitversion module")
  endif()
  set(TALOS_SLANG_SOURCE_DIR "${talos_slang_SOURCE_DIR}")
  message(STATUS
    "Building slang v${TALOS_SLANG_VERSION} from ${TALOS_SLANG_SOURCE_DIR}")
endif()
set(TALOS_SLANG_INCLUDE_DIR "${TALOS_SLANG_SOURCE_DIR}/include")
if(_talos_configure_wasm_support)
  # The standalone runtime compiler needs Slang's canonical IEEE VPI headers.
  # Configure it only after FetchContent has resolved every supported source
  # and base-directory override to the actual materialized source tree.
  include("${PROJECT_SOURCE_DIR}/cmake/TargetWasmSupport.cmake")
endif()
if(_talos_configure_native_support)
  # The standalone runtime compiler needs Slang's canonical IEEE VPI headers.
  # Configure it only after FetchContent has resolved the source tree.
  include("${PROJECT_SOURCE_DIR}/cmake/TargetNativeSupport.cmake")
endif()
if(_talos_configure_windows_support)
  include("${PROJECT_SOURCE_DIR}/cmake/TargetWindowsSupport.cmake")
endif()
if(_talos_configure_macos_support)
  include("${PROJECT_SOURCE_DIR}/cmake/TargetMacOSSupport.cmake")
endif()

#===------------------------------------------------------------------------===
# Language standard. Talos stays at the SDK's C++17 baseline except for the
# isolated slang-facing frontend; upstream slang configures its own C++20 build.
#===------------------------------------------------------------------------===
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(WIN32)
  # Runtime and coverage code use the standard C interfaces on every host.
  # MSVC's alternative secure-CRT interfaces are Windows-specific.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_BINARY_DIR}/include)

if(TALOS_INCLUDE_TESTS)
  find_program(TALOS_LIT_EXECUTABLE NAMES llvm-lit lit)
  if(NOT TALOS_LIT_EXECUTABLE)
    set(_talos_lit_fallback "${PROJECT_SOURCE_DIR}/env/bin/lit")
    if(EXISTS "${_talos_lit_fallback}")
      set(TALOS_LIT_EXECUTABLE "${_talos_lit_fallback}")
    else()
      message(FATAL_ERROR
        "LLVM lit was not found on PATH and the fallback does not exist at "
        "${_talos_lit_fallback}")
    endif()
  endif()
  set(LLVM_EXTERNAL_LIT "${TALOS_LIT_EXECUTABLE}"
      CACHE FILEPATH "LLVM lit runner used by Talos tests" FORCE)
  message(STATUS "Using lit: ${LLVM_EXTERNAL_LIT}")
endif()

add_custom_target(talos-generated-files)
add_subdirectory(utils/talos-tblgen)
add_subdirectory(include)
if((EMSCRIPTEN OR WIN32 OR APPLE) AND TARGET talos_target_runtime)
  # TargetWasmSupport is loaded before the reflection generators are declared.
  # Keep its direct compiler commands behind the imported host-generated files.
  add_dependencies(talos_target_runtime talos-generated-files)
endif()
add_subdirectory(runtime)
add_subdirectory(lib)
add_subdirectory(tools)
add_subdirectory(docs)

if(TALOS_INCLUDE_TESTS)
  add_subdirectory(test)
endif()

if(WIN32)
  # LLVM's Windows packages use a few DLLs. Stage them beside our tools so
  # generated-file commands and tests also work without activating conda.
  function(talos_collect_windows_tools directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(type "${target}" TYPE)
      if(type STREQUAL "EXECUTABLE" OR type STREQUAL "MODULE_LIBRARY")
        set_property(GLOBAL APPEND PROPERTY TALOS_WINDOWS_TOOLS "${target}")
      endif()
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
      talos_collect_windows_tools("${subdirectory}")
    endforeach()
  endfunction()
  talos_collect_windows_tools("${PROJECT_SOURCE_DIR}")
  get_property(windows_tools GLOBAL PROPERTY TALOS_WINDOWS_TOOLS)
  set(windows_tool_directories)
  foreach(target IN LISTS windows_tools)
    list(APPEND windows_tool_directories "$<TARGET_FILE_DIR:${target}>")
  endforeach()
  add_custom_target(talos_windows_runtime_dlls
    COMMAND "${CMAKE_COMMAND}"
      "-DSDK=${TALOS_LLVM_DIST_DIR}"
      "-DDIRECTORIES=${windows_tool_directories}"
      -P "${PROJECT_SOURCE_DIR}/cmake/StageWindowsDLLs.cmake"
    VERBATIM)
  foreach(target IN LISTS windows_tools)
    add_dependencies("${target}" talos_windows_runtime_dlls)
  endforeach()
endif()

message(STATUS "talos configured against prebuilt LLVM + MLIR")
