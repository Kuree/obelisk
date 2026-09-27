add_mlir_library(ObeliskSimulationToSchedule
  PARTIAL_SOURCES_INTENDED
  BuildComputeGraph.cpp
  ComputeGraph.cpp
  ExtractPeriodicClocks.cpp
  MaterializeClockedControl.cpp
  SimulationToSchedule.cpp
  Utils.cpp
  DEPENDS
  ObeliskConversionPassIncGen
  LINK_LIBS PUBLIC
  ObeliskSimulationAnalysis
  ObeliskSimulationIR
  ObeliskScheduleIR
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRPass
  MLIRTransforms
)

# Refinement after native storage/actor identities are known. This owns plan
# derivation; LLVM globals, runtime calls and ABI emission stay in the backend.
add_mlir_library(ObeliskSimulationToScheduleNative
  PARTIAL_SOURCES_INTENDED
  NativeSchedulePasses.cpp
  NativeEvalPlanning.cpp
  NativePlanning.cpp
  NativeNBAPlanning.cpp
  DEPENDS
  ObeliskConversionPassIncGen
  LINK_LIBS PUBLIC
  ObeliskSimulationIR
  ObeliskSimulationAnalysis
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRLLVMDialect
  MLIRTransforms
)
target_include_directories(ObeliskSimulationToScheduleNative PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include")
