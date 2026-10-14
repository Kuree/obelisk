add_llvm_executable(obelisk-translate
  obelisk-translate.cpp
)

llvm_update_compile_flags(obelisk-translate)

target_link_libraries(obelisk-translate
  PRIVATE
    ObeliskCoverageIR
    ObeliskDPIIR
    ObeliskFrontend
    ObeliskNativeIR
    ObeliskRuntimeIR
    ObeliskSDFIR
    ObeliskScheduleIR
    MLIRBuiltinToLLVMIRTranslation
    MLIRLLVMToLLVMIRTranslation
    MLIRTargetLLVMIRExport
    MLIRIR
    MLIRSupport
    MLIRTranslateLib
)

mlir_check_all_link_libraries(obelisk-translate)
