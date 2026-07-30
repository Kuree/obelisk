add_mlir_library(ObeliskSimulationToBytecode
  BytecodeAggregateEncoding.cpp
  BytecodeArithmeticEncoding.cpp
  BytecodeCallEncoding.cpp
  BytecodeClassEncoding.cpp
  BytecodeContainerEncoding.cpp
  BytecodeCoverageEncoding.cpp
  BytecodeImageSerialization.cpp
  BytecodeLayout.cpp
  BytecodeManagedReferenceEncoding.cpp
  BytecodeOperationEncoding.cpp
  BytecodePlanning.cpp
  BytecodeRegisterPlanning.cpp
  BytecodeStateEncoding.cpp
  BytecodeStringEncoding.cpp
  BytecodeSuspensionEncoding.cpp
  BytecodeWaitEncoding.cpp
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
