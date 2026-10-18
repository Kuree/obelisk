//===- KnownVersioningAnalysis.cpp - Regions that can run on value planes -===//

#include "obelisk/Analysis/KnownVersioningAnalysis.h"
#include "mlir/Analysis/CFGLoopInfo.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/RegionGraphTraits.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Interfaces/ViewLikeInterface.h"
#include "obelisk/Analysis/ScopedIntegerRangeAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SetVector.h"

using namespace mlir;
namespace obelisk::analysis {
namespace {

bool isUnknownConstant(Value value) {
  auto constant = value.getDefiningOp<sim::SimLogicConstantOp>();
  return constant && !constant.getUnknown().isZero();
}

//===----------------------------------------------------------------------===//
// Function-local value domains
//===----------------------------------------------------------------------===//

} // namespace

bool provesSelectionInRange(Operation *selection,
                            const ScopedIntegerRangeAnalysis &ranges,
                            const ScopedRangeLattice &state) {
  if (!state.reachable)
    return false;
  auto within = [](std::optional<ConstantIntRanges> range, int64_t low,
                   int64_t high) {
    if (!range)
      return false;
    APInt min = range->smin(), max = range->smax();
    return min.getSignificantBits() <= 64 && max.getSignificantBits() <= 64 &&
           min.getSExtValue() >= low && max.getSExtValue() <= high;
  };
  if (auto extract = dyn_cast<sim::SimLogicDynExtractOp>(selection)) {
    int64_t inputWidth = extract.getInput().getType().getWidth();
    int64_t resultWidth = extract.getResult().getType().getWidth();
    return within(ranges.getRange(extract.getLowBit(), state), 0,
                  inputWidth - resultWidth);
  }
  if (auto extract = dyn_cast<sim::SimArrayDynExtractOp>(selection)) {
    auto bounds = getArraySelectionBounds(extract.getInput().getType());
    return bounds && within(ranges.getRange(extract.getIndex(), state),
                            bounds->first, bounds->second);
  }
  return false;
}

namespace {
/// Whether `op` is a dynamic selection that needs a range proof.
bool needsRangeProof(Operation *op) {
  return isa<sim::SimLogicDynExtractOp, sim::SimArrayDynExtractOp>(op) &&
         !isConstantInRangeSelection(op);
}

/// Range proofs for dynamic selections, read from the solver that also
/// solved the value domains.
class RangeProofs {
public:
  RangeProofs(DataFlowSolver &solver, const ScopedIntegerRangeAnalysis *ranges)
      : solver(solver), ranges(ranges) {}

