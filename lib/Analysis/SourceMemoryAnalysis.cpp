#include "talos/Analysis/SourceMemoryAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "talos/Analysis/HandleDataflowAnalysis.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace talos::analysis {
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
                 bool canonicalWrites, DominanceInfo &dominance)
      : DenseForwardDataFlowAnalysis(solver), ranges(ranges),
        canonicalWrites(canonicalWrites), dominance(dominance) {}
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
  void visitBlockTransfer(Block *block, ProgramPoint *, Block *,
                          const MemoryLattice &before,
                          MemoryLattice *after) override {
    if (!before.reachable)
      return;
    Memory next = before.values;
    // A dominating reference keeps its exact SSA address binding through
    // branches and inner loops. Entering its defining block can recompute the
    // reference (or rebind a block argument), so discard that snapshot there.
    // Bounds prove validity, not identity across a loop backedge (LRM 7.4.5).
    SmallVector<Range> remove;
    for (const auto &entry : next)
      if (Value identity = std::get<0>(entry.first);
          identity && !std::get<5>(entry.first)) {
        Block *binding = identity.getParentBlock();
        if (binding == block || !dominance.dominates(binding, block))
          remove.push_back(entry.first);
      }
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
  DominanceInfo &dominance;
};
} // namespace
SourceMemoryAnalysis::SourceMemoryAnalysis(
    sim::SimFuncOp function, bool canonicalWrites,
    const HandleDataflowAnalysis *designHandles) {
  auto handles = designHandles
                     ? designHandles->analyze(function)
                     : HandleDataflowAnalysis(function).analyze(function);
  DenseMap<Value, Range> ranges;
  auto collect = [&](Value reference) {
    Type type = cast<sim::RefType>(reference.getType()).getElementType();
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
  };
  function.walk([&](Operation *op) {
    Value reference;
    if (auto load = dyn_cast<sim::SimRefLoadOp>(op))
      reference = load.getReference();
    else if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
      reference = store.getReference();
    while (reference) {
      collect(reference);
      Operation *view = reference.getDefiningOp();
      reference =
          isa_and_nonnull<sim::SimRefSubelementOp, sim::SimRefArrayElementOp,
                          sim::SimRefExtractOp, sim::SimRefDynExtractOp>(view)
              ? view->getOperand(0)
              : Value{};
    }
  });
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  DominanceInfo dominance(function);
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<MemoryDataflow>(ranges, canonicalWrites, dominance);
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
    if (before && before->reachable) {
      Value parent;
      OpFoldResult offset;
      if (auto slice =
              load.getReference().getDefiningOp<sim::SimRefExtractOp>()) {
        parent = slice.getInput();
        offset = slice.getLowBitAttr();
      } else if (auto slice = load.getReference()
                                  .getDefiningOp<sim::SimRefDynExtractOp>()) {
        parent = slice.getInput();
        offset = slice.getLowBit();
      }
      if (parent && sim::getPackedScalarType(
                        cast<sim::RefType>(parent.getType()).getElementType()))
        if (auto key = ranges.find(parent); key != ranges.end())
          if (Value snapshot = before->values.lookup(key->second))
            slices.try_emplace(load, SliceSnapshot{snapshot, offset});
    }
    // A field read may use a dominating whole-aggregate store even when the
    // aggregate address is dynamically selected. Match the parent's exact
    // address identity, never its may-alias hull. Intervening overlapping
    // writes and address rebinding have already killed that snapshot.
    if (before && before->reachable) {
      Value reference = load.getReference();
      SmallVector<int64_t> indices;
      while (auto field = reference.getDefiningOp<sim::SimRefSubelementOp>()) {
        Type type = field.getInput().getType().getElementType();
        bool representable = true;
        for (int64_t index : field.getIndices()) {
          auto packed = dyn_cast<sim::PackedUnionType>(type);
          if (isa<sim::UnpackedUnionType>(type) ||
              (packed && packed.getIsTagged())) {
            representable = false;
            break;
          }
          type = sim::getAggregateElementType(type, index);
        }
        // Tagged union reference access must retain its active-member check
        // (IEEE 1800-2023 7.3.2); this snapshot handles raw packed/array lanes.
        if (!representable)
          break;
        indices.insert(indices.begin(), field.getIndices().begin(),
                       field.getIndices().end());
        reference = field.getInput();
        auto parent = ranges.find(reference);
        if (parent == ranges.end())
          continue;
        if (Value snapshot = before->values.lookup(parent->second)) {
          subelements.try_emplace(load, SubelementSnapshot{snapshot, indices});
          break;
        }
      }
    }
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
uint64_t
forwardSourceMemoryLoads(sim::SimFuncOp function, bool canonicalWrites,
                         llvm::function_ref<bool(Operation *)> canForward,
                         const HandleDataflowAnalysis *designHandles) {
  SourceMemoryAnalysis memory(function, canonicalWrites, designHandles);
  DominanceInfo dominance(function);
  SmallVector<sim::SimRefLoadOp> loads;
  function.walk([&](sim::SimRefLoadOp load) { loads.push_back(load); });
  DenseMap<Value, Value> replacements;
  SmallVector<sim::SimRefLoadOp> forwarded;
  auto resolve = [&](Value value) {
    while (value && replacements.contains(value))
      value = replacements.lookup(value);
    return value;
  };
  for (auto load : loads) {
    if (canForward && !canForward(load))
      continue;
    Value previous = resolve(memory.getForwarded(load));
    OpBuilder builder(load);
    if (auto field = memory.getSubelementSnapshot(load)) {
      Value aggregate = resolve(field->aggregate);
      if (dominance.dominates(aggregate, load)) {
        previous = aggregate;
        for (int64_t index : field->indices) {
          Type type = sim::getAggregateElementType(previous.getType(), index);
          previous = isa<sim::PackedUnionType>(previous.getType())
                         ? sim::SimUnionExtractOp::create(
                               builder, load.getLoc(), type, previous, index)
                               .getResult()
                         : sim::SimAggregateExtractOp::create(
                               builder, load.getLoc(), type, previous, index)
                               .getResult();
        }
      }
    } else if (auto element = memory.getElementSnapshot(load)) {
      Value aggregate = resolve(element->aggregate);
      if (dominance.dominates(aggregate, load))
        previous = sim::SimArrayDynExtractOp::create(builder, load.getLoc(),
                                                     load.getType(), aggregate,
                                                     resolve(element->index));
    } else if (auto slice = memory.getSliceSnapshot(load)) {
      Value aggregate = resolve(slice->aggregate);
      if (dominance.dominates(aggregate, load)) {
        Type scalar = sim::getPackedScalarType(aggregate.getType());
        if (aggregate.getType() != scalar)
          aggregate = sim::SimPackedFlattenOp::create(builder, load.getLoc(),
                                                      scalar, aggregate);
        Value offset = resolve(dyn_cast<Value>(slice->offset));
        if (!offset) {
          auto constant = cast<IntegerAttr>(cast<Attribute>(slice->offset));
          offset = arith::ConstantOp::create(builder, load.getLoc(), constant);
        }
        Type result = sim::getPackedScalarType(load.getType());
        unsigned width = *sim::getPackedWidth(result);
        Type physical =
            isa<sim::LogicType>(scalar)
                ? Type(sim::LogicType::get(function.getContext(), width))
                : Type(builder.getIntegerType(width));
        if (isa<sim::LogicType>(scalar))
          previous = sim::SimLogicDynExtractOp::create(
              builder, load.getLoc(), physical, aggregate, offset);
        else
          previous = sim::SimBitsDynExtractOp::create(
              builder, load.getLoc(), physical, aggregate, offset);
        if (physical != result) {
          if (isa<sim::LogicType>(physical))
            previous = sim::SimLogicToBitsOp::create(builder, load.getLoc(),
                                                     result, previous);
          else
            previous = sim::SimLogicFromBitsOp::create(builder, load.getLoc(),
                                                       result, previous);
        }
        if (load.getType() != result)
          previous = sim::SimPackedUnflattenOp::create(
              builder, load.getLoc(), load.getType(), previous);
      }
    }
    if (!previous || previous.getType() != load.getType() ||
        !dominance.dominates(previous, load))
      continue;
    replacements.try_emplace(load.getResult(), previous);
    load.replaceAllUsesWith(previous);
    forwarded.push_back(load);
  }
  // The snapshot facts and replacement keys name load results. Erasing a load
  // inside the loop lets a newly built extract reuse its storage, and resolve
  // would then follow the stale entry to an unrelated value.
  for (auto load : forwarded)
    load.erase();
  return forwarded.size();
}
} // namespace talos::analysis
