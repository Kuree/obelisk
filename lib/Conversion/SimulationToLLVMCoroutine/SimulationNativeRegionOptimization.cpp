//===- SimulationNativeRegionOptimization.cpp - Native region SSA -------===//

#include "obelisk/Conversion/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMOPTIMIZENATIVEREGIONSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

class ObeliskSimOptimizeNativeRegionsPass final
    : public impl::ObeliskSimOptimizeNativeRegionsPassBase<
          ObeliskSimOptimizeNativeRegionsPass> {
public:
  using Base = impl::ObeliskSimOptimizeNativeRegionsPassBase<
      ObeliskSimOptimizeNativeRegionsPass>;
  using Base::Base;
  ObeliskSimOptimizeNativeRegionsPass(
      const ObeliskSimOptimizeNativeRegionsPass &other)
      : Base(other) {}

  void runOnOperation() override;

private:
  Statistic optimizedRegions{this, "optimized-regions",
                             "generated native region bodies optimized"};
  Statistic coalescedRoots{
      this, "coalesced-nba-roots",
      "native region roots forwarded to one boundary NBA stage"};
  Statistic eliminatedStages{
      this, "eliminated-nba-stages",
      "static native root-accumulator stage sites eliminated"};
};

struct RegionRoot {
  uint32_t commit;
  Value destination;
  Type valueType;
  sim::NBASiteAttr representativeSite;
  Location representativeLocation;
  SmallVector<sim::SimNBAEnqueueOp> enqueues;
};

bool isRegionLocalAccumulator(sim::SimNBAEnqueueOp enqueue) {
  sim::NBASiteAttr site = enqueue.getSiteAttr();
  return site && !enqueue.getDelay() && !site.getTiming() &&
         site.getStorage() == sim::ComputeNBAStorageKind::RootAccumulator;
}

