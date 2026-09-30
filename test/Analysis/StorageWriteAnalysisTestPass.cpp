#include "AnalysisTestPasses.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "obelisk/Analysis/StorageWriteAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace {
class StorageWriteAnalysisTestPass
    : public PassWrapper<StorageWriteAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(StorageWriteAnalysisTestPass)
  StringRef getArgument() const final { return "test-obelisk-storage-writes"; }
  StringRef getDescription() const final {
    return "print storage-write lattice facts";
  }
  void runOnOperation() final {
    getOperation().walk([&](obelisk::sim::SimFuncOp function) {
      obelisk::analysis::HandleDataflowAnalysis handles(
          function->getParentOfType<obelisk::sim::SimDesignOp>());
      obelisk::analysis::StorageWriteAnalysis analysis(function, handles);
      llvm::errs() << "function " << function.getSymName() << '\n';
      SmallVector<Operation *> writes;
      function.walk([&](Operation *op) {
        Value destination;
        if (auto store = dyn_cast<obelisk::sim::SimRefStoreOp>(op))
          destination = store.getReference();
        else if (auto enqueue = dyn_cast<obelisk::sim::SimNBAEnqueueOp>(op))
          destination = enqueue.getDestination();
        if (!destination)
          return;
        llvm::errs() << "write " << writes.size()
                     << " once=" << analysis.executesAtMostOnce(op) << ' ';
        analysis.lookup(destination).print(llvm::errs());
        auto certificate = analysis.getHandles().certificates.find(destination);
        llvm::errs() << " fixed="
                     << (certificate !=
                             analysis.getHandles().certificates.end() &&
                         certificate->second.constantAddress)
                     << " direct="
                     << analysis.hasDirectDynamicSelection(destination);
        auto effect = analysis.getHandles().facts.find(destination);
        if (effect != analysis.getHandles().facts.end())
          llvm::errs() << " effect=" << effect->second.low << ':'
                       << effect->second.width
                       << " dynamic=" << effect->second.dynamic;
        llvm::errs() << '\n';
        writes.push_back(op);
      });
      for (auto [i, lhs] : llvm::enumerate(writes))
        for (auto [j, rhs] : llvm::enumerate(writes))
          if (i < j)
            llvm::errs() << "pair " << i << ',' << j << " exclusive="
                         << analysis.mutuallyExclusive(lhs, rhs) << '\n';
    });
    markAllAnalysesPreserved();
  }
};
} // namespace
void obelisk::registerStorageWriteAnalysisTestPass() {
  PassRegistration<StorageWriteAnalysisTestPass>();
}
