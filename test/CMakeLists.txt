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

function(obelisk_add_generated_process_object output_var directory input_file)
  set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/${directory}")
  set(generated_mlir "${generated_dir}/process.mlir")
  set(generated_raw_llvm "${generated_dir}/process.raw.ll")
  set(generated_llvm "${generated_dir}/process.ll")
  set(generated_object "${generated_dir}/process.o")
  set(generated_opt_options)
  if(LLVM_USE_SANITIZER MATCHES "(^|;)Thread($|;)")
    # The official prebuilt LLVM/MLIR SDK is not TSAN-instrumented. Keep its
    # internal verifier thread pool from crossing the instrumentation boundary
    # while generating runtime test fixtures; the runtime tests themselves
    # remain multithreaded.
    list(APPEND generated_opt_options --mlir-disable-threading)
  endif()
  file(MAKE_DIRECTORY "${generated_dir}")
  add_custom_command(
    OUTPUT "${generated_object}"
    COMMAND $<TARGET_FILE:obelisk-opt>
            ${generated_opt_options}
            "${CMAKE_CURRENT_SOURCE_DIR}/${input_file}"
            "--pass-pipeline=builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension)),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)"
            -o "${generated_mlir}"
    COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/mlir-translate" --mlir-to-llvmir
            "${generated_mlir}" -o "${generated_raw_llvm}"
    COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/opt"
            "-passes=default<O3>"
            -S "${generated_raw_llvm}" -o "${generated_llvm}"
    COMMAND "${OBELISK_LLVM_DIST_DIR}/bin/llc" -filetype=obj
            -relocation-model=pic "${generated_llvm}"
            -o "${generated_object}"
    DEPENDS
    obelisk-opt
    "${CMAKE_CURRENT_SOURCE_DIR}/${input_file}"
    VERBATIM
  )
  set_source_files_properties("${generated_object}" PROPERTIES
    EXTERNAL_OBJECT TRUE
    GENERATED TRUE
  )
  set(${output_var} "${generated_object}" PARENT_SCOPE)
endfunction()

obelisk_add_generated_process_object(
  _generated_process_object
  GeneratedProcess
  Inputs/generated-process-coroutine.mlir
)
obelisk_add_generated_process_object(
  _generated_arithmetic_object
  GeneratedArithmetic
  Inputs/generated-bytecode-arithmetic.mlir
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
add_executable(ObeliskGeneratedArithmeticRuntimeTests
  "${PROJECT_SOURCE_DIR}/runtime/unittests/GeneratedArithmeticTest.cpp"
  "${_generated_arithmetic_object}"
)
target_link_libraries(ObeliskGeneratedArithmeticRuntimeTests PRIVATE
  obelisk_rt
  GTest::gtest_main
)
target_compile_features(ObeliskGeneratedArithmeticRuntimeTests PRIVATE
  cxx_std_17
)
set_target_properties(ObeliskGeneratedArithmeticRuntimeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/runtime/unittests"
)
add_dependencies(check-obelisk-runtime-units
  ObeliskGeneratedArithmeticRuntimeTests
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
