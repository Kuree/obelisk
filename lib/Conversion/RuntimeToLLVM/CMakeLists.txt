add_mlir_library(ObeliskRuntimeToLLVM
  EmbeddedDesign.cpp
  RuntimeToLLVM.cpp

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