  /// Whether the selection is valid by a constant index or a range proof.
  bool inRange(Operation *selection) const {
    if (isConstantInRangeSelection(selection))
      return true;
    if (!ranges)
      return false;
    auto *state = solver.lookupState<ScopedRangeLattice>(
        solver.getProgramPointBefore(selection));
    return state && provesSelectionInRange(selection, *ranges, *state);
  }

private:
  DataFlowSolver &solver;
  const ScopedIntegerRangeAnalysis *ranges;
};

/// The state-domain lattice of one SSA value: Bottom < TwoState < MayFourState.
class DomainValue {
public:
  DomainValue() = default;
  explicit DomainValue(StateDomain domain) : domain(domain) {}
  static DomainValue join(const DomainValue &lhs, const DomainValue &rhs) {
    return DomainValue(static_cast<unsigned>(lhs.domain) >=
                               static_cast<unsigned>(rhs.domain)
                           ? lhs.domain
                           : rhs.domain);
  }
  bool operator==(const DomainValue &other) const {
    return domain == other.domain;
  }
  void print(raw_ostream &os) const { os << stringifyStateDomain(domain); }
  StateDomain getDomain() const { return domain; }

private:
  StateDomain domain = StateDomain::Bottom;
};
using DomainLattice = dataflow::Lattice<DomainValue>;

/// The unconditional value domains of one function, under the shared IEEE
/// transfer. Entry arguments, calls, loads and net reads are four-state. This
/// only decides which operations a two-state copy actually improves.
class LocalStateDomainAnalysis final
    : public dataflow::SparseForwardDataFlowAnalysis<DomainLattice> {
public:
  LocalStateDomainAnalysis(DataFlowSolver &solver,
                           const ScopedIntegerRangeAnalysis *ranges,
                           llvm::function_ref<bool(Value)> provenKnown)
      : SparseForwardDataFlowAnalysis(solver), ranges(ranges),
        provenKnown(provenKnown) {}

  LogicalResult visitOperation(Operation *op,
                               ArrayRef<const DomainLattice *> operands,
                               ArrayRef<DomainLattice *> results) override {
    for (auto [operand, lattice] : llvm::zip(op->getOperands(), operands))
      if (hasUnknownPlane(operand.getType()) &&
          lattice->getValue().getDomain() == StateDomain::Bottom)
        return success();
    auto operandFact = [&](Value value) {
      for (auto [operand, lattice] : llvm::zip(op->getOperands(), operands))
        if (operand == value)
          return StateDomainFact{lattice->getValue().getDomain(),
                                 StateDomainReason::CFGJoin};
      return StateDomainFact{StateDomain::MayFourState,
                             StateDomainReason::UnsupportedProducer};
    };
    auto inRange = [&](Operation *selection) {
      if (isConstantInRangeSelection(selection))
        return true;
      if (!ranges)
        return false;
      // Revisit the selection when the ranges before it change.
      return provesSelectionInRange(
          selection, *ranges,
          *getOrCreateFor<ScopedRangeLattice>(
              getProgramPointAfter(op), getProgramPointBefore(selection)));
    };
    StateDomainFact fact = transferStateDomain(
        op, operandFact, [](Operation *) { return false; }, inRange);
    if (fact.domain == StateDomain::Bottom)
      return success();
    for (auto [result, lattice] : llvm::zip(op->getResults(), results))
      propagateIfChanged(
          lattice, lattice->join(DomainValue(
                       hasUnknownPlane(result.getType()) && !isProven(result)
                           ? fact.domain
                           : StateDomain::TwoState)));
    return success();
  }

  void setToEntryState(DomainLattice *lattice) override {
    auto value = dyn_cast<Value>(lattice->getAnchor());
    propagateIfChanged(
        lattice, lattice->join(DomainValue(value && isProven(value)
                                               ? StateDomain::TwoState
                                               : StateDomain::MayFourState)));
  }

private:
  bool isProven(Value value) const { return provenKnown && provenKnown(value); }

  const ScopedIntegerRangeAnalysis *ranges;
  llvm::function_ref<bool(Value)> provenKnown;
};

//===----------------------------------------------------------------------===//
// Region membership and operation admission
//===----------------------------------------------------------------------===//

/// The operations and block arguments of a candidate region.
struct Scope {
  llvm::DenseSet<Operation *> operations;
  llvm::DenseSet<Block *> argumentBlocks;
  bool defines(Value value) const {
    if (auto argument = dyn_cast<BlockArgument>(value))
      return argumentBlocks.contains(argument.getOwner());
    return operations.contains(value.getDefiningOp());
  }
};

enum class ReadKind { Checked, Owned, Varies };

/// Whether `value` is two-state before versioning: by the function's own
/// facts or by facts it already relies on.
class KnownFacts {
public:
  KnownFacts(const DataFlowSolver &domains,
             llvm::function_ref<bool(Value)> provenKnown,
             llvm::function_ref<bool(Value)> expectedKnown)
      : domains(domains), provenKnown(provenKnown),
        expectedKnown(expectedKnown) {}
  bool isExpectedKnown(Value value) const {
    return !expectedKnown || isKnown(value) || expectedKnown(value);
  }
  bool isKnown(Value value) const {
    if (!hasUnknownPlane(value.getType()))
      return true;
    // The local fixpoint already joins `provenKnown` with range proofs; a
    // value outside it (dead code) keeps the external fact alone.
    auto *lattice = domains.lookupState<DomainLattice>(value);
    if (lattice && lattice->getValue().getDomain() == StateDomain::TwoState)
      return true;
    return provenKnown && provenKnown(value);
  }

private:
  const DataFlowSolver &domains;
  llvm::function_ref<bool(Value)> provenKnown;
  llvm::function_ref<bool(Value)> expectedKnown;
};

class Admission {
public:
  Admission(sim::SimFuncOp function, const KnownFacts &facts,
            const RangeProofs &ranges)
      : function(function), facts(facts), ranges(ranges) {}

