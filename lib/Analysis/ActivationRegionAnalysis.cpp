//===- ActivationRegionAnalysis.cpp - CFG activation regions -------------===//
#include "obelisk/Analysis/ActivationRegionAnalysis.h"
#include "mlir/Analysis/CFGLoopInfo.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/LivenessAnalysis.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/RegionGraphTraits.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/RegionUtils.h"
#include "obelisk/Analysis/ActivationControlFlow.h"
#include "obelisk/Analysis/BoundedLoopAnalysis.h"
#include "obelisk/Analysis/LocalStorageAnalysis.h"
#include "obelisk/Analysis/SSAValueAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SetVector.h"
#include <queue>
#include <set>

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
        name.getDialectNamespace() == "cf")
      name.getCanonicalizationPatterns(patterns, operation->getContext());
  return applyPatternsGreedily(operation, std::move(patterns));
}

/// An activation creates each flushed private slot from its root and may then
/// execute the slot's resumption refresh, a store of the same root load into
/// the fresh slot. Without an intervening access of the root or the slot that
/// store rewrites the initial contents; drop it so the initial and resumed
/// views match.
void dropRedundantSlotRefreshes(sim::SimFuncOp function) {
  Block &entry = function.getBody().front();
  SmallVector<Operation *> redundant;
  for (Operation &op : entry) {
    auto alloc = dyn_cast<sim::SimRefAllocOp>(op);
    if (!alloc || !alloc->hasAttr(sim::metadata::flushedPrivateSlot))
      continue;
    auto initial = alloc.getInitialValue().getDefiningOp<sim::SimRefLoadOp>();
    Value root = initial ? initial.getReference() : Value{};
    for (Operation *next = alloc->getNextNode(); next;
         next = next->getNextNode()) {
      auto store = dyn_cast<sim::SimRefStoreOp>(next);
      auto load = store ? store.getValue().getDefiningOp<sim::SimRefLoadOp>()
                        : sim::SimRefLoadOp{};
      if (store && store.getReference() == alloc.getResult() &&
          (store.getValue() == alloc.getInitialValue() ||
           (root && load && load.getReference() == root))) {
        redundant.push_back(store);
        break;
      }
      // Pure computations, loads, fresh slots and stores into other fresh
      // slots touch neither this slot nor its root.
      auto otherSlot = [&](Value reference) {
        auto slot = reference.getDefiningOp<sim::SimRefAllocOp>();
        return slot && slot != alloc;
      };
      if (!isMemoryEffectFree(next) &&
          !isa<sim::SimRefLoadOp, sim::SimRefAllocOp>(next) &&
          !(store && otherSlot(store.getReference())))
        break;
    }
  }
  for (Operation *store : redundant) {
    auto load = cast<sim::SimRefStoreOp>(store).getValue().getDefiningOp();
    store->erase();
    if (load && load->use_empty())
      load->erase();
  }
  // Place every slot, with its root load, immediately before its first use in
  // the entry block, so slot creation order does not distinguish the views.
  // Nothing in between writes the exclusive root or the fresh slot.
  SmallVector<sim::SimRefAllocOp> slots;
  for (Operation &op : entry)
    if (auto alloc = dyn_cast<sim::SimRefAllocOp>(op);
        alloc && alloc->hasAttr(sim::metadata::flushedPrivateSlot))
      slots.push_back(alloc);
  for (auto alloc : slots) {
    auto initial = alloc.getInitialValue().getDefiningOp<sim::SimRefLoadOp>();
    Operation *first = entry.getTerminator();
    for (Operation *user : alloc->getUsers())
      if (Operation *ancestor = entry.findAncestorOpInBlock(*user);
          ancestor && ancestor->isBeforeInBlock(first))
        first = ancestor;
    if (alloc->hasAttr(sim::metadata::activationDefaultSlot)) {
      // Fresh default scratch does not read canonical state. Sinking its
      // allocation past unrelated pure initializers gives both views the
      // same placement even when several slots share an aggregate default.
      alloc->moveBefore(first);
      continue;
    }
    if (!initial || initial->getBlock() != &entry || !initial->hasOneUse())
      continue;
    bool clear = true;
    for (Operation *op = alloc->getNextNode(); op && op != first && clear;
         op = op->getNextNode())
      if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
        clear = store.getReference() != initial.getReference();
      else
        clear = isMemoryEffectFree(op) ||
                isa<sim::SimRefLoadOp, sim::SimRefAllocOp>(op);
    if (!clear)
      continue;
    initial->moveBefore(first);
    alloc->moveBefore(first);
  }
}

