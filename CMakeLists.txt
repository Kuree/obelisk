cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# FetchContent's base directory contains configured object files in addition to
# downloaded sources. Pointing it into another CMake build tree lets independent
# Ninja processes write the same objects while maintaining different dependency
# databases, which corrupts incremental-build state. A standalone external
# cache remains valid; reject only a base nested in a different configured tree.
cmake_path(ABSOLUTE_PATH FETCHCONTENT_BASE_DIR NORMALIZE
  OUTPUT_VARIABLE _obelisk_fetchcontent_base_dir)
cmake_path(ABSOLUTE_PATH CMAKE_BINARY_DIR NORMALIZE
  OUTPUT_VARIABLE _obelisk_binary_dir)
set(_obelisk_fetchcontent_probe "${_obelisk_fetchcontent_base_dir}")
while(TRUE)
  if("${_obelisk_fetchcontent_probe}" STREQUAL "${_obelisk_binary_dir}")
    break()
  endif()
  if(EXISTS "${_obelisk_fetchcontent_probe}/CMakeCache.txt")
    message(FATAL_ERROR
      "FETCHCONTENT_BASE_DIR (${_obelisk_fetchcontent_base_dir}) is inside "
      "another CMake build tree (${_obelisk_fetchcontent_probe}). Use this "
      "build's own ${CMAKE_BINARY_DIR}/_deps directory or a standalone cache.")
  endif()
  cmake_path(GET _obelisk_fetchcontent_probe PARENT_PATH
    _obelisk_fetchcontent_parent)
  if("${_obelisk_fetchcontent_parent}" STREQUAL
     "${_obelisk_fetchcontent_probe}")
    break()
  endif()
  set(_obelisk_fetchcontent_probe "${_obelisk_fetchcontent_parent}")
endwhile()
unset(_obelisk_fetchcontent_parent)
unset(_obelisk_fetchcontent_probe)
unset(_obelisk_binary_dir)
unset(_obelisk_fetchcontent_base_dir)

option(OBELISK_INCLUDE_TESTS "Build the Obelisk test suites" ON)
option(OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS
  "Report failed bytecode image validation checks" OFF)
option(OBELISK_ENABLE_Z3
  "Use compiler-side Z3 analysis for constrained-random planning" ON)
set(OBELISK_Z3_SOURCE_DIR "" CACHE PATH
  "Extracted Z3 source tree used instead of the pinned archive")
set(OBELISK_Z3_INSTALL_DIR "" CACHE PATH
  "Existing pinned static Z3 installation for the compiler host")

# Op definitions generated from the SystemVerilog AST inventory dominate the
# build's critical path when one translation unit compiles a whole dialect.
# TableGen splits them into this many shards, each its own translation unit.
set(OBELISK_OP_SHARD_COUNT "4" CACHE STRING
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
set(OBELISK_LLVM_VERSION "22.1.6")

# CMAKE_HOST_SYSTEM_PROCESSOR is only set by project().
cmake_host_system_information(RESULT _obelisk_host_processor
  QUERY OS_PLATFORM)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND
   DEFINED ENV{PROCESSOR_ARCHITEW6432})
  # An emulated shell may report its own architecture instead of the host's.
  set(_obelisk_host_processor "$ENV{PROCESSOR_ARCHITEW6432}")
endif()
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND
   _obelisk_host_processor MATCHES "^(aarch64|arm64|ARM64)$")
  # The first conda-forge Windows ARM64 MLIR SDK is 22.1.8.
  set(OBELISK_LLVM_VERSION "22.1.8")
endif()

set(OBELISK_LLVM_INSTALL_DIR "" CACHE PATH
  "Existing matching LLVM + MLIR + Clang + LLD binary SDK")
if(OBELISK_LLVM_INSTALL_DIR)
  get_filename_component(OBELISK_LLVM_DIST_DIR
    "${OBELISK_LLVM_INSTALL_DIR}" ABSOLUTE)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND
       _obelisk_host_processor MATCHES "^(x86_64|amd64|AMD64|aarch64|arm64|ARM64)$" AND
       NOT OBELISK_LLVM_PREBUILT_SHA256)
  # LLVM's official Windows archive omits MLIR. Use one pinned set of
  # upstream conda-forge binaries, including the LLD development libraries.
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/WindowsPrebuiltLLVM.cmake")
endif()

