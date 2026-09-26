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

arith::CmpIPredicate swapPredicate(arith::CmpIPredicate predicate) {
  using P = arith::CmpIPredicate;
  switch (predicate) {
  case P::slt:
    return P::sgt;
  case P::sle:
    return P::sge;
  case P::sgt:
    return P::slt;
  case P::sge:
    return P::sle;
  case P::ult:
    return P::ugt;
  case P::ule:
    return P::uge;
  case P::ugt:
    return P::ult;
  case P::uge:
    return P::ule;
  case P::eq:
  case P::ne:
    return predicate;
  }
  llvm_unreachable("unknown integer comparison");
}

/// Prove the induction sequence leaves the continuation predicate after a
/// finite number of steps, in closed form, without enumerating iterations. Only
/// the monotone inequality and exactly reachable modular equality cases
/// qualify.
bool terminatesInFiniteSteps(const BoundedLoop &loop) {
  // IEEE 1800-2023 11.6.1 and 12.7.1: induction arithmetic has a fixed
  // width. Reaching the bound in mathematical integers is insufficient if
  // the last stride wraps before the next condition is evaluated.
  if (!arith::applyCmpPredicate(loop.predicate, loop.init, loop.limit))
    return true;
  if (loop.stride.isZero())
    return false;
  if (loop.predicate == arith::CmpIPredicate::eq)
    return true; // A nonzero modular step leaves equality after one iteration.
  if (loop.predicate == arith::CmpIPredicate::ne) {
    // A modular induction visits exactly one residue class modulo gcd(step,
    // 2^width). This also proves != loops that intentionally cross zero.
    APInt distance = loop.limit - loop.init;
    return distance.countr_zero() >= loop.stride.countr_zero();
  }

  bool increasing;
  bool inclusive;
  switch (loop.predicate) {
  case arith::CmpIPredicate::slt:
  case arith::CmpIPredicate::ult:
    increasing = true;
    inclusive = false;
    break;
  case arith::CmpIPredicate::sle:
  case arith::CmpIPredicate::ule:
    increasing = true;
    inclusive = true;
    break;
  case arith::CmpIPredicate::sgt:
  case arith::CmpIPredicate::ugt:
    increasing = false;
    inclusive = false;
    break;
  case arith::CmpIPredicate::sge:
  case arith::CmpIPredicate::uge:
    increasing = false;
    inclusive = true;
    break;
  default:
    return false;
  }
  unsigned width = loop.init.getBitWidth();
  unsigned proofWidth = width + 2;
  auto extend = [&](const APInt &value) {
    return loop.signedCompare ? value.sext(proofWidth) : value.zext(proofWidth);
  };
  APInt start = extend(loop.init), limit = extend(loop.limit);
  APInt delta = extend(loop.stride);
  if (!loop.isAdd)
    delta = -delta;
  if (delta.isNegative() == increasing)
    return false;
  APInt stride = increasing ? delta : -delta;
  APInt distance = increasing ? limit - start : start - limit;
  if (inclusive)
    ++distance;
  APInt trips = distance.udiv(stride);
  if (!distance.urem(stride).isZero())
    ++trips;
  APInt last = increasing ? start + trips * stride : start - trips * stride;
  APInt minimum = loop.signedCompare
                      ? APInt::getSignedMinValue(width).sext(proofWidth)
                      : APInt::getZero(proofWidth);
  APInt maximum = loop.signedCompare
                      ? APInt::getSignedMaxValue(width).sext(proofWidth)
                      : APInt::getMaxValue(width).zext(proofWidth);
  return last.sge(minimum) && last.sle(maximum);
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
/// other header computations and values used after the loop: none of them
/// can change how often the induction test passes. Replication also supports
/// loop-carried accumulators when their results leave via exit block arguments;
/// direct uses outside the loop still require a separate SSA repair.
std::optional<BoundedLoop>
recognizeBoundedLoop(Block *header,
                     const llvm::DenseSet<Operation *> &provenBackedges,
                     bool forMarking = false) {
  if (header->getNumArguments() == 0)
    return std::nullopt;
  auto condition = dyn_cast<cf::CondBranchOp>(header->getTerminator());
  if (!condition)
    return std::nullopt;
  // The induction variable is the header argument the continuation test
  // reads; any other argument is a loop-carried value.
  Value tested;
  if (auto compare = condition.getCondition().getDefiningOp<arith::CmpIOp>())
    tested = isa<BlockArgument>(compare.getLhs()) ? compare.getLhs()
                                                  : compare.getRhs();
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
    bool reversed = compare.getRhs() == induction;
    auto bound = (reversed ? compare.getLhs() : compare.getRhs())
                     .getDefiningOp<arith::ConstantIntOp>();
    if (compare->getBlock() != header || !bound)
      return std::nullopt;
    predicate = reversed ? swapPredicate(compare.getPredicate())
                         : compare.getPredicate();
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
    case sim::CompareKind::Eq:
      predicate = arith::CmpIPredicate::eq;
      break;
    case sim::CompareKind::Ne:
      predicate = arith::CmpIPredicate::ne;
      break;
    default:
      // Case/wildcard forms are not induction comparisons.
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
  else if (add && add.getRhs() == induction)
    step = add.getLhs();
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
    // A repeated run must not retain proofs invalidated by intervening edits.
    function.walk([&](Operation *operation) {
      operation->removeAttr(sim::metadata::boundedLoopLatch);
      operation->removeAttr(sim::metadata::boundedLoopHeader);
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
