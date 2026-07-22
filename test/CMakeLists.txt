option(OBELISK_ENABLE_REAL_UVM_TESTS
  "Fetch and run the opt-in Accellera UVM 2020.3.1 regression" OFF)
set(OBELISK_REAL_UVM_DIR "")
if(OBELISK_ENABLE_REAL_UVM_TESTS)
  FetchContent_Declare(obelisk_uvm
    URL
      "https://github.com/accellera-official/uvm-core/archive/78c06547a2a0a29b3dc9dcafae62b75b2ff61544.tar.gz"
    URL_HASH
      "SHA256=48e2f4ae3e9341c354b11e9c4a20ce24cc5ef7e6f87e05a8061386b611324422"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    # UVM is a source fixture, not a CMake dependency.
    SOURCE_SUBDIR _obelisk_no_build
  )
  FetchContent_MakeAvailable(obelisk_uvm)
  set(OBELISK_REAL_UVM_DIR "${obelisk_uvm_SOURCE_DIR}/src")
  message(STATUS "Real-UVM regression source: ${OBELISK_REAL_UVM_DIR}")
endif()

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

add_llvm_executable(obelisk-sim-standard-api-test EXCLUDE_FROM_ALL
  SimulationToStandardAPITest.cpp
)
llvm_update_compile_flags(obelisk-sim-standard-api-test)
target_link_libraries(obelisk-sim-standard-api-test PRIVATE
  ObeliskSimulationIR
  ObeliskSimulationToStandard
  MLIRArithDialect
  MLIRFuncDialect
  MLIRIR
  MLIRParser
  MLIRTransforms
)
set_target_properties(obelisk-sim-standard-api-test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

set(_generated_process_dir "${CMAKE_CURRENT_BINARY_DIR}/GeneratedProcess")
set(_generated_process_mlir "${_generated_process_dir}/process.mlir")
set(_generated_process_raw_llvm "${_generated_process_dir}/process.raw.ll")
set(_generated_process_llvm "${_generated_process_dir}/process.ll")
set(_generated_process_object "${_generated_process_dir}/process.o")
file(MAKE_DIRECTORY "${_generated_process_dir}")
add_custom_command(
  OUTPUT "${_generated_process_object}"
  COMMAND $<TARGET_FILE:obelisk-opt>
          "${CMAKE_CURRENT_SOURCE_DIR}/Inputs/generated-process-coroutine.mlir"
          --convert-obelisk-sim-processes-to-llvm-coroutines
          -o "${_generated_process_mlir}"
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/mlir-translate" --mlir-to-llvmir
          "${_generated_process_mlir}" -o "${_generated_process_raw_llvm}"
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/opt"
          "-passes=verify,coro-early,sroa,instcombine,simplifycfg,coro-split<reuse-storage>,coro-elide,coro-cleanup,sroa,instcombine,simplifycfg,dce,strip-dead-prototypes,verify"
          -S "${_generated_process_raw_llvm}" -o "${_generated_process_llvm}"
  COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llc" -filetype=obj
          -relocation-model=pic "${_generated_process_llvm}"
          -o "${_generated_process_object}"
  DEPENDS
  obelisk-opt
  "${CMAKE_CURRENT_SOURCE_DIR}/Inputs/generated-process-coroutine.mlir"
  VERBATIM
)
set_source_files_properties("${_generated_process_object}" PROPERTIES
  EXTERNAL_OBJECT TRUE
  GENERATED TRUE
)
add_executable(ObeliskGeneratedProcessRuntimeTests
  "${PROJECT_SOURCE_DIR}/runtime/unittests/GeneratedProcessTest.cpp"
  "${_generated_process_object}"
)
target_link_libraries(ObeliskGeneratedProcessRuntimeTests PRIVATE
  obelisk_rt
  GTest::gtest_main
)
target_compile_features(ObeliskGeneratedProcessRuntimeTests PRIVATE cxx_std_17)
set_target_properties(ObeliskGeneratedProcessRuntimeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/runtime/unittests"
)
add_dependencies(check-obelisk-runtime-units
  ObeliskGeneratedProcessRuntimeTests
)

add_lit_testsuite(check-obelisk "Running the Obelisk regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS
  obelisk
  obelisk-filecheck
  obelisk-not
  obelisk-opt
  obelisk-sim-standard-api-test
)
set_target_properties(check-obelisk PROPERTIES FOLDER "Tests")
add_dependencies(check-obelisk check-obelisk-runtime-units)
