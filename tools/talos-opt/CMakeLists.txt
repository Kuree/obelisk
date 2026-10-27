add_llvm_executable(talos-opt
  talos-opt.cpp
)

if(TALOS_INCLUDE_TESTS)
  add_subdirectory(
    ${PROJECT_SOURCE_DIR}/test/Analysis
    ${CMAKE_CURRENT_BINARY_DIR}/AnalysisTestPasses
  )
  target_include_directories(talos-opt PRIVATE
    ${PROJECT_SOURCE_DIR}/test/Analysis
  )
  target_compile_definitions(talos-opt PRIVATE TALOS_INCLUDE_TESTS=1)
endif()

llvm_update_compile_flags(talos-opt)

target_link_libraries(talos-opt
  PRIVATE
    TalosIR
    TalosRuntimeIR
    TalosSDFIR
    TalosRuntimeToLLVM
    TalosSimulationIR
    TalosSimulationTransforms
    TalosSimulationToLLVMCoroutine
    TalosSimulationToBytecode
    TalosSimulationToRuntime
    TalosSlangIR
    TalosSlangToTalos
    TalosSimulationToStandard
    TalosToSimulation
    MLIROptLib
    MLIRTransforms
    MLIRSCFToControlFlow
    MLIRRegisterAllDialects
)

if(TALOS_INCLUDE_TESTS)
  target_link_libraries(talos-opt PRIVATE TalosAnalysisTestPasses)
endif()

mlir_check_all_link_libraries(talos-opt)
