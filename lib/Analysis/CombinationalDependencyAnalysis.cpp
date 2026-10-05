//===- CombinationalDependencyAnalysis.cpp - Statement dependencies ===//
#include "obelisk/Analysis/CombinationalDependencyAnalysis.h"
#include "obelisk/Analysis/GraphAlgorithms.h"

#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/MapVector.h"
#include <queue>

using namespace mlir;
namespace obelisk::analysis {
namespace {
using Analysis = CombinationalDependencyAnalysis;
using Access = Analysis::Access;
using Kind = Analysis::AccessKind;
using Summary = Analysis::Summary;
using Resource = schedule::ComputeResourceKind;

static auto accessKey(const Access &access) {
  const auto &r = access.target;
  return std::tuple(access.kind, r.resource, r.descriptor, r.formal,
                    r.rootWidth, r.low, r.width, r.dynamic);
}

static void normalize(SmallVectorImpl<Access> &accesses) {
  llvm::sort(accesses, [](const Access &a, const Access &b) {
    return accessKey(a) < accessKey(b);
  });
  accesses.erase(std::unique(accesses.begin(), accesses.end()), accesses.end());
}

struct Call {
  sim::SimCallOp operation;
  sim::SimFuncOp callee;
  bool recursive = false;
};
struct Function {
  sim::SimFuncOp operation;
  HandleFacts handles;
  Summary local;
  SmallVector<Call> calls;
  DenseMap<Operation *, Summary> statements;
};

static Summary directEffects(Operation *op, const HandleFacts &handles) {
  Summary result;
  auto add = [&](Kind kind, Value reference) {
    auto found = handles.find(reference);
    if (found == handles.end()) {
      result.opaque = true;
      return;
    }
    const HandleFact &fact = found->second;
    // Local memory still contributes source-order edges, but cannot publish
    // a dependency into another process. Its SSA promotion is independent.
    if (fact.resource == Resource::Local)
      return;
    if (fact.resource != Resource::Storage ||
        (!fact.descriptor && !fact.formal) || !fact.width ||
        fact.low > fact.rootWidth || fact.width > fact.rootWidth - fact.low) {
      result.opaque = true;
      return;
    }
    result.accesses.push_back({kind, fact});
  };
  if (auto load = dyn_cast<sim::SimRefLoadOp>(op))
    add(Kind::Read, load.getReference());
  else if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
    add(Kind::Write, store.getReference());
  else if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(op))
    add(Kind::NBA, nba.getDestination());
  else if (auto wait = dyn_cast<sim::SimSuspendChangeOp>(op))
    add(Kind::Watch, wait.getWatched());
  else if (auto wait = dyn_cast<sim::SimSuspendEdgeOp>(op))
    add(Kind::Watch, wait.getWatched());
  else if (auto wait = dyn_cast<sim::SimSuspendAnyOp>(op))
    for (Value reference : wait.getWatched())
      add(Kind::Watch, reference);
  else if (!isa<sim::SimCallOp, sim::SimReturnOp, sim::SimRefAllocOp,
                cf::BranchOp, cf::CondBranchOp>(op) &&
           !isMemoryEffectFree(op))
    result.opaque = true;
  // A region op cannot be flattened into a statement without its region
  // control-flow semantics. Its effects still reach the caller summary.
  result.opaque |= op->getNumRegions() != 0;
  return result;
}

static Summary instantiate(const Summary &callee, const Call &call,
                           const Function &caller) {
  Summary result;
  result.opaque = callee.opaque || call.recursive;
  for (const Access &access : callee.accesses) {
    Access instantiated = access;
    const HandleFact &formal = access.target;
    if (formal.formal) {
      unsigned index = *formal.formal;
      if (index >= call.operation->getNumOperands()) {
        result.opaque = true;
        continue;
      }
      auto found = caller.handles.find(call.operation->getOperand(index));
      if (found == caller.handles.end()) {
        result.opaque = true;
        continue;
      }
      const HandleFact &actual = found->second;
      auto actualSpan =
          sim::getProvenanceSpan(call.operation->getOperand(index).getType());
      if (actual.resource == Resource::Local)
        continue;
      if (actual.resource != formal.resource ||
          (!actual.descriptor && !actual.formal) || !actual.width ||
          actual.low > actual.rootWidth ||
          actual.width > actual.rootWidth - actual.low || !actualSpan ||
          actual.width < *actualSpan || formal.low > *actualSpan ||
          formal.width > *actualSpan - formal.low) {
        result.opaque = true;
        continue;
      }
      instantiated.target = actual;
      if (!actual.dynamic) {
        instantiated.target.low += formal.low;
        // A reference phi can have several fixed offsets. Its may-range is
        // wider than the actual's type even when dynamic is false. Project
        // the formal slice through the complete hull, not just its first view.
        instantiated.target.width = actual.width - *actualSpan + formal.width;
      }
      instantiated.target.dynamic |= formal.dynamic;
    }
    result.accesses.push_back(instantiated);
  }
  normalize(result.accesses);
  return result;
}

class SummaryState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  Summary value;
  ChangeResult join(const Summary &incoming) {
    Summary next = value;
    next.opaque |= incoming.opaque;
    llvm::append_range(next.accesses, incoming.accesses);
    normalize(next.accesses);
    if (next.opaque == value.opaque && next.accesses == value.accesses)
      return ChangeResult::NoChange;
    value = std::move(next);
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override {
    os << value.accesses.size() << " accesses, opaque=" << value.opaque;
  }
};

// Sparse call-graph equations. MLIR owns the worklist and dependency updates;
// this is not a fixed number of whole-program rescans. Recursive substitutions
// deliberately become top rather than producing an unbounded offset chain.
class SummaryDataflow final : public DataFlowAnalysis {
public:
  SummaryDataflow(DataFlowSolver &solver,
                  const llvm::MapVector<Operation *, Function> &functions)
      : DataFlowAnalysis(solver), functions(functions) {}
  LogicalResult initialize(Operation *) override {
    for (const auto &entry : functions)
      if (failed(visit(getProgramPointBefore(entry.first))))
        return failure();
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    const Function &function = functions.at(point->getNextOp());
    Summary next = function.local;
    for (const Call &call : function.calls) {
      if (!call.callee || call.recursive) {
        next.opaque = true;
        continue;
      }
      const auto *callee = getOrCreateFor<SummaryState>(
          point, getProgramPointBefore(call.callee));
      Summary effects = instantiate(callee->value, call, function);
      next.opaque |= effects.opaque;
      llvm::append_range(next.accesses, effects.accesses);
    }
    auto *state = getOrCreate<SummaryState>(point);
    propagateIfChanged(state, state->join(next));
    return success();
  }

private:
  const llvm::MapVector<Operation *, Function> &functions;
};

static bool combinational(sim::SimFuncOp function) {
  if (function.getHomeRegion() != sim::EventRegion::Active)
    return false;
  switch (function.getEntryKind()) {
  case sim::EntryKind::AlwaysComb:
  case sim::EntryKind::Continuous:
  case sim::EntryKind::PortInput:
  case sim::EntryKind::PortOutput:
    return true;
  default:
    return false;
  }
}

static bool overlaps(const HandleFact &a, const HandleFact &b) {
  return a.descriptor && b.descriptor && a.resource == b.resource &&
         a.descriptor == b.descriptor && a.low < b.low + b.width &&
         b.low < a.low + a.width;
}
} // namespace