  /// Classify `op` against the region built so far. On success, update the
  /// candidate's reads, proofs and benefit and return an empty reason.
  StringRef admit(Operation *op, const Scope &scope, KnownRegion &region) {
    if (op->hasTrait<OpTrait::IsTerminator>()) {
      auto branch = dyn_cast<BranchOpInterface>(op);
      if (!branch)
        return "non-branch terminator";
      for (unsigned index = 0; index < op->getNumSuccessors(); ++index)
        if (branch.getSuccessorOperands(index).getProducedOperandCount())
          return "produced successor operand";
      return {};
    }
    if (op->getNumRegions() != 0)
      return "nested region";
    // An X/Z constant is an input no check can make known; the operation
    // using it, wherever the constant was materialized, stays four-state.
    if (llvm::any_of(op->getOperands(), isUnknownConstant))
      return "uses an X/Z constant";
    if (isa<sim::SimRefLoadOp, sim::SimNetReadOp>(op)) {
      // A read the function already proves known needs no check.
      if (facts.isKnown(op->getResult(0)))
        return {};
      if (!KnownVersioningAnalysis::isCastable(op->getResult(0).getType()))
        return "uncastable four-state read";
      llvm::SetVector<Operation *> slice;
      switch (classifyReference(op->getOperand(0), scope, slice)) {
      case ReadKind::Varies:
        return "read reference varies inside the region";
      case ReadKind::Owned:
        region.ownedReads.push_back(op);
        return {};
      case ReadKind::Checked:
        region.checkedReads.push_back({op, {slice.begin(), slice.end()}});
        return {};
      }
    }
    // A blocking store writes a known value in the two-state copy and clears
    // its destination's unknown bits. A private allocation starts from a
    // value checked or computed by the region. Every other effect keeps the
    // operation four-state.
    if (isa<sim::SimRefStoreOp, sim::SimRefAllocOp>(op))
      return {};
    if (!isMemoryEffectFree(op))
      return "unsupported effect";
    bool needsRange = false;
    auto inRange = [&](Operation *selection) {
      if (isConstantInRangeSelection(selection))
        return true;
      needsRange = ranges.inRange(selection);
      return needsRange;
    };
    if (llvm::any_of(op->getResultTypes(), hasUnknownPlane)) {
      StateDomainFact fact = transferStateDomain(
          op,
          [](Value) {
            return StateDomainFact{StateDomain::TwoState,
                                   StateDomainReason::NonLogic};
          },
          [](Operation *) { return true; }, inRange);
      if (fact.domain != StateDomain::TwoState)
        return "creates X/Z from known operands";
      if (needsRange) {
        // The copy casts the selection, so later lowering sees the proof.
        if (!KnownVersioningAnalysis::isCastable(op->getResult(0).getType()))
          return "uncastable range-proved selection";
        region.rangeProved.push_back(op);
      }
    }
    if (improves(op))
      region.benefit += getUnknownPlaneWork(op);
    return {};
  }

  /// Collect the in-region producers of `value`, which must be computable
  /// from values defined before the region. The slice runs at the region
  /// entry even where the original computes it on one path only, so every
  /// producer must be pure and speculatable. A probe through the recomputed
  /// reference is then safe as well: every reference view denotes the
  /// element default for an invalid selection rather than faulting.
  bool collectSlice(Value value, const Scope &scope,
                    llvm::SetVector<Operation *> &slice) {
    if (!scope.defines(value))
      return true;
    Operation *producer = value.getDefiningOp();
    if (!producer)
      return false;
    if (slice.contains(producer))
      return true;
    if (producer->getNumRegions() != 0 || !isPure(producer))
      return false;
    for (Value operand : producer->getOperands())
      if (!collectSlice(operand, scope, slice))
        return false;
    slice.insert(producer);
    return true;
  }

private:
  /// Whether the original function computes `op` with four-state operands or
  /// results, which the two-state copy removes.
  bool improves(Operation *op) const {
    auto fourState = [&](Value value) { return !facts.isKnown(value); };
    return llvm::any_of(op->getResults(), fourState) ||
           llvm::any_of(op->getOperands(), fourState);
  }

  ReadKind classifyReference(Value reference, const Scope &scope,
                             llvm::SetVector<Operation *> &slice) {
    // Storage allocated by the region holds only values the region wrote.
    for (Value root = reference;;) {
      Operation *producer = root.getDefiningOp();
      if (!producer || !scope.defines(root))
        break;
      if (isa<sim::SimRefAllocOp>(producer))
        return ReadKind::Owned;
      auto view = dyn_cast<ViewLikeOpInterface>(producer);
      if (!view)
        break;
      root = view.getViewSource();
    }
    return collectSlice(reference, scope, slice) ? ReadKind::Checked
                                                 : ReadKind::Varies;
  }

