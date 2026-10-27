//===- SimulationVPIAnalysisTestPass.cpp - Print VPI facts --------------===//

#include "AnalysisTestPasses.h"
#include "talos/Dialect/Schedule/ScheduleEnums.h"

#include "talos/Analysis/SimulationVPIAnalysis.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

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
    return "test-talos-simulation-vpi-analysis";
  }
  StringRef getDescription() const final {
    return "print simulation VPI capability facts";
  }

  void runOnOperation() final {
    getOperation().walk([&](talos::sim::SimDesignOp design) {
      talos::analysis::SimulationVPIAnalysis analysis =
          talos::analysis::SimulationVPIAnalysis::compute(design);
      llvm::errs()
          << "vpi @" << design.getSymName()
          << " graph=" << (analysis.hasComputeGraph() ? "true" : "false")
          << " mode="
          << talos::schedule::stringifyComputeVPIMode(analysis.getMode())
          << " observability="
          << talos::schedule::stringifyComputeObservabilityKind(
                 analysis.getObservability())
          << " read=" << (analysis.allowsRead() ? "true" : "false")
          << " write=" << (analysis.allowsWrite() ? "true" : "false")
          << " static-dependencies="
          << (analysis.preservesStaticDependencies() ? "true" : "false")
          << "\n";
    });
    markAllAnalysesPreserved();
  }
};

} // namespace

namespace talos {

void registerSimulationVPIAnalysisTestPass() {
  PassRegistration<SimulationVPIAnalysisTestPass>();
}

} // namespace talos
