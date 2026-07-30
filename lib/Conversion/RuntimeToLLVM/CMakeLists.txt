add_mlir_library(ObeliskRuntimeToLLVM
  EmbeddedDesign.cpp
  RuntimeToLLVM.cpp
  RuntimeToLLVMABI.cpp
  RuntimeToLLVMPatterns.cpp

  DEPENDS
  ObeliskConversionPassIncGen

  LINK_LIBS PUBLIC
  ObeliskRuntimeIR
  MLIRFuncDialect
  MLIRFuncTransforms
  MLIRIR
  MLIRLLVMDialect
  MLIRLoopLikeInterface
  MLIRPass
  MLIRTransforms
)

target_include_directories(ObeliskRuntimeToLLVM PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
