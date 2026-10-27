add_mlir_library(TalosSimulationToSchedule
  PARTIAL_SOURCES_INTENDED
  BuildComputeGraph.cpp
  ComputeGraph.cpp
  ExtractPeriodicClocks.cpp
  MaterializeClockedControl.cpp
  SimulationToSchedule.cpp
  Utils.cpp
  DEPENDS
  TalosConversionPassIncGen
  LINK_LIBS PUBLIC
  TalosSimulationAnalysis
  TalosSimulationIR
  TalosScheduleIR
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRPass
  MLIRTransforms
)

# Refinement after native storage/actor identities are known. This owns plan
# derivation; LLVM globals, runtime calls and ABI emission stay in the backend.
add_mlir_library(TalosSimulationToScheduleNative
  PARTIAL_SOURCES_INTENDED
  NativeSchedulePasses.cpp
  NativeEvalPlanning.cpp
  NativePlanning.cpp
  NativeNBAPlanning.cpp
  DEPENDS
  TalosConversionPassIncGen
  LINK_LIBS PUBLIC
  TalosScheduleTransforms
  TalosSimulationIR
  TalosSimulationAnalysis
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRLLVMDialect
  MLIRTransforms
)
target_include_directories(TalosSimulationToScheduleNative PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include")