if(NOT OBELISK_LLVM_DIST_DIR)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
   _obelisk_host_processor MATCHES "^(x86_64|amd64|AMD64)$")
  set(_obelisk_llvm_archive_name
      "LLVM-${OBELISK_LLVM_VERSION}-Linux-X64.tar.xz")
  set(_obelisk_llvm_archive_sha256
      "c5ac8ef89ca39d30cb32e9b83772f995dd891c685ebc188d593c943a64d5f8b5")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
       _obelisk_host_processor MATCHES "^(aarch64|arm64|ARM64)$")
  set(_obelisk_llvm_archive_name
      "LLVM-${OBELISK_LLVM_VERSION}-Linux-ARM64.tar.xz")
  set(_obelisk_llvm_archive_sha256
      "b67817634e8e1c2632dfc056af14d61b94f8e6502f4e557560eea227aa22ce37")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
       _obelisk_host_processor MATCHES "^(aarch64|arm64|ARM64)$")
  set(_obelisk_llvm_archive_name
      "LLVM-${OBELISK_LLVM_VERSION}-macOS-ARM64.tar.xz")
  set(_obelisk_llvm_archive_sha256
      "8059d9d9eeb059c30d812b4a37291888f8dcba04d2b5ace61fd12d2904eaa0e9")
else()
  set(_obelisk_llvm_archive_name "")
  set(_obelisk_llvm_archive_sha256 "")
endif()

set(OBELISK_LLVM_PREBUILT_URL
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-${OBELISK_LLVM_VERSION}/${_obelisk_llvm_archive_name}"
    CACHE STRING "Official or local file:// LLVM + MLIR prebuilt archive")
set(OBELISK_LLVM_PREBUILT_SHA256 "${_obelisk_llvm_archive_sha256}"
    CACHE STRING "SHA-256 of OBELISK_LLVM_PREBUILT_URL")

if(NOT OBELISK_LLVM_PREBUILT_URL OR NOT OBELISK_LLVM_PREBUILT_SHA256)
  message(FATAL_ERROR
    "No official LLVM + MLIR v${OBELISK_LLVM_VERSION} archive is known for "
    "${CMAKE_HOST_SYSTEM_NAME}/${_obelisk_host_processor}; set "
    "OBELISK_LLVM_INSTALL_DIR to a matching binary SDK, or set "
    "OBELISK_LLVM_PREBUILT_URL and OBELISK_LLVM_PREBUILT_SHA256")
endif()

if(NOT _obelisk_llvm_archive_name)
  set(_obelisk_llvm_archive_name "llvm-mlir-prebuilt.archive")
endif()

set(_obelisk_llvm_download_dir "${CMAKE_BINARY_DIR}/_downloads")
set(_obelisk_llvm_extract_dir "${CMAKE_BINARY_DIR}/llvm-mlir")
set(_obelisk_llvm_extract_stamp
    "${_obelisk_llvm_extract_dir}/.obelisk-archive-sha256")
set(_obelisk_llvm_archive
    "${_obelisk_llvm_download_dir}/${_obelisk_llvm_archive_name}")
file(MAKE_DIRECTORY "${_obelisk_llvm_download_dir}")
file(DOWNLOAD "${OBELISK_LLVM_PREBUILT_URL}" "${_obelisk_llvm_archive}"
  EXPECTED_HASH "SHA256=${OBELISK_LLVM_PREBUILT_SHA256}"
  STATUS _obelisk_llvm_download_status
  SHOW_PROGRESS
  TLS_VERIFY ON)
list(GET _obelisk_llvm_download_status 0 _obelisk_llvm_download_code)
list(GET _obelisk_llvm_download_status 1 _obelisk_llvm_download_message)
if(NOT _obelisk_llvm_download_code EQUAL 0)
  message(FATAL_ERROR
    "Failed to download the LLVM + MLIR prebuilt archive: "
    "${_obelisk_llvm_download_message}")
endif()

file(GLOB_RECURSE _obelisk_llvm_configs_before
  LIST_DIRECTORIES FALSE
  "${_obelisk_llvm_extract_dir}/LLVMConfig.cmake")
set(_obelisk_llvm_extracted_sha256 "")
if(EXISTS "${_obelisk_llvm_extract_stamp}")
  file(READ "${_obelisk_llvm_extract_stamp}"
       _obelisk_llvm_extracted_sha256)
  string(STRIP "${_obelisk_llvm_extracted_sha256}"
         _obelisk_llvm_extracted_sha256)
endif()
if(NOT _obelisk_llvm_configs_before OR
   NOT _obelisk_llvm_extracted_sha256 STREQUAL
       OBELISK_LLVM_PREBUILT_SHA256)
  file(REMOVE_RECURSE "${_obelisk_llvm_extract_dir}")
  file(MAKE_DIRECTORY "${_obelisk_llvm_extract_dir}")
  file(ARCHIVE_EXTRACT INPUT "${_obelisk_llvm_archive}"
    DESTINATION "${_obelisk_llvm_extract_dir}")
  file(WRITE "${_obelisk_llvm_extract_stamp}"
       "${OBELISK_LLVM_PREBUILT_SHA256}\n")
endif()

file(GLOB_RECURSE _obelisk_llvm_configs
  LIST_DIRECTORIES FALSE
  "${_obelisk_llvm_extract_dir}/LLVMConfig.cmake")
set(_obelisk_llvm_package_config "")
foreach(config_file IN LISTS _obelisk_llvm_configs)
  if(config_file MATCHES "/lib/cmake/llvm/LLVMConfig\\.cmake$")
    set(_obelisk_llvm_package_config "${config_file}")
    break()
  endif()
