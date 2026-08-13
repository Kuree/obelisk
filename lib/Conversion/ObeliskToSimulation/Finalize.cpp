//===- Finalize.cpp - Verify the executable simulation boundary ---------===//

#include "Detail.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassOptions.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/Passes.h"

#include "llvm/ADT/StringSet.h"

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
  Option<unsigned> optLevel{*this, "opt-level",
                            llvm::cl::desc("optimization level from 0 to 3"),
                            llvm::cl::init(3)};
  Option<std::string> staticSpecialization{
      *this, "static-specialization",
      llvm::cl::desc("static state/NBA specialization: auto, off, or on"),
      llvm::cl::init("auto")};
};

/// The executable boundary is defined by the operations that may remain, not
/// merely by their dialect. In particular, BuiltinDialect also owns temporary
/// conversion operations that are not executable simulation IR.
static bool isExecutableOperation(Operation *op, ModuleOp root) {
  return op == root.getOperation() ||
         isa_and_nonnull<sim::ObeliskSimulationDialect, arith::ArithDialect,
                         cf::ControlFlowDialect, math::MathDialect>(
             op->getDialect());
}

static bool isExecutableType(Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.isSignless();
  if (isa<FloatType>(type))
    return true;
  return isa<FunctionType>(type) || isa<runtime::StatusType>(type) ||
         isa<sim::ContextType, sim::BytesType, sim::LogicType, sim::TimeType,
             sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
             sim::ProcessType, sim::ClassHandleType, sim::CovergroupHandleType,
             sim::VirtualInterfaceType, sim::ChandleType, sim::StringType,
             sim::DynamicArrayType, sim::QueueType, sim::MailboxType,
             sim::SemaphoreType, sim::AssocArrayType, sim::ReferencePathType,
             sim::ManagedRefType, sim::ArgumentRefType, sim::ControlType,
             sim::ObserverType>(type) ||
         sim::isAggregateType(type);
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

  llvm::StringSet<> executableSymbols;
  module.walk([&](Operation *op) {
    if (!isa<sim::SimCovergroupDeclOp, sim::SimClassDeclOp,
             sim::SimClassFieldDeclOp, sim::SimClassMethodDeclOp,
             sim::SimFuncOp>(op))
      return;
    if (auto name =
            op->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName()))
      executableSymbols.insert(name.getValue());
  });

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
            isa<sim::SimCallOp, sim::SimTaskCallOp, sim::SimSpawnOp>(op) &&
            named.getName() == sim::SimCallOp::getCalleeAttrName(op->getName());
        bool observerTarget =
            isa<sim::SimObserverBindOp>(op) &&
            named.getName() ==
                sim::SimObserverBindOp::getEvaluatorAttrName(op->getName());
        bool graphReference =
            isa<sim::SimDesignOp>(op) &&
            named.getName() ==
                sim::SimDesignOp::getComputeGraphAttrName(op->getName());
        bool executableReference =
            reference.getNestedReferences().empty() &&
            executableSymbols.contains(reference.getRootReference());
        bool allowed = callTarget || observerTarget || graphReference ||
                       executableReference;
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
    function->removeAttr(delayQuantumAttrName);
    function->removeAttr(sim::metadata::lowered);
    function->removeAttr("obelisk_sim.this_argument");
    function->removeAttr("obelisk_sim.constructor");
  });

  if (invalid)
    signalPassFailure();
}

} // namespace

