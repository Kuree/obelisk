#include "AnalysisTestPasses.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "obelisk/Analysis/SSAValueAnalysis.h"
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

class StorageFlowTestPass
    : public PassWrapper<StorageFlowTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(StorageFlowTestPass)
  StringRef getArgument() const final { return "test-obelisk-storage-flow"; }
  StringRef getDescription() const final {
    return "print activation definitions and lifetime execution bounds";
  }
  void runOnOperation() final {
    namespace sim = obelisk::sim;
    getOperation().walk([&](sim::SimFuncOp function) {
      SmallVector<Operation *> definitions;
      function.walk([&](sim::SimRefStoreOp op) { definitions.push_back(op); });
      auto isBarrier = [](Operation *op) {
        return sim::isSuspensionOp(op) ||
               isa<sim::SimCallOp, sim::SimSpawnOp>(op);
      };
      obelisk::analysis::MustDefinitionAnalysis must(function, definitions,
                                                     isBarrier);
      obelisk::analysis::NoBarrierAnalysis prefix(function, isBarrier);
      obelisk::analysis::WriteExecutionBounds lifetime(function, definitions,
                                                       true, false);
      llvm::errs() << "function " << function.getSymName() << '\n';
      for (auto [index, definition] : llvm::enumerate(definitions))
        llvm::errs() << "definition " << index << " lifetime-once="
                     << lifetime.executesAtMostOnce(definition) << '\n';
      unsigned read = 0;
      function.walk([&](sim::SimRefLoadOp load) {
        llvm::errs() << "read " << read++
                     << " prefix=" << prefix.isSafeBefore(load) << " must=";
        for (auto [index, definition] : llvm::enumerate(definitions))
          if (must.containsBefore(definition, load))
            llvm::errs() << index << ',';
        llvm::errs() << '\n';
      });
    });
    markAllAnalysesPreserved();
  }
};

class SemanticRootsTestPass
    : public PassWrapper<SemanticRootsTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(SemanticRootsTestPass)
  StringRef getArgument() const final { return "test-obelisk-semantic-roots"; }
  StringRef getDescription() const final {
    return "print CFG semantic value roots";
  }
  void runOnOperation() final {
    getOperation().walk([&](obelisk::sim::SimFuncOp function) {
      obelisk::analysis::SemanticValueRootAnalysis roots(function);
      DenseMap<Block *, unsigned> blocks;
      for (auto [index, block] : llvm::enumerate(function.getBody()))
        blocks[&block] = index;
      function.walk([&](Operation *op) {
        auto query = op->getAttrOfType<StringAttr>("test.root");
        if (!query)
          return;
        Value root = roots.lookup(op->getOperand(0));
        llvm::errs() << query.getValue() << ": ";
        if (auto argument = dyn_cast<BlockArgument>(root))
          llvm::errs() << "block " << blocks.lookup(argument.getOwner())
                       << " argument " << argument.getArgNumber();
        else if (Operation *definition = root.getDefiningOp()) {
          if (auto name = definition->getAttrOfType<StringAttr>("test.value"))
            llvm::errs() << name.getValue();
          else
            llvm::errs() << definition->getName();
        }
        llvm::errs() << '\n';
      });
    });
    markAllAnalysesPreserved();
  }
};
} // namespace
void obelisk::registerStorageWriteAnalysisTestPass() {
  PassRegistration<StorageWriteAnalysisTestPass>();
  PassRegistration<StorageFlowTestPass>();
  PassRegistration<SemanticRootsTestPass>();
}
