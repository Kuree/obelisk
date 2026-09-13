//===- UnrollBoundedLoops.cpp - Expose bounded combinational CFGs --------===//

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/DenseSet.h"

using namespace mlir;

namespace obelisk {
#define GEN_PASS_DEF_OBELISKSIMUNROLLBOUNDEDLOOPSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

// Preserve the exact sequential activations of IEEE 1800-2023 12.7. A small
// constant induction loop is not a scheduler control loop: exposing its finite
// CFG before compute-graph construction also exposes constant packed indices.
// This deliberately excludes suspension, side exits, nested cycles, additional
// entries, and escaping SSA state. No loop-progress assumption is inferred
// from a SystemVerilog type or from an arbitrary iteration cutoff.
bool unrollLoop(Block *header, size_t &budget) {
  constexpr unsigned maxTrips = 64;
  if (header->getNumArguments() != 1)
    return false;
  auto induction = header->getArgument(0);
  if (!isa<IntegerType>(induction.getType()))
    return false;
  auto condition = dyn_cast<cf::CondBranchOp>(header->getTerminator());
  auto compare = condition
                     ? condition.getCondition().getDefiningOp<arith::CmpIOp>()
                     : nullptr;
  if (!compare || compare.getLhs() != induction ||
      compare->getBlock() != header)
    return false;
  auto bound = compare.getRhs().getDefiningOp<arith::ConstantIntOp>();
  if (!bound)
    return false;
  for (Operation &op : header->without_terminator())
    if (&op != compare.getOperation() && !isa<arith::ConstantOp>(op))
      return false;

  cf::BranchOp entry, latch;
  for (Block *pred : header->getPredecessors()) {
    auto branch = dyn_cast<cf::BranchOp>(pred->getTerminator());
    if (!branch || branch.getDestOperands().size() != 1)
      return false;
    if (branch.getDestOperands()[0].getDefiningOp<arith::ConstantIntOp>()) {
      if (entry)
        return false;
      entry = branch;
    } else {
      if (latch)
        return false;
      latch = branch;
    }
  }
  if (!entry || !latch)
    return false;
  Value increment = latch.getDestOperands()[0];
  auto add = increment.getDefiningOp<arith::AddIOp>();
  auto sub = increment.getDefiningOp<arith::SubIOp>();
  Value step;
  if (add && add.getLhs() == induction)
    step = add.getRhs();
  else if (sub && sub.getLhs() == induction)
    step = sub.getRhs();
  else
    return false;
  auto constantStep = step.getDefiningOp<arith::ConstantIntOp>();
  if (!constantStep)
    return false;

  APInt value = cast<IntegerAttr>(entry.getDestOperands()[0]
                                      .getDefiningOp<arith::ConstantIntOp>()
                                      .getValue())
                    .getValue();
  APInt limit = cast<IntegerAttr>(bound.getValue()).getValue();
  APInt stride = cast<IntegerAttr>(constantStep.getValue()).getValue();
  auto predicate = compare.getPredicate();
  bool signedCompare = predicate == arith::CmpIPredicate::slt ||
                       predicate == arith::CmpIPredicate::sle ||
                       predicate == arith::CmpIPredicate::sgt ||
                       predicate == arith::CmpIPredicate::sge;
  unsigned trips = 0;
  while (arith::applyCmpPredicate(compare.getPredicate(), value, limit)) {
    if (trips++ == maxTrips)
      return false;
    bool overflow = false;
    if (add)
      value = signedCompare ? value.sadd_ov(stride, overflow)
                            : value.uadd_ov(stride, overflow);
    else
      value = signedCompare ? value.ssub_ov(stride, overflow)
                            : value.usub_ov(stride, overflow);
    if (overflow)
      return false;
  }

  llvm::DenseSet<Block *> members{header};
  SmallVector<Block *> blocks{header};
  SmallVector<Block *> pending{condition.getTrueDest()};
  Block *exit = condition.getFalseDest();
  size_t operations = header->getOperations().size();
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    if (block == exit || block == entry->getBlock())
      return false;
    if (!members.insert(block).second)
      continue;
    if (block->getParent() != header->getParent() ||
        !isa<cf::BranchOp, cf::CondBranchOp>(block->getTerminator()))
      return false;
    blocks.push_back(block);
    operations += block->getOperations().size();
    for (Operation &op : *block)
      if (op.getNumRegions() != 0)
        return false;
    for (Block *successor : block->getSuccessors())
      if (successor != header)
        pending.push_back(successor);
  }
  if (!members.contains(latch->getBlock()) || members.contains(exit) ||
      (trips + 1) * operations > budget)
    return false;
  llvm::DenseMap<Block *, unsigned> indegree;
  for (Block *block : blocks) {
    if (block == header)
      continue;
    for (Block *pred : block->getPredecessors()) {
      if (!members.contains(pred))
        return false;
      ++indegree[block];
    }
  }
  // Removing the sole backedge must leave a DAG, not another loop that just
  // happens to be reachable from this constant induction header.
  pending = {header};
  unsigned visited = 0;
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    ++visited;
    for (Block *successor : block->getSuccessors())
      if (successor != header && members.contains(successor) &&
          --indegree[successor] == 0)
        pending.push_back(successor);
  }
  if (visited != blocks.size())
    return false;
  for (Block *block : blocks) {
    auto escapes = [&](Value value) {
      return llvm::any_of(value.getUsers(), [&](Operation *user) {
        return !members.contains(user->getBlock());
      });
    };
    if (llvm::any_of(block->getArguments(), escapes))
      return false;
    for (Operation &op : *block)
      if (llvm::any_of(op.getResults(), escapes))
        return false;
  }

  budget -= (trips + 1) * operations;
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
  void runOnOperation() override {
    size_t budget = 4096;
    bool changed;
    do {
      changed = false;
      for (Block &block : getOperation().getBody())
        if (unrollLoop(&block, budget)) {
          changed = true;
          break;
        }
    } while (changed && budget != 0);
  }
};
} // namespace
} // namespace obelisk
