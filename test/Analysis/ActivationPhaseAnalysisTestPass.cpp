#include "AnalysisTestPasses.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "obelisk/Analysis/ActivationPhaseAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/StringExtras.h"

using namespace mlir;
namespace {
using Analysis = obelisk::analysis::ActivationPhaseAnalysis;

/// Print a phase set as `-`, `any`, or a comma list where `32+` covers phase
/// 32 and every later phase.
void printSet(raw_ostream &os, Analysis::PhaseSet phases) {
  if (phases == Analysis::anyPhase) {
    os << "any";
    return;
  }
  if (!phases) {
    os << '-';
    return;
  }
  llvm::ListSeparator separator(",");
  for (unsigned bit = 0; bit != 32; ++bit)
    if (phases & (Analysis::PhaseSet{1} << bit))
      os << separator << bit + 1 << (bit == 31 ? "+" : "");
}
/// `{time-zero phases|later-slot phases}`.
void printPhases(raw_ostream &os, Analysis::Phases phases) {
  os << '{';
  printSet(os, phases.zero);
  os << '|';
  printSet(os, phases.later);
  os << '}';
}

class ActivationPhaseAnalysisTestPass
    : public PassWrapper<ActivationPhaseAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ActivationPhaseAnalysisTestPass)
  StringRef getArgument() const final {
    return "test-obelisk-activation-phases";
  }
  StringRef getDescription() const final {
    return "print time-slot phases of activations and root changes";
  }
  void runOnOperation() final {
    namespace sim = obelisk::sim;
    getOperation().walk([&](sim::SimDesignOp design) {
      obelisk::analysis::HandleDataflowAnalysis handles(design);
      Analysis analysis(design, handles);
      llvm::errs() << "phases " << design.getSymName()
                   << " valid=" << analysis.isValid() << '\n';
      for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
        if (function.isExternal() ||
            function.getEntryKind() == sim::EntryKind::RootInitializer ||
            function.getEntryKind() == sim::EntryKind::Function)
          continue;
        llvm::errs() << "  @" << function.getSymName() << ':';
        unsigned index = 0;
        for (Block &block : function.getBody()) {
          llvm::errs() << " ^" << index++;
          printPhases(llvm::errs(), analysis.getPhases(&block));
        }
        llvm::errs() << '\n';
      }
      for (auto storage :
           design.getBody().front().getOps<sim::SimStorageDeclOp>()) {
        llvm::errs() << "  s" << storage.getId() << " changes ";
        printPhases(llvm::errs(), analysis.getChangePhases(storage.getId()));
        llvm::errs() << '\n';
      }
    });
    markAllAnalysesPreserved();
  }
};
} // namespace

void obelisk::registerActivationPhaseAnalysisTestPass() {
  PassRegistration<ActivationPhaseAnalysisTestPass>();
}
