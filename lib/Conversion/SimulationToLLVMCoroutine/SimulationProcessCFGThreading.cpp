//===- SimulationProcessCFGThreading.cpp - Thread process CFG state -------===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "talos/Analysis/SSAValueAnalysis.h"

#include "talos/Dialect/Schedule/ScheduleOps.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/GenericDomTree.h"
#include "llvm/Support/GenericDomTreeConstruction.h"

#include <deque>

using namespace mlir;

namespace {
struct ActivationGraph;
/// A block of the activation graph: the CFG with suspension edges cut and a
/// virtual root entering at the process entry and at every resume target.
/// Every activation starts at one of those entries and runs to its next
/// suspension, so dominance on this graph certifies that a definition ran
/// earlier in the same activation.
struct ActivationNode {
  Block *block = nullptr;
  ActivationGraph *graph = nullptr;
  SmallVector<ActivationNode *, 2> successors, predecessors;
  ActivationGraph *getParent() const { return graph; }
  void printAsOperand(raw_ostream &os, bool) const {
    os << (block ? "block" : "activation root");
  }
};
struct ActivationGraph {
  /// The virtual root comes first; deque storage keeps node addresses stable.
  std::deque<ActivationNode> nodes;
  ActivationNode &front() { return nodes.front(); }
};
} // namespace

namespace llvm {
template <> struct GraphTraits<ActivationNode *> {
  using NodeRef = ActivationNode *;
  using ChildIteratorType = SmallVectorImpl<ActivationNode *>::iterator;
  static NodeRef getEntryNode(NodeRef node) { return node; }
  static ChildIteratorType child_begin(NodeRef node) {
    return node->successors.begin();
  }
  static ChildIteratorType child_end(NodeRef node) {
    return node->successors.end();
  }
};
template <> struct GraphTraits<Inverse<ActivationNode *>> {
  using NodeRef = ActivationNode *;
  using ChildIteratorType = SmallVectorImpl<ActivationNode *>::iterator;
  static NodeRef getEntryNode(Inverse<NodeRef> node) { return node.Graph; }
  static ChildIteratorType child_begin(NodeRef node) {
    return node->predecessors.begin();
  }
  static ChildIteratorType child_end(NodeRef node) {
    return node->predecessors.end();
  }
};
template <>
struct GraphTraits<ActivationGraph *> : GraphTraits<ActivationNode *> {
  using nodes_iterator = pointer_iterator<std::deque<ActivationNode>::iterator>;
  static NodeRef getEntryNode(ActivationGraph *graph) {
    return &graph->front();
  }
  static nodes_iterator nodes_begin(ActivationGraph *graph) {
    return nodes_iterator(graph->nodes.begin());
  }
  static nodes_iterator nodes_end(ActivationGraph *graph) {
    return nodes_iterator(graph->nodes.end());
  }
};
} // namespace llvm

