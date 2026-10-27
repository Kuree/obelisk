//===- talos-opt.cpp - Talos IR parser and optimizer driver ----------===//

#include "talos/Conversion/TalosToSimulation.h"
#include "talos/Conversion/RuntimeToLLVM.h"
#include "talos/Conversion/SimulationToLLVMCoroutine.h"
#include "talos/Conversion/SimulationToSchedule.h"
#include "talos/Conversion/SlangToTalos.h"
#include "talos/Dialect/Coverage/CoverageDialect.h"
#include "talos/Dialect/DPI/DPIDialect.h"
#include "talos/Dialect/Native/NativeDialect.h"
#include "talos/Dialect/Talos/TalosDialect.h"
#include "talos/Dialect/Runtime/RuntimeDialect.h"
#include "talos/Dialect/SDF/SDFDialect.h"
#include "talos/Dialect/Schedule/Transforms/Passes.h"
#include "talos/Dialect/Simulation/SimulationDialect.h"
#include "talos/Dialect/Simulation/Transforms/Passes.h"
#include "talos/Dialect/Slang/SlangDialect.h"

#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/InitAllDialects.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "mlir/Transforms/Passes.h"
#include "talos/Dialect/Schedule/ScheduleDialect.h"

#ifdef TALOS_INCLUDE_TESTS
#include "AnalysisTestPasses.h"
#endif

int main(int argc, char **argv) {
  // The core transforms are part of the lowering pipeline, so tests need to
  // be able to run them directly on simulation IR.
  mlir::registerTransformsPasses();
  mlir::registerPass([] { return mlir::createSCFToControlFlowPass(); });
  talos::registerTalosConversionPasses();
  talos::registerRuntimeToLLVMPipeline();
  talos::registerSimulationToLLVMCoroutinePipeline();
  talos::registerTalosSimulationPasses();
  talos::registerTalosSchedulePasses();
  talos::registerSimulationToSchedulePipeline();
  talos::registerTalosToSimulationPipeline();
#ifdef TALOS_INCLUDE_TESTS
  talos::registerAccessFootprintAnalysisTestPass();
  talos::registerActivationMemoryAnalysisTestPass();
  talos::registerActivationPhaseAnalysisTestPass();
  talos::registerBytecodeImageTestPass();
  talos::registerClockInferenceAnalysisTestPass();
  talos::registerDPIEffectAnalysisTestPass();
  talos::registerClassDispatchAnalysisTestPass();
  talos::registerCombinationalDependencyAnalysisTestPass();
  talos::registerCoverageSchemaTestPass();
  talos::registerStorageWriteAnalysisTestPass();
  talos::registerManagedClassLayoutAnalysisTestPass();
  talos::registerLocalStorageAnalysisTestPass();
  talos::registerLocalUnknownAnalysisTestPass();
  talos::registerSteadyUnknownAnalysisTestPass();
  talos::registerNativeAOTAnalysisTestPass();
  talos::registerNativeSizeCensusTestPass();
  talos::registerNativeStateLayoutAnalysisTestPass();
  talos::registerRandomProgramTestPass();
  talos::registerSimulationProcessFrameAnalysisTestPass();
  talos::registerSimulationScheduleAnalysisTestPass();
  talos::registerSimulationVPIAnalysisTestPass();
  talos::registerStaticSpecializationAnalysisTestPass();
  talos::registerStateDomainTestPasses();
  talos::registerModuleTestPasses();
  talos::registerNativeBackendTestPipeline();
  talos::registerValueInterfacesTestPass();
#endif

  mlir::DialectRegistry registry;
  mlir::registerAllDialects(registry);
  registry.insert<
      talos::slangir::SlangDialect, talos::ir::TalosDialect,
      talos::cov::CoverageDialect, talos::dpi::DPIDialect,
      talos::native::NativeDialect, talos::runtime::TalosRuntimeDialect,
      talos::sdf::TalosSDFDialect, talos::schedule::ScheduleDialect,
      talos::sim::TalosSimulationDialect>();

  return mlir::asMainReturnCode(
      mlir::MlirOptMain(argc, argv, "Talos IR optimizer\n", registry));
}
