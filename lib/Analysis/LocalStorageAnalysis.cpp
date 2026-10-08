//===- LocalStorageAnalysis.cpp - Private local storage -------------------===//

#include "obelisk/Analysis/LocalStorageAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Interfaces/ViewLikeInterface.h"
#include "obelisk/Analysis/SSAValueAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SetVector.h"

using namespace mlir;
namespace obelisk::analysis {

LocalStorageAnalysis::LocalStorageAnalysis(sim::SimFuncOp function,
                                           Lifetime lifetime)
    : function(function) {
  if (function.isExternal() || (lifetime == Lifetime::Invocation &&
                                function
                                    .walk([](Operation *op) {
                                      return sim::isSuspensionOp(op)
                                                 ? WalkResult::interrupt()
                                                 : WalkResult::advance();
                                    })
                                    .wasInterrupted()))
    return;
  SmallVector<sim::SimRefAllocOp> allocations;
  function.walk([&](sim::SimRefAllocOp allocation) {
    allocations.push_back(allocation);
  });
  if (allocations.empty())
    return;
  SemanticValueRootAnalysis forwarding(function);
  for (sim::SimRefAllocOp allocation : allocations) {
    auto analyze = [&] {
      Type type = allocation.getInitialValue().getType();
      auto width = sim::getProvenanceSpan(type);
      SmallVector<sim::ManagedHandleSlot> managed;
      if (!width || !*width || *width > UINT_MAX ||
          !sim::getManagedHandleSlots(type, managed) || !managed.empty())
        return;
      // Bound stack growth independently of the target's register width.
      if (*width > 32768)
        return;
      llvm::SetVector<Value> family;
      family.insert(allocation.getResult());
      for (size_t index = 0; index != family.size(); ++index) {
        Value reference = family[index];
        for (OpOperand &use : reference.getUses()) {
          Operation *user = use.getOwner();
          if (user->getParentOfType<sim::SimFuncOp>() != function)
            return;
          if (auto load = dyn_cast<sim::SimRefLoadOp>(user)) {
            if (load.getReference() == reference)
              continue;
          } else if (auto store = dyn_cast<sim::SimRefStoreOp>(user)) {
            if (store.getReference() == reference)
              continue;
          } else if (isa<sim::SimRefExtractOp, schedule::NativeWordViewOp,
                         sim::SimRefSubelementOp, sim::SimRefArrayElementOp>(
                         user)) {
            auto view = cast<ViewLikeOpInterface>(user);
            if (view.getViewSource() != reference)
              return;
            // Tagged unions need active-member checks. Partial dynamic bit
            // selections need clipping; neither may become an unchecked GEP.
            Type parent =
                cast<sim::RefType>(reference.getType()).getElementType();
            if (auto field = dyn_cast<sim::SimRefSubelementOp>(user))
              for (int64_t ordinal : field.getIndices()) {
                if (isa<sim::UnpackedUnionType>(parent))
                  return;
                if (auto u = dyn_cast<sim::PackedUnionType>(parent);
                    u && u.getIsTagged())
                  return;
                parent = sim::getAggregateElementType(parent, ordinal);
              }
            family.insert(user->getResult(0));
            continue;
          } else if (auto branch = dyn_cast<BranchOpInterface>(user)) {
            if (!branch.getSuccessorBlockArgument(use.getOperandNumber()))
              return;
            bool forwarded = false;
            for (unsigned edge = 0; edge != user->getNumSuccessors(); ++edge) {
              auto operands = branch.getSuccessorOperands(edge);
              for (BlockArgument argument :
                   user->getSuccessor(edge)->getArguments()) {
                unsigned ordinal = argument.getArgNumber();
                if (operands.isOperandProduced(ordinal) ||
                    operands[ordinal] != reference)
                  continue;
                if (forwarding.lookup(argument) != forwarding.lookup(reference))
                  return;
                family.insert(argument);
                forwarded = true;
              }
            }
            if (forwarded)
              continue;
          }
          // Calls, captures, ownership transfers, reference PHIs and unknown
          // accessors retain the existing runtime lifetime implementation.
          return;
        }
      }
      for (Value reference : family)
        references.try_emplace(reference, allocation);
    };
    analyze();
  }
}
namespace {
using Definitions = llvm::DenseSet<Value>;
class InitializationLattice final : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  bool reachable = false;
  Definitions initialized;
  ChangeResult merge(const Definitions &incoming) {
    if (!reachable) {
      reachable = true;
      initialized = incoming;
      return ChangeResult::Change;
    }
    SmallVector<Value> removed;
    for (Value value : initialized)
      if (!incoming.contains(value))
        removed.push_back(value);
    for (Value value : removed)
      initialized.erase(value);
    return removed.empty() ? ChangeResult::NoChange : ChangeResult::Change;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const InitializationLattice &>(rhs);
    return other.reachable ? merge(other.initialized) : ChangeResult::NoChange;
  }
  void print(raw_ostream &os) const override { os << initialized.size(); }
};
class InitializationDataflow final
    : public dataflow::DenseForwardDataFlowAnalysis<InitializationLattice> {
public:
  InitializationDataflow(DataFlowSolver &solver, const Definitions &recreated,
                         const SemanticValueRootAnalysis &roots)
      : DenseForwardDataFlowAnalysis(solver), recreated(recreated),
        roots(roots) {}
  LogicalResult visitOperation(Operation *operation,
                               const InitializationLattice &before,
                               InitializationLattice *after) override {
    if (!before.reachable)
      return success();
    Definitions next = before.initialized;
    if (auto store = dyn_cast<sim::SimRefStoreOp>(operation)) {
      Value root = roots.lookup(store.getReference());
      if (recreated.contains(root))
        next.insert(root);
    } else if (auto allocation = dyn_cast<sim::SimRefAllocOp>(operation)) {
      // A recreated allocation's initializer has no relation to its previous
      // activation. In a loop each new allocation also starts a new lifetime.
      next.erase(allocation.getResult());
    }
    propagateIfChanged(after, after->merge(next));
    return success();
  }
  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction,
                                    const InitializationLattice &before,
                                    InitializationLattice *after) override {
    // Closed reference families cannot be accessed by a callee.
    (void)visitOperation(call, before, after);
  }

private:
  void setToEntryState(InitializationLattice *state) override {
    propagateIfChanged(state, state->merge(Definitions{}));
  }
  const Definitions &recreated;
  const SemanticValueRootAnalysis &roots;
};

