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