  sim::SimFuncOp function;
  const KnownFacts &facts;
  const RangeProofs &ranges;
};

/// Record the region's four-state live-ins: the values defined before it that
/// the function's own facts cannot prove two-state. Returns a rejection reason
/// when a live-in cannot be checked.
StringRef finishInputs(KnownRegion &region, const Scope &scope,
                       ArrayRef<Operation *> operations,
                       const KnownFacts &facts) {
  llvm::SetVector<Value> liveIns;
  for (Operation *op : operations)
    for (Value operand : op->getOperands())
      if (!scope.defines(operand) && !facts.isKnown(operand))
        liveIns.insert(operand);
  for (Value liveIn : liveIns)
    if (!facts.isExpectedKnown(liveIn))
      return "input expected to hold X/Z";
  for (const KnownRegionRead &read : region.checkedReads)
    if (!facts.isExpectedKnown(read.read->getResult(0)))
      return "input expected to hold X/Z";
  for (Value liveIn : liveIns)
    if (!KnownVersioningAnalysis::isCastable(liveIn.getType()) &&
        !(isa<sim::UnpackedArrayType, sim::UnpackedStructType>(
              liveIn.getType()) &&
          sim::getFixedBitStreamPlan(liveIn.getType())))
      return "uncheckable four-state live-in";
  region.liveIns.assign(liveIns.begin(), liveIns.end());
  return {};
}

/// A storage, net or other handle leaving the region is rematerialized after
/// it rather than merged: reference provenance must stay a static chain of
/// views for state layout, NBA planning and alias proofs.
bool handlesRematerialize(Admission &admission, const Scope &scope,
                          ArrayRef<Operation *> operations) {
  for (Operation *op : operations)
    for (Value result : op->getResults()) {
      if (!sim::isSimulationHandleType(result.getType()) ||
          llvm::all_of(result.getUsers(), [&](Operation *user) {
            return scope.operations.contains(user);
          }))
        continue;
      llvm::SetVector<Operation *> slice;
      // The handle itself is recomputed, so test its producer's operands.
      if (op->getNumRegions() != 0 || !isMemoryEffectFree(op))
        return false;
      for (Value operand : op->getOperands())
        if (!admission.collectSlice(operand, scope, slice))
          return false;
    }
  return true;
}

/// Operations versioning adds for `region`: the copy, one test and one
/// conjunction per checked input, a flatten/to_bits/from_bits/unflatten cast
/// per live-in, each read's recomputed reference and probe, and the branch.
unsigned addedOperations(const KnownRegion &region) {
  unsigned added = region.operations + 6 * region.liveIns.size() + 2;
  // A checked read is probed at the entry and cast in the copy.
  for (const KnownRegionRead &read : region.checkedReads)
    added += read.slice.size() + 3 + 4;
  added += 4 * (region.ownedReads.size() + region.rangeProved.size());
  return added;
}

//===----------------------------------------------------------------------===//
// Region discovery
//===----------------------------------------------------------------------===//

class Discovery {
public:
  Discovery(sim::SimFuncOp function, const KnownFacts &facts,
            const RangeProofs &ranges, SmallVectorImpl<KnownRegion> &candidates,
            SmallVectorImpl<RejectedKnownRegion> &rejected)
      : function(function), facts(facts), dominance(function),
        postDominance(function), admission(function, facts, ranges),
        candidates(candidates), rejected(rejected) {}

  void run() {
    Region &body = function.getBody();
    // MLIR builds no dominator tree for a single-block region, which has
    // neither loops nor branches.
    if (!body.hasOneBlock()) {
      CFGLoopInfo loopInfo(dominance.getDomTree(&body));
      SmallVector<CFGLoop *> worklist(
          llvm::reverse(loopInfo.getTopLevelLoops()));
      while (!worklist.empty()) {
        CFGLoop *loop = worklist.pop_back_val();
        if (admitLoop(loop))
          continue;
        llvm::append_range(worklist, llvm::reverse(loop->getSubLoops()));
      }
      for (CFGLoop *loop : loopInfo.getLoopsInPreorder())
        for (Block *block : loop->getBlocks())
          loopBlocks.insert(block);
      summarizeBlocks(body);
    }
    chains.push_back({&body.front(), nullptr, ~0u});
    while (!chains.empty())
      walkChain(chains.pop_back_val());
  }

private:
  void reject(KnownRegionKind kind, Block *block, StringRef reason,
              Operation *operation = nullptr) {
    rejected.push_back({kind, block, reason, operation});
  }

  //===--------------------------------------------------------------------===//
  // Loops: switch copies at every header visit.
  //===--------------------------------------------------------------------===//

