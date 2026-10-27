set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs)
add_public_tablegen_target(TalosDriverOptions)

set(TALOS_RESOURCE_DIR "${CMAKE_BINARY_DIR}/lib/talos")
file(MAKE_DIRECTORY "${TALOS_RESOURCE_DIR}/include")
configure_file("${PROJECT_SOURCE_DIR}/runtime/include/svdpi.h"
               "${TALOS_RESOURCE_DIR}/include/svdpi.h" COPYONLY)
foreach(vpi_header vpi_user.h sv_vpi_user.h vpi_compatibility.h)
  configure_file(
    "${TALOS_SLANG_SOURCE_DIR}/external/ieee1800/${vpi_header}"
    "${TALOS_RESOURCE_DIR}/include/${vpi_header}" COPYONLY)
endforeach()
if(APPLE)
  # Outside Linux and Windows the IEEE header takes its fixed-width types from
  # <sys/types.h>, which on Darwin lacks the unsigned ones VPI applications use.
  file(READ "${TALOS_RESOURCE_DIR}/include/vpi_user.h" _talos_vpi_user)
  file(WRITE "${TALOS_RESOURCE_DIR}/include/vpi_user.h"
    "#include <stdint.h>\n${_talos_vpi_user}")
  unset(_talos_vpi_user)
endif()
file(MAKE_DIRECTORY "${TALOS_RESOURCE_DIR}/include/talos")
configure_file(
  "${PROJECT_SOURCE_DIR}/runtime/include/talos/vpi_user_compat.h"
  "${TALOS_RESOURCE_DIR}/include/talos/vpi_user_compat.h" COPYONLY)
# Keep the canonical SDK filename while layering prototype-era 2023 constants
# over the vendored IEEE header. No generated SDK file is checked into source.
file(APPEND "${TALOS_RESOURCE_DIR}/include/sv_vpi_user.h"
  "\n#include \"talos/vpi_user_compat.h\"\n")

set(LLVM_LINK_COMPONENTS
  Analysis
  BinaryFormat
  BitReader
  BitWriter
  CodeGen
  Core
  Coroutines
  IPO
  MC
  Option
  Object
  Passes
  ScalarOpts
  Support
  Target
  TransformUtils
)

# A backend is compiled only when its LLVM target and LLD driver are present.
# The wasm SDK is built with LLVM_TARGETS_TO_BUILD=WebAssembly, so naming the
# native backend there is not a harmless extra: component mapping hard-fails on
# a component whose target was omitted.
#
# A native build gets both, because the official distribution carries every
# target. That is what lets a native Talos cross-compile a design to wasm.
set(TALOS_BACKEND_SOURCES)
set(TALOS_NATIVE_INPUTS_SOURCE)
set(TALOS_HAS_NATIVE_BACKEND 0)
set(TALOS_HAS_HOST_C_RUNTIME 0)
set(TALOS_HAS_WASM_BACKEND 0)

list(APPEND LLVM_LINK_COMPONENTS
  WebAssemblyCodeGen
  WebAssemblyDesc
  WebAssemblyInfo)
list(APPEND TALOS_BACKEND_SOURCES WasmBackend.cpp)
set(TALOS_HAS_WASM_BACKEND 1)

if(NOT EMSCRIPTEN)
  set(Clang_DIR "${TALOS_LLVM_DIST_DIR}/lib/cmake/clang" CACHE PATH
    "Matching Clang CMake package" FORCE)
  find_package(Clang REQUIRED CONFIG NO_DEFAULT_PATH)
  if(TALOS_NATIVE_PLATFORM STREQUAL "linux")
    list(APPEND LLVM_LINK_COMPONENTS nativecodegen)
    list(APPEND TALOS_BACKEND_SOURCES NativeBackend.cpp HostCRuntime.cpp)
    set(TALOS_NATIVE_INPUTS_SOURCE NativeInputs.cpp)
    set(TALOS_HAS_HOST_C_RUNTIME 1)
  elseif(TALOS_NATIVE_PLATFORM STREQUAL "windows")
    list(APPEND LLVM_LINK_COMPONENTS nativecodegen)
    list(APPEND TALOS_BACKEND_SOURCES WindowsBackend.cpp)
    set(TALOS_NATIVE_INPUTS_SOURCE NativeInputs.cpp)
  elseif(TALOS_NATIVE_PLATFORM STREQUAL "macos")
    list(APPEND LLVM_LINK_COMPONENTS nativecodegen)
    list(APPEND TALOS_BACKEND_SOURCES MacOSBackend.cpp)
    set(TALOS_NATIVE_INPUTS_SOURCE NativeInputs.cpp)
  else()
    message(FATAL_ERROR
      "Native platform '${TALOS_NATIVE_PLATFORM}' has no registered driver "
      "backend, runtime discovery, and input classifier source set")
  endif()
  set(TALOS_HAS_NATIVE_BACKEND 1)
  set(TALOS_DEFAULT_TARGET "native")
