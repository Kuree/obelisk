#include "AnalysisTestPasses.h"
#include "obelisk/Analysis/DemandedBitAnalysis.h"
#include "obelisk/Analysis/IntegerRangeIPO.h"
#include "obelisk/Analysis/LogicBitAnalysis.h"
#include "obelisk/Analysis/ScopedIntegerRangeAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/IntegerRangeAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/Pass.h"

using namespace mlir;

namespace {
// Deliberately differs from arith.select's operand order and has no fold hook.
// Precision must come from the interface, not the opcode or constant folding.
class TestSelectOp final
    : public Op<TestSelectOp, OpTrait::NOperands<3>::Impl, OpTrait::OneResult,
                OpTrait::ZeroRegions, SelectLikeOpInterface::Trait,
                MemoryEffectOpInterface::Trait> {
public:
  using Op::Op;
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(TestSelectOp)
  static StringRef getOperationName() { return "value_test.select"; }
  static ArrayRef<StringRef> getAttributeNames() { return {}; }
  Value getFalseValue() { return getOperand(0); }
  Value getCondition() { return getOperand(1); }
  Value getTrueValue() { return getOperand(2); }
  void getEffects(SmallVectorImpl<MemoryEffects::EffectInstance> &) {}
};

class ValueTestDialect final : public Dialect {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ValueTestDialect)
  static StringRef getDialectNamespace() { return "value_test"; }
  explicit ValueTestDialect(MLIRContext *context)
      : Dialect(getDialectNamespace(), context,
                TypeID::get<ValueTestDialect>()) {
    addOperations<TestSelectOp>();
  }
};

class ValueInterfacesTestPass final
    : public PassWrapper<ValueInterfacesTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ValueInterfacesTestPass)
  StringRef getArgument() const final {
    return "test-obelisk-value-interfaces";
  }
  StringRef getDescription() const final {
    return "exercise value analyses through a second select interface model";
  }
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<ValueTestDialect>();
  }
  void runOnOperation() final {
    getOperation().walk([](arith::SelectOp select) {
      OpBuilder builder(select);
      OperationState state(select.getLoc(), TestSelectOp::getOperationName());
      state.addOperands({select.getFalseValue(), select.getCondition(),
                         select.getTrueValue()});
      state.addTypes(select.getType());
      state.addAttributes(select->getAttrs());
      Operation *replacement = builder.create(state);
      select.replaceAllUsesWith(replacement);
      select.erase();
    });
    getOperation().walk([&](obelisk::sim::SimDesignOp design) {
      auto domains = obelisk::StateDomainAnalysis::compute(design, false);
      if (failed(domains)) {
        signalPassFailure();
        return;
      }
      design.walk([&](obelisk::sim::SimFuncOp function) {
        obelisk::LogicBitAnalysis known(function);
        obelisk::analysis::DemandedBitAnalysis demanded(function);
        function.walk([&](Operation *op) {
          auto label = op->getAttrOfType<StringAttr>("test.fact");
          if (!label || op->getNumResults() != 1)
            return;
          Value result = op->getResult(0);
          llvm::errs() << label.getValue();
          if (auto bits = known.get(result)) {
            llvm::errs() << " zero=";
            bits->zero.print(llvm::errs(), false);
            llvm::errs() << " one=";
            bits->one.print(llvm::errs(), false);
            llvm::errs() << " known=";
            bits->known.print(llvm::errs(), false);
          }
          if (auto *mask = demanded.get(result)) {
            llvm::errs() << " demand=";
            mask->print(llvm::errs(), false);
          }
          llvm::errs() << " domain="
                       << obelisk::stringifyStateDomain(
                              domains->get(result).domain)
                       << '\n';
        });
      });
    });
  }
};