endforeach()
if(NOT _obelisk_llvm_package_config)
  message(FATAL_ERROR
    "The downloaded prebuilt archive contains no LLVM CMake package")
endif()
get_filename_component(_obelisk_llvm_cmake_dir
  "${_obelisk_llvm_package_config}" DIRECTORY)
get_filename_component(OBELISK_LLVM_DIST_DIR
  "${_obelisk_llvm_cmake_dir}/../../.." ABSOLUTE)
endif()

if(NOT EXISTS "${OBELISK_LLVM_DIST_DIR}/lib/cmake/llvm/LLVMConfig.cmake" OR
   NOT EXISTS "${OBELISK_LLVM_DIST_DIR}/lib/cmake/mlir/MLIRConfig.cmake")
  message(FATAL_ERROR
    "The prebuilt distribution at ${OBELISK_LLVM_DIST_DIR} does not "
    "contain both LLVM and MLIR CMake packages")
endif()

# Compile with the SDK's clang unless a compiler was chosen with
# CMAKE_<LANG>_COMPILER, CC/CXX or a toolchain file. GCC takes about twice as
# long per translation unit on this code base. An existing build directory
# keeps the compiler it was first configured with, since CMake caches it.
option(OBELISK_USE_SDK_CLANG
  "Compile with the prebuilt LLVM distribution's clang unless a compiler is chosen"
  ON)
set(_obelisk_host_executable_suffix "")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(_obelisk_host_executable_suffix ".exe")
endif()
set(_obelisk_sdk_clang "${OBELISK_LLVM_DIST_DIR}/bin/clang")
set(_obelisk_sdk_cxx "${_obelisk_sdk_clang}++${_obelisk_host_executable_suffix}")
set(_obelisk_probe_flags)
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
  set(_obelisk_macos_sdk "${CMAKE_OSX_SYSROOT}")
  if(NOT IS_DIRECTORY "${_obelisk_macos_sdk}")
    set(_obelisk_sdk_name macosx)
    if(CMAKE_OSX_SYSROOT)
      set(_obelisk_sdk_name "${CMAKE_OSX_SYSROOT}")
    endif()
    execute_process(COMMAND xcrun --sdk "${_obelisk_sdk_name}" --show-sdk-path
      RESULT_VARIABLE _obelisk_xcrun_result
      OUTPUT_VARIABLE _obelisk_macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _obelisk_xcrun_result EQUAL 0 OR
       NOT IS_DIRECTORY "${_obelisk_macos_sdk}")
      message(FATAL_ERROR
        "Building Obelisk on macOS requires Xcode or the Command Line Tools")
    endif()
    set(CMAKE_OSX_SYSROOT "${_obelisk_macos_sdk}" CACHE PATH "macOS SDK")
  endif()
  set(OBELISK_HOST_CXX_STDLIB_FLAGS
    "-stdlib++-isystem ${_obelisk_macos_sdk}/usr/include/c++/v1")
  set(_obelisk_probe_flags -isysroot "${_obelisk_macos_sdk}"
    "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}"
    -stdlib++-isystem "${_obelisk_macos_sdk}/usr/include/c++/v1")
endif()
if(OBELISK_USE_SDK_CLANG AND
   CMAKE_HOST_SYSTEM_NAME MATCHES "^(Linux|Windows|Darwin)$" AND
   NOT CMAKE_TOOLCHAIN_FILE AND
   NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED CMAKE_CXX_COMPILER AND
   NOT DEFINED ENV{CC} AND NOT DEFINED ENV{CXX} AND
   EXISTS "${_obelisk_sdk_cxx}")
  # The SDK's Clang uses the host's C++ library and C runtime. Verify that
  # their headers and libraries are installed before selecting the compiler.
  set(_obelisk_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/ObeliskSDKClangProbe")
  file(MAKE_DIRECTORY "${_obelisk_probe_dir}")
  file(WRITE "${_obelisk_probe_dir}/probe.cpp"
    "#include <string>\n"
    "int main() { return static_cast<int>(std::string(\"a\").size()) - 1; }\n")
  execute_process(
    COMMAND "${_obelisk_sdk_cxx}" ${_obelisk_probe_flags}
      "${_obelisk_probe_dir}/probe.cpp"
      -o "${_obelisk_probe_dir}/probe${_obelisk_host_executable_suffix}"
    RESULT_VARIABLE _obelisk_probe_result
    OUTPUT_QUIET
    ERROR_VARIABLE _obelisk_probe_error)
  if(_obelisk_probe_result EQUAL 0)
    set(CMAKE_C_COMPILER "${_obelisk_sdk_clang}${_obelisk_host_executable_suffix}"
      CACHE FILEPATH "C compiler")
    set(CMAKE_CXX_COMPILER "${_obelisk_sdk_cxx}" CACHE FILEPATH
      "C++ compiler")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR
      "The SDK's Clang cannot build against this host's C++ library. "
      "Install Visual Studio C++ build tools and a Windows SDK:\n${_obelisk_probe_error}")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR
      "The SDK's Clang cannot build against the macOS SDK:\n${_obelisk_probe_error}")
  else()
    message(STATUS
      "The SDK's clang cannot build against this host's C++ library; using "
      "the default compiler instead:\n${_obelisk_probe_error}")
  endif()
  unset(_obelisk_probe_dir)
  unset(_obelisk_probe_result)
  unset(_obelisk_probe_error)