// Each bit names the memory version observed by a load. A version remains
// available only if every executable path preserves its backing storage.
// Testing availability at uses is sufficient; a write after the last use
// cannot create a read-after-write conflict and needs no snapshot.
class SnapshotLattice final : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  bool reachable = false;
  llvm::BitVector available;
  ChangeResult merge(const llvm::BitVector &incoming) {
    if (!reachable) {
      reachable = true;
      available = incoming;
      return ChangeResult::Change;
    }
    llvm::BitVector next = available;
    next &= incoming;
    if (next == available)
      return ChangeResult::NoChange;
    available = std::move(next);
    return ChangeResult::Change;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const SnapshotLattice &>(rhs);
    return other.reachable ? merge(other.available) : ChangeResult::NoChange;
  }
  void print(raw_ostream &os) const override { os << available.count(); }
};
class SnapshotDataflow final
    : public dataflow::DenseForwardDataFlowAnalysis<SnapshotLattice> {
public:
  SnapshotDataflow(DataFlowSolver &solver, ArrayRef<sim::SimRefLoadOp> loads,
                   const LocalStorageAnalysis &locals)
      : DenseForwardDataFlowAnalysis(solver), locals(locals) {
    for (auto [index, operation] : llvm::enumerate(loads)) {
      auto load = operation;
      definitions.try_emplace(load, index);
      sources.push_back(locals.lookup(load.getReference()));
    }
  }
  LogicalResult visitOperation(Operation *op, const SnapshotLattice &before,
                               SnapshotLattice *after) override {
    if (!before.reachable)
      return success();
    llvm::BitVector next = before.available;
    next.reset(getKills(op));
    if (auto definition = definitions.find(op); definition != definitions.end())
      next.set(definition->second);
    propagateIfChanged(after, after->merge(next));
    return success();
  }
  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction,
                                    const SnapshotLattice &before,
                                    SnapshotLattice *after) override {
    (void)visitOperation(call, before, after);
  }
  void visitRegionBranchControlFlowTransfer(RegionBranchOpInterface branch,
                                            std::optional<unsigned>,
                                            std::optional<unsigned>,
                                            const SnapshotLattice &before,
                                            SnapshotLattice *after) override {
    // A region operation's own effects may occur on entry or exit. Nested
    // effects are propagated independently through the region interfaces.
    (void)visitOperation(branch, before, after);
  }
  const llvm::BitVector &getKills(Operation *op) {
    auto [entry, inserted] = kills.try_emplace(op, sources.size());
    if (!inserted)
      return entry->second;
    auto &result = entry->second;
    if (op->getNumRegions() && !isa<RegionBranchOpInterface>(op)) {
      // Without a region control-flow contract, even a private reference can
      // be captured and modified by the opaque body.
      result.set();
      return result;
    }
    auto invalidate = [&](Value reference) {
      auto target = reference ? locals.lookup(reference) : sim::SimRefAllocOp{};
      for (auto [index, source] : llvm::enumerate(sources))
        if (source ? source == target : !target)
          result.set(index);
    };
    SmallVector<MemoryEffects::EffectInstance> effects;
    if (auto interface = dyn_cast<MemoryEffectOpInterface>(op)) {
      interface.getEffects(effects);
    } else if (op->hasTrait<OpTrait::HasRecursiveMemoryEffects>() &&
               isa<RegionBranchOpInterface>(op)) {
      // Region interfaces propagate the effects of the nested operations.
      return result;
    } else if (isMemoryEffectFree(op)) {
      return result;
    } else {
      // Closed private families cannot be reached by unknown callees.
      invalidate({});
      return result;
    }
    for (const auto &effect : effects) {
      if (isa<MemoryEffects::Write, MemoryEffects::Free>(effect.getEffect()))
        invalidate(effect.getValue());
      else if (isa<MemoryEffects::Allocate>(effect.getEffect())) {
        // Re-entering an allocation in a loop begins a new storage lifetime.
        if (Value value = effect.getValue(); value && locals.lookup(value))
          invalidate(value);
      }
    }
    return result;
  }

