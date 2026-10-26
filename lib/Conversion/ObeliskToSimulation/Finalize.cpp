//===- Finalize.cpp - Verify the executable simulation boundary ---------===//

#include "Detail.h"
#include "obelisk/Conversion/SimulationToSchedule.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationResources.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/Threading.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassOptions.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/Passes.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringSet.h"

#include <memory>
#include <set>
#include <string>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMFINALIZEPASS
#define GEN_PASS_DEF_OBELISKSIMINSTRUMENTVPIFRAMESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using namespace obelisk::simlowering;

// Capture the actual typed dispatch contract before executable devirtualization
// or inlining replaces executable call operations. Source calls keep symbolic
// class/method identity; the encoder derives concrete targets independently.
struct MaterializeVPIClassDispatchPass
    : PassWrapper<MaterializeVPIClassDispatchPass,
                  OperationPass<sim::SimDesignOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(MaterializeVPIClassDispatchPass)

  void runOnOperation() override {
    auto design = getOperation();
    llvm::DenseMap<Attribute, sim::SimVPIClassCallDispatchOp> inventory;
    for (auto declaration :
         design.getBody().front().getOps<sim::SimVPIClassCallDispatchOp>())
      if (!inventory.try_emplace(declaration.getCallAttr(), declaration)
               .second) {
        declaration.emitOpError("duplicates a class call dispatch contract");
        return signalPassFailure();
      }
    OpBuilder builder(design.getContext());
    builder.setInsertionPointToEnd(&design.getBody().front());
    bool invalid = false;
    SmallVector<Operation *> calls;
    design.walk([&](Operation *operation) {
      if (isa<sim::SimClassVirtualCallOp, sim::SimClassVirtualTaskCallOp>(
              operation) &&
          sim::has<sim::metadata::VpiCallSite>(operation))
        calls.push_back(operation);
    });
    for (Operation *operation : calls) {
      auto source = sim::get<sim::metadata::VpiCallSite>(operation);
      Value receiver;
      FlatSymbolRefAttr method;
      if (auto call = dyn_cast<sim::SimClassVirtualCallOp>(operation)) {
        receiver = call.getReceiver();
        method = call.getMethodAttr();
      } else if (auto call =
                     dyn_cast<sim::SimClassVirtualTaskCallOp>(operation)) {
        receiver = call.getReceiver();
        method = call.getMethodAttr();
      } else {
        continue;
      }
      Type type = receiver.getType();
      if (auto found = inventory.find(source); found != inventory.end()) {
        if (found->second.getReceiverType() != type ||
            found->second.getMethodAttr() != method) {
          operation->emitOpError(
              "cloned source call has conflicting class dispatch contracts");
          invalid = true;
        }
        continue;
      }
      auto declaration = sim::SimVPIClassCallDispatchOp::create(
          builder, operation->getLoc(), source, TypeAttr::get(type), method);
      inventory.try_emplace(source, declaration);
    }
    if (invalid)
      signalPassFailure();
  }
};