OwningOpRef<sim::SimFuncOp>
cloneActivation(sim::SimFuncOp source, Operation *suspend, bool resume,
                IRMapping &mapping, ActivationRegionAnalysis::Trace trace,
                ActivationRegionAnalysis::ControlFlowPreparation control) {
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
      // A flushed private slot equals a fresh copy of its exclusive root at
      // every resumption: recreate it together with the root load.
      auto flushed = [](Operation *op) {
        return op && op->hasAttr(sim::metadata::flushedPrivateSlot);
      };
      bool slotInitializer = isa_and_nonnull<sim::SimRefLoadOp>(definition) &&
                             definition->hasOneUse() &&
                             flushed(*definition->getUsers().begin());
      if (!definition || !visiting.insert(value).second ||
          definition->getNumRegions() ||
          (!flushed(definition) && !slotInitializer &&
           (!isMemoryEffectFree(definition) || !isSpeculatable(definition))))
        return {};
      IRMapping operands;
      for (Value operand : definition->getOperands()) {
        Value mapped = materialize(operand, materialize);
        if (!mapped)
          return {};
        operands.map(operand, mapped);
      }
      // Immutable entry definitions already dominate the new resume edge.
      // Reuse them when their operands are unchanged, retaining canonical
      // definition order (including aggregate defaults shared by slots).
      // A snapshot load is never reused by this rule.
      bool reuse = definition->getBlock() == &source.getBody().front() &&
                   isMemoryEffectFree(definition) &&
                   isSpeculatable(definition) &&
                   llvm::all_of(definition->getOperands(), [&](Value operand) {
                     return operands.lookup(operand) == mapping.lookup(operand);
                   });
      auto *cloned = reuse ? mapping.lookup(definition)
                           : builder.clone(*definition, operands);
      cloned->removeAttr("simulation.rematerialized");
      for (auto [from, to] :
           llvm::zip_equal(definition->getResults(), cloned->getResults()))
        rematerialized[from] = to;
      visiting.erase(value);
      return rematerialized.lookup(value);
    };
    // Recreate flushed private slots in entry order so both activations
    // allocate them identically.
    for (Operation &op : source.getBody().front().without_terminator())
      if (op.hasAttr(sim::metadata::flushedPrivateSlot) &&
          !materialize(op.getResult(0), materialize))
        return {};
    auto branch = cast<BranchOpInterface>(suspend);
    SmallVector<Value> arguments;
    for (Value argument :
         branch.getSuccessorOperands(0).getForwardedOperands()) {
      Value mapped = materialize(argument, materialize);
      if (!mapped)
        return {};
      arguments.push_back(mapped);
    }
    // Unthreaded CFGs may use an entry slot directly instead of passing its
    // reference on the suspension edge. Rebind those uses to the recreated
    // slot too; otherwise the clone keeps an artificial bootstrap live-in.
    for (auto [original, recreated] : rematerialized)
      if (auto alloc = original.getDefiningOp<sim::SimRefAllocOp>();
          alloc && alloc->hasAttr(sim::metadata::flushedPrivateSlot))
        mapping.lookup(original).replaceAllUsesWith(recreated);
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
  dropRedundantSlotRefreshes(copy);
  // Normalize lowered break/continue dispatch only when it obstructs the
  // ordinary induction proof. Already admitted activations keep their CFG.
  if (control ==
          ActivationRegionAnalysis::ControlFlowPreparation::ThreadDispatch &&
      !hasFiniteControlFlow(copy.getBody()))
    while (threadActivationControlFlow(copy.getBody())) {
      if (failed(canonicalizeControl(copy)))
        return {};
    }
  if (trace)
    trace(resume ? ActivationRegionAnalysis::TracePhase::Resume
                 : ActivationRegionAnalysis::TracePhase::Initial,
          copy);
  return owner;
}

/// Give every top-level loop a dedicated entry block and one landing block
/// per exit edge, so the blocks around a loop bound it as a unit. Canonical
/// pass-through forwarding otherwise lets a loop exit straight into the next
/// loop's header, fusing both loops into one scheduling unit. Entry operands
/// pass through unchanged, keeping a constant induction start visible.
void separateTopLevelLoops(sim::SimFuncOp function) {
  Region &body = function.getBody();
  SmallVector<SmallVector<Block *>> loops;
  for (auto scc = llvm::scc_begin(&body); !scc.isAtEnd(); ++scc)
    if (scc.hasCycle())
      loops.emplace_back(scc->begin(), scc->end());
  OpBuilder builder(function.getContext());
  auto splitEdge = [&](Block *from, unsigned successor) {
    auto branch = cast<BranchOpInterface>(from->getTerminator());
    Block *to = from->getTerminator()->getSuccessor(successor);
    SmallVector<Value> operands(
        branch.getSuccessorOperands(successor).getForwardedOperands());
    auto *landing = new Block;
    body.getBlocks().insert(to->getIterator(), landing);
    builder.setInsertionPointToEnd(landing);
    cf::BranchOp::create(builder, from->getTerminator()->getLoc(), to,
                         operands);
    branch.getSuccessorOperands(successor).erase(0, operands.size());
    from->getTerminator()->setSuccessor(landing, successor);
  };
  for (const auto &loop : loops) {
    DenseSet<Block *> members(loop.begin(), loop.end());
    SmallVector<std::pair<Block *, unsigned>> entries, exits;
    for (Block *block : loop) {
      for (auto it = block->pred_begin(); it != block->pred_end(); ++it)
        if (!members.contains(*it))
          entries.push_back({*it, it.getSuccessorIndex()});
      for (auto [index, next] : llvm::enumerate(block->getSuccessors()))
        if (!members.contains(next))
          exits.push_back({block, unsigned(index)});
    }
    // A loop already entered by the sole edge of a straight-line block has
    // its boundary; several entries would need a merged entry block.
    if (entries.size() == 1 && entries.front().first->getNumSuccessors() > 1)
      splitEdge(entries.front().first, entries.front().second);
    for (auto [block, index] : exits)
      splitEdge(block, index);
  }
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
  case FailureReason::UnmappedMemoryAccess:
    return "unmapped memory access";
  }
  llvm_unreachable("unknown activation failure");
}

