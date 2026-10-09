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
  LocalStorageTestPass() = default;
  LocalStorageTestPass(const LocalStorageTestPass &other)
      : PassWrapper(other) {}
  Option<std::string> functions{*this, "functions",
                                llvm::cl::desc("Function name to inspect"),
                                llvm::cl::init("")};
  Option<bool> allocations{*this, "allocations",
                           llvm::cl::desc("Print allocation classification"),
                           llvm::cl::init(false)};
  Option<bool> coroutine{*this, "coroutine",
                         llvm::cl::desc("Use coroutine local lifetimes"),
                         llvm::cl::init(false)};
  StringRef getArgument() const final { return "test-obelisk-local-storage"; }
  StringRef getDescription() const final {
    return "print private storage snapshot conflicts";
  }
  void runOnOperation() final {
    getOperation().walk([&](obelisk::sim::SimFuncOp function) {
      if (!functions.empty() && function.getSymName() != functions)
        return;
      using Analysis = obelisk::analysis::LocalStorageAnalysis;
      Analysis locals(function, coroutine ? Analysis::Lifetime::Coroutine
                                          : Analysis::Lifetime::Invocation);
      if (allocations)
        function.walk([&](obelisk::sim::SimRefAllocOp allocation) {
          if (locals.lookup(allocation.getResult())) {
            llvm::errs() << "local allocation\n";
            return;
          }
          llvm::errs() << "runtime ";
          allocation.print(llvm::errs());
          llvm::errs() << '\n';
          for (Operation *user : allocation.getResult().getUsers()) {
            llvm::errs() << "  user: ";
            user->print(llvm::errs());
            llvm::errs() << '\n';
          }
        });
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