namespace talos {

#define GEN_PASS_DEF_TALOSSIMTHREADPROCESSCFGPASS
#include "talos/Conversion/Passes.h.inc"

namespace detail {

LogicalResult threadProcessStateThroughCFG(sim::SimFuncOp function) {
  if (function.getBody().empty())
    return success();
  // Zero-time callable and observer entries never leave their interpreter or
  // native call frame. In particular, process.control transfers synchronously
  // to its successor and deliberately has no canonical-frame operands.
  if (function.getEntryKind() == sim::EntryKind::Function ||
      function.getEntryKind() == sim::EntryKind::Observer)
    return success();
  Block *entry = &function.getBody().front();

  // IEEE 1800-2017 31.7 condition observers are static descriptors consumed
  // by their clock suspension. Native/AOT fragment extraction can hoist a
  // descriptor back to an entry block after front-end suspension threading;
  // rematerialize it at each clock_set so CFG threading never gives it a
  // canonical-frame lane.
  SmallVector<sim::SimSuspendClockSetOp> clockSets;
  function.walk(
      [&](sim::SimSuspendClockSetOp clocks) { clockSets.push_back(clocks); });
  for (sim::SimSuspendClockSetOp clocks : clockSets) {
    IRRewriter rewriter(function.getContext());
    rewriter.setInsertionPoint(clocks);
    DenseMap<Operation *, sim::SimObserverBindOp> clones;
    for (OpOperand &operand : clocks->getOpOperands()) {
      auto binding = operand.get().getDefiningOp<sim::SimObserverBindOp>();
      if (!binding || binding->getBlock() == clocks->getBlock())
        continue;
      auto [entry, inserted] = clones.try_emplace(binding.getOperation());
      if (inserted)
        entry->second = cast<sim::SimObserverBindOp>(rewriter.clone(*binding));
      operand.set(entry->second.getResult());
      if (binding.getResult().use_empty())
        rewriter.eraseOp(binding);
    }
  }

  // Front-end suspension threading is deliberately conservative and may
  // forward literal constants into continuation arguments. Recreate those
  // constants in the continuation instead: immutable byte spans contain a
  // generated native address and therefore must never be persisted in the
  // pointer-free canonical frame.
  for (Block &block : llvm::drop_begin(function.getBody())) {
    // The predecessor set does not change when argument lanes are erased.
    // Avoid scanning the whole function for every lane, and visit a block
    // with parallel incoming edges only once (each edge is inspected below).
    SmallVector<Block *> predecessors;
    llvm::SmallPtrSet<Block *, 4> seenPredecessors;
    for (Block *predecessor : block.getPredecessors())
      if (seenPredecessors.insert(predecessor).second)
        predecessors.push_back(predecessor);
    for (int64_t argumentIndex =
             static_cast<int64_t>(block.getNumArguments()) - 1;
         argumentIndex >= 0; --argumentIndex) {
      Value incomingValue;
      SmallVector<std::pair<Operation *, unsigned>> incomingEdges;
      bool canRematerialize = true;
      for (Block *predecessor : predecessors) {
        Operation *terminator = predecessor->getTerminator();
        auto branch = dyn_cast<BranchOpInterface>(terminator);
        if (!branch) {
          if (llvm::is_contained(predecessor->getSuccessors(), &block)) {
            canRematerialize = false;
            break;
          }
          continue;
        }
        for (auto [successorIndex, successor] :
             llvm::enumerate(predecessor->getSuccessors())) {
          if (successor != &block)
            continue;
          SuccessorOperands operands =
              branch.getSuccessorOperands(successorIndex);
          if (static_cast<unsigned>(argumentIndex) >= operands.size() ||
              operands.isOperandProduced(argumentIndex)) {
            canRematerialize = false;
            break;
          }
          Value value = operands[argumentIndex];
          if (!incomingValue)
            incomingValue = value;
          else if (incomingValue != value) {
            canRematerialize = false;
            break;
          }
          incomingEdges.emplace_back(terminator, successorIndex);
        }
        if (!canRematerialize)
          break;
      }
      auto result = dyn_cast_or_null<OpResult>(incomingValue);
      Operation *constant = result ? result.getOwner() : nullptr;
      if (!canRematerialize || incomingEdges.empty() || !constant ||
          constant->getNumOperands() != 0 ||
          !constant->hasTrait<OpTrait::ConstantLike>())
        continue;

      OpBuilder builder(&block, block.begin());
      Operation *clone = builder.clone(*constant);
      block.getArgument(argumentIndex)
          .replaceAllUsesWith(clone->getResult(result.getResultNumber()));
      for (auto [terminator, successorIndex] : incomingEdges) {
        auto branch = cast<BranchOpInterface>(terminator);
        branch.getSuccessorOperands(successorIndex)
            .erase(static_cast<unsigned>(argumentIndex));
      }
      block.eraseArgument(static_cast<unsigned>(argumentIndex));
    }
  }

  DenseMap<Block *, DenseMap<Value, BlockArgument>> threadedValues;
  DenseMap<Value, Value> threadedRoots;
  auto rootOf = [&](Value value) {
    while (Value root = threadedRoots.lookup(value))
      value = root;
    return value;
  };
  // Suspension lowering may already have made some values explicit successor
  // operands. Record those lanes before finding external uses so a value does
  // not acquire a second continuation argument when it is also live through a
  // later ordinary CFG edge.
  analysis::SemanticValueRootAnalysis semanticRoots(function);
  for (Block &block : llvm::drop_begin(function.getBody()))
    for (BlockArgument argument : block.getArguments()) {
      Value root = semanticRoots.lookup(argument);
      if (root != argument) {
        threadedRoots.try_emplace(argument, root);
        threadedValues[&block].try_emplace(root, argument);
      }
    }

  // A continuation operand is restored from the canonical process frame and
  // therefore starts a second dynamic path for its semantic root. Ordinary
  // dominance does not model that path: after coroutine splitting, it may
  // reach a loop exit without executing the original SSA definition again.
  // Remember the direct continuation blocks now, before later threading adds
  // ordinary phi-like arguments that must not themselves be treated as frame
  // restores.
  DenseMap<Value, llvm::SmallPtrSet<Block *, 4>> restoredRoots;
  for (Block &predecessor : function.getBody()) {
    Operation *terminator = predecessor.getTerminator();
    if (!isa<sim::SimTaskCallOp, sim::SimClassVirtualTaskCallOp,
             sim::SimProcessControlOp, sim::SimSuspendDelayOp,
             sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
             sim::SimSuspendEdgeIffOp, sim::SimSuspendLevelOp,
             sim::SimSuspendAnyOp, sim::SimSuspendClockSetOp,
             sim::SimSuspendEventOp, sim::SimSuspendEventOrderOp,
             sim::SimSuspendMailboxOp, sim::SimSuspendSemaphoreOp,
             sim::SimSuspendObserveOp, sim::SimSuspendAwaitOp,
             sim::SimSuspendJoinOp, sim::SimSuspendChildrenOp>(terminator))
      continue;
    auto branch = cast<BranchOpInterface>(terminator);
    for (auto [successorIndex, successor] :
         llvm::enumerate(predecessor.getSuccessors())) {
      SuccessorOperands operands = branch.getSuccessorOperands(successorIndex);
      for (auto [argumentIndex, argument] :
           llvm::enumerate(successor->getArguments())) {
        if (argumentIndex >= operands.size() ||
            operands.isOperandProduced(argumentIndex))
          continue;
        Value root = rootOf(argument);
        if (root && rootOf(operands[argumentIndex]) == root)
          restoredRoots[root].insert(successor);
      }
    }
  }

  DominanceInfo dominance(function);

  // Every activation starts at the entry or at a resumed suspension
  // successor and runs to its next suspension. A value needs no frame lane
  // in a block its definition dominates on those activation paths: the CFG
  // with suspension edges cut and every resume target as another entry. The
  // fused kernels otherwise thread hundreds of activation-local slots through
  // every block. Control boundaries resume through hidden edges and keep the
  // per-block lanes below.
  bool hiddenResumes = false;
  function.walk([&](Operation *operation) {
    hiddenResumes |=
        isa<sim::SimControlBoundaryOp, schedule::NativeControlBoundaryOp>(
            operation);
  });
  ActivationGraph activationGraph;
  DenseMap<Block *, ActivationNode *> activationNodes;
  llvm::DominatorTreeBase<ActivationNode, false> activationDominance;
  if (!hiddenResumes) {
    activationGraph.nodes.emplace_back();
    activationGraph.front().graph = &activationGraph;
    for (Block &block : function.getBody()) {
      ActivationNode &node = activationGraph.nodes.emplace_back();
      node.block = &block;
      node.graph = &activationGraph;
      activationNodes[&block] = &node;
    }
    auto addEdge = [](ActivationNode *from, ActivationNode *to) {
      from->successors.push_back(to);
      to->predecessors.push_back(from);
    };
    addEdge(&activationGraph.front(), activationNodes.lookup(entry));
    for (Block &block : function.getBody()) {
      Operation *terminator = block.getTerminator();
      ActivationNode *from = sim::isSuspensionOp(terminator)
                                 ? &activationGraph.front()
                                 : activationNodes.lookup(&block);
      for (Block *successor : terminator->getSuccessors())
        addEdge(from, activationNodes.lookup(successor));
    }
    activationDominance.recalculate(activationGraph);
  }
  auto activationDominatesBlock = [&](Block *definition, Block *block) {
    if (hiddenResumes || !definition || definition == block ||
        definition->getParent() != &function.getBody())
      return false;
    ActivationNode *from = activationNodes.lookup(definition);
    ActivationNode *to = activationNodes.lookup(block);
    return from && to && activationDominance.isReachableFromEntry(to) &&
           activationDominance.dominates(from, to);
  };
  auto activationDominates = [&](Value root, Block *block) {
    return activationDominatesBlock(root.getParentBlock(), block);
  };

  // A control boundary has a scheduler resume edge that is deliberately not
  // represented in the ordinary CFG: after a remote disable the coroutine is
  // entered directly at the boundary's resume successor.  Find values used by
  // the synchronous body that can be reached again from that hidden edge and
  // make them explicit resume operands.  Without this step ordinary dominance
  // incorrectly says that a loop-carried value is available after resumption.
  struct ControlRequirement {
    sim::SimControlBoundaryOp boundary;
    Value root;
    bool seededResume;
  };
  SmallVector<ControlRequirement> controlRequirements;
  DenseSet<Value> controlRoots;
  auto reachesWithoutRedefinition = [&](Block *start, Block *target,
                                        Block *definition) {
    SmallVector<Block *> worklist{start};
    llvm::SmallPtrSet<Block *, 16> visited;
    while (!worklist.empty()) {
      Block *block = worklist.pop_back_val();
      if (!visited.insert(block).second)
        continue;
      if (block == target)
        return true;
      if (block == definition)
        continue;
      llvm::append_range(worklist, block->getSuccessors());
    }
    return false;
  };
  function.walk([&](sim::SimControlBoundaryOp boundary) {
    Block *body = boundary.getBody();
    llvm::SetVector<Value> roots;
    for (Block &block : function.getBody()) {
      if (!dominance.dominates(body, &block))
        continue;
      for (Operation &operation : block)
        for (Value value : operation.getOperands()) {
          if (value.getParentBlock() == &block)
            continue;
          Value root = rootOf(value);
          Block *definition = root.getParentBlock();
          if (!definition || definition == entry ||
              !dominance.dominates(root, boundary.getOperation()) ||
              !reachesWithoutRedefinition(boundary.getResume(), &block,
                                          definition))
            continue;
          roots.insert(root);
        }
    }
    for (Value root : roots) {
      auto &resumeValues = threadedValues[boundary.getResume()];
      bool seededResume = false;
      if (!resumeValues.count(root)) {
        boundary.getResumeOperandsMutable().append(root);
        BlockArgument argument =
            boundary.getResume()->addArgument(root.getType(), root.getLoc());
        resumeValues.insert({root, argument});
        threadedRoots.try_emplace(argument, root);
        seededResume = true;
      }
      controlRequirements.push_back({boundary, root, seededResume});
      controlRoots.insert(root);
    }
  });

  // Return the value of a semantic root available at a block under the
  // extended CFG above.  Phi-like block arguments are created lazily only at
  // reconvergence points between ordinary execution and a restored resume
  // lane, keeping both the canonical frame and native CFG compact.
  DenseMap<Value, llvm::SmallPtrSet<Block *, 16>> resumeReachable;
  auto markRestoredReachability = [&](Value root, Block *start) {
    Block *definition = root.getParentBlock();
    SmallVector<Block *> worklist{start};
    auto &reachable = resumeReachable[root];
    while (!worklist.empty()) {
      Block *block = worklist.pop_back_val();
      if (block == definition || !reachable.insert(block).second)
        continue;
      llvm::append_range(worklist, block->getSuccessors());
    }
  };
  for (ControlRequirement &requirement : controlRequirements) {
    markRestoredReachability(requirement.root,
                             requirement.boundary.getResume());
  }
  for (auto &[root, starts] : restoredRoots)
    for (Block *start : starts)
      markRestoredReachability(root, start);

  // A design view defined in the entry block never changes value. When one
  // continuation restores it, its restored lane holds the view in every block
  // that continuation dominates on activation paths. Full CFG dominance is
  // not enough: a later resume target can enter such a block in another
  // activation, where the lane was never restored. Using the lane keeps the
  // same frame lanes without threading a copy through every block between
  // the continuation and each use, which fused kernels would otherwise pay
  // for hundreds of captured views per block.
  // Views are pure expressions over process arguments with no managed
  // lifetime; owned references keep per-block lanes for release planning.
  DenseMap<Value, bool> pureOverArguments;
  std::function<bool(Value)> isPureOverArguments = [&](Value value) {
    if (auto argument = dyn_cast<BlockArgument>(value))
      return argument.getOwner() == entry;
    auto [found, inserted] = pureOverArguments.try_emplace(value, false);
    if (!inserted)
      return found->second;
    Operation *definition = value.getDefiningOp();
    bool pure = definition->getNumRegions() == 0 &&
                isMemoryEffectFree(definition) && isSpeculatable(definition) &&
                llvm::all_of(definition->getOperands(), isPureOverArguments);
    pureOverArguments[value] = pure;
    return pure;
  };
  auto dominatingRestoredLane = [&](Value root, Block *block) -> Value {
    if (root.getParentBlock() != entry || controlRoots.contains(root) ||
        !isa<sim::RefType, sim::NetType, sim::DriverType>(root.getType()) ||
        !isPureOverArguments(root))
      return {};
    auto restored = restoredRoots.find(root);
    if (restored == restoredRoots.end() || restored->second.size() != 1)
      return {};
    Block *resume = *restored->second.begin();
    if (!activationDominatesBlock(resume, block))
      return {};
    auto lanes = threadedValues.find(resume);
    if (lanes == threadedValues.end())
      return {};
    auto lane = lanes->second.find(root);
    return lane == lanes->second.end() ? Value() : Value(lane->second);
  };

  std::function<FailureOr<Value>(Value, Block *)> makeAvailable;
  struct CreatedThreadArgument {
    Block *block;
    Value root;
    BlockArgument argument;
  };
  struct AppendedThreadOperand {
    Operation *terminator;
    unsigned successorIndex;
  };
  SmallVector<CreatedThreadArgument> createdThreadArguments;
  SmallVector<AppendedThreadOperand> appendedThreadOperands;
  makeAvailable = [&](Value root, Block *block) -> FailureOr<Value> {
    if (block == entry)
      return dominance.dominates(root, block->getTerminator())
                 ? FailureOr<Value>(root)
                 : FailureOr<Value>(failure());
    auto existing = threadedValues[block].find(root);
    if (existing != threadedValues[block].end())
      return existing->second;
    if (Value lane = dominatingRestoredLane(root, block))
      return lane;

    // Recursive reconstruction asks for the value at predecessor
    // terminators. When it reaches the block containing the definition, the
    // root itself is available on every outgoing edge even though it does not
    // dominate the block's first operation. Stopping here also prevents a
    // loop from incorrectly demanding an undefined initial value on paths
    // entering before the definition executes.
    if (root.getParentBlock() == block)
      return root;

    bool bypassed = resumeReachable[root].contains(block);
    if (!bypassed && dominance.dominates(root, &block->front()))
      return root;

    // A boundary body cannot take successor operands.  Resolve the value at
    // the boundary itself; that value dominates the synchronous body edge.
    auto predecessors = block->getPredecessors();
    if (predecessors.begin() != predecessors.end() &&
        std::next(predecessors.begin()) == predecessors.end()) {
      Block *predecessor = *predecessors.begin();
      if (auto boundary =
              dyn_cast<sim::SimControlBoundaryOp>(predecessor->getTerminator());
          boundary && boundary.getBody() == block)
        return makeAvailable(root, predecessor);
    }

    BlockArgument argument = block->addArgument(root.getType(), root.getLoc());
    threadedValues[block].insert({root, argument});
    threadedRoots.try_emplace(argument, root);
    createdThreadArguments.push_back({block, root, argument});

    llvm::SmallPtrSet<Block *, 4> seenPredecessors;
    for (Block *predecessor : block->getPredecessors()) {
      if (!seenPredecessors.insert(predecessor).second)
        continue;
      auto branch = dyn_cast<BranchOpInterface>(predecessor->getTerminator());
      if (!branch)
        return failure();
      FailureOr<Value> incoming = makeAvailable(root, predecessor);
      if (failed(incoming))
        return failure();
      for (auto [index, successor] :
           llvm::enumerate(predecessor->getSuccessors()))
        if (successor == block) {
          branch.getSuccessorOperands(index).append(*incoming);
          appendedThreadOperands.push_back(
              {predecessor->getTerminator(), static_cast<unsigned>(index)});
        }
    }
    return argument;
  };

  // Recursive reconstruction creates phi-like lanes eagerly to break CFG
  // cycles. If a later predecessor proves unavailable, roll the entire
  // speculative subgraph back; otherwise abandoned block arguments accumulate
  // without corresponding successor operands and invalidate unrelated loops.
  auto tryMakeAvailable = [&](Value root, Block *block) -> FailureOr<Value> {
    size_t argumentCheckpoint = createdThreadArguments.size();
    size_t operandCheckpoint = appendedThreadOperands.size();
    FailureOr<Value> result = makeAvailable(root, block);
    if (succeeded(result))
      return result;
    while (appendedThreadOperands.size() != operandCheckpoint) {
      AppendedThreadOperand appended = appendedThreadOperands.pop_back_val();
      auto branch = cast<BranchOpInterface>(appended.terminator);
      SuccessorOperands operands =
          branch.getSuccessorOperands(appended.successorIndex);
      operands.erase(operands.size() - 1);
    }
    while (createdThreadArguments.size() != argumentCheckpoint) {
      CreatedThreadArgument created = createdThreadArguments.pop_back_val();
      threadedValues[created.block].erase(created.root);
      threadedRoots.erase(created.argument);
      created.block->eraseArgument(created.argument.getArgNumber());
    }
    return failure();
  };

  // The resume successor can also have ordinary local-control predecessors.
  // They reach the same block without going through control.boundary, so add
  // the newly seeded lane to those explicit branch edges as well.
  for (ControlRequirement &requirement : controlRequirements) {
    if (!requirement.seededResume)
      continue;
    Block *resume = requirement.boundary.getResume();
    llvm::SmallPtrSet<Block *, 4> seenPredecessors;
    for (Block *predecessor : resume->getPredecessors()) {
      if (!seenPredecessors.insert(predecessor).second ||
          predecessor == requirement.boundary->getBlock())
        continue;
      auto branch = dyn_cast<BranchOpInterface>(predecessor->getTerminator());
      if (!branch)
        return predecessor->getTerminator()->emitError(
            "cannot thread control-resume state through a non-branch "
            "terminator");
      FailureOr<Value> incoming =
          tryMakeAvailable(requirement.root, predecessor);
      if (failed(incoming))
        return predecessor->getTerminator()->emitError(
            "cannot reconstruct control-resume state on this predecessor");
      for (auto [index, successor] :
           llvm::enumerate(predecessor->getSuccessors()))
        if (successor == resume)
          branch.getSuccessorOperands(index).append(*incoming);
    }
  }

  for (ControlRequirement &requirement : controlRequirements) {
    Block *boundaryBlock = requirement.boundary->getBlock();
    if (!resumeReachable[requirement.root].contains(boundaryBlock))
      continue;
    FailureOr<Value> available =
        tryMakeAvailable(requirement.root, boundaryBlock);
    if (failed(available))
      return requirement.boundary.emitOpError(
          "cannot reconstruct control-resume state on every predecessor");
    Block *body = requirement.boundary.getBody();
    for (Block &block : function.getBody()) {
      if (!dominance.dominates(body, &block))
        continue;
      for (Operation &operation : block)
        for (OpOperand &operand : operation.getOpOperands())
          if (operand.get().getParentBlock() != &block &&
              rootOf(operand.get()) == requirement.root)
            operand.set(*available);
    }
  }

  // Recursive reconstruction can discover equivalent roots in a different
  // order on adjacent blocks.  Reassociate every threaded successor lane by
  // semantic root so operand order always follows the target block arguments.
  for (Block &predecessor : function.getBody()) {
    auto branch = dyn_cast<BranchOpInterface>(predecessor.getTerminator());
    if (!branch)
      continue;
    for (auto [successorIndex, successor] :
         llvm::enumerate(predecessor.getSuccessors())) {
      SuccessorOperands operands = branch.getSuccessorOperands(successorIndex);
      for (auto [argumentIndex, argument] :
           llvm::enumerate(successor->getArguments())) {
        Value root = threadedRoots.lookup(argument);
        if (!root || !controlRoots.contains(root) ||
            argumentIndex >= operands.size() ||
            operands.isOperandProduced(argumentIndex))
          continue;
        FailureOr<Value> incoming = tryMakeAvailable(root, &predecessor);
        if (failed(incoming))
          return predecessor.getTerminator()->emitError(
              "cannot normalize threaded control-resume state");
        operands.slice(argumentIndex, 1).assign(*incoming);
      }
    }
  }

  SmallVector<Block *> synchronousControlBodies;
  function.walk([&](sim::SimControlBoundaryOp boundary) {
    synchronousControlBodies.push_back(boundary.getBody());
  });
  bool changed;
  do {
    changed = false;
    Operation *unresolvedUse = nullptr;
    Value unresolvedRoot;
    for (Block &block : function.getBody()) {
      if (&block == entry)
        continue;
      bool controlBoundaryBody =
          llvm::any_of(synchronousControlBodies, [&](Block *body) {
            return dominance.dominates(body, &block);
          });
      bool controlBoundaryEntryBody =
          llvm::is_contained(synchronousControlBodies, &block);
      llvm::SetVector<Value> externalRoots;
      DenseMap<Value, Value> rematerializedConstants;
      DenseSet<Value> nonRematerializableConstants, nonRematerializableViews;
      bool rematerializingView = false;
      OpBuilder rematerializationBuilder(&block, block.begin());
      std::function<Value(Value)> rematerializeConstantExpression =
          [&](Value value) -> Value {
        if (Value replacement = rematerializedConstants.lookup(value))
          return replacement;
        DenseSet<Value> &unavailable = rematerializingView
                                           ? nonRematerializableViews
                                           : nonRematerializableConstants;
        if (unavailable.contains(value))
          return {};
        // Process arguments are never redefined and are available after
        // every resume. A design view that is a pure expression over them,
        // such as a static element of a captured root, is recomputed where
        // its semantic root is used instead of becoming a frame lane in every
        // using block. Fused kernels use hundreds of such views per block.
        if (auto argument = dyn_cast<BlockArgument>(value);
            argument && rematerializingView) {
          if (argument.getOwner() == entry)
            return value;
          Value root = rootOf(value);
          Value replacement =
              root != value ? rematerializeConstantExpression(root) : Value();
          if (replacement)
            rematerializedConstants.try_emplace(value, replacement);
          else
            unavailable.insert(value);
          return replacement;
        }
        auto result = dyn_cast<OpResult>(value);
        Operation *definition = result ? result.getOwner() : nullptr;
        if (!definition || definition->getNumRegions() != 0 ||
            !isMemoryEffectFree(definition) || !isSpeculatable(definition)) {
          unavailable.insert(value);
          return {};
        }
        IRMapping mapping;
        for (Value operand : definition->getOperands()) {
          Value replacement = rematerializeConstantExpression(operand);
          if (!replacement) {
            unavailable.insert(value);
            return {};
          }
          mapping.map(operand, replacement);
        }
        Operation *clone = rematerializationBuilder.clone(*definition, mapping);
        for (auto [original, replacement] :
             llvm::zip_equal(definition->getResults(), clone->getResults()))
          rematerializedConstants.try_emplace(original, replacement);
        return rematerializedConstants.lookup(value);
      };
      for (Operation &operation : block)
        for (OpOperand &operand : operation.getOpOperands()) {
          Value value = operand.get();
          if (value.getParentBlock() == &block)
            continue;
          if (auto argument = dyn_cast<BlockArgument>(value);
              argument && argument.getOwner() == entry)
            continue;
          // Suspension operands keep their lanes: watch and continuation
          // inputs are part of the wait fragment's frame contract.
          rematerializingView =
              isa<sim::RefType, sim::NetType, sim::DriverType>(
                  value.getType()) &&
              !sim::isSuspensionOp(&operation);
          if (Value replacement = rematerializeConstantExpression(value)) {
            operand.set(replacement);
            continue;
          }
          Value root = rootOf(value);
          if (activationDominates(root, &block)) {
            operand.set(root);
            continue;
          }
          externalRoots.insert(root);
        }
      for (Value root : externalRoots) {
        auto replaceExternalUses = [&](Value replacement) {
          for (Operation &operation : block)
            for (OpOperand &operand : operation.getOpOperands()) {
              Value value = operand.get();
              if (value.getParentBlock() != &block && rootOf(value) == root)
                operand.set(replacement);
            }
        };
        // A control boundary's body edge and every block it dominates execute
        // synchronously. Prefer the closest restored lane when the hidden
        // resume edge can reach this body; otherwise ordinary SSA dominance
        // is sufficient and no frame slot is needed.
        if (controlBoundaryBody) {
          // Only synchronous control bodies use a dominating restored lane.
          // Scanning all threaded blocks for ordinary CFG uses is quadratic
          // in large replicated activations, and its result is unused there.
          bool restoredPath = resumeReachable[root].contains(&block);
          Value dominating =
              !restoredPath && dominance.dominates(root, &block.front())
                  ? root
                  : Value{};
          Block *dominatingBlock = dominating ? root.getParentBlock() : nullptr;
          for (auto &[candidateBlock, candidates] : threadedValues) {
            auto found = candidates.find(root);
            if (found == candidates.end() ||
                !dominance.dominates(candidateBlock, &block))
              continue;
            if (!dominatingBlock ||
                dominance.dominates(dominatingBlock, candidateBlock)) {
              dominating = found->second;
              dominatingBlock = candidateBlock;
            }
          }
          if (dominating && (!restoredPath || controlBoundaryEntryBody)) {
            replaceExternalUses(dominating);
            continue;
          }
        }
        auto existing = threadedValues[&block].find(root);
        if (existing != threadedValues[&block].end()) {
          replaceExternalUses(existing->second);
          continue;
        }
        if (Value lane = dominatingRestoredLane(root, &block)) {
          replaceExternalUses(lane);
          continue;
        }

        // Block::getPredecessors() visits predecessor edges, so a cond_br with
        // both destinations equal yields the same block twice. Update every
        // successor edge during one visit to each predecessor; otherwise each
        // edge receives the threaded operand twice.
        struct IncomingEdge {
          BranchOpInterface branch;
          unsigned successorIndex;
          Value value;
        };
        SmallVector<IncomingEdge> incomingEdges;
        bool unavailable = false;
        llvm::SmallPtrSet<Block *, 4> seenPredecessors;
        for (Block *predecessor : block.getPredecessors()) {
          if (!seenPredecessors.insert(predecessor).second)
            continue;
          auto branch =
              dyn_cast<BranchOpInterface>(predecessor->getTerminator());
          if (!branch)
            return predecessor->getTerminator()->emitError(
                "cannot thread suspension-live state through a non-branch "
                "terminator");
          FailureOr<Value> incoming =
              !resumeReachable[root].contains(predecessor) &&
                      dominance.dominates(root, predecessor->getTerminator())
                  ? FailureOr<Value>(root)
                  : tryMakeAvailable(root, predecessor);
          if (failed(incoming)) {
            unavailable = true;
            break;
          }
          bool found = false;
          for (auto [index, successor] :
               llvm::enumerate(predecessor->getSuccessors())) {
            if (successor != &block)
              continue;
            incomingEdges.push_back(
                {branch, static_cast<unsigned>(index), *incoming});
            found = true;
          }
          if (!found)
            return predecessor->getTerminator()->emitError(
                "predecessor is missing its CFG successor");
        }
        if (unavailable || incomingEdges.empty()) {
          if (!block.hasNoPredecessors()) {
            unresolvedUse = &block.front();
            unresolvedRoot = root;
          }
          continue;
        }

        BlockArgument argument =
            block.addArgument(root.getType(), root.getLoc());
        // `tryMakeAvailable` above may insert entries for predecessor blocks
        // and rehash the outer map. Do not retain a reference to this inner
        // map across that recursion.
        threadedValues[&block].insert({root, argument});
        threadedRoots.try_emplace(argument, root);
        replaceExternalUses(argument);
        for (IncomingEdge &incoming : incomingEdges)
          incoming.branch.getSuccessorOperands(incoming.successorIndex)
              .append(incoming.value);
        changed = true;
      }
    }
    if (!changed && unresolvedUse) {
      InFlightDiagnostic diagnostic = unresolvedUse->emitError(
          "cannot reconstruct suspension-live state on every predecessor");
      if (unresolvedRoot)
        diagnostic << " for " << unresolvedRoot;
      return failure();
    }
  } while (changed);
  return success();
}

} // namespace detail

namespace {

class TalosSimThreadProcessCFGPass
    : public impl::TalosSimThreadProcessCFGPassBase<
          TalosSimThreadProcessCFGPass> {
public:
  void runOnOperation() override {
    if (failed(detail::threadProcessStateThroughCFG(getOperation())))
      signalPassFailure();
  }
};

} // namespace
} // namespace talos
