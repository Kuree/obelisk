//===- UnrollBoundedLoops.cpp - Expose bounded combinational CFGs --------===//

#include "obelisk/Dialect/Simulation/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>
#include <string>

using namespace mlir;

namespace obelisk {
#define GEN_PASS_DEF_OBELISKSIMUNROLLBOUNDEDLOOPSPASS
#define GEN_PASS_DEF_OBELISKSIMMARKBOUNDEDLOOPSPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {

// Preserve the exact sequential activations of IEEE 1800-2023 12.7. A small
// constant induction loop is not a scheduler control loop: exposing its finite
// CFG before compute-graph construction also exposes constant packed indices.
// This deliberately excludes suspension, side exits, nested cycles, additional
// entries, and escaping SSA state. No loop-progress assumption is inferred
// from a SystemVerilog type or from an arbitrary iteration cutoff.
//
// Termination and unrolling are separate questions. The structural proof below
// establishes that the loop finishes without any external event; whether to
// replicate its body is a code-size policy on top of that. A large constant
// induction loop -- an X-initialization sweep over a memory, a reset sweep over
// a queue -- must keep the proof even when replication is refused, because the
// scheduler otherwise has to treat it as an unbounded control loop and hand its
// whole process to bytecode.
struct BoundedLoop {
  Block *header;
  cf::CondBranchOp condition;
  cf::BranchOp entry;
  cf::BranchOp latch;
  Block *exit;
  SmallVector<Block *> blocks;
  llvm::DenseSet<Block *> members;
  size_t operations = 0;
  APInt init;
  APInt limit;
  APInt stride;
  arith::CmpIPredicate predicate = arith::CmpIPredicate::slt;
  bool isAdd = false;
  bool signedCompare = false;
};

/// Prove the induction sequence leaves the continuation predicate after a finite
/// number of steps, in closed form, without enumerating iterations. Only the
/// monotone cases are accepted; anything else is declined rather than assumed.
bool terminatesInFiniteSteps(const BoundedLoop &loop) {
  unsigned width = loop.init.getBitWidth();
  if (loop.stride.isZero())
    return false;
  bool towardLimit;
  bool needsHeadroom;
  switch (loop.predicate) {
  case arith::CmpIPredicate::slt:
  case arith::CmpIPredicate::ult:
    // The value must reach `limit`, which is representable by construction.
    towardLimit = loop.isAdd;
    needsHeadroom = false;
    break;
  case arith::CmpIPredicate::sle:
  case arith::CmpIPredicate::ule:
    // The value must pass `limit`, so `limit` itself must not be the extreme.
    towardLimit = loop.isAdd;
    needsHeadroom = true;
    break;
  case arith::CmpIPredicate::sgt:
  case arith::CmpIPredicate::ugt:
    towardLimit = !loop.isAdd;
    needsHeadroom = false;
    break;
  case arith::CmpIPredicate::sge:
  case arith::CmpIPredicate::uge:
    towardLimit = !loop.isAdd;
    needsHeadroom = true;
    break;
  default:
    // eq/ne give no monotone progress toward a bound.
    return false;
  }
  if (!towardLimit)
    return false;
  // A stride that is negative under the comparison's own signedness moves away
  // from the bound, so the direction above would be wrong.
  if (loop.signedCompare ? loop.stride.isNegative()
                         : loop.stride.isZero())
    return false;
  if (!needsHeadroom)
    return true;
  bool increasing = loop.isAdd;
  if (loop.signedCompare)
    return increasing ? !loop.limit.isMaxSignedValue()
                      : !loop.limit.isMinSignedValue();
  return increasing ? !loop.limit.isMaxValue() : !loop.limit.isZero();
}

/// Structural recognition shared by the marking and unrolling consumers.
/// `provenBackedges` names latch branches already proven to terminate, so an
/// enclosing loop can be recognized once its inner loops are: the DAG check
/// below would otherwise reject every nested sweep.
///
/// `forMarking` asks only whether the latch runs a bounded number of times,
/// not whether the loop can be replicated. IEEE 1800-2023 12.7.1 exits a
/// for-loop as soon as its test fails, and the induction variable here is an
/// SSA value no other process can modify. The marking therefore accepts a body
/// that leaves before the induction bound: `break` and `return` (12.8) only
/// leave the loop sooner, and neither does the `$finish` check that an inlined
/// call leaves behind (20.2), a terminator with no successors. It also accepts
/// values carried around the loop beside the induction variable (an
/// accumulator such as `x ^= a[i]` promoted to SSA), other header
/// computations, and values used after the loop: none of them can change how
/// often the induction test passes. Replication rewrites the SSA of a
/// single-exit loop and needs all of these restrictions.
std::optional<BoundedLoop>
recognizeBoundedLoop(Block *header,
                     const llvm::DenseSet<Operation *> &provenBackedges,
                     bool forMarking = false) {
  if (header->getNumArguments() == 0 ||
      (!forMarking && header->getNumArguments() != 1))
    return std::nullopt;
  auto condition = dyn_cast<cf::CondBranchOp>(header->getTerminator());
  if (!condition)
    return std::nullopt;
  // The induction variable is the header argument the continuation test
  // reads; any other argument is a loop-carried value.
  Value tested;
  if (auto compare = condition.getCondition().getDefiningOp<arith::CmpIOp>())
    tested = compare.getLhs();
  else if (auto isTrue =
               condition.getCondition().getDefiningOp<sim::SimLogicIsTrueOp>())
    if (auto compare = isTrue.getInput().getDefiningOp<sim::SimLogicCompareOp>())
      if (auto fromBits =
              compare.getLhs().getDefiningOp<sim::SimLogicFromBitsOp>())
        tested = fromBits.getInput();
  auto induction = dyn_cast_or_null<BlockArgument>(tested);
  if (!induction || induction.getOwner() != header ||
      !isa<IntegerType>(induction.getType()))
    return std::nullopt;
  unsigned inductionIndex = induction.getArgNumber();

  // The continuation test appears in two equivalent shapes. A two-state
  // comparison lowers to arith.cmpi directly. A comparison written against an
  // elaborated parameter keeps SystemVerilog's four-state form:
  //   from_bits(induction) -> logic.compare -> is_true
  // Both describe the same induction test. Accept either, but require the
  // four-state bound to have an empty unknown plane: an X in the bound makes
  // the comparison result unknown, which proves nothing about termination.
  llvm::SmallPtrSet<Operation *, 4> testOperations;
  std::optional<arith::CmpIPredicate> predicate;
  APInt limit;
  if (auto compare = condition.getCondition().getDefiningOp<arith::CmpIOp>()) {
    auto bound = compare.getRhs().getDefiningOp<arith::ConstantIntOp>();
    if (compare.getLhs() != induction || compare->getBlock() != header || !bound)
      return std::nullopt;
    predicate = compare.getPredicate();
    limit = cast<IntegerAttr>(bound.getValue()).getValue();
    testOperations.insert(compare.getOperation());
  } else if (auto isTrue =
                 condition.getCondition().getDefiningOp<sim::SimLogicIsTrueOp>()) {
    auto compare = isTrue.getInput().getDefiningOp<sim::SimLogicCompareOp>();
    if (!compare || compare->getBlock() != header ||
        isTrue->getBlock() != header)
      return std::nullopt;
    auto fromBits = compare.getLhs().getDefiningOp<sim::SimLogicFromBitsOp>();
    auto bound = compare.getRhs().getDefiningOp<sim::SimLogicConstantOp>();
    if (!fromBits || fromBits.getInput() != induction || !bound ||
        !bound.getUnknown().isZero())
      return std::nullopt;
    switch (compare.getKind()) {
    case sim::CompareKind::SLT:
      predicate = arith::CmpIPredicate::slt;
      break;
    case sim::CompareKind::SLE:
      predicate = arith::CmpIPredicate::sle;
      break;
    case sim::CompareKind::SGT:
      predicate = arith::CmpIPredicate::sgt;
      break;
    case sim::CompareKind::SGE:
      predicate = arith::CmpIPredicate::sge;
      break;
    case sim::CompareKind::ULT:
      predicate = arith::CmpIPredicate::ult;
      break;
    case sim::CompareKind::ULE:
      predicate = arith::CmpIPredicate::ule;
      break;
    case sim::CompareKind::UGT:
      predicate = arith::CmpIPredicate::ugt;
      break;
    case sim::CompareKind::UGE:
      predicate = arith::CmpIPredicate::uge;
      break;
    default:
      // Equality and the case/wildcard forms give no monotone bound.
      return std::nullopt;
    }
    limit = bound.getValue();
    testOperations.insert({isTrue.getOperation(), compare.getOperation(),
                           fromBits.getOperation()});
  } else {
    return std::nullopt;
  }
  // The induction variable and the limit are compared as one width.
  if (limit.getBitWidth() != induction.getType().getIntOrFloatBitWidth())
    limit = limit.zextOrTrunc(induction.getType().getIntOrFloatBitWidth());
  if (!forMarking)
    for (Operation &op : header->without_terminator())
      if (!testOperations.contains(&op) &&
          !isa<arith::ConstantOp, sim::SimLogicConstantOp>(op))
        return std::nullopt;

  // The entry predecessor is a plain branch for a top-level loop, but a
  // conditional branch whenever the loop is nested inside another sweep or
  // guarded by an `if` -- the two shapes RTL reset and initialization loops
  // actually take. Folding an empty `if` join into the header leaves several
  // entry edges; they are one entry when all start the induction at the same
  // constant. The latch must stay a plain branch: a conditional backedge
  // would be a second exit that the termination argument does not cover.
  SmallVector<Operation *, 2> entryOps;
  Value entryValue;
  cf::BranchOp entry, latch;
  for (Block *pred : header->getPredecessors()) {
    Operation *terminator = pred->getTerminator();
    Value incoming;
    if (auto branch = dyn_cast<cf::BranchOp>(terminator)) {
      if (branch.getDestOperands().size() != header->getNumArguments())
        return std::nullopt;
      incoming = branch.getDestOperands()[inductionIndex];
      if (!incoming.getDefiningOp<arith::ConstantIntOp>()) {
        if (latch)
          return std::nullopt;
        latch = branch;
        continue;
      }
    } else if (auto branch = dyn_cast<cf::CondBranchOp>(terminator)) {
      bool trueEdge = branch.getTrueDest() == header;
      bool falseEdge = branch.getFalseDest() == header;
      // A predicate that reaches the header on both edges gives no single
      // initial value.
      if (trueEdge == falseEdge)
        return std::nullopt;
      OperandRange operands = trueEdge ? branch.getTrueDestOperands()
                                       : branch.getFalseDestOperands();
      if (operands.size() != header->getNumArguments())
        return std::nullopt;
      incoming = operands[inductionIndex];
      if (!incoming.getDefiningOp<arith::ConstantIntOp>())
        return std::nullopt;
    } else {
      return std::nullopt;
    }
    if (entryValue &&
        cast<IntegerAttr>(
            entryValue.getDefiningOp<arith::ConstantIntOp>().getValue())
                .getValue() !=
            cast<IntegerAttr>(
                incoming.getDefiningOp<arith::ConstantIntOp>().getValue())
                .getValue())
      return std::nullopt;
    entryOps.push_back(terminator);
    entryValue = incoming;
  }
  if (entryOps.empty() || !latch)
    return std::nullopt;
  // Replication rewrites a single plain entry branch in place.
  if (entryOps.size() == 1)
    entry = dyn_cast<cf::BranchOp>(entryOps.front());
  Value increment = latch.getDestOperands()[inductionIndex];
  auto add = increment.getDefiningOp<arith::AddIOp>();
  auto sub = increment.getDefiningOp<arith::SubIOp>();
  Value step;
  if (add && add.getLhs() == induction)
    step = add.getRhs();
  else if (sub && sub.getLhs() == induction)
    step = sub.getRhs();
  else
    return std::nullopt;
  auto constantStep = step.getDefiningOp<arith::ConstantIntOp>();
  if (!constantStep)
    return std::nullopt;

  BoundedLoop loop;
  loop.header = header;
  loop.condition = condition;
  loop.entry = entry;
  loop.latch = latch;
  loop.exit = condition.getFalseDest();
  loop.init =
      cast<IntegerAttr>(
          entryValue.getDefiningOp<arith::ConstantIntOp>().getValue())
          .getValue();
  loop.limit = limit;
  loop.stride = cast<IntegerAttr>(constantStep.getValue()).getValue();
  loop.predicate = *predicate;
  loop.isAdd = static_cast<bool>(add);
  loop.signedCompare = loop.predicate == arith::CmpIPredicate::slt ||
                       loop.predicate == arith::CmpIPredicate::sle ||
                       loop.predicate == arith::CmpIPredicate::sgt ||
                       loop.predicate == arith::CmpIPredicate::sge;

  loop.members.insert(header);
  loop.blocks.push_back(header);
  SmallVector<Block *> pending{condition.getTrueDest()};
  loop.operations = header->getOperations().size();
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    if (forMarking && block == loop.exit)
      continue;
    if (block == loop.exit ||
        llvm::any_of(entryOps, [&](Operation *entryOp) {
          return entryOp->getBlock() == block;
        }))
      return std::nullopt;
    if (!loop.members.insert(block).second)
      continue;
    Operation *terminator = block->getTerminator();
    bool leavesProcedure =
        forMarking && terminator->getNumSuccessors() == 0;
    if (block->getParent() != header->getParent() ||
        (!leavesProcedure &&
         !isa<cf::BranchOp, cf::CondBranchOp>(terminator)))
      return std::nullopt;
    loop.blocks.push_back(block);
    loop.operations += block->getOperations().size();
    for (Operation &op : *block)
      if (op.getNumRegions() != 0)
        return std::nullopt;
    for (Block *successor : block->getSuccessors())
      if (successor != header)
        pending.push_back(successor);
  }
  if (!loop.members.contains(latch->getBlock()) ||
      loop.members.contains(loop.exit))
    return std::nullopt;
  llvm::DenseMap<Block *, unsigned> indegree;
  for (Block *block : loop.blocks) {
    if (block == header)
      continue;
    for (Block *pred : block->getPredecessors()) {
      if (!loop.members.contains(pred))
        return std::nullopt;
      ++indegree[block];
    }
  }
  // Removing this backedge, and any inner backedge already proven to terminate,
  // must leave a DAG -- not another loop that merely happens to be reachable
  // from this constant induction header.
  auto ignoredEdge = [&](Block *block, Block *successor) {
    if (successor == header)
      return true;
    auto branch = dyn_cast<cf::BranchOp>(block->getTerminator());
    return branch && provenBackedges.contains(branch.getOperation()) &&
           branch.getDest() == successor;
  };
  for (Block *block : loop.blocks)
    for (Block *successor : block->getSuccessors())
      if (successor != header && loop.members.contains(successor) &&
          ignoredEdge(block, successor))
        --indegree[successor];
  pending = {header};
  unsigned visited = 0;
  llvm::DenseSet<Block *> seen{header};
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    ++visited;
    for (Block *successor : block->getSuccessors())
      if (loop.members.contains(successor) &&
          !ignoredEdge(block, successor) && --indegree[successor] == 0 &&
          seen.insert(successor).second)
        pending.push_back(successor);
  }
  if (visited != loop.blocks.size())
    return std::nullopt;
  // Replication rewrites each value defined in the loop; marking does not.
  if (!forMarking)
    for (Block *block : loop.blocks) {
      auto escapes = [&](Value value) {
        return llvm::any_of(value.getUsers(), [&](Operation *user) {
          return !loop.members.contains(user->getBlock());
        });
      };
      if (llvm::any_of(block->getArguments(), escapes))
        return std::nullopt;
      for (Operation &op : *block)
        if (llvm::any_of(op.getResults(), escapes))
          return std::nullopt;
    }
  if (!terminatesInFiniteSteps(loop))
    return std::nullopt;
  return loop;
}

