//===- Finalize.cpp - Verify the executable simulation boundary ---------===//

#include "Detail.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassOptions.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/Passes.h"

#include <string>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMFINALIZEPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using namespace obelisk::simlowering;

struct ObeliskToSimulationPipelineOptions
    : PassPipelineOptions<ObeliskToSimulationPipelineOptions> {
  Option<unsigned> workers{*this, "workers",
                           llvm::cl::desc("number of generated worker lanes"),
                           llvm::cl::init(1)};
  Option<std::string> vpi{
      *this, "vpi", llvm::cl::desc("VPI visibility mode: off, read, or full"),
      llvm::cl::init("off")};
};

/// The executable boundary is defined by the operations that may remain, not
/// merely by their dialect. In particular, BuiltinDialect also owns temporary
/// conversion operations that are not executable simulation IR.
static bool isExecutableOperation(Operation *op, ModuleOp root) {
  return op == root.getOperation() ||
         isa_and_nonnull<sim::ObeliskSimulationDialect, arith::ArithDialect,
                         cf::ControlFlowDialect>(op->getDialect());
}

static bool isExecutableType(Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.isSignless();
  return isa<FunctionType>(type) ||
         isa<sim::ContextType, sim::LogicType, sim::TimeType, sim::RefType,
             sim::NetType, sim::DriverType, sim::EventType, sim::ProcessType>(
             type);
}

static bool containsForbiddenType(Type type) {
  bool forbidden = false;
  type.walk([&](Type nested) { forbidden |= !isExecutableType(nested); });
  return forbidden;
}

class ObeliskSimFinalizePass
    : public impl::ObeliskSimFinalizePassBase<ObeliskSimFinalizePass> {
public:
  void runOnOperation() override;
};

void ObeliskSimFinalizePass::runOnOperation() {
  ModuleOp module = getOperation();
  bool invalid = false;

  // This is deliberately a module pass: operation passes must never mutate
  // ancestors or siblings, and design passes may execute concurrently.
  SmallVector<Operation *> obsoleteSemanticRoots;
  module.walk<WalkOrder::PreOrder>([&](Operation *op) {
    if (isa<sim::SimDesignOp>(op))
      return WalkResult::skip();
    if (!isSemanticOp(op))
      return WalkResult::advance();
    obsoleteSemanticRoots.push_back(op);
    return WalkResult::skip();
  });
  for (Operation *op : obsoleteSemanticRoots)
    op->erase();

  module.walk([&](Operation *op) {
    if (!isExecutableOperation(op, module)) {
      op->emitError() << "operation from dialect '"
                      << op->getName().getDialectNamespace()
                      << "' survived obelisk_sim finalization";
      invalid = true;
    }
    for (Type type : op->getOperandTypes())
      if (containsForbiddenType(type)) {
        op->emitError() << "operand contains a forbidden semantic type "
                        << type;
        invalid = true;
      }
    for (Type type : op->getResultTypes())
      if (containsForbiddenType(type)) {
        op->emitError() << "result contains a forbidden semantic type " << type;
        invalid = true;
      }
    for (Region &region : op->getRegions())
      for (Block &block : region)
        for (BlockArgument argument : block.getArguments())
          if (containsForbiddenType(argument.getType())) {
            op->emitError()
                << "region block argument contains a forbidden semantic type "
                << argument.getType();
            invalid = true;
          }
    for (NamedAttribute named : op->getAttrs()) {
      named.getValue().walk([&](Type type) {
        if (containsForbiddenType(type)) {
          op->emitError() << "attribute contains a forbidden semantic type "
                          << type;
          invalid = true;
        }
      });
      named.getValue().walk([&](SymbolRefAttr reference) {
        bool callTarget =
            isa<sim::SimCallOp, sim::SimSpawnOp>(op) &&
            named.getName() == sim::SimCallOp::getCalleeAttrName(op->getName());
        bool graphReference =
            isa<sim::SimDesignOp>(op) &&
            named.getName() ==
                sim::SimDesignOp::getComputeGraphAttrName(op->getName());
        bool allowed = callTarget || graphReference;
        if (!allowed) {
          op->emitError() << "disallowed symbol reference " << reference;
          invalid = true;
        }
      });
    }
    if (isa<sim::ObeliskSimulationDialect>(op->getDialect()) &&
        !isa<MemoryEffectOpInterface>(op) &&
        !op->hasTrait<OpTrait::HasRecursiveMemoryEffects>()) {
      op->emitError("core operation has no precise memory-effect interface");
      invalid = true;
    }
  });

  module.walk([&](sim::SimFuncOp function) {
    function->removeAttr(bindingsAttrName);
    function->removeAttr(delayScaleAttrName);
  });

  if (invalid)
    signalPassFailure();
}

} // namespace

void buildObeliskToSimulationPipeline(OpPassManager &manager, uint32_t workers,
                                      StringRef vpiMode) {
  manager.addPass(createObeliskSimPreparePass());
  OpPassManager &designManager = manager.nest<sim::SimDesignOp>();
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createObeliskSimLowerUnitPass());
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
    functionManager.addPass(createMem2Reg());
  }
  // Graph metadata does not participate in symbol liveness. Complete all
  // symbol pruning before graph construction so its nested references can
  // never become stale.
  designManager.addPass(createSymbolDCEPass());
  designManager.addPass(createObeliskSimSCCPPass());
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
  }
  designManager.addPass(createSymbolDCEPass());

  // Whole-program summaries and static fragment extraction deliberately run
  // before continuation-frame state is made explicit. This lets promotion and
  // canonicalization erase unnecessary process state first.
  ObeliskSimBuildComputeGraphPassOptions graphOptions;
  graphOptions.workers = workers;
  graphOptions.vpi = vpiMode.str();
  designManager.addPass(
      createObeliskSimBuildComputeGraphPass(std::move(graphOptions)));
  designManager.addPass(createObeliskSimVerifyComputeGraphPass());
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createObeliskSimThreadSuspensionPass());
  }
  designManager.addPass(createObeliskSimVerifyComputeGraphPass());
  manager.addPass(createObeliskSimFinalizePass());
}

void buildObeliskToSimulationPipeline(OpPassManager &manager) {
  buildObeliskToSimulationPipeline(manager, 1, "off");
}

void registerObeliskToSimulationPipeline() {
  PassPipelineRegistration<ObeliskToSimulationPipelineOptions>(
      "lower-obelisk-to-sim",
      "Lower elaborated obelisk.sv semantic IR to isolated obelisk_sim SSA",
      [](OpPassManager &manager,
         const ObeliskToSimulationPipelineOptions &options) {
        buildObeliskToSimulationPipeline(manager, options.workers.getValue(),
                                         options.vpi.getValue());
      });
}

} // namespace obelisk
