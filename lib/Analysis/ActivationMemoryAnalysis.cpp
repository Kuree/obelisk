//===- ActivationMemoryAnalysis.cpp - Context-partitioned memory facts ----===//
#include "obelisk/Analysis/ActivationMemoryAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Analysis/Liveness.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Analysis/BoundedLoopAnalysis.h"
#include "obelisk/Analysis/NativeStorageLayout.h"
#include "obelisk/Analysis/RangeSet.h"
#include "obelisk/Analysis/SSAValueAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/SimulationEffectAnalysis.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/ADT/bit.h"
#include <limits>
#include <memory>
#include <numeric>

using namespace mlir;
namespace obelisk::analysis {
namespace {
using Range = ActivationMemoryAnalysis::Range;
using Ranges = ActivationMemoryAnalysis::Ranges;
using DependencyMode = ActivationMemoryAnalysis::DependencyMode;
// Provenance spans include target ABI holes so adjacent typed views use the
// same offsets as lowering. Those holes are not language values. Keep their
// geometry, but exclude them from dependencies and definite-write coverage.
// Cache the mask by immutable type; most scalar/packed types need no mask.
class LogicalLanes {
public:
  /// Above this many logical lanes a type keeps every provenance lane, as an
  /// unrepresentable mask does.
  static constexpr size_t maximumLogicalLanes = 1024;
  ArrayRef<Range> get(Type type) {
    auto array = dyn_cast<sim::UnpackedArrayType>(type);
    auto record = dyn_cast<sim::UnpackedStructType>(type);
    if ((!array || !array.getLayout()) && (!record || !record.getLayout()))
      return {};
    auto [entry, inserted] = lanes.try_emplace(type);
    if (inserted) {
      entry->second = std::make_unique<Ranges>();
      auto width = sim::getProvenanceSpan(type);
      // An unrepresentable mask conservatively keeps every provenance lane.
      if (!width || !*width || *width > UINT_MAX)
        return {};
      // Each lane is a separate relation at every node that touches the
      // type. Past the cap, the padding precision is not worth that cost.
      auto runs = getLogicalStorageRuns(type, maximumLogicalLanes);
      if (!runs || (runs->size() == 1 && runs->front().first == 0 &&
                    runs->front().second ==
                        *getSimulationStorageBitWidth(type)))
        return {};
      for (auto [low, laneWidth] : *runs)
        entry->second->push_back({0, low, laneWidth});
    }
    return *entry->second;
  }
  Ranges access(Type type, Range span, bool exact) {
    auto valid = exact ? get(type) : ArrayRef<Range>{};
    if (valid.empty())
      return {span};
    Ranges result;
    for (auto lane : valid)
      result.push_back({span.root, span.low + lane.low, lane.width});
    return result;
  }

private:
  DenseMap<Type, std::unique_ptr<Ranges>> lanes;
};
// Forward value dependence, with exact lane correspondence for loads and
// aggregate projections. Unknown pure operations conservatively make every
// result lane depend on all operand sources. Inserting a value kills only the
// overwritten lanes; published values and control decisions expose their
// remaining sources. Both four-state planes share the same logical bit range.
struct Influence {
  uint64_t low, width;
  Range source;
  bool identity;
  bool operator==(const Influence &other) const {
    return low == other.low && width == other.width && source == other.source &&
           identity == other.identity;
  }
};
using Influences = SmallVector<Influence>;
uint64_t valueSpan(Value value) {
  return sim::getProvenanceSpan(value.getType()).value_or(1);
}
Ranges sources(ArrayRef<Influence> influences) {
  Ranges result;
  for (const auto &influence : influences)
    result.push_back(influence.source);
  normalize(result);
  return result;
}
Influences spread(ArrayRef<Range> ranges, uint64_t width) {
  Influences result;
  for (auto source : ranges)
    result.push_back({0, width, source, false});
  return result;
}
void normalize(Influences &influences) {
  llvm::sort(influences, [](const Influence &a, const Influence &b) {
    return std::tuple(a.source.root, a.identity, a.low, a.source.low,
                      UINT64_MAX - a.width, a.source.width) <
           std::tuple(b.source.root, b.identity, b.low, b.source.low,
                      UINT64_MAX - b.width, b.source.width);
  });
  Influences merged;
  for (auto influence : influences) {
    if (!merged.empty()) {
      auto &last = merged.back();
      bool sameMap =
          last.identity && influence.identity &&
          last.source.root == influence.source.root &&
          influence.low >= last.low &&
          influence.source.low >= last.source.low &&
          influence.low - last.low == influence.source.low - last.source.low;
      bool sameSource = !last.identity && !influence.identity &&
                        last.source == influence.source;
      if ((sameMap || sameSource) && influence.low <= last.low + last.width) {
        last.width =
            std::max(last.low + last.width, influence.low + influence.width) -
            last.low;
        if (last.identity)
          last.source.width = last.width;
        continue;
      }
    }
    merged.push_back(influence);
  }
  influences = std::move(merged);
}
Influences project(ArrayRef<Influence> input, uint64_t low, uint64_t width,
                   uint64_t destination = 0) {
  Influences result;
  for (auto influence : input) {
    uint64_t first = std::max(low, influence.low);
    uint64_t end = std::min(low + width, influence.low + influence.width);
    if (first >= end)
      continue;
    if (influence.identity) {
      influence.source.low += first - influence.low;
      influence.source.width = end - first;
    }
    influence.low = destination + first - low;
    influence.width = end - first;
    result.push_back(influence);
  }
  return result;
}

IntegerAttr knownIndex(Value value,
                       const DenseMap<Value, Attribute> &constants) {
  Attribute folded = constants.lookup(value);
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

// Sparse value facts are separate from the dense reaching-definition state.
// A link projects source lanes into destination lanes, or conservatively makes
// every destination lane depend on a source interval.
enum class LaneMapping { Identity, AllToAll };
struct LaneLink {
  uint64_t low, width;
  unsigned source;
  uint64_t sourceLow, sourceWidth;
  LaneMapping mapping;
  bool operator==(const LaneLink &other) const {
    return std::tie(low, width, source, sourceLow, sourceWidth, mapping) ==
           std::tie(other.low, other.width, other.source, other.sourceLow,
                    other.sourceWidth, other.mapping);
  }
};
using LaneLinks = SmallVector<LaneLink>;
bool linkLess(const LaneLink &a, const LaneLink &b) {
  return std::tie(a.source, a.mapping, a.low, a.sourceLow, a.width,
                  a.sourceWidth) < std::tie(b.source, b.mapping, b.low,
                                            b.sourceLow, b.width,
                                            b.sourceWidth);
}
void normalize(LaneLinks &links) {
  llvm::sort(links, linkLess);
  links.erase(std::unique(links.begin(), links.end()), links.end());
}
LaneLinks projectLinks(ArrayRef<LaneLink> links, uint64_t low, uint64_t width,
                       uint64_t destination = 0) {
  LaneLinks result;
  for (auto link : links) {
    uint64_t first = std::max(low, link.low);
    uint64_t end = std::min(low + width, link.low + link.width);
    if (first >= end)
      continue;
    if (link.mapping == LaneMapping::Identity) {
      link.sourceLow += first - link.low;
      link.sourceWidth = end - first;
    }
    link.low = destination + first - low;
    link.width = end - first;
    result.push_back(link);
  }
  return result;
}
struct ReachingDefinitions {
  uint64_t width;
  unsigned incoming;
  LaneLinks links;
  LaneLink identity() const {
    return {0, width, incoming, 0, width, LaneMapping::Identity};
  }
  bool operator==(const ReachingDefinitions &other) const {
    return width == other.width && incoming == other.incoming &&
           links == other.links;
  }
  bool operator!=(const ReachingDefinitions &other) const {
    return !(*this == other);
  }
};
/// Reaching definitions per tracked root. Every CFG edge and revisited row
/// copies a whole dense state, while an activation updates few roots between
/// copies. Cells are therefore shared copy-on-write: a copy only shares them,
/// a write clones just its own cell, and a join skips the cells both states
/// still share.
class ReachingMap {
  struct Cell : llvm::RefCountedBase<Cell> {
    explicit Cell(ReachingDefinitions value) : value(std::move(value)) {}
    ReachingDefinitions value;
  };
  using Ptr = llvm::IntrusiveRefCntPtr<Cell>;
  DenseMap<uint64_t, Ptr> cells;

public:
  const ReachingDefinitions *find(uint64_t root) const {
    auto found = cells.find(root);
    return found == cells.end() ? nullptr : &found->second->value;
  }
  /// The cell of `root` for writing, first unshared, or null if untracked.
  ReachingDefinitions *write(uint64_t root) {
    auto found = cells.find(root);
    if (found == cells.end())
      return nullptr;
    if (found->second->UseCount() > 1)
      found->second = llvm::makeIntrusiveRefCnt<Cell>(found->second->value);
    return &found->second->value;
  }
  const ReachingDefinitions &set(uint64_t root, ReachingDefinitions cell) {
    Ptr &slot = cells[root];
    slot = llvm::makeIntrusiveRefCnt<Cell>(std::move(cell));
    return slot->value;
  }
  /// Joins `other` into this map. Missing roots are unchanged incoming values,
  /// not bottom. Stored definition links are normalized, so their union is a
  /// linear merge, and it changed exactly when it grew.
  bool join(const ReachingMap &other) {
    bool changed = false;
    for (auto &[root, cell] : cells) {
      auto found = other.cells.find(root);
      if (found != other.cells.end() && found->second == cell)
        continue;
      const ReachingDefinitions &own = cell->value;
      if (found == other.cells.end()) {
        LaneLink identity = own.identity();
        auto position = llvm::lower_bound(own.links, identity, linkLess);
        if (position == own.links.end() || !(*position == identity)) {
          ReachingDefinitions next = own;
          next.links.insert(next.links.begin() + (position - own.links.begin()),
                            identity);
          cell = llvm::makeIntrusiveRefCnt<Cell>(std::move(next));
          changed = true;
        }
        continue;
      }
      const ReachingDefinitions &incoming = found->second->value;
      LaneLinks merged;
      merged.reserve(own.links.size() + incoming.links.size());
      std::merge(own.links.begin(), own.links.end(), incoming.links.begin(),
                 incoming.links.end(), std::back_inserter(merged), linkLess);
      merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
      if (merged.size() != own.links.size()) {
        cell = llvm::makeIntrusiveRefCnt<Cell>(
            ReachingDefinitions{own.width, own.incoming, std::move(merged)});
        changed = true;
      } else if (incoming == own) {
        // Equal contents: share the cell so later joins skip it.
        cell = found->second;
      }
    }
    for (const auto &[root, cell] : other.cells) {
      if (cells.contains(root))
        continue;
      LaneLink identity = cell->value.identity();
      auto position = llvm::lower_bound(cell->value.links, identity, linkLess);
      if (position != cell->value.links.end() && *position == identity) {
        cells.try_emplace(root, cell);
      } else {
        ReachingDefinitions copy = cell->value;
        copy.links.insert(
            copy.links.begin() + (position - cell->value.links.begin()),
            identity);
        cells.try_emplace(root, llvm::makeIntrusiveRefCnt<Cell>(std::move(copy)));
      }
      changed = true;
    }
    return changed;
  }
};
struct LaneNode {
  unsigned id;
  uint64_t width;
  Block *block;
  Operation *operation;
  LaneLinks links;
  Influences seed;
  ArrayRef<Range> logical;
};
class LaneAnchor final
    : public GenericLatticeAnchorBase<LaneAnchor, LaneNode *> {
public:
  using Base::Base;
  Location getLoc() const override {
    return getValue()->block->getParentOp()->getLoc();
  }
  void print(raw_ostream &os) const override {
    os << "activation SSA lane " << getValue()->id;
  }
};
struct LaneWorklist {
  ProgramPoint *point;
  DataFlowAnalysis *analysis;
  llvm::SetVector<unsigned> pending;

  void enqueue(unsigned id, DataFlowSolver *solver) {
    bool wasEmpty = pending.empty();
    pending.insert(id);
    if (wasEmpty)
      solver->enqueue({point, analysis});
  }
};
class LaneState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  Influences influences;
  SmallVector<std::pair<LaneWorklist *, unsigned>> consumers;
  bool widened = false;
  ChangeResult join(ArrayRef<Influence> inputs, uint64_t width,
                    ArrayRef<Range> logical) {
    Influences next(influences);
    llvm::append_range(next, inputs);
    normalize(next);
    // Widening is sticky. Otherwise repeatedly joining an exact relation into
    // a widened relation could oscillate around the correlation-size limit.
    bool nextWidened = widened || next.size() > 256;
    if (nextWidened)
      next = spread(sources(next), width);
    if (!logical.empty()) {
      Influences filtered;
      for (auto lane : logical)
        llvm::append_range(filtered,
                           project(next, lane.low, lane.width, lane.low));
      next = std::move(filtered);
      normalize(next);
    }
    if (next == influences && nextWidened == widened)
      return ChangeResult::NoChange;
    influences = std::move(next);
    widened = nextWidened;
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override {
    os << influences.size() << " lane relations";
  }

protected:
  void onUpdate(DataFlowSolver *solver) const override {
    AnalysisState::onUpdate(solver);
    for (auto [work, id] : consumers)
      work->enqueue(id, solver);
  }
};
/// Solves the lanes of every representative node. A forwarding node shares
/// its representative's anchor and state.
class SparseLaneDataflow final : public DataFlowAnalysis {
public:
  SparseLaneDataflow(DataFlowSolver &solver,
                     ArrayRef<std::unique_ptr<LaneNode>> nodes,
                     ArrayRef<unsigned> representatives)
      : DataFlowAnalysis(solver), nodes(nodes),
        representatives(representatives) {
    registerAnchorKind<LaneAnchor>();
  }
  LogicalResult initialize(Operation *) override {
    anchors.resize(nodes.size());
    states.resize(nodes.size());
    for (const auto &node : nodes)
      if (isRepresentative(*node)) {
        anchors[node->id] = getLatticeAnchor<LaneAnchor>(node.get());
        states[node->id] = getOrCreate<LaneState>(anchors[node->id]);
      }
    for (const auto &node : nodes)
      if (!isRepresentative(*node)) {
        anchors[node->id] = anchors[representatives[node->id]];
        states[node->id] = states[representatives[node->id]];
      }
    for (const auto &node : nodes) {
      if (!isRepresentative(*node))
        continue;
      auto *point = node->operation ? getProgramPointAfter(node->operation)
                                    : getProgramPointBefore(node->block);
      auto &work = targets[point];
      if (!work)
        work = std::make_unique<LaneWorklist>(LaneWorklist{point, this, {}});
      work->pending.insert(node->id);
      // Links are normalized by source. Register each node-to-node dependency
      // once, even when several lane projections read the same source.
      unsigned previous = std::numeric_limits<unsigned>::max();
      for (const auto &link : node->links) {
        if (link.source != previous)
          states[link.source]->consumers.emplace_back(work.get(), node->id);
        previous = link.source;
      }
    }
    for (const auto &node : nodes) {
      if (!isRepresentative(*node))
        continue;
      auto *state = states[node->id];
      propagateIfChanged(state,
                         state->join(node->seed, node->width, node->logical));
    }
    for (const auto &[point, work] : targets)
      if (failed(visit(point)))
        return failure();
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    // Many induction contexts share one MLIR program point. A changed source
    // only invalidates its actual consumers, not every context at that point.
    auto &work = *targets.at(point);
    SmallVector<unsigned> pending(work.pending.begin(), work.pending.end());
    work.pending.clear();
    for (unsigned id : pending) {
      auto &node = *nodes[id];
      Influences next;
      for (const auto &link : node.links) {
        const auto *source = states[link.source];
        auto part =
            project(source->influences, link.sourceLow, link.sourceWidth);
        if (link.mapping == LaneMapping::AllToAll)
          part = spread(sources(part), link.width);
        llvm::append_range(next, project(part, 0, link.width, link.low));
      }
      auto *state = states[id];
      propagateIfChanged(state, state->join(next, node.width, node.logical));
    }
    return success();
  }
  SmallVector<LaneAnchor *> anchors;

private:
  bool isRepresentative(const LaneNode &node) const {
    return representatives[node.id] == node.id;
  }
  ArrayRef<std::unique_ptr<LaneNode>> nodes;
  ArrayRef<unsigned> representatives;
  SmallVector<LaneState *> states;
  llvm::MapVector<ProgramPoint *, std::unique_ptr<LaneWorklist>> targets;
};

// The dense pass records memory definitions and feasible CFG edges. SSA facts
// themselves live only at these sparse nodes. Live-in proxies connect values
// across induction contexts using MLIR liveness; they never copy value lattices
// through a CFG state. Contexts describe bounded iterations, not executable IR.
class ActivationValueGraph {
public:
  ActivationValueGraph(sim::SimFuncOp function,
                       const DenseMap<uint64_t, ArrayRef<Range>> &rootLanes,
                       const Liveness &liveness)
      : function(function), rootLanes(rootLanes), liveness(liveness) {
    unsigned ordinal = 0;
    function.walk([&](Block *block) {
      for (Value value : block->getArguments())
        ordinals[value] = ordinal++;
      for (Operation &op : *block)
        for (Value result : op.getResults())
          ordinals[result] = ordinal++;
    });
  }
  LogicalResult initialize() {
    for (Block &block : function.getBody())
      if (sim::isSuspensionOp(block.getTerminator()))
        return failure();
    if (function.getBody().hasOneBlock())
      return success();
    PostDominanceInfo postDominance(function);
    for (Block &block : function.getBody()) {
      if (!isa<cf::CondBranchOp, cf::SwitchOp>(block.getTerminator()))
        continue;
      auto *node = postDominance.getNode(&block);
      if (!node)
        return failure();
      auto *stop = node->getIDom();
      for (Block *successor : block.getSuccessors()) {
        auto *runner = postDominance.getNode(successor);
        if (!runner)
          return failure();
        while (runner && runner != stop) {
          if (Block *controlled = runner->getBlock())
            controllers[controlled].insert(&block);
          runner = runner->getIDom();
        }
      }
    }
    for (Block &block : function.getBody()) {
      SmallVector<Block *> pending{&block};
      DenseSet<Block *> visited;
      while (!pending.empty()) {
        Block *current = pending.pop_back_val();
        if (!visited.insert(current).second)
          continue;
        for (Block *controller : controllers.lookup(current)) {
          Operation *term = controller->getTerminator();
          Value condition = isa<cf::CondBranchOp>(term)
                                ? cast<cf::CondBranchOp>(term).getCondition()
                                : cast<cf::SwitchOp>(term).getFlag();
          SmallVector<Block *> users{&block};
          while (!users.empty()) {
            Block *use = users.pop_back_val();
            if (condition.getParentBlock() == use ||
                !implicitLive[use].insert(condition))
              continue;
            llvm::append_range(users, use->getPredecessors());
          }
          pending.push_back(controller);
        }
      }
    }
    return success();
  }
  void enter(Block *block, ArrayAttr context) {
    currentBlock = block;
    currentContext = context;
    currentScope = &scopeOf(block, context);
  }
  unsigned size() const { return nodes.size(); }
  /// Blocks whose context key is always empty: they belong to no proven
  /// loop and carry no live control values, so each executes in one context.
  void setSingleContextBlocks(DenseSet<Block *> blocks) {
    singleContextBlocks = std::move(blocks);
    emptyContext = ArrayAttr::get(function.getContext(), {});
  }
  /// A value defined in a single-context block has one dynamic version, so
  /// its live-in proxies would be identity copies of the definition node.
  bool hasSingleVersion(Value value) const {
    return singleContextBlocks.contains(value.getParentBlock());
  }
  unsigned valueNode(Value value, Block *block, ArrayAttr context) {
    // Entry definitions dominate the entire activation and have one value in
    // every induction context. In particular, thousands of immutable capture
    // handles must not get a live-in proxy at every block of a fused kernel.
    if (value.getParentBlock() == &function.getBody().front()) {
      block = value.getParentBlock();
      context = {};
    } else if (hasSingleVersion(value)) {
      block = value.getParentBlock();
      context = emptyContext;
    }
    DenseMap<Value, unsigned> &scope =
        currentScope && block == currentBlock && context == currentContext
            ? *currentScope
            : scopeOf(block, context);
    return valueNodeIn(scope, value, block);
  }
  /// Value nodes of one (block, induction context) scope. A fused kernel
  /// makes millions of live-in lookups; small per-scope maps keep them local.
  DenseMap<Value, unsigned> &scopeOf(Block *block, ArrayAttr context) {
    auto &scope = valueScopes[{block, context}];
    if (!scope)
      scope = std::make_unique<DenseMap<Value, unsigned>>();
    return *scope;
  }
  unsigned valueNodeIn(DenseMap<Value, unsigned> &scope, Value value,
                       Block *block) {
    auto [it, inserted] = scope.try_emplace(value, 0);
    if (inserted) {
      Operation *definition = value.getDefiningOp();
      it->second = addNode(
          valueSpan(value), block,
          definition && definition->getBlock() == block ? definition : nullptr);
      nodes[it->second]->logical = logical.get(value.getType());
    }
    return it->second;
  }
  unsigned valueNode(Value value) {
    return valueNode(value, currentBlock, currentContext);
  }
  LaneLink valueLink(Value value, uint64_t destinationWidth) {
    return {0, destinationWidth, valueNode(value),
            0, valueSpan(value), LaneMapping::AllToAll};
  }
  unsigned incomingNode(uint64_t root, uint64_t width) {
    auto [it, inserted] = incoming.try_emplace(root, 0);
    if (inserted) {
      it->second = addNode(width, &function.getBody().front(), nullptr);
      nodes[it->second]->seed.push_back({0, width, {root, 0, width}, true});
      nodes[it->second]->logical = rootLanes.lookup(root);
    }
    return it->second;
  }
  void append(unsigned target, ArrayRef<LaneLink> links) {
    LaneLinks &existing = nodes[target]->links;
    // Links stay normalized. Most edges add one proxy link to a block value.
    if (links.size() == 1) {
      auto position = llvm::lower_bound(existing, links.front(), linkLess);
      if (position == existing.end() || !(*position == links.front()))
        existing.insert(position, links.front());
      return;
    }
    llvm::append_range(existing, links);
    normalize(existing);
  }
  LaneLinks controls(uint64_t width) {
    LaneLinks result;
    SmallVector<Block *> pending{currentBlock};
    DenseSet<Block *> visited;
    while (!pending.empty()) {
      Block *block = pending.pop_back_val();
      if (!visited.insert(block).second)
        continue;
      for (Block *controller : controllers.lookup(block)) {
        Operation *term = controller->getTerminator();
        Value condition = isa<cf::CondBranchOp>(term)
                              ? cast<cf::CondBranchOp>(term).getCondition()
                              : cast<cf::SwitchOp>(term).getFlag();
        result.push_back(valueLink(condition, width));
        pending.push_back(controller);
      }
    }
    return result;
  }
  void edge(Block *target, ArrayAttr targetContext, ValueRange operands,
            Value condition) {
    // A row is revisited whenever its memory state grows, but the links of
    // an edge depend only on its blocks, contexts and successor operands (the
    // condition is the terminator's own). Emit each edge once.
    if (!emittedEdges
             .insert({currentBlock, currentContext, target, targetContext,
                      operands.empty() ? nullptr
                                       : operands.getBase().getOpaqueValue()})
             .second)
      return;
    // Both live sets are fixed before solving, but a block is entered once
    // per incoming edge and context. Order its live values once.
    auto [cached, inserted] = liveIns.try_emplace(target);
    SmallVector<std::pair<Value, uint64_t>> &live = cached->second;
    if (inserted) {
      SmallVector<Value> values;
      auto appendLive = [&](const auto &range) {
        for (Value value : range)
          if (value.getParentBlock() != &function.getBody().front())
            values.push_back(value);
      };
      appendLive(liveness.getLiveIn(target));
      appendLive(implicitLive.lookup(target));
      // Liveness sets are unordered. Stable SSA ordinals keep the evaluation
      // graph and widening independent of hash iteration order.
      llvm::sort(values, [&](Value a, Value b) {
        return ordinals.at(a) < ordinals.at(b);
      });
      values.erase(std::unique(values.begin(), values.end()), values.end());
      for (Value value : values)
        live.emplace_back(value, valueSpan(value));
    }
    DenseMap<Value, unsigned> &targetScope = scopeOf(target, targetContext);
    for (auto [value, span] : live) {
      if (hasSingleVersion(value))
        continue;
      append(valueNodeIn(targetScope, value, target),
             {{0, span, valueNode(value), 0, span, LaneMapping::Identity}});
    }
    for (auto [argument, operand] :
         llvm::zip_equal(target->getArguments(), operands)) {
      auto links = controls(valueSpan(argument));
      links.push_back({0, valueSpan(argument), valueNode(operand), 0,
                       valueSpan(operand), LaneMapping::Identity});
      if (condition)
        links.push_back(valueLink(condition, valueSpan(argument)));
      append(valueNode(argument, target, targetContext), links);
    }
  }
  void expose(Value value, bool isReturn = false) {
    unsigned node = valueNode(value);
    sinks.insert(node);
    if (isReturn)
      finalSinks.insert(node);
  }
  void pure(Operation *op, const DenseMap<Value, Attribute> &constants);
  /// `lane` is the stored value's lane published at `output.low` when exact.
  unsigned publication(Operation *store, Range output, bool exact,
                       uint64_t lane = 0) {
    auto key = std::make_pair(store, currentContext);
    auto [it, inserted] = stores.try_emplace(key, 0);
    if (inserted)
      it->second =
          addNode(valueSpan(ActivationMemoryAnalysis::getStoredValue(store)),
                  currentBlock, store);
    unsigned node = it->second;
    nodes[node]->logical =
        logical.get(ActivationMemoryAnalysis::getStoredValue(store).getType());
    auto links = controls(nodes[node]->width);
    links.push_back({0, nodes[node]->width,
                     valueNode(ActivationMemoryAnalysis::getStoredValue(store)),
                     0,
                     valueSpan(ActivationMemoryAnalysis::getStoredValue(store)),
                     LaneMapping::Identity});
    links.push_back(valueLink(ActivationMemoryAnalysis::getWriteHandle(store),
                              nodes[node]->width));
    append(node, links);
    sinks.insert(node);
    records.push_back({store, node, output, exact, lane});
    return node;
  }
  /// The value a store writes into a private slot: its value, address and
  /// controls, but no design publication.
  unsigned privateStore(sim::SimRefStoreOp store) {
    auto key = std::make_pair(store.getOperation(), currentContext);
    auto [it, inserted] = stores.try_emplace(key, 0);
    if (inserted)
      it->second = addNode(valueSpan(store.getValue()), currentBlock, store);
    unsigned node = it->second;
    nodes[node]->logical = logical.get(store.getValue().getType());
    auto links = controls(nodes[node]->width);
    links.push_back({0, nodes[node]->width, valueNode(store.getValue()), 0,
                     valueSpan(store.getValue()), LaneMapping::Identity});
    links.push_back(valueLink(store.getReference(), nodes[node]->width));
    append(node, links);
    return node;
  }
  LogicalResult solve(
      Ranges &inputs, Ranges &finalInputs,
      const ReachingMap &finalMemory,
      ArrayRef<Range> finalWrites,
      DenseMap<Operation *, SmallVector<ActivationMemoryAnalysis::Publication>>
          &publications,
      bool inputsOnly = false) {
    // Keep the final reaching version of each written lane distinct from
    // intermediate publications. A whole-array copy inside an induction loop
    // can expose old lanes early and overwrite them before the activation ends.
    for (auto range : inputsOnly ? ArrayRef<Range>{} : finalWrites) {
      const ReachingDefinitions *found = finalMemory.find(range.root);
      if (!found)
        return failure();
      unsigned node =
          addNode(range.width, &function.getBody().front(), function);
      append(node, projectLinks(found->links, range.low, range.width));
      finalSinks.insert(node);
    }
    SmallVector<unsigned> representatives = collapseForwarders();
    DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
    auto *flow = solver.load<SparseLaneDataflow>(nodes, representatives);
    if (failed(solver.initializeAndRun(function)))
      return failure();
    auto facts = [&](unsigned node) -> const Influences & {
      return solver.lookupState<LaneState>(flow->anchors[node])->influences;
    };
    for (unsigned node : sinks)
      unite(inputs, sources(facts(node)));
    if (inputsOnly)
      return success();
    for (unsigned node : finalSinks)
      unite(finalInputs, sources(facts(node)));
    // Influences are per written root: a store of a collapsed net publishes
    // the same value to every member, each at its own offsets.
    DenseMap<std::pair<Operation *, uint64_t>, Influences> stored;
    DenseMap<Operation *, Ranges> written;
    for (const auto &record : records) {
      auto inputs = facts(record.node);
      if (!record.exact)
        inputs = spread(sources(inputs), record.output.width);
      auto &output = stored[{record.operation, record.output.root}];
      llvm::append_range(output, project(inputs, record.exact ? record.lane : 0,
                                         record.output.width,
                                         record.output.low));
      normalize(output);
      unite(written[record.operation], {record.output});
    }
    for (const auto &[operation, ranges] : written) {
      auto &lanes = publications[operation];
      for (Range output : ranges) {
        const auto &influences = stored.at({operation, output.root});
        SmallVector<uint64_t> bounds{output.low, output.low + output.width};
        for (const auto &influence : influences) {
          uint64_t low = std::max(output.low, influence.low);
          uint64_t high = std::min(output.low + output.width,
                                   influence.low + influence.width);
          if (low < high) {
            bounds.push_back(low);
            bounds.push_back(high);
          }
        }
        llvm::sort(bounds);
        bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
        for (size_t i = 1; i < bounds.size(); ++i) {
          Range lane{output.root, bounds[i - 1], bounds[i] - bounds[i - 1]};
          lanes.push_back(
              {lane, sources(project(influences, lane.low, lane.width))});
        }
      }
    }
    return success();
  }

private:
  /// Most nodes are live-in proxies that forward one definition unchanged
  /// along CFG edges, for example through both arms of a diamond. A node whose
  /// only links are full identities of one source, besides its own, and that
  /// adds no seed or lane filter denotes that source's lanes. Map each such
  /// node to a representative whose state it shares, and point every link at
  /// representatives, so the solver anchors only nodes that seed, project or
  /// join lanes. A separately joined copy could only keep exact relations that
  /// a widened source already covers, so sharing is as sound and as precise.
  SmallVector<unsigned> collapseForwarders() {
    SmallVector<unsigned> parent(nodes.size());
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](unsigned id) {
      while (parent[id] != id)
        id = parent[id] = parent[parent[id]];
      return id;
    };
    auto isForward = [](const LaneNode &node, const LaneLink &link) {
      return link.mapping == LaneMapping::Identity && link.low == 0 &&
             link.sourceLow == 0 && link.width == node.width &&
             link.sourceWidth == node.width;
    };
    auto forwardedSource = [&](const LaneNode &node) -> std::optional<unsigned> {
      if (!node.seed.empty())
        return std::nullopt;
      std::optional<unsigned> source;
      for (const LaneLink &link : node.links) {
        if (!isForward(node, link))
          return std::nullopt;
        unsigned from = find(link.source);
        if (from == node.id)
          continue;
        if (source && *source != from)
          return std::nullopt;
        source = from;
      }
      if (!source)
        return std::nullopt;
      const LaneNode &target = *nodes[*source];
      if (target.width != node.width ||
          !(node.logical.empty() || llvm::equal(node.logical, target.logical)))
        return std::nullopt;
      return source;
    };
    // Links may point forward (loop back edges), so repeat until stable.
    // Only roots are re-parented, onto other roots, so no cycle can form.
    for (bool changed = true; changed;) {
      changed = false;
      for (const auto &node : nodes)
        if (parent[node->id] == node->id)
          if (auto source = forwardedSource(*node)) {
            parent[node->id] = *source;
            changed = true;
          }
    }
    for (unsigned id = 0; id != nodes.size(); ++id)
      parent[id] = find(id);
    for (const auto &node : nodes) {
      if (parent[node->id] != node->id)
        continue;
      bool remapped = false;
      for (LaneLink &link : node->links) {
        remapped |= parent[link.source] != link.source;
        link.source = parent[link.source];
      }
      if (!remapped)
        continue;
      // A full identity of the node itself adds nothing to its join.
      llvm::erase_if(node->links, [&](const LaneLink &link) {
        return link.source == node->id && isForward(*node, link);
      });
      normalize(node->links);
    }
    return parent;
  }
  unsigned addNode(uint64_t width, Block *block, Operation *operation) {
    unsigned id = nodes.size();
    nodes.push_back(std::make_unique<LaneNode>(
        LaneNode{id, width, block, operation, {}, {}, {}}));
    return id;
  }
  struct Record {
    Operation *operation;
    unsigned node;
    Range output;
    bool exact;
    uint64_t lane;
  };
  sim::SimFuncOp function;
  const DenseMap<uint64_t, ArrayRef<Range>> &rootLanes;
  LogicalLanes logical;
  const Liveness &liveness;
  DenseMap<Value, unsigned> ordinals;
  DenseMap<Block *, llvm::SetVector<Block *>> controllers;
  DenseMap<Block *, llvm::SetVector<Value>> implicitLive;
  DenseMap<Block *, SmallVector<std::pair<Value, uint64_t>>> liveIns;
  DenseSet<std::tuple<Block *, ArrayAttr, Block *, ArrayAttr, void *>>
      emittedEdges;
  DenseSet<Block *> singleContextBlocks;
  ArrayAttr emptyContext;
  Block *currentBlock = nullptr;
  ArrayAttr currentContext;
  DenseMap<std::pair<Block *, ArrayAttr>,
           std::unique_ptr<DenseMap<Value, unsigned>>>
      valueScopes;
  DenseMap<Value, unsigned> *currentScope = nullptr;
  DenseMap<uint64_t, unsigned> incoming;
  DenseMap<std::pair<Operation *, ArrayAttr>, unsigned> stores;
  SmallVector<std::unique_ptr<LaneNode>> nodes;
  llvm::SetVector<unsigned> sinks, finalSinks;
  SmallVector<Record> records;
};

void ActivationValueGraph::pure(Operation *op,
                                const DenseMap<Value, Attribute> &constants) {
  auto put = [&](Value result, LaneLinks links) {
    if (!constants.lookup(result))
      append(valueNode(result), links);
  };
  auto slice = [&](Value input, Value result, uint64_t low, uint64_t width) {
    auto span = sim::getProvenanceSpan(input.getType());
    if (!span || low > *span || width > *span - low ||
        width != valueSpan(result))
      return false;
    put(result,
        {{0, width, valueNode(input), low, width, LaneMapping::Identity}});
    return true;
  };
  auto insert = [&](Value input, Value replacement, Value result, uint64_t low,
                    uint64_t width) {
    auto span = sim::getProvenanceSpan(input.getType());
    if (!span || *span != valueSpan(result) || low > *span ||
        width > *span - low || width != valueSpan(replacement))
      return false;
    LaneLinks links;
    if (low)
      links.push_back(
          {0, low, valueNode(input), 0, low, LaneMapping::Identity});
    uint64_t end = low + width;
    if (end < *span)
      links.push_back({end, *span - end, valueNode(input), end, *span - end,
                       LaneMapping::Identity});
    links.push_back(
        {low, width, valueNode(replacement), 0, width, LaneMapping::Identity});
    put(result, std::move(links));
    return true;
  };
  auto subelement = [&](Type type, int64_t index) {
    return index >= 0 && uint64_t(index) <= UINT_MAX
               ? sim::getAggregateProvenanceSubelement(type, index)
               : std::optional<std::pair<uint64_t, uint64_t>>{};
  };
  auto arrayElement = [&](Type type, IntegerAttr index) {
    std::optional<std::pair<uint64_t, uint64_t>> result;
    if (!index || !index.getValue().isSignedIntN(64))
      return result;
    int64_t logical = index.getInt();
    TypeSwitch<Type>(type).Case<sim::PackedArrayType, sim::UnpackedArrayType>(
        [&](auto array) {
          int64_t left = array.getLeft(), right = array.getRight();
          if (logical < std::min(left, right) ||
              logical > std::max(left, right))
            return;
          uint64_t ordinal = left <= right ? uint64_t(logical) - uint64_t(left)
                                           : uint64_t(left) - uint64_t(logical);
          if (ordinal <= UINT_MAX)
            result = sim::getAggregateProvenanceSubelement(type, ordinal);
        });
    return result;
  };
  auto invalidArrayIndex = [&](Value index, Type type) {
    auto integer = knownIndex(index, constants);
    if (integer)
      return TypeSwitch<Type, bool>(type)
          .Case<sim::PackedArrayType, sim::UnpackedArrayType>([&](auto array) {
            return !integer.getValue().isSignedIntN(64) ||
                   integer.getInt() <
                       std::min(array.getLeft(), array.getRight()) ||
                   integer.getInt() >
                       std::max(array.getLeft(), array.getRight());
          })
          .Default(false);
    auto planes = dyn_cast_or_null<ArrayAttr>(constants.lookup(index));
    return planes && planes.size() == 2 && isa<IntegerAttr>(planes[1]) &&
           !cast<IntegerAttr>(planes[1]).getValue().isZero();
  };
  if (auto extract = dyn_cast<sim::SimAggregateExtractOp>(op)) {
    auto child = subelement(extract.getInput().getType(), extract.getIndex());
    if (child && slice(extract.getInput(), extract.getResult(), child->first,
                       child->second))
      return;
  } else if (auto update = dyn_cast<sim::SimAggregateInsertOp>(op)) {
    auto child = subelement(update.getInput().getType(), update.getIndex());
    if (child && insert(update.getInput(), update.getReplacement(),
                        update.getResult(), child->first, child->second))
      return;
  } else if (auto extract = dyn_cast<sim::SimArrayDynExtractOp>(op)) {
    if (invalidArrayIndex(extract.getIndex(), extract.getInput().getType())) {
      put(extract.getResult(), {});
      return;
    }
    auto index = knownIndex(extract.getIndex(), constants);
    auto child = arrayElement(extract.getInput().getType(), index);
    if (child && slice(extract.getInput(), extract.getResult(), child->first,
                       child->second))
      return;
  } else if (auto update = dyn_cast<sim::SimArrayDynInsertOp>(op)) {
    if (invalidArrayIndex(update.getIndex(), update.getInput().getType())) {
      put(update.getResult(),
          {{0, valueSpan(update.getResult()), valueNode(update.getInput()), 0,
            valueSpan(update.getInput()), LaneMapping::Identity}});
      return;
    }
    auto index = knownIndex(update.getIndex(), constants);
    auto child = arrayElement(update.getInput().getType(), index);
    if (child && insert(update.getInput(), update.getReplacement(),
                        update.getResult(), child->first, child->second))
      return;
  } else if (isa<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp,
                 sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp>(op) ||
             (isa<arith::TruncIOp>(op) &&
              cast<arith::TruncIOp>(op).getOverflowFlags() ==
                  arith::IntegerOverflowFlags::none)) {
    if (slice(op->getOperand(0), op->getResult(0), 0,
              valueSpan(op->getResult(0))))
      return;
  } else if (auto extract = dyn_cast<sim::SimLogicExtractOp>(op)) {
    if (slice(extract.getInput(), extract.getResult(), extract.getLowBit(),
              valueSpan(extract.getResult())))
      return;
  } else if (isa<sim::SimLogicDynExtractOp, sim::SimBitsDynExtractOp>(op)) {
    auto index = knownIndex(op->getOperand(1), constants);
    if (index && !index.getValue().isNegative() &&
        index.getValue().getActiveBits() <= 64 &&
        slice(op->getOperand(0), op->getResult(0),
              index.getValue().getZExtValue(), valueSpan(op->getResult(0))))
      return;
  } else if (auto update = dyn_cast<sim::SimLogicInsertOp>(op)) {
    if (insert(update.getInput(), update.getReplacement(), update.getResult(),
               update.getLowBit(), valueSpan(update.getReplacement())))
      return;
  } else if (auto update = dyn_cast<sim::SimBitsDynInsertOp>(op)) {
    // Fixed packed-field writes in integer storage use the dynamic operation
    // with a constant selector. Preserve the same overwrite proof as the
    // four-state insertion above; otherwise old output lanes become false
    // activation inputs and can introduce spurious publication cycles.
    auto index = knownIndex(update.getLowBit(), constants);
    if (index && !index.getValue().isNegative() &&
        index.getValue().getActiveBits() <= 64 &&
        insert(update.getInput(), update.getReplacement(), update.getResult(),
               index.getValue().getZExtValue(),
               valueSpan(update.getReplacement())))
      return;
  } else if (auto extend = dyn_cast<arith::ExtUIOp>(op)) {
    if (isa<IntegerType>(extend.getIn().getType())) {
      uint64_t width = valueSpan(extend.getIn());
      put(extend.getResult(),
          {{0, width, valueNode(extend.getIn()), 0, width,
            LaneMapping::Identity}});
      return;
    }
  } else if (isa<arith::ShLIOp, arith::ShRUIOp>(op)) {
    Value input = op->getOperand(0), result = op->getResult(0);
    auto amount = knownIndex(op->getOperand(1), constants);
    bool hasPoisonFlags =
        isa<arith::ShLIOp>(op)
            ? cast<arith::ShLIOp>(op).getOverflowFlags() !=
                  arith::IntegerOverflowFlags::none
            : cast<arith::ShRUIOp>(op).getIsExact();
    // Overflow/exact promises make discarded bits observable through poison.
    if (!hasPoisonFlags && isa<IntegerType>(input.getType()) && amount &&
        !amount.getValue().isNegative() &&
        amount.getValue().getActiveBits() <= 64) {
      uint64_t width = valueSpan(input);
      uint64_t shift = amount.getValue().getZExtValue();
      // Oversized arith shifts are poison, not zero-filling projections.
      // Keep their conservative fallback along with unproved selectors.
      if (shift < width) {
        uint64_t low = isa<arith::ShLIOp>(op) ? shift : 0;
        uint64_t sourceLow = isa<arith::ShRUIOp>(op) ? shift : 0;
        put(result, {{low, width - shift, valueNode(input), sourceLow,
                      width - shift, LaneMapping::Identity}});
        return;
      }
    }
  } else if (isa<arith::AndIOp, arith::OrIOp, arith::XOrIOp>(op) &&
             isa<IntegerType>(op->getResult(0).getType())) {
    // Integer concatenations and packed projections lower to extension,
    // shifts and bitwise operations. Each bitwise result lane depends only
    // on the corresponding operand lanes, independently of their values.
    uint64_t width = valueSpan(op->getResult(0));
    LaneLinks links;
    for (Value operand : op->getOperands())
      links.push_back(
          {0, width, valueNode(operand), 0, width, LaneMapping::Identity});
    put(op->getResult(0), std::move(links));
    return;
  }
  for (Value result : op->getResults()) {
    LaneLinks links;
    for (Value operand : op->getOperands())
      links.push_back(valueLink(operand, valueSpan(result)));
    put(result, std::move(links));
  }
}

struct Memory {
  DenseMap<Value, Attribute> constants;
  HandleFacts references;
  ReachingMap reaching;
  RangeSet assigned, exposed, written, repeated;
  /// Joins `other` into this state in place. Returns whether any fact
  /// changed, which is exactly when the joined state differs from the old one.
  bool join(const Memory &other) {
    bool changed = false;
    SmallVector<Value> forget;
    for (auto [value, constant] : constants)
      if (other.constants.lookup(value) != constant)
        forget.push_back(value);
    for (Value value : forget)
      constants.erase(value);
    changed |= !forget.empty();
    forget.clear();
    for (auto &[value, reference] : references) {
      auto found = other.references.find(value);
      if (found == other.references.end() || !(reference == found->second))
        forget.push_back(value);
    }
    // Losing an exact reference falls back to the shared sparse may facts.
    for (Value value : forget)
      references.erase(value);
    changed |= !forget.empty();
    // Only definition links are carried here; sparse SSA lattices are not.
    changed |= reaching.join(other.reaching);
    changed |= assigned.intersectWith(other.assigned);
    changed |= exposed.uniteWith(other.exposed);
    changed |= written.uniteWith(other.written);
    changed |= repeated.uniteWith(other.repeated);
    return changed;
  }
};
class MemoryState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  llvm::MapVector<ArrayAttr, Memory> rows;
  llvm::SetVector<ArrayAttr> pending;
  ChangeResult join(ArrayAttr context, Memory &&incoming) {
    // try_emplace leaves `incoming` intact when the row already exists.
    auto [found, inserted] = rows.try_emplace(context, std::move(incoming));
    if (!inserted && !found->second.join(incoming))
      return ChangeResult::NoChange;
    pending.insert(context);
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override {
    os << "activation partitions=" << rows.size();
  }
};
/// A finite, read-only call closure. Formal ranges remain relative to the
/// formal until each call site maps them to its actual handle. Local slots
/// belong to the callee and do not escape. A recursion sentinel rejects cycles.
struct ReadOnlyCallSummary {
  bool valid = false;
  SmallVector<HandleFact> reads;
};
std::optional<HandleFact> mapCallRead(const HandleFact &read,
                                      const HandleFact &actual) {
  if (!actual.width || actual.low > actual.rootWidth ||
      actual.width > actual.rootWidth - actual.low || !read.width ||
      read.low > read.rootWidth || read.width > read.rootWidth - read.low)
    return std::nullopt;
  HandleFact mapped = actual;
  if (actual.dynamic || actual.width != read.rootWidth)
    mapped.dynamic = true;
  else {
    mapped.low += read.low;
    mapped.width = read.width;
    mapped.dynamic = read.dynamic;
  }
  return mapped;
}
ReadOnlyCallSummary
summarizeReadOnlyCall(sim::SimFuncOp function, Operation *symbols,
                      SymbolTableCollection &symbolTables,
                      const HandleDataflowAnalysis &provenance,
                      DenseMap<Operation *, ReadOnlyCallSummary> &cache) {
  auto [found, inserted] = cache.try_emplace(function);
  if (!inserted)
    return found->second;
  if (!function || function.isExternal() ||
      !hasFiniteControlFlow(function.getBody()))
    return {};
  auto handleResult = provenance.analyze(function);
  const auto &handles = handleResult.facts;
  ReadOnlyCallSummary summary;
  summary.valid = true;
  auto add = [&](HandleFact fact) {
    if (fact.resource == schedule::ComputeResourceKind::Local)
      return;
    if ((fact.resource != schedule::ComputeResourceKind::Storage &&
         fact.resource != schedule::ComputeResourceKind::Net) ||
        (!fact.descriptor && !fact.formal) || !fact.width ||
        fact.low > fact.rootWidth || fact.width > fact.rootWidth - fact.low) {
      summary.valid = false;
      return;
    }
    if (!llvm::is_contained(summary.reads, fact))
      summary.reads.push_back(fact);
  };
  function.walk([&](Operation *op) {
    if (!summary.valid || op == function.getOperation() ||
        !handleResult.isExecutable(op->getBlock()) ||
        isa<sim::SimReturnOp, cf::BranchOp, cf::CondBranchOp, cf::SwitchOp>(op))
      return;
    if (op->getNumRegions()) {
      summary.valid = false;
      return;
    }
    if (auto alloc = dyn_cast<sim::SimRefAllocOp>(op))
      summary.valid = isPlainValueType(alloc.getType().getElementType());
    else if (isa<sim::SimRefLoadOp, sim::SimNetReadOp>(op)) {
      auto fact = handles.find(op->getOperand(0));
      if (fact == handles.end())
        summary.valid = false;
      else
        add(fact->second);
    } else if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
      summary.valid = isLocalSlotReference(store.getReference());
    else if (auto call = dyn_cast<sim::SimCallOp>(op)) {
      auto nested = summarizeReadOnlyCall(
          symbolTables.lookupNearestSymbolFrom<sim::SimFuncOp>(
              symbols, call.getCalleeAttr()),
          symbols, symbolTables, provenance, cache);
      summary.valid = nested.valid;
      for (const auto &read : nested.reads) {
        if (read.descriptor) {
          add(read);
          continue;
        }
        auto actual = *read.formal < call.getNumOperands()
                          ? handles.find(call.getOperand(*read.formal))
                          : handles.end();
        auto mapped = actual != handles.end()
                          ? mapCallRead(read, actual->second)
                          : std::nullopt;
        if (!mapped)
          summary.valid = false;
        else
          add(*mapped);
      }
    } else
      summary.valid = isMemoryEffectFree(op);
  });
  cache[function] = summary;
  return summary;
}

class ActivationDataflow final : public DataFlowAnalysis {
public:
  ActivationDataflow(DataFlowSolver &solver, sim::SimFuncOp function,
                     const HandleDataflowResult &handleResult,
                     const HandleDataflowAnalysis &provenance,
                     unsigned maximumPartitions, ActivationValueGraph *values,
                     Operation *symbols, bool inputsOnly,
                     ValueLiveness &liveness)
      : DataFlowAnalysis(solver), function(function), liveness(liveness),
        handleResult(handleResult), handles(handleResult.facts),
        provenance(provenance), maximumPartitions(maximumPartitions),
        values(values), symbols(symbols), inputsOnly(inputsOnly),
        loops(analyzeActivationBoundedLoops(function.getBody())) {
    // A fused activation can contain hundreds of proven loops. Index loop
    // membership and latch conditions once so each CFG edge only touches the
    // loops that contain its target.
    for (auto [index, loop] : llvm::enumerate(loops)) {
      for (Block *member : loop.blocks)
        blockLoops[member].push_back(index);
      loopConditions[loop.condition.getOperation()].push_back(index);
    }
    // A dependent loop can begin on a break path, outside the natural loop
    // that computed its initializer or bound. Preserve live control operands
    // across intervening blocks, including landing blocks inserted by
    // activation slicing. Otherwise different outer iterations merge before the
    // inner header and erase the exact initializer. Track the expression's
    // operands too, so moving a pure calculation across an edge has the same
    // result.
    llvm::SetVector<Value> controlValues;
    for (const auto &loop : loops) {
      if (loop.initValue)
        controlValues.insert(loop.initValue);
      if (loop.limitValue)
        controlValues.insert(loop.limitValue);
    }
    for (unsigned i = 0; i < controlValues.size(); ++i)
      if (Operation *op = controlValues[i].getDefiningOp())
        if (isMemoryEffectFree(op))
          controlValues.insert(op->operand_begin(), op->operand_end());
    if (!controlValues.empty()) {
      for (Block &block : function.getBody())
        for (Value value : controlValues)
          if (liveness.isLiveIn(value, &block))
            liveControlValues[&block].push_back(value);
    }
    if (values) {
      DenseSet<Block *> singleContext;
      for (Block &block : function.getBody())
        if (!blockLoops.count(&block) && !liveControlValues.count(&block))
          singleContext.insert(&block);
      values->setSingleContextBlocks(std::move(singleContext));
    }
  }
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
    return propagate(&function.getBody().front(), initial);
  }
  LogicalResult visit(ProgramPoint *point) override {
    Block *block = point->getBlock();
    // A new induction partition invalidates only that row. Revisiting every
    // earlier iteration on each worklist notification makes a finite loop
    // quadratic in its trip count. Snapshot changed rows before transfer,
    // since a backedge can enqueue another row while this block is visited.
    auto *input = getOrCreate<MemoryState>(point);
    SmallVector<std::pair<ArrayAttr, Memory>, 0> rows;
    for (ArrayAttr context : input->pending)
      rows.emplace_back(context, input->rows.at(context));
    input->pending.clear();
    for (auto &[context, incoming] : rows) {
      if (values)
        values->enter(block, context);
      Memory state = std::move(incoming);
      for (Operation &op : block->without_terminator())
        if (failed(transfer(&op, state))) {
          if (!unsupported)
            unsupported = &op;
          return failure();
        }
      Operation *term = block->getTerminator();
      if (values) {
        if (auto branch = dyn_cast<cf::CondBranchOp>(term))
          values->expose(branch.getCondition());
        else if (auto branch = dyn_cast<cf::SwitchOp>(term))
          values->expose(branch.getFlag());
        else if (isa<sim::SimReturnOp>(term))
          for (Value operand : term->getOperands())
            values->expose(operand, /*isReturn=*/true);
      }
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
        for (unsigned index : loopConditions.lookup(branch.getOperation()))
          if (const auto &loop = loops[index]; true) {
            auto iv = knownIndex(loop.induction, state.constants);
            if (!iv)
              return failure();
            auto bound = loop.limitValue
                             ? dyn_cast_or_null<IntegerAttr>(
                                   state.constants.lookup(loop.limitValue))
                             : IntegerAttr::get(
                                   IntegerType::get(function.getContext(),
                                                    loop.limit.getBitWidth()),
                                   loop.limit);
            // A contextual outer induction gives the exact bound. If it is
            // unavailable, both edges remain feasible until the conservative
            // envelope itself rules out another iteration.
            if (bound || !arith::applyCmpPredicate(loop.predicate,
                                                   iv.getValue(), loop.limit))
              condition =
                  IntegerAttr::get(IntegerType::get(function.getContext(), 1),
                                   bound && arith::applyCmpPredicate(
                                                loop.predicate, iv.getValue(),
                                                bound.getValue()));
          }
        if ((!condition || !condition.getValue().isZero()) &&
            failed(branchTo(branch.getTrueDest(), branch.getTrueDestOperands(),
                            state, branch.getCondition())))
          return failure();
        if ((!condition || condition.getValue().isZero()) &&
            failed(branchTo(branch.getFalseDest(),
                            branch.getFalseDestOperands(), state,
                            branch.getCondition())))
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
                                branch.getCaseOperands(i), state,
                                branch.getFlag())))
              return failure();
          }
        if ((!condition || !matched) &&
            failed(branchTo(branch.getDefaultDestination(),
                            branch.getDefaultOperands(), state,
                            branch.getFlag())))
          return failure();
      } else
        return failure();
    }
    return success();
  }
  std::optional<Memory> exit;
  DenseMap<Operation *, Ranges> reads, writes;
  DenseSet<Operation *> privateReads;
  unsigned partitions = 0;
  /// A private slot whose lanes reaching values cannot follow.
  bool untrackedLocalSlots = false;
  /// An access through a reference merged from several roots.
  bool ambiguousReferences = false;
  bool mixedPrivateWrites = false;
  Operation *unsupported = nullptr;
  DenseMap<Operation *, uint64_t> localRoots;

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
  LogicalResult propagate(Block *block, Memory state,
                          std::optional<ValueRange> operands = std::nullopt,
                          Value condition = {}) {
    if (!handleResult.isExecutable(block))
      return success();
    // The key holds one induction value per loop containing the block, then
    // the live control values. Rows are only compared within one block, whose
    // loop membership is fixed.
    SmallVector<Attribute> key;
    for (unsigned index : blockLoops.lookup(block)) {
      Attribute value = knownIndex(loops[index].induction, state.constants);
      if (!isa_and_nonnull<IntegerAttr>(value))
        return failure();
      key.push_back(value);
    }
    for (Value operand : liveControlValues.lookup(block)) {
      Attribute value = state.constants.lookup(operand);
      key.push_back(value ? value : UnitAttr::get(function.getContext()));
    }
    auto context = ArrayAttr::get(function.getContext(), key);
    if (values && operands)
      values->edge(block, context, *operands, condition);
    auto *target = getOrCreate<MemoryState>(getProgramPointBefore(block));
    if (!target->rows.contains(context) && ++partitions > maximumPartitions)
      return failure();
    propagateIfChanged(target, target->join(context, std::move(state)));
    return success();
  }
  LogicalResult branchTo(Block *target, ValueRange values, Memory state,
                         Value condition = {}) {
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
    // Context-specific facts only need to cross edges where their SSA value
    // is live. Carrying every earlier loop's constants and projected handles
    // through a large fused activation makes the dense state quadratic in
    // body size. Static handle facts already live in the immutable summary.
    DenseSet<Value> induction;
    for (unsigned index : blockLoops.lookup(target)) {
      const auto &loop = loops[index];
      induction.insert(loop.induction);
      if (loop.initValue)
        induction.insert(loop.initValue);
      if (loop.limitValue)
        induction.insert(loop.limitValue);
    }
    auto needed = [&](Value value) {
      auto argument = dyn_cast<BlockArgument>(value);
      return (argument && argument.getOwner() == target) ||
             liveness.isLiveIn(value, target) || induction.contains(value);
    };
    for (auto it = state.constants.begin(); it != state.constants.end();) {
      if (!needed(it->first))
        state.constants.erase(it++);
      else
        ++it;
    }
    for (auto it = state.references.begin(); it != state.references.end();) {
      auto known = handles.find(it->first);
      if (!needed(it->first) ||
          (known != handles.end() && known->second == it->second))
        state.references.erase(it++);
      else
        ++it;
    }
    return propagate(target, std::move(state), values, condition);
  }
  void foldConstants(Operation *op, Memory &state) {
    for (Value value : op->getResults())
      state.constants.erase(value);
    if (!isMemoryEffectFree(op))
      return;
    // Packed wrappers preserve the two constant planes, even though their
    // result is not a scalar IntegerType/LogicType accepted by fold hooks.
    if (isa<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(op)) {
      if (Attribute value = state.constants.lookup(op->getOperand(0)))
        state.constants[op->getResult(0)] = value;
      return;
    }
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
  /// Bit offset of a projection within its parent reference, when constant.
  std::optional<uint64_t> projectionOffset(Operation *op,
                                           const Memory &state) const {
    auto parentType = dyn_cast<sim::RefType>(op->getOperand(0).getType());
    if (!parentType)
      return std::nullopt;
    std::optional<uint64_t> offset;
    if (auto element = dyn_cast<sim::SimRefArrayElementOp>(op)) {
      auto index = knownIndex(element->getOperand(1), state.constants);
      if (!index || !index.getValue().isSignedIntN(64))
        return std::nullopt;
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
      auto index = knownIndex(op->getOperand(1), state.constants);
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
        if (!child || child->first > UINT64_MAX - *offset)
          return std::nullopt;
        *offset += child->first;
        type = sim::getAggregateElementType(type, index);
      }
    }
    return offset;
  }
  /// Apply one projection to an exact parent fact; a projection that cannot
  /// be resolved keeps the parent's range as a dynamic selection.
  HandleFact projectFact(Operation *op, HandleFact parent,
                         const Memory &state) const {
    auto parentSpan = sim::getProvenanceSpan(op->getOperand(0).getType());
    auto span = sim::getProvenanceSpan(op->getResult(0).getType());
    auto offset = projectionOffset(op, state);
    if (parent.dynamic || !parentSpan || parent.width != *parentSpan ||
        !offset || !span || *offset > *parentSpan ||
        *span > *parentSpan - *offset) {
      parent.dynamic = true;
      return parent;
    }
    parent.low += *offset;
    parent.width = *span;
    return parent;
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
    HandleFact projected = projectFact(op, *parent, state);
    if (!projected.dynamic)
      state.references[op->getResult(0)] = projected;
  }
  /// Design roots a reference may select. A reference merged by a select or
  /// at a CFG join resolves through every incoming reference, and projections
  /// apply to each candidate.
  SmallVector<HandleFact> candidates(Value value, const Memory &state) const {
    SmallVector<HandleFact> result;
    DenseSet<Value> visiting;
    if (!collectCandidates(value, state, visiting, result))
      return {};
    return result;
  }
  bool collectCandidates(Value value, const Memory &state,
                         DenseSet<Value> &visiting,
                         SmallVectorImpl<HandleFact> &result) const {
    auto canonical = [](const std::optional<HandleFact> &fact) {
      return fact && fact->descriptor &&
             ActivationMemoryAnalysis::getRoot(fact->resource,
                                               *fact->descriptor) &&
             fact->width && fact->low <= fact->rootWidth &&
             fact->width <= fact->rootWidth - fact->low;
    };
    auto add = [&](const HandleFact &fact) {
      if (!llvm::is_contained(result, fact))
        result.push_back(fact);
      return true;
    };
    if (auto fact = reference(value, state)) {
      if (canonical(fact))
        return add(*fact);
      if (fact->resource == schedule::ComputeResourceKind::Local) {
        auto slot = localRoots.find(getLocalSlot(value));
        if (slot != localRoots.end() && fact->width &&
            fact->low <= fact->rootWidth &&
            fact->width <= fact->rootWidth - fact->low) {
          fact->descriptor = slot->second;
          return add(*fact);
        }
      }
    }
    // A cyclic phi adds no root of its own.
    if (!visiting.insert(value).second)
      return true;
    if (Operation *definition = value.getDefiningOp()) {
      if (auto select = dyn_cast<SelectLikeOpInterface>(definition))
        return collectCandidates(select.getTrueValue(), state, visiting,
                                 result) &&
               collectCandidates(select.getFalseValue(), state, visiting,
                                 result);
      if (!isa<sim::SimRefExtractOp, sim::SimRefDynExtractOp,
               sim::SimRefArrayElementOp, sim::SimRefSubelementOp>(definition))
        return false;
      SmallVector<HandleFact> parents;
      if (!collectCandidates(definition->getOperand(0), state, visiting,
                             parents))
        return false;
      for (const auto &parent : parents)
        add(projectFact(definition, parent, state));
      return true;
    }
    auto argument = cast<BlockArgument>(value);
    Block *block = argument.getOwner();
    if (block->isEntryBlock())
      return false;
    for (auto it = block->pred_begin(); it != block->pred_end(); ++it) {
      auto branch = dyn_cast<BranchOpInterface>((*it)->getTerminator());
      if (!branch)
        return false;
      Value incoming = branch.getSuccessorOperands(it.getSuccessorIndex())
                           .getForwardedOperands()[argument.getArgNumber()];
      if (incoming == argument)
        continue;
      auto found = handles.find(incoming);
      if (found != handles.end() && canonical(found->second)) {
        add(found->second);
        continue;
      }
      if (!collectCandidates(incoming, state, visiting, result))
        return false;
    }
    return true;
  }
  ReachingDefinitions incomingCell(uint64_t root, const HandleFact &fact,
                                   const Memory &state) {
    ReachingDefinitions cell{
        fact.rootWidth, values->incomingNode(root, fact.rootWidth), {}};
    // Inputs mode needs a union over every publication and control decision.
    // Every canonical store is already a sink, so its input dependencies are
    // included without carrying its definition through all later CFG blocks.
    // Reads of definitely written lanes add no new incoming dependency.
    // Private scratch still uses reaching definitions because its stores are
    // not publications. Partial and conditional writes retain exposed lanes.
    for (auto range :
         state.assigned.subtractFrom({{root, fact.low, fact.width}}))
      cell.links.push_back({range.low, range.width, cell.incoming, range.low,
                            range.width, LaneMapping::Identity});
    return cell;
  }
  LogicalResult transfer(Operation *op, Memory &state) {
    if (op->getNumRegions())
      return failure();
    foldConstants(op, state);
    projectReference(op, state);
    Operation *load =
        isa<sim::SimRefLoadOp, sim::SimNetReadOp>(op) ? op : nullptr;
    Operation *store =
        isa<sim::SimRefStoreOp, sim::SimDriverDriveOp>(op) ? op : nullptr;
    if (auto call = dyn_cast<sim::SimCallOp>(op)) {
      if (!symbols)
        return failure();
      auto summary = summarizeReadOnlyCall(
          symbolTables.lookupNearestSymbolFrom<sim::SimFuncOp>(
              symbols, call.getCalleeAttr()),
          symbols, symbolTables, provenance, callSummaries);
      if (!summary.valid)
        return failure();
      if (values)
        values->pure(op, state.constants);
      for (auto read : summary.reads) {
        Value actualValue;
        if (!read.descriptor) {
          if (!read.formal || *read.formal >= call.getNumOperands())
            return failure();
          actualValue = call.getOperand(*read.formal);
          auto actual = reference(actualValue, state);
          auto mapped = actual ? mapCallRead(read, *actual) : std::nullopt;
          if (!mapped)
            return failure();
          read = *mapped;
        }
        std::optional<uint64_t> root;
        if (read.resource == schedule::ComputeResourceKind::Local) {
          auto slot = localRoots.find(getLocalSlot(actualValue));
          if (values && slot == localRoots.end())
            return failure();
          if (!values)
            continue;
          root = slot->second;
        } else {
          root = read.descriptor ? ActivationMemoryAnalysis::getRoot(
                                       read.resource, *read.descriptor)
                                 : std::nullopt;
          if (!root)
            return failure();
          Ranges access{{*root, read.low, read.width}};
          unite(reads[op], access);
          state.exposed.unite(state.assigned.subtractFrom(access));
        }
        if (!values)
          continue;
        ReachingDefinitions incoming;
        const ReachingDefinitions *cell;
        if (inputsOnly &&
            read.resource != schedule::ComputeResourceKind::Local) {
          incoming = incomingCell(*root, read, state);
          cell = &incoming;
        } else {
          cell = &trackedCell(state, *root, read.rootWidth);
        }
        auto links = projectLinks(cell->links, read.low, read.width);
        for (Value result : call.getResults()) {
          auto dependencies = links;
          for (auto &link : dependencies) {
            link.low = 0;
            link.width = valueSpan(result);
            link.mapping = LaneMapping::AllToAll;
          }
          values->append(values->valueNode(result), dependencies);
        }
      }
      return success();
    }
    if (auto alloc = dyn_cast<sim::SimRefAllocOp>(op)) {
      // A fresh private slot; its accesses are not design reads or writes.
      // Reaching values follow it as a pseudo-root seeded by its initializer.
      if (!isPlainValueType(alloc.getType().getElementType()))
        return failure();
      auto [root, inserted] = localRoots.try_emplace(
          alloc, (uint64_t{1} << 63) | localRoots.size());
      if (values) {
        auto span = sim::getProvenanceSpan(alloc.getType());
        if (!span) {
          untrackedLocalSlots = true;
          return success();
        }
        ReachingDefinitions cell{
            *span, values->valueNode(alloc.getInitialValue()), {}};
        cell.links.push_back(cell.identity());
        state.reaching.set(root->second, std::move(cell));
      }
      return success();
    }
    if (!load && !store) {
      if (values)
        values->pure(op, state.constants);
      return success(isMemoryEffectFree(op));
    }
    Value value = store ? ActivationMemoryAnalysis::getWriteHandle(store)
                        : load->getOperand(0);
    auto fact = reference(value, state);
    if (fact && fact->resource == schedule::ComputeResourceKind::Local) {
      if (load)
        privateReads.insert(load);
      if (!values)
        return success();
      auto slot = localRoots.find(getLocalSlot(value));
      const ReachingDefinitions *cell =
          slot == localRoots.end() ? nullptr
                                   : state.reaching.find(slot->second);
      if (!cell || !fact->width || fact->low > cell->width ||
          fact->width > cell->width - fact->low) {
        untrackedLocalSlots = true;
        if (load)
          values->pure(op, state.constants);
        return success();
      }
      auto span = sim::getProvenanceSpan(value.getType());
      bool exact = !fact->dynamic && span && fact->width == *span;
      if (load)
        readReaching(*cell, load, value, *fact, exact);
      else
        writeReaching(*state.reaching.write(slot->second), store, *fact, exact,
                      /*publication=*/{});
      return success();
    }
    // A drive is a write only where this driver is the sole strong
    // contribution (IEEE 1800-2023 6.6.1); any other resolution stays an
    // unsupported effect. A dynamic selector may leave the driver's own
    // partition on a sparse net, so it keeps the single-driver requirement.
    if (isa<sim::SimDriverDriveOp>(op) &&
        (!fact || !fact->descriptor ||
         fact->resource != schedule::ComputeResourceKind::Net ||
         !(provenance.hasIsolatedSingleDriver(*fact->descriptor) ||
           (!fact->dynamic &&
            provenance.hasExclusiveDriverSlice(*fact->descriptor, fact->low,
                                               fact->width)))))
      return failure();
    auto root = fact && fact->descriptor
                    ? ActivationMemoryAnalysis::getRoot(fact->resource,
                                                        *fact->descriptor)
                    : std::nullopt;
    if (!root || !fact->width || fact->low > fact->rootWidth ||
        fact->width > fact->rootWidth - fact->low) {
      // A merged handle may select storage, a resolved net or a private slot.
      // Loads follow all possible reaching versions and the selector.
      // A merged store is never a definite write. Inputs mode can weakly
      // update each private candidate and expose every canonical publication;
      // a final per-lane publication proof still requires an exact target.
      auto possible = candidates(value, state);
      if (possible.empty())
        return failure();
      ambiguousReferences |= bool(store) && !inputsOnly;
      Ranges access;
      bool hasPrivateCandidate = false;
      for (const auto &candidate : possible) {
        bool local = candidate.resource == schedule::ComputeResourceKind::Local;
        uint64_t root = local ? *candidate.descriptor
                              : *ActivationMemoryAnalysis::getRoot(
                                    candidate.resource, *candidate.descriptor);
        if (local) {
          hasPrivateCandidate = true;
          if (load)
            privateReads.insert(load);
        } else
          access.push_back({root, candidate.low, candidate.width});
        if (!values)
          continue;
        if (store) {
          if (!inputsOnly)
            continue;
          if (!local) {
            values->publication(store, {root, candidate.low, candidate.width},
                                /*exact=*/false);
            continue;
          }
          ReachingDefinitions *cell = state.reaching.write(root);
          if (!cell)
            return failure();
          writeReaching(*cell, store, candidate, /*exact=*/false, {});
          continue;
        }
        auto span = sim::getProvenanceSpan(value.getType());
        bool exact = !candidate.dynamic && span && candidate.width == *span;
        if (inputsOnly && !local) {
          auto cell = incomingCell(root, candidate, state);
          readReaching(cell, load, value, candidate, exact);
          continue;
        }
        if (local && !state.reaching.find(root))
          return failure();
        readReaching(trackedCell(state, root, candidate.rootWidth), load, value,
                     candidate, exact);
      }
      normalize(access);
      mixedPrivateWrites |= bool(store) && hasPrivateCandidate && !access.empty();
      if (load) {
        if (!access.empty())
          unite(reads[op], access);
        state.exposed.unite(state.assigned.subtractFrom(access));
      } else {
        // An empty canonical range is a private-only access, not a design
        // publication. Slicing uses inventory membership to retain its local
        // definitions; an empty entry would let it erase a selected store.
        if (!access.empty())
          unite(writes[op], access);
        state.repeated.unite(state.written.intersectWith(access));
        state.written.unite(access);
      }
      return success();
    }
    auto span = sim::getProvenanceSpan(value.getType());
    bool exact = !fact->dynamic && span && fact->width == *span;
    Type accessedType =
        store ? ActivationMemoryAnalysis::getStoredValue(store).getType()
              : load->getResult(0).getType();
    Ranges access =
        logical.access(accessedType, {*root, fact->low, fact->width}, exact);
    // Port-connected nets merge into one simulated net (IEEE 1800-2023
    // 23.3.3.7), so a drive of an exclusive slice (6.6.1, Table 6-2) updates
    // every collapsed member. Record the write on each alias as the compute
    // graph expands producers, so ordering, self-feedback through a port and
    // exclusive-writer proofs see the same roots; a later read of an alias in
    // this activation sees this value.
    SmallVector<HandleDataflowAnalysis::DriveAlias> aliases;
    if (store && isa<sim::SimDriverDriveOp>(op) && !fact->dynamic)
      aliases = provenance.getExclusiveDriveAliases(*fact->descriptor,
                                                    fact->low, fact->width);
    for (const auto &alias : aliases) {
      auto aliasRoot = ActivationMemoryAnalysis::getRoot(
          schedule::ComputeResourceKind::Net, alias.net);
      if (!aliasRoot || alias.low > alias.rootWidth ||
          alias.width > alias.rootWidth - alias.low)
        return failure();
      access.push_back({*aliasRoot, alias.low, alias.width});
    }
    if (!aliases.empty())
      normalize(access);
    if (values) {
      ReachingDefinitions incoming;
      if (inputsOnly)
        incoming = incomingCell(*root, *fact, state);
      else
        trackedCell(state, *root, fact->rootWidth);
      if (load)
        readReaching(inputsOnly ? incoming : *state.reaching.find(*root), load,
                     value, *fact, exact);
      else
        writeReaching(inputsOnly ? incoming : *state.reaching.write(*root),
                      store, *fact, exact, Range{*root, fact->low, fact->width});
      for (const auto &alias : aliases) {
        uint64_t aliasRoot = *ActivationMemoryAnalysis::getRoot(
            schedule::ComputeResourceKind::Net, alias.net);
        HandleFact aliasFact = *fact;
        aliasFact.descriptor = alias.net;
        aliasFact.low = alias.low;
        aliasFact.width = alias.width;
        aliasFact.rootWidth = alias.rootWidth;
        // An ascending run receives the driven lanes from `drivenLow` one
        // to one; a reversed run receives them bit by bit in reverse order.
        ReachingDefinitions incoming;
        ReachingDefinitions *cell;
        if (inputsOnly) {
          incoming = incomingCell(aliasRoot, aliasFact, state);
          cell = &incoming;
        } else {
          trackedCell(state, aliasRoot, alias.rootWidth);
          cell = state.reaching.write(aliasRoot);
        }
        if (!exact || !alias.reversed) {
          writeReaching(*cell, store, aliasFact, exact,
                        Range{aliasRoot, alias.low, alias.width},
                        alias.drivenLow);
          continue;
        }
        for (uint64_t bit = 0; bit != alias.width; ++bit) {
          HandleFact bitFact = aliasFact;
          bitFact.low = alias.low + bit;
          bitFact.width = 1;
          writeReaching(*cell, store, bitFact, /*exact=*/true,
                        Range{aliasRoot, bitFact.low, 1},
                        alias.drivenLow + alias.width - 1 - bit);
        }
      }
    }
    if (load) {
      unite(reads[op], access);
      state.exposed.unite(state.assigned.subtractFrom(access));
    } else {
      unite(writes[op], access);
      state.repeated.unite(state.written.intersectWith(access));
      state.written.unite(access);
      auto span = sim::getProvenanceSpan(value.getType());
      if (!fact->dynamic && span && fact->width == *span)
        state.assigned.unite(access);
    }
    return success();
  }
  /// The tracked cell of `root`, created as its incoming value if absent.
  const ReachingDefinitions &trackedCell(Memory &state, uint64_t root,
                                         uint64_t width) {
    if (const ReachingDefinitions *cell = state.reaching.find(root))
      return *cell;
    ReachingDefinitions cell{width, values->incomingNode(root, width), {}};
    cell.links.push_back(cell.identity());
    return state.reaching.set(root, std::move(cell));
  }
  /// Reaching-value lanes of one root cell for a load.
  void readReaching(const ReachingDefinitions &cell, Operation *load,
                    Value value, const HandleFact &fact, bool exact) {
    auto links = projectLinks(cell.links, fact.low, fact.width);
    if (!exact)
      for (auto &link : links) {
        link.low = 0;
        link.width = valueSpan(load->getResult(0));
        link.mapping = LaneMapping::AllToAll;
      }
    links.push_back(values->valueLink(value, valueSpan(load->getResult(0))));
    values->append(values->valueNode(load->getResult(0)), links);
  }
  /// The new reaching definition of a store. A design store also records its
  /// publication; a private slot store does not. An exact write places the
  /// stored value's lanes from `lane` onto the written bits.
  void writeReaching(ReachingDefinitions &cell, Operation *store,
                     const HandleFact &fact, bool exact,
                     std::optional<Range> publication, uint64_t lane = 0) {
    unsigned node =
        publication ? values->publication(store, *publication, exact, lane)
                    : values->privateStore(cast<sim::SimRefStoreOp>(store));
    if (exact) {
      auto retained = projectLinks(cell.links, 0, fact.low);
      llvm::append_range(retained,
                         projectLinks(cell.links, fact.low + fact.width,
                                      cell.width - fact.low - fact.width,
                                      fact.low + fact.width));
      cell.links = std::move(retained);
    }
    uint64_t span = valueSpan(ActivationMemoryAnalysis::getStoredValue(store));
    cell.links.push_back({fact.low, fact.width, node, exact ? lane : 0,
                          exact ? span - std::min(lane, span) : span,
                          exact ? LaneMapping::Identity
                                : LaneMapping::AllToAll});
    normalize(cell.links);
  }
  sim::SimFuncOp function;
  ValueLiveness &liveness;
  LogicalLanes logical;
  const HandleDataflowResult &handleResult;
  const HandleFacts &handles;
  const HandleDataflowAnalysis &provenance;
  unsigned maximumPartitions;
  ActivationValueGraph *values;
  Operation *symbols;
  bool inputsOnly;
  SymbolTableCollection symbolTables;
  DenseMap<Operation *, ReadOnlyCallSummary> callSummaries;
  SmallVector<BoundedLoop, 0> loops;
  DenseMap<Block *, SmallVector<unsigned, 2>> blockLoops;
  DenseMap<Operation *, SmallVector<unsigned, 1>> loopConditions;
  DenseMap<Block *, SmallVector<Value>> liveControlValues;
};
} // namespace