ActivationRegionAnalysis::ActivationRegionAnalysis(
    sim::SimFuncOp function, const HandleDataflowAnalysis &handles, Trace trace,
    ActivationMemoryAnalysis::DependencyMode mode,
    ControlFlowPreparation control) {
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
      cloneActivation(function, suspension, false, initialMap, trace, control);
  auto resumed =
      cloneActivation(function, suspension, true, resumeMap, trace, control);
  if (!initial || !resumed ||
      !equivalentActivations(initial.get(), resumed.get())) {
    reason = FailureReason::InitialResumeEquivalence;
    return;
  }
  if (!hasFiniteControlFlow(initial->getBody())) {
    reason = FailureReason::Termination;
    return;
  }
  separateTopLevelLoops(*initial);
  // Canonicalization can merge CFG blocks and change a reference phi's range.
  // Recompute memory facts on this revision; original access-site facts must
  // never be projected through a rewrite that may have merged accesses.
  memory = ActivationMemoryAnalysis(initial.get(), handles, 65536, mode,
                                    function->getParentOp());
  if (!memory.isValid()) {
    // Failure diagnostics retain the unsupported operation as well.
    view = std::move(initial);
    reason = FailureReason::MemoryEffects;
    return;
  }
  const auto &reads = memory.getReads();
  const auto &writes = memory.getWrites();
  // The dense solver visits every feasible activation path. Frontend slice
  // lowering may retain out-of-bounds arms that no proven induction value
  // reaches. Record those exact source sites; a newly introduced or cloned
  // access must still have its own effect mapping.
  DenseSet<Operation *> unreachableAccesses;
  initial->walk([&](Operation *op) {
    if ((isa<sim::SimRefLoadOp, sim::SimNetReadOp>(op) &&
         !reads.contains(op)) ||
        (isa<sim::SimRefStoreOp, sim::SimDriverDriveOp>(op) &&
         !writes.contains(op)))
      unreachableAccesses.insert(op);
  });
  Region &body = initial->getBody();
  Block *exit = nullptr;
  for (Block &block : body)
    if (isa<sim::SimReturnOp>(block.getTerminator())) {
      if (exit)
        return;
      exit = &block;
    }
  if (!exit)
    return;
  // A block on a CFG cycle executes a variable number of times, so it never
  // bounds a top-level statement. Every remaining dominator of the exit runs
  // exactly once on every path through the activation.
  DenseSet<Block *> cyclic;
  for (auto scc = llvm::scc_begin(&body); !scc.isAtEnd(); ++scc)
    if (scc.hasCycle())
      cyclic.insert(scc->begin(), scc->end());
  SmallVector<Block *> chain;
  if (body.hasOneBlock()) {
    chain.push_back(exit);
  } else {
    DominanceInfo dominance(*initial);
    for (auto *node = dominance.getNode(exit); node; node = node->getIDom())
      if (Block *block = node->getBlock(); block && !cyclic.contains(block))
        chain.push_back(block);
    std::reverse(chain.begin(), chain.end());
  }
  if (chain.empty() || chain.front() != &body.front())
    return;
  DenseSet<Block *> covered(chain.begin(), chain.end());
  auto addNode = [&](Node node) {
    unsigned id = nodes.size();
    nodes.push_back(std::move(node));
    nodes.back().walk([&](Operation *op) { nodeIndices[op] = id; });
    return id;
  };
  for (auto [index, block] : llvm::enumerate(chain)) {
    for (Operation &op : block->without_terminator())
      addNode({&op, {}, nullptr, {}, {}});
    if (block == exit)
      break;
    // Every block strictly between two chain blocks belongs to the region
    // entered by the first block's branch; the next chain block is its join.
    Block *join = chain[index + 1];
    Node region{block->getTerminator(), {}, join, {}, {}};
    SmallVector<Block *> pending(block->getSuccessors());
    DenseSet<Block *> inside;
    while (!pending.empty()) {
      Block *next = pending.pop_back_val();
      if (next == join || !inside.insert(next).second)
        continue;
      if (covered.contains(next))
        return;
      llvm::append_range(pending, next->getSuccessors());
    }
    for (Block &candidate : body)
      if (inside.contains(&candidate)) {
        region.blocks.push_back(&candidate);
        covered.insert(&candidate);
      }
    unsigned id = addNode(std::move(region));
    for (Block *inner : nodes[id].blocks)
      argumentNodes[inner] = id;
    argumentNodes[join] = id;
  }
  if (covered.size() != body.getBlocks().size()) {
    nodes.clear();
    nodeIndices.clear();
    argumentNodes.clear();
    return;
  }
  for (Node &node : nodes) {
    bool accounted = true;
    node.walk([&](Operation *nested) {
      if (isa<sim::SimRefLoadOp, sim::SimNetReadOp, sim::SimCallOp>(nested)) {
        auto found = reads.find(nested);
        if (found == reads.end())
          accounted &= isa<sim::SimCallOp>(nested) ||
                       unreachableAccesses.contains(nested);
        else
          unionRanges(node.reads, found->second);
      } else if (isa<sim::SimRefStoreOp, sim::SimDriverDriveOp>(nested)) {
        auto found = writes.find(nested);
        if (found == writes.end())
          accounted &= unreachableAccesses.contains(nested);
        else
          unionRanges(node.writes, found->second);
      }
    });
    if (!accounted) {
      nodes.clear();
      nodeIndices.clear();
      argumentNodes.clear();
      reason = FailureReason::UnmappedMemoryAccess;
      return;
    }
  }
  view = std::move(initial);
  reason = FailureReason::None;
}

void ActivationRegionAnalysis::Node::walk(
    function_ref<void(Operation *)> callback) const {
  operation->walk(callback);
  for (Block *block : blocks)
    for (Operation &op : *block)
      op.walk(callback);
}

const ActivationRegionAnalysis::Node *
ActivationRegionAnalysis::getNode(Operation *operation) const {
  auto found = nodeIndices.find(operation);
  return found == nodeIndices.end() ? nullptr : &nodes[found->second];
}

