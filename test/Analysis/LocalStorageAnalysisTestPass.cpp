#include "AnalysisTestPasses.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "obelisk/Analysis/LocalStorageAnalysis.h"

using namespace mlir;
namespace {
class LocalStorageTestPass final
    : public PassWrapper<LocalStorageTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LocalStorageTestPass)
  StringRef getArgument() const final { return "test-obelisk-local-storage"; }
  StringRef getDescription() const final {
    return "print private storage snapshot conflicts";
  }
  void runOnOperation() final {
    getOperation().walk([](obelisk::sim::SimFuncOp function) {
      obelisk::analysis::LocalStorageAnalysis locals(function);
      SmallVector<obelisk::sim::SimRefLoadOp> loads;
      function.walk([&](obelisk::sim::SimRefLoadOp load) {
        if (load->hasAttr("test.snapshot"))
          loads.push_back(load);
      });
      auto borrowed = locals.getBorrowableLoads(loads);
      for (auto load : loads)
        llvm::errs()
            << load->getAttrOfType<StringAttr>("test.snapshot").getValue()
            << ": " << (borrowed.contains(load.getResult()) ? "borrow" : "copy")
            << '\n';
    });
    markAllAnalysesPreserved();
  }
};
} // namespace
void obelisk::registerLocalStorageAnalysisTestPass() {
  PassRegistration<LocalStorageTestPass>();
}
