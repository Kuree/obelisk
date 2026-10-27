add_llvm_executable(talos-translate
  talos-translate.cpp
)

llvm_update_compile_flags(talos-translate)

target_link_libraries(talos-translate
  PRIVATE
    TalosCoverageIR
    TalosDPIIR
    TalosFrontend
    TalosNativeIR
    TalosRuntimeIR
    TalosSDFIR
    TalosScheduleIR
    TalosSimulationAnalysis
    MLIRControlFlowDialect
    MLIRBuiltinToLLVMIRTranslation
    MLIRLLVMToLLVMIRTranslation
    MLIRTargetLLVMIRExport
    MLIRIR
    MLIRSupport
    MLIRTranslateLib
)

mlir_check_all_link_libraries(talos-translate)