// Keep reflected zero-time activations explicit when a callee disables an
// older named-block activation. The final result is compiler control state,
// not an SV value: zero denotes normal return and a positive ID denotes the
// exact dynamic control activation already unwound by the runtime.
static LogicalResult propagateVPIControlEscapes(sim::SimDesignOp design) {
  using Targets = std::set<uint64_t>;
  llvm::StringMap<sim::SimFuncOp> functions;
  llvm::StringMap<sim::SimClassMethodDeclOp> methods;
  DenseMap<Operation *, Targets> targets;
  DenseMap<uint64_t, SmallVector<sim::SimClassMethodDeclOp>> families;
  SmallVector<Operation *> calls;
  for (auto function : design.getOps<sim::SimFuncOp>()) {
    functions[function.getSymName()] = function;
    function.walk([&](sim::SimControlNonlocalExitOp exit) {
      if (function.getEntryKind() == sim::EntryKind::Function)
        targets[function].insert(exit.getTargetId());
    });
  }
  for (auto method : design.getOps<sim::SimClassMethodDeclOp>()) {
    methods[method.getSymName()] = method;
    if (!method.getIsTask() && method.getSignatureId())
      families[*method.getSignatureId()].push_back(method);
  }
  design.walk([&](Operation *op) {
    if (isa<sim::SimCallOp, sim::SimClassDirectCallOp,
            sim::SimClassVirtualCallOp>(op))
      calls.push_back(op);
  });
  auto merge = [](Targets &destination, const Targets &source) {
    size_t before = destination.size();
    destination.insert(source.begin(), source.end());
    return before != destination.size();
  };
  auto familyTargets = [&](uint64_t signature) {
    Targets result;
    for (auto method : families[signature])
      if (auto implementation = method.getImplementation())
        if (auto function = functions.lookup(*implementation))
          merge(result, targets[function]);
    return result;
  };
  auto callTargets = [&](Operation *call) {
    if (auto virtualCall = dyn_cast<sim::SimClassVirtualCallOp>(call))
      return familyTargets(virtualCall.getSignatureId());
    auto callee = call->getAttrOfType<FlatSymbolRefAttr>("callee");
    auto function =
        callee ? functions.lookup(callee.getValue()) : sim::SimFuncOp{};
    return function ? targets[function] : Targets{};
  };
  bool changed;
  do {
    changed = false;
    // Every implementation of one virtual ABI family must agree, including
    // overrides that never themselves take an exceptional return.
    for (auto &family : families) {
      Targets all = familyTargets(family.first);
      for (auto method : family.second)
        if (auto implementation = method.getImplementation())
          if (auto function = functions.lookup(*implementation))
            changed |= merge(targets[function], all);
    }
    for (Operation *call : calls) {
      auto owner = call->getParentOfType<sim::SimFuncOp>();
      if (owner && owner.getEntryKind() == sim::EntryKind::Function)
        changed |= merge(targets[owner], callTargets(call));
    }
  } while (changed);

  struct Boundary {
    Value activation;
    Block *body = nullptr;
    Block *exit = nullptr;
    SmallVector<Value> operands;
  };
  struct CallEscape {
    Operation *call;
    Targets targets;
    DenseMap<uint64_t, Boundary> boundaries;
  };
  SmallVector<CallEscape> affectedCalls;
  DominanceInfo dominance(design);
  for (Operation *call : calls) {
    Targets exits = callTargets(call);
    if (exits.empty())
      continue;
    CallEscape entry{call, std::move(exits), DenseMap<uint64_t, Boundary>()};
    auto owner = call->getParentOfType<sim::SimFuncOp>();
    owner.walk([&](Operation *operation) {
      Boundary boundary;
      if (auto resumable = dyn_cast<sim::SimControlBoundaryOp>(operation)) {
        boundary = {resumable.getActivation(), resumable.getBody(),
                    resumable.getResume(),
                    SmallVector<Value>(resumable.getResumeOperands())};
      } else if (auto local =
                     dyn_cast<sim::SimControlLocalBoundaryOp>(operation)) {
        boundary = {
            local.getActivation(), local.getBody(), local.getExit(), {}};
      } else {
        return;
      }
      auto enter = boundary.activation.getDefiningOp<sim::SimControlEnterOp>();
      if (enter && entry.targets.count(enter.getTargetId()) &&
          dominance.dominates(boundary.body, call->getBlock()))
        entry.boundaries.try_emplace(enter.getTargetId(), std::move(boundary));
    });
    affectedCalls.push_back(std::move(entry));
  }
  Builder types(design.getContext());
  Type statusType = types.getI64Type();
  auto extended = [&](FunctionType original) {
    SmallVector<Type> results(original.getResults());
    results.push_back(statusType);
    return FunctionType::get(design.getContext(), original.getInputs(),
                             results);
  };
  for (auto &entry : functions) {
    auto function = entry.getValue();
    if (targets[function].empty())
      continue;
    if (function.isExternal())
      return function.emitOpError(
          "external ABI cannot propagate a source control escape");
    function.setFunctionType(extended(function.getFunctionType()));
    function.walk([&](sim::SimReturnOp result) {
      Value selected;
      SmallVector<sim::SimControlNonlocalExitOp> markers;
      for (auto marker :
           result->getBlock()->getOps<sim::SimControlNonlocalExitOp>()) {
        selected = marker.getSelected();
        markers.push_back(marker);
      }
      OpBuilder builder(result);
      Value status =
          selected
              ? selected
              : arith::ConstantIntOp::create(builder, result.getLoc(), 0, 64)
                    .getResult();
      result->insertOperands(result.getNumOperands(), ValueRange{status});
      for (auto marker : markers)
        marker.erase();
    });
  }
  for (auto &entry : methods) {
    auto method = entry.getValue();
    bool affected = method.getSignatureId() &&
                    !familyTargets(*method.getSignatureId()).empty();
    if (auto implementation = method.getImplementation())
      if (auto function = functions.lookup(*implementation))
        affected |= !targets[function].empty();
    if (affected && !method.getIsTask())
      method.setFunctionTypeAttr(TypeAttr::get(
          extended(cast<FunctionType>(method.getFunctionType()))));
  }
  for (CallEscape &entry : affectedCalls) {
    Operation *old = entry.call;
    Location loc = old->getLoc();
    auto owner = old->getParentOfType<sim::SimFuncOp>();
    SmallVector<Value> observations;
    ArrayAttr endingTargets;
    if (auto marker = dyn_cast_or_null<sim::SimVPICallObservationOp>(
            old->getNextNode())) {
      llvm::append_range(observations, marker.getObserved());
      endingTargets = marker.getEndingTargets();
      marker.erase();
    }
    OpBuilder builder(old);
    OperationState state(loc, old->getName());
    state.addOperands(old->getOperands());
    state.addAttributes(old->getAttrs());
    state.addTypes(old->getResultTypes());
    state.addTypes(statusType);
    Operation *call = builder.create(state);
    for (auto [oldValue, newValue] :
         llvm::zip(old->getResults(), call->getResults()))
      oldValue.replaceAllUsesWith(newValue);
    old->erase();
    Value status = call->getResults().back();
    Block *head = call->getBlock();
    Block *normal = head->splitBlock(std::next(call->getIterator()));
    auto *abnormal = new Block;
    owner.getBody().getBlocks().insert(normal->getIterator(), abnormal);
    builder.setInsertionPointToEnd(head);
    Value zero = arith::ConstantIntOp::create(builder, loc, 0, 64);
    Value success = arith::CmpIOp::create(
        builder, loc, arith::CmpIPredicate::eq, status, zero);
    cf::CondBranchOp::create(builder, loc, success, normal, abnormal);
    builder.setInsertionPointToStart(abnormal);
    for (uint64_t target : entry.targets) {
      auto boundary = entry.boundaries.lookup(target);
      if (!boundary.activation)
        continue;
      auto *next = new Block;
      owner.getBody().getBlocks().insert(normal->getIterator(), next);
      Value matches = sim::SimControlMatchOp::create(builder, loc, status,
                                                     boundary.activation);
      auto *exit = new Block;
      owner.getBody().getBlocks().insert(normal->getIterator(), exit);
      cf::CondBranchOp::create(builder, loc, matches, exit, next);
      builder.setInsertionPointToStart(exit);
      SmallVector<Value> ended;
      for (auto [index, reference] : llvm::enumerate(observations))
        if (llvm::is_contained(
                cast<DenseI64ArrayAttr>(endingTargets[index]).asArrayRef(),
                static_cast<int64_t>(target)))
          ended.push_back(reference);
      if (!ended.empty())
        sim::SimVPIFrameObservationEndOp::create(builder, loc, ended);
      cf::BranchOp::create(builder, loc, boundary.exit, boundary.operands);
      builder.setInsertionPointToStart(next);
    }
    if (!observations.empty())
      sim::SimVPIFrameObservationEndOp::create(builder, loc, observations);
    SmallVector<Value> results;
    auto resultTypes = owner.getFunctionType().getResults();
    bool propagates = owner.getEntryKind() == sim::EntryKind::Function;
    for (Type type : propagates ? resultTypes.drop_back() : resultTypes) {
      Value value = createDefaultValue(builder, loc, type);
      if (!value)
        return owner.emitOpError("cannot form an exceptional callable return");
      results.push_back(value);
    }
    if (propagates)
      results.push_back(status);
    sim::SimReturnOp::create(builder, loc, results);
  }
  SmallVector<sim::SimControlLocalBoundaryOp> localBoundaries;
  design.walk([&](sim::SimControlLocalBoundaryOp boundary) {
    localBoundaries.push_back(boundary);
  });
  for (auto boundary : localBoundaries) {
    OpBuilder builder(boundary);
    cf::BranchOp::create(builder, boundary.getLoc(), boundary.getBody());
    boundary.erase();
  }
  SmallVector<sim::SimVPICallObservationOp> observations;
  design.walk(
      [&](sim::SimVPICallObservationOp op) { observations.push_back(op); });
  for (auto op : observations)
    op.erase();
  return success();
}

