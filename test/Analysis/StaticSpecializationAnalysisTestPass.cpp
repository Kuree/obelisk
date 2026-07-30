//===- StaticSpecializationAnalysisTestPass.cpp - Print plan facts -------===//

#include "AnalysisTestPasses.h"

#include "obelisk/Analysis/StaticSpecializationAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

class StaticSpecializationAnalysisTestPass
    : public PassWrapper<StaticSpecializationAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      StaticSpecializationAnalysisTestPass)

  StringRef getArgument() const final {
    return "test-obelisk-static-specialization-analysis";
  }
  StringRef getDescription() const final {
    return "print validated static-specialization plan facts";
  }

  void runOnOperation() final {
    obelisk::sim::SimDesignOp design;
    getOperation().walk(
        [&](obelisk::sim::SimDesignOp candidate) { design = candidate; });
    if (!design) {
      getOperation().emitError("has no simulation design");
      signalPassFailure();
      return;
    }
    FailureOr<obelisk::analysis::StaticSpecializationAnalysis> analysis =
        obelisk::analysis::StaticSpecializationAnalysis::compute(design);
    if (failed(analysis)) {
      signalPassFailure();
      return;
    }

    llvm::errs() << "static-specialization present="
                 << (*analysis ? "true" : "false") << "\n";
    if (!*analysis) {
      markAllAnalysesPreserved();
      return;
    }

    SmallVector<uint64_t> roots;
    for (const auto &[descriptor, policy] : analysis->getRoots())
      roots.push_back(descriptor);
    llvm::sort(roots);
    for (uint64_t descriptor : roots) {
      obelisk::sim::StaticStateRootAttr policy =
          analysis->getRoots().lookup(descriptor);
      llvm::errs() << "  root " << descriptor << " width=" << policy.getWidth()
                   << " direct=" << (policy.getDirect() ? "true" : "false")
                   << " guarded=" << (policy.getGuarded() ? "true" : "false")
                   << " nba=" << (policy.getNba() ? "true" : "false") << "\n";
    }
    for (uint64_t descriptor : analysis->getNBARoots())
      llvm::errs() << "  nba-root " << descriptor << "\n";
    SmallVector<uint64_t> sites(analysis->getNBASites().begin(),
                                analysis->getNBASites().end());
    llvm::sort(sites);
    for (uint64_t site : sites)
      llvm::errs() << "  nba-site " << site << "\n";
    for (obelisk::sim::ComputeNBACommitAttr commit :
         analysis->getOrderedNBACommits())
      llvm::errs() << "  nba-commit " << commit.getId()
                   << " descriptor=" << commit.getEffect().getDescriptor()
                   << "\n";
    markAllAnalysesPreserved();
  }
};

} // namespace

namespace obelisk {

void registerStaticSpecializationAnalysisTestPass() {
  PassRegistration<StaticSpecializationAnalysisTestPass>();
}

} // namespace obelisk
