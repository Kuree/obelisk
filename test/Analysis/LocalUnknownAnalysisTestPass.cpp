//===- LocalUnknownAnalysisTestPass.cpp - Print gated activation reads ----===//

#include "AnalysisTestPasses.h"

#include "talos/Analysis/HandleDataflowAnalysis.h"
#include "talos/Analysis/LocalUnknownAnalysis.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

/// Prints each function's gate, the class of every read tagged with a
/// `test.read` name, and whether each store tagged `test.store` is known.
class LocalUnknownAnalysisTestPass
    : public PassWrapper<LocalUnknownAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LocalUnknownAnalysisTestPass)
  LocalUnknownAnalysisTestPass() = default;
  LocalUnknownAnalysisTestPass(const LocalUnknownAnalysisTestPass &pass)
      : PassWrapper(pass) {}

  StringRef getArgument() const final { return "test-talos-local-unknown"; }
  StringRef getDescription() const final {
    return "print the reads a gated two-state activation copy can make "
           "value-only";
  }

  void runOnOperation() final {
    for (auto design : getOperation().getOps<talos::sim::SimDesignOp>()) {
      using talos::analysis::LocalUnknownAnalysis;
      talos::analysis::HandleDataflowAnalysis handles(design);
      auto callees = LocalUnknownAnalysis::summarize(design, handles);
      for (auto function : design.getOps<talos::sim::SimFuncOp>()) {
        if (function.isExternal())
          continue;
        LocalUnknownAnalysis analysis(
            function, handles.analyze(function),
            LocalUnknownAnalysis::Options{nullptr, &callees, false, nullptr,
                                          checks});
        llvm::errs() << "local-unknown @" << function.getSymName();
        if (!analysis.getFailure().empty())
          llvm::errs() << " failed: " << analysis.getFailure();
        llvm::errs() << " benefit=" << analysis.getBenefit() << " gate:";
        for (uint64_t root : analysis.getGate())
          llvm::errs() << ' ' << root;
        llvm::errs() << '\n';
        for (Operation *read : analysis.getReads())
          if (auto name = read->getAttrOfType<StringAttr>("test.read"))
            llvm::errs() << "  read " << name.getValue() << ": "
                         << talos::analysis::LocalUnknownAnalysis::stringify(
                                analysis.getKind(read))
                         << '\n';
        function.walk([&](talos::sim::SimRefStoreOp store) {
          if (auto name = store->getAttrOfType<StringAttr>("test.store"))
            llvm::errs() << "  store " << name.getValue() << ": "
                         << (analysis.isKnownStore(store) ? "known"
                                                          : "four-state")
                         << '\n';
        });
      }
    }
  }

  Option<bool> checks{*this, "checks",
                      llvm::cl::desc("test reads where they load"),
                      llvm::cl::init(false)};
};

} // namespace

namespace talos {
void registerLocalUnknownAnalysisTestPass() {
  PassRegistration<LocalUnknownAnalysisTestPass>();
}
} // namespace talos