CombinationalDependencyAnalysis::CombinationalDependencyAnalysis(
    sim::SimDesignOp design) {
  HandleDataflowAnalysis handles(design);
  SymbolTable symbols(design);
  llvm::MapVector<Operation *, Function> functions;
  SmallVector<Operation *> callNodes;
  DenseMap<Operation *, SmallVector<Operation *>> callEdges;
  for (auto function : design.getBody().getOps<sim::SimFuncOp>()) {
    Function info;
    info.operation = function;
    info.handles = handles.derive(function);
    info.local.opaque = function.isExternal();
    function.walk([&](Operation *op) {
      if (op == function)
        return;
      Summary effects = directEffects(op, info.handles);
      info.local.opaque |= effects.opaque;
      llvm::append_range(info.local.accesses, effects.accesses);
      info.statements.try_emplace(op, std::move(effects));
      if (auto call = dyn_cast<sim::SimCallOp>(op)) {
        auto callee = symbols.lookup<sim::SimFuncOp>(call.getCallee());
        info.calls.push_back({call, callee});
        if (callee)
          callEdges[function].push_back(callee);
      }
    });
    normalize(info.local.accesses);
    functions.try_emplace(function, std::move(info));
    callNodes.push_back(function);
  }
  DenseMap<Operation *, unsigned> functionOrder, callComponent;
  for (auto [index, function] : llvm::enumerate(callNodes))
    functionOrder[function] = index;
  auto callSCCs = computeStronglyConnectedComponents<Operation *>(
      callNodes, callEdges, [&](Operation *a, Operation *b) {
        return functionOrder.lookup(a) < functionOrder.lookup(b);
      });
  for (auto [index, component] : llvm::enumerate(callSCCs))
    for (Operation *member : component)
      callComponent[member] = index;
  for (auto &[op, function] : functions)
    for (Call &call : function.calls)
      call.recursive = call.callee && callComponent.lookup(op) ==
                                          callComponent.lookup(call.callee);

  DataFlowSolver solver;
  solver.load<SummaryDataflow>(functions);
  if (failed(solver.initializeAndRun(design)))
    return;
  unknownEffects = false;
  for (auto &[op, function] : functions) {
    const auto *state =
        solver.lookupState<SummaryState>(solver.getProgramPointBefore(op));
    if (!state) {
      unknownEffects = true;
      return;
    }
    summaries.try_emplace(op, state->value);
    for (const Call &call : function.calls) {
      Summary effects;
      const auto *callee = call.callee
                               ? solver.lookupState<SummaryState>(
                                     solver.getProgramPointBefore(call.callee))
                               : nullptr;
      if (!callee || call.recursive)
        effects.opaque = true;
      else
        effects = instantiate(callee->value, call, function);
      function.statements[call.operation] = std::move(effects);
    }
    // A declaration contributes through its callers. Only actual process
    // observers belong in the whole-design observation inventory.
    if (function.operation.getEntryKind() == sim::EntryKind::Function ||
        function.operation.getEntryKind() == sim::EntryKind::Task)
      continue;
    if (llvm::any_of(state->value.accesses, [](const Access &access) {
          return !access.target.descriptor;
        }))
      summaries.at(op).opaque = true;
    unknownEffects |= state->value.opaque;
    unknownEffects |= summaries.at(op).opaque;
    if (!combinational(function.operation)) {
      observers.push_back({op, function.operation, state->value.accesses,
                           summaries.at(op).opaque});
      continue;
    }
    DenseMap<Block *, unsigned> first, last;
    for (Block &block : function.operation.getBody()) {
      for (Operation &statement : block) {
        const Summary &effects = function.statements.at(&statement);
        // Keep branch/wait nodes even without data effects. This preserves
        // control and statement order without a quadratic transitive closure.
        if (effects.accesses.empty() && !effects.opaque &&
            !statement.hasTrait<OpTrait::IsTerminator>() &&
            isMemoryEffectFree(&statement))
          continue;
        unsigned id = nodes.size();
        nodes.push_back(
            {&statement, function.operation, effects.accesses, effects.opaque});
        auto previous = last.find(&block);
        if (previous != last.end())
          edges.push_back({previous->second, id, EdgeKind::SourceOrder});
        first.try_emplace(&block, id);
        last[&block] = id;
      }
    }
    for (Block &block : function.operation.getBody()) {
      Operation *terminator = block.getTerminator();
      if (sim::isSuspensionOp(terminator))
        continue;
      for (Block *successor : block.getSuccessors())
        edges.push_back(
            {last.at(&block), first.at(successor), EdgeKind::SourceOrder});
    }
  }

  // Index by root before comparing ranges. Dynamic selections keep the may
  // range from handle dataflow; no current index value is used as a proof.
  DenseMap<uint64_t, SmallVector<std::pair<unsigned, HandleFact>>> readers;
  for (auto [id, node] : llvm::enumerate(nodes))
    for (const Access &access : node.accesses)
      if (access.kind == Kind::Read && access.target.descriptor)
        readers[*access.target.descriptor].push_back({id, access.target});
  for (auto [id, node] : llvm::enumerate(nodes))
    for (const Access &access : node.accesses) {
      if (access.kind != Kind::Write || !access.target.descriptor)
        continue;
      for (const auto &[target, read] : readers[*access.target.descriptor])
        if (nodes[target].owner != node.owner && overlaps(access.target, read))
          edges.push_back(
              {static_cast<unsigned>(id), target, EdgeKind::Memory});
    }
  llvm::sort(edges, [](const Edge &a, const Edge &b) {
    return std::tie(a.source, a.target, a.kind) <
           std::tie(b.source, b.target, b.kind);
  });
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
  SmallVector<unsigned> ids;
  DenseMap<unsigned, SmallVector<unsigned>> adjacency;
  for (unsigned id = 0; id < nodes.size(); ++id)
    ids.push_back(id);
  for (const Edge &edge : edges)
    adjacency[edge.source].push_back(edge.target);
  auto sccs = computeStronglyConnectedComponents<unsigned>(ids, adjacency);
  for (auto &members : llvm::reverse(sccs)) {
    bool cyclic =
        members.size() > 1 ||
        llvm::is_contained(adjacency[members.front()], members.front());
    components.push_back({std::move(members), cyclic});
  }
}

