//===- ManagedCollectionAnalysis.cpp - Managed heap collection points ----===//

#include "obelisk/Analysis/ManagedCollectionAnalysis.h"

#include "obelisk/Analysis/ClassDispatchAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SetVector.h"

#include <memory>
#include <optional>

using namespace mlir;

namespace obelisk::analysis {
namespace {

/// A directed graph over dense indices, walked by llvm::scc_iterator. A
/// virtual entry node reaches every other node.
struct IndexGraph {
  struct Node {
    SmallVector<Node *> successors;
  };
  explicit IndexGraph(size_t size) {
    for (size_t index = 0; index != size + 1; ++index)
      nodes.push_back(std::make_unique<Node>());
    for (size_t index = 0; index != size; ++index)
      nodes[size]->successors.push_back(nodes[index].get());
  }
  void addEdge(unsigned from, unsigned to) {
    nodes[from]->successors.push_back(nodes[to].get());
  }
  /// Nodes on a cycle of the graph, including self-loops.
  llvm::BitVector cyclicNodes();

  SmallVector<std::unique_ptr<Node>> nodes;
};

} // namespace
} // namespace obelisk::analysis

template <>
struct llvm::GraphTraits<obelisk::analysis::IndexGraph::Node *> {
  using NodeRef = obelisk::analysis::IndexGraph::Node *;
  using ChildIteratorType = SmallVectorImpl<NodeRef>::iterator;
  static ChildIteratorType child_begin(NodeRef node) {
    return node->successors.begin();
  }
  static ChildIteratorType child_end(NodeRef node) {
    return node->successors.end();
  }
};

template <>
struct llvm::GraphTraits<obelisk::analysis::IndexGraph *>
    : llvm::GraphTraits<obelisk::analysis::IndexGraph::Node *> {
  static NodeRef getEntryNode(obelisk::analysis::IndexGraph *graph) {
    return graph->nodes.back().get();
  }
};

namespace obelisk::analysis {
namespace {

llvm::BitVector IndexGraph::cyclicNodes() {
  DenseMap<Node *, unsigned> indices;
  for (auto [index, node] : llvm::enumerate(nodes))
    indices[node.get()] = index;
  llvm::BitVector cyclic(nodes.size() - 1);
  for (auto scc = llvm::scc_begin(this); !scc.isAtEnd(); ++scc)
    if (scc.hasCycle())
      for (Node *node : *scc)
        if (node != nodes.back().get())
          cyclic.set(indices.lookup(node));
  return cyclic;
}

constexpr unsigned allEffects = ManagedCollectionAnalysis::Allocates |
                                ManagedCollectionAnalysis::Polls;

/// A DPI boundary converts managed strings, open arrays, and handles through
/// managed storage; plain bits cross it without allocation.
bool carriesManagedValues(Type type) {
  if (auto reference = dyn_cast<sim::RefType>(type))
    type = reference.getElementType();
  if (isa<sim::ManagedRefType, sim::ArgumentRefType>(type))
    return true;
  SmallVector<sim::ManagedHandleSlot> slots;
  return !sim::getManagedHandleSlots(type, slots) || !slots.empty();
}

bool isForeignEntry(sim::SimFuncOp function) {
  return dpi::has<dpi::Export>(function) ||
         dpi::has<dpi::ExportBridge>(function);
}

/// A call into compiled code: a function, method, or DPI import call, or a
/// task call that runs the task in the caller's logical process.
struct CallSite {
  Operation *operation;
  SmallVector<unsigned> callees{};
  bool unresolved = false;
  bool reentrant = false;
};

/// A top-level operation of a function body, with everything nested in it.
struct BodyOperation {
  Operation *operation;
  bool allocates = false;
  SmallVector<unsigned> calls;
};

struct FunctionInfo {
  sim::SimFuncOp function;
  bool allocates = false;
  bool polls = false;
  SmallVector<CallSite> calls{};
  /// Top-level operations grouped by block, in region order.
  SmallVector<SmallVector<BodyOperation>> blocks{};
  llvm::BitVector cyclicBlocks{};
};

class SummaryState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  unsigned summary = 0;
  ChangeResult join(unsigned incoming) {
    unsigned next = summary | incoming;
    if (next == summary)
      return ChangeResult::NoChange;
    summary = next;
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override { os << summary; }
};

/// Shared between the solver and the final placement: functions and the
/// call graph they form.
struct CallGraph {
  SmallVector<FunctionInfo, 0> functions;
  DenseMap<Operation *, unsigned> indices;