endif()
unset(_obelisk_sdk_clang)
unset(_obelisk_sdk_cxx)
unset(_obelisk_probe_flags)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_LINKER "${OBELISK_LLVM_DIST_DIR}/bin/lld-link.exe"
    CACHE FILEPATH "SDK COFF linker")
endif()

project(obelisk
  VERSION 0.1.0
  DESCRIPTION "An AOT SystemVerilog simulator targeting IEEE 1800 conformance and real UVM"
  LANGUAGES C CXX)

set(MLIR_DIR "${OBELISK_LLVM_DIST_DIR}/lib/cmake/mlir")
set(LLVM_DIR "${OBELISK_LLVM_DIST_DIR}/lib/cmake/llvm")
list(PREPEND CMAKE_PREFIX_PATH "${OBELISK_LLVM_DIST_DIR}")
if(WIN32 AND NOT DEFINED ENV{VSINSTALLDIR})
  find_program(_obelisk_vswhere vswhere
    HINTS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer")
  if(_obelisk_vswhere)
    execute_process(COMMAND "${_obelisk_vswhere}" -latest -products "*"
      -property installationPath
      OUTPUT_VARIABLE _obelisk_visual_studio OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_obelisk_visual_studio)
      set(ENV{VSINSTALLDIR} "${_obelisk_visual_studio}")
    endif()
  endif()
endif()

find_package(LLVM REQUIRED CONFIG)
find_package(MLIR REQUIRED CONFIG)
set(LLD_DIR "${OBELISK_LLVM_DIST_DIR}/lib/cmake/lld")
find_package(LLD REQUIRED CONFIG)
if(NOT LLVM_PACKAGE_VERSION VERSION_EQUAL OBELISK_LLVM_VERSION)
  message(FATAL_ERROR
    "The binary SDK must provide LLVM ${OBELISK_LLVM_VERSION}, got ${LLVM_PACKAGE_VERSION}")
endif()
if(WIN32)
  set(OBELISK_TARGET_TRIPLE "${LLVM_HOST_TRIPLE}" CACHE STRING
    "Native code-generation target triple")
  include("${PROJECT_SOURCE_DIR}/cmake/WindowsTarget.cmake")
  obelisk_windows_target("${LLVM_HOST_TRIPLE}" OBELISK_WINDOWS_HOST)
  obelisk_windows_target("${OBELISK_TARGET_TRIPLE}" OBELISK_WINDOWS_TARGET)
  if(NOT OBELISK_WINDOWS_HOST_COFF_MACHINE STREQUAL
         OBELISK_WINDOWS_TARGET_COFF_MACHINE)
    message(FATAL_ERROR
      "Native target ${OBELISK_TARGET_TRIPLE} must match the SDK host ${LLVM_HOST_TRIPLE}")
  endif()
  execute_process(
    COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/clang.exe"
      --target=${OBELISK_TARGET_TRIPLE} --rtlib=compiler-rt -print-libgcc-file-name
    RESULT_VARIABLE _obelisk_builtins_status
    OUTPUT_VARIABLE OBELISK_COMPILER_RT_BUILTINS
    ERROR_VARIABLE _obelisk_builtins_diagnostic OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _obelisk_builtins_status EQUAL 0 OR
     NOT EXISTS "${OBELISK_COMPILER_RT_BUILTINS}")
    message(FATAL_ERROR
      "The SDK has no compiler-rt builtins for ${OBELISK_TARGET_TRIPLE}: "
      "${OBELISK_COMPILER_RT_BUILTINS} ${_obelisk_builtins_diagnostic}")
  endif()
endif()

message(STATUS "Using prebuilt LLVM ${LLVM_PACKAGE_VERSION} + MLIR")
message(STATUS "  LLVM package = ${LLVM_DIR}")
message(STATUS "  MLIR package = ${MLIR_DIR}")
message(STATUS "  LLD package = ${LLD_DIR}")

if(NOT EXISTS "${OBELISK_LLVM_DIST_DIR}/bin/llvm-tblgen${_obelisk_host_executable_suffix}" OR
   NOT EXISTS "${OBELISK_LLVM_DIST_DIR}/bin/mlir-tblgen${_obelisk_host_executable_suffix}")
  message(FATAL_ERROR
    "The prebuilt LLVM + MLIR distribution is missing TableGen tools")
endif()
set(OBELISK_LLVM_TOOLS_DIR "${OBELISK_LLVM_DIST_DIR}/bin" CACHE INTERNAL
    "Matching prebuilt LLVM/MLIR tool directory" FORCE)
if(WIN32)
  include("${PROJECT_SOURCE_DIR}/cmake/WindowsSDKSupport.cmake")
endif()

# Keep generators and libraries on the same prebuilt distribution even when a
# build directory was previously configured against another SDK.
set(LLVM_TABLEGEN_EXE "${OBELISK_LLVM_TOOLS_DIR}/llvm-tblgen${_obelisk_host_executable_suffix}"
    CACHE FILEPATH "Matching LLVM TableGen executable" FORCE)
set(MLIR_TABLEGEN_EXE "${OBELISK_LLVM_TOOLS_DIR}/mlir-tblgen${_obelisk_host_executable_suffix}"
    CACHE FILEPATH "Matching MLIR TableGen executable" FORCE)

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")

# The macOS SDK ships its libraries as ThinLTO bitcode, which only its own LLD
# can link. Select it before LLVM's options detect the linker, and share one
# ThinLTO cache across the host executables.
if(APPLE AND OBELISK_USE_SDK_LLD AND NOT LLVM_USE_LINKER AND
   EXISTS "${OBELISK_LLVM_TOOLS_DIR}/ld64.lld")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(LLVM_USE_LINKER lld)
  else()
    set(LLVM_USE_LINKER "${OBELISK_LLVM_TOOLS_DIR}/ld64.lld")
  endif()
  set(OBELISK_HOST_LTO_CACHE_DIR "${CMAKE_BINARY_DIR}/lto.cache" CACHE PATH
    "ThinLTO cache for host links of the macOS SDK's bitcode libraries")
  foreach(flags CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS
                CMAKE_MODULE_LINKER_FLAGS)
    string(APPEND ${flags} " -Wl,-cache_path_lto,${OBELISK_HOST_LTO_CACHE_DIR}")
  endforeach()
  message(STATUS "Host linker: ${OBELISK_LLVM_TOOLS_DIR}/ld64.lld")
endif()
# Upstream Clang would otherwise use the SDK's bundled libc++ headers.
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
   OBELISK_HOST_CXX_STDLIB_FLAGS)
  string(APPEND CMAKE_CXX_FLAGS " ${OBELISK_HOST_CXX_STDLIB_FLAGS}")
