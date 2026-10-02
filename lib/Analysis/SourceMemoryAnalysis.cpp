#include "obelisk/Analysis/SourceMemoryAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk::analysis {
namespace {
// Identity distinguishes two exact dynamically selected addresses whose
// may-access hulls overlap. The hull is used for kills, never for equality.
using Range = std::tuple<Value, uint64_t, uint64_t, uint64_t, Type, unsigned>;
using Memory = DenseMap<Range, Value>;
class MemoryLattice final : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  bool reachable = false;
  Memory values;
  ChangeResult merge(const Memory &incoming) {
    if (!reachable) {
      reachable = true;
      values = incoming;
      return ChangeResult::Change;
    }
    SmallVector<Range> remove;
    for (const auto &entry : values)
      if (incoming.lookup(entry.first) != entry.second)
        remove.push_back(entry.first);
    for (const auto &key : remove)
      values.erase(key);
    return remove.empty() ? ChangeResult::NoChange : ChangeResult::Change;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const MemoryLattice &>(rhs);
    return other.reachable ? merge(other.values) : ChangeResult::NoChange;
  }
  void print(raw_ostream &os) const override { os << values.size(); }
};
class MemoryDataflow final
    : public dataflow::DenseForwardDataFlowAnalysis<MemoryLattice> {
public:
  MemoryDataflow(DataFlowSolver &solver, const DenseMap<Value, Range> &ranges,
                 bool canonicalWrites)
      : DenseForwardDataFlowAnalysis(solver), ranges(ranges),
        canonicalWrites(canonicalWrites) {}
  LogicalResult visitOperation(Operation *op, const MemoryLattice &before,
                               MemoryLattice *after) override {
    if (!before.reachable)
      return success();
    Memory next = before.values;
    if (auto load = dyn_cast<sim::SimRefLoadOp>(op)) {
      if (auto key = ranges.find(load.getReference()); key != ranges.end())
        next.try_emplace(key->second, load.getResult());
    } else if (auto store = dyn_cast<sim::SimRefStoreOp>(op)) {
      auto key = ranges.find(store.getReference());
      if (key == ranges.end()) {
        next.clear();
      } else {
        auto [identity, root, low, width, type, local] = key->second;
        SmallVector<Range> remove;
        for (const auto &entry : next) {
          auto [otherIdentity, otherRoot, otherLow, otherWidth, otherType,
                otherLocal] = entry.first;
          bool sameRoot = local ? otherLocal && identity == otherIdentity
                                : !otherLocal && root == otherRoot;
          if ((!canonicalWrites && !local) ||
              (sameRoot && low < otherLow + otherWidth &&
               otherLow < low + width))
            remove.push_back(entry.first);
        }
        for (const auto &range : remove)
          next.erase(range);
        if (local || canonicalWrites)
          next[key->second] = store.getValue();
      }
    } else if (auto allocation = dyn_cast<sim::SimRefAllocOp>(op)) {
      if (auto key = ranges.find(allocation.getResult()); key != ranges.end())
        next[key->second] = allocation.getInitialValue();
    } else if (isa<cf::BranchOp, cf::CondBranchOp>(op)) {
      // Dense CFG propagation intersects snapshots on every incoming edge.
    } else if (!isMemoryEffectFree(op)) {
      next.clear();
    }
    propagateIfChanged(after, after->merge(next));
    return success();
  }
  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction,
                                    const MemoryLattice &before,
                                    MemoryLattice *after) override {
    (void)visitOperation(call, before, after);
  }
  void visitBlockTransfer(Block *, ProgramPoint *, Block *,
                          const MemoryLattice &before,
                          MemoryLattice *after) override {
    if (!before.reachable)
      return;
    Memory next = before.values;
    // A loop argument denotes a new selector value on each iteration. Keep
    // dynamically addressed snapshots local to one block until an invariant
    // address certificate proves that an edge retains the same binding.
    SmallVector<Range> remove;
    for (const auto &entry : next)
      if (std::get<0>(entry.first) && !std::get<5>(entry.first))
        remove.push_back(entry.first);
    for (const auto &key : remove)
      next.erase(key);
    propagateIfChanged(after, after->merge(next));
  }