else()
  # A wasm build of the compiler cannot produce native ELF, so its only target
  # is the one it also runs on.
  set(TALOS_NATIVE_INPUTS_SOURCE NativeInputs.cpp)
  set(TALOS_DEFAULT_TARGET "wasm32")
endif()

set(TALOS_DRIVER_COMMON_SOURCES
  driver.cpp
  TargetBackend.cpp
  ${TALOS_BACKEND_SOURCES}
  ${TALOS_NATIVE_INPUTS_SOURCE}
  Options.cpp
)

function(talos_configure_driver target)
  add_dependencies(${target}
    TalosDriverOptions
    talos_native_support
  )
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/runtime/include
  )
  target_compile_definitions(${target} PRIVATE
    TALOS_VERSION_STRING="${PROJECT_VERSION}"
    TALOS_RESOURCE_DIR="${TALOS_RESOURCE_DIR}"
    TALOS_HAS_NATIVE_BACKEND=${TALOS_HAS_NATIVE_BACKEND}
    TALOS_HAS_HOST_C_RUNTIME=${TALOS_HAS_HOST_C_RUNTIME}
    TALOS_HAS_WASM_BACKEND=${TALOS_HAS_WASM_BACKEND}
    TALOS_DEFAULT_TARGET="${TALOS_DEFAULT_TARGET}"
    TALOS_NATIVE_TARGET_TRIPLE="${TALOS_TARGET_TRIPLE}"
  )
  if(APPLE)
    target_compile_definitions(${target} PRIVATE
      TALOS_MACOS_DEPLOYMENT_TARGET="${CMAKE_OSX_DEPLOYMENT_TARGET}"
      TALOS_MACOS_SDK_VERSION="${TALOS_MACOS_SDK_VERSION}")
  endif()
  llvm_update_compile_flags(${target})
  target_link_libraries(${target} PRIVATE
    TalosFrontend
    TalosIR
    TalosRuntimeIR
    TalosSimulationIR
    TalosSimulationTransforms
    TalosSimulationToBytecode
    TalosSimulationToLLVMCoroutine
    TalosLLVMPasses
    TalosSlangIR
    TalosSlangToTalos
    TalosToSimulation
    MLIRBytecodeWriter
    MLIRIR
    MLIRBuiltinToLLVMIRTranslation
    MLIRLLVMToLLVMIRTranslation
    MLIRPass
    MLIRSupport
    MLIRTargetLLVMIRExport
    lldCommon
    lldWasm
    $<$<STREQUAL:${TALOS_NATIVE_PLATFORM},linux>:lldELF>
    $<$<STREQUAL:${TALOS_NATIVE_PLATFORM},windows>:lldCOFF>
    $<$<STREQUAL:${TALOS_NATIVE_PLATFORM},macos>:lldMachO>
  )
  if(NOT EMSCRIPTEN)
    target_link_libraries(${target} PRIVATE clangDriver clangBasic)
  endif()
  if(WIN32)
    target_link_libraries(${target} PRIVATE "${TALOS_COMPILER_RT_BUILTINS}")
  endif()
  mlir_check_all_link_libraries(${target})
endfunction()

# talos and talos-test-driver differ only in main(). Compile the shared
# driver sources once and link the same objects into both executables.
add_library(talos-driver-objects OBJECT ${TALOS_DRIVER_COMMON_SOURCES})
talos_configure_driver(talos-driver-objects)

add_llvm_executable(talos
  PARTIAL_SOURCES_INTENDED
  Main.cpp
)
talos_configure_driver(talos)
target_link_libraries(talos PRIVATE talos-driver-objects)

if(EMSCRIPTEN)
  # compiler-worker.js loads the compiler as a reusable factory and invokes it
  # repeatedly. Keep Node available as well so test/web/smoke-test.mjs
  # exercises exactly the same artifact outside a browser.
  target_link_options(talos PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createTalosModule"
    "-sEXPORTED_RUNTIME_METHODS=FS,callMain"
    "-sINVOKE_RUN=0"
    "-sSTACK_SIZE=16777216"
    "-sENVIRONMENT=web,worker,node")
endif()

if(TALOS_INCLUDE_TESTS AND TALOS_NATIVE_PLATFORM STREQUAL "linux")
  add_llvm_executable(talos-host-c-runtime-test EXCLUDE_FROM_ALL
    PARTIAL_SOURCES_INTENDED
    ${PROJECT_SOURCE_DIR}/test/Support/HostCRuntimeTest.cpp
    HostCRuntime.cpp
  )
  target_include_directories(talos-host-c-runtime-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  llvm_update_compile_flags(talos-host-c-runtime-test)
  target_link_libraries(talos-host-c-runtime-test PRIVATE
    clangDriver
    clangBasic
  )
endif()

if(TALOS_INCLUDE_TESTS)
  add_llvm_executable(talos-test-driver EXCLUDE_FROM_ALL
    PARTIAL_SOURCES_INTENDED
    ${PROJECT_SOURCE_DIR}/test/Support/DriverTestMain.cpp
  )
  talos_configure_driver(talos-test-driver)
  target_link_libraries(talos-test-driver PRIVATE talos-driver-objects)
endif()
