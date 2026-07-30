//===- SimulationVPIAnalysisTestPass.cpp - Print VPI facts --------------===//

#include "AnalysisTestPasses.h"

#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

class SimulationVPIAnalysisTestPass
    : public PassWrapper<SimulationVPIAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(SimulationVPIAnalysisTestPass)

  StringRef getArgument() const final {
    return "test-obelisk-simulation-vpi-analysis";
  }
  StringRef getDescription() const final {
    return "print simulation VPI capability facts";
  }

  void runOnOperation() final {
    getOperation().walk([&](obelisk::sim::SimDesignOp design) {
      obelisk::analysis::SimulationVPIAnalysis analysis =
          obelisk::analysis::SimulationVPIAnalysis::compute(design);
      llvm::errs() << "vpi @" << design.getSymName() << " graph="
                   << (analysis.hasComputeGraph() ? "true" : "false")
                   << " mode="
                   << obelisk::sim::stringifyComputeVPIMode(analysis.getMode())
                   << " observability="
                   << obelisk::sim::stringifyComputeObservabilityKind(
                          analysis.getObservability())
                   << " read=" << (analysis.allowsRead() ? "true" : "false")
                   << " write=" << (analysis.allowsWrite() ? "true" : "false")
                   << " static-dependencies="
                   << (analysis.preservesStaticDependencies() ? "true"
                                                              : "false")
                   << "\n";
    });
    markAllAnalysesPreserved();
  }
};

} // namespace

namespace obelisk {

void registerSimulationVPIAnalysisTestPass() {
  PassRegistration<SimulationVPIAnalysisTestPass>();
}

} // namespace obelisk
