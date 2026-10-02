//===- UnrollBoundedLoops.cpp - Expose bounded combinational CFGs --------===//

#include "obelisk/Analysis/BoundedLoopAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/DenseSet.h"

#include <optional>

using namespace mlir;

namespace obelisk {
#define GEN_PASS_DEF_OBELISKSIMUNROLLBOUNDEDLOOPSPASS
#define GEN_PASS_DEF_OBELISKSIMMARKBOUNDEDLOOPSPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {

using analysis::BoundedLoop;
using analysis::recognizeBoundedLoop;

bool unrollLoop(Block *header, size_t &budget, uint64_t &growthBudget) {
  constexpr unsigned maxTrips = 64;
  llvm::DenseSet<Operation *> noProvenBackedges;
  auto recognized = recognizeBoundedLoop(header, noProvenBackedges);
  if (!recognized)
    return false;
  BoundedLoop &loop = *recognized;
  cf::BranchOp entry = loop.entry, latch = loop.latch;
  ArrayRef<Block *> blocks = loop.blocks;
  bool add = loop.isAdd;

  // Replication needs the exact iteration count, so enumerate it here under the
  // policy cap. Termination itself was already proven in closed form.
  APInt value = loop.init;
  unsigned trips = 0;
  while (arith::applyCmpPredicate(loop.predicate, value, loop.limit)) {
    if (trips++ == maxTrips)
      return false;
    // The proof above handles fixed-width arithmetic, including modular
    // equality loops. Enumerate those same bit patterns for replication.
    value = add ? value + loop.stride : value - loop.stride;
  }
  size_t entryCost = entry ? 0 : header->getNumArguments() + 1;
  if ((trips + 1) * loop.operations + entryCost > budget)
    return false;
  uint64_t originalCost = 0;
  uint64_t replicatedCost = 0;
  for (Block *block : blocks) {
    uint64_t cost = 0;
    for (Operation &op : *block)
      cost += analysis::getSimulationOperationCost(op);
    originalCost += cost;
    replicatedCost += cost * (trips + (block == header));
  }
  uint64_t growth =
      replicatedCost > originalCost ? replicatedCost - originalCost : 0;
  // The operation cap bounds construction; the growth cap controls footprint.
  // Charge the same state/call-weighted cost as the inliner, after early
  // memory/value simplification. A termination certificate alone never
  // justifies replicating hundreds of state accesses and their publications.
  if (growth > growthBudget)
    return false;
  growthBudget -= growth;
  budget -= (trips + 1) * loop.operations + entryCost;
  // Recognition already proved every external entry's induction value and
  // all exits/wrap semantics. Funnel only the selected edges; an untaken
  // conditional entry must never execute even the zero-trip header effects.
  if (!entry) {
    SmallVector<std::pair<Operation *, unsigned>> edges;
    for (Block *predecessor : header->getPredecessors()) {
      auto *terminator = predecessor->getTerminator();
      if (terminator == latch.getOperation())
        continue;
      for (unsigned index = 0; index != terminator->getNumSuccessors(); ++index)
        if (terminator->getSuccessor(index) == header)
          edges.emplace_back(terminator, index);
    }
    auto *funnel = new Block;
    header->getParent()->getBlocks().insert(header->getIterator(), funnel);
    for (BlockArgument argument : header->getArguments())
      funnel->addArgument(argument.getType(), argument.getLoc());
    OpBuilder builder(header->getParent()->getContext());
    builder.setInsertionPointToEnd(funnel);
    entry = cf::BranchOp::create(builder, loop.condition.getLoc(), header,
                                 funnel->getArguments());
    for (auto [terminator, index] : edges)
      terminator->setSuccessor(funnel, index);
  }
  SmallVector<IRMapping> mappings(trips + 1);
  Region *region = header->getParent();
  for (unsigned iteration = 0; iteration <= trips; ++iteration) {
    IRMapping &mapping = mappings[iteration];
    for (Block *block : blocks) {
      if (iteration == trips && block != header)
        continue;
      auto *copy = new Block;
      region->getBlocks().insert(header->getIterator(), copy);
      mapping.map(block, copy);
      for (BlockArgument arg : block->getArguments())
        mapping.map(arg, copy->addArgument(arg.getType(), arg.getLoc()));
    }
    for (Block *block : blocks) {
      if (iteration == trips && block != header)
        continue;
      Block *copy = mapping.lookup(block);
      for (Operation &op : *block) {
        Operation *cloned = op.clone();
        copy->push_back(cloned);
        mapping.map(op.getResults(), cloned->getResults());
      }
    }
    // Remap after every definition exists; source block layout need not be
    // dominance order (e.g. a latch can precede one arm of a conditional).
    for (Block *block : blocks) {
      if (iteration == trips && block != header)
        continue;
      for (Operation &op : *mapping.lookup(block)) {
        for (OpOperand &operand : op.getOpOperands())
          operand.set(mapping.lookupOrDefault(operand.get()));
        for (BlockOperand &successor : op.getBlockOperands())
          successor.set(mapping.lookupOrDefault(successor.get()));
      }
    }
    auto branch =
        cast<cf::CondBranchOp>(mapping.lookup(header)->getTerminator());
    OpBuilder builder(branch);
    cf::BranchOp::create(builder, branch.getLoc(),
                         iteration == trips ? branch.getFalseDest()
                                            : branch.getTrueDest(),
                         iteration == trips ? branch.getFalseDestOperands()
                                            : branch.getTrueDestOperands());
    branch.erase();
  }
  for (unsigned iteration = 0; iteration < trips; ++iteration)
    cast<cf::BranchOp>(
        mappings[iteration].lookup(latch->getBlock())->getTerminator())
        .setDest(mappings[iteration + 1].lookup(header));
  entry.setDest(mappings.front().lookup(header));
  for (Block *block : blocks)
    block->dropAllReferences();
  for (Block *block : blocks)
    block->dropAllDefinedValueUses();
  for (Block *block : blocks)
    block->erase();
  return true;
}

class ObeliskSimUnrollBoundedLoopsPass
    : public impl::ObeliskSimUnrollBoundedLoopsPassBase<
          ObeliskSimUnrollBoundedLoopsPass> {
  using Base::Base;
  void runOnOperation() override {
    size_t budget = 4096;
    uint64_t growthBudget = maximumGrowth;
    bool changed;
    do {
      changed = false;
      for (Block &block : getOperation().getBody())
        if (unrollLoop(&block, budget, growthBudget)) {
          changed = true;
          break;
        }
    } while (changed && budget != 0);
  }
};

// Record the termination proof for every bounded induction loop, including the
// ones replication refused. This changes no IR: it only stops the compute graph
// from having to assume that a remaining CFG backedge might never exit.
class ObeliskSimMarkBoundedLoopsPass
    : public impl::ObeliskSimMarkBoundedLoopsPassBase<
          ObeliskSimMarkBoundedLoopsPass> {
  void runOnOperation() override {
    sim::SimFuncOp function = getOperation();
    if (function.getBody().empty())
      return;
    // A repeated run must not retain proofs invalidated by intervening edits.
    function.walk([&](Operation *operation) {
      ::obelisk::schedule::remove<schedule::metadata::boundedLoopLatch>(
          operation);
      ::obelisk::schedule::remove<schedule::metadata::boundedLoopHeader>(
          operation);
    });
    UnitAttr marker = UnitAttr::get(&getContext());
    llvm::DenseSet<Operation *> provenBackedges;
    // Innermost loops are recognized first; each round lets an enclosing sweep
    // become recognizable once its inner backedges are proven.
    bool changed;
    do {
      changed = false;
      for (Block &block : function.getBody()) {
        auto loop = recognizeBoundedLoop(&block, provenBackedges,
                                         /*forMarking=*/true);
        if (!loop || provenBackedges.contains(loop->latch.getOperation()))
          continue;
        provenBackedges.insert(loop->latch.getOperation());
        ::obelisk::schedule::set<schedule::metadata::boundedLoopLatch>(
            loop->latch, marker);
        ::obelisk::schedule::set<schedule::metadata::boundedLoopHeader>(
            loop->condition, marker);
        changed = true;
      }
    } while (changed);
    if (!provenBackedges.empty() &&
        function->getParentOfType<ModuleOp>()->hasAttr(
            "obelisk.debug.native_timing")) {
      // The diagnostic engine serializes output from concurrent function
      // passes.
      emitRemark(function.getLoc())
          << "obelisk bounded loops: proven=" << provenBackedges.size()
          << " actor=" << function.getSymName();
    }
  }
};
} // namespace
} // namespace obelisk
