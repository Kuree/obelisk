//===- ActivationMemoryAnalysis.cpp - Context-partitioned memory facts ----===//
#include "obelisk/Analysis/ActivationMemoryAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Analysis/Liveness.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Analysis/BoundedLoopAnalysis.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/TypeSwitch.h"
#include <memory>

using namespace mlir;
namespace obelisk::analysis {
namespace {
using Range = ActivationMemoryAnalysis::Range;
using Ranges = ActivationMemoryAnalysis::Ranges;
using DependencyMode = ActivationMemoryAnalysis::DependencyMode;
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
void normalize(LaneLinks &links) {
  llvm::sort(links, [](const auto &a, const auto &b) {
    return std::tie(a.source, a.mapping, a.low, a.sourceLow, a.width,
                    a.sourceWidth) < std::tie(b.source, b.mapping, b.low,
                                              b.sourceLow, b.width,
                                              b.sourceWidth);
  });
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
struct LaneNode {
  unsigned id;
  uint64_t width;
  Block *block;
  Operation *operation;
  LaneLinks links;
  Influences seed;
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
class LaneState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  Influences influences;
  bool widened = false;
  ChangeResult join(ArrayRef<Influence> inputs, uint64_t width) {
    Influences next(influences);
    llvm::append_range(next, inputs);
    normalize(next);
    // Widening is sticky. Otherwise repeatedly joining an exact relation into
    // a widened relation could oscillate around the correlation-size limit.
    bool nextWidened = widened || next.size() > 256;
    if (nextWidened)
      next = spread(sources(next), width);
    if (next == influences && nextWidened == widened)
      return ChangeResult::NoChange;
    influences = std::move(next);
    widened = nextWidened;
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override {
    os << influences.size() << " lane relations";
  }
};
class SparseLaneDataflow final : public DataFlowAnalysis {
public:
  SparseLaneDataflow(DataFlowSolver &solver,
                     ArrayRef<std::unique_ptr<LaneNode>> nodes)
      : DataFlowAnalysis(solver), nodes(nodes) {
    registerAnchorKind<LaneAnchor>();
  }
  LogicalResult initialize(Operation *) override {
    for (const auto &node : nodes)
      anchors.push_back(getLatticeAnchor<LaneAnchor>(node.get()));
    for (const auto &node : nodes) {
      auto *point = node->operation ? getProgramPointAfter(node->operation)
                                    : getProgramPointBefore(node->block);
      targets[point].push_back(node->id);
      for (const auto &link : node->links)
        addDependency(getOrCreate<LaneState>(anchors[link.source]), point);
      auto *state = getOrCreate<LaneState>(anchors[node->id]);
      propagateIfChanged(state, state->join(node->seed, node->width));
    }
    for (const auto &[point, ids] : targets)
      if (failed(visit(point)))
        return failure();
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    for (unsigned id : targets.at(point)) {
      auto &node = *nodes[id];
      Influences next;
      for (const auto &link : node.links) {
        const auto *source =
            getOrCreateFor<LaneState>(point, anchors[link.source]);
        auto part =
            project(source->influences, link.sourceLow, link.sourceWidth);
        if (link.mapping == LaneMapping::AllToAll)
          part = spread(sources(part), link.width);
        llvm::append_range(next, project(part, 0, link.width, link.low));
      }
      auto *state = getOrCreate<LaneState>(anchors[id]);
      propagateIfChanged(state, state->join(next, node.width));
    }
    return success();
  }
  SmallVector<LaneAnchor *> anchors;

private:
  ArrayRef<std::unique_ptr<LaneNode>> nodes;
  llvm::MapVector<ProgramPoint *, SmallVector<unsigned>> targets;
};

// The dense pass records memory definitions and feasible CFG edges. SSA facts
// themselves live only at these sparse nodes. Live-in proxies connect values
// across induction contexts using MLIR liveness; they never copy value lattices
// through a CFG state. Contexts describe bounded iterations, not executable IR.
class ActivationValueGraph {
public:
  explicit ActivationValueGraph(sim::SimFuncOp function)
      : function(function), liveness(function) {
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
  }
  unsigned valueNode(Value value, Block *block, ArrayAttr context) {
    auto key = std::make_tuple(value, block, context);
    auto [it, inserted] = values.try_emplace(key, 0);
    if (inserted) {
      Operation *definition = value.getDefiningOp();
      it->second = addNode(
          valueSpan(value), block,
          definition && definition->getBlock() == block ? definition : nullptr);
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
    }
    return it->second;
  }
  void append(unsigned target, ArrayRef<LaneLink> links) {
    llvm::append_range(nodes[target]->links, links);
    normalize(nodes[target]->links);
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
    SmallVector<Value> live(liveness.getLiveIn(target).begin(),
                            liveness.getLiveIn(target).end());
    llvm::append_range(live, implicitLive.lookup(target));
    // Liveness sets are unordered. Stable SSA ordinals keep the evaluation
    // graph and widening independent of hash iteration order.
    llvm::sort(live, [&](Value a, Value b) {
      return ordinals.at(a) < ordinals.at(b);
    });
    live.erase(std::unique(live.begin(), live.end()), live.end());
    for (Value value : live)
      append(valueNode(value, target, targetContext),
             {{0, valueSpan(value), valueNode(value), 0, valueSpan(value),
               LaneMapping::Identity}});
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
  void expose(Value value) { sinks.insert(valueNode(value)); }
  void pure(Operation *op, const DenseMap<Value, Attribute> &constants);
  unsigned publication(sim::SimRefStoreOp store, Range output, bool exact) {
    auto key = std::make_pair(store.getOperation(), currentContext);
    auto [it, inserted] = stores.try_emplace(key, 0);
    if (inserted)
      it->second = addNode(valueSpan(store.getValue()), currentBlock, store);
    unsigned node = it->second;
    auto links = controls(nodes[node]->width);
    links.push_back({0, nodes[node]->width, valueNode(store.getValue()), 0,
                     valueSpan(store.getValue()), LaneMapping::Identity});
    links.push_back(valueLink(store.getReference(), nodes[node]->width));
    append(node, links);
    sinks.insert(node);
    records.push_back({store, node, output, exact});
    return node;
  }
  LogicalResult solve(
      Ranges &inputs,
      DenseMap<Operation *, SmallVector<ActivationMemoryAnalysis::Publication>>
          &publications) {
    DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
    auto *flow = solver.load<SparseLaneDataflow>(nodes);
    if (failed(solver.initializeAndRun(function)))
      return failure();
    auto facts = [&](unsigned node) -> const Influences & {
      return solver.lookupState<LaneState>(flow->anchors[node])->influences;
    };
    for (unsigned node : sinks)
      unite(inputs, sources(facts(node)));
    DenseMap<Operation *, Influences> stored;
    DenseMap<Operation *, Ranges> written;
    for (const auto &record : records) {
      auto inputs = facts(record.node);
      if (!record.exact)
        inputs = spread(sources(inputs), record.output.width);
      auto &output = stored[record.operation];
      llvm::append_range(
          output, project(inputs, 0, record.output.width, record.output.low));
      normalize(output);
      unite(written[record.operation], {record.output});
    }
    for (const auto &[operation, ranges] : written) {
      const auto &influences = stored.at(operation);
      auto &lanes = publications[operation];
      for (Range output : ranges) {
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
  unsigned addNode(uint64_t width, Block *block, Operation *operation) {
    unsigned id = nodes.size();
    nodes.push_back(std::make_unique<LaneNode>(
        LaneNode{id, width, block, operation, {}, {}}));
    return id;
  }
  struct Record {
    Operation *operation;
    unsigned node;
    Range output;
    bool exact;
  };
  sim::SimFuncOp function;
  Liveness liveness;
  DenseMap<Value, unsigned> ordinals;
  DenseMap<Block *, llvm::SetVector<Block *>> controllers;
  DenseMap<Block *, llvm::SetVector<Value>> implicitLive;
  Block *currentBlock = nullptr;
  ArrayAttr currentContext;
  llvm::MapVector<std::tuple<Value, Block *, ArrayAttr>, unsigned> values;
  DenseMap<uint64_t, unsigned> incoming;
  DenseMap<std::pair<Operation *, ArrayAttr>, unsigned> stores;
  SmallVector<std::unique_ptr<LaneNode>> nodes;
  llvm::SetVector<unsigned> sinks;
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
                 sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp,
                 arith::TruncIOp>(op)) {
    if (slice(op->getOperand(0), op->getResult(0), 0,
              valueSpan(op->getResult(0))))
      return;
  } else if (auto extract = dyn_cast<sim::SimLogicExtractOp>(op)) {
    if (slice(extract.getInput(), extract.getResult(), extract.getLowBit(),
              valueSpan(extract.getResult())))
      return;
  } else if (auto update = dyn_cast<sim::SimLogicInsertOp>(op)) {
    if (insert(update.getInput(), update.getReplacement(), update.getResult(),
               update.getLowBit(), valueSpan(update.getReplacement())))
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
  DenseMap<uint64_t, ReachingDefinitions> reaching;
  Ranges assigned, exposed, written, repeated;
  bool operator==(const Memory &other) const {
    return constants == other.constants && reaching == other.reaching &&
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
    // Missing memory roots are unchanged incoming values, not bottom.
    // Only definition links are carried here; sparse SSA lattices are not.
    for (auto &[root, cell] : reaching) {
      auto found = other.reaching.find(root);
      if (found == other.reaching.end())
        cell.links.push_back(cell.identity());
      else
        llvm::append_range(cell.links, found->second.links);
      normalize(cell.links);
    }
    for (const auto &[root, cell] : other.reaching)
      if (!reaching.contains(root)) {
        auto copy = cell;
        copy.links.push_back(copy.identity());
        normalize(copy.links);
        reaching.try_emplace(root, std::move(copy));
      }
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
                     const HandleFacts &handles, unsigned maximumPartitions,
                     ActivationValueGraph *values)
      : DataFlowAnalysis(solver), function(function), handles(handles),
        maximumPartitions(maximumPartitions), values(values),
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
      if (values)
        values->enter(block, context);
      Memory state = incoming;
      for (Operation &op : block->without_terminator())
        if (failed(transfer(&op, state)))
          return failure();
      Operation *term = block->getTerminator();
      if (values) {
        if (auto branch = dyn_cast<cf::CondBranchOp>(term))
          values->expose(branch.getCondition());
        else if (auto branch = dyn_cast<cf::SwitchOp>(term))
          values->expose(branch.getFlag());
        else if (isa<sim::SimReturnOp>(term))
          for (Value operand : term->getOperands())
            values->expose(operand);
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
  LogicalResult propagate(Block *block, const Memory &state,
                          std::optional<ValueRange> operands = std::nullopt,
                          Value condition = {}) {
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
    if (values && operands)
      values->edge(block, context, *operands, condition);
    auto *target = getOrCreate<MemoryState>(getProgramPointBefore(block));
    if (!target->rows.contains(context) && ++partitions > maximumPartitions)
      return failure();
    propagateIfChanged(target, target->join(context, state));
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
    return propagate(target, state, values, condition);
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
      auto index = knownIndex(element->getOperand(1), state.constants);
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
    if (!load && !store) {
      if (values)
        values->pure(op, state.constants);
      return success(isMemoryEffectFree(op));
    }
    Value value = load ? load.getReference() : store.getReference();
    auto fact = reference(value, state);
    if (!fact || !fact->descriptor ||
        fact->resource != schedule::ComputeResourceKind::Storage ||
        !fact->width || fact->low > fact->rootWidth ||
        fact->width > fact->rootWidth - fact->low)
      return failure();
    Ranges access{{*fact->descriptor, fact->low, fact->width}};
    auto span = sim::getProvenanceSpan(value.getType());
    bool exact = !fact->dynamic && span && fact->width == *span;
    if (values) {
      auto [it, inserted] = state.reaching.try_emplace(*fact->descriptor);
      auto &cell = it->second;
      if (inserted) {
        cell.width = fact->rootWidth;
        cell.incoming =
            values->incomingNode(*fact->descriptor, fact->rootWidth);
        cell.links.push_back(cell.identity());
      }
      if (load) {
        auto links = projectLinks(cell.links, fact->low, fact->width);
        if (!exact)
          for (auto &link : links) {
            link.low = 0;
            link.width = valueSpan(load.getResult());
            link.mapping = LaneMapping::AllToAll;
          }
        links.push_back(values->valueLink(value, valueSpan(load.getResult())));
        values->append(values->valueNode(load.getResult()), links);
      } else {
        unsigned node = values->publication(store, access.front(), exact);
        if (exact) {
          auto retained = projectLinks(cell.links, 0, fact->low);
          llvm::append_range(
              retained, projectLinks(cell.links, fact->low + fact->width,
                                     fact->rootWidth - fact->low - fact->width,
                                     fact->low + fact->width));
          cell.links = std::move(retained);
        }
        cell.links.push_back(
            {fact->low, fact->width, node, 0, valueSpan(store.getValue()),
             exact ? LaneMapping::Identity : LaneMapping::AllToAll});
        normalize(cell.links);
      }
    }
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
  ActivationValueGraph *values;
  SmallVector<BoundedLoop> loops;
};
} // namespace

ActivationMemoryAnalysis::ActivationMemoryAnalysis(
    sim::SimFuncOp function, const HandleDataflowAnalysis &handleAnalysis,
    unsigned maximumPartitions, DependencyMode mode) {
  if (function.isExternal() || function.getBody().empty())
    return;
  std::unique_ptr<ActivationValueGraph> values;
  if (mode == DependencyMode::ReachingValues) {
    values = std::make_unique<ActivationValueGraph>(function);
    if (failed(values->initialize()))
      values.reset();
  }
  auto handles = handleAnalysis.derive(function);
  DataFlowSolver solver;
  auto *analysis = solver.load<ActivationDataflow>(
      function, handles, maximumPartitions, values.get());
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
  if (values && succeeded(values->solve(valueInputs, publications)))
    valueDependencies = true;
}
bool ActivationMemoryAnalysis::hasCompleteWrites() const {
  return valid && subtract(mayWrites, mustWrites).empty();
}
} // namespace obelisk::analysis
