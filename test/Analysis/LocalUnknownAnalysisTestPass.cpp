//===- LocalUnknownAnalysisTestPass.cpp - Print gated activation reads ----===//

#include "AnalysisTestPasses.h"

#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Analysis/LocalUnknownAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

/// Prints each function's gate and the class of every read tagged with a
/// `test.read` name.
class LocalUnknownAnalysisTestPass
    : public PassWrapper<LocalUnknownAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LocalUnknownAnalysisTestPass)
  LocalUnknownAnalysisTestPass() = default;
  LocalUnknownAnalysisTestPass(const LocalUnknownAnalysisTestPass &pass)
      : PassWrapper(pass) {}

  StringRef getArgument() const final { return "test-obelisk-local-unknown"; }
  StringRef getDescription() const final {
    return "print the reads a gated two-state activation copy can make "
           "value-only";
  }

  void runOnOperation() final {
    for (auto design : getOperation().getOps<obelisk::sim::SimDesignOp>()) {
      using obelisk::analysis::LocalUnknownAnalysis;
      obelisk::analysis::HandleDataflowAnalysis handles(design);
      auto callees = LocalUnknownAnalysis::summarize(design, handles);
      for (auto function : design.getOps<obelisk::sim::SimFuncOp>()) {
        if (function.isExternal())
          continue;
        LocalUnknownAnalysis analysis(
            function, handles.analyze(function),
            LocalUnknownAnalysis::Options{nullptr, &callees, false, nullptr,
                                          checks});
        llvm::errs() << "local-unknown @" << function.getSymName();
        if (!analysis.getFailure().empty())
          llvm::errs() << " failed: " << analysis.getFailure();
        llvm::errs() << " benefit=" << analysis.getBenefit()
                     << " gate-words=" << analysis.getGateWords() << '\n';
        for (const auto &root : analysis.getGate()) {
          llvm::errs() << "  gate " << root.descriptor << ':';
          for (auto range : root.ranges)
            llvm::errs() << " [" << range.low << ','
                         << range.low + range.width << ')';
          llvm::errs() << '\n';
        }
        for (Operation *read : analysis.getReads())
          if (auto name = read->getAttrOfType<StringAttr>("test.read"))
            llvm::errs() << "  read " << name.getValue() << ": "
                         << obelisk::analysis::LocalUnknownAnalysis::stringify(
                                analysis.getKind(read))
                         << '\n';
      }
    }
  }

  Option<bool> checks{*this, "checks",
                      llvm::cl::desc("test reads where they load"),
                      llvm::cl::init(false)};
};

} // namespace

namespace obelisk {
void registerLocalUnknownAnalysisTestPass() {
  PassRegistration<LocalUnknownAnalysisTestPass>();
}
} // namespace obelisk
