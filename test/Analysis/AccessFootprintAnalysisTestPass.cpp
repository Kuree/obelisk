#include "AnalysisTestPasses.h"
#include "mlir/IR/AsmState.h"
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
  AccessFootprintTestPass() = default;
  AccessFootprintTestPass(const AccessFootprintTestPass &other)
      : PassWrapper(other) {}
  Option<bool> printHandles{
      *this, "print-handles",
      llvm::cl::desc("print block executability and SSA handle facts"),
      llvm::cl::init(false)};
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
      if (printHandles) {
        AsmState names(function);
        llvm::errs() << function.getSymName() << '\n';
        auto print = [&](Value value) {
          if (!isa<obelisk::sim::RefType, obelisk::sim::DriverType,
                   obelisk::sim::NetType>(value.getType()))
            return;
          llvm::errs() << "  ";
          value.printAsOperand(llvm::errs(), names);
          auto found = handles.facts.find(value);
          if (found == handles.facts.end())
            llvm::errs() << " no fact";
          else {
            const auto &fact = found->second;
            llvm::errs() << " resource=" << unsigned(fact.resource);
            if (fact.descriptor)
              llvm::errs() << " descriptor=" << *fact.descriptor;
            if (fact.formal)
              llvm::errs() << " formal=" << *fact.formal;
            llvm::errs() << " low=" << fact.low << " width=" << fact.width
                         << " dynamic=" << fact.dynamic;
          }
          llvm::errs() << '\n';
        };
        for (auto [index, block] : llvm::enumerate(function.getBody())) {
          llvm::errs() << "block " << index
                       << " executable=" << handles.isExecutable(&block)
                       << '\n';
          for (Value value : block.getArguments())
            print(value);
          for (Operation &op : block)
            for (Value value : op.getResults())
              print(value);
        }
      }
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
