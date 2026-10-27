add_mlir_library(TalosRuntimeToLLVM
  EmbeddedDesign.cpp
  PrepareRuntimeToLLVM.cpp
  RuntimeToLLVM.cpp
  RuntimeToLLVMABI.cpp
  RuntimeToLLVMPatterns.cpp

  DEPENDS
  TalosConversionPassIncGen

  LINK_LIBS PUBLIC
  TalosRuntimeIR
  TalosSimulationAnalysis
  TalosSimulationIR
  MLIRFuncDialect
  MLIRFuncTransforms
  MLIRIR
  MLIRLLVMDialect
  MLIRLoopLikeInterface
  MLIRPass
  MLIRTransforms
)

target_include_directories(TalosRuntimeToLLVM PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
