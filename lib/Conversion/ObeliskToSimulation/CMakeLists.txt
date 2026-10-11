if(OBELISK_ENABLE_Z3)
  add_compile_definitions(OBELISK_ENABLE_Z3=1)
endif()

add_mlir_library(ObeliskToSimulation
  Finalize.cpp
  LowerUnit.cpp
  LowerUnitArrayQuerySystemCalls.cpp
  LowerUnitCalls.cpp
  LowerUnitConditionals.cpp
  LowerUnitConcurrent.cpp
  LowerUnitContainers.cpp
  LowerUnitControlFlow.cpp
  LowerUnitCovergroups.cpp
  LowerUnitDisplaySystemCalls.cpp
  LowerUnitDumpSystemCalls.cpp
  LowerUnitExpressions.cpp
  LowerUnitFileSystemCalls.cpp
  LowerUnitLValues.cpp
  LowerUnitOperators.cpp
  LowerUnitPlusargSystemCalls.cpp
  LowerUnitPlaSystemCalls.cpp
  LowerUnitRealConversionSystemCalls.cpp
  LowerUnitRandomize.cpp
  RandomArithmetic.cpp
  LowerUnitRealMathSystemCalls.cpp
  LowerUnitScanSystemCalls.cpp
  LowerUnitSystemCalls.cpp
  LowerUnitTiming.cpp
  LowerUnitTimingChecks.cpp
  MaterializeClockedSamples.cpp
  PrepareCoverage.cpp
  PrepareCaptures.cpp
  Prepare.cpp
  PrepareDeclarations.cpp
  PrepareNetTopology.cpp
  PrepareRandomFunctions.cpp
  PrepareRandomIteration.cpp
  PrepareRandomTemplates.cpp
  PrepareTopology.cpp
  PrepareUnits.cpp
  PrepareValidation.cpp
  SemanticUtils.cpp
  UnitLoweringInputs.cpp

  DEPENDS
  ObeliskConversionPassIncGen
  ObeliskSimulationOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskIR
  ObeliskCoverage
  ObeliskSimulationAnalysis
  ObeliskSimulationIR
  ObeliskSimulationTransforms
  ObeliskSolver
  MLIRAnalysis
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRIR
  MLIRPass
  MLIRTransforms
)

target_include_directories(ObeliskToSimulation PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)
