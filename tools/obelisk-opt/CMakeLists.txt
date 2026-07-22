add_llvm_executable(obelisk-opt
  obelisk-opt.cpp
)

if(OBELISK_INCLUDE_TESTS)
  add_subdirectory(
    ${PROJECT_SOURCE_DIR}/test/Analysis
    ${CMAKE_CURRENT_BINARY_DIR}/StateDomainTestPasses
  )
  target_include_directories(obelisk-opt PRIVATE
    ${PROJECT_SOURCE_DIR}/test/Analysis
  )
  target_compile_definitions(obelisk-opt PRIVATE OBELISK_INCLUDE_TESTS=1)
endif()

llvm_update_compile_flags(obelisk-opt)

target_link_libraries(obelisk-opt
  PRIVATE
    ObeliskIR
    ObeliskRuntimeIR
    ObeliskRuntimeToLLVM
    ObeliskSimulationIR
    ObeliskSimulationToStandard
    ObeliskSlangIR
    ObeliskSlangToObelisk
    ObeliskToSimulation
    MLIROptLib
    MLIRTransforms
    MLIRRegisterAllDialects
)

if(OBELISK_INCLUDE_TESTS)
  target_link_libraries(obelisk-opt PRIVATE ObeliskStateDomainTestPasses)
endif()

mlir_check_all_link_libraries(obelisk-opt)