/// Replace repeated immediate assignments to one fixed NBA root in an
/// acyclic activation with SSA next state and one stage at the wait boundary.
/// The bytecode image has already been frozen when this helper runs, so the
/// original sites remain the fallback implementation.
bool forwardRegionNextState(sim::SimFuncOp function, uint64_t &rootCount,
                            uint64_t &stageCount) {
  Operation *suspension = nullptr;
  bool multipleSuspensions = false;
  function.walk([&](Operation *operation) {
    if (!sim::isSuspensionOp(operation))
      return;
    multipleSuspensions |= suspension != nullptr;
    suspension = operation;
  });
  if (multipleSuspensions || !suspension ||
      suspension->getNumSuccessors() != 1)
    return false;
  Block *wait = suspension->getBlock();
  Block *activationEntry = suspension->getSuccessor(0);
  if (activationEntry == wait ||
      activationEntry == &function.getBody().front())
    return false;

  // Collect the complete activation without crossing its suspension block.
  SmallVector<Block *> activation;
  SmallVector<Block *> pending{activationEntry};
  DenseSet<Block *> activationSet;
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    if (block == wait || !activationSet.insert(block).second)
      continue;
    if (block->getParent() != &function.getBody() ||
        llvm::any_of(*block, [](Operation &operation) {
          return sim::isSuspensionOp(&operation);
        }))
      return false;
    activation.push_back(block);
    for (Block *successor : block->getSuccessors())
      if (successor != wait)
        pending.push_back(successor);
  }
  // Until the epilogue also owns direct commit and fanout, large state-machine
  // regions pay more phi traffic than they save in accumulator staging. Keep
  // this first form within one fine-mask leaf; larger actors remain untouched
  // rather than taking a compile-time-size heuristic as a semantic proof.
  if (activation.empty() || activation.size() > 64)
    return false;

  // The initial implementation deliberately handles an acyclic region with
  // explicit exits. Convergence SCCs need a generated fixpoint loop and are
  // not silently approximated here. Mixed conditional exits are split below
  // so their next-state values reach an edge-local epilogue.
  DenseMap<Block *, unsigned> indegree;
  for (Block *block : activation)
    indegree[block] = 0;
  for (Block *block : activation) {
    if (!isa<cf::BranchOp, cf::CondBranchOp>(block->getTerminator()))
      return false;
    if (block->getNumSuccessors() == 0)
      return false;
    for (Block *successor : block->getSuccessors()) {
      if (successor == wait) {
        if (auto branch = dyn_cast<cf::BranchOp>(block->getTerminator())) {
          if (branch.getDest() != wait)
            return false;
        } else {
          auto conditional = cast<cf::CondBranchOp>(block->getTerminator());
          if (conditional.getTrueDest() == wait &&
              conditional.getFalseDest() == wait)
            return false;
        }
        continue;
      }
      if (!activationSet.contains(successor))
        return false;
      ++indegree[successor];
    }
  }
  for (Block *block : activation)
    if (block != activationEntry)
      for (Block *predecessor : block->getPredecessors())
        if (!activationSet.contains(predecessor))
          return false;

  SmallVector<Block *> ready;
  for (Block *block : activation)
    if (indegree[block] == 0)
      ready.push_back(block);
  SmallVector<Block *> order;
  while (!ready.empty()) {
    Block *block = ready.pop_back_val();
    order.push_back(block);
    for (Block *successor : block->getSuccessors())
      if (successor != wait && --indegree[successor] == 0)
        ready.push_back(successor);
  }
  if (order.size() != activation.size() || order.front() != activationEntry)
    return false;

  // One commit root may have several part-select destinations. Their relative
  // order cannot be reconstructed by independent epilogues, so optimize a root
  // only when every local site names the same SSA destination and type.
  SmallVector<RegionRoot> roots;
  DenseMap<uint32_t, unsigned> rootsByCommit;
  for (Block *block : order)
    for (Operation &operation : block->without_terminator()) {
      auto enqueue = dyn_cast<sim::SimNBAEnqueueOp>(&operation);
      if (!enqueue || !isRegionLocalAccumulator(enqueue))
        continue;
      sim::NBASiteAttr site = enqueue.getSiteAttr();
      auto found = rootsByCommit.find(site.getCommit());
      if (found == rootsByCommit.end()) {
        unsigned index = roots.size();
        rootsByCommit.try_emplace(site.getCommit(), index);
        roots.push_back({site.getCommit(), enqueue.getDestination(),
                         enqueue.getValue().getType(), site, enqueue.getLoc(),
                         {enqueue}});
        continue;
      }
      RegionRoot &root = roots[found->second];
      if (root.destination != enqueue.getDestination() ||
          root.valueType != enqueue.getValue().getType()) {
        root.destination = {};
        continue;
      }
      root.enqueues.push_back(enqueue);
      root.representativeSite = site;
      root.representativeLocation = enqueue.getLoc();
    }
  llvm::erase_if(roots, [&](const RegionRoot &root) {
    if (!root.destination)
      return true;
    auto argument = dyn_cast<BlockArgument>(root.destination);
    return !argument || argument.getOwner() != &function.getBody().front() ||
           root.enqueues.size() < 2;
  });
  if (roots.empty())
    return false;

  DenseMap<Operation *, unsigned> rootByEnqueue;
  for (auto [rootIndex, root] : llvm::enumerate(roots))
    for (sim::SimNBAEnqueueOp enqueue : root.enqueues)
      rootByEnqueue[enqueue.getOperation()] = rootIndex;

  // Compute pruned SSA liveness independently for every root. An exit uses
  // every selected root; a block containing an enqueue defines (and therefore
  // kills the incoming value of) that root. Since the activation is acyclic,
  // one reverse topological sweep is sufficient. This avoids threading all
  // roots through a large state-machine CFG merely because they share a clock.
  DenseMap<Block *, SmallVector<bool>> defined;
  DenseMap<Block *, SmallVector<bool>> liveIn;
  DenseMap<Block *, SmallVector<bool>> liveOut;
  for (Block *block : order) {
    defined[block] = SmallVector<bool>(roots.size(), false);
    liveIn[block] = SmallVector<bool>(roots.size(), false);
    liveOut[block] = SmallVector<bool>(roots.size(), false);
    for (Operation &operation : block->without_terminator())
      if (auto found = rootByEnqueue.find(&operation);
          found != rootByEnqueue.end())
        defined[block][found->second] = true;
  }
  for (Block *block : llvm::reverse(order))
    for (unsigned root = 0; root != roots.size(); ++root) {
      bool out = llvm::is_contained(block->getSuccessors(), wait);
      for (Block *successor : block->getSuccessors())
        if (successor != wait)
          out |= liveIn[successor][root];
      liveOut[block][root] = out;
      liveIn[block][root] = out && !defined[block][root];
    }

  // Every internal merge receives (valid, next-value) for each selected root.
  // The activation entry starts with no pending assignment and the current
  // value as a typed poison-free placeholder. Standard CSE/LLVM forwarding can
  // merge that load with an existing region live-in.
  using RootState = std::pair<Value, Value>;
  using OptionalRootState = std::optional<RootState>;
  DenseMap<Block *, SmallVector<OptionalRootState>> incoming;
  OpBuilder builder(function.getContext());
  builder.setInsertionPointToStart(activationEntry);
  SmallVector<OptionalRootState> initial(roots.size());
  for (auto [rootIndex, root] : llvm::enumerate(roots)) {
    if (!liveIn[activationEntry][rootIndex])
      continue;
    Value valid = arith::ConstantOp::create(
        builder, root.representativeLocation, builder.getI1Type(),
        builder.getBoolAttr(false));
    Value value = sim::SimRefLoadOp::create(
        builder, root.representativeLocation, root.valueType,
        root.destination);
    initial[rootIndex] = RootState{valid, value};
  }
  incoming[activationEntry] = std::move(initial);
  for (Block *block : order) {
    if (block == activationEntry)
      continue;
    SmallVector<OptionalRootState> state(roots.size());
    for (auto [rootIndex, root] : llvm::enumerate(roots)) {
      if (!liveIn[block][rootIndex])
        continue;
      BlockArgument valid =
          block->addArgument(builder.getI1Type(), root.representativeLocation);
      BlockArgument value =
          block->addArgument(root.valueType, root.representativeLocation);
      state[rootIndex] = RootState{valid, value};
    }
    incoming[block] = std::move(state);
  }

  DenseMap<Block *, SmallVector<OptionalRootState>> outgoing;
  for (Block *block : order) {
    SmallVector<OptionalRootState> state = incoming.lookup(block);
    for (Operation &operation :
         llvm::make_early_inc_range(block->without_terminator())) {
      auto found = rootByEnqueue.find(&operation);
      if (found == rootByEnqueue.end())
        continue;
      auto enqueue = cast<sim::SimNBAEnqueueOp>(&operation);
      builder.setInsertionPoint(enqueue);
      Value valid = arith::ConstantOp::create(
          builder, enqueue.getLoc(), builder.getI1Type(),
          builder.getBoolAttr(true));
      state[found->second] = RootState{valid, enqueue.getValue()};
      enqueue.erase();
    }
    outgoing[block] = state;

    Operation *terminator = block->getTerminator();
    if (auto branch = dyn_cast<cf::BranchOp>(terminator)) {
      if (branch.getDest() == wait)
        continue;
      SmallVector<Value> operands(branch.getDestOperands());
      for (unsigned root = 0; root != roots.size(); ++root)
        if (liveIn[branch.getDest()][root]) {
          assert(state[root] && "live-out root has no reaching definition");
          operands.push_back(state[root]->first);
          operands.push_back(state[root]->second);
        }
      builder.setInsertionPoint(branch);
      cf::BranchOp::create(builder, branch.getLoc(), branch.getDest(),
                           operands);
      branch.erase();
      continue;
    }
    // Every terminator was proven to be one of the two branch forms before
    // this rewrite began. Bailing out here would leave the erased enqueues
    // without their replacement epilogue, so this is an invariant, not a
    // rejection point.
    auto branch = cast<cf::CondBranchOp>(terminator);
    SmallVector<Value> trueOperands(branch.getTrueDestOperands());
    SmallVector<Value> falseOperands(branch.getFalseDestOperands());
    auto appendState = [&](Block *destination, SmallVectorImpl<Value> &values) {
      if (destination == wait)
        return;
      for (unsigned root = 0; root != roots.size(); ++root)
        if (liveIn[destination][root]) {
          assert(state[root] && "live-out root has no reaching definition");
          values.push_back(state[root]->first);
          values.push_back(state[root]->second);
        }
    };
    appendState(branch.getTrueDest(), trueOperands);
    appendState(branch.getFalseDest(), falseOperands);
    builder.setInsertionPoint(branch);
    cf::CondBranchOp::create(builder, branch.getLoc(), branch.getCondition(),
                             branch.getTrueDest(), trueOperands,
                             branch.getFalseDest(), falseOperands);
    branch.erase();
  }

  // Each exit stages at most one final value per root. This is the generated
  // NBA boundary epilogue; publication/commit remains owned by the existing
  // graph-ordered barrier and therefore retains four-state and VPI behavior.
  struct Exit {
    Block *block;
    SmallVector<Value> waitOperands;
  };
  SmallVector<Exit> exits;
  for (Block *block : order) {
    if (!llvm::is_contained(block->getSuccessors(), wait))
      continue;
    if (auto branch = dyn_cast<cf::BranchOp>(block->getTerminator())) {
      exits.push_back({block, SmallVector<Value>(branch.getDestOperands())});
    } else {
      auto conditional = cast<cf::CondBranchOp>(block->getTerminator());
      ValueRange operands = conditional.getTrueDest() == wait
                                ? conditional.getTrueDestOperands()
                                : conditional.getFalseDestOperands();
      exits.push_back({block, SmallVector<Value>(operands)});
    }
  }
  for (Exit &regionExit : exits) {
    Block *exit = regionExit.block;
    Block *test = new Block;
    function.getBody().push_back(test);
    Operation *terminator = exit->getTerminator();
    builder.setInsertionPoint(terminator);
    if (auto branch = dyn_cast<cf::BranchOp>(terminator)) {
      cf::BranchOp::create(builder, branch.getLoc(), test);
    } else {
      // The epilogue chain takes no arguments. Wait-block operands are held
      // in `waitOperands` and re-supplied on the branch that finally reaches
      // the wait block; only the other edge keeps its own operands here.
      auto conditional = cast<cf::CondBranchOp>(terminator);
      bool trueExits = conditional.getTrueDest() == wait;
      Block *trueDest = trueExits ? test : conditional.getTrueDest();
      Block *falseDest = trueExits ? conditional.getFalseDest() : test;
      ValueRange trueOperands = trueExits ? ValueRange{}
                                          : conditional.getTrueDestOperands();
      ValueRange falseOperands = trueExits ? conditional.getFalseDestOperands()
                                           : ValueRange{};
      cf::CondBranchOp::create(builder, conditional.getLoc(),
                               conditional.getCondition(), trueDest,
                               trueOperands, falseDest, falseOperands);
    }
    terminator->erase();
    for (auto [rootIndex, root] : llvm::enumerate(roots)) {
      Block *emit = new Block;
      function.getBody().push_back(emit);
      Block *next = new Block;
      function.getBody().push_back(next);
      OptionalRootState state = outgoing.lookup(exit)[rootIndex];
      assert(state && "NBA epilogue root has no reaching definition");
      builder.setInsertionPointToEnd(test);
      cf::CondBranchOp::create(builder, root.representativeLocation,
                               state->first, emit, ValueRange{}, next,
                               ValueRange{});
      builder.setInsertionPointToStart(emit);
      sim::SimNBAEnqueueOp::create(
          builder, root.representativeLocation, state->second,
          root.destination, Value{}, root.representativeSite);
      cf::BranchOp::create(builder, root.representativeLocation, next);
      test = next;
    }
    builder.setInsertionPointToEnd(test);
    cf::BranchOp::create(builder, function.getLoc(), wait,
                         regionExit.waitOperands);
  }

  rootCount += roots.size();
  for (const RegionRoot &root : roots)
    stageCount += root.enqueues.size() - 1;
  return true;
}

void ObeliskSimOptimizeNativeRegionsPass::runOnOperation() {
  auto scheduler = getOperation()->getAttrOfType<sim::NativeSchedulerModeAttr>(
      "obelisk.native_scheduler");
  bool retainDirectBody =
      scheduler && scheduler.getValue() != sim::NativeSchedulerMode::Generic;
  SmallVector<sim::SimFuncOp> regions;
  getOperation().walk([&](sim::SimFuncOp function) {
    if (function->hasAttr(sim::metadata::nativeRegionBody))
      regions.push_back(function);
  });

  for (sim::SimFuncOp function : regions) {
    // Consume the marker here. No later lowering is permitted to infer a
    // semantic property merely from the generated symbol name.
    if (!retainDirectBody)
      function->removeAttr(sim::metadata::nativeRegionBody);
    uint64_t roots = 0;
    uint64_t stages = 0;
    if (!forwardRegionNextState(function, roots, stages))
      continue;
    ++optimizedRegions;
    coalescedRoots += roots;
    eliminatedStages += stages;
  }
}

} // namespace
} // namespace obelisk