void buildObeliskToSimulationPipeline(OpPassManager &manager, uint32_t workers,
                                      StringRef vpiMode, uint32_t optLevel,
                                      StringRef staticSpecialization) {
  manager.addPass(createObeliskSimPreparePass());
  OpPassManager &designManager = manager.nest<sim::SimDesignOp>();
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createObeliskSimLowerUnitPass());
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
    functionManager.addPass(createSROA());
    functionManager.addPass(createMem2Reg());
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
  }
  designManager.addPass(createObeliskSimMaterializeClockedSamplesPass());
  if (optLevel > 0)
    designManager.addPass(createObeliskSimDevirtualizeClassCallsPass());
  // Graph metadata does not participate in symbol liveness. Complete all
  // symbol pruning before graph construction so its nested references can
  // never become stale.
  designManager.addPass(createSymbolDCEPass());
  ObeliskSimSCCPPassOptions firstSCCPOptions;
  firstSCCPOptions.vpi = vpiMode.str();
  designManager.addPass(
      createObeliskSimSCCPPass(std::move(firstSCCPOptions)));
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
  }

  ObeliskSimInlinePassOptions inlineOptions;
  inlineOptions.optLevel = optLevel;
  designManager.addPass(createObeliskSimInlinePass(std::move(inlineOptions)));
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createSROA());
    functionManager.addPass(createMem2Reg());
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
  }
  if (optLevel > 0)
    designManager.addPass(createObeliskSimDevirtualizeClassCallsPass());
  ObeliskSimSCCPPassOptions secondSCCPOptions;
  secondSCCPOptions.vpi = vpiMode.str();
  designManager.addPass(
      createObeliskSimSCCPPass(std::move(secondSCCPOptions)));
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
  }
  if (optLevel > 0) {
    designManager.addPass(createObeliskSimEliminateDeadBoundariesPass());
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
  }
  designManager.addPass(createSymbolDCEPass());

  // Make continuation-frame state explicit before freezing whole-program
  // summaries. Threading can rematerialize pointer-free constants in resume
  // blocks, which is an executable CFG change and therefore must precede the
  // persistent compute graph.
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createObeliskSimThreadSuspensionPass());
  }

  ObeliskSimBuildComputeGraphPassOptions graphOptions;
  graphOptions.workers = workers;
  graphOptions.vpi = vpiMode.str();
  designManager.addPass(
      createObeliskSimBuildComputeGraphPass(std::move(graphOptions)));
  designManager.addPass(createObeliskSimVerifyComputeGraphPass());
  if (optLevel > 0) {
    ObeliskSimFuseComputeFragmentsPassOptions bodyFusionOptions;
    bodyFusionOptions.bodyFusion = true;
    designManager.addPass(
        createObeliskSimFuseComputeFragmentsPass(std::move(bodyFusionOptions)));
    designManager.addPass(createObeliskSimMaterializeComputeFusionPass());
    // The first inliner runs before graph construction. Body materialization
    // then creates new hot callers and new specialization opportunities, so
    // run the same identity-preserving simulation inliner once more before
    // freezing the final graph. Non-private callable symbols remain present;
    // process, hierarchy, descriptor, and VPI identities are not code bodies.
    ObeliskSimInlinePassOptions fusedInlineOptions;
    fusedInlineOptions.optLevel = optLevel;
    if (optLevel >= 3) {
      fusedInlineOptions.tinyCost = 64;
      fusedInlineOptions.specializationCost = 192;
      fusedInlineOptions.callerGrowthPercent = 100;
      fusedInlineOptions.callerGrowthConstant = 256;
      fusedInlineOptions.designGrowthPercent = 10;
      fusedInlineOptions.designGrowthConstant = 1024;
      fusedInlineOptions.maxIterations = 2;
    }
    designManager.addPass(
        createObeliskSimInlinePass(std::move(fusedInlineOptions)));
    ObeliskSimBuildComputeGraphPassOptions fusedGraphOptions;
    fusedGraphOptions.workers = workers;
    fusedGraphOptions.vpi = vpiMode.str();
    designManager.addPass(
        createObeliskSimBuildComputeGraphPass(std::move(fusedGraphOptions)));
    designManager.addPass(createObeliskSimVerifyComputeGraphPass());
    designManager.addPass(createObeliskSimMaterializeGraphRegionsPass());
    designManager.addPass(createObeliskSimFuseComputeFragmentsPass());
  }
  bool specialize = staticSpecialization == "on" ||
                    (staticSpecialization == "auto" && optLevel >= 2);
  if (specialize) {
    designManager.addPass(createObeliskSimSpecializeStaticStateNBAPass());
    designManager.addPass(createObeliskSimPlanStaticSuperstepPass());
  }
  manager.addPass(createObeliskSimFinalizePass());
}

void buildObeliskToSimulationPipeline(OpPassManager &manager) {
  buildObeliskToSimulationPipeline(manager, 1, "off", 3, "auto");
}

void buildObeliskToSimulationPipeline(OpPassManager &manager, uint32_t workers,
                                      StringRef vpiMode) {
  buildObeliskToSimulationPipeline(manager, workers, vpiMode, 3, "auto");
}

void buildObeliskToSimulationPipeline(OpPassManager &manager, uint32_t workers,
                                      StringRef vpiMode, uint32_t optLevel) {
  buildObeliskToSimulationPipeline(manager, workers, vpiMode, optLevel, "auto");
}

void registerObeliskToSimulationPipeline() {
  PassPipelineRegistration<ObeliskToSimulationPipelineOptions>(
      "lower-obelisk-to-sim",
      "Lower elaborated obelisk.sv semantic IR to isolated obelisk_sim SSA",
      [](OpPassManager &manager,
         const ObeliskToSimulationPipelineOptions &options) {
        buildObeliskToSimulationPipeline(
            manager, options.workers.getValue(), options.vpi.getValue(),
            options.optLevel.getValue(),
            options.staticSpecialization.getValue());
      });
}

} // namespace obelisk