endif()

include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(HandleLLVMOptions)
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
option(OBELISK_USE_SDK_LLD
  "Link host executables with the prebuilt LLVM distribution's lld" ON)
if(OBELISK_USE_SDK_LLD AND NOT LLVM_USE_LINKER AND NOT EMSCRIPTEN AND
   CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   EXISTS "${OBELISK_LLVM_TOOLS_DIR}/ld.lld")
  set(_obelisk_lld_flags "")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC's -fuse-ld=lld runs the first ld.lld on its program search path. -B
    # prepends a directory holding only the SDK's ld.lld, so GCC still finds
    # its own assembler and collect2.
    set(_obelisk_lld_dir "${CMAKE_BINARY_DIR}/host-linker")
    file(MAKE_DIRECTORY "${_obelisk_lld_dir}")
    file(CREATE_LINK "${OBELISK_LLVM_TOOLS_DIR}/ld.lld"
      "${_obelisk_lld_dir}/ld.lld" SYMBOLIC)
    set(_obelisk_lld_flags "-fuse-ld=lld -B${_obelisk_lld_dir}/")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(_obelisk_lld_flags "--ld-path=${OBELISK_LLVM_TOOLS_DIR}/ld.lld")
  endif()
  if(_obelisk_lld_flags)
    foreach(flags CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS
                  CMAKE_MODULE_LINKER_FLAGS)
      string(APPEND ${flags} " ${_obelisk_lld_flags}")
    endforeach()
    message(STATUS "Host linker: ${OBELISK_LLVM_TOOLS_DIR}/ld.lld")
  endif()
  unset(_obelisk_lld_flags)
  unset(_obelisk_lld_dir)
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
  set(_obelisk_configure_wasm_support TRUE)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  set(OBELISK_NATIVE_PLATFORM "linux")
  set(_obelisk_configure_native_support TRUE)
