set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs)
add_public_tablegen_target(ObeliskDriverOptions)

set(LLVM_LINK_COMPONENTS
  Analysis
  CodeGen
  Core
  Coroutines
  IPO
  MC
  Option
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
)

llvm_update_compile_flags(obelisk)

target_link_libraries(obelisk
  PRIVATE
  ObeliskFrontend
  ObeliskIR
  ObeliskRuntimeIR
  ObeliskSimulationIR
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
