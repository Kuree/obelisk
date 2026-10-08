//===- LocalStorageAnalysis.cpp - Private local storage -------------------===//

#include "obelisk/Analysis/LocalStorageAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/ViewLikeInterface.h"
#include "obelisk/Analysis/SSAValueAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
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
} // namespace

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
