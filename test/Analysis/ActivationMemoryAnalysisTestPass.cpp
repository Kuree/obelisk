#include "AnalysisTestPasses.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "obelisk/Analysis/ActivationControlFlow.h"
#include "obelisk/Analysis/ActivationMemoryAnalysis.h"
#include "obelisk/Analysis/ActivationRegionAnalysis.h"
#include "obelisk/Analysis/SSAValueAnalysis.h"
#include <memory>

using namespace mlir;
namespace {
class ActivationControlFlowTestPass
    : public PassWrapper<ActivationControlFlowTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ActivationControlFlowTestPass)
  StringRef getArgument() const final {
    return "test-obelisk-activation-control-flow";
  }
  StringRef getDescription() const final {
    return "print edge-threaded disposable activation CFGs";
  }
  void runOnOperation() final {
    getOperation().walk([&](func::FuncOp function) {
      if (function.isExternal())
        return;
      OwningOpRef<func::FuncOp> copy(cast<func::FuncOp>(function->clone()));
      bool changed = false;
      while (obelisk::analysis::threadActivationControlFlow(copy->getBody())) {
        changed = true;
        RewritePatternSet patterns(function.getContext());
        for (RegisteredOperationName name :
             function.getContext()->getRegisteredOperations())
          if (name.getDialectNamespace() == "arith" ||
              name.getDialectNamespace() == "cf")
            name.getCanonicalizationPatterns(patterns, function.getContext());
        if (failed(applyPatternsGreedily(*copy, std::move(patterns))) ||
            failed(verify(*copy))) {
          signalPassFailure();
          return;
        }
      }
      llvm::errs() << "control @" << function.getSymName()
                   << " changed=" << changed << '\n';
      copy->print(llvm::errs());
      llvm::errs() << '\n';
    });
    markAllAnalysesPreserved();
  }
};
class RegionValueDependencyTestPass
    : public PassWrapper<RegionValueDependencyTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(RegionValueDependencyTestPass)
  StringRef getArgument() const final {
    return "test-obelisk-region-value-dependencies";
  }
  StringRef getDescription() const final {
    return "print result-specific SSA dependencies through region yields";
  }
  void runOnOperation() final {
    getOperation().walk([&](func::FuncOp function) {
      if (function.isExternal())
        return;
      SmallVector<Value> inputs(function.getArguments());
      Operation *root = function;
      function.walk([&](Operation *op) {
        if (op->hasAttr("test.root"))
          root = op;
      });
      obelisk::analysis::RegionValueDependencyAnalysis analysis(root, inputs);
      llvm::errs() << "values @" << function.getSymName()
                   << " valid=" << analysis.isValid() << '\n';
      if (!analysis.isValid())
        return;
      function.walk([&](Operation *op) {
        if (!op->hasAttr("test.values"))
          return;
        for (auto [index, result] : llvm::enumerate(op->getResults())) {
          llvm::errs() << "  " << op->getName() << " result " << index << ':';
          for (Value input : analysis.getInputs(result))
            llvm::errs() << " arg" << cast<BlockArgument>(input).getArgNumber();
          llvm::errs() << '\n';
        }
        if (op->getNumResults() == 0) {
          llvm::errs() << "  " << op->getName() << " effect:";
          for (Value input : analysis.getInputs(op))
            llvm::errs() << " arg" << cast<BlockArgument>(input).getArgNumber();
          llvm::errs() << '\n';
        }
      });
    });
    markAllAnalysesPreserved();
  }
};
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
  Option<bool> values{*this, "values", llvm::cl::init(false)};
  void runOnOperation() final {
    getOperation().walk([&](obelisk::sim::SimDesignOp design) {
      obelisk::analysis::HandleDataflowAnalysis handles(design);
      for (auto function : design.getBody().getOps<obelisk::sim::SimFuncOp>()) {
        obelisk::analysis::ActivationMemoryAnalysis memory(
            function, handles, maximumPartitions,
            values ? obelisk::analysis::ActivationMemoryAnalysis::
                         DependencyMode::ReachingValues
                   : obelisk::analysis::ActivationMemoryAnalysis::
                         DependencyMode::Accesses);
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
        if (values) {
          llvm::errs() << "  values valid=" << memory.hasValueDependencies()
                       << '\n';
          print("value-input", memory.getValueInputs());
          function.walk([&](obelisk::sim::SimRefStoreOp store) {
            auto found = memory.getPublications().find(store);
            if (found != memory.getPublications().end())
              for (const auto &lane : found->second) {
                llvm::errs() << "  output s" << lane.output.root << '['
                             << lane.output.low << ','
                             << lane.output.low + lane.output.width << ')';
                for (auto input : lane.inputs)
                  llvm::errs() << " s" << input.root << '[' << input.low << ','
                               << input.low + input.width << ')';
                llvm::errs() << '\n';
              }
          });
        }
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
  Option<bool> threadControl{*this, "thread-control", llvm::cl::init(false)};
  Option<bool> publications{*this, "publications", llvm::cl::init(false)};
  Option<bool> regionOrder{*this, "order", llvm::cl::init(false)};
  Option<bool> reachingValues{*this, "reaching-values", llvm::cl::init(false)};
  ListOption<std::string> functions{*this, "functions",
                                    llvm::cl::CommaSeparated};
  void runOnOperation() final {
    getOperation().walk([&](obelisk::sim::SimDesignOp design) {
      obelisk::analysis::HandleDataflowAnalysis handles(design);
      SmallVector<std::unique_ptr<obelisk::analysis::ActivationRegionAnalysis>>
          owned;
      for (auto function : design.getBody().getOps<obelisk::sim::SimFuncOp>()) {
        if (!functions.empty() &&
            !llvm::is_contained(functions, function.getSymName().str()))
          continue;
        auto trace =
            [&](obelisk::analysis::ActivationRegionAnalysis::TracePhase phase,
                Operation *view) {
              if (!dump)
                return;
              llvm::errs()
                  << "activation "
                  << (phase == obelisk::analysis::ActivationRegionAnalysis::
                                   TracePhase::Initial
                          ? "initial"
                          : "resume")
                  << " @" << function.getSymName() << '\n';
              view->print(llvm::errs());
              llvm::errs() << '\n';
            };
        auto proof =
            std::make_unique<obelisk::analysis::ActivationRegionAnalysis>(
                function, handles, trace,
                reachingValues ? obelisk::analysis::ActivationMemoryAnalysis::
                                     DependencyMode::ReachingValues
                               : obelisk::analysis::ActivationMemoryAnalysis::
                                     DependencyMode::Accesses,
                threadControl ? obelisk::analysis::ActivationRegionAnalysis::
                                    ControlFlowPreparation::ThreadDispatch
                              : obelisk::analysis::ActivationRegionAnalysis::
                                    ControlFlowPreparation::Canonical);
        auto &analysis = *proof;
        llvm::errs() << "regions @" << function.getSymName()
                     << " valid=" << analysis.isValid();
        if (!analysis.isValid())
          llvm::errs() << " reason="
                       << obelisk::analysis::stringifyActivationFailure(
                              analysis.getFailureReason());
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
        if (publications && analysis.isValid()) {
          auto values = obelisk::analysis::getActivationPublications(analysis);
          if (values)
            for (const auto &publication : *values) {
              llvm::errs() << "  publication";
              auto print = [&](StringRef kind, auto ranges) {
                for (auto range : ranges)
                  llvm::errs()
                      << ' ' << kind << ":s" << range.root << '[' << range.low
                      << ',' << range.low + range.width << ')';
              };
              print("read", publication.reads);
              print("write", publication.writes);
              llvm::errs() << '\n';
            }
        }
        if (publications || regionOrder)
          owned.push_back(std::move(proof));
      }
      if (publications || regionOrder) {
        SmallVector<const obelisk::analysis::ActivationRegionAnalysis *> views;
        for (const auto &view : owned)
          views.push_back(view.get());
        SmallVector<obelisk::analysis::ActivationRegionStep> cycle;
        if (publications) {
          auto order =
              obelisk::analysis::getActivationPublicationOrder(views, &cycle);
          llvm::errs() << "publication graph: "
                       << (order           ? "acyclic"
                           : cycle.empty() ? "unavailable"
                                           : "cyclic")
                       << '\n';
        }
        if (regionOrder) {
          auto order =
              obelisk::analysis::getActivationRegionOrder(views, &cycle);
          llvm::errs() << "region order: "
                       << (order           ? "acyclic"
                           : cycle.empty() ? "unavailable"
                                           : "cyclic")
                       << '\n';
          if (order)
            for (const auto &step : *order)
              step.operation->walk([&](obelisk::sim::SimRefStoreOp store) {
                auto found =
                    step.activation->getMemory().getWrites().find(store);
                if (found != step.activation->getMemory().getWrites().end())
                  for (auto range : found->second)
                    llvm::errs()
                        << "  write s" << range.root << '[' << range.low << ','
                        << range.low + range.width << ")\n";
              });
        }
      }
    });
    markAllAnalysesPreserved();
  }
};
} // namespace
void obelisk::registerActivationMemoryAnalysisTestPass() {
  PassRegistration<ActivationControlFlowTestPass>();
  PassRegistration<RegionValueDependencyTestPass>();
  PassRegistration<ActivationMemoryTestPass>();
  PassRegistration<ActivationRegionTestPass>();
}
