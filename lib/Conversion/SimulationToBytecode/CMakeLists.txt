add_mlir_library(ObeliskSimulationToBytecode
  BytecodeAggregateEncoding.cpp
  BytecodeArithmeticEncoding.cpp
  BytecodeCallEncoding.cpp
  BytecodeClassEncoding.cpp
  BytecodeContainerEncoding.cpp
  BytecodeOperationEncoding.cpp
  BytecodePlanning.cpp
  BytecodeStringEncoding.cpp
  BytecodeWaitEncoding.cpp
  BytecodeImageSerialization.cpp
  BytecodeLayout.cpp
  BytecodeRegisterPlanning.cpp
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