  bool admitLoop(CFGLoop *loop) {
    Block *header = loop->getHeader();
    KnownRegion region;
    region.kind = KnownRegionKind::Loop;
    region.header = header;
    Scope scope;
    SmallVector<Operation *> operations;
    for (Block *block : loop->getBlocks()) {
      scope.argumentBlocks.insert(block);
      for (Operation &op : *block) {
        scope.operations.insert(&op);
        operations.push_back(&op);
      }
    }
    auto fail = [&](StringRef reason, Operation *op = nullptr) {
      reject(KnownRegionKind::Loop, header, reason, op);
      return false;
    };
    for (BlockArgument argument : header->getArguments())
      if (hasUnknownPlane(argument.getType()) &&
          !KnownVersioningAnalysis::isCastable(argument.getType()))
        return fail("uncastable four-state header argument");
    // Reads are checked at the header, so their references may depend only on
    // the header's arguments and on values defined before the loop.
    Scope headerScope = scope;
    headerScope.argumentBlocks.erase(header);
    for (Operation *op : operations)
      if (StringRef reason = admission.admit(op, headerScope, region);
          !reason.empty())
        return fail(reason, op);
    if (StringRef reason = finishInputs(region, scope, operations, facts);
        !reason.empty())
      return fail(reason);

    // Values leaving the loop are merged where both copies exit: an exit
    // block entered only from the loop.
    SmallVector<Block *> exits;
    loop->getUniqueExitBlocks(exits);
    SmallVector<Block *> dedicated;
    for (Block *exit : exits)
      if (llvm::all_of(exit->getPredecessors(),
                       [&](Block *pred) { return loop->contains(pred); }))
        dedicated.push_back(exit);
    auto mergeable = [&](Value value) {
      return llvm::all_of(value.getUsers(), [&](Operation *user) {
        return scope.operations.contains(user) ||
               llvm::any_of(dedicated, [&](Block *exit) {
                 return dominance.dominates(exit, user->getBlock());
               });
      });
    };
    for (Operation *op : operations)
      if (!llvm::all_of(op->getResults(), mergeable))
        return fail("live-out not dominated by an exit", op);
    for (Block *block : loop->getBlocks())
      if (!llvm::all_of(block->getArguments(), mergeable))
        return fail("live-out not dominated by an exit");

    if (!handlesRematerialize(admission, scope, operations))
      return fail("handle live-out varies inside the region");
    for (Block *block : loop->getBlocks())
      for (BlockArgument argument : block->getArguments())
        if (sim::isSimulationHandleType(argument.getType()) &&
            !llvm::all_of(argument.getUsers(), [&](Operation *user) {
              return scope.operations.contains(user);
            }))
          return fail("handle live-out varies inside the region");
    region.operations = operations.size();
    // A two-state iteration loads each checked location's unknown plane once,
    // as the four-state iteration did, and then folds it into one test.
    region.checkCost =
        region.checkedReads.empty() ? 0 : region.checkedReads.size() + 2;
    if (region.benefit <= region.checkCost)
      return fail("unprofitable");
    region.addedOperations = addedOperations(region);
    candidates.push_back(std::move(region));
    return true;
  }

  //===--------------------------------------------------------------------===//
  // Runs: acyclic code along dominator chains.
  //===--------------------------------------------------------------------===//

  struct Run {
    KnownRegion region;
    Scope scope;
    SmallVector<Operation *> operations;
    bool empty() const { return operations.empty(); }
  };

  Block *postDominator(Block *block) {
    if (function.getBody().hasOneBlock())
      return nullptr;
    auto *node = postDominance.getDomTree(&function.getBody()).getNode(block);
    auto *idom = node ? node->getIDom() : nullptr;
    return idom ? idom->getBlock() : nullptr;
  }

  /// A block's subtree as a range of dominator-tree preorder positions, and
  /// what keeps any run from holding it.
  struct BlockSummary {
    unsigned first = 0, last = 0;
    /// On a CFG cycle, which only a loop region may hold.
    bool cyclic = false;
    /// An operation, or the terminator, that no run admits whatever it
    /// already holds.
    bool blocked = false, terminatorBlocked = false;
  };