elseif(APPLE)
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" OR
     (CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"))
    message(FATAL_ERROR "The macOS native backend requires an ARM64 host and build")
  endif()
  set(OBELISK_NATIVE_PLATFORM "macos")
  set(_obelisk_configure_macos_support TRUE)
elseif(WIN32)
  set(OBELISK_NATIVE_PLATFORM "windows")
  set(_obelisk_configure_windows_support TRUE)
else()
  message(FATAL_ERROR
    "native executable generation requires a Linux host, or "
    "configure with emcmake to target wasm32")
endif()

#===------------------------------------------------------------------------===
# Upstream slang semantic-AST frontend.
#
# Build the latest upstream release as part of Obelisk instead of using a
# compiler SDK's private copy. Offline builds can point
# OBELISK_SLANG_SOURCE_DIR at an extracted copy of this exact release.
#===------------------------------------------------------------------------===
set(OBELISK_SLANG_VERSION "11.0")
set(OBELISK_SLANG_ARCHIVE_SHA256
    "50676d5a9adbefb97d266a4b174e6b0513901afd5ac57a6cdfea0a61149c3704")
set(OBELISK_SLANG_SOURCE_DIR "" CACHE PATH
    "Extracted slang v${OBELISK_SLANG_VERSION} source tree (offline builds)")

# Z3 is a host-side compiler dependency only. Build a pinned static library
# from source so the compiler does not inherit the glibc floor of Z3's release
# binaries, and keep the runtime dependency-free. The source override supports
# fully offline builds without weakening the archive checksum path.
if(OBELISK_ENABLE_Z3)
  include(ExternalProject)
  set(OBELISK_Z3_VERSION "4.13.4")
  set(OBELISK_Z3_ARCHIVE_SHA256
      "4071977e66e9f3d239b7b098ceddfe62ffdf3c71e345e9524a4a5001d1f4adf3")
  set(_obelisk_z3_library_name "libz3.a")
  if(WIN32)
    set(_obelisk_z3_library_name "z3.lib")
  endif()
  set(_obelisk_z3_install "${CMAKE_BINARY_DIR}/z3-${OBELISK_Z3_VERSION}")
  if(EMSCRIPTEN)
    set(_obelisk_z3_single_threaded ON)
  else()
    set(_obelisk_z3_single_threaded OFF)
  endif()
  set(_obelisk_z3_cmake_args
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
      -DZ3_SINGLE_THREADED=${_obelisk_z3_single_threaded}
      -DZ3_POLLING_TIMER=ON)
  # Preserve cross-compilation, including Emscripten, across the isolated
  # nested configure. Native compilers use Z3's thread-safe build so independent
  # MLIR workers can solve concurrently. The wasm compiler retains
  # single-threaded polling mode because it has no pthread runtime.
  if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND _obelisk_z3_cmake_args
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
  endif()
  if(EMSCRIPTEN)
    # The toolchain selects em++, but it does not carry the outer project's
    # exception ABI flags into ExternalProject's isolated cache. Z3 is linked
    # into the wasm compiler, so its objects must match.
    list(APPEND _obelisk_z3_cmake_args
      "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
      "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}")
  endif()
  if(CMAKE_CXX_COMPILER_TARGET)
    list(APPEND _obelisk_z3_cmake_args
      "-DCMAKE_CXX_COMPILER_TARGET=${CMAKE_CXX_COMPILER_TARGET}")
  endif()
  # Build Z3 for the outer macOS target. Its own default is the SDK version,
  # which makes the linker warn about every Z3 object.
  if(APPLE)
    foreach(variable IN ITEMS CMAKE_OSX_DEPLOYMENT_TARGET
                              CMAKE_OSX_ARCHITECTURES CMAKE_OSX_SYSROOT)
      if(${variable})
        list(APPEND _obelisk_z3_cmake_args "-D${variable}=${${variable}}")
      endif()
    endforeach()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND OBELISK_HOST_CXX_STDLIB_FLAGS)
      list(APPEND _obelisk_z3_cmake_args
        "-DCMAKE_CXX_FLAGS=${OBELISK_HOST_CXX_STDLIB_FLAGS}")
    endif()
  endif()
  # Z3 is by far the largest nested build, so a compiler cache the outer build
  # opted into has to reach it too; the isolated configure would otherwise miss
  # it silently. Purely a pass-through: unset by default, so ordinary builds
  # are unaffected. Z3 declares LANGUAGES CXX, so only the C++ launcher applies.
  if(CMAKE_CXX_COMPILER_LAUNCHER)
    list(APPEND _obelisk_z3_cmake_args
      "-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")
  endif()
  if(OBELISK_Z3_INSTALL_DIR)
    set(_obelisk_z3_install "${OBELISK_Z3_INSTALL_DIR}")
    if(NOT EXISTS "${_obelisk_z3_install}/lib/${_obelisk_z3_library_name}" OR
       NOT EXISTS "${_obelisk_z3_install}/include/z3.h" OR
       NOT EXISTS "${_obelisk_z3_install}/include/z3_version.h")
      message(FATAL_ERROR
        "OBELISK_Z3_INSTALL_DIR must contain pinned static Z3 headers and lib/libz3.a")
    endif()
    file(STRINGS "${_obelisk_z3_install}/include/z3_version.h"
      _obelisk_z3_installed_version
      REGEX "^#define Z3_FULL_VERSION[ \t]+\"")
    string(REGEX REPLACE "^#define Z3_FULL_VERSION[ \t]+\"([^\"]+)\"$" "\\1"
      _obelisk_z3_installed_version "${_obelisk_z3_installed_version}")
    if(NOT _obelisk_z3_installed_version STREQUAL "${OBELISK_Z3_VERSION}.0")
      message(FATAL_ERROR
        "OBELISK_Z3_INSTALL_DIR must contain Z3 ${OBELISK_Z3_VERSION}.0")
    endif()
    # Keep the dependency used by the solver's object library without running
    # a second ExternalProject against the shared installation.
    add_custom_target(obelisk_z3_project)
  elseif(OBELISK_Z3_SOURCE_DIR)
    file(STRINGS "${OBELISK_Z3_SOURCE_DIR}/CMakeLists.txt"
      _obelisk_z3_project_line
      REGEX "^project\\(Z3 VERSION ${OBELISK_Z3_VERSION}\\.0 LANGUAGES CXX\\)$")
    if(NOT _obelisk_z3_project_line)
      message(FATAL_ERROR
        "OBELISK_Z3_SOURCE_DIR must contain Z3 ${OBELISK_Z3_VERSION}")
    endif()
    ExternalProject_Add(obelisk_z3_project
      SOURCE_DIR "${OBELISK_Z3_SOURCE_DIR}"
      DOWNLOAD_COMMAND ""
      EXCLUDE_FROM_ALL TRUE
      CMAKE_ARGS ${_obelisk_z3_cmake_args}
      INSTALL_DIR "${_obelisk_z3_install}"
      BUILD_BYPRODUCTS "${_obelisk_z3_install}/lib/${_obelisk_z3_library_name}")
  else()
    ExternalProject_Add(obelisk_z3_project
      URL "https://github.com/Z3Prover/z3/archive/refs/tags/z3-${OBELISK_Z3_VERSION}.tar.gz"
      URL_HASH "SHA256=${OBELISK_Z3_ARCHIVE_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL TRUE
      CMAKE_ARGS ${_obelisk_z3_cmake_args}
      INSTALL_DIR "${_obelisk_z3_install}"
      BUILD_BYPRODUCTS "${_obelisk_z3_install}/lib/${_obelisk_z3_library_name}")
  endif()
  if(NOT OBELISK_Z3_INSTALL_DIR)
    file(MAKE_DIRECTORY "${_obelisk_z3_install}/include")
  endif()
  add_library(ObeliskZ3 STATIC IMPORTED GLOBAL)
  set_target_properties(ObeliskZ3 PROPERTIES
    IMPORTED_LOCATION "${_obelisk_z3_install}/lib/${_obelisk_z3_library_name}"
    INTERFACE_INCLUDE_DIRECTORIES "${_obelisk_z3_install}/include")
  if(EMSCRIPTEN)
    # The solver benefits from a growable compiler heap. Exception handling is
    # already fixed globally to -fexceptions, which is what Z3 needs; it throws
    # far more than the runtime does.
    target_link_options(ObeliskZ3 INTERFACE
      "SHELL:-s ALLOW_MEMORY_GROWTH=1")
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(ObeliskZ3 INTERFACE Threads::Threads)
  endif()
  if(OBELISK_Z3_INSTALL_DIR)
    message(STATUS "Reusing compiler-only Z3 ${OBELISK_Z3_VERSION} at ${_obelisk_z3_install}")
  else()
    add_dependencies(ObeliskZ3 obelisk_z3_project)
    message(STATUS "Building compiler-only Z3 ${OBELISK_Z3_VERSION} from source")
  endif()
