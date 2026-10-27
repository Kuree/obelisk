//===- StateDomainTestPass.cpp - Print whole-value state facts -----------===//

#include "AnalysisTestPasses.h"
#include "talos/Dialect/Schedule/ScheduleEnums.h"

#include "talos/Analysis/KnownVersioningAnalysis.h"
#include "talos/Analysis/StateDomainAnalysis.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

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
    if (isa<talos::sim::LogicType>(type))
      return true;
    if (!talos::sim::isAggregateType(type))
      return false;
    for (unsigned index = 0;
         index < talos::sim::getAggregateNumElements(type); ++index)
      if (containsLogic(talos::sim::getAggregateElementType(type, index)))
        return true;
    return false;
  };
  if (containsLogic(value.getType()))
    return true;
  auto result = dyn_cast<OpResult>(value);
  return result && isa<talos::sim::SimLogicCompareOp>(result.getOwner());
}

class StateDomainTestPass
    : public PassWrapper<StateDomainTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(StateDomainTestPass)
  StateDomainTestPass() = default;
  StateDomainTestPass(const StateDomainTestPass &other) : PassWrapper(other) {}

  StringRef getArgument() const final {
    return "test-talos-sim-state-domain";
  }
  StringRef getDescription() const final {
    return "print read-only Talos simulation state-domain facts";
  }

  void runOnOperation() final {
    SmallVector<talos::sim::SimDesignOp> designs(
        getOperation().getOps<talos::sim::SimDesignOp>());
    llvm::sort(designs, [](auto lhs, auto rhs) {
      return lhs.getSymName() < rhs.getSymName();
    });
    for (talos::sim::SimDesignOp design : designs) {
      FailureOr<talos::StateDomainAnalysis> analysis = failure();
      if (shared) {
        auto proofs =
            talos::StateDomainAnalysis::computeForSpecialization(design);
        if (succeeded(proofs))
          analysis = std::move(proofs->first);
      } else {
        analysis = talos::StateDomainAnalysis::compute(design);
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
  static void printDesign(talos::sim::SimDesignOp design,
                          const talos::StateDomainAnalysis &analysis) {
    llvm::errs() << "state-domain @" << design.getSymName() << "\n";
    for (const talos::InductiveStateRoot &root :
         analysis.getInductiveRoots()) {
      StringRef resource = "unknown";
      if (root.resource == talos::schedule::ComputeResourceKind::Storage)
        resource = "storage";
      else if (root.resource == talos::schedule::ComputeResourceKind::Net)
        resource = "net";
      llvm::errs() << "root " << resource << " " << root.descriptor
                   << ": inductive-two-state\n";
    }
    SmallVector<talos::sim::SimFuncOp> functions(
        design.getBody().front().getOps<talos::sim::SimFuncOp>());
    llvm::sort(functions, [](auto lhs, auto rhs) {
      return lhs.getSymName() < rhs.getSymName();
    });
    for (talos::sim::SimFuncOp function : functions) {
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

  static void printFact(const talos::StateDomainAnalysis &analysis,
                        unsigned block, std::optional<unsigned> operation,
                        unsigned valueIndex, Value value) {
    talos::StateDomainFact fact = analysis.get(value);
    llvm::errs() << "  bb" << block;
    if (operation)
      llvm::errs() << ".op" << *operation << ".result";
    else
      llvm::errs() << ".arg";
    llvm::errs() << valueIndex << ": "
                 << talos::stringifyStateDomain(fact.domain) << " ("
                 << talos::stringifyStateDomainReason(fact.reason) << ")\n";
  }
};

/// Remove a marked declaration after input verification so SCCP's conservative
/// unresolved-call path can be exercised without accepting malformed test IR.
class EraseMarkedSimulationFunctionTestPass
    : public PassWrapper<EraseMarkedSimulationFunctionTestPass,
                         OperationPass<talos::sim::SimDesignOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      EraseMarkedSimulationFunctionTestPass)

  StringRef getArgument() const final {
    return "test-talos-erase-marked-sim-function";
  }
  StringRef getDescription() const final {
    return "erase marked simulation functions for unresolved-call tests";
  }

  void runOnOperation() final {
    for (talos::sim::SimFuncOp function : llvm::make_early_inc_range(
             getOperation().getBody().getOps<talos::sim::SimFuncOp>()))
      if (function->hasAttr("test.erase_before_sccp"))
        function.erase();
  }
};

/// Print the regions admitted for known-state versioning. Blocks are named by
/// their position in the function, operations by their name.
class KnownVersioningTestPass
    : public PassWrapper<KnownVersioningTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(KnownVersioningTestPass)
  StringRef getArgument() const final {
    return "test-talos-sim-known-versioning";
  }
  StringRef getDescription() const final {
    return "print regions admitted for known-state versioning";
  }

  void runOnOperation() final {
    getOperation().walk([](talos::sim::SimFuncOp function) {
      talos::analysis::KnownVersioningAnalysis analysis(function);
      if (analysis.getRegions().empty() && analysis.getRejected().empty())
        return;
      DenseMap<Block *, unsigned> blockIndex;
      for (auto [index, block] : llvm::enumerate(function.getBody()))
        blockIndex[&block] = index;
      auto kind = [](talos::analysis::KnownRegionKind value) {
        return value == talos::analysis::KnownRegionKind::Loop ? "loop"
                                                                 : "run";
      };
      auto name = [](Operation *op) -> std::string {
        return op ? op->getName().getStringRef().str() : "<none>";
      };
      llvm::errs() << "known-versioning @" << function.getSymName() << "\n";
      for (const auto &region : analysis.getRegions()) {
        Block *block = region.header ? region.header : region.begin->getBlock();
        llvm::errs() << "  admit " << kind(region.kind) << " bb"
                     << blockIndex.lookup(block);
        if (region.begin)
          llvm::errs() << " from " << name(region.begin) << " to "
                       << name(region.end);
        llvm::errs() << " operations=" << region.operations
                     << " benefit=" << region.benefit
                     << " check=" << region.checkCost
                     << " live-ins=" << region.liveIns.size()
                     << " checked=" << region.checkedReads.size()
                     << " owned=" << region.ownedReads.size()
                     << " ranged=" << region.rangeProved.size() << "\n";
      }
      for (const auto &region : analysis.getRejected())
        llvm::errs() << "  reject " << kind(region.kind) << " bb"
                     << blockIndex.lookup(region.block) << ": " << region.reason
                     << " (" << name(region.operation) << ")\n";
    });
    markAllAnalysesPreserved();
  }
};

} // namespace

namespace talos {

void registerStateDomainTestPasses() {
  PassRegistration<StateDomainTestPass>();
  PassRegistration<KnownVersioningTestPass>();
  PassRegistration<EraseMarkedSimulationFunctionTestPass>();
}

} // namespace talos