  void summarizeBlocks(Region &body) {
    for (auto scc = llvm::scc_begin(&body); !scc.isAtEnd(); ++scc)
      if (scc.hasCycle())
        for (Block *block : *scc)
          summaries[block].cyclic = true;
    // Only a read's reference depends on what a run holds, and an empty run
    // admits every read; so a failure with an empty scope fails in any run.
    Scope empty;
    for (Block &block : body)
      for (Operation &op : block) {
        KnownRegion scratch;
        if (admission.admit(&op, empty, scratch).empty())
          continue;
        BlockSummary &summary = summaries[&block];
        (op.hasTrait<OpTrait::IsTerminator>() ? summary.terminatorBlocked
                                              : summary.blocked) = true;
      }
    auto &tree = dominance.getDomTree(&body);
    SmallVector<Block *> preorder;
    for (auto *node : llvm::depth_first(tree.getRootNode())) {
      summaries[node->getBlock()].first = preorder.size();
      preorder.push_back(node->getBlock());
    }
    for (Block *block : llvm::reverse(preorder)) {
      BlockSummary &summary = summaries[block];
      summary.last = summary.first;
      for (auto *child : *tree.getNode(block))
        summary.last =
            std::max(summary.last, summaries[child->getBlock()].last);
    }
    blockedBefore.assign(preorder.size() + 1, 0);
    for (auto [index, block] : llvm::enumerate(preorder)) {
      const BlockSummary &summary = summaries[block];
      blockedBefore[index + 1] =
          blockedBefore[index] +
          (summary.cyclic || summary.blocked || summary.terminatorBlocked);
    }
  }

  /// The blocks a segment from `head` to `next` may hold besides `head`: the
  /// head's dominator subtree without `next`'s, as a half-open preorder range
  /// with an optional hole. A single-entry segment holds exactly these.
  struct SubtreeRange {
    unsigned first, last, holeFirst, holeLast;
  };
  SubtreeRange segmentRange(Block *head, Block *next) {
    const BlockSummary &summary = summaries.lookup(head);
    SubtreeRange range{summary.first + 1, summary.last + 1, 0, 0};
    if (next) {
      const BlockSummary &hole = summaries.lookup(next);
      if (summary.first < hole.first && hole.first <= summary.last)
        range.holeFirst = hole.first, range.holeLast = hole.last + 1;
    }
    return range;
  }

  /// Whether a segment from `head` holds a cycle or an operation no run
  /// admits, beyond the head's own operations, which are added one by one.
  bool segmentBlocked(Block *head, Block *next) {
    if (summaries.lookup(head).terminatorBlocked)
      return true;
    SubtreeRange range = segmentRange(head, next);
    unsigned count =
        blockedBefore[range.last] - blockedBefore[range.first] -
        (blockedBefore[range.holeLast] - blockedBefore[range.holeFirst]);
    return count != 0;
  }

  unsigned segmentSize(Block *head, Block *next) {
    SubtreeRange range = segmentRange(head, next);
    return 1 + range.last - range.first - (range.holeLast - range.holeFirst);
  }

  /// The blocks reachable from `head` before `next`, in reverse post-order,
  /// or none when one is entered from outside, which would give a run a
  /// second entry. The blocks are acyclic once `segmentBlocked` is false.
  SmallVector<Block *> segment(Block *head, Block *next) {
    SmallVector<Block *> postOrder;
    llvm::SmallPtrSet<Block *, 16> visited;
    visited.insert(head);
    SmallVector<std::pair<Block *, unsigned>> stack{{head, 0}};
    while (!stack.empty()) {
      Block *current = stack.back().first;
      unsigned index = stack.back().second++;
      if (index == current->getNumSuccessors()) {
        postOrder.push_back(current);
        stack.pop_back();
        continue;
      }
      Block *successor = current->getSuccessor(index);
      if (successor == next || !visited.insert(successor).second)
        continue;
      if (!dominance.dominates(head, successor))
        return {};
      stack.push_back({successor, 0});
    }
    return {postOrder.rbegin(), postOrder.rend()};
  }

  /// A chain of post-dominators to walk, and the largest segment it may try
  /// to hold whole.
  struct Chain {
    Block *start, *stop;
    unsigned limit;
  };

  /// Queue the chains below a segment that is not one run: the dominator-tree
  /// children of `head` that `next` does not dominate, which are exactly the
  /// ones the segment reaches. Each block is owned by the chain of the child
  /// that dominates it, so no block is walked twice.
  void queueChildren(Block *head, Block *next, unsigned limit) {
    auto *node = dominance.getNode(head);
    SmallVector<Block *> children;
    for (auto *child : *node)
      if (Block *block = child->getBlock();
          !next || !dominance.dominates(next, block))
        children.push_back(block);
    for (Block *child : llvm::reverse(children))
      chains.push_back({child, next, limit});
  }

  void startRun(Run &run, Operation *begin) {
    run = Run();
    run.region.kind = KnownRegionKind::Run;
    run.region.begin = begin;
  }

  /// Add one operation, or end the run before it. A read whose reference
  /// varies inside the run starts a new run, where it may be checkable.
  void addOperation(Run &run, Operation *op) {
    if (!run.region.begin)
      startRun(run, op);
    if (tryAdd(run, op))
      return;
    flush(run, op);
    startRun(run, op);
    if (tryAdd(run, op))
      return;
    run = Run();
  }

