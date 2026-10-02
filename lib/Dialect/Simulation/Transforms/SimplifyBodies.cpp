#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Analysis/DemandedBitAnalysis.h"
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Analysis/LogicBitAnalysis.h"
#include "obelisk/Analysis/PrivateStorageAnalysis.h"
#include "obelisk/Analysis/SimulationEffectAnalysis.h"
#include "obelisk/Analysis/SourceMemoryAnalysis.h"
#include "obelisk/Analysis/StorageWriteAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"

using namespace mlir;
namespace obelisk {
#define GEN_PASS_DEF_OBELISKSIMSIMPLIFYBODIESPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"
namespace {
class TerminationLattice final : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  bool reachable = false, mayTerminate = false;
  Value lastPoll;
  ChangeResult merge(bool may, Value poll) {
    if (!reachable) {
      reachable = true;
      mayTerminate = may;
      lastPoll = poll;
      return ChangeResult::Change;
    }
    bool nextMay = mayTerminate || may;
    Value nextPoll = lastPoll == poll ? poll : Value{};
    bool changed = nextMay != mayTerminate || nextPoll != lastPoll;
    mayTerminate = nextMay;
    lastPoll = nextPoll;
    return changed ? ChangeResult::Change : ChangeResult::NoChange;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const TerminationLattice &>(rhs);
    return other.reachable ? merge(other.mayTerminate, other.lastPoll)
                           : ChangeResult::NoChange;
  }
  void print(raw_ostream &os) const override { os << mayTerminate; }
};
class TerminationDataflow final
    : public dataflow::DenseForwardDataFlowAnalysis<TerminationLattice> {
public:
  TerminationDataflow(DataFlowSolver &solver,
                      const analysis::SimulationEffectAnalysis &effects)
      : DenseForwardDataFlowAnalysis(solver), effects(effects) {}
  LogicalResult visitOperation(Operation *op, const TerminationLattice &before,
                               TerminationLattice *after) override {
    if (!before.reachable)
      return success();
    bool may = before.mayTerminate;
    Value poll = before.lastPoll;
    if (auto query = dyn_cast<sim::SimTerminationRequestedOp>(op)) {
      poll = query.getResult();
    } else if (auto call = dyn_cast<sim::SimCallOp>(op)) {
      auto callee = symbols.lookupNearestSymbolFrom<sim::SimFuncOp>(
          call, call.getCalleeAttr());
      if (!callee || !effects.isHarmless(callee)) {
        may = true;
        poll = {};
      }
    } else if (!isa<cf::BranchOp, cf::CondBranchOp>(op) &&
               !isMemoryEffectFree(op)) {
      may = true;
      poll = {};
    }
    propagateIfChanged(after, after->merge(may, poll));
    return success();
  }
  void visitBlockTransfer(Block *block, ProgramPoint *, Block *predecessor,
                          const TerminationLattice &before,
                          TerminationLattice *after) override {
    if (!before.reachable)
      return;
    auto branch = dyn_cast<cf::CondBranchOp>(predecessor->getTerminator());
    bool clear = branch && branch.getCondition() == before.lastPoll &&
                 branch.getFalseDest() == block &&
                 branch.getTrueDest() != block;
    propagateIfChanged(after, after->merge(clear ? false : before.mayTerminate,
                                           before.lastPoll));
  }
  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction,
                                    const TerminationLattice &before,
                                    TerminationLattice *after) override {
    (void)visitOperation(call, before, after);
  }

private:
  void setToEntryState(TerminationLattice *state) override {
    // Entry does not assume the latch is clear. Only an executed false poll
    // edge proves that, and every hazardous call invalidates the proof.
    propagateIfChanged(state, state->merge(true, {}));
  }
  const analysis::SimulationEffectAnalysis &effects;
  SymbolTableCollection symbols;
};
void simplifyTerminationPolls(
    sim::SimFuncOp function,
    const analysis::SimulationEffectAnalysis &effects) {
  SmallVector<sim::SimTerminationRequestedOp> polls;
  function.walk(
      [&](sim::SimTerminationRequestedOp poll) { polls.push_back(poll); });
  if (polls.size() < 2)
    return;
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<TerminationDataflow>(effects);
  if (failed(solver.initializeAndRun(function)))
    return;
  SmallVector<sim::SimTerminationRequestedOp> redundant;
  for (auto poll : polls)
    if (auto *state = solver.lookupState<TerminationLattice>(
            solver.getProgramPointBefore(poll));
        state && state->reachable && !state->mayTerminate)
      redundant.push_back(poll);
  for (auto poll : redundant) {
    OpBuilder builder(poll);
    Value clear =
        arith::ConstantOp::create(builder, poll.getLoc(), builder.getI1Type(),
                                  builder.getBoolAttr(false));
    poll.replaceAllUsesWith(clear);
    poll.erase();
  }
}
void removeDeadPrivateStores(sim::SimDesignOp design) {
  DenseSet<uint64_t> candidates;
  for (auto storage : design.getOps<sim::SimStorageDeclOp>())
    if (storage->hasAttr(sim::metadata::subroutineStorage) &&
        isa<IntegerType, sim::LogicType>(storage.getType()))
      candidates.insert(storage.getId());
  if (candidates.empty())
    return;
  SmallVector<std::pair<sim::SimRefStoreOp, uint64_t>> stores;
  bool foreign = false;
  analysis::HandleDataflowAnalysis handles(design);
  for (auto function : design.getOps<sim::SimFuncOp>()) {
    auto facts = handles.derive(function);
    function.walk([&](Operation *op) {
      foreign |= isa<sim::SimDPICallOp>(op);
      if (auto call = dyn_cast<sim::SimCallOp>(op)) {
        auto callee = design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
        foreign |= !callee || callee.isExternal();
      }
      for (Value operand : op->getOperands()) {
        if (!isa<sim::RefType>(operand.getType()))
          continue;
        auto fact = facts.find(operand);
        if (fact == facts.end() || !fact->second.descriptor)
          continue;
        uint64_t root = *fact->second.descriptor;
        if (auto store = dyn_cast<sim::SimRefStoreOp>(op);
            store && store.getReference() == operand) {
          stores.emplace_back(store, root);
        } else if (isa<sim::SimRefExtractOp, sim::SimRefDynExtractOp,
                       sim::SimRefArrayElementOp, sim::SimRefSubelementOp>(
                       op) &&
                   llvm::all_of(op->getResults(), [&](Value result) {
                     auto selected = facts.find(result);
                     return selected != facts.end() &&
                            selected->second.descriptor ==
                                fact->second.descriptor;
                   })) {
          // Only complete root provenance keeps an unobserved view private.
        } else {
          // Loads, waits, aliases, captures, force/release and escaping
          // references all make the state observable, regardless of liveness.
          candidates.erase(root);
        }
      }
    });
  }
  if (foreign)
    return;
  for (auto [store, root] : stores)
    if (candidates.contains(root))
      store.erase();
}
void promotePrivateReductions(sim::SimDesignOp design) {
  analysis::PrivateStorageAnalysis storage(design);
  // Immutable access inventories and dense must-definition facts precede all
  // rewrites. Generic mem2reg subsequently constructs the necessary phis.
  DenseMap<Operation *,
           SmallVector<const analysis::PrivateStorageAnalysis::Accesses *>>
      owners;
  for (const auto &[id, access] : storage.getRoots()) {
    // The closed access inventory also proves source-authored write-only
    // state dead when VPI/foreign observation is excluded. Keep declarations
    // and identities, but let canonicalization remove the unused producers.
    if (access.owner && access.loads.empty()) {
      for (auto store : access.stores)
        store.erase();
    } else if (access.owner && access.stores.size() > 1)
      owners[access.owner].push_back(&access);
  }
  for (const auto &[operation, roots] : owners) {
    auto function = cast<sim::SimFuncOp>(operation);
    bool calls =
        function
            .walk([](Operation *op) {
              return isa<sim::SimCallOp, sim::SimTaskCallOp, sim::SimSpawnOp,
                         sim::SimClassDirectCallOp, sim::SimClassVirtualCallOp,
                         sim::SimClassVirtualTaskCallOp>(op)
                         ? WalkResult::interrupt()
                         : WalkResult::advance();
            })
            .wasInterrupted();
    if (calls)
      continue;
    SmallVector<Operation *> definitions;
    for (const auto *access : roots)
      for (auto store : access->stores)
        definitions.push_back(store);
    auto barrier = [](Operation *op) { return sim::isSuspensionOp(op); };
    analysis::MustDefinitionAnalysis must(function, definitions, barrier);
    DominanceInfo dominance(function);
    for (const auto *access : roots) {
      auto declaration = access->declaration;
      Type type = declaration.getType();
      if (!isa<IntegerType, sim::LogicType>(type) ||
          !llvm::all_of(access->loads,
                        [&](auto load) { return load.getType() == type; }) ||
          !llvm::all_of(access->stores, [&](auto store) {
            return store.getValue().getType() == type;
          }))
        continue;
      bool initialized = llvm::any_of(access->stores, [&](auto store) {
        return llvm::all_of(access->loads, [&](auto load) {
          return dominance.dominates(store.getOperation(),
                                     load.getOperation()) &&
                 must.containsBefore(store, load);
        });
      });
      if (!initialized)
        continue;
      // Invisible exclusive state has no transition consumer. Its persistent
      // contents are overwritten before every read in each activation.
      OpBuilder builder(&function.getBody().front(),
                        function.getBody().front().begin());
      Value initial;
      if (auto integer = dyn_cast<IntegerType>(type))
        initial = arith::ConstantOp::create(builder, function.getLoc(), type,
                                            builder.getIntegerAttr(type, 0));
      else if (auto logic = dyn_cast<sim::LogicType>(type)) {
        auto plane = builder.getIntegerType(logic.getWidth());
        initial = sim::SimLogicConstantOp::create(
            builder, function.getLoc(), type,
            builder.getIntegerAttr(plane, APInt(logic.getWidth(), 0)),
            builder.getIntegerAttr(plane, APInt::getAllOnes(logic.getWidth())));
      }
      if (!initial)
        continue;
      auto local = sim::SimRefAllocOp::create(
          builder, function.getLoc(),
          sim::RefType::get(design.getContext(), initial.getType()), initial);
      for (auto load : access->loads)
        load.getReferenceMutable().assign(local);
      for (auto store : access->stores)
        store.getReferenceMutable().assign(local);
    }
  }
}
void promotePrivateAggregates(sim::SimDesignOp design) {
  analysis::PrivateStorageAnalysis storage(design);
  DenseMap<Operation *, bool> allowed;
  for (const auto &[id, access] : storage.getRoots()) {
    if (!access.owner || !access.promotableViews ||
        access.bases.empty() || access.stores.size() < 2)
      continue;
    auto declaration = access.declaration;
    auto width = sim::getProvenanceSpan(declaration.getType());
    // Bound SSA/frame growth. Large memories retain canonical storage.
    if (!width || *width > 512 ||
        (!sim::getPackedScalarType(declaration.getType()) &&
         !sim::getFixedBitStreamPlan(declaration.getType())))
      continue;
    auto function = access.owner;
    auto [safe, inserted] = allowed.try_emplace(function, true);
    if (inserted)
      safe->second =
          !function
               .walk([&](Operation *op) {
                 return op == function || isMemoryEffectFree(op) ||
                                isa<sim::SimRefLoadOp, sim::SimRefStoreOp,
                                    sim::SimReturnOp, cf::BranchOp,
                                    cf::CondBranchOp>(op) ||
                                sim::isSuspensionOp(op)
                            ? WalkResult::advance()
                            : WalkResult::interrupt();
               })
               .wasInterrupted();
    if (!safe->second)
      continue;
    SmallVector<Operation *> boundaries;
    DenseSet<Block *> resumptions;
    function.walk([&](Operation *op) {
      if (isa<sim::SimReturnOp>(op))
        boundaries.push_back(op);
      else if (sim::isSuspensionOp(op)) {
        boundaries.push_back(op);
        for (Block *successor : op->getSuccessors())
          resumptions.insert(successor);
      }
    });
    // Materialization at every suspension/return must remove stores, not
    // multiply them. In particular, a long initial process with many waits
    // should not acquire whole-root flushes at all of those cold boundaries.
    if (boundaries.empty() || function.getBody().empty() ||
        boundaries.size() >= access.stores.size())
      continue;
    Value context;
    for (Value argument : function.getBody().front().getArguments())
      if (isa<sim::ContextType>(argument.getType())) {
        context = argument;
        break;
      }
    if (!context)
      continue;
    Value canonical;
    for (Value base : access.bases)
      if (auto argument = dyn_cast<BlockArgument>(base);
          argument && argument.getOwner() == &function.getBody().front()) {
        canonical = base;
        break;
      }
    // An immutable entry capture needs no live frame slot. Without one,
    // suspendable owners would acquire an implementation handle in their
    // fallback frame, offsetting the representation saving.
    if (!canonical && !resumptions.empty())
      continue;
    SmallVector<OpOperand *> originalUses;
    for (Value base : access.bases)
      for (OpOperand &use : base.getUses())
        originalUses.push_back(&use);
    OpBuilder builder(&function.getBody().front(),
                      function.getBody().front().begin());
    Type type = declaration.getType();
    auto ref = sim::RefType::get(design.getContext(), type);
    if (!canonical)
      canonical = sim::SimContextStorageOp::create(builder, function.getLoc(),
                                                   ref, context, id);
    Value initial =
        sim::SimRefLoadOp::create(builder, function.getLoc(), type, canonical);
    auto local =
        sim::SimRefAllocOp::create(builder, function.getLoc(), ref, initial);
    for (OpOperand *use : originalUses)
      use->set(local);
    for (Operation *boundary : boundaries) {
      builder.setInsertionPoint(boundary);
      Value value =
          sim::SimRefLoadOp::create(builder, boundary->getLoc(), type, local);
      auto flush = sim::SimRefStoreOp::create(builder, boundary->getLoc(),
                                              value, canonical);
      if (llvm::all_of(access.stores, [](auto store) {
            return store->hasAttr("simulation.continuous_store");
          }))
        flush->setAttr("simulation.continuous_store", builder.getUnitAttr());
    }
    // Other instances of the same source process may run while suspended.
    // Reload canonical state before any resumed access; private SSA never
    // carries an equality or ownership assumption across that boundary.
    for (Block *block : resumptions) {
      builder.setInsertionPointToStart(block);
      Value value = sim::SimRefLoadOp::create(builder, function.getLoc(), type,
                                              canonical);
      sim::SimRefStoreOp::create(builder, function.getLoc(), value, local);
    }
  }
}
struct ObeliskSimSimplifyBodiesPass
    : impl::ObeliskSimSimplifyBodiesPassBase<ObeliskSimSimplifyBodiesPass> {
  using ObeliskSimSimplifyBodiesPassBase::ObeliskSimSimplifyBodiesPassBase;
  void runOnOperation() override {
    auto design = getOperation();
    if (vpi != "off" && vpi != "read" && vpi != "full") {
      design.emitError("invalid VPI body simplification contract");
      return signalPassFailure();
    }
    bool canonicalWrites = vpi == "off";
    design.walk([&](Operation *op) {
      if (isa<sim::SimOverrideOp, sim::SimDynamicOverrideOp,
              sim::SimReleaseOverrideOp>(op))
        canonicalWrites = false;
    });
    if (canonicalWrites) {
      promotePrivateReductions(design);
      promotePrivateAggregates(design);
    }
    for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
      analysis::SourceMemoryAnalysis memory(function, canonicalWrites);
      DominanceInfo dominance(function);
      SmallVector<sim::SimRefLoadOp> loads;
      function.walk([&](sim::SimRefLoadOp load) { loads.push_back(load); });
      // Freeze all analysis facts before mutation. A forwarding chain is
      // resolved through replacement Values; no stale operation is queried.
      DenseMap<Value, Value> replacements;
      for (auto load : loads) {
        Value previous = memory.getForwarded(load);
        if (auto element = memory.getElementSnapshot(load)) {
          Value aggregate = element->aggregate;
          while (replacements.contains(aggregate))
            aggregate = replacements.lookup(aggregate);
          if (dominance.dominates(aggregate, load)) {
            OpBuilder builder(load);
            previous = sim::SimArrayDynExtractOp::create(
                builder, load.getLoc(), load.getType(), aggregate,
                element->index);
          }
        }
        while (previous && replacements.contains(previous))
          previous = replacements.lookup(previous);
        if (!previous || previous.getType() != load.getType() ||
            !dominance.dominates(previous, load))
          continue;
        replacements.try_emplace(load.getResult(), previous);
        load.getResult().replaceAllUsesWith(previous);
        load.erase();
      }
      LogicBitAnalysis bits(function);
      analysis::DemandedBitAnalysis demanded(function);
      SmallVector<Operation *> constants;
      function.walk([&](Operation *op) {
        if (op->getNumResults() == 1 && isMemoryEffectFree(op) &&
            !isa<sim::SimLogicConstantOp>(op))
          if (auto fact = bits.get(op->getResult(0))) {
            const auto *mask = demanded.get(op->getResult(0));
            if ((fact->zero | fact->one).isAllOnes() ||
                (mask && mask->getBitWidth() == fact->one.getBitWidth() &&
                 ((*mask & ~(fact->zero | fact->one)).isZero())))
              constants.push_back(op);
          }
      });
      for (Operation *op : constants) {
        auto fact = bits.get(op->getResult(0));
        OpBuilder builder(op);
        auto replacement = sim::SimLogicConstantOp::create(
            builder, op->getLoc(), op->getResult(0).getType(),
            builder.getIntegerAttr(
                builder.getIntegerType(fact->one.getBitWidth()), fact->one),
            builder.getIntegerAttr(
                builder.getIntegerType(fact->one.getBitWidth()),
                APInt(fact->one.getBitWidth(), 0)));
        op->getResult(0).replaceAllUsesWith(replacement);
        op->erase();
      }
    }
    analysis::SimulationEffectAnalysis effects(design);
    for (auto function : design.getBody().front().getOps<sim::SimFuncOp>())
      simplifyTerminationPolls(function, effects);
    if (canonicalWrites)
      removeDeadPrivateStores(design);
  }
};
} // namespace
} // namespace obelisk
