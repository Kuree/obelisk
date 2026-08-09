cmake_minimum_required(VERSION 3.20)

project(obelisk
  VERSION 0.1.0
  DESCRIPTION "An AOT SystemVerilog simulator targeting IEEE 1800 conformance and real UVM"
  LANGUAGES C CXX)

option(OBELISK_INCLUDE_TESTS "Build the Obelisk test suites" ON)
option(OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS
  "Report failed bytecode image validation checks" OFF)
option(OBELISK_ENABLE_Z3
  "Use compiler-side Z3 analysis for constrained-random planning" ON)
set(OBELISK_Z3_SOURCE_DIR "" CACHE PATH
  "Extracted Z3 source tree used instead of the pinned archive")

# Op definitions generated from the SystemVerilog AST inventory dominate the
# build's critical path when one translation unit compiles a whole dialect.
# TableGen splits them into this many shards, each its own translation unit.
set(OBELISK_OP_SHARD_COUNT "4" CACHE STRING
  "Translation units used for each dialect's generated op definitions")

#===------------------------------------------------------------------------===
# Toolchain: a CMake-managed prebuilt LLVM + MLIR release SDK.
#
# We deliberately do not build LLVM or MLIR from source and do not consume any
# downstream compiler package. The selected binary SDK must contain matching
# LLVM and MLIR headers, libraries, TableGen tools, and CMake package files.
#
# The official archive is downloaded and extracted below CMAKE_BINARY_DIR. A
# local file:// URL provides a fully offline path while keeping the extracted
# distribution owned by the build tree.
#===------------------------------------------------------------------------===
set(OBELISK_LLVM_VERSION "22.1.6")

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set(_obelisk_llvm_archive_name
      "LLVM-${OBELISK_LLVM_VERSION}-Linux-X64.tar.xz")
  set(_obelisk_llvm_archive_sha256
      "c5ac8ef89ca39d30cb32e9b83772f995dd891c685ebc188d593c943a64d5f8b5")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
  set(_obelisk_llvm_archive_name
      "LLVM-${OBELISK_LLVM_VERSION}-Linux-ARM64.tar.xz")
  set(_obelisk_llvm_archive_sha256
      "b67817634e8e1c2632dfc056af14d61b94f8e6502f4e557560eea227aa22ce37")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
       CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
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
    "${CMAKE_HOST_SYSTEM_NAME}/${CMAKE_HOST_SYSTEM_PROCESSOR}; set "
    "OBELISK_LLVM_PREBUILT_URL and OBELISK_LLVM_PREBUILT_SHA256")
endif()

if(NOT _obelisk_llvm_archive_name)
  set(_obelisk_llvm_archive_name "llvm-mlir-prebuilt.archive")
endif()

set(_obelisk_llvm_download_dir "${CMAKE_BINARY_DIR}/_downloads")
set(_obelisk_llvm_extract_dir "${CMAKE_BINARY_DIR}/llvm-mlir")
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
if(NOT _obelisk_llvm_configs_before)
  file(REMOVE_RECURSE "${_obelisk_llvm_extract_dir}")
  file(MAKE_DIRECTORY "${_obelisk_llvm_extract_dir}")
  file(ARCHIVE_EXTRACT INPUT "${_obelisk_llvm_archive}"
    DESTINATION "${_obelisk_llvm_extract_dir}")
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

if(NOT EXISTS "${OBELISK_LLVM_DIST_DIR}/lib/cmake/llvm/LLVMConfig.cmake" OR
   NOT EXISTS "${OBELISK_LLVM_DIST_DIR}/lib/cmake/mlir/MLIRConfig.cmake")
  message(FATAL_ERROR
    "The prebuilt distribution at ${OBELISK_LLVM_DIST_DIR} does not "
    "contain both LLVM and MLIR CMake packages")
endif()

set(MLIR_DIR "${OBELISK_LLVM_DIST_DIR}/lib/cmake/mlir")
set(LLVM_DIR "${OBELISK_LLVM_DIST_DIR}/lib/cmake/llvm")

find_package(LLVM REQUIRED CONFIG)
find_package(MLIR REQUIRED CONFIG)
set(LLD_DIR "${OBELISK_LLVM_DIST_DIR}/lib/cmake/lld")
find_package(LLD REQUIRED CONFIG)

message(STATUS "Using prebuilt LLVM ${LLVM_PACKAGE_VERSION} + MLIR")
message(STATUS "  LLVM package = ${LLVM_DIR}")
message(STATUS "  MLIR package = ${MLIR_DIR}")
message(STATUS "  LLD package = ${LLD_DIR}")

if(NOT EXISTS "${OBELISK_LLVM_DIST_DIR}/bin/llvm-tblgen" OR
   NOT EXISTS "${OBELISK_LLVM_DIST_DIR}/bin/mlir-tblgen")
  message(FATAL_ERROR
    "The prebuilt LLVM + MLIR distribution is missing TableGen tools")
endif()
set(OBELISK_LLVM_TOOLS_DIR "${OBELISK_LLVM_DIST_DIR}/bin" CACHE INTERNAL
    "Matching prebuilt LLVM/MLIR tool directory" FORCE)

