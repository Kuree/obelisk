//===- SimulationScheduleAnalysisTestPass.cpp - Print schedule facts -----===//

#include "AnalysisTestPasses.h"

#include "obelisk/Analysis/SimulationScheduleAnalysis.h"

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
    return "test-obelisk-simulation-schedule-analysis";
  }
  StringRef getDescription() const final {
    return "print shared native and bytecode schedule ranks";
  }

  void runOnOperation() final {
    SmallVector<obelisk::sim::SimDesignOp> designs(
        getOperation().getOps<obelisk::sim::SimDesignOp>());
    llvm::sort(designs, [](auto lhs, auto rhs) {
      return lhs.getSymName() < rhs.getSymName();
    });
    for (obelisk::sim::SimDesignOp design : designs) {
      FailureOr<obelisk::analysis::SimulationScheduleAnalysis> analysis =
          obelisk::analysis::SimulationScheduleAnalysis::compute(design);
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
  printDesign(obelisk::sim::SimDesignOp design,
              const obelisk::analysis::SimulationScheduleAnalysis &analysis) {
    llvm::errs() << "schedule @" << design.getSymName() << "\n";
    SmallVector<obelisk::sim::SimFuncOp> functions;
    design.walk([&](obelisk::sim::SimFuncOp function) {
      if (analysis.getEntryRank(function.getOperation()))
        functions.push_back(function);
    });
    llvm::sort(functions, [](auto lhs, auto rhs) {
      return lhs.getSymName() < rhs.getSymName();
    });
    for (obelisk::sim::SimFuncOp function : functions) {
      llvm::errs() << "  func @" << function.getSymName() << " entry="
                   << *analysis.getEntryRank(function.getOperation()) << "\n";
      for (auto [index, block] : llvm::enumerate(function.getBody()))
        if (std::optional<uint32_t> rank = analysis.getBlockRank(&block))
          llvm::errs() << "    bb" << index << " rank=" << *rank << "\n";
    }
  }
};

} // namespace

namespace obelisk {

void registerSimulationScheduleAnalysisTestPass() {
  PassRegistration<SimulationScheduleAnalysisTestPass>();
}

} // namespace obelisk