// Source calls are frozen before per-unit lowering, while continuation edges
// become concrete only afterwards. Attach the protocol here so every selected
// virtual target has its own exact ticket and post-return continuation.
class ObeliskSimInstrumentVPIFramesPass
    : public impl::ObeliskSimInstrumentVPIFramesPassBase<
          ObeliskSimInstrumentVPIFramesPass> {
public:
  void runOnOperation() override {
    auto design = getOperation();
    if (failed(propagateVPIControlEscapes(design)))
      return signalPassFailure();
    SymbolTable symbols(design);
    llvm::DenseSet<uint64_t> initializerUnits;
    for (auto binding : design.getOps<sim::SimVPIInitializerBindingOp>())
      initializerUnits.insert(binding.getCodeUnitId());
    SmallVector<Operation *> calls;
    design.walk([&](Operation *operation) {
      if (!isa<sim::SimCallOp, sim::SimTaskCallOp, sim::SimClassVirtualCallOp,
               sim::SimClassVirtualTaskCallOp, sim::SimDPICallOp>(operation))
        return;
      auto function = operation->getParentOfType<sim::SimFuncOp>();
      auto codeUnitID =
          function ? function->getAttrOfType<IntegerAttr>("code_unit_id")
                   : IntegerAttr{};
      if (function &&
          (sim::has<sim::metadata::VpiFrameInstrumentation>(function) ||
           (codeUnitID && initializerUnits.contains(codeUnitID.getInt()))) &&
          sim::has<sim::metadata::VpiCallSite>(operation))
        calls.push_back(operation);
    });
    for (Operation *operation : calls) {
      auto function = operation->getParentOfType<sim::SimFuncOp>();
      auto site = sim::get<sim::metadata::VpiCallSite>(operation);
      auto declaration = symbols.lookup<sim::SimVPICallDeclOp>(site.getValue());
      if (!declaration || function.getNumArguments() == 0 ||
          !isa<sim::ContextType>(function.getArgument(0).getType())) {
        operation->emitOpError("frame instrumentation requires an exact source "
                               "call and runtime context");
        return signalPassFailure();
      }
      // This pass is only run once after source lowering. Authored primitive
      // IR is not selected without the explicit source instrumentation marker.
      OpBuilder builder(operation);
      Value context = function.getArgument(0);
      Value ticket = sim::SimVPICallPrepareOp::create(
          builder, operation->getLoc(), context, site);
      if (auto foreign = dyn_cast<sim::SimDPICallOp>(operation)) {
        auto callee = declaration.getCalleeCodeUnitId();
        if (!callee) {
          operation->emitOpError(
              "direct DPI import has no exact callable binding");
          return signalPassFailure();
        }
        Value activation = sim::SimVPIFrameEnterForeignOp::create(
            builder, operation->getLoc(), context, site,
            builder.getI64IntegerAttr(*callee));
        builder.setInsertionPointAfter(operation);
        sim::SimVPIFrameLeaveOp::create(builder, operation->getLoc(), context,
                                        activation, ValueRange{});
        sim::SimVPICallFinishOp::create(builder, operation->getLoc(), context,
                                        ticket);
        continue;
      }
      if (isa<sim::SimTaskCallOp, sim::SimClassVirtualTaskCallOp>(operation)) {
        Block *continuation = operation->getSuccessor(0);
        auto *resume = new Block;
        for (auto argument : continuation->getArguments())
          resume->addArgument(argument.getType(), argument.getLoc());
        function.getBody().getBlocks().insert(continuation->getIterator(),
                                              resume);
        operation->setSuccessor(resume, 0);
        builder.setInsertionPointToStart(resume);
        sim::SimVPICallFinishOp::create(builder, operation->getLoc(), context,
                                        ticket);
        cf::BranchOp::create(builder, operation->getLoc(), continuation,
                             resume->getArguments());
      } else {
        builder.setInsertionPointAfter(operation);
        sim::SimVPICallFinishOp::create(builder, operation->getLoc(), context,
                                        ticket);
      }
    }
    // A generated net resolver helper has no source activation of its own.
    // Its exact resolver call can still prepare a Net-origin activation when
    // the selected source function has an instrumented callee prologue.
    SmallVector<sim::SimCallOp> resolutionCalls;
    design.walk([&](sim::SimCallOp call) {
      if (sim::has<sim::metadata::VpiResolutionNet>(call))
        resolutionCalls.push_back(call);
    });
    for (auto call : resolutionCalls) {
      auto callee = symbols.lookup<sim::SimFuncOp>(call.getCallee());
      if (!callee || !sim::has<sim::metadata::VpiFrameInstrumentation>(callee))
        continue;
      auto owner = call->getParentOfType<sim::SimFuncOp>();
      if (!owner || owner.getNumArguments() == 0 ||
          !isa<sim::ContextType>(owner.getArgument(0).getType()) ||
          sim::has<sim::metadata::VpiCallSite>(call)) {
        call.emitOpError("resolution frame instrumentation requires a "
                         "generated exact call and runtime context");
        return signalPassFailure();
      }
      OpBuilder builder(call);
      Value context = owner.getArgument(0);
      Value ticket = sim::SimVPIResolutionPrepareOp::create(
          builder, call.getLoc(), context,
          sim::get<sim::metadata::VpiResolutionNet>(call));
      builder.setInsertionPointAfter(call);
      sim::SimVPICallFinishOp::create(builder, call.getLoc(), context, ticket);
      sim::remove<sim::metadata::VpiResolutionNet>(call);
    }
  }
};

