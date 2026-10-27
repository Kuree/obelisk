add_mlir_library(TalosSimulationToBytecode
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
  TalosConversionPassIncGen
  TalosDesignReflectionIncGen
  TalosSimulationOpsIncGen
  TalosVPIObjectModelIncGen

  LINK_LIBS PUBLIC
  TalosCoverage
  TalosSimulationAnalysis
  TalosSimulationIR
  TalosSimulationToStandard
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRIR
  MLIRPass
  MLIRTransforms
)

target_include_directories(TalosSimulationToBytecode PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
