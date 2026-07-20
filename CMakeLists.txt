cmake_minimum_required(VERSION 3.20)

project(obelisk
  VERSION 0.1.0
  DESCRIPTION "An AOT SystemVerilog simulator targeting IEEE 1800 conformance and real UVM"
  LANGUAGES C CXX)

#===------------------------------------------------------------------------===
# Toolchain: the prebuilt CIRCT release SDK.
#
# We deliberately do NOT use the system MLIR/LLVM. CIRCT tracks a bleeding-edge
# LLVM (this SDK is LLVM 23.0.0git) that the distro packages do not provide, and
# obelisk must lower Moore IR against the *same* MLIR/LLVM that produced it. The
# SDK bundles matching LLVM + MLIR + CIRCT libraries, headers, and CMake config,
# plus the `circt-verilog` slang frontend. See DESIGN.md.
#
# Override with -DOBELISK_CIRCT_DIR=/path/to/circt-<ver> if relocated.
#===------------------------------------------------------------------------===
set(OBELISK_CIRCT_DIR "/home/keyi/workspace/circt-1.153.1"
    CACHE PATH "Root of the prebuilt CIRCT release SDK")

if(NOT EXISTS "${OBELISK_CIRCT_DIR}/lib/cmake/circt/CIRCTConfig.cmake")
  message(FATAL_ERROR
    "CIRCT SDK not found at OBELISK_CIRCT_DIR=${OBELISK_CIRCT_DIR}. "
    "Download circt-full-static-linux-x64.tar.gz from the llvm/circt releases "
    "and point OBELISK_CIRCT_DIR at the extracted directory.")
endif()

set(CIRCT_DIR "${OBELISK_CIRCT_DIR}/lib/cmake/circt")
set(MLIR_DIR  "${OBELISK_CIRCT_DIR}/lib/cmake/mlir")
set(LLVM_DIR  "${OBELISK_CIRCT_DIR}/lib/cmake/llvm")

find_package(CIRCT REQUIRED CONFIG)
find_package(MLIR  REQUIRED CONFIG)
find_package(LLVM  REQUIRED CONFIG)

message(STATUS "Using CIRCT ${CIRCT_PACKAGE_VERSION} / LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "  CIRCT_DIR = ${CIRCT_DIR}")

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}" "${CIRCT_CMAKE_DIR}")

include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(HandleLLVMOptions)

#===------------------------------------------------------------------------===
# Language standard. Match the LLVM/MLIR headers (C++17). RTTI/exceptions follow
# LLVM's configuration via HandleLLVMOptions.
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
include_directories(${CIRCT_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_BINARY_DIR}/include)

add_subdirectory(include)
add_subdirectory(lib)
add_subdirectory(tools)

add_subdirectory(test)

message(STATUS "obelisk configured against CIRCT SDK at ${OBELISK_CIRCT_DIR}")
