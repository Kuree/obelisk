add_mlir_library(ObeliskSimulationToBytecode
  SimulationToBytecode.cpp

  DEPENDS
  ObeliskConversionPassIncGen
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationIR
  ObeliskSimulationToStandard
  ObeliskSimulationToLLVMCoroutine
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRIR
  MLIRPass
  MLIRTransforms
)

target_include_directories(ObeliskSimulationToBytecode PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