endif()

# slang v11.0 declares these dependencies by git tag. Declare checksummed
# release archives first (FetchContent's first declaration wins) so Obelisk's
# dependency graph is immutable and works without git.
find_package(Python 3.8 COMPONENTS Interpreter REQUIRED)
FetchContent_Declare(fmt
  URL "https://github.com/fmtlib/fmt/archive/refs/tags/12.1.0.tar.gz"
  URL_HASH
    "SHA256=ea7de4299689e12b6dddd392f9896f08fb0777ac7168897a244a6d6085043fea"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
if(OBELISK_SLANG_SOURCE_DIR)
  FetchContent_Declare(obelisk_slang
    SOURCE_DIR "${OBELISK_SLANG_SOURCE_DIR}")
else()
  FetchContent_Declare(obelisk_slang
    URL "https://github.com/MikePopoloski/slang/archive/refs/tags/v${OBELISK_SLANG_VERSION}.tar.gz"
    URL_HASH "SHA256=${OBELISK_SLANG_ARCHIVE_SHA256}"
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
# extracted source into Obelisk's checkout: Obelisk tags garble or break the
# version and every Obelisk commit reruns CMake. Pre-empt slang's gitversion
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
FetchContent_MakeAvailable(obelisk_slang)
get_directory_property(_obelisk_slang_version_hash
  DIRECTORY "${obelisk_slang_SOURCE_DIR}" DEFINITION SLANG_VERSION_HASH)
if(NOT _obelisk_slang_version_hash STREQUAL "0")
  message(FATAL_ERROR
    "slang no longer uses Obelisk's get_git_version override (hash "
    "'${_obelisk_slang_version_hash}'); update it for slang's gitversion module")
endif()
set(OBELISK_SLANG_SOURCE_DIR "${obelisk_slang_SOURCE_DIR}")
set(OBELISK_SLANG_INCLUDE_DIR "${OBELISK_SLANG_SOURCE_DIR}/include")
message(STATUS
  "Building slang v${OBELISK_SLANG_VERSION} from ${OBELISK_SLANG_SOURCE_DIR}")
if(_obelisk_configure_wasm_support)
  # The standalone runtime compiler needs Slang's canonical IEEE VPI headers.
  # Configure it only after FetchContent has resolved every supported source
  # and base-directory override to the actual materialized source tree.
  include("${PROJECT_SOURCE_DIR}/cmake/TargetWasmSupport.cmake")
endif()
if(_obelisk_configure_native_support)
  # The standalone runtime compiler needs Slang's canonical IEEE VPI headers.
  # Configure it only after FetchContent has resolved the source tree.
  include("${PROJECT_SOURCE_DIR}/cmake/TargetNativeSupport.cmake")
endif()
if(_obelisk_configure_windows_support)
  include("${PROJECT_SOURCE_DIR}/cmake/TargetWindowsSupport.cmake")
endif()
if(_obelisk_configure_macos_support)
  include("${PROJECT_SOURCE_DIR}/cmake/TargetMacOSSupport.cmake")
endif()

#===------------------------------------------------------------------------===
# Language standard. Obelisk stays at the SDK's C++17 baseline except for the
# isolated slang-facing frontend; upstream slang configures its own C++20 build.
#===------------------------------------------------------------------------===
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_BINARY_DIR}/include)