  /// A point a run can return to without copying it.
  struct Mark {
    size_t operations, checked, owned, proved;
    unsigned benefit;
  };
  static Mark mark(const Run &run) {
    return {run.operations.size(), run.region.checkedReads.size(),
            run.region.ownedReads.size(), run.region.rangeProved.size(),
            run.region.benefit};
  }
  static void rollback(Run &run, const Mark &mark,
                       ArrayRef<Block *> argumentBlocks = {}) {
    for (Operation *op : llvm::drop_begin(run.operations, mark.operations))
      run.scope.operations.erase(op);
    run.operations.resize(mark.operations);
    for (Block *block : argumentBlocks)
      run.scope.argumentBlocks.erase(block);
    run.region.checkedReads.resize(mark.checked);
    run.region.ownedReads.resize(mark.owned);
    run.region.rangeProved.resize(mark.proved);
    run.region.benefit = mark.benefit;
  }

  bool tryAdd(Run &run, Operation *op) {
    Mark before = mark(run);
    if (!admission.admit(op, run.scope, run.region).empty()) {
      rollback(run, before);
      return false;
    }
    run.scope.operations.insert(op);
    run.operations.push_back(op);
    return true;
  }

  /// Try to add a whole single-entry acyclic segment after its head's
  /// ordinary operations. On failure the run is unchanged.
  bool tryAddSegment(Run &run, ArrayRef<Block *> blocks) {
    Mark before = mark(run);
    if (!tryAdd(run, blocks.front()->getTerminator()))
      return false;
    for (auto [index, block] : llvm::enumerate(blocks.drop_front())) {
      run.scope.argumentBlocks.insert(block);
      for (Operation &op : *block)
        if (!tryAdd(run, &op)) {
          rollback(run, before, blocks.slice(1, index + 1));
          return false;
        }
    }
    return true;
  }

  void flush(Run &run, Operation *end) {
    // A run reaching the end of the body has no exit edge to merge at.
    if (run.empty() || !run.region.begin || !end) {
      run = Run();
      return;
    }
    KnownRegion &region = run.region;
    region.end = end;
    // A run ending where a block starts passes that block its arguments.
    if (end == &end->getBlock()->front())
      run.scope.argumentBlocks.erase(end->getBlock());
    Block *block = region.begin->getBlock();
    auto fail = [&](StringRef reason) {
      reject(KnownRegionKind::Run, block, reason, region.begin);
      run = Run();
    };
    if (StringRef reason =
            finishInputs(region, run.scope, run.operations, facts);
        !reason.empty())
      return fail(reason);
    if (!handlesRematerialize(admission, run.scope, run.operations))
      return fail("handle live-out varies inside the region");
    for (Block *argumentBlock : run.scope.argumentBlocks)
      for (BlockArgument argument : argumentBlock->getArguments())
        if (sim::isSimulationHandleType(argument.getType()) &&
            !llvm::all_of(argument.getUsers(), [&](Operation *user) {
              return run.scope.operations.contains(user);
            }))
          return fail("handle live-out varies inside the region");
    region.operations = run.operations.size();
    region.checkCost = region.checkedReads.size() + region.liveIns.size() + 2;
    // Code without four-state work is not a candidate at all.
    if (region.benefit == 0) {
      run = Run();
      return;
    }
    if (region.benefit <= region.checkCost)
      return fail("unprofitable");
    region.addedOperations = addedOperations(region);
    candidates.push_back(std::move(region));
    run = Run();
  }

  /// Partition the chain of post-dominators from `chain.start` into runs. The
  /// chain ends at `chain.stop` or at a block the start does not dominate,
  /// which a sibling path also enters and an enclosing chain owns.
  ///
  /// A segment is tried whole only when nothing in it is known to fail, and
  /// below a failed attempt only when at most half that attempt's size, so
  /// each block is visited by O(log n) failed attempts.
  void walkChain(const Chain &chain) {
    Run run;
    Block *block = chain.start;
    while (block && block != chain.stop) {
      if (!dominance.dominates(chain.start, block))
        break;
      Block *next = postDominator(block);
      bool loop = loopBlocks.contains(block);
      if (loop || summaries.lookup(block).cyclic) {
        flush(run, &block->front());
        // A cycle below a branch leaves its sibling paths acyclic.
        if (!loop)
          queueChildren(block, next, chain.limit);
        block = next;
        continue;
      }
      // A run entering this block from the previous segment owns its
      // arguments; a run starting here receives them from outside.
      if (!run.empty() && run.region.begin->getBlock() != block)
        run.scope.argumentBlocks.insert(block);
      for (Operation &op : block->without_terminator())
        addOperation(run, &op);
      Operation *terminator = block->getTerminator();
      if (!run.region.begin)
        startRun(run, terminator);
      if (llvm::all_of(block->getSuccessors(),
                       [&](Block *successor) { return successor == next; })) {
        if (!tryAdd(run, terminator))
          flush(run, terminator);
        block = next;
        continue;
      }
      unsigned limit = chain.limit;
      bool added = false;
      if (!segmentBlocked(block, next)) {
        unsigned size = segmentSize(block, next);
        if (size <= limit) {
          SmallVector<Block *> blocks = segment(block, next);
          added = !blocks.empty() && tryAddSegment(run, blocks);
          limit = size / 2;
        }
      }
      if (!added) {
        flush(run, terminator);
        queueChildren(block, next, limit);
      }
      block = next;
    }
    flush(run, block ? &block->front() : nullptr);
  }

