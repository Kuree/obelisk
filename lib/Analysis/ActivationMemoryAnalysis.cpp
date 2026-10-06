//===- ActivationMemoryAnalysis.cpp - Context-partitioned memory facts ----===//
#include "obelisk/Analysis/ActivationMemoryAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Analysis/BoundedLoopAnalysis.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
namespace obelisk::analysis {
namespace {
using Range = ActivationMemoryAnalysis::Range;
using Ranges = ActivationMemoryAnalysis::Ranges;
void normalize(Ranges &ranges) {
  llvm::sort(ranges, [](const Range &a, const Range &b) {
    return std::tie(a.root, a.low, a.width) < std::tie(b.root, b.low, b.width);
  });
  Ranges merged;
  for (const Range &r : ranges) {
    if (!r.width)
      continue;
    if (!merged.empty() && merged.back().root == r.root &&
        r.low <= merged.back().low + merged.back().width) {
      auto &last = merged.back();
      last.width = std::max(last.low + last.width, r.low + r.width) - last.low;
    } else
      merged.push_back(r);
  }
  ranges = std::move(merged);
}
void unite(Ranges &target, ArrayRef<Range> incoming) {
  llvm::append_range(target, incoming);
  normalize(target);
}
Ranges intersect(ArrayRef<Range> a, ArrayRef<Range> b) {
  Ranges result;
  for (const Range &x : a)
    for (const Range &y : b) {
      uint64_t low = std::max(x.low, y.low);
      uint64_t high = std::min(x.low + x.width, y.low + y.width);
      if (x.root == y.root && low < high)
        result.push_back({x.root, low, high - low});
    }
  normalize(result);
  return result;
}
Ranges subtract(ArrayRef<Range> a, ArrayRef<Range> b) {
  Ranges result;
  for (const Range &x : a) {
    uint64_t cursor = x.low, end = x.low + x.width;
    for (const Range &y : b) {
      if (x.root != y.root || y.low + y.width <= cursor || y.low >= end)
        continue;
      if (cursor < y.low)
        result.push_back({x.root, cursor, y.low - cursor});
      cursor = std::min(end, std::max(cursor, y.low + y.width));
    }
    if (cursor < end)
      result.push_back({x.root, cursor, end - cursor});
  }
  normalize(result);
  return result;
}
struct Memory {
  DenseMap<Value, Attribute> constants;
  HandleFacts references;
  Ranges assigned, exposed, written, repeated;
  bool operator==(const Memory &other) const {
    return constants == other.constants &&
           references.size() == other.references.size() &&
           llvm::all_of(references,
                        [&](const auto &entry) {
                          auto found = other.references.find(entry.first);
                          return found != other.references.end() &&
                                 entry.second == found->second;
                        }) &&
           assigned == other.assigned && exposed == other.exposed &&
           written == other.written && repeated == other.repeated;
  }
  void join(const Memory &other) {
    SmallVector<Value> forget;
    for (auto [value, constant] : constants)
      if (other.constants.lookup(value) != constant)
        forget.push_back(value);
    for (Value value : forget)
      constants.erase(value);
    forget.clear();
    for (auto &[value, reference] : references) {
      auto found = other.references.find(value);
      if (found == other.references.end() || !(reference == found->second))
        forget.push_back(value);
    }
    // Losing an exact reference falls back to the shared sparse may facts.
    for (Value value : forget)
      references.erase(value);
    assigned = intersect(assigned, other.assigned);
    unite(exposed, other.exposed);
    unite(written, other.written);
    unite(repeated, other.repeated);
  }
};
class MemoryState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  llvm::MapVector<ArrayAttr, Memory> rows;
  ChangeResult join(ArrayAttr context, const Memory &incoming) {
    auto [found, inserted] = rows.try_emplace(context, incoming);
    if (inserted)
      return ChangeResult::Change;
    Memory next = found->second;
    next.join(incoming);
    if (next == found->second)
      return ChangeResult::NoChange;
    found->second = std::move(next);
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override {
    os << "activation partitions=" << rows.size();
  }
};
class ActivationDataflow final : public DataFlowAnalysis {
public:
  ActivationDataflow(DataFlowSolver &solver, sim::SimFuncOp function,
                     const HandleFacts &handles, unsigned maximumPartitions)
      : DataFlowAnalysis(solver), function(function), handles(handles),
        maximumPartitions(maximumPartitions),
        loops(analyzeActivationBoundedLoops(function.getBody())) {}
  LogicalResult initialize(Operation *) override {
    // Ordinary loops use the finite constant/must-definition lattice. Proven
    // induction loops additionally retain iteration correlations in separate
    // partitions. No termination claim is needed to describe memory on paths
    // that reach the next suspension; scheduling keeps the loop itself intact.
    for (Block &block : function.getBody()) {
      auto *point = getProgramPointBefore(&block);
      addDependency(getOrCreate<MemoryState>(point), point);
    }
    Memory initial;
    for (BlockArgument argument : function.getBody().front().getArguments())
      if (auto found = handles.find(argument); found != handles.end())
        initial.references[argument] = found->second;
    return propagate(&function.getBody().front(), initial);
  }
  LogicalResult visit(ProgramPoint *point) override {
    Block *block = point->getBlock();
    // Successors can feed this block while it is being visited. Snapshot the
    // disjunction so insertion cannot invalidate references or iterators.
    auto rows = getOrCreateFor<MemoryState>(point, point)->rows;
    for (auto &[context, incoming] : rows) {
      (void)context;
      Memory state = incoming;
      for (Operation &op : block->without_terminator())
        if (failed(transfer(&op, state)))
          return failure();
      Operation *term = block->getTerminator();
      if (sim::isSuspensionOp(term) || isa<sim::SimReturnOp>(term)) {
        if (!exit)
          exit = state;
        else
          exit->join(state);
        continue;
      }
      if (auto branch = dyn_cast<cf::BranchOp>(term)) {
        if (failed(branchTo(branch.getDest(), branch.getDestOperands(), state)))
          return failure();
      } else if (auto branch = dyn_cast<cf::CondBranchOp>(term)) {
        auto condition = dyn_cast_or_null<IntegerAttr>(
            state.constants.lookup(branch.getCondition()));
        // Reuse the validated fixed-width test, including a known four-state
        // parameter bound. This is exactly the recognizer's continuation test.
        for (const auto &loop : loops)
          if (loop.condition == branch) {
            auto iv = dyn_cast_or_null<IntegerAttr>(
                state.constants.lookup(loop.induction));
            if (!iv)
              return failure();
            condition = IntegerAttr::get(
                IntegerType::get(function.getContext(), 1),
                arith::applyCmpPredicate(loop.predicate, iv.getValue(),
                                         loop.limit));
          }
        if ((!condition || !condition.getValue().isZero()) &&
            failed(branchTo(branch.getTrueDest(), branch.getTrueDestOperands(),
                            state)))
          return failure();
        if ((!condition || condition.getValue().isZero()) &&
            failed(branchTo(branch.getFalseDest(),
                            branch.getFalseDestOperands(), state)))
          return failure();
      } else if (auto branch = dyn_cast<cf::SwitchOp>(term)) {
        auto condition = dyn_cast_or_null<IntegerAttr>(
            state.constants.lookup(branch.getFlag()));
        bool matched = false;
        if (branch.getCaseValues())
          for (auto [i, value] :
               llvm::enumerate(branch.getCaseValues()->getValues<APInt>())) {
            if (condition && condition.getValue() != value)
              continue;
            matched = true;
            if (failed(branchTo(branch.getCaseDestinations()[i],
                                branch.getCaseOperands(i), state)))
              return failure();
          }
        if ((!condition || !matched) &&
            failed(branchTo(branch.getDefaultDestination(),
                            branch.getDefaultOperands(), state)))
          return failure();
      } else
        return failure();
    }
    return success();
  }
  std::optional<Memory> exit;
  DenseMap<Operation *, Ranges> reads, writes;
  unsigned partitions = 0;

private:
  std::optional<HandleFact> reference(Value value, const Memory &state) const {
    auto known = state.references.find(value);
    if (known != state.references.end())
      return known->second;
    auto may = handles.find(value);
    if (may == handles.end())
      return std::nullopt;
    return may->second;
  }
  LogicalResult propagate(Block *block, const Memory &state) {
    SmallVector<Attribute> key;
    for (const auto &loop : loops) {
      Attribute value = UnitAttr::get(function.getContext());
      if (loop.members.contains(block)) {
        value = state.constants.lookup(loop.induction);
        if (!isa_and_nonnull<IntegerAttr>(value))
          return failure();
      }
      key.push_back(value);
    }
    auto context = ArrayAttr::get(function.getContext(), key);
    auto *target = getOrCreate<MemoryState>(getProgramPointBefore(block));
    if (!target->rows.contains(context) && ++partitions > maximumPartitions)
      return failure();
    propagateIfChanged(target, target->join(context, state));
    return success();
  }
  LogicalResult branchTo(Block *target, ValueRange values, Memory state) {
    SmallVector<Attribute> constants;
    SmallVector<std::optional<HandleFact>> references;
    for (Value value : values) {
      constants.push_back(state.constants.lookup(value));
      references.push_back(reference(value, state));
    }
    for (auto [argument, constant, fact] :
         llvm::zip_equal(target->getArguments(), constants, references)) {
      state.constants.erase(argument);
      state.references.erase(argument);
      if (constant)
        state.constants[argument] = constant;
      if (fact)
        state.references[argument] = *fact;
    }
    return propagate(target, state);
  }
  void foldConstants(Operation *op, Memory &state) {
    for (Value value : op->getResults())
      state.constants.erase(value);
    if (!isMemoryEffectFree(op))
      return;
    if (!llvm::any_of(op->getResultTypes(), [](Type t) {
          return isa<IntegerType, sim::LogicType>(t);
        }))
      return;
    SmallVector<Attribute> operands;
    for (Value operand : op->getOperands()) {
      Attribute value = state.constants.lookup(operand);
      if (!value)
        return;
      operands.push_back(value);
    }
    // Dialect fold hooks may fold in place. A disposable operation keeps this
    // analysis read-only and gives each partition an independent fold query.
    Operation *copy = op->cloneWithoutRegions();
    SmallVector<OpFoldResult> folded;
    if (succeeded(copy->fold(operands, folded)) &&
        folded.size() == op->getNumResults())
      for (auto [result, fold] : llvm::zip_equal(op->getResults(), folded)) {
        Attribute value = dyn_cast<Attribute>(fold);
        if (auto forwarded = dyn_cast<Value>(fold))
          value = state.constants.lookup(forwarded);
        if (value)
          state.constants[result] = value;
      }
    copy->destroy();
  }
  IntegerAttr knownIndex(Value value, const Memory &state) const {
    Attribute folded = state.constants.lookup(value);
    if (auto integer = dyn_cast_or_null<IntegerAttr>(folded))
      return integer;
    // Simulation's fold interface represents both four-state planes. An
    // integer address exists only when the unknown plane is empty.
    if (auto planes = dyn_cast_or_null<ArrayAttr>(folded);
        planes && planes.size() == 2) {
      auto bits = dyn_cast<IntegerAttr>(planes[0]);
      auto unknown = dyn_cast<IntegerAttr>(planes[1]);
      if (bits && unknown && bits.getType() == unknown.getType() &&
          unknown.getValue().isZero())
        return bits;
    }
    return {};
  }
  void projectReference(Operation *op, Memory &state) {
    for (Value value : op->getResults())
      state.references.erase(value);
    if (op->getNumResults() != 1 ||
        !isa<sim::RefType>(op->getResult(0).getType()))
      return;
    if (op->getNumOperands() == 0)
      return;
    auto parent = reference(op->getOperand(0), state);
    if (!parent || parent->dynamic)
      return;
    auto parentType = dyn_cast<sim::RefType>(op->getOperand(0).getType());
    auto parentSpan = sim::getProvenanceSpan(op->getOperand(0).getType());
    if (!parentType || !parentSpan || parent->width != *parentSpan)
      return;
    std::optional<uint64_t> offset;
    if (auto element = dyn_cast<sim::SimRefArrayElementOp>(op)) {
      auto index = knownIndex(element->getOperand(1), state);
      if (!index || !index.getValue().isSignedIntN(64))
        return;
      int64_t logical = index.getInt();
      Type aggregate = parentType.getElementType();
      auto ordinal = [&](auto type) -> std::optional<uint64_t> {
        int64_t left = type.getLeft(), right = type.getRight();
        if (logical < std::min(left, right) || logical > std::max(left, right))
          return std::nullopt;
        return left <= right ? uint64_t(logical) - uint64_t(left)
                             : uint64_t(left) - uint64_t(logical);
      };
      auto selected =
          TypeSwitch<Type, std::optional<uint64_t>>(aggregate)
              .Case<sim::PackedArrayType, sim::UnpackedArrayType>(ordinal)
              .Default(std::nullopt);
      if (selected && *selected <= UINT_MAX)
        if (auto child =
                sim::getAggregateProvenanceSubelement(aggregate, *selected))
          offset = child->first;
    } else if (auto extract = dyn_cast<sim::SimRefExtractOp>(op))
      offset = extract.getLowBit();
    else if (isa<sim::SimRefDynExtractOp>(op)) {
      auto index = knownIndex(op->getOperand(1), state);
      if (index && !index.getValue().isNegative() &&
          index.getValue().getActiveBits() <= 64)
        offset = index.getValue().getZExtValue();
    } else if (auto element = dyn_cast<sim::SimRefSubelementOp>(op)) {
      Type type = parentType.getElementType();
      offset = 0;
      for (int64_t index : element.getIndices()) {
        auto child = index >= 0 && uint64_t(index) <= UINT_MAX
                         ? sim::getAggregateProvenanceSubelement(type, index)
                         : std::nullopt;
        if (!child || child->first > UINT64_MAX - *offset) {
          offset.reset();
          break;
        }
        *offset += child->first;
        type = sim::getAggregateElementType(type, index);
      }
    }
    auto span = sim::getProvenanceSpan(op->getResult(0).getType());
    if (!offset || !span || *offset > *parentSpan ||
        *span > *parentSpan - *offset)
      return;
    parent->low += *offset;
    parent->width = *span;
    state.references[op->getResult(0)] = *parent;
  }
  LogicalResult transfer(Operation *op, Memory &state) {
    if (op->getNumRegions())
      return failure();
    foldConstants(op, state);
    projectReference(op, state);
    auto load = dyn_cast<sim::SimRefLoadOp>(op);
    auto store = dyn_cast<sim::SimRefStoreOp>(op);
    if (!load && !store)
      return success(isMemoryEffectFree(op));
    Value value = load ? load.getReference() : store.getReference();
    auto fact = reference(value, state);
    if (!fact || !fact->descriptor ||
        fact->resource != schedule::ComputeResourceKind::Storage ||
        !fact->width || fact->low > fact->rootWidth ||
        fact->width > fact->rootWidth - fact->low)
      return failure();
    Ranges access{{*fact->descriptor, fact->low, fact->width}};
    if (load) {
      unite(reads[op], access);
      unite(state.exposed, subtract(access, state.assigned));
    } else {
      unite(writes[op], access);
      unite(state.repeated, intersect(access, state.written));
      unite(state.written, access);
      auto span = sim::getProvenanceSpan(value.getType());
      if (!fact->dynamic && span && fact->width == *span)
        unite(state.assigned, access);
    }
    return success();
  }
  sim::SimFuncOp function;
  const HandleFacts &handles;
  unsigned maximumPartitions;
  SmallVector<BoundedLoop> loops;
};
} // namespace

ActivationMemoryAnalysis::ActivationMemoryAnalysis(
    sim::SimFuncOp function, const HandleDataflowAnalysis &handleAnalysis,
    unsigned maximumPartitions) {
  if (function.isExternal() || function.getBody().empty())
    return;
  auto handles = handleAnalysis.derive(function);
  DataFlowSolver solver;
  auto *analysis =
      solver.load<ActivationDataflow>(function, handles, maximumPartitions);
  if (failed(solver.initializeAndRun(function)) || !analysis->exit)
    return;
  valid = true;
  partitions = analysis->partitions;
  liveIns = std::move(analysis->exit->exposed);
  mayWrites = std::move(analysis->exit->written);
  mustWrites = std::move(analysis->exit->assigned);
  repeatedWrites = std::move(analysis->exit->repeated);
  reads = std::move(analysis->reads);
  writes = std::move(analysis->writes);
}
bool ActivationMemoryAnalysis::hasCompleteWrites() const {
  return valid && subtract(mayWrites, mustWrites).empty();
}
} // namespace obelisk::analysis