if(OBELISK_INCLUDE_TESTS)
  find_program(OBELISK_LIT_EXECUTABLE NAMES llvm-lit lit)
  if(NOT OBELISK_LIT_EXECUTABLE)
    set(_obelisk_lit_fallback "${PROJECT_SOURCE_DIR}/env/bin/lit")
    if(EXISTS "${_obelisk_lit_fallback}")
      set(OBELISK_LIT_EXECUTABLE "${_obelisk_lit_fallback}")
    else()
      message(FATAL_ERROR
        "LLVM lit was not found on PATH and the fallback does not exist at "
        "${_obelisk_lit_fallback}")
    endif()
  endif()
  set(LLVM_EXTERNAL_LIT "${OBELISK_LIT_EXECUTABLE}"
      CACHE FILEPATH "LLVM lit runner used by Obelisk tests" FORCE)
  message(STATUS "Using lit: ${LLVM_EXTERNAL_LIT}")
endif()

add_custom_target(obelisk-generated-files)
add_subdirectory(utils/obelisk-tblgen)
add_subdirectory(include)
if((EMSCRIPTEN OR WIN32 OR APPLE) AND TARGET obelisk_target_runtime)
  # TargetWasmSupport is loaded before the reflection generators are declared.
  # Keep its direct compiler commands behind the imported host-generated files.
  add_dependencies(obelisk_target_runtime obelisk-generated-files)
endif()
add_subdirectory(runtime)
add_subdirectory(lib)
add_subdirectory(tools)

if(OBELISK_INCLUDE_TESTS)
  add_subdirectory(test)
endif()

if(WIN32)
  # LLVM's Windows packages use a few DLLs. Stage them beside our tools so
  # generated-file commands and tests also work without activating conda.
  function(obelisk_collect_windows_tools directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(type "${target}" TYPE)
      if(type STREQUAL "EXECUTABLE" OR type STREQUAL "MODULE_LIBRARY")
        set_property(GLOBAL APPEND PROPERTY OBELISK_WINDOWS_TOOLS "${target}")
      endif()
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
      obelisk_collect_windows_tools("${subdirectory}")
    endforeach()
  endfunction()
  obelisk_collect_windows_tools("${PROJECT_SOURCE_DIR}")
  get_property(windows_tools GLOBAL PROPERTY OBELISK_WINDOWS_TOOLS)
  set(windows_tool_directories)
  foreach(target IN LISTS windows_tools)
    list(APPEND windows_tool_directories "$<TARGET_FILE_DIR:${target}>")
  endforeach()
  add_custom_target(obelisk_windows_runtime_dlls
    COMMAND "${CMAKE_COMMAND}"
      "-DSDK=${OBELISK_LLVM_DIST_DIR}"
      "-DDIRECTORIES=${windows_tool_directories}"
      -P "${PROJECT_SOURCE_DIR}/cmake/StageWindowsDLLs.cmake"
    VERBATIM)
  foreach(target IN LISTS windows_tools)
    add_dependencies("${target}" obelisk_windows_runtime_dlls)
  endforeach()
endif()

message(STATUS "obelisk configured against prebuilt LLVM + MLIR")