const ActivationRegionAnalysis::Node *
ActivationRegionAnalysis::getDefiningNode(Value value) const {
  if (Operation *definition = value.getDefiningOp())
    return getNode(definition);
  auto found = argumentNodes.find(cast<BlockArgument>(value).getOwner());
  return found == argumentNodes.end() ? nullptr : &nodes[found->second];
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

namespace {
/// Loads an operation depends on through SSA values, CFG block arguments and
/// the branches that decide whether it executes. Block arguments depend on
/// every incoming operand and on the branches selecting the incoming edge.
class ActivationLoadSlice {
public:
  ActivationLoadSlice(sim::SimFuncOp function,
                      const ActivationMemoryAnalysis &memory)
      : ActivationLoadSlice(function, memory.getReads(), memory.getWrites(),
                            &memory.getPrivateReads()) {}
  ActivationLoadSlice(sim::SimFuncOp function,
                      const DenseMap<Operation *, Ranges> &reads,
                      const DenseMap<Operation *, Ranges> &writes,
                      const DenseSet<Operation *> *privateReads = nullptr)
      : reads(reads), privateReads(privateReads), locals(function) {
    // Accesses outside the design inventory are private slots (or dead
    // arms). A private load may observe any private store of the activation.
    function.walk([&](sim::SimRefStoreOp store) {
      if (!writes.contains(store))
        privateStores.push_back(store);
    });
    if (function.getBody().hasOneBlock())
      return;
    PostDominanceInfo postDominance(function);
    for (Block &block : function.getBody()) {
      Operation *term = block.getTerminator();
      if (!isa<cf::CondBranchOp, cf::SwitchOp>(term))
        continue;
      auto *node = postDominance.getNode(&block);
      auto *stop = node ? node->getIDom() : nullptr;
      for (Block *successor : block.getSuccessors())
        for (auto *runner = postDominance.getNode(successor);
             runner && runner != stop; runner = runner->getIDom())
          if (Block *controlled = runner->getBlock())
            controllers[controlled].insert(term);
    }
  }
  void collect(Operation *root, SmallVectorImpl<Operation *> &loads,
               DenseSet<Operation *> *required = nullptr) const {
    SmallVector<Operation *> pendingOps{root};
    SmallVector<Value> pendingValues;
    DenseSet<Operation *> seenOps{root};
    DenseSet<Value> seenValues;
    DenseSet<Block *> seenBlocks;
    auto visitBlock = [&](Block *block) {
      if (!seenBlocks.insert(block).second)
        return;
      for (Operation *term : controllers.lookup(block))
        if (seenOps.insert(term).second)
          pendingOps.push_back(term);
    };
    while (!pendingOps.empty() || !pendingValues.empty()) {
      if (!pendingOps.empty()) {
        Operation *op = pendingOps.pop_back_val();
        if (isa<sim::SimRefLoadOp, sim::SimNetReadOp, sim::SimCallOp>(op) &&
            reads.contains(op))
          loads.push_back(op);
        if (auto load = dyn_cast<sim::SimRefLoadOp>(op);
            load && (!reads.contains(op) ||
                     (privateReads && privateReads->contains(op)))) {
          auto root = locals.lookup(load.getReference());
          for (Operation *store : privateStores)
            if ((!root ||
                 locals.lookup(
                     cast<sim::SimRefStoreOp>(store).getReference()) == root) &&
                seenOps.insert(store).second)
              pendingOps.push_back(store);
        }
        if (isa<sim::SimCallOp>(op) &&
            llvm::any_of(op->getOperandTypes(),
                         [](Type type) { return isa<sim::RefType>(type); }))
          for (Operation *store : privateStores)
            if (seenOps.insert(store).second)
              pendingOps.push_back(store);
        visitBlock(op->getBlock());
        op->walk([&](Operation *nested) {
          // Successor operands are separate SSA lanes. Visiting a branch as
          // a control dependency must not import every other forwarded lane.
          auto visit = [&](Value operand) {
            if (seenValues.insert(operand).second)
              pendingValues.push_back(operand);
          };
          if (auto branch = dyn_cast<cf::CondBranchOp>(nested))
            visit(branch.getCondition());
          else if (auto branch = dyn_cast<cf::SwitchOp>(nested))
            visit(branch.getFlag());
          else if (!isa<cf::BranchOp>(nested))
            for (Value operand : nested->getOperands())
              visit(operand);
        });
        continue;
      }
      Value value = pendingValues.pop_back_val();
      if (Operation *definition = value.getDefiningOp()) {
        if (seenOps.insert(definition).second)
          pendingOps.push_back(definition);
        continue;
      }
      Block *block = cast<BlockArgument>(value).getOwner();
      visitBlock(block);
      unsigned index = cast<BlockArgument>(value).getArgNumber();
      for (auto it = block->pred_begin(); it != block->pred_end(); ++it) {
        Operation *term = (*it)->getTerminator();
        if (seenOps.insert(term).second)
          pendingOps.push_back(term);
        auto branch = dyn_cast<BranchOpInterface>(term);
        if (!branch)
          continue;
        Value incoming = branch.getSuccessorOperands(it.getSuccessorIndex())
                             .getForwardedOperands()[index];
        if (seenValues.insert(incoming).second)
          pendingValues.push_back(incoming);
      }
    }
    if (required)
      required->insert(seenOps.begin(), seenOps.end());
  }

private:
  const DenseMap<Operation *, Ranges> &reads;
  const DenseSet<Operation *> *privateReads;
  LocalStorageAnalysis locals;
  SmallVector<Operation *> privateStores;
  DenseMap<Block *, llvm::SetVector<Operation *>> controllers;
};

/// Source-written temporaries can be reconstructed independently in each
/// slice when every read follows a definition in this activation. Keep their
/// source-ordered versions in private SSA-promotable storage, and publish only
/// the selected stores. This permits a temporary to feed independent outputs
/// without merging them into one unit, while preserving saved snapshots.
DenseMap<Value, uint64_t>
getActivationTemporaryRoots(sim::SimFuncOp function,
                            const ActivationMemoryAnalysis &memory) {
  DenseSet<uint64_t> roots, readRoots;
  for (auto range : memory.getMayWrites())
    if (range.root < ActivationMemoryAnalysis::netRootTag)
      roots.insert(range.root);
  for (const auto &[op, ranges] : memory.getReads())
    for (auto range : ranges)
      readRoots.insert(range.root);
  for (auto range : memory.getLiveIns())
    roots.erase(range.root);
  for (const auto &[op, ranges] : memory.getReads())
    if (!isa<sim::SimRefLoadOp>(op))
      for (auto range : ranges)
        roots.erase(range.root);
  for (auto root : llvm::make_early_inc_range(roots))
    if (!readRoots.contains(root))
      roots.erase(root);
  DenseMap<Value, uint64_t> bases;
  if (roots.empty())
    return bases;
  auto facts = deriveHandleFacts(function);
  DenseMap<uint64_t, Type> rootTypes;
  for (auto [value, fact] : facts) {
    auto ref = dyn_cast<sim::RefType>(value.getType());
    bool entry =
        isa<BlockArgument>(value)
            ? cast<BlockArgument>(value).getOwner() ==
                  &function.getBody().front()
            : isa_and_nonnull<sim::SimContextStorageOp>(value.getDefiningOp());
    if (ref && entry && fact.descriptor && roots.contains(*fact.descriptor) &&
        !fact.dynamic && fact.low == 0 && fact.width == fact.rootWidth &&
        (sim::isAggregateType(ref.getElementType()) ||
         isa<IntegerType, sim::LogicType>(ref.getElementType()))) {
      auto [found, inserted] = rootTypes.try_emplace(*fact.descriptor, ref);
      if (!inserted && found->second != ref)
        roots.erase(*fact.descriptor);
      bases[value] = *fact.descriptor;
    }
  }
  auto reconstructible = [&](Value reference, uint64_t root,
                             auto &reconstructible) -> bool {
    if (auto found = bases.find(reference); found != bases.end())
      return found->second == root;
    Operation *definition = reference.getDefiningOp();
    if (!definition || !isMemoryEffectFree(definition) ||
        definition->getNumRegions())
      return false;
    bool hasReference = false;
    for (Value operand : definition->getOperands())
      if (isa<sim::RefType>(operand.getType())) {
        hasReference = true;
        if (!reconstructible(operand, root, reconstructible))
          return false;
      }
    return hasReference;
  };
  for (const auto *accesses : {&memory.getReads(), &memory.getWrites()})
    for (const auto &[op, ranges] : *accesses)
      for (auto range : ranges)
        if (roots.contains(range.root)) {
          Value reference;
          if (auto load = dyn_cast<sim::SimRefLoadOp>(op))
            reference = load.getReference();
          else if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
            reference = store.getReference();
          if (!reference ||
              !reconstructible(reference, range.root, reconstructible))
            roots.erase(range.root);
        }
  for (auto [value, root] : llvm::make_early_inc_range(bases))
    if (!roots.contains(root))
      bases.erase(value);
  return bases;
}

OwningOpRef<sim::SimFuncOp>
cloneSliceWithTemporaries(const ActivationSlice &slice, IRMapping &mapping,
                          const DenseMap<Value, uint64_t> &temporaries);

/// The source activation already has a termination certificate. Slicing and
/// dialect canonicalization preserve it, even when they rotate a dead loop
/// into a different recurrence shape. A loop and its exit paths can therefore
/// be bypassed when they have no effects or escaping values and all paths
/// forward the same available values to their common postdominator.
bool removeEmptyFiniteLoop(sim::SimFuncOp function) {
  if (function.getBody().hasOneBlock())
    return false;
  DominanceInfo dominance;
  PostDominanceInfo postDominance;
  CFGLoopInfo loopInfo(dominance.getDomTree(&function.getBody()));
  auto preorder = loopInfo.getLoopsInPreorder();
  for (auto *loop : llvm::reverse(preorder)) {
    SmallVector<Block *> exits;
    loop->getExitBlocks(exits);
    if (exits.empty())
      continue;
    Block *join = exits.front();
    for (Block *exit : llvm::drop_begin(exits)) {
      join = postDominance.findNearestCommonDominator(join, exit);
      if (!join)
        break;
    }
    Block *header = loop->getHeader();
    if (!join || loop->contains(join))
      continue;
    DenseSet<Block *> members;
    SmallVector<Block *> blocks, pending{header};
    while (!pending.empty()) {
      Block *block = pending.pop_back_val();
      if (block == join || !members.insert(block).second)
        continue;
      blocks.push_back(block);
      llvm::append_range(pending, block->getSuccessors());
    }
    bool removable = true;
    std::optional<SmallVector<Value>> exitValues;
    for (Block *block : blocks) {
      auto escapes = [&](Value value) {
        return llvm::any_of(value.getUsers(), [&](Operation *user) {
          return !members.contains(user->getBlock());
        });
      };
      for (Operation &op : *block) {
        if (!isa<cf::BranchOp, cf::CondBranchOp, cf::SwitchOp>(op) &&
            !wouldOpBeTriviallyDead(&op))
          removable = false;
        if (llvm::any_of(op.getResults(), escapes))
          removable = false;
      }
      if (llvm::any_of(block->getArguments(), escapes))
        removable = false;
      auto branch = dyn_cast<BranchOpInterface>(block->getTerminator());
      if (!branch || block->getNumSuccessors() == 0) {
        removable = false;
        continue;
      }
      for (auto [index, successor] : llvm::enumerate(block->getSuccessors())) {
        if (successor != join)
          continue;
        SmallVector<Value> operands(
            branch.getSuccessorOperands(index).getForwardedOperands());
        if (!llvm::all_of(operands, [&](Value value) {
              return !members.contains(value.getParentBlock()) &&
                     dominance.dominates(value, header->getTerminator());
            }))
          removable = false;
        if (exitValues && *exitValues != operands)
          removable = false;
        else
          exitValues = std::move(operands);
      }
    }
    if (!removable || !exitValues)
      continue;
    Operation *terminator = header->getTerminator();
    OpBuilder builder(terminator);
    cf::BranchOp::create(builder, terminator->getLoc(), join, *exitValues);
    terminator->erase();
    IRRewriter rewriter(function.getContext());
    (void)eraseUnreachableBlocks(rewriter, function.getBody());
    return true;
  }
  return false;
}

std::optional<Ranges>
getSliceReads(const ActivationSlice &slice,
              const DenseMap<Value, uint64_t> *temporaries = nullptr) {
  IRMapping mapping;
  auto copy = temporaries
                  ? cloneSliceWithTemporaries(slice, mapping, *temporaries)
                  : cloneActivationSlice(slice, &mapping);
  if (!copy)
    return std::nullopt;
  DenseMap<Operation *, Ranges> mappedReads;
  for (const auto &[op, ranges] : slice.activation->getMemory().getReads())
    mappedReads[mapping.lookup(op)] = ranges;
  LocalStorageAnalysis locals(*copy);
  Ranges reads;
  copy->walk([&](Operation *op) {
    if (isa<sim::SimRefLoadOp, sim::SimNetReadOp, sim::SimCallOp>(op) &&
        !locals.isLocalAccess(op))
      if (auto found = mappedReads.find(op); found != mappedReads.end())
        unionRanges(reads, found->second);
  });
  return reads;
}
} // namespace

std::optional<SmallVector<ActivationPublication>>
getActivationPublications(const ActivationRegionAnalysis &activation) {
  if (!activation.isValid() || activation.getMemory().hasMixedPrivateWrites())
    return std::nullopt;
  auto function = activation.getActivationFunction();
  const auto &memory = activation.getMemory();
  if (memory.hasValueDependencies()) {
    SmallVector<ActivationPublication> publications;
    function.walk([&](Operation *store) {
      auto found = memory.getPublications().find(store);
      if (found != memory.getPublications().end())
        for (const auto &lane : found->second)
          publications.push_back({store, lane.inputs, {lane.output}});
    });
    return publications;
  }
  ActivationLoadSlice slice(function, memory);
  SmallVector<ActivationPublication> publications;
  function.walk([&](Operation *store) {
    auto writes = memory.getWrites().find(store);
    // Accesses absent from the validated memory inventory are unreachable for
    // every proven induction context. The CFG retains their dead arms.
    if (writes == memory.getWrites().end())
      return;
    ActivationPublication publication{store, {}, writes->second};
    SmallVector<Operation *> loads;
    slice.collect(store, loads);
    for (Operation *load : loads) {
      auto reads = memory.getReads().find(load);
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
  struct Access {
    unsigned step;
    Range range;
    schedule::SourceOwnerAttr owner;
  };
  SmallVector<ActivationRegionStep> steps;
  SmallVector<std::pair<unsigned, unsigned>> edges;
  DenseMap<uint64_t, SmallVector<Access>> readers, writers;
  for (const auto *activation : activations) {
    if (!activation->isValid())
      return std::nullopt;
    const auto &memory = activation->getMemory();
    bool hasOwners = true;
    activation->getActivationFunction().walk([&](Operation *op) {
      if ((memory.getReads().contains(op) || memory.getWrites().contains(op)) &&
          !schedule::get<schedule::Field::EvalSourceOwner>(op))
        hasOwners = false;
    });
    unsigned first = steps.size();
    DenseMap<schedule::SourceOwnerAttr, unsigned> previous;
    for (const auto &node : activation->getNodes()) {
      unsigned id = steps.size();
      steps.push_back({activation, node.operation});
      llvm::SetVector<schedule::SourceOwnerAttr> owners;
      if (!hasOwners)
        owners.insert({});
      node.walk([&](Operation *op) {
        auto owner = hasOwners
                         ? schedule::get<schedule::Field::EvalSourceOwner>(op)
                         : schedule::SourceOwnerAttr{};
        if (owner)
          owners.insert(owner);
        auto record = [&](const auto &accesses, auto &inventory) {
          auto found = accesses.find(op);
          if (found != accesses.end())
            for (Range range : found->second)
              inventory[range.root].push_back({id, range, owner});
        };
        record(memory.getReads(), readers);
        record(memory.getWrites(), writers);
      });
      // Fusion does not create source-order relations between the original
      // processes. Preserve each owner's order, including owners sharing a
      // unit. Incomplete provenance keeps the whole-body chain.
      for (auto owner : owners) {
        auto found = previous.find(owner);
        if (found != previous.end())
          edges.emplace_back(found->second, id);
        previous[owner] = id;
      }
    }
    // Pure SSA producers may have been shared by CSE. Their def-use edges are
    // required independently of publication provenance, including a region's
    // join arguments and values captured by nested operations.
    auto nodeIndex = [&](const ActivationRegionAnalysis::Node *node) {
      return first + unsigned(node - activation->getNodes().data());
    };
    for (const auto &node : activation->getNodes()) {
      unsigned consumer = nodeIndex(&node);
      node.walk([&](Operation *op) {
        for (Value operand : op->getOperands())
          if (const auto *producer = activation->getDefiningNode(operand);
              producer && producer != &node)
            edges.emplace_back(nodeIndex(producer), consumer);
      });
    }
  }
  auto overlaps = [](Range a, Range b) {
    return a.low < b.low + b.width && b.low < a.low + a.width;
  };
  for (const auto &[root, writes] : writers)
    for (const auto &write : writes) {
      for (const auto &read : readers[root])
        if (write.step != read.step &&
            (steps[write.step].activation != steps[read.step].activation ||
             write.owner != read.owner) &&
            overlaps(write.range, read.range))
          edges.emplace_back(write.step, read.step);
      // If distinct original owners publish overlapping ranges, provenance
      // alone cannot justify changing their order within an existing kernel.
      for (const auto &later : writes)
        if (write.step < later.step && write.owner != later.owner &&
            steps[write.step].activation == steps[later.step].activation &&
            overlaps(write.range, later.range))
          edges.emplace_back(write.step, later.step);
    }
  return orderSteps(steps, std::move(edges), cycle);
}
std::optional<SmallVector<ActivationSlice>>
getActivationSliceOrder(ArrayRef<const ActivationRegionAnalysis *> activations,
                        SmallVectorImpl<ActivationRegionStep> *cycle) {
  if (cycle)
    cycle->clear();
  struct StoreNode {
    const ActivationRegionAnalysis *activation;
    SmallVector<Operation *> stores;
    Ranges reads, writes;
  };
  auto overlaps = [](const Ranges &a, const Ranges &b) {
    for (const auto &x : a)
      for (const auto &y : b)
        if (x.root == y.root && x.low < y.low + y.width &&
            y.low < x.low + x.width)
          return true;
    return false;
  };
  SmallVector<StoreNode> nodes;
  for (const auto *activation : activations) {
    if (!activation->isValid())
      return std::nullopt;
    const auto &memory = activation->getMemory();
    // IEEE 1800-2023 4.6 preserves source-local store/load ordering. A store
    // selecting between a private slot and canonical storage participates in
    // both dependency graphs. Until slicing models that coupling, retain the
    // whole activation instead of dropping the private update in another slice.
    if (memory.hasMixedPrivateWrites())
      return std::nullopt;
    ActivationLoadSlice slice(activation->getActivationFunction(), memory);
    auto temporaries = getActivationTemporaryRoots(
        activation->getActivationFunction(), memory);
    DenseMap<uint64_t, SmallVector<Operation *>> temporaryWriters;
    for (auto [base, root] : temporaries)
      temporaryWriters.try_emplace(root);
    DenseMap<Operation *, Ranges> sourceReads;
    activation->getActivationFunction().walk([&](Operation *store) {
      auto writes = memory.getWrites().find(store);
      if (writes == memory.getWrites().end())
        return;
      SmallVector<Operation *> loads;
      slice.collect(store, loads);
      for (Operation *load : loads)
        if (auto reads = memory.getReads().find(load);
            reads != memory.getReads().end())
          unionRanges(sourceReads[store], reads->second);
      for (auto range : writes->second)
        if (auto found = temporaryWriters.find(range.root);
            found != temporaryWriters.end())
          found->second.push_back(store);
    });
    SmallVector<StoreNode> local;
    activation->getActivationFunction().walk([&](Operation *store) {
      auto writes = memory.getWrites().find(store);
      if (writes == memory.getWrites().end())
        return;
      StoreNode node{activation, {store}, {}, writes->second};
      // Follow reconstructed temporary definitions in the immutable summary.
      // Only cycle witnesses need executable clones and CFG simplification;
      // cloning every publication would make planning quadratic in body size.
      SmallVector<Operation *> pending{store};
      DenseSet<Operation *> seen{store};
      while (!pending.empty()) {
        Operation *definition = pending.pop_back_val();
        for (auto read : sourceReads.lookup(definition)) {
          auto temporary = temporaryWriters.find(read.root);
          if (temporary == temporaryWriters.end()) {
            unionRanges(node.reads, {read});
            continue;
          }
          for (Operation *writer : temporary->second)
            if (overlaps({read}, memory.getWrites().lookup(writer)) &&
                seen.insert(writer).second)
              pending.push_back(writer);
        }
      }
      // Keep all remaining source-local memory dependence in one slice.
      // Reconstructed private versions have no canonical read dependency;
      // all other snapshots still need their source stores in the same slice.
      // Restart after merging, since a newly included range can connect a
      // previously disjoint group.
      for (bool changed = true; changed;) {
        changed = false;
        for (auto it = local.begin(); it != local.end();) {
          if (!overlaps(it->writes, node.writes) &&
              !overlaps(it->writes, node.reads) &&
              !overlaps(it->reads, node.writes)) {
            ++it;
            continue;
          }
          llvm::append_range(node.stores, it->stores);
          unionRanges(node.reads, it->reads);
          unionRanges(node.writes, it->writes);
          it = local.erase(it);
          changed = true;
        }
      }
      local.push_back(std::move(node));
    });
    for (auto &node : local)
      nodes.push_back(std::move(node));
  }
  SmallVector<ActivationRegionStep> steps;
  for (const auto &node : nodes)
    steps.push_back({node.activation, node.stores.front()});
  SmallVector<std::pair<unsigned, unsigned>> edges;
  DenseMap<uint64_t, SmallVector<unsigned>> readers;
  for (auto [id, node] : llvm::enumerate(nodes))
    for (const auto &read : node.reads)
      readers[read.root].push_back(id);
  for (auto [id, node] : llvm::enumerate(nodes))
    for (const auto &write : node.writes)
      for (unsigned reader : readers.lookup(write.root))
        if (reader != id && overlaps({write}, nodes[reader].reads))
          edges.emplace_back(id, reader);
  DenseSet<unsigned> refined;
  while (true) {
    SmallVector<ActivationRegionStep> witness;
    if (orderSteps(steps, edges, &witness))
      break;
    bool changed = false;
    for (const auto &step : witness) {
      auto found = llvm::find_if(nodes, [&](const StoreNode &node) {
        return node.activation == step.activation &&
               node.stores.front() == step.operation;
      });
      unsigned index = found - nodes.begin();
      if (!refined.insert(index).second)
        continue;
      auto reads = getSliceReads({found->activation, found->stores});
      if (!reads)
        continue;
      found->reads = std::move(*reads);
      changed = true;
    }
    if (!changed) {
      if (cycle)
        llvm::append_range(*cycle, witness);
      return std::nullopt;
    }
    // Rebuild only dependencies. Store grouping protects source-local
    // snapshots and is never weakened by control-flow simplification.
    edges.clear();
    readers.clear();
    for (auto [id, node] : llvm::enumerate(nodes))
      for (const auto &read : node.reads)
        readers[read.root].push_back(id);
    for (auto [id, node] : llvm::enumerate(nodes))
      for (const auto &write : node.writes)
        for (unsigned reader : readers.lookup(write.root))
          if (reader != id && overlaps({write}, nodes[reader].reads))
            edges.emplace_back(id, reader);
  }
  // Topological order that continues with the previous activation whenever
  // possible, so each process is split into as few slices as possible.
  llvm::sort(edges);
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
  SmallVector<SmallVector<unsigned>> successors(nodes.size());
  SmallVector<unsigned> incoming(nodes.size(), 0);
  for (auto [from, to] : edges) {
    successors[from].push_back(to);
    ++incoming[to];
  }
  std::set<unsigned> ready;
  for (unsigned id = 0; id < nodes.size(); ++id)
    if (!incoming[id])
      ready.insert(id);
  SmallVector<ActivationSlice> slices;
  while (!ready.empty()) {
    auto next = ready.begin();
    if (!slices.empty())
      for (auto it = ready.begin(); it != ready.end(); ++it)
        if (nodes[*it].activation == slices.back().activation) {
          next = it;
          break;
        }
    unsigned id = *next;
    ready.erase(next);
    if (slices.empty() || slices.back().activation != nodes[id].activation)
      slices.push_back({nodes[id].activation, {}});
    llvm::append_range(slices.back().stores, nodes[id].stores);
    for (unsigned successor : successors[id])
      if (!--incoming[successor])
        ready.insert(successor);
  }
  return slices;
}

namespace {
OwningOpRef<sim::SimFuncOp>
cloneSliceWithTemporaries(const ActivationSlice &slice,
                          IRMapping &mappingStorage,
                          const DenseMap<Value, uint64_t> &temporaries) {
  IRMapping *mapping = &mappingStorage;
  auto function = slice.activation->getActivationFunction();
  const auto &memory = slice.activation->getMemory();
  OwningOpRef<sim::SimFuncOp> copy(
      cast<sim::SimFuncOp>(function->clone(*mapping)));
  DenseMap<Value, Value> privateReferences;
  DenseMap<uint64_t, Value> slots;
  OpBuilder entry(&copy->getBody().front(), copy->getBody().front().begin());
  // Iterate source order, never DenseMap order: stable slot order matters to
  // deterministic planning and generated IR.
  auto allocate = [&](Value base) {
    auto found = temporaries.find(base);
    if (found == temporaries.end())
      return;
    auto ref = cast<sim::RefType>(base.getType());
    Value &slot = slots[found->second];
    if (!slot) {
      Type type = ref.getElementType();
      Value initial;
      if (sim::isAggregateType(type))
        initial =
            sim::SimAggregateDefaultOp::create(entry, base.getLoc(), type);
      else if (isa<IntegerType>(type))
        initial = arith::ConstantOp::create(entry, base.getLoc(), type,
                                            entry.getIntegerAttr(type, 0));
      else {
        auto logic = cast<sim::LogicType>(type);
        auto plane = entry.getIntegerType(logic.getWidth());
        initial = sim::SimLogicConstantOp::create(
            entry, base.getLoc(), type,
            entry.getIntegerAttr(plane, APInt(logic.getWidth(), 0)),
            entry.getIntegerAttr(plane, APInt::getAllOnes(logic.getWidth())));
      }
      slot = sim::SimRefAllocOp::create(entry, base.getLoc(), ref, initial);
    }
    privateReferences[base] = slot;
  };
  for (BlockArgument argument : function.getArguments())
    allocate(argument);
  function.walk(
      [&](sim::SimContextStorageOp storage) { allocate(storage.getResult()); });
  auto privateReference = [&](Value reference,
                              auto &privateReference) -> Value {
    if (Value found = privateReferences.lookup(reference))
      return found;
    Operation *definition = reference.getDefiningOp();
    assert(definition && "temporary reference was validated");
    IRMapping operands;
    for (Value operand : definition->getOperands())
      operands.map(operand, isa<sim::RefType>(operand.getType())
                                ? privateReference(operand, privateReference)
                                : mapping->lookup(operand));
    OpBuilder builder(mapping->lookup(definition));
    Operation *cloned = builder.clone(*definition, operands);
    for (auto [from, to] :
         llvm::zip_equal(definition->getResults(), cloned->getResults()))
      privateReferences[from] = to;
    return privateReferences.lookup(reference);
  };
  DenseSet<Operation *> kept;
  for (Operation *store : slice.stores)
    kept.insert(mapping->lookup(store));
  DenseMap<Operation *, Ranges> reads, writes;
  DenseSet<Operation *> privateReads;
  for (Operation *read : memory.getPrivateReads())
    privateReads.insert(mapping->lookup(read));
  for (const auto &[op, ranges] : memory.getReads()) {
    auto load = dyn_cast<sim::SimRefLoadOp>(op);
    if (load && !ranges.empty() && slots.contains(ranges.front().root)) {
      cast<sim::SimRefLoadOp>(mapping->lookup(op))
          .getReferenceMutable()
          .assign(privateReference(load.getReference(), privateReference));
    } else
      reads[mapping->lookup(op)] = ranges;
  }
  function.walk([&](Operation *op) {
    auto found = memory.getWrites().find(op);
    if (found == memory.getWrites().end())
      return;
    Operation *cloned = mapping->lookup(op);
    if (auto store = dyn_cast<sim::SimRefStoreOp>(op);
        store && !found->second.empty() &&
        slots.contains(found->second.front().root)) {
      OpBuilder builder(cloned);
      sim::SimRefStoreOp::create(
          builder, store.getLoc(), mapping->lookup(store.getValue()),
          privateReference(store.getReference(), privateReference));
    }
    if (kept.contains(cloned))
      writes[cloned] = found->second;
    else
      cloned->erase();
  });
  auto removeDeadStores = [&] {
    ActivationLoadSlice dependencies(*copy, reads, writes, &privateReads);
    DenseSet<Operation *> required;
    SmallVector<Operation *> loads;
    copy->walk([&](Operation *op) {
      if (isa<sim::SimRefStoreOp, sim::SimDriverDriveOp>(op) &&
          writes.contains(op))
        dependencies.collect(op, loads, &required);
    });
    SmallVector<Operation *> deadStores;
    copy->walk([&](sim::SimRefStoreOp store) {
      if (!writes.contains(store) && !required.contains(store))
        deadStores.push_back(store);
    });
    for (Operation *store : deadStores)
      store->erase();
    return !deadStores.empty();
  };
  removeDeadStores();
  if (failed(canonicalizeControl(*copy)))
    return {};
  while (true) {
    // Removing a branch can make a private load dead; its reaching stores
    // must then be sliced again before their own inputs can disappear.
    bool removedStores = removeDeadStores();
    // Admission proved every call finite and read-only. Its generic call
    // interface remains conservative, so ordinary DCE cannot remove it when
    // slicing discards all results. Retaining it would retain its input reads
    // and could keep an otherwise empty loop alive.
    SmallVector<sim::SimCallOp> deadCalls;
    copy->walk([&](sim::SimCallOp call) {
      if (call->use_empty())
        deadCalls.push_back(call);
    });
    for (auto call : deadCalls)
      call.erase();
    bool removedLoop = removeEmptyFiniteLoop(*copy);
    if (deadCalls.empty() && !removedLoop && !removedStores)
      break;
    if (failed(canonicalizeControl(*copy)))
      return {};
  }
  return copy;
}
} // namespace

OwningOpRef<sim::SimFuncOp> cloneActivationSlice(const ActivationSlice &slice,
                                                 IRMapping *mapping) {
  if (slice.activation->getMemory().hasMixedPrivateWrites())
    return {};
  IRMapping localMapping;
  auto temporaries = getActivationTemporaryRoots(
      slice.activation->getActivationFunction(), slice.activation->getMemory());
  return cloneSliceWithTemporaries(slice, mapping ? *mapping : localMapping,
                                   temporaries);
}
} // namespace obelisk::analysis