ActivationMemoryAnalysis::ActivationMemoryAnalysis(
    sim::SimFuncOp function, const HandleDataflowAnalysis &handleAnalysis,
    unsigned maximumPartitions, DependencyMode mode, Operation *symbols) {
  if (function.isExternal() || function.getBody().empty())
    return;
  if (!symbols)
    symbols = function->getParentOp();
  auto handleResult = handleAnalysis.analyze(function);
  const auto &handles = handleResult.facts;
  LogicalLanes layout;
  DenseMap<uint64_t, ArrayRef<Range>> rootLanes;
  DenseSet<uint64_t> conflictingLayouts;
  // Dynamic element selectors retain the enclosing root as their may-range.
  // Recover its logical lanes from exact whole-root views. Disagreeing views
  // keep the conservative span, independent of handle-map iteration order.
  for (auto [value, fact] : handles) {
    auto root = fact.descriptor ? getRoot(fact.resource, *fact.descriptor)
                                : std::nullopt;
    if (!root || fact.dynamic || fact.low || fact.width != fact.rootWidth ||
        sim::getProvenanceSpan(value.getType()) != fact.rootWidth ||
        conflictingLayouts.contains(*root))
      continue;
    Type type = value.getType();
    if (auto reference = dyn_cast<sim::RefType>(type))
      type = reference.getElementType();
    else if (auto net = dyn_cast<sim::NetType>(type))
      type = net.getElementType();
    else if (auto driver = dyn_cast<sim::DriverType>(type))
      type = driver.getElementType();
    else
      continue;
    auto lanes = layout.get(type);
    auto [found, inserted] = rootLanes.try_emplace(*root, lanes);
    if (!inserted && found->second != lanes) {
      rootLanes.erase(found);
      conflictingLayouts.insert(*root);
    }
  }
  auto filterPadding = [&](Ranges &ranges) {
    Ranges filtered;
    for (auto range : ranges) {
      auto lanes = rootLanes.lookup(range.root);
      if (lanes.empty()) {
        filtered.push_back(range);
        continue;
      }
      for (auto lane : lanes) {
        uint64_t low = std::max(range.low, lane.low);
        uint64_t end = std::min(range.low + range.width, lane.low + lane.width);
        if (low < end)
          filtered.push_back({range.root, low, end - low});
      }
    }
    ranges = std::move(filtered);
    normalize(ranges);
  };
  // The value graph enumerates every value live into a block. The dense
  // memory transfer only asks whether particular values are live, so it
  // computes liveness per value on demand.
  std::optional<Liveness> blockLiveness;
  std::unique_ptr<ActivationValueGraph> values;
  if (mode != DependencyMode::Accesses) {
    blockLiveness.emplace(function);
    values = std::make_unique<ActivationValueGraph>(function, rootLanes,
                                                    *blockLiveness);
    if (failed(values->initialize())) {
      dependencyFailure = "control flow";
      values.reset();
    }
  }
  ValueLiveness valueLiveness;
  auto solver = std::make_unique<DataFlowSolver>();
  auto *analysis = solver->load<ActivationDataflow>(
      function, handleResult, handleAnalysis, maximumPartitions, values.get(),
      symbols, mode == DependencyMode::Inputs, valueLiveness);
  if (failed(solver->initializeAndRun(function)) || !analysis->exit) {
    unsupported = analysis->unsupported;
    return;
  }
  valid = true;
  partitions = analysis->partitions;
  liveIns = analysis->exit->exposed.toRanges();
  mayWrites = analysis->exit->written.toRanges();
  mustWrites = analysis->exit->assigned.toRanges();
  repeatedWrites = analysis->exit->repeated.toRanges();
  reads = std::move(analysis->reads);
  privateReads = std::move(analysis->privateReads);
  mixedPrivateWrites = analysis->mixedPrivateWrites;
  writes = std::move(analysis->writes);
  filterPadding(liveIns);
  filterPadding(mayWrites);
  filterPadding(mustWrites);
  filterPadding(repeatedWrites);
  for (auto &[op, ranges] : reads)
    filterPadding(ranges);
  for (auto &[op, ranges] : writes)
    filterPadding(ranges);
  auto finalMemory = std::move(analysis->exit->reaching);
  bool untrackedLocalSlots = analysis->untrackedLocalSlots;
  bool ambiguousReferences = analysis->ambiguousReferences;
  // The sparse graph owns its definition links. Release the dense CFG states
  // before solving it, rather than retaining both complete lattices at once.
  solver.reset();
  if (values) {
    if (untrackedLocalSlots)
      dependencyFailure = "private slot";
    else if (ambiguousReferences)
      dependencyFailure = "reference store";
    else if (failed(values->solve(valueInputs, finalInputs, finalMemory,
                                  mayWrites, publications,
                                  mode == DependencyMode::Inputs)))
      dependencyFailure = "lane dataflow";
    else {
      inputDependencies = true;
      valueDependencies = mode == DependencyMode::ReachingValues;
      dependencyFailure = {};
    }
    valueNodes = values->size();
  }
}
bool ActivationMemoryAnalysis::hasCompleteWrites() const {
  return valid && subtract(mayWrites, mustWrites).empty();
}
} // namespace obelisk::analysis
