set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs)
add_public_tablegen_target(ObeliskDriverOptions)

set(OBELISK_RESOURCE_DIR "${CMAKE_BINARY_DIR}/lib/obelisk")
file(MAKE_DIRECTORY "${OBELISK_RESOURCE_DIR}/include")
configure_file("${PROJECT_SOURCE_DIR}/runtime/include/svdpi.h"
               "${OBELISK_RESOURCE_DIR}/include/svdpi.h" COPYONLY)
foreach(vpi_header vpi_user.h sv_vpi_user.h vpi_compatibility.h)
  configure_file(
    "${OBELISK_SLANG_SOURCE_DIR}/external/ieee1800/${vpi_header}"
    "${OBELISK_RESOURCE_DIR}/include/${vpi_header}" COPYONLY)
endforeach()

set(LLVM_LINK_COMPONENTS
  Analysis
  BinaryFormat
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
# The wasm SDK is built with LLVM_TARGETS_TO_BUILD=WebAssembly, so naming X86
# there is not a harmless extra: llvm_map_components_to_libnames hard-fails on
# a component whose target was omitted.
#
# A native build gets both, because the official distribution carries every
# target. That is what lets an x86-64 obelisk cross-compile a design to wasm.
set(OBELISK_BACKEND_SOURCES)
set(OBELISK_HAS_NATIVE_BACKEND 0)
set(OBELISK_HAS_WASM_BACKEND 0)

list(APPEND LLVM_LINK_COMPONENTS
  WebAssemblyCodeGen
  WebAssemblyDesc
  WebAssemblyInfo)
list(APPEND OBELISK_BACKEND_SOURCES WasmBackend.cpp)
set(OBELISK_HAS_WASM_BACKEND 1)

if(NOT EMSCRIPTEN)
  list(APPEND LLVM_LINK_COMPONENTS
    X86CodeGen
    X86Desc
    X86Info)
  list(APPEND OBELISK_BACKEND_SOURCES NativeBackend.cpp)
  set(OBELISK_HAS_NATIVE_BACKEND 1)
  set(OBELISK_DEFAULT_TARGET "native")
else()
  # A wasm build of the compiler cannot produce x86-64, so its only target is
  # the one it also runs on.
  set(OBELISK_DEFAULT_TARGET "wasm32")
endif()

set(OBELISK_DRIVER_COMMON_SOURCES
  driver.cpp
  TargetBackend.cpp
  ${OBELISK_BACKEND_SOURCES}
  NativeInputs.cpp
  Options.cpp
)

function(obelisk_configure_driver target)
  add_dependencies(${target}
    ObeliskDriverOptions
    obelisk_native_support
  )
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/runtime/include
  )
  target_compile_definitions(${target} PRIVATE
    OBELISK_VERSION_STRING="${PROJECT_VERSION}"
    OBELISK_RESOURCE_DIR="${OBELISK_RESOURCE_DIR}"
    OBELISK_HAS_NATIVE_BACKEND=${OBELISK_HAS_NATIVE_BACKEND}
    OBELISK_HAS_WASM_BACKEND=${OBELISK_HAS_WASM_BACKEND}
    OBELISK_DEFAULT_TARGET="${OBELISK_DEFAULT_TARGET}"
  )
  llvm_update_compile_flags(${target})
  target_link_libraries(${target} PRIVATE
    ObeliskFrontend
    ObeliskIR
    ObeliskRuntimeIR
    ObeliskSimulationIR
    ObeliskSimulationToBytecode
    ObeliskSimulationToLLVMCoroutine
    ObeliskSlangIR
    ObeliskSlangToObelisk
    ObeliskToSimulation
    MLIRIR
    MLIRBuiltinToLLVMIRTranslation
    MLIRLLVMToLLVMIRTranslation
    MLIRPass
    MLIRSupport
    MLIRTargetLLVMIRExport
    lldCommon
    lldWasm
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:lldELF>
  )
  mlir_check_all_link_libraries(${target})
endfunction()

add_llvm_executable(obelisk
  PARTIAL_SOURCES_INTENDED
  Main.cpp
  ${OBELISK_DRIVER_COMMON_SOURCES}
)
obelisk_configure_driver(obelisk)

if(EMSCRIPTEN)
  # compiler-worker.js loads the compiler as a reusable factory and invokes it
  # repeatedly. Keep Node available as well so web/smoke-test.mjs exercises
  # exactly the same artifact outside a browser.
  target_link_options(obelisk PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createObeliskModule"
    "-sEXPORTED_RUNTIME_METHODS=FS,callMain"
    "-sINVOKE_RUN=0"
    "-sSTACK_SIZE=16777216"
    "-sENVIRONMENT=web,worker,node")
endif()

if(OBELISK_INCLUDE_TESTS)
  set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/test/Support/ProtectedEnvelopeTestProvider.cpp
    PROPERTIES COMPILE_OPTIONS "-fexceptions")
  add_llvm_executable(obelisk-protect-test EXCLUDE_FROM_ALL
    PARTIAL_SOURCES_INTENDED
    ${PROJECT_SOURCE_DIR}/test/Support/ProtectedEnvelopeTestMain.cpp
    ${PROJECT_SOURCE_DIR}/test/Support/ProtectedEnvelopeTestProvider.cpp
    ${OBELISK_DRIVER_COMMON_SOURCES}
  )
  target_include_directories(obelisk-protect-test PRIVATE
    ${PROJECT_SOURCE_DIR}/test/Support
  )
  obelisk_configure_driver(obelisk-protect-test)
endif()
