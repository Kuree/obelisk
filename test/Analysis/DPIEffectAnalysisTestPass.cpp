//===- DPIEffectAnalysisTestPass.cpp - Print DPI call transparency --------===//

#include "AnalysisTestPasses.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "obelisk/Analysis/DPIEffectAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;

namespace {
class DPIEffectAnalysisTestPass
    : public PassWrapper<DPIEffectAnalysisTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(DPIEffectAnalysisTestPass)
  StringRef getArgument() const final { return "test-obelisk-dpi-effects"; }
  StringRef getDescription() const final {
    return "print whether DPI calls can reach design state";
  }
  void runOnOperation() final {
    getOperation().walk([](obelisk::sim::SimDesignOp design) {
      obelisk::analysis::DPIEffectAnalysis analysis(design);
      llvm::errs() << "design @" << design.getSymName() << " exports "
                   << (analysis.exportsAreTransparent() ? "transparent"
                                                        : "opaque")
                   << '\n';
      design.walk([&](obelisk::sim::SimDPICallOp call) {
        llvm::errs() << "  call " << call.getCIdentifier() << ": "
                     << (analysis.mayAccessDesign(call) ? "may access design"
                                                        : "transparent")
                     << '\n';
      });
    });
    markAllAnalysesPreserved();
  }
};
} // namespace

void obelisk::registerDPIEffectAnalysisTestPass() {
  PassRegistration<DPIEffectAnalysisTestPass>();
}