// Initialization bindings have no observable effect in a closed design that
// neither uses its random streams nor observes logical process identity.
// Inspect lowered effects after pruning: direct calls/spawns are represented
// by the complete sibling function inventory, while foreign calls and external
// function declarations keep the bindings conservatively.
struct PruneUnusedInitializationBindingsPass
    : PassWrapper<PruneUnusedInitializationBindingsPass,
                  OperationPass<sim::SimDesignOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      PruneUnusedInitializationBindingsPass)

  void runOnOperation() override {
    bool observable = false;
    SmallVector<sim::SimRandomInitializationEnterOp> entries;
    SmallVector<sim::SimRandomInitializationLeaveOp> exits;
    getOperation().walk([&](Operation *operation) {
      if (auto enter =
              dyn_cast<sim::SimRandomInitializationEnterOp>(operation)) {
        entries.push_back(enter);
        return;
      }
      if (auto leave =
              dyn_cast<sim::SimRandomInitializationLeaveOp>(operation)) {
        exits.push_back(leave);
        return;
      }
      if (isa<sim::SimCallOp, sim::SimTaskCallOp, sim::SimSpawnOp,
              sim::SimLegacyRandomOp>(operation))
        return;
      if (auto function = dyn_cast<sim::SimFuncOp>(operation))
        observable |= function.isExternal();
      observable |=
          isa<sim::SimDPICallOp, sim::SimProcessCurrentOp,
              sim::SimProcessRandomStateOp, sim::SimProcessSetRandomStateOp,
              sim::SimProcessControlOp>(operation);
      if (auto interface = dyn_cast<MemoryEffectOpInterface>(operation)) {
        SmallVector<MemoryEffects::EffectInstance> effects;
        interface.getEffects(effects);
        for (const auto &effect : effects)
          observable |= effect.getResource() == sim::RNGResource::get();
      }
    });
    if (observable)
      return;
    for (auto leave : exits)
      leave.erase();
    for (auto enter : entries)
      enter.erase();
  }
};

struct ObeliskToSimulationPipelineOptions
    : PassPipelineOptions<ObeliskToSimulationPipelineOptions> {
  Option<unsigned> workers{*this, "workers",
                           llvm::cl::desc("number of generated worker lanes"),
                           llvm::cl::init(1)};
  Option<std::string> vpi{
      *this, "vpi", llvm::cl::desc("VPI visibility mode: off, read, or full"),
      llvm::cl::init("off")};
  Option<bool> instrumentVPIFrames{
      *this, "instrument-vpi-frames",
      llvm::cl::desc(
          "enable source frame probes while bridge coverage is developed"),
      llvm::cl::init(false)};
  Option<unsigned> optLevel{*this, "opt-level",
                            llvm::cl::desc("optimization level from 0 to 3"),
                            llvm::cl::init(3)};
  Option<bool> earlySymbolDCE{
      *this, "early-symbol-dce",
      llvm::cl::desc("prune unreachable prepared symbols before unit lowering"),
      llvm::cl::init(true)};
  Option<std::string> staticSpecialization{
      *this, "static-specialization",
      llvm::cl::desc("static state/NBA specialization: auto, off, or on"),
      llvm::cl::init("auto")};
  Option<std::string> clockedKernels{
      *this, "clocked-kernels",
      llvm::cl::desc("clocked kernels: off, immediate, or deferred"),
      llvm::cl::init("deferred")};
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
             sim::BoxType, sim::SemaphoreType, sim::AssocArrayType,
             sim::ReferencePathType, sim::ManagedRefType, sim::ArgumentRefType,
             sim::ControlType, sim::ObserverType, sim::ManagedWatchType>(
             type) ||
         sim::isAggregateType(type);
}

static bool containsForbiddenType(Type type) {
  bool forbidden = false;
  type.walk([&](Type nested) { forbidden |= !isExecutableType(nested); });
  return forbidden;
}

// Definition member declarations by the symbol table that holds them. A flat
// reference names one of them exactly when lookupNearestSymbolFrom would find
// it, without scanning the table once per reference.
using DefinitionMemberTables = DenseMap<Operation *, llvm::StringSet<>>;