private:
  void setToEntryState(MemoryLattice *state) override {
    propagateIfChanged(state, state->merge(Memory{}));
  }
  const DenseMap<Value, Range> &ranges;
  bool canonicalWrites;
};
} // namespace
SourceMemoryAnalysis::SourceMemoryAnalysis(sim::SimFuncOp function,
                                           bool canonicalWrites) {
  auto handles = HandleDataflowAnalysis(function).analyze(function);
  DenseMap<Value, Range> ranges;
  function.walk([&](Operation *op) {
    Value reference;
    Type type;
    if (auto load = dyn_cast<sim::SimRefLoadOp>(op)) {
      reference = load.getReference();
      type = load.getType();
    } else if (auto store = dyn_cast<sim::SimRefStoreOp>(op)) {
      reference = store.getReference();
      type = store.getValue().getType();
    } else
      return;
    if (reference.getDefiningOp<sim::SimRefAllocOp>()) {
      // An exact local slot, never an invalid or partially clipped selector.
      ranges.try_emplace(reference, reference, 0, 0, 1, type, true);
      return;
    }
    auto fact = handles.facts.find(reference);
    auto certificate = handles.certificates.find(reference);
    if (fact == handles.facts.end() || !fact->second.descriptor ||
        fact->second.resource != schedule::ComputeResourceKind::Storage ||
        certificate == handles.certificates.end() ||
        certificate->second.clipped)
      return;
    const auto &range = fact->second;
    uint64_t width = range.width ? range.width : range.rootWidth;
    auto actualWidth = sim::getProvenanceSpan(reference.getType());
    if (width && range.low <= range.rootWidth &&
        width <= range.rootWidth - range.low && actualWidth &&
        ((!range.dynamic && width == *actualWidth) ||
         (range.dynamic && certificate->second.inBounds &&
          *actualWidth <= width)))
      ranges.try_emplace(reference, range.dynamic ? reference : Value{},
                         *range.descriptor, range.low, width, type, false);
  });
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<MemoryDataflow>(ranges, canonicalWrites);
  if (failed(solver.initializeAndRun(function)))
    return;
  function.walk([&](sim::SimRefLoadOp load) {
    auto key = ranges.find(load.getReference());
    const auto *before =
        solver.lookupState<MemoryLattice>(solver.getProgramPointBefore(load));
    if (key != ranges.end() && before && before->reachable)
      if (Value previous = before->values.lookup(key->second);
          previous && previous != load.getResult())
        forwarded.try_emplace(load, previous);
    // A whole-array snapshot is also a relational certificate: distinct
    // physical arrays assigned that snapshot have equal contents in this
    // scope. Share the SSA value, never either array's address or identity.
    // Any possible overlapping write has already killed the whole snapshot.
    auto element =
        load.getReference().getDefiningOp<sim::SimRefArrayElementOp>();
    if (!element || !before || !before->reachable)
      return;
    auto parent = handles.facts.find(element.getInput());
    auto proof = handles.certificates.find(element.getInput());
    auto parentType = element.getInput().getType().getElementType();
    auto parentWidth = sim::getProvenanceSpan(parentType);
    if (!isa<sim::UnpackedArrayType>(parentType) || !parentWidth ||
        parent == handles.facts.end() || parent->second.dynamic ||
        !parent->second.descriptor || proof == handles.certificates.end() ||
        !proof->second.inBounds || proof->second.clipped ||
        parent->second.width != *parentWidth)
      return;
    auto &range = parent->second;
    Range whole{Value{},      *range.descriptor, range.low,
                *parentWidth, parentType,        false};
    if (Value snapshot = before->values.lookup(whole))
      elements.try_emplace(load, ElementSnapshot{snapshot, element.getIndex()});
  });
}
} // namespace obelisk::analysis