  void build(Operation *root);
  /// Poll sites of `info` given callee summaries from `summaryOf`.
  template <typename SummaryOf>
  SmallVector<Operation *> pollSites(const FunctionInfo &info,
                                     unsigned functionSummary,
                                     SummaryOf summaryOf) const;
  template <typename SummaryOf>
  unsigned callSummary(const CallSite &call, SummaryOf summaryOf) const {
    if (call.unresolved)
      return allEffects;
    unsigned summary = 0;
    for (unsigned callee : call.callees)
      summary |= summaryOf(callee);
    return summary;
  }
};

void CallGraph::build(Operation *root) {
  root->walk([&](sim::SimFuncOp function) {
    indices[function] = functions.size();
    functions.push_back({function});
  });
  SmallVector<unsigned> foreignEntries;
  for (auto [index, info] : llvm::enumerate(functions))
    if (isForeignEntry(info.function))
      foreignEntries.push_back(index);

  SymbolTableCollection symbols;
  DenseMap<Operation *, std::unique_ptr<ClassDispatchAnalysis>> dispatches;
  auto dispatchFor = [&](Operation *operation) -> ClassDispatchAnalysis * {
    auto design = operation->getParentOfType<sim::SimDesignOp>();
    if (!design)
      return nullptr;
    std::unique_ptr<ClassDispatchAnalysis> &dispatch = dispatches[design];
    if (!dispatch)
      dispatch = std::make_unique<ClassDispatchAnalysis>(design);
    return dispatch.get();
  };
  auto functionIndex = [&](Operation *user,
                           FlatSymbolRefAttr symbol) -> std::optional<unsigned> {
    auto callee = symbols.lookupNearestSymbolFrom<sim::SimFuncOp>(user, symbol);
    if (!callee || callee.isExternal())
      return std::nullopt;
    auto found = indices.find(callee);
    if (found == indices.end())
      return std::nullopt;
    return found->second;
  };
  // The implementations a dispatch may select, or nullopt when the closed
  // hierarchy does not account for the receiver.
  auto dispatchTargets = [&](Operation *call, Value receiver, uint64_t slot,
                             uint64_t signature,
                             bool isTask) -> std::optional<SmallVector<unsigned>> {
    ClassDispatchAnalysis *dispatch = dispatchFor(call);
    auto type = dyn_cast<sim::ClassHandleType>(receiver.getType());
    sim::SimClassDeclOp staticClass =
        dispatch && type ? dispatch->lookup(type) : sim::SimClassDeclOp();
    if (!staticClass)
      return std::nullopt;
    SmallVector<unsigned> targets;
    for (sim::SimClassMethodDeclOp method : dispatch->compatibleImplementations(
             staticClass, slot, signature, isTask)) {
      std::optional<unsigned> target =
          functionIndex(method, method.getImplementationAttr());
      if (!target)
        return std::nullopt;
      targets.push_back(*target);
    }
    if (targets.empty())
      return std::nullopt;
    return targets;
  };
  auto resolve = [&](Operation *operation) -> std::optional<CallSite> {
    CallSite call{operation};
    auto setTargets = [&](std::optional<SmallVector<unsigned>> targets) {
      if (targets)
        call.callees = std::move(*targets);
      else
        call.unresolved = true;
    };
    auto setTarget = [&](std::optional<unsigned> target) {
      if (target)
        call.callees.push_back(*target);
      else
        call.unresolved = true;
    };
    if (auto direct = dyn_cast<sim::SimCallOp>(operation)) {
      setTarget(functionIndex(direct, direct.getCalleeAttr()));
    } else if (auto method = dyn_cast<sim::SimClassDirectCallOp>(operation)) {
      setTarget(functionIndex(method, method.getCalleeAttr()));
    } else if (auto dispatch = dyn_cast<sim::SimClassVirtualCallOp>(operation)) {
      setTargets(dispatchTargets(dispatch, dispatch.getReceiver(),
                                 dispatch.getSlot(), dispatch.getSignatureId(),
                                 /*isTask=*/false));
    } else if (auto dpi = dyn_cast<sim::SimDPICallOp>(operation)) {
      if (dpi.getIsContext())
        call.callees = foreignEntries;
    } else if (auto task = dyn_cast<sim::SimTaskCallOp>(operation)) {
      setTarget(functionIndex(task, task.getCalleeAttr()));
    } else if (auto task = dyn_cast<sim::SimClassVirtualTaskCallOp>(operation)) {
      setTargets(dispatchTargets(task, task.getReceiver(), task.getSlot(),
                                 task.getSignatureId(), /*isTask=*/true));
    } else {
      return std::nullopt;
    }
    return call;
  };

  IndexGraph calls(functions.size());
  for (auto [index, info] : llvm::enumerate(functions)) {
    if (info.function.isExternal())
      continue;
    Region &body = info.function.getBody();
    DenseMap<Block *, unsigned> blockIndices;
    for (Block &block : body)
      blockIndices[&block] = blockIndices.size();
    IndexGraph flow(blockIndices.size());
    for (Block &block : body) {
      SmallVector<BodyOperation> &operations = info.blocks.emplace_back();
      for (Operation &operation : block) {
        BodyOperation &entry = operations.emplace_back();
        entry.operation = &operation;
        operation.walk([&, &info = info, &index = index](Operation *nested) {
          if (isa<sim::SimGCSafepointOp>(nested))
            info.polls = true;
          if (mayAllocateManagedStorage(nested))
            entry.allocates = true;
          if (std::optional<CallSite> call = resolve(nested)) {
            for (unsigned callee : call->callees)
              calls.addEdge(index, callee);
            entry.calls.push_back(info.calls.size());
            info.calls.push_back(std::move(*call));
          }
        });
        info.allocates |= entry.allocates;
      }
      Operation *terminator = block.getTerminator();
      if (!terminator || sim::returnsToScheduler(terminator))
        continue;
      for (Block *successor : terminator->getSuccessors())
        flow.addEdge(blockIndices.lookup(&block),
                     blockIndices.lookup(successor));
    }
    info.cyclicBlocks = flow.cyclicNodes();
  }

  // A call re-enters its caller when a callee shares the caller's strongly
  // connected component of the call graph.
  llvm::BitVector recursive = calls.cyclicNodes();
  SmallVector<unsigned> component(functions.size());
  {
    unsigned next = 0;
    DenseMap<IndexGraph::Node *, unsigned> nodeIndices;
    for (auto [index, node] : llvm::enumerate(calls.nodes))
      nodeIndices[node.get()] = index;
    for (auto scc = llvm::scc_begin(&calls); !scc.isAtEnd(); ++scc, ++next)
      for (IndexGraph::Node *node : *scc)
        if (node != calls.nodes.back().get())
          component[nodeIndices.lookup(node)] = next;
  }
  for (auto [index, info] : llvm::enumerate(functions))
    for (CallSite &call : info.calls)
      call.reentrant =
          call.unresolved ||
          llvm::any_of(call.callees, [&, caller = index](unsigned callee) {
            return recursive.test(caller) &&
                   component[callee] == component[caller];
          });
}

template <typename SummaryOf>
SmallVector<Operation *>
CallGraph::pollSites(const FunctionInfo &info, unsigned functionSummary,
                     SummaryOf summaryOf) const {
  llvm::SetVector<Operation *> sites;
  if (isForeignEntry(info.function) &&
      (functionSummary & ManagedCollectionAnalysis::Allocates) &&
      !info.blocks.empty() && !info.blocks.front().empty())
    sites.insert(info.blocks.front().front().operation);
  for (auto [blockIndex, operations] : llvm::enumerate(info.blocks)) {
    bool cyclic = info.cyclicBlocks.test(blockIndex);
    bool polled = false;
    for (const BodyOperation &entry : operations) {
      bool allocates = entry.allocates;
      bool reentrantAllocation = false;
      for (unsigned callIndex : entry.calls) {
        const CallSite &call = info.calls[callIndex];
        bool callAllocates =
            callSummary(call, summaryOf) & ManagedCollectionAnalysis::Allocates;
        allocates |= callAllocates;
        reentrantAllocation |= call.reentrant && callAllocates;
      }
      // One poll per pass through a cyclic block bounds the allocation of
      // that pass; every re-entering allocation needs its own.
      if ((cyclic && allocates && !polled) || reentrantAllocation) {
        sites.insert(entry.operation);
        polled = true;
      }
    }
  }
  return sites.takeVector();
}

/// Solves the call-graph closure of the per-function summaries. Poll
/// placement depends on callee allocation summaries, so a function's `Polls`
/// bit is recomputed whenever a callee summary grows.
class SummaryDataflow final : public DataFlowAnalysis {
public:
  SummaryDataflow(DataFlowSolver &solver, const CallGraph &graph)
      : DataFlowAnalysis(solver), graph(graph) {}