  sim::SimFuncOp function;
  const KnownFacts &facts;
  DominanceInfo dominance;
  PostDominanceInfo postDominance;
  Admission admission;
  llvm::DenseSet<Block *> loopBlocks;
  DenseMap<Block *, BlockSummary> summaries;
  /// Blocked or cyclic blocks before each dominator-tree preorder position.
  SmallVector<unsigned> blockedBefore;
  SmallVector<Chain> chains;
  SmallVectorImpl<KnownRegion> &candidates;
  SmallVectorImpl<RejectedKnownRegion> &rejected;
};

} // namespace

bool KnownVersioningAnalysis::isCastable(Type type) {
  if (isa<sim::LogicType>(type))
    return true;
  return sim::isAggregateType(type) &&
         isa_and_nonnull<sim::LogicType>(sim::getPackedScalarType(type));
}

KnownVersioningAnalysis::~KnownVersioningAnalysis() = default;

KnownVersioningAnalysis::KnownVersioningAnalysis(
    sim::SimFuncOp function, llvm::function_ref<bool(Value)> provenKnown,
    llvm::function_ref<bool(Value)> expectedKnown) {
  Region &body = function.getBody();
  if (body.empty())
    return;
  // Suspension makes a region span activations; switching copies there would
  // move a continuation, so only activation-local bodies are considered.
  if (function
          .walk([](Operation *operation) {
            return sim::isSuspensionOp(operation) ? WalkResult::interrupt()
                                                  : WalkResult::advance();
          })
          .wasInterrupted())
    return;

  DataFlowSolver domains(DataFlowConfig().setInterprocedural(false));
  domains.load<dataflow::DeadCodeAnalysis>();
  domains.load<dataflow::SparseConstantPropagation>();
  // Path-sensitive ranges are solved only for a function that selects with a
  // non-constant index.
  ScopedIntegerRangeAnalysis *ranges = nullptr;
  if (function
          .walk([](Operation *op) {
            return needsRangeProof(op) ? WalkResult::interrupt()
                                       : WalkResult::advance();
          })
          .wasInterrupted())
    ranges = domains.load<ScopedIntegerRangeAnalysis>(function);
  domains.load<LocalStateDomainAnalysis>(ranges, provenKnown);
  if (failed(domains.initializeAndRun(function)))
    return;
  RangeProofs proofs(domains, ranges);

  SmallVector<KnownRegion, 0> candidates;
  KnownFacts facts(domains, provenKnown, expectedKnown);
  Discovery(function, facts, proofs, candidates, rejected).run();

  // Added code is a compile-space cost: allow half the function's size, or a
  // small fixed allowance that only matters for small functions, admitting
  // the regions that remove the most estimated work per added operation
  // first.
  constexpr unsigned smallFunctionAllowance = 512;
  unsigned size = 0;
  function.walk([&](Operation *) { ++size; });
  unsigned budget = std::max(size / 2, smallFunctionAllowance);
  llvm::stable_sort(
      candidates, [](const KnownRegion &lhs, const KnownRegion &rhs) {
        return uint64_t(lhs.benefit - lhs.checkCost) * rhs.addedOperations >
               uint64_t(rhs.benefit - rhs.checkCost) * lhs.addedOperations;
      });
  for (KnownRegion &region : candidates) {
    if (region.addedOperations > budget) {
      rejected.push_back(
          {region.kind,
           region.header ? region.header : region.begin->getBlock(),
           "exceeds the copy budget", region.begin});
      continue;
    }
    budget -= region.addedOperations;
    regions.push_back(std::move(region));
  }
}

} // namespace obelisk::analysis
