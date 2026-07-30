add_mlir_library(ObeliskSimulationToBytecode
  BytecodeLayout.cpp
  DesignDatabaseSerialization.cpp
  SimulationToBytecode.cpp

  DEPENDS
  ObeliskConversionPassIncGen
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskSimulationAnalysis
  ObeliskSimulationIR
  ObeliskSimulationToStandard
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRIR
  MLIRPass
  MLIRTransforms
)

target_include_directories(ObeliskSimulationToBytecode PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