private:
  void setToEntryState(SnapshotLattice *state) override {
    propagateIfChanged(state, state->merge(llvm::BitVector(sources.size())));
  }
  const LocalStorageAnalysis &locals;
  SmallVector<sim::SimRefAllocOp> sources;
  DenseMap<Operation *, unsigned> definitions;
  DenseMap<Operation *, llvm::BitVector> kills;
};
} // namespace

DenseSet<Value> LocalStorageAnalysis::getBorrowableLoads(
    ArrayRef<sim::SimRefLoadOp> loads) const {
  DenseSet<Value> borrowed;
  if (loads.empty())
    return borrowed;
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  auto *analysis = solver.load<SnapshotDataflow>(loads, *this);
  if (failed(solver.initializeAndRun(function)))
    return borrowed;
  for (auto [index, operation] : llvm::enumerate(loads)) {
    auto load = operation;
    if (llvm::all_of(load->getUsers(), [&](Operation *user) {
          // Only direct copy consumers can accept borrowed memory; calls,
          // returns and SSA forwarding need a separate ownership contract.
          if (!isa<sim::SimRefAllocOp, sim::SimRefStoreOp>(user))
            return false;
          auto *state = solver.lookupState<SnapshotLattice>(
              solver.getProgramPointBefore(user));
          return state &&
                 (!state->reachable || (state->available.test(index) &&
                                        !analysis->getKills(user).test(index)));
        }))
      borrowed.insert(load.getResult());
  }
  return borrowed;
}

bool LocalStorageAnalysis::areResetBeforeReads(
    ArrayRef<sim::SimRefAllocOp> allocations) const {
  if (allocations.empty())
    return true;
  Definitions recreated;
  for (sim::SimRefAllocOp allocation : allocations) {
    if (!lookup(allocation.getResult()))
      return false;
    recreated.insert(allocation.getResult());
  }
  SemanticValueRootAnalysis roots(function);
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<InitializationDataflow>(recreated, roots);
  if (failed(solver.initializeAndRun(function)))
    return false;
  return !function
              ->walk([&](sim::SimRefLoadOp load) {
                auto allocation = lookup(load.getReference());
                if (!allocation || !recreated.contains(allocation.getResult()))
                  return WalkResult::advance();
                auto *state = solver.lookupState<InitializationLattice>(
                    solver.getProgramPointBefore(load));
                if (!state || (state->reachable && !state->initialized.contains(
                                                       allocation.getResult())))
                  return WalkResult::interrupt();
                return WalkResult::advance();
              })
              .wasInterrupted();
}
} // namespace obelisk::analysis
