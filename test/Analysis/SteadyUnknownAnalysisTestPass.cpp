//===- SteadyUnknownAnalysisTestPass.cpp - Print bits that stay X/Z ------===//

#include "AnalysisTestPasses.h"

#include "obelisk/Analysis/SteadyUnknownAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

/// Prints, per storage root, the bits expected to hold X/Z in steady state.
class SteadyUnknownAnalysisTestPass
    : public PassWrapper<SteadyUnknownAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(SteadyUnknownAnalysisTestPass)

  StringRef getArgument() const final { return "test-obelisk-steady-unknown"; }
  StringRef getDescription() const final {
    return "print the storage bits expected to hold X/Z in steady state";
  }

  void runOnOperation() final {
    for (auto design : getOperation().getOps<obelisk::sim::SimDesignOp>()) {
      auto analysis = obelisk::analysis::SteadyUnknownAnalysis::compute(design);
      if (failed(analysis))
        return signalPassFailure();
      llvm::errs() << "steady-unknown @" << design.getSymName()
                   << (analysis->isTop() ? " top" : "") << '\n';
      for (auto storage : design.getOps<obelisk::sim::SimStorageDeclOp>()) {
        auto ranges = analysis->lookup(storage.getId());
        if (ranges.empty())
          continue;
        llvm::errs() << "  root " << storage.getId() << ':';
        for (auto [low, width] : ranges)
          llvm::errs() << " [" << low << ',' << low + width << ')';
        llvm::errs() << '\n';
      }
    }
  }
};

} // namespace

namespace obelisk {
void registerSteadyUnknownAnalysisTestPass() {
  PassRegistration<SteadyUnknownAnalysisTestPass>();
}
} // namespace obelisk
