#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/RegionUtils.h"
#include "talos/Analysis/ActivationRegionAnalysis.h"
#include "talos/Conversion/SimulationToSchedule/Utils.h"
#include "talos/Analysis/DemandedBitAnalysis.h"
#include "talos/Analysis/HandleDataflowAnalysis.h"
#include "talos/Analysis/LogicBitAnalysis.h"
#include "talos/Analysis/PrivateStorageAnalysis.h"
#include "talos/Analysis/SimulationEffectAnalysis.h"
#include "talos/Analysis/SourceMemoryAnalysis.h"
#include "talos/Analysis/StorageWriteAnalysis.h"
#include "talos/Dialect/Simulation/SimulationMetadata.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Dialect/Simulation/Transforms/Passes.h"

using namespace mlir;
namespace talos {
#define GEN_PASS_DEF_TALOSSIMSIMPLIFYBODIESPASS
#include "talos/Dialect/Simulation/Transforms/Passes.h.inc"
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
/// Event-triggered processes start, and resume from a wait, only while the
/// finish latch is clear: the scheduler stops dispatching once $finish, $stop,
/// $fatal or program completion latches it (IEEE 1800-2023 20.2, 9.2.3).
/// Final procedures, subroutines and observers may run with it set; time-zero
/// initializers keep their conservative entry poll.
bool startsWithClearTermination(sim::SimFuncOp function) {
  switch (function.getEntryKind()) {
  case sim::EntryKind::Always:
  case sim::EntryKind::AlwaysComb:
  case sim::EntryKind::AlwaysFF:
  case sim::EntryKind::AlwaysLatch:
  case sim::EntryKind::Continuous:
  case sim::EntryKind::PortInput:
  case sim::EntryKind::PortOutput:
    return true;
  default:
    return false;
  }
}
class TerminationDataflow final
    : public dataflow::DenseForwardDataFlowAnalysis<TerminationLattice> {
public:
  TerminationDataflow(DataFlowSolver &solver,
                      const analysis::SimulationEffectAnalysis &effects,
                      const analysis::HandleFacts &handles, bool clearEntry)
      : DenseForwardDataFlowAnalysis(solver), effects(effects),
        handles(handles), clearEntry(clearEntry) {}
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
    } else if (clearEntry &&
               isa<sim::SimSuspendChangeOp, sim::SimSuspendAnyOp,
                   sim::SimSuspendEdgeOp, sim::SimSuspendDelayOp,
                   sim::SimSuspendEventOp, sim::SimSuspendForeverOp>(op)) {
      may = false;
      poll = {};
    } else if (!isa<cf::BranchOp, cf::CondBranchOp>(op) &&
               !isMemoryEffectFree(op) && !cannotFail(op)) {
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
  /// Accesses to static storage, nets and plain local slots have no runtime
  /// failure path, so they cannot request termination.
  bool cannotFail(Operation *op) const {
    if (auto alloc = dyn_cast<sim::SimRefAllocOp>(op))
      return analysis::isPlainValueType(alloc.getType().getElementType());
    Value reference;
    if (auto load = dyn_cast<sim::SimRefLoadOp>(op))
      reference = load.getReference();
    else if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
      reference = store.getReference();
    else if (auto read = dyn_cast<sim::SimNetReadOp>(op))
      reference = read.getNet();
    if (!reference)
      return false;
    auto fact = handles.find(reference);
    return fact != handles.end() &&
           (fact->second.resource == schedule::ComputeResourceKind::Storage ||
            fact->second.resource == schedule::ComputeResourceKind::Net ||
            fact->second.resource == schedule::ComputeResourceKind::Local);
  }
  void setToEntryState(TerminationLattice *state) override {
    // Only scheduled processes may assume a clear latch at entry. Otherwise an
    // executed false poll edge proves it, and every hazard invalidates it.
    propagateIfChanged(state, state->merge(!clearEntry, {}));
  }
  const analysis::SimulationEffectAnalysis &effects;
  const analysis::HandleFacts &handles;
  bool clearEntry;
  SymbolTableCollection symbols;
};
void simplifyTerminationPolls(
    sim::SimFuncOp function, const analysis::SimulationEffectAnalysis &effects,
    const analysis::HandleDataflowAnalysis &provenance, bool clearEntry) {
  SmallVector<sim::SimTerminationRequestedOp> polls;
  function.walk(
      [&](sim::SimTerminationRequestedOp poll) { polls.push_back(poll); });
  if (polls.size() < (clearEntry ? 1u : 2u))
    return;
  analysis::HandleFacts handles = provenance.derive(function);
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<TerminationDataflow>(effects, handles, clearEntry);
  if (failed(solver.initializeAndRun(function)))
    return;
  SmallVector<sim::SimTerminationRequestedOp> redundant;
  for (auto poll : polls)
    if (auto *state = solver.lookupState<TerminationLattice>(
            solver.getProgramPointBefore(poll));
        state && state->reachable && !state->mayTerminate)
      redundant.push_back(poll);
  SmallVector<Operation *> branches;
  for (auto poll : redundant) {
    OpBuilder builder(poll);
    Value clear =
        arith::ConstantOp::create(builder, poll.getLoc(), builder.getI1Type(),
                                  builder.getBoolAttr(false));
    poll.replaceAllUsesWith(clear);
    poll.erase();
    for (Operation *user : clear.getUsers())
      if (isa<cf::CondBranchOp>(user))
        branches.push_back(user);
  }
  if (branches.empty())
    return;
  // Canonicalize the branches on the cleared polls now. The private-storage
  // promotion below proves activation shapes on this same CFG, and a return
  // left behind a constant-false edge is not an exit of the process.
  RewritePatternSet patterns(function.getContext());
  cf::CondBranchOp::getCanonicalizationPatterns(patterns,
                                                function.getContext());
  (void)applyOpPatternsGreedily(branches, std::move(patterns));
  IRRewriter rewriter(function.getContext());
  (void)eraseUnreachableBlocks(rewriter, function.getBody());
}

/// A private subroutine may inherit the scheduler's clear finish latch only
/// when every symbol use is a direct call and every call site proves it clear.
/// Propagate this must fact along the call graph; unresolved uses and recursive
/// cycles without an established entry proof retain their polls.
DenseSet<Operation *> getClearTerminationEntries(
    sim::SimDesignOp design, const analysis::SimulationEffectAnalysis &effects,
    const analysis::HandleDataflowAnalysis &provenance, bool closedCalls) {
  DenseSet<Operation *> clearEntries;
  SmallVector<sim::SimFuncOp> pending;
  DenseMap<Operation *, SmallVector<sim::SimCallOp>> callsTo;
  DenseMap<Operation *, sim::SimFuncOp> targets;
  SymbolTableCollection symbols;
  // Check that symbol traversal is complete before using the indexed users.
  closedCalls &= bool(SymbolTable::getSymbolUses(design));
  SymbolUserMap users(symbols, design);
  for (auto function : design.getOps<sim::SimFuncOp>()) {
    if (startsWithClearTermination(function))
      clearEntries.insert(function);
    if (!closedCalls)
      continue;
    bool hasCalls = false;
    function.walk([&](sim::SimCallOp call) { hasCalls = true; });
    if (hasCalls)
      pending.push_back(function);
    if (!function.isPrivate() || function.isExternal() ||
        function.getEntryKind() != sim::EntryKind::Function)
      continue;
    auto uses = users.getUsers(function);
    if (uses.empty() || llvm::any_of(uses, [&](Operation *use) {
          auto call = dyn_cast<sim::SimCallOp>(use);
          return !call || symbols.lookupNearestSymbolFrom<sim::SimFuncOp>(
                              call, call.getCalleeAttr()) != function;
        }))
      continue;
    for (Operation *use : uses) {
      auto call = cast<sim::SimCallOp>(use);
      callsTo[function].push_back(call);
      targets[call] = function;
    }
  }
  DenseSet<Operation *> clearCalls;
  for (unsigned index = 0; index < pending.size(); ++index) {
    auto function = pending[index];
    auto handles = provenance.derive(function);
    DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
    solver.load<dataflow::DeadCodeAnalysis>();
    solver.load<dataflow::SparseConstantPropagation>();
    solver.load<TerminationDataflow>(effects, handles,
                                     clearEntries.contains(function));
    if (failed(solver.initializeAndRun(function)))
      continue;
    function.walk([&](sim::SimCallOp call) {
      auto target = targets.find(call);
      if (target == targets.end())
        return;
      auto *state = solver.lookupState<TerminationLattice>(
          solver.getProgramPointBefore(call));
      if (!state || !state->reachable || state->mayTerminate)
        return;
      clearCalls.insert(call);
      auto callee = target->second;
      if (!clearEntries.contains(callee) &&
          llvm::all_of(callsTo.lookup(callee), [&](auto incoming) {
            return clearCalls.contains(incoming);
          })) {
        clearEntries.insert(callee);
        pending.push_back(callee);
      }
    });
  }
  return clearEntries;
}
void removeDeadPrivateStores(sim::SimDesignOp design) {
  DenseSet<uint64_t> candidates;
  for (auto storage : design.getOps<sim::SimStorageDeclOp>())
    if (sim::has<sim::metadata::SubroutineStorage>(storage) &&
        isa<IntegerType, sim::LogicType>(storage.getType()))
      candidates.insert(storage.getId());
  if (candidates.empty())
    return;
  SmallVector<std::pair<sim::SimRefStoreOp, uint64_t>> stores;
  bool foreign = false;
  analysis::HandleDataflowAnalysis handles(design);
  SymbolTableCollection symbols;
  for (auto function : design.getOps<sim::SimFuncOp>()) {
    auto facts = handles.derive(function);
    function.walk([&](Operation *op) {
      foreign |= isa<sim::SimDPICallOp>(op);
      if (auto call = dyn_cast<sim::SimCallOp>(op)) {
        auto callee = symbols.lookupSymbolIn<sim::SimFuncOp>(
            design, call.getCalleeAttr());
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
/// IEEE 1800-2023 9.6.2: a named block's activation only matters to a
/// `disable` statement that names it. Foreign code has no disable primitive;
/// it reaches SystemVerilog only through exported subroutines (35.7, 35.8),
/// whose disable statements are part of the design. An enter/leave pair with
/// no boundary and no disable of its target anywhere in the design therefore
/// has no user-visible effect (4.3). Unrolled named loop bodies leave many of
/// them in combinational processes, where they are the only scheduler
/// interaction.
/// A process with deferred assertions keeps every activation: their pending
/// reports belong to the innermost activation (16.4.1), which the runtime
/// tracks through these entries.
void removeUnusedControlActivations(sim::SimDesignOp design) {
  DenseSet<uint64_t> targets;
  design.walk([&](Operation *operation) {
    if (auto disable = dyn_cast<sim::SimControlDisableOp>(operation))
      targets.insert(disable.getTargetId());
    else if (auto exit = dyn_cast<sim::SimControlNonlocalExitOp>(operation))
      targets.insert(exit.getTargetId());
  });
  SmallVector<sim::SimControlEnterOp> unused;
  for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
    bool deferred = function
                        .walk([](sim::SimDeferredEnqueueOp) {
                          return WalkResult::interrupt();
                        })
                        .wasInterrupted();
    if (deferred)
      continue;
    function.walk([&](sim::SimControlEnterOp enter) {
      if (!targets.contains(enter.getTargetId()) &&
          llvm::all_of(enter.getControl().getUsers(), [](Operation *user) {
            return isa<sim::SimControlLeaveOp>(user);
          }))
        unused.push_back(enter);
    });
  }
  for (sim::SimControlEnterOp enter : unused) {
    for (Operation *leave :
         llvm::make_early_inc_range(enter.getControl().getUsers()))
      leave->erase();
    enter.erase();
  }
}
void promotePrivateAggregates(sim::SimDesignOp design) {
  analysis::PrivateStorageAnalysis storage(design);
  analysis::HandleDataflowAnalysis handles(design);
  analysis::SimulationEffectAnalysis effects(design);
  SymbolTable symbols(design);
  DenseMap<Operation *, std::unique_ptr<analysis::ActivationRegionAnalysis>>
      activations;
  DenseMap<Operation *, bool> allowed;
  for (const auto &[id, access] : storage.getRoots()) {
    if (!access.owner || !access.promotableViews || access.bases.empty() ||
        access.stores.size() < 2)
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
                 if (auto call = dyn_cast<sim::SimCallOp>(op)) {
                   auto callee =
                       symbols.lookup<sim::SimFuncOp>(call.getCallee());
                   return callee && effects.isHarmless(callee)
                              ? WalkResult::advance()
                              : WalkResult::interrupt();
                 }
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
    // IEEE 1800-2023 6.8/6.11 and 4.3: an exclusive lane with no write keeps
    // its default value. If every other lane is defined before each read in
    // equivalent initial/resumed activations, a fresh default slot recreates
    // all observable values. In particular, aggregate copy-out must not turn
    // an unwritten field into artificial state carried between activations.
    bool defaultSlot = false;
    analysis::ActivationMemoryAnalysis::Ranges written;
    auto observability = declaration.getObservability();
    if (function.getEntryKind() == sim::EntryKind::AlwaysComb &&
        llvm::none_of(access.stores,
                      [&, &access = access](auto store) {
                        return llvm::is_contained(access.bases,
                                                  store.getReference());
                      }) &&
        (!observability ||
         *observability == schedule::ComputeObservabilityKind::Invisible)) {
      auto [found, fresh] = activations.try_emplace(function);
      if (fresh)
        found->second = std::make_unique<analysis::ActivationRegionAnalysis>(
            function, handles);
      if (found->second->isValid()) {
        const auto &memory = found->second->getMemory();
        for (const auto &range : memory.getMayWrites())
          if (range.root == id)
            written.push_back(range);
        defaultSlot =
            !written.empty() &&
            llvm::none_of(memory.getLiveIns(), [&, &id = id](const auto &read) {
              return read.root == id && llvm::any_of(written, [&](auto write) {
                       return read.low < write.low + write.width &&
                              write.low < read.low + read.width;
                     });
            });
      }
    }
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
    Value initial;
    if (defaultSlot) {
      if (sim::isAggregateType(type))
        initial = sim::SimAggregateDefaultOp::create(builder, function.getLoc(),
                                                     type);
      else if (isa<IntegerType>(type))
        initial = arith::ConstantOp::create(builder, function.getLoc(), type,
                                            builder.getIntegerAttr(type, 0));
      else if (auto logic = dyn_cast<sim::LogicType>(type)) {
        auto plane = builder.getIntegerType(logic.getWidth());
        initial = sim::SimLogicConstantOp::create(
            builder, function.getLoc(), type,
            builder.getIntegerAttr(plane, APInt(logic.getWidth(), 0)),
            builder.getIntegerAttr(plane, APInt::getAllOnes(logic.getWidth())));
      }
    }
    if (!initial) {
      defaultSlot = false;
      initial = sim::SimRefLoadOp::create(builder, function.getLoc(), type,
                                          canonical);
    }
    auto local =
        sim::SimRefAllocOp::create(builder, function.getLoc(), ref, initial);
    // The slot is flushed to its exclusive canonical root at every boundary,
    // so reloading the root at any resumption recreates the same contents.
    sim::mark<sim::metadata::FlushedPrivateSlot>(local);
    if (defaultSlot)
      sim::mark<sim::metadata::ActivationDefaultSlot>(local);
    for (OpOperand *use : originalUses)
      use->set(local);
    for (Operation *boundary : boundaries) {
      builder.setInsertionPoint(boundary);
      if (defaultSlot) {
        // Flush only subtrees that contain a source write. The untouched
        // fields retain their canonical defaults and create no publications.
        auto flush = [&](Value slot, Value root, Type type, uint64_t low,
                         auto &flush) -> void {
          auto span = sim::getProvenanceSpan(type);
          if (!span || llvm::none_of(written, [&](auto range) {
                return low < range.low + range.width && range.low < low + *span;
              }))
            return;
          bool covered = llvm::any_of(written, [&](auto range) {
            return range.low <= low && low + *span <= range.low + range.width;
          });
          if (!covered &&
              isa<sim::UnpackedArrayType, sim::UnpackedStructType,
                  sim::PackedArrayType, sim::PackedStructType>(type)) {
            for (unsigned index = 0, count = sim::getAggregateNumElements(type);
                 index < count; ++index) {
              auto child = sim::getAggregateProvenanceSubelement(type, index);
              Type childType = sim::getAggregateElementType(type, index);
              assert(child && "promotable aggregate has a fixed layout");
              auto project = [&](Value reference) -> Value {
                return sim::SimRefSubelementOp::create(
                    builder, boundary->getLoc(),
                    sim::RefType::get(design.getContext(), childType),
                    reference, builder.getDenseI64ArrayAttr({int64_t(index)}));
              };
              flush(project(slot), project(root), childType, low + child->first,
                    flush);
            }
            return;
          }
          Value value = sim::SimRefLoadOp::create(builder, boundary->getLoc(),
                                                  type, slot);
          sim::SimRefStoreOp::create(builder, boundary->getLoc(), value, root);
        };
        flush(local, canonical, type, 0, flush);
        continue;
      }
      Value value =
          sim::SimRefLoadOp::create(builder, boundary->getLoc(), type, local);
      auto flush = sim::SimRefStoreOp::create(builder, boundary->getLoc(),
                                              value, canonical);
      if (llvm::all_of(access.stores, [](auto store) {
            return sim::has<sim::metadata::ContinuousStore>(store);
          }))
        sim::mark<sim::metadata::ContinuousStore>(flush);
    }
    // Other instances of the same source process may run while suspended.
    // Reload canonical state before any resumed access; private SSA never
    // carries an equality or ownership assumption across that boundary.
    for (Block *block : resumptions) {
      builder.setInsertionPointToStart(block);
      // Reset certified scratch explicitly. Besides making the lifetime
      // visible to mem2reg, this prevents an aggregate insert loop from
      // acquiring a spurious previous-activation SSA input.
      Value value = defaultSlot
                        ? initial
                        : sim::SimRefLoadOp::create(builder, function.getLoc(),
                                                    type, canonical);
      sim::SimRefStoreOp::create(builder, function.getLoc(), value, local);
    }
  }
}
struct TalosSimSimplifyBodiesPass
    : impl::TalosSimSimplifyBodiesPassBase<TalosSimSimplifyBodiesPass> {
  using TalosSimSimplifyBodiesPassBase::TalosSimSimplifyBodiesPassBase;
  void runOnOperation() override {
    auto design = getOperation();
    if (vpi != "off" && vpi != "read" && vpi != "full") {
      design.emitError("invalid VPI body simplification contract");
      return signalPassFailure();
    }
    analysis::SimulationEffectAnalysis effects(design);
    analysis::HandleDataflowAnalysis provenance(design);
    auto clearEntries =
        getClearTerminationEntries(design, effects, provenance, vpi == "off");
    for (auto function : design.getBody().front().getOps<sim::SimFuncOp>())
      simplifyTerminationPolls(function, effects, provenance,
                               clearEntries.contains(function));
    bool canonicalWrites = vpi == "off";
    design.walk([&](Operation *op) {
      if (isa<sim::SimOverrideOp, sim::SimDynamicOverrideOp,
              sim::SimReleaseOverrideOp>(op))
        canonicalWrites = false;
    });
    if (canonicalWrites) {
      promotePrivateReductions(design);
      promotePrivateAggregates(design);
      removeUnusedControlActivations(design);
      simlowering::removeEffectFreeProcesses(design);
    }
    // Promotion and forwarding rewrite storage and function bodies, never
    // drivers or nets, so the driver index above still serves every function.
    // They add or remove no symbols, so call resolution shares one table.
    SymbolTableCollection symbols;
    for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
      analysis::forwardSourceMemoryLoads(function, canonicalWrites,
                                         /*canForward=*/{}, &provenance);
      LogicBitAnalysis bits(function);
      analysis::DemandedBitAnalysis demanded(function, symbols);
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
    if (canonicalWrites)
      removeDeadPrivateStores(design);
  }
};
} // namespace
} // namespace talos