  LogicalResult initialize(Operation *) override {
    for (const FunctionInfo &info : graph.functions)
      if (failed(visit(getProgramPointBefore(static_cast<Operation *>(info.function)))))
        return failure();
    return success();
  }

  LogicalResult visit(ProgramPoint *point) override {
    auto found = graph.indices.find(point->getNextOp());
    if (found == graph.indices.end())
      return success();
    const FunctionInfo &info = graph.functions[found->second];
    if (sim::SimFuncOp(info.function).isExternal()) {
      auto *state = getOrCreate<SummaryState>(point);
      propagateIfChanged(state, state->join(allEffects));
      return success();
    }
    auto summaryOf = [&](unsigned callee) {
      return getOrCreateFor<SummaryState>(
                 point, getProgramPointBefore(
                            static_cast<Operation *>(graph.functions[callee].function)))
          ->summary;
    };
    unsigned summary = 0;
    if (info.allocates)
      summary |= ManagedCollectionAnalysis::Allocates;
    if (info.polls)
      summary |= ManagedCollectionAnalysis::Polls;
    for (const CallSite &call : info.calls)
      summary |= graph.callSummary(call, summaryOf);
    if (!graph.pollSites(info, summary, summaryOf).empty())
      summary |= ManagedCollectionAnalysis::Polls;
    auto *state = getOrCreate<SummaryState>(point);
    propagateIfChanged(state, state->join(summary));
    return success();
  }

private:
  const CallGraph &graph;
};

} // namespace

bool mayAllocateManagedStorage(Operation *operation) {
  return operation->hasTrait<OpTrait::AllocatesManagedStorage>() ||
         (isa<sim::SimDPICallOp>(operation) &&
          (llvm::any_of(operation->getOperandTypes(), carriesManagedValues) ||
           llvm::any_of(operation->getResultTypes(), carriesManagedValues)));
}

bool isCompiledCall(Operation *operation) {
  return isa<sim::SimCallOp, sim::SimClassDirectCallOp,
             sim::SimClassVirtualCallOp, sim::SimDPICallOp>(operation);
}

ManagedCollectionAnalysis::ManagedCollectionAnalysis(Operation *root) {
  CallGraph graph;
  graph.build(root);
  DataFlowSolver solver;
  solver.load<SummaryDataflow>(graph);
  if (failed(solver.initializeAndRun(root))) {
    for (const FunctionInfo &info : graph.functions)
      summaries[info.function] = allEffects;
  } else {
    for (const FunctionInfo &info : graph.functions)
      if (auto *state = solver.lookupState<SummaryState>(
              solver.getProgramPointBefore(static_cast<Operation *>(info.function))))
        summaries[info.function] = state->summary;
  }
  auto summaryOf = [&](unsigned callee) {
    return getSummary(graph.functions[callee].function);
  };
  for (const FunctionInfo &info : graph.functions) {
    for (const CallSite &call : info.calls)
      callSummaries[call.operation] = graph.callSummary(call, summaryOf);
    SmallVector<Operation *> sites =
        graph.pollSites(info, getSummary(info.function), summaryOf);
    if (!sites.empty())
      pollSites[info.function] = std::move(sites);
  }
}

unsigned ManagedCollectionAnalysis::getSummary(sim::SimFuncOp function) const {
  auto found = summaries.find(function);
  return found == summaries.end() ? allEffects : found->second;
}

unsigned ManagedCollectionAnalysis::getCallSummary(Operation *call) const {
  auto found = callSummaries.find(call);
  return found == callSummaries.end() ? 0 : found->second;
}

ArrayRef<Operation *>
ManagedCollectionAnalysis::getPollSites(sim::SimFuncOp function) const {
  auto found = pollSites.find(function);
  return found == pollSites.end() ? ArrayRef<Operation *>{} : found->second;
}

} // namespace obelisk::analysis
