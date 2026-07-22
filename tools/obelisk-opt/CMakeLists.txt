add_llvm_executable(obelisk-opt
  obelisk-opt.cpp
)

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

mlir_check_all_link_libraries(obelisk-opt)