class IntegerInterfacesTestPass final
    : public PassWrapper<IntegerInterfacesTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(IntegerInterfacesTestPass)
  StringRef getArgument() const final {
    return "test-obelisk-integer-interfaces";
  }
  StringRef getDescription() const final {
    return "query Simulation operations using standard MLIR value interfaces";
  }
  void runOnOperation() final {
    DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
    solver.load<dataflow::DeadCodeAnalysis>();
    solver.load<dataflow::SparseConstantPropagation>();
    solver.load<dataflow::IntegerRangeAnalysis>();
    if (failed(solver.initializeAndRun(getOperation()))) {
      signalPassFailure();
      return;
    }
    getOperation().walk([&](Operation *op) {
      auto label = op->getAttrOfType<StringAttr>("test.fact");
      if (!label || op->getNumResults() != 1)
        return;
      llvm::errs() << label.getValue();
      if (auto cast = dyn_cast<CastOpInterface>(op))
        llvm::errs() << " cast="
                     << cast.areCastCompatible(op->getOperandTypes(),
                                               op->getResultTypes());
      if (auto view = dyn_cast<ViewLikeOpInterface>(op))
        llvm::errs() << " view=" << (view.getViewSource() == op->getOperand(0))
                     << " dest=" << (view.getViewDest() == op->getResult(0));
      if (auto *state = solver.lookupState<dataflow::IntegerValueRangeLattice>(
              op->getResult(0));
          state && !state->getValue().isUninitialized()) {
        const auto &range = state->getValue().getValue();
        llvm::errs() << " umin=";
        range.umin().print(llvm::errs(), false);
        llvm::errs() << " umax=";
        range.umax().print(llvm::errs(), false);
      }
      llvm::errs() << '\n';
    });
    markAllAnalysesPreserved();
  }
};

class ScopedIntegerRangesTestPass final
    : public PassWrapper<ScopedIntegerRangesTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ScopedIntegerRangesTestPass)
  StringRef getArgument() const final {
    return "test-obelisk-scoped-integer-ranges";
  }
  StringRef getDescription() const final {
    return "query selector ranges and bound retained scoped facts";
  }
  void runOnOperation() final {
    obelisk::analysis::prepareIntegerRangeIPO(getOperation());
    getOperation().walk([&](obelisk::sim::SimFuncOp function) {
      DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
      solver.load<dataflow::DeadCodeAnalysis>();
      solver.load<dataflow::SparseConstantPropagation>();
      solver.load<dataflow::IntegerRangeAnalysis>();
      auto *analysis =
          solver.load<obelisk::analysis::ScopedIntegerRangeAnalysis>(function);
      if (failed(solver.initializeAndRun(function))) {
        signalPassFailure();
        return;
      }
      size_t largest = 0;
      function.walk([&](Operation *op) {
        auto *state = solver.lookupState<obelisk::analysis::ScopedRangeLattice>(
            solver.getProgramPointBefore(op));
        if (!state || !state->reachable)
          return;
        largest = std::max<size_t>(largest, state->getRanges().size());
        auto label = op->getAttrOfType<StringAttr>("test.scoped");
        if (!label)
          return;
        llvm::errs() << label.getValue();
        if (auto range = analysis->getRange(op->getOperand(1), *state)) {
          llvm::errs() << " umin=";
          range->umin().print(llvm::errs(), false);
          llvm::errs() << " umax=";
          range->umax().print(llvm::errs(), false);
        } else
          llvm::errs() << " unknown";
        if (auto bounds =
                op->getAttrOfType<DenseI64ArrayAttr>("test.prove_bounds")) {
          bool valid = false;
          unsigned repeats = op->hasAttr("test.repeat_proof") ? 64 : 1;
          for (unsigned i = 0; i < repeats; ++i)
            valid = analysis->proveBounds(op->getOperand(1), *state,
                                          APInt(65, bounds[0], true),
                                          APInt(65, bounds[1], true));
          llvm::errs() << " proven=" << valid;
        }
        llvm::errs() << '\n';
      });
      if (auto limit = function->getAttrOfType<IntegerAttr>("test.scoped_max"))
        if (largest > limit.getUInt()) {
          function.emitError() << "retained " << largest
                               << " scoped facts; limit is " << limit.getUInt();
          signalPassFailure();
        }
    });
    getOperation().walk([&](Operation *op) {
      obelisk::sim::remove<obelisk::sim::metadata::ResultRanges>(op);
      obelisk::sim::remove<obelisk::sim::metadata::ResultExprs>(op);
      if (auto function = dyn_cast<obelisk::sim::SimFuncOp>(op))
        for (unsigned i = 0; i < function.getNumArguments(); ++i)
          function.removeArgAttr(
              i, obelisk::sim::metadata::IntegerRange::getNameStr());
    });
  }
};
} // namespace

void obelisk::registerValueInterfacesTestPass() {
  PassRegistration<ValueInterfacesTestPass>();
  PassRegistration<IntegerInterfacesTestPass>();
  PassRegistration<ScopedIntegerRangesTestPass>();
}