// Attribute and type storage is immutable. Verify each distinct value once
// per subtree, retaining the original per-operation diagnostics for failures.
// Function workers share only the frozen inventories of symbols.
static LogicalResult
verifyExecutableSubtree(Operation *root, ModuleOp module,
                        const llvm::StringSet<> &executableSymbols,
                        const DefinitionMemberTables &definitionMembers,
                        bool skipFunctions) {
  auto isDefinitionMember = [&](Operation *from, SymbolRefAttr reference) {
    if (!reference.getNestedReferences().empty())
      return isa_and_nonnull<sim::SimVPIDefinitionMemberDeclOp>(
          SymbolTable::lookupNearestSymbolFrom(from, reference));
    auto found =
        definitionMembers.find(SymbolTable::getNearestSymbolTable(from));
    return found != definitionMembers.end() &&
           found->second.contains(reference.getRootReference().getValue());
  };
  bool invalid = false;
  DenseMap<Type, bool> types;
  auto forbiddenType = [&](Type type) {
    auto [entry, inserted] = types.try_emplace(type, false);
    if (inserted)
      entry->second = containsForbiddenType(type);
    return entry->second;
  };
  struct AttributeInventory {
    SmallVector<Type, 1> forbiddenTypes;
    SmallVector<SymbolRefAttr, 1> references;
  };
  DenseMap<Attribute, AttributeInventory> attributes;
  root->walk<WalkOrder::PreOrder>([&](Operation *op) {
    if (skipFunctions && isa<sim::SimFuncOp>(op))
      return WalkResult::skip();
    if (isa<sim::SimCovergroupClockingSpawnOp,
            sim::SimCovergroupBlockEventPlanOp>(op)) {
      op->emitError("deferred covergroup sampler survived its design-level "
                    "materialization pass");
      invalid = true;
    }
    if (!isExecutableOperation(op, module)) {
      op->emitError() << "operation from dialect '"
                      << op->getName().getDialectNamespace()
                      << "' survived simulation finalization";
      invalid = true;
    }
    for (Type type : op->getOperandTypes())
      if (forbiddenType(type)) {
        op->emitError() << "operand contains a forbidden semantic type "
                        << type;
        invalid = true;
      }
    for (Type type : op->getResultTypes())
      if (forbiddenType(type)) {
        op->emitError() << "result contains a forbidden semantic type " << type;
        invalid = true;
      }
    for (Region &region : op->getRegions())
      for (Block &block : region)
        for (BlockArgument argument : block.getArguments())
          if (forbiddenType(argument.getType())) {
            op->emitError()
                << "region block argument contains a forbidden semantic type "
                << argument.getType();
            invalid = true;
          }
    for (NamedAttribute named : op->getAttrs()) {
      auto [entry, inserted] = attributes.try_emplace(named.getValue());
      if (inserted) {
        named.getValue().walk([&](Type type) {
          if (forbiddenType(type))
            entry->second.forbiddenTypes.push_back(type);
        });
        named.getValue().walk([&](SymbolRefAttr reference) {
          entry->second.references.push_back(reference);
        });
      }
      for (Type type : entry->second.forbiddenTypes) {
        op->emitError() << "attribute contains a forbidden semantic type "
                        << type;
        invalid = true;
      }
      for (SymbolRefAttr reference : entry->second.references) {
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
        bool definitionReference =
            ((isa<sim::SimScopeDeclOp>(op) &&
              named.getName() == sim::SimScopeDeclOp::getVpiDefinitionAttrName(
                                     op->getName())) ||
             (isa<sim::SimVPIDefinitionBindingOp>(op) &&
              named.getName() ==
                  sim::SimVPIDefinitionBindingOp::getDefinitionAttrName(
                      op->getName())) ||
             (isa<sim::SimVPIExpressionDeclOp>(op) &&
              named.getName() ==
                  sim::SimVPIExpressionDeclOp::getDefinitionAttrName(
                      op->getName())) ||
             (isa<sim::SimVPIDefinitionMemberDeclOp>(op) &&
              named.getName() ==
                  sim::SimVPIDefinitionMemberDeclOp::getDefinitionAttrName(
                      op->getName())) ||
             (isa<sim::SimVPIDefinitionSpecializationDeclOp>(op) &&
              named.getName() == sim::SimVPIDefinitionSpecializationDeclOp::
                                     getDefinitionAttrName(op->getName()))) &&
            reference.getNestedReferences().empty();
        bool specializationReference =
            ((isa<sim::SimScopeDeclOp>(op) &&
              named.getName() ==
                  sim::SimScopeDeclOp::getVpiSpecializationAttrName(
                      op->getName())) ||
             (isa<sim::SimVPIDefinitionMemberSpecializationOp>(op) &&
              named.getName() ==
                  sim::SimVPIDefinitionMemberSpecializationOp::
                      getSpecializationAttrName(op->getName()))) &&
            reference.getNestedReferences().empty();
        bool definitionMemberReference =
            isa<sim::SimVPIDefinitionTypeDefaultOp,
                sim::SimVPIDefinitionMemberSpecializationOp,
                sim::SimVPIDefinitionMemberInstanceBindingOp,
                sim::SimVPIDefinitionMemberInstanceRelationOp>(op) &&
            named.getName() ==
                (isa<sim::SimVPIDefinitionTypeDefaultOp>(op)
                     ? sim::SimVPIDefinitionTypeDefaultOp::getMemberAttrName(
                           op->getName())
                 : isa<sim::SimVPIDefinitionMemberSpecializationOp>(op)
                     ? sim::SimVPIDefinitionMemberSpecializationOp::
                           getMemberAttrName(op->getName())
                 : isa<sim::SimVPIDefinitionMemberInstanceBindingOp>(op)
                     ? sim::SimVPIDefinitionMemberInstanceBindingOp::
                           getMemberAttrName(op->getName())
                     : sim::SimVPIDefinitionMemberInstanceRelationOp::
                           getMemberAttrName(op->getName())) &&
            reference.getNestedReferences().empty();
        // Contextual definition members are exact typed inventory references,
        // not executable callees. Their SymbolUser verifier proves the scope's
        // definition matches the referenced member before finalization.
        bool contextualMemberReference = false;
        if (auto relation = dyn_cast<sim::SimVPIRelationDeclOp>(op))
          if (named.getName() == relation.getSourceAttrName() ||
              named.getName() == relation.getTargetAttrName())
            if (auto endpoint =
                    dyn_cast<sim::VPIObjectRefAttr>(named.getValue()))
              contextualMemberReference =
                  endpoint.getKind() == sim::VPIObjectRefKind::DefinitionMember &&
                  endpoint.getSymbol() == reference;
        if (auto binding =
                dyn_cast<sim::SimVPIDefinitionMemberInstanceBindingOp>(op))
          if (named.getName() == binding.getExprTargetAttrName()) {
            auto endpoint = binding.getExprTarget();
            contextualMemberReference =
                endpoint.getKind() == sim::VPIObjectRefKind::DefinitionMember &&
                endpoint.getSymbol() == reference;
          }
        bool originalDefaultReference = false;
        if (auto endpoint = dyn_cast<sim::VPIObjectRefAttr>(named.getValue());
            endpoint && endpoint.getKind() == sim::VPIObjectRefKind::Symbol &&
            endpoint.getSymbol() == reference) {
          if (auto relation = dyn_cast<sim::SimVPIRelationDeclOp>(op))
            originalDefaultReference =
                named.getName() == relation.getSourceAttrName() ||
                named.getName() == relation.getTargetAttrName();
          else if (auto expression = dyn_cast<sim::SimVPIExpressionDeclOp>(op))
            originalDefaultReference =
                expression.getDefinitionAttr() &&
                named.getName() == expression.getOwnerAttrName();
          originalDefaultReference &= isDefinitionMember(op, reference);
        }
        bool allowed = callTarget || observerTarget || graphReference ||
                       executableReference || definitionReference ||
                       specializationReference || definitionMemberReference ||
                       contextualMemberReference || originalDefaultReference;
        if (!allowed) {
          op->emitError() << "disallowed symbol reference " << reference;
          invalid = true;
        }
      }
    }
    if (isa<sim::ObeliskSimulationDialect>(op->getDialect()) &&
        !isa<MemoryEffectOpInterface>(op) &&
        !op->hasTrait<OpTrait::HasRecursiveMemoryEffects>()) {
      op->emitError("core operation has no precise memory-effect interface");
      invalid = true;
    }
    return WalkResult::advance();
  });
  return failure(invalid);
}

class ObeliskSimFinalizePass
    : public impl::ObeliskSimFinalizePassBase<ObeliskSimFinalizePass> {
public:
  void runOnOperation() override;
};