const CombinationalDependencyAnalysis::Summary *
CombinationalDependencyAnalysis::lookup(sim::SimFuncOp function) const {
  auto found = summaries.find(function);
  return found == summaries.end() ? nullptr : &found->second;
}

std::optional<SmallVector<Operation *>>
CombinationalDependencyAnalysis::getInterleaving(
    ArrayRef<sim::SimFuncOp> functions) const {
  SmallVector<Operation *> operations;
  DenseMap<Operation *, unsigned> ids;
  SmallVector<std::pair<unsigned, unsigned>> dependencies;
  for (sim::SimFuncOp function : functions) {
    const Summary *summary = lookup(function);
    if (!summary || summary->opaque ||
        function.getBody().getBlocks().size() != 2)
      return std::nullopt;
    Block &entry = function.getBody().front(),
          &body = function.getBody().back();
    auto start = dyn_cast<cf::BranchOp>(entry.getTerminator());
    Operation *wait = body.getTerminator();
    if (!start || start.getDest() != &body ||
        !isa<sim::SimSuspendChangeOp, sim::SimSuspendAnyOp>(wait) ||
        wait->getSuccessor(0) != &body)
      return std::nullopt;
    std::optional<unsigned> previous;
    for (Operation &op : body.without_terminator()) {
      unsigned id = operations.size();
      if (!ids.try_emplace(&op, id).second)
        return std::nullopt;
      operations.push_back(&op);
      if (previous)
        dependencies.emplace_back(*previous, id);
      previous = id;
    }
  }
  for (const Edge &edge : edges) {
    if (edge.kind != EdgeKind::Memory)
      continue;
    auto source = ids.find(nodes[edge.source].operation);
    auto target = ids.find(nodes[edge.target].operation);
    if (source != ids.end() && target != ids.end())
      dependencies.emplace_back(source->second, target->second);
  }
  llvm::sort(dependencies);
  dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                     dependencies.end());
  SmallVector<SmallVector<unsigned>> successors(operations.size());
  SmallVector<unsigned> incoming(operations.size(), 0);
  for (auto [source, target] : dependencies) {
    successors[source].push_back(target);
    ++incoming[target];
  }
  std::priority_queue<unsigned, SmallVector<unsigned>, std::greater<unsigned>>
      ready;
  for (unsigned id = 0; id < operations.size(); ++id)
    if (!incoming[id])
      ready.push(id);
  SmallVector<Operation *> order;
  while (!ready.empty()) {
    unsigned id = ready.top();
    ready.pop();
    order.push_back(operations[id]);
    for (unsigned next : successors[id])
      if (!--incoming[next])
        ready.push(next);
  }
  if (order.size() != operations.size())
    return std::nullopt;
  return order;
}
} // namespace obelisk::analysis
