#include "AnalysisTestPasses.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "obelisk/Analysis/ActivationMemoryAnalysis.h"
#include "obelisk/Analysis/ActivationRegionAnalysis.h"

using namespace mlir;
namespace {
class ActivationMemoryTestPass
    : public PassWrapper<ActivationMemoryTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ActivationMemoryTestPass)
  ActivationMemoryTestPass() = default;
  ActivationMemoryTestPass(const ActivationMemoryTestPass &other)
      : PassWrapper(other) {}
  StringRef getArgument() const final {
    return "test-obelisk-activation-memory";
  }
  StringRef getDescription() const final {
    return "print source-ordered activation memory facts";
  }
  Option<unsigned> maximumPartitions{*this, "maximum-partitions",
                                     llvm::cl::init(65536)};
  void runOnOperation() final {
    getOperation().walk([&](obelisk::sim::SimDesignOp design) {
      obelisk::analysis::HandleDataflowAnalysis handles(design);
      for (auto function : design.getBody().getOps<obelisk::sim::SimFuncOp>()) {
        obelisk::analysis::ActivationMemoryAnalysis memory(function, handles,
                                                           maximumPartitions);
        llvm::errs() << "activation @" << function.getSymName()
                     << " valid=" << memory.isValid()
                     << " complete=" << memory.hasCompleteWrites() << '\n';
        auto print = [&](StringRef name, auto ranges) {
          llvm::errs() << "  " << name;
          for (auto r : ranges)
            llvm::errs() << " s" << r.root << '[' << r.low << ','
                         << r.low + r.width << ')';
          llvm::errs() << '\n';
        };
        print("live-in", memory.getLiveIns());
        print("must-write", memory.getMustWrites());
        print("may-write", memory.getMayWrites());
        print("repeated", memory.getRepeatedWrites());
      }
    });
    markAllAnalysesPreserved();
  }
};
class ActivationRegionTestPass
    : public PassWrapper<ActivationRegionTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ActivationRegionTestPass)
  ActivationRegionTestPass() = default;
  ActivationRegionTestPass(const ActivationRegionTestPass &other)
      : PassWrapper(other) {}
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<func::FuncDialect, arith::ArithDialect,
                    cf::ControlFlowDialect, scf::SCFDialect, ub::UBDialect>();
  }
  StringRef getArgument() const final {
    return "test-obelisk-activation-regions";
  }
  StringRef getDescription() const final {
    return "print structured activation regions and their memory effects";
  }
  Option<bool> dump{*this, "dump", llvm::cl::init(false)};
  ListOption<std::string> functions{*this, "functions",
                                    llvm::cl::CommaSeparated};
  void runOnOperation() final {
    getOperation().walk([&](obelisk::sim::SimDesignOp design) {
      obelisk::analysis::HandleDataflowAnalysis handles(design);
      for (auto function : design.getBody().getOps<obelisk::sim::SimFuncOp>()) {
        if (!functions.empty() &&
            !llvm::is_contained(functions, function.getSymName().str()))
          continue;
        obelisk::analysis::ActivationRegionAnalysis analysis(function, handles);
        llvm::errs() << "regions @" << function.getSymName()
                     << " valid=" << analysis.isValid();
        if (!analysis.isValid())
          llvm::errs() << " reason=" << analysis.getFailureReason();
        llvm::errs() << '\n';
        for (const auto &node : analysis.getNodes()) {
          if (node.reads.empty() && node.writes.empty())
            continue;
          llvm::errs() << "  node " << node.operation->getName();
          auto print = [&](StringRef name, auto ranges) {
            for (auto range : ranges)
              llvm::errs() << ' ' << name << ":s" << range.root << '['
                           << range.low << ',' << range.low + range.width
                           << ')';
          };
          print("read", node.reads);
          print("write", node.writes);
          llvm::errs() << '\n';
        }
        if (dump && analysis.isValid())
          analysis.getStructuredFunction().print(llvm::errs());
      }
    });
    markAllAnalysesPreserved();
  }
};
} // namespace
void obelisk::registerActivationMemoryAnalysisTestPass() {
  PassRegistration<ActivationMemoryTestPass>();
  PassRegistration<ActivationRegionTestPass>();
}
