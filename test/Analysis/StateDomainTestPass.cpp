//===- StateDomainTestPass.cpp - Print whole-value state facts -----------===//

#include "AnalysisTestPasses.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"

#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

#include <functional>
#include <optional>

using namespace mlir;

namespace {

bool shouldPrint(Value value) {
  std::function<bool(Type)> containsLogic = [&](Type type) {
    if (isa<obelisk::sim::LogicType>(type))
      return true;
    if (!obelisk::sim::isAggregateType(type))
      return false;
    for (unsigned index = 0;
         index < obelisk::sim::getAggregateNumElements(type); ++index)
      if (containsLogic(obelisk::sim::getAggregateElementType(type, index)))
        return true;
    return false;
  };
  if (containsLogic(value.getType()))
    return true;
  auto result = dyn_cast<OpResult>(value);
  return result && isa<obelisk::sim::SimLogicCompareOp>(result.getOwner());
}

class StateDomainTestPass
    : public PassWrapper<StateDomainTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(StateDomainTestPass)
  StateDomainTestPass() = default;
  StateDomainTestPass(const StateDomainTestPass &other) : PassWrapper(other) {}

  StringRef getArgument() const final {
    return "test-obelisk-sim-state-domain";
  }
  StringRef getDescription() const final {
    return "print read-only Obelisk simulation state-domain facts";
  }

  void runOnOperation() final {
    SmallVector<obelisk::sim::SimDesignOp> designs(
        getOperation().getOps<obelisk::sim::SimDesignOp>());
    llvm::sort(designs, [](auto lhs, auto rhs) {
      return lhs.getSymName() < rhs.getSymName();
    });
    for (obelisk::sim::SimDesignOp design : designs) {
      FailureOr<obelisk::StateDomainAnalysis> analysis = failure();
      if (shared) {
        auto proofs =
            obelisk::StateDomainAnalysis::computeForSpecialization(design);
        if (succeeded(proofs))
          analysis = std::move(proofs->first);
      } else {
        analysis = obelisk::StateDomainAnalysis::compute(design);
      }
      if (failed(analysis)) {
        signalPassFailure();
        return;
      }
      printDesign(design, *analysis);
    }
    markAllAnalysesPreserved();
  }

  Option<bool> shared{*this, "shared", llvm::cl::init(false),
                      llvm::cl::desc("share specialization proof indexes")};

private:
  static void printDesign(obelisk::sim::SimDesignOp design,
                          const obelisk::StateDomainAnalysis &analysis) {
    llvm::errs() << "state-domain @" << design.getSymName() << "\n";
    for (const obelisk::InductiveStateRoot &root :
         analysis.getInductiveRoots()) {
      StringRef resource = "unknown";
      if (root.resource == obelisk::schedule::ComputeResourceKind::Storage)
        resource = "storage";
      else if (root.resource == obelisk::schedule::ComputeResourceKind::Net)
        resource = "net";
      llvm::errs() << "root " << resource << " " << root.descriptor
                   << ": inductive-two-state\n";
    }
    SmallVector<obelisk::sim::SimFuncOp> functions(
        design.getBody().front().getOps<obelisk::sim::SimFuncOp>());
    llvm::sort(functions, [](auto lhs, auto rhs) {
      return lhs.getSymName() < rhs.getSymName();
    });
    for (obelisk::sim::SimFuncOp function : functions) {
      llvm::errs() << "func @" << function.getSymName() << "\n";
      SmallVector<Block *> blocks;
      function.walk<WalkOrder::PreOrder>(
          [&](Block *block) { blocks.push_back(block); });
      for (auto [blockIndex, block] : llvm::enumerate(blocks)) {
        for (BlockArgument argument : block->getArguments()) {
          if (shouldPrint(argument))
            printFact(analysis, blockIndex, std::nullopt,
                      argument.getArgNumber(), argument);
        }
        for (auto [opIndex, operation] : llvm::enumerate(*block))
          for (auto [resultIndex, result] :
               llvm::enumerate(operation.getResults()))
            if (shouldPrint(result))
              printFact(analysis, blockIndex, opIndex, resultIndex, result);
      }
    }
  }

  static void printFact(const obelisk::StateDomainAnalysis &analysis,
                        unsigned block, std::optional<unsigned> operation,
                        unsigned valueIndex, Value value) {
    obelisk::StateDomainFact fact = analysis.get(value);
    llvm::errs() << "  bb" << block;
    if (operation)
      llvm::errs() << ".op" << *operation << ".result";
    else
      llvm::errs() << ".arg";
    llvm::errs() << valueIndex << ": "
                 << obelisk::stringifyStateDomain(fact.domain) << " ("
                 << obelisk::stringifyStateDomainReason(fact.reason) << ")\n";
  }
};

/// Remove a marked declaration after input verification so SCCP's conservative
/// unresolved-call path can be exercised without accepting malformed test IR.
class EraseMarkedSimulationFunctionTestPass
    : public PassWrapper<EraseMarkedSimulationFunctionTestPass,
                         OperationPass<obelisk::sim::SimDesignOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      EraseMarkedSimulationFunctionTestPass)

  StringRef getArgument() const final {
    return "test-obelisk-erase-marked-sim-function";
  }
  StringRef getDescription() const final {
    return "erase marked simulation functions for unresolved-call tests";
  }

  void runOnOperation() final {
    for (obelisk::sim::SimFuncOp function : llvm::make_early_inc_range(
             getOperation().getBody().getOps<obelisk::sim::SimFuncOp>()))
      if (function->hasAttr("test.erase_before_sccp"))
        function.erase();
  }
};

} // namespace

namespace obelisk {

void registerStateDomainTestPasses() {
  PassRegistration<StateDomainTestPass>();
  PassRegistration<EraseMarkedSimulationFunctionTestPass>();
}

} // namespace obelisk