# Keep generators and libraries on the same prebuilt distribution even when a
# build directory was previously configured against another SDK.
set(LLVM_TABLEGEN_EXE "${OBELISK_LLVM_TOOLS_DIR}/llvm-tblgen"
    CACHE FILEPATH "Matching LLVM TableGen executable" FORCE)
set(MLIR_TABLEGEN_EXE "${OBELISK_LLVM_TOOLS_DIR}/mlir-tblgen"
    CACHE FILEPATH "Matching MLIR TableGen executable" FORCE)

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")

include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(HandleLLVMOptions)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  include("${PROJECT_SOURCE_DIR}/cmake/TargetNativeSupport.cmake")
else()
  message(FATAL_ERROR
    "native executable generation currently requires an x86-64 Linux host")
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

include(FetchContent)

# Z3 is a host-side compiler dependency only. Build a pinned static library
# from source so the compiler does not inherit the glibc floor of Z3's release
# binaries, and keep the runtime dependency-free. The source override supports
# fully offline builds without weakening the archive checksum path.
if(OBELISK_ENABLE_Z3)
  include(ExternalProject)
  set(OBELISK_Z3_VERSION "4.13.4")
  set(OBELISK_Z3_ARCHIVE_SHA256
      "4071977e66e9f3d239b7b098ceddfe62ffdf3c71e345e9524a4a5001d1f4adf3")
  set(_obelisk_z3_install "${CMAKE_BINARY_DIR}/z3-${OBELISK_Z3_VERSION}")
  set(_obelisk_z3_cmake_args
      -DCMAKE_BUILD_TYPE=Release
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
      -DZ3_SINGLE_THREADED=ON
      -DZ3_POLLING_TIMER=ON)
  # Preserve cross-compilation, including Emscripten, across the isolated
  # nested configure. Z3's single-threaded polling mode avoids timer threads
  # and does not require a pthread-enabled WebAssembly runtime.
  if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND _obelisk_z3_cmake_args
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
  endif()
  if(CMAKE_CXX_COMPILER_TARGET)
    list(APPEND _obelisk_z3_cmake_args
      "-DCMAKE_CXX_COMPILER_TARGET=${CMAKE_CXX_COMPILER_TARGET}")
  endif()
  if(OBELISK_Z3_SOURCE_DIR)
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
      BUILD_BYPRODUCTS "${_obelisk_z3_install}/lib/libz3.a")
  else()
    ExternalProject_Add(obelisk_z3_project
      URL "https://github.com/Z3Prover/z3/archive/refs/tags/z3-${OBELISK_Z3_VERSION}.tar.gz"
      URL_HASH "SHA256=${OBELISK_Z3_ARCHIVE_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL TRUE
      CMAKE_ARGS ${_obelisk_z3_cmake_args}
      INSTALL_DIR "${_obelisk_z3_install}"
      BUILD_BYPRODUCTS "${_obelisk_z3_install}/lib/libz3.a")
  endif()
  file(MAKE_DIRECTORY "${_obelisk_z3_install}/include")
  add_library(ObeliskZ3 STATIC IMPORTED GLOBAL)
  set_target_properties(ObeliskZ3 PROPERTIES
    IMPORTED_LOCATION "${_obelisk_z3_install}/lib/libz3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_obelisk_z3_install}/include")
  if(EMSCRIPTEN)
    # Match Z3's own Emscripten consumer requirements. The solver uses C++
    # exceptions internally and benefits from a growable compiler heap.
    target_link_options(ObeliskZ3 INTERFACE
      "SHELL:-s DISABLE_EXCEPTION_CATCHING=0"
      "SHELL:-s ALLOW_MEMORY_GROWTH=1")
  endif()
  add_dependencies(ObeliskZ3 obelisk_z3_project)
  message(STATUS "Building compiler-only Z3 ${OBELISK_Z3_VERSION} from source")
endif()

# slang v11.0 declares these dependencies by git tag. Declare checksummed
# release archives first (FetchContent's first declaration wins) so Obelisk's
# dependency graph is immutable and works without git.
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
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PATCH_COMMAND
      ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR>
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchSlang.cmake"
  )
endif()

# Only the slang library is needed; keep third-party build surface minimal.
set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_INSTALL OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SLANG_USE_MIMALLOC OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(obelisk_slang)
set(OBELISK_SLANG_SOURCE_DIR "${obelisk_slang_SOURCE_DIR}")
set(OBELISK_SLANG_INCLUDE_DIR "${OBELISK_SLANG_SOURCE_DIR}/include")
message(STATUS
  "Building slang v${OBELISK_SLANG_VERSION} from ${OBELISK_SLANG_SOURCE_DIR}")

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

add_subdirectory(runtime)
add_subdirectory(include)
add_subdirectory(lib)
add_subdirectory(tools)

if(OBELISK_INCLUDE_TESTS)
  add_subdirectory(test)
endif()

message(STATUS "obelisk configured against prebuilt LLVM + MLIR")