void ObeliskSimFinalizePass::runOnOperation() {
  ModuleOp module = getOperation();
  bool invalid = false;

  // Coverage keepalives are explicit SymbolUser edges needed only until the
  // final symbol-pruning pass. They are compiler inventory markers rather than
  // executable runtime operations.
  SmallVector<sim::SimCoverageKeepaliveOp> coverageKeepalives;
  module.walk([&](sim::SimCoverageKeepaliveOp op) {
    coverageKeepalives.push_back(op);
  });
  for (sim::SimCoverageKeepaliveOp op : coverageKeepalives)
    op.erase();

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
  DefinitionMemberTables definitionMembers;
  module.walk([&](Operation *op) {
    if (auto member = dyn_cast<sim::SimVPIDefinitionMemberDeclOp>(op)) {
      Operation *table = op->getParentOp();
      if (table && table->hasTrait<OpTrait::SymbolTable>() &&
          op->getBlock() == &table->getRegion(0).front())
        definitionMembers[table].insert(member.getSymName());
    }
    if (!isa<sim::SimCovergroupDeclOp, sim::SimVPIObjectAnchorOp,
             sim::SimVPINettypeDeclOp, sim::SimVPITypespecDeclOp,
             sim::SimVPIUnresolvedTypeAliasDeclOp,
             sim::SimVPILexicalClassTypespecDeclOp,
             sim::SimVPIClassExtendsDeclOp,
             sim::SimVPIClassParameterAssignDeclOp, sim::SimVPIBoxTypeDeclOp,
             sim::SimVPIClassMemberDeclOp, sim::SimVPIEventBindingOp,
             sim::SimVPIThreadOriginBindingOp, sim::SimVPICallDeclOp,
             sim::SimVPICodeUnitScopeBindingOp, sim::SimVPIInitializerBindingOp,
             sim::SimVPITerminalDriverBindingOp, sim::SimVPIExpressionDeclOp,
             sim::SimVPIDefinitionTypeDefaultOp, sim::SimVPIClassCallDispatchOp,
             sim::SimVPILocalDeclOp, sim::SimVPIForeachDeclOp,
             sim::SimVPIStaticLocalBindingOp, sim::SimVPICallableDeclOp,
             sim::SimVPIInterfaceMemberDeclOp,
             sim::SimVPIInterfaceInstanceBindingOp,
             sim::SimVPIInterfaceCallDispatchOp, sim::SimClassDeclOp,
             sim::SimClassFieldDeclOp, sim::SimClassMethodDeclOp,
             sim::SimRandomConstraintTemplateOp, sim::SimFuncOp>(op))
      return;
    if (auto name =
            op->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName()))
      executableSymbols.insert(name.getValue());
  });

  // Module cleanup and symbol discovery are complete. Verify module metadata
  // once, then verify and clean each isolated function independently.
  invalid |= failed(verifyExecutableSubtree(module, module, executableSymbols,
                                            definitionMembers,
                                            /*skipFunctions=*/true));
  SmallVector<sim::SimFuncOp> functions;
  module.walk([&](sim::SimFuncOp function) { functions.push_back(function); });

  invalid |= failed(failableParallelForEach(
      module.getContext(), functions, [&](sim::SimFuncOp function) {
        LogicalResult verified =
            verifyExecutableSubtree(function, module, executableSymbols,
                                    definitionMembers, /*skipFunctions=*/false);
        sim::remove<sim::metadata::Bindings>(function);
        sim::remove<sim::metadata::DelayScale>(function);
        sim::remove<sim::metadata::DelayQuantum>(function);
        sim::remove<sim::metadata::Lowered>(function);
        sim::remove<sim::metadata::ThisArgument>(function);
        sim::remove<sim::metadata::Constructor>(function);
        return verified;
      }));

  if (invalid)
    signalPassFailure();
}

} // namespace

static void buildObeliskToSimulationPipelineImpl(
    OpPassManager &manager, uint32_t workers, StringRef vpiMode,
    uint32_t optLevel, StringRef staticSpecialization, bool earlySymbolDCE,
    StringRef clockedKernels, bool instrumentVPIFrames) {
  ObeliskSimPreparePassOptions prepareOptions;
  prepareOptions.pruneUnusedCoverage = earlySymbolDCE && vpiMode == "off";
  // Source inventory may outlive executable code. Only a VPI profile needs
  // method bindings to retain otherwise unreachable implementations for
  // queries.
  prepareOptions.preserveVPIMethodBindings = vpiMode != "off";
  prepareOptions.preserveVPIBoxBindings = vpiMode != "off";
  prepareOptions.preserveVPIFrames = instrumentVPIFrames && vpiMode == "full";
  manager.addPass(createObeliskSimPreparePass(std::move(prepareOptions)));
  // Preparation freezes dynamic class dispatch, factory initialization, and
  // external entry points into explicit symbol references. Prune unreachable
  // private code units before paying the per-function lowering cost.
  if (earlySymbolDCE) {
    OpPassManager &earlyDesignManager = manager.nest<sim::SimDesignOp>();
    earlyDesignManager.addPass(createSymbolDCEPass());
  }
  // Coverage owns module-level schema metadata, so run it at the module
  // anchor after the nested early DCE has completed. A nested pass must not
  // mutate its parent operation.
  manager.addPass(createObeliskSimPrepareCoveragePass());
  // Prepared virtual calls expose all compatible targets as symbol edges.
  // Once DCE removes unused method families, compact their vtable slots before
  // those call contracts are lowered to executable dispatch operations.
  ObeliskSimDevirtualizeClassCallsPassOptions earlyDevirtualizeOptions;
  earlyDevirtualizeOptions.preserveAllMethods = !earlySymbolDCE;
  manager.nest<sim::SimDesignOp>().addPass(
      createObeliskSimDevirtualizeClassCallsPass(
          std::move(earlyDevirtualizeOptions)));
  // Freeze after DCE and dispatch preparation, before any nested function
  // worker can replace a sibling function's attribute dictionary.
  manager.addPass(createObeliskSimPrepareUnitLoweringPass());
  OpPassManager &designManager = manager.nest<sim::SimDesignOp>();
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createObeliskSimLowerUnitPass());
#ifdef OBELISK_ENABLE_Z3
    // Exact random solves sample a plan compiled from their residual program.
    functionManager.addPass(createObeliskSimCompileRandomPlansPass());
