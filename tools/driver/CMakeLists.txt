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
  X86CodeGen
  X86Desc
  X86Info
)

add_llvm_executable(obelisk
  driver.cpp
  NativeBackend.cpp
  NativeInputs.cpp
  Options.cpp
)

add_dependencies(obelisk
  ObeliskDriverOptions
  obelisk_native_support
)

target_include_directories(obelisk
  PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(obelisk
  PRIVATE
  OBELISK_VERSION_STRING="${PROJECT_VERSION}"
  OBELISK_RESOURCE_DIR="${OBELISK_RESOURCE_DIR}"
)

llvm_update_compile_flags(obelisk)

target_link_libraries(obelisk
  PRIVATE
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
  lldELF
)

mlir_check_all_link_libraries(obelisk)
