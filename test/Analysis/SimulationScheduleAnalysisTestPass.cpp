//===- SimulationScheduleAnalysisTestPass.cpp - Print schedule facts -----===//

#include "AnalysisTestPasses.h"

#include "talos/Analysis/SimulationScheduleAnalysis.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

class SimulationScheduleAnalysisTestPass
    : public PassWrapper<SimulationScheduleAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      SimulationScheduleAnalysisTestPass)

  StringRef getArgument() const final {
    return "test-talos-simulation-schedule-analysis";
  }
  StringRef getDescription() const final {
    return "print shared native and bytecode schedule ranks";
  }

  void runOnOperation() final {
    SmallVector<talos::sim::SimDesignOp> designs(
        getOperation().getOps<talos::sim::SimDesignOp>());
    llvm::sort(designs, [](auto lhs, auto rhs) {
      return lhs.getSymName() < rhs.getSymName();
    });
    for (talos::sim::SimDesignOp design : designs) {
      FailureOr<talos::analysis::SimulationScheduleAnalysis> analysis =
          talos::analysis::SimulationScheduleAnalysis::compute(design);
      if (failed(analysis)) {
        signalPassFailure();
        return;
      }
      printDesign(design, *analysis);
    }
    markAllAnalysesPreserved();
  }

private:
  static void
  printDesign(talos::sim::SimDesignOp design,
              const talos::analysis::SimulationScheduleAnalysis &analysis) {
    llvm::errs() << "schedule @" << design.getSymName() << "\n";
    SmallVector<talos::sim::SimFuncOp> functions;
    design.walk([&](talos::sim::SimFuncOp function) {
      if (analysis.getEntryRank(function.getOperation()))
        functions.push_back(function);
    });
    llvm::sort(functions, [](auto lhs, auto rhs) {
      return lhs.getSymName() < rhs.getSymName();
    });
    for (talos::sim::SimFuncOp function : functions) {
      llvm::errs() << "  func @" << function.getSymName() << " entry="
                   << *analysis.getEntryRank(function.getOperation()) << "\n";
      for (auto [index, block] : llvm::enumerate(function.getBody()))
        if (std::optional<uint32_t> rank = analysis.getBlockRank(&block))
          llvm::errs() << "    bb" << index << " rank=" << *rank << "\n";
    }
  }
};

} // namespace

namespace talos {

void registerSimulationScheduleAnalysisTestPass() {
  PassRegistration<SimulationScheduleAnalysisTestPass>();
}

} // namespace talos