#endif
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
    // Promote direct local cells. Keep aggregate field accesses addressable;
    // native lowering exposes them to LLVM's target-aware SROA pipeline.
    functionManager.addPass(createMem2Reg());
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
  }
  if (vpiMode != "off") {
    designManager.addPass(std::make_unique<MaterializeVPIClassDispatchPass>());
    if (instrumentVPIFrames && vpiMode == "full")
      designManager.addPass(createObeliskSimInstrumentVPIFramesPass());
  }
  designManager.addPass(createObeliskSimMaterializeClockedSamplesPass());
  if (optLevel > 0)
    designManager.addPass(createObeliskSimDevirtualizeClassCallsPass());
  // Graph metadata does not participate in symbol liveness. Complete all
  // symbol pruning before graph construction so its nested references can
  // never become stale.
  designManager.addPass(createSymbolDCEPass());
  designManager.addPass(
      std::make_unique<PruneUnusedInitializationBindingsPass>());
  ObeliskSimSCCPPassOptions firstSCCPOptions;
  firstSCCPOptions.vpi = vpiMode.str();
  designManager.addPass(createObeliskSimSCCPPass(std::move(firstSCCPOptions)));
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
    // IEEE 1800-2023 6.21: inlining removes local-reference escapes without
    // changing automatic initialization or lifetime. Expose whole-value
    // updates before promoting newly private cells (7.4.6, 11.5.1).
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createMem2Reg());
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
  }
  if (optLevel > 0)
    designManager.addPass(createObeliskSimDevirtualizeClassCallsPass());
  ObeliskSimSCCPPassOptions secondSCCPOptions;
  secondSCCPOptions.vpi = vpiMode.str();
  designManager.addPass(createObeliskSimSCCPPass(std::move(secondSCCPOptions)));
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
  }
  if (optLevel > 0)
    designManager.addPass(createObeliskSimEliminateDeadBoundariesPass());
  else
    // Even -O0 must not preserve unused values across native ABI boundaries:
    // a dead wide task argument becomes a giant LLVM value in every spawn and
    // activation wrapper. Keep result/call elimination optimization-gated,
    // but always run the semantics-preserving argument-only analysis.
    designManager.addPass(createObeliskSimEliminateDeadCapturesPass());
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    functionManager.addPass(createCanonicalizerPass());
    functionManager.addPass(createCSEPass());
  }
  designManager.addPass(createSymbolDCEPass());

  if (optLevel > 0 && vpiMode == "off") {
    // IEEE 1800-2023 23.3.3/4.9.6/4.7: an invisible single-driver port
    // connection may update its sink with the source write itself. Share the
    // storage before clock extraction sees clock ports, then drop the copies.
    ObeliskSimAliasPortConnectionsPassOptions aliasOptions;
    aliasOptions.vpi = vpiMode.str();
    designManager.addPass(
        createObeliskSimAliasPortConnectionsPass(std::move(aliasOptions)));
    designManager.addPass(createSymbolDCEPass());
  }

  if (optLevel > 0) {
    ObeliskSimExtractPeriodicClocksPassOptions periodicOptions;
    periodicOptions.vpi = vpiMode.str();
    designManager.addPass(
        createObeliskSimExtractPeriodicClocksPass(std::move(periodicOptions)));
  }

  // Make continuation-frame state explicit before freezing whole-program
  // summaries. Threading can rematerialize pointer-free constants in resume
  // blocks, which is an executable CFG change and therefore must precede the
  // persistent compute graph.
  if (optLevel > 0) {
    ObeliskSimPropagateInitializedStoragePassOptions initializedStorageOptions;
    initializedStorageOptions.vpi = vpiMode.str();
    designManager.addPass(createObeliskSimPropagateInitializedStoragePass(
        std::move(initializedStorageOptions)));
    ObeliskSimSimplifyBodiesPassOptions simplifyOptions;
    simplifyOptions.vpi = vpiMode.str();
    designManager.addPass(createObeliskSimSimplifyBodiesPass(simplifyOptions));
    auto &functions = designManager.nest<sim::SimFuncOp>();
    functions.addPass(createCanonicalizerPass());
    functions.addPass(createMem2Reg());
    functions.addPass(createCSEPass());
  }
  {
    OpPassManager &functionManager = designManager.nest<sim::SimFuncOp>();
    if (optLevel > 0) {
      // Replicate small complete nests before four-state expansion inflates
      // their size. IEEE 1800-2023 4.6/12.7.1: preserve every statement and
      // induction step in order; only a proved finite recurrence is eligible.
      if (optLevel >= 3)
        functionManager.addPass(createObeliskSimUnrollBoundedLoopsPass());
      functionManager.addPass(createCanonicalizerPass());
      functionManager.addPass(createCSEPass());
    }
  }
  if (optLevel >= 3) {
    // Replication can expose constants through CFG arguments that ordinary
    // canonicalization cannot propagate. Remove the resulting dead branches
    // before effect summaries and compute-fusion admission inspect the body.
    ObeliskSimSCCPPassOptions cleanupOptions;
    cleanupOptions.vpi = vpiMode.str();
    designManager.addPass(createObeliskSimSCCPPass(std::move(cleanupOptions)));
    auto &functions = designManager.nest<sim::SimFuncOp>();
    functions.addPass(createCanonicalizerPass());
    functions.addPass(createCSEPass());
  }
  if (optLevel > 0) {
    ObeliskSimSimplifyBodiesPassOptions simplifyOptions;
    simplifyOptions.vpi = vpiMode.str();
    designManager.addPass(createObeliskSimSimplifyBodiesPass(simplifyOptions));
    auto &functions = designManager.nest<sim::SimFuncOp>();
    functions.addPass(createCanonicalizerPass());
    functions.addPass(createMem2Reg());
    functions.addPass(createCSEPass());
  }
  if (optLevel >= 3) {
    ObeliskSimRefineContinuousSensitivityPassOptions sensitivityOptions;
    sensitivityOptions.vpi = vpiMode.str();
    designManager.addPass(createObeliskSimRefineContinuousSensitivityPass(
        std::move(sensitivityOptions)));
  }
  {
    auto &functions = designManager.nest<sim::SimFuncOp>();
    // Finish executable simplification before making continuation liveness
    // explicit. Canonicalization after threading can remove forwarded live
    // values and change admission when the frame planner threads them again.
    // Record the remaining loop proofs before those new arguments obscure
    // the induction shape, then freeze the complete suspension operands.
    functions.addPass(createObeliskSimMarkBoundedLoopsPass());
    functions.addPass(createObeliskSimThreadSuspensionPass());
  }

  if (optLevel > 0)
    designManager.addPass(createObeliskSimMaterializeClockedControlPass());

  ObeliskSimBuildComputeGraphPassOptions graphOptions;
  graphOptions.workers = workers;
  graphOptions.vpi = vpiMode.str();
  graphOptions.pruneEffectFree = optLevel > 0;
  designManager.addPass(
      createObeliskSimBuildComputeGraphPass(std::move(graphOptions)));
  // Forced-native generated gate cohorts must not materialize one LLVM
  // coroutine per primitive.  This is a structural compile-space bound rather
  // than an optimization-level choice, so form fixed i64-mask kernels even at
  // O0.  The default auto policy marks large primitive fragments bytecode;
  // the native-tier filter in the fusion planner deliberately leaves those
  // compact actors unchanged.
  {
    ObeliskSimFuseComputeFragmentsPassOptions primitiveFusionOptions;
    primitiveFusionOptions.bodyFusion = true;
    primitiveFusionOptions.primitiveOnly = true;
    primitiveFusionOptions.maxStraightLineMembers = 16;
    designManager.addPass(createObeliskSimFuseComputeFragmentsPass(
        std::move(primitiveFusionOptions)));
    // Early evaluator copies retain accesses from actors subsequently fused
    // away, preventing private-state proofs on the final kernel. Clone only
    // after the last fusion round; O0 has no later round.
    ObeliskSimMaterializeComputeFusionPassOptions materializeOptions;
    materializeOptions.materializeEvalBodies = optLevel == 0;
    designManager.addPass(createObeliskSimMaterializeComputeFusionPass(
        std::move(materializeOptions)));
    ObeliskSimBuildComputeGraphPassOptions primitiveGraphOptions;
    primitiveGraphOptions.workers = workers;
    primitiveGraphOptions.vpi = vpiMode.str();
    primitiveGraphOptions.pruneEffectFree = optLevel > 0;
    designManager.addPass(createObeliskSimBuildCurrentComputeGraphPass(
        std::move(primitiveGraphOptions)));
  }
  if (optLevel > 0) {
    if (vpiMode == "off" && clockedKernels != "off") {
      // IEEE 1800-2023 4.3/4.5/4.7: run each clock edge's processes as one
      // activation and store NBAs that nothing can observe early directly.
      ObeliskSimFuseClockedKernelsPassOptions kernelOptions;
      kernelOptions.defer = clockedKernels == "deferred";
      designManager.addPass(
          createObeliskSimFuseClockedKernelsPass(std::move(kernelOptions)));
      ObeliskSimBuildComputeGraphPassOptions clockedGraphOptions;
      clockedGraphOptions.workers = workers;
      clockedGraphOptions.vpi = vpiMode.str();
      clockedGraphOptions.pruneEffectFree = optLevel > 0;
      designManager.addPass(createObeliskSimBuildCurrentComputeGraphPass(
          std::move(clockedGraphOptions)));
    }
    if (optLevel >= 3) {
      designManager.addPass(createPlanCombinationalStatementsPass());
      ObeliskSimMaterializeComputeFusionPassOptions materializeOptions;
      materializeOptions.materializeEvalBodies = false;
      designManager.addPass(createObeliskSimMaterializeComputeFusionPass(
          std::move(materializeOptions)));
      ObeliskSimBuildComputeGraphPassOptions statementGraphOptions;
      statementGraphOptions.workers = workers;
      statementGraphOptions.vpi = vpiMode.str();
      statementGraphOptions.pruneEffectFree = optLevel > 0;
      designManager.addPass(createObeliskSimBuildCurrentComputeGraphPass(
          std::move(statementGraphOptions)));
    }
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
    designManager.addPass(createLocalizePrivateEvalStoragePass());
    // Localization creates slots after the early mem2reg passes. Destructure
    // and promote them before rebuilding effects and the final compute graph;
    // canonical actors must retain their frozen continuation operands.
    designManager.addPass(createPromoteEvalStoragePass());
    ObeliskSimBuildComputeGraphPassOptions fusedGraphOptions;
    fusedGraphOptions.workers = workers;
    fusedGraphOptions.vpi = vpiMode.str();
    fusedGraphOptions.pruneEffectFree = optLevel > 0;
    // Body fusion removes the graph exactly when it changes executable CFG.
    // If no candidate materialized, the late inliner is also a no-op and the
    // already verified graph remains authoritative.
    designManager.addPass(createObeliskSimBuildCurrentComputeGraphPass(
        std::move(fusedGraphOptions)));
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

void buildObeliskToSimulationPipeline(OpPassManager &manager, uint32_t workers,
                                      StringRef vpiMode, uint32_t optLevel,
                                      StringRef staticSpecialization,
                                      bool earlySymbolDCE,
                                      StringRef clockedKernels) {
  buildObeliskToSimulationPipelineImpl(manager, workers, vpiMode, optLevel,
                                       staticSpecialization, earlySymbolDCE,
                                       clockedKernels, false);
}

void buildObeliskToSimulationPipeline(OpPassManager &manager) {
  buildObeliskToSimulationPipeline(manager, 1, "off", 3, "auto", true);
}

void buildObeliskToSimulationPipeline(OpPassManager &manager, uint32_t workers,
                                      StringRef vpiMode) {
  buildObeliskToSimulationPipeline(manager, workers, vpiMode, 3, "auto", true);
}

void buildObeliskToSimulationPipeline(OpPassManager &manager, uint32_t workers,
                                      StringRef vpiMode, uint32_t optLevel) {
  buildObeliskToSimulationPipeline(manager, workers, vpiMode, optLevel, "auto",
                                   true);
}

void registerObeliskToSimulationPipeline() {
  PassPipelineRegistration<ObeliskToSimulationPipelineOptions>(
      "lower-obelisk-to-sim",
      "Lower elaborated obelisk.sv semantic IR to isolated simulation SSA",
      [](OpPassManager &manager,
         const ObeliskToSimulationPipelineOptions &options) {
        buildObeliskToSimulationPipelineImpl(
            manager, options.workers.getValue(), options.vpi.getValue(),
            options.optLevel.getValue(),
            options.staticSpecialization.getValue(),
            options.earlySymbolDCE.getValue(),
            options.clockedKernels.getValue(),
            options.instrumentVPIFrames.getValue());
      });
}

} // namespace obelisk
