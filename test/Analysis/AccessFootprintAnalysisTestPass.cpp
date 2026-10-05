#include "AnalysisTestPasses.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "obelisk/Analysis/AccessFootprintAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace {
class AccessFootprintTestPass
    : public PassWrapper<AccessFootprintTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(AccessFootprintTestPass)
  StringRef getArgument() const final {
    return "test-obelisk-access-footprints";
  }
  StringRef getDescription() const final {
    return "print reachable reference access footprints";
  }
  void runOnOperation() final {
    getOperation().walk([&](obelisk::sim::SimFuncOp function) {
      auto handles =
          obelisk::analysis::HandleDataflowAnalysis(function).analyze(function);
      obelisk::analysis::AccessFootprintAnalysis footprints(function, handles);
      function.walk([&](Operation *op) {
        auto name = op->getAttrOfType<StringAttr>("test.footprint");
        if (!name)
          return;
        for (Value value : op->getOperands()) {
          if (!isa<obelisk::sim::RefType>(value.getType()))
            continue;
          llvm::errs() << function.getSymName() << ": " << name.getValue();
          if (auto *ranges = footprints.lookup(op, value))
            for (auto [low, width] : *ranges)
              llvm::errs() << " [" << low << "," << low + width << ")";
          else
            llvm::errs() << " conservative";
          llvm::errs() << '\n';
        }
      });
    });
    markAllAnalysesPreserved();
  }
};
} // namespace

void obelisk::registerAccessFootprintAnalysisTestPass() {
  PassRegistration<AccessFootprintTestPass>();
}
