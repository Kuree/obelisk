set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs)
add_public_tablegen_target(ObeliskDriverOptions)

set(LLVM_LINK_COMPONENTS
  Option
  Support
)

add_llvm_executable(obelisk
  driver.cpp
  Options.cpp
)

add_dependencies(obelisk
  ObeliskDriverOptions
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
  ObeliskSimulationIR
  ObeliskSlangIR
  ObeliskSlangToObelisk
  ObeliskToSimulation
  MLIRIR
  MLIRPass
  MLIRSupport
)

mlir_check_all_link_libraries(obelisk)
