#include "AnalysisTestPasses.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "talos/Analysis/CombinationalDependencyAnalysis.h"

using namespace mlir;
namespace {
class CombinationalDependencyTestPass
    : public PassWrapper<CombinationalDependencyTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CombinationalDependencyTestPass)
  StringRef getArgument() const final {
    return "test-talos-combinational-dependencies";
  }
  StringRef getDescription() const final {
    return "print whole-program statement and storage dependencies";
  }
  void runOnOperation() final {
    using Analysis = talos::analysis::CombinationalDependencyAnalysis;
    getOperation().walk([&](talos::sim::SimDesignOp design) {
      Analysis analysis(design);
      auto printAccesses = [&](ArrayRef<Analysis::Access> accesses) {
        for (const auto &access : accesses) {
          const auto &r = access.target;
          static const char *names[] = {"read", "write", "nba", "watch"};
          llvm::errs() << ' ' << names[static_cast<unsigned>(access.kind)]
                       << ':';
          if (r.descriptor)
            llvm::errs() << 's' << *r.descriptor;
          else if (r.formal)
            llvm::errs() << 'a' << *r.formal;
          else
            llvm::errs() << '?';
          llvm::errs() << '[' << r.low << ',' << r.low + r.width << ')';
          if (r.dynamic)
            llvm::errs() << '*';
        }
      };
      llvm::errs() << "dependencies @" << design.getSymName()
                   << " unknown=" << analysis.hasUnknownEffects() << '\n';
      for (auto function : design.getBody().getOps<talos::sim::SimFuncOp>()) {
        llvm::errs() << "summary @" << function.getSymName();
        if (const auto *summary = analysis.lookup(function)) {
          llvm::errs() << " opaque=" << summary->opaque;
          printAccesses(summary->accesses);
        } else
          llvm::errs() << " missing";
        llvm::errs() << '\n';
      }
      for (auto [id, node] : llvm::enumerate(analysis.getNodes())) {
        llvm::errs() << "node " << id << " @"
                     << SymbolTable::getSymbolName(node.owner).getValue() << ' '
                     << node.operation->getName() << " opaque=" << node.opaque;
        printAccesses(node.accesses);
        llvm::errs() << '\n';
      }
      for (const auto &edge : analysis.getEdges())
        llvm::errs() << "edge " << edge.source << " -> " << edge.target << ' '
                     << (edge.kind == Analysis::EdgeKind::SourceOrder
                             ? "order"
                             : "memory")
                     << '\n';
      for (const auto &component : analysis.getComponents()) {
        llvm::errs() << "component cyclic=" << component.cyclic << " nodes=";
        llvm::interleaveComma(component.nodes, llvm::errs());
        llvm::errs() << '\n';
      }
      for (const auto &node : analysis.getObservers()) {
        llvm::errs() << "observer @"
                     << SymbolTable::getSymbolName(node.owner).getValue();
        printAccesses(node.accesses);
        llvm::errs() << '\n';
      }
    });
    markAllAnalysesPreserved();
  }
};
} // namespace
void talos::registerCombinationalDependencyAnalysisTestPass() {
  PassRegistration<CombinationalDependencyTestPass>();
}
