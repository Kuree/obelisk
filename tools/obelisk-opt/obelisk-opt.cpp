//===- obelisk-opt.cpp - Obelisk IR parser and optimizer driver ----------===//

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Conversion/SlangToObelisk.h"
#include "obelisk/Dialect/Obelisk/ObeliskDialect.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Simulation/SimulationDialect.h"
#include "obelisk/Dialect/Slang/SlangDialect.h"

#include "mlir/IR/DialectRegistry.h"
#include "mlir/InitAllDialects.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "mlir/Transforms/Passes.h"

#ifdef OBELISK_INCLUDE_TESTS
#include "AnalysisTestPasses.h"
#endif

int main(int argc, char **argv) {
  // The core transforms are part of the lowering pipeline, so tests need to
  // be able to run them directly on obelisk_sim IR.
  mlir::registerTransformsPasses();
  obelisk::registerObeliskConversionPasses();
  obelisk::registerObeliskToSimulationPipeline();
#ifdef OBELISK_INCLUDE_TESTS
  obelisk::registerNativeAOTAnalysisTestPass();
  obelisk::registerStateDomainTestPasses();
#endif

  mlir::DialectRegistry registry;
  mlir::registerAllDialects(registry);
  registry.insert<obelisk::slangir::SlangDialect, obelisk::ir::ObeliskDialect,
                  obelisk::runtime::ObeliskRuntimeDialect,
                  obelisk::sim::ObeliskSimulationDialect>();

  return mlir::asMainReturnCode(
      mlir::MlirOptMain(argc, argv, "Obelisk IR optimizer\n", registry));
}
