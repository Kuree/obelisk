//===- ActivationRegionAnalysis.cpp - Structured activation regions ------===//
#include "obelisk/Analysis/ActivationRegionAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/LivenessAnalysis.h"
#include "mlir/Conversion/ControlFlowToSCF/ControlFlowToSCF.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/RegionUtils.h"
#include "obelisk/Analysis/BoundedLoopAnalysis.h"
#include "obelisk/Analysis/SSAValueAnalysis.h"
#include "obelisk/Analysis/SimulationControlFlowToSCF.h"
#include <queue>

using namespace mlir;
namespace obelisk::analysis {
namespace {
using Ranges = ActivationMemoryAnalysis::Ranges;

// Normalize disposable initial/resume views with ordinary dialect patterns.
// Memory summaries are computed only after this revision is stable.
LogicalResult canonicalizeControl(Operation *operation) {
  RewritePatternSet patterns(operation->getContext());
  for (RegisteredOperationName name :
       operation->getContext()->getRegisteredOperations())
    if (name.getDialectNamespace() == "arith" ||
        name.getDialectNamespace() == "cf" ||
        name.getDialectNamespace() == "scf")
      name.getCanonicalizationPatterns(patterns, operation->getContext());
  return applyPatternsGreedily(operation, std::move(patterns));
}

bool finiteActivation(sim::SimFuncOp function) {
  DenseSet<Operation *> latches;
  for (auto &loop : analyzeActivationBoundedLoops(function.getBody()))
    latches.insert(loop.latch);
  // Kahn's algorithm after cutting only validated loop latches and the wait.
  // A procedural or irreducible cycle without a proof is not a DAG region.
  DenseMap<Block *, unsigned> incoming;
  for (Block &block : function.getBody())
    incoming[&block] = 0;
  for (Block &block : function.getBody()) {
    auto *term = block.getTerminator();
    if (isa<sim::SimReturnOp>(term) || latches.contains(term))
      continue;
    if (!isa<cf::BranchOp, cf::CondBranchOp, cf::SwitchOp>(term))
      return false;
    for (Block *successor : block.getSuccessors())
      ++incoming[successor];
  }
  SmallVector<Block *> ready;
  for (Block &block : function.getBody())
    if (!incoming[&block])
      ready.push_back(&block);
  for (size_t i = 0; i < ready.size(); ++i) {
    auto *term = ready[i]->getTerminator();
    if (isa<sim::SimReturnOp>(term) || latches.contains(term))
      continue;
    for (Block *successor : ready[i]->getSuccessors())
      if (!--incoming[successor])
        ready.push_back(successor);
  }
  return ready.size() == incoming.size();
}

OwningOpRef<sim::SimFuncOp>
cloneActivation(sim::SimFuncOp source, Operation *suspend, bool resume,
                IRMapping &mapping, ActivationRegionAnalysis::Trace trace) {
  auto copy = cast<sim::SimFuncOp>(source->clone(mapping));
  OwningOpRef<sim::SimFuncOp> owner(copy);
  DenseSet<Operation *> initialEffects;
  if (resume)
    for (Operation &op : source.getBody().front().without_terminator())
      if (!isMemoryEffectFree(&op))
        initialEffects.insert(mapping.lookup(&op));
  auto *wait = mapping.lookup(suspend);
  OpBuilder builder(wait);
  sim::SimReturnOp::create(builder, wait->getLoc(), ValueRange{});
  wait->erase();
  // The threading marker expresses where a constant was materialized, not
  // its value. It must not distinguish initial and resume computations.
  copy.walk([](Operation *op) { op->removeAttr("simulation.rematerialized"); });
  if (resume) {
    auto *entryTerminator = copy.getBody().front().getTerminator();
    if (!isa<cf::BranchOp>(entryTerminator))
      return {};
    builder.setInsertionPoint(entryTerminator);
    SemanticValueRootAnalysis roots(source);
    DenseMap<Value, Value> rematerialized;
    DenseSet<Value> visiting;
    auto materialize = [&](Value value, auto &materialize) -> Value {
      value = roots.lookup(value);
      if (auto argument = dyn_cast<BlockArgument>(value))
        return argument.getOwner() == &source.getBody().front()
                   ? mapping.lookup(argument)
                   : Value{};
      if (Value found = rematerialized.lookup(value))
        return found;
      auto *definition = value.getDefiningOp();
      if (!definition || !visiting.insert(value).second ||
          definition->getNumRegions() || !isMemoryEffectFree(definition) ||
          !isSpeculatable(definition))
        return {};
      IRMapping operands;
      for (Value operand : definition->getOperands()) {
        Value mapped = materialize(operand, materialize);
        if (!mapped)
          return {};
        operands.map(operand, mapped);
      }
      auto *cloned = builder.clone(*definition, operands);
      cloned->removeAttr("simulation.rematerialized");
      for (auto [from, to] :
           llvm::zip_equal(definition->getResults(), cloned->getResults()))
        rematerialized[from] = to;
      visiting.erase(value);
      return rematerialized.lookup(value);
    };
    auto branch = cast<BranchOpInterface>(suspend);
    SmallVector<Value> arguments;
    for (Value argument :
         branch.getSuccessorOperands(0).getForwardedOperands()) {
      Value mapped = materialize(argument, materialize);
      if (!mapped)
        return {};
      arguments.push_back(mapped);
    }
    cf::BranchOp::create(builder, source.getLoc(),
                         mapping.lookup(suspend->getSuccessor(0)), arguments);
    entryTerminator->erase();
  }
  IRRewriter rewriter(copy.getContext());
  (void)eraseUnreachableBlocks(rewriter, copy.getBody());
  // Suspension threading can introduce forwarding cycles through loop
  // headers. Collapse their proven SSA identities before structuring, so a
  // stable reference does not become a loop-carried region result.
  propagateActivationValueRoots(copy);
  if (!initialEffects.empty()) {
    // A live one-time snapshot is captured state, not a fresh resume read.
    // Sparse backward liveness follows CFG forwarding lanes, including dead
    // phi cycles, before any rewrite can merge loads or recycle op addresses.
    SymbolTableCollection symbols;
    DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
    solver.load<dataflow::DeadCodeAnalysis>();
    solver.load<dataflow::SparseConstantPropagation>();
    solver.load<dataflow::LivenessAnalysis>(symbols);
    if (failed(solver.initializeAndRun(copy)))
      return {};
    for (Operation *effect : initialEffects) {
      if (!wouldOpBeTriviallyDead(effect))
        return {};
      for (Value result : effect->getResults()) {
        auto *state = solver.lookupState<dataflow::Liveness>(result);
        if (!state || state->isLive)
          return {};
      }
    }
  }
  if (failed(canonicalizeControl(copy)))
    return {};
  if (trace)
    trace(resume ? ActivationRegionAnalysis::TracePhase::Resume
                 : ActivationRegionAnalysis::TracePhase::Initial,
          copy);
  return owner;
}

void unionRanges(Ranges &into, ArrayRef<ActivationMemoryAnalysis::Range> from) {
  llvm::append_range(into, from);
  llvm::sort(into, [](const auto &a, const auto &b) {
    return std::tie(a.root, a.low, a.width) < std::tie(b.root, b.low, b.width);
  });
  Ranges merged;
  for (auto range : into) {
    if (!merged.empty() && merged.back().root == range.root &&
        range.low <= merged.back().low + merged.back().width) {
      auto &last = merged.back();
      last.width =
          std::max(last.low + last.width, range.low + range.width) - last.low;
    } else
      merged.push_back(range);
  }
  into = std::move(merged);
}

bool equivalentActivations(sim::SimFuncOp lhs, sim::SimFuncOp rhs) {
  // CFG block order is not dominance order. Register corresponding SSA
  // definitions before comparing operands, including definitions in a loop
  // header printed after one of its dominated exit blocks.
  auto definitions = [](sim::SimFuncOp function) {
    SmallVector<Value> values;
    function.walk([&](Operation *op) {
      llvm::append_range(values, op->getResults());
      for (Region &region : op->getRegions())
        for (Block &block : region)
          llvm::append_range(values, block.getArguments());
    });
    return values;
  };
  auto left = definitions(lhs), right = definitions(rhs);
  if (left.size() != right.size())
    return false;
  IRMapping mapping;
  mapping.map(left, right);
  return OperationEquivalence::isEquivalentTo(
      lhs, rhs,
      [&](Value a, Value b) {
        return success(mapping.lookupOrDefault(a) == b);
      },
      nullptr, OperationEquivalence::IgnoreLocations);
}
} // namespace

StringRef
stringifyActivationFailure(ActivationRegionAnalysis::FailureReason reason) {
  using FailureReason = ActivationRegionAnalysis::FailureReason;
  switch (reason) {
  case FailureReason::None:
    return {};
  case FailureReason::ActivationShape:
    return "activation shape";
  case FailureReason::InitialResumeEquivalence:
    return "initial/resume equivalence";
  case FailureReason::Termination:
    return "termination";
  case FailureReason::MemoryEffects:
    return "memory effects";
  case FailureReason::CFGStructuring:
    return "CFG structuring";
  case FailureReason::UnsupportedRegionSSASlot:
    return "unsupported region SSA slot";
  case FailureReason::UnmappedStructuredMemoryAccess:
    return "unmapped structured memory access";
  }
  llvm_unreachable("unknown activation failure");
}

ActivationRegionAnalysis::ActivationRegionAnalysis(
    sim::SimFuncOp function, const HandleDataflowAnalysis &handles,
    Trace trace) {
  if (function.isExternal() || !function.getFunctionType().getResults().empty())
    return;
  unsigned waits = 0;
  bool returns = false;
  function.walk([&](Operation *op) {
    returns |= isa<sim::SimReturnOp>(op);
    if (sim::isSuspensionOp(op)) {
      ++waits;
      suspension = op;
    }
  });
  if (returns || waits != 1 ||
      !isa<sim::SimSuspendChangeOp, sim::SimSuspendAnyOp>(suspension))
    return;
  if (auto any = dyn_cast<sim::SimSuspendAnyOp>(suspension))
    if (llvm::any_of(any.getEdges(), [](int32_t edge) {
          return edge != static_cast<int32_t>(sim::EdgeKind::Change);
        }))
      return;
  IRMapping initialMap, resumeMap;
  auto initial =
      cloneActivation(function, suspension, false, initialMap, trace);
  auto resumed = cloneActivation(function, suspension, true, resumeMap, trace);
  if (!initial || !resumed ||
      !equivalentActivations(initial.get(), resumed.get())) {
    reason = FailureReason::InitialResumeEquivalence;
    return;
  }
  if (!finiteActivation(initial.get())) {
    reason = FailureReason::Termination;
    return;
  }
  // Canonicalization can merge CFG blocks and change a reference phi's range.
  // Recompute memory facts on this revision; original access-site facts must
  // never be projected through a rewrite that may have merged accesses.
  memory = ActivationMemoryAnalysis(initial.get(), handles);
  if (!memory.isValid()) {
    reason = FailureReason::MemoryEffects;
    return;
  }
  const auto &reads = memory.getReads();
  const auto &writes = memory.getWrites();
  // The dense solver visits every feasible activation path. Frontend slice
  // lowering may retain out-of-bounds arms that no proven induction value
  // reaches. Record those exact source sites before structuring; a newly
  // introduced or cloned access must still have its own effect mapping.
  DenseSet<Operation *> unreachableAccesses;
  initial->walk([&](Operation *op) {
    if ((isa<sim::SimRefLoadOp>(op) && !reads.contains(op)) ||
        (isa<sim::SimRefStoreOp>(op) && !writes.contains(op)))
      unreachableAccesses.insert(op);
  });
  auto view = func::FuncOp::create(function.getLoc(), "activation",
                                   function.getFunctionType());
  OwningOpRef<func::FuncOp> structuredView(view);
  view.getBody().takeBody(initial->getBody());
  for (Block &block : view.getBody()) {
    auto *term = block.getTerminator();
    if (!isa<sim::SimReturnOp>(term))
      continue;
    OpBuilder builder(term);
    func::ReturnOp::create(builder, term->getLoc());
    term->erase();
  }
  // Upstream structuring moves memory operations and repairs their SSA uses;
  // it does not clone/merge their executions. Do not run another canonicalizer
  // between this step and summarizing the moved access operations.
  SimulationControlFlowToSCF conversion;
  DominanceInfo dominance(view);
  if (failed(transformCFGToSCF(view.getBody(), conversion, dominance)) ||
      !llvm::hasSingleElement(view.getBody()) || failed(verify(view))) {
    reason = FailureReason::CFGStructuring;
    return;
  }
  if (view.walk([](ub::PoisonOp) { return WalkResult::interrupt(); })
          .wasInterrupted()) {
    reason = FailureReason::UnsupportedRegionSSASlot;
    return;
  }
  for (Operation &op : view.getBody().front().without_terminator()) {
    Node node{&op, {}, {}};
    bool accounted = true;
    op.walk([&](Operation *nested) {
      if (isa<sim::SimRefLoadOp>(nested)) {
        auto found = reads.find(nested);
        if (found == reads.end())
          accounted &= unreachableAccesses.contains(nested);
        else
          unionRanges(node.reads, found->second);
      } else if (isa<sim::SimRefStoreOp>(nested)) {
        auto found = writes.find(nested);
        if (found == writes.end())
          accounted &= unreachableAccesses.contains(nested);
        else
          unionRanges(node.writes, found->second);
      }
    });
    if (!accounted) {
      nodes.clear();
      reason = FailureReason::UnmappedStructuredMemoryAccess;
      return;
    }
    nodes.push_back(std::move(node));
  }
  structured = std::move(structuredView);
  reason = FailureReason::None;
}

namespace {
std::optional<SmallVector<ActivationRegionStep>>
orderSteps(ArrayRef<ActivationRegionStep> steps,
           SmallVector<std::pair<unsigned, unsigned>> edges,
           SmallVectorImpl<ActivationRegionStep> *cycle) {
  if (cycle)
    cycle->clear();
  llvm::sort(edges);
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
  SmallVector<SmallVector<unsigned>> successors(steps.size());
  SmallVector<unsigned> incoming(steps.size(), 0);
  for (auto [from, to] : edges) {
    successors[from].push_back(to);
    ++incoming[to];
  }
  std::priority_queue<unsigned, SmallVector<unsigned>, std::greater<unsigned>>
      ready;
  for (unsigned id = 0; id < steps.size(); ++id)
    if (!incoming[id])
      ready.push(id);
  SmallVector<ActivationRegionStep> result;
  while (!ready.empty()) {
    unsigned id = ready.top();
    ready.pop();
    result.push_back(steps[id]);
    for (unsigned next : successors[id])
      if (!--incoming[next])
        ready.push(next);
  }
  if (result.size() != steps.size()) {
    if (cycle) {
      // An iterative depth-first walk gives a deterministic directed witness,
      // including the source-order paths between cross-activation accesses.
      // Do this only when a caller requests cycle information.
      struct Frame {
        unsigned node;
        unsigned successor = 0;
      };
      SmallVector<uint8_t> color(steps.size(), 0);
      SmallVector<Frame> path;
      for (unsigned root = 0; root < steps.size() && cycle->empty(); ++root) {
        if (!incoming[root] || color[root])
          continue;
        color[root] = 1;
        path.push_back({root});
        while (!path.empty() && cycle->empty()) {
          auto &frame = path.back();
          if (frame.successor == successors[frame.node].size()) {
            color[frame.node] = 2;
            path.pop_back();
            continue;
          }
          unsigned next = successors[frame.node][frame.successor++];
          if (color[next] == 1) {
            auto start = llvm::find_if(
                path, [&](const Frame &entry) { return entry.node == next; });
            for (auto it = start; it != path.end(); ++it)
              cycle->push_back(steps[it->node]);
          } else if (!color[next]) {
            color[next] = 1;
            path.push_back({next});
          }
        }
      }
    }
    return std::nullopt;
  }
  return result;
}
} // namespace

std::optional<SmallVector<ActivationPublication>>
getActivationPublications(const ActivationRegionAnalysis &activation) {
  if (!activation.isValid())
    return std::nullopt;
  auto function = activation.getStructuredFunction();
  SmallVector<Value> inputs;
  function.walk(
      [&](sim::SimRefLoadOp load) { inputs.push_back(load.getResult()); });
  RegionValueDependencyAnalysis values(function, inputs);
  if (!values.isValid())
    return std::nullopt;
  SmallVector<ActivationPublication> publications;
  const auto &memory = activation.getMemory();
  function.walk([&](sim::SimRefStoreOp store) {
    auto writes = memory.getWrites().find(store);
    // Accesses absent from the validated memory inventory are unreachable for
    // every proven induction context. Structuring retains their dead arms.
    if (writes == memory.getWrites().end())
      return;
    ActivationPublication publication{store, {}, writes->second};
    for (Value input : values.getInputs(store)) {
      auto reads = memory.getReads().find(input.getDefiningOp());
      if (reads != memory.getReads().end())
        unionRanges(publication.reads, reads->second);
    }
    publications.push_back(std::move(publication));
  });
  return publications;
}

std::optional<SmallVector<ActivationRegionStep>> getActivationPublicationOrder(
    ArrayRef<const ActivationRegionAnalysis *> activations,
    SmallVectorImpl<ActivationRegionStep> *cycle) {
  if (cycle)
    cycle->clear();
  using Range = ActivationMemoryAnalysis::Range;
  SmallVector<ActivationRegionStep> steps;
  SmallVector<std::pair<unsigned, unsigned>> edges;
  DenseMap<uint64_t, SmallVector<std::pair<unsigned, Range>>> readers, writers;
  for (const auto *activation : activations) {
    auto publications = getActivationPublications(*activation);
    if (!publications)
      return std::nullopt;
    for (const auto &publication : *publications) {
      unsigned id = steps.size();
      steps.push_back({activation, publication.operation});
      for (const auto &range : publication.reads)
        readers[range.root].emplace_back(id, range);
      for (const auto &range : publication.writes)
        writers[range.root].emplace_back(id, range);
    }
  }
  for (const auto &[root, writes] : writers)
    for (const auto &[producer, write] : writes)
      for (const auto &[consumer, read] : readers[root])
        if (write.low < read.low + read.width &&
            read.low < write.low + write.width)
          edges.emplace_back(producer, consumer);
  return orderSteps(steps, std::move(edges), cycle);
}

std::optional<SmallVector<ActivationRegionStep>>
getActivationRegionOrder(ArrayRef<const ActivationRegionAnalysis *> activations,
                         SmallVectorImpl<ActivationRegionStep> *cycle) {
  if (cycle)
    cycle->clear();
  using Range = ActivationMemoryAnalysis::Range;
  SmallVector<ActivationRegionStep> steps;
  SmallVector<std::pair<unsigned, unsigned>> edges;
  DenseMap<uint64_t, SmallVector<std::pair<unsigned, Range>>> readers, writers;
  for (const auto *activation : activations) {
    if (!activation->isValid())
      return std::nullopt;
    std::optional<unsigned> previous;
    for (const auto &node : activation->getNodes()) {
      unsigned id = steps.size();
      steps.push_back({activation, node.operation});
      if (previous)
        edges.emplace_back(*previous, id);
      previous = id;
      for (const Range &range : node.reads)
        readers[range.root].emplace_back(id, range);
      for (const Range &range : node.writes)
        writers[range.root].emplace_back(id, range);
    }
  }
  for (const auto &[root, writes] : writers)
    for (const auto &[producer, write] : writes)
      for (const auto &[consumer, read] : readers[root])
        if (steps[producer].activation != steps[consumer].activation &&
            write.low < read.low + read.width &&
            read.low < write.low + write.width)
          edges.emplace_back(producer, consumer);
  return orderSteps(steps, std::move(edges), cycle);
}
} // namespace obelisk::analysis