bool unrollLoop(Block *header, size_t &budget) {
  constexpr unsigned maxTrips = 64;
  llvm::DenseSet<Operation *> noProvenBackedges;
  auto recognized = recognizeBoundedLoop(header, noProvenBackedges);
  if (!recognized)
    return false;
  BoundedLoop &loop = *recognized;
  // Replication rewrites the entry edge in place, so it stays restricted to a
  // plain entry branch. Recognition also admits a conditional entry -- a nested
  // or `if`-guarded sweep -- which only the termination mark consumes.
  if (!loop.entry)
    return false;
  cf::CondBranchOp condition = loop.condition;
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
    bool overflow = false;
    if (add)
      value = loop.signedCompare ? value.sadd_ov(loop.stride, overflow)
                                 : value.uadd_ov(loop.stride, overflow);
    else
      value = loop.signedCompare ? value.ssub_ov(loop.stride, overflow)
                                 : value.usub_ov(loop.stride, overflow);
    if (overflow)
      return false;
  }
  if ((trips + 1) * loop.operations > budget)
    return false;
  budget -= (trips + 1) * loop.operations;
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
        loop->latch->setAttr(sim::metadata::boundedLoopLatch, marker);
        loop->condition->setAttr(sim::metadata::boundedLoopHeader, marker);
        changed = true;
      }
    } while (changed);
    if (!provenBackedges.empty() &&
        function->getParentOfType<ModuleOp>()->hasAttr(
            "obelisk.debug.native_timing")) {
      // Per-function passes run concurrently, so build the line before writing
      // it: separate stream insertions interleave between actors.
      std::string line;
      llvm::raw_string_ostream(line)
          << "obelisk bounded loops: proven=" << provenBackedges.size()
          << " actor=" << function.getSymName() << '\n';
      llvm::errs() << line;
    }
  }
};
} // namespace
} // namespace obelisk
