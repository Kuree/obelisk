//===- VersionKnownActivations.cpp - Gated two-state activation copies ----===//
//
// An activation whose storage roots are known in steady state runs copies in
// which its reads of those roots load only their value planes and its stores
// of known values write only theirs. The body is cut into segments: runs of
// operations that control enters only at their first operation. One check at
// a segment's entry tests the unknown summary bits of the roots the segment
// reads or stores known, and the private slots and live-in values that only
// the copy treats as known; when it fails, the original four-state segment
// runs:
//
//   head:  %ok = known.roots ...  cond_br %ok, ^cast, ^segment
//   ^cast: live-in casts; br ^copy     ^segment: the original operations
//   ^copy: the same operations, gated reads and known stores value-only
//   both -> ^exit(live-outs)
//
// A read the check cannot cover is tested where the copy loads it; on X/Z
// the copy continues in the original right after the same read.
//
// Both copies perform the same effects with the same values: every gated
// read observes known bits, and every known store writes a known value over
// unknown bits that are zero (LocalUnknownAnalysis). A check observes the
// canonical planes after every earlier write, so a failed check affects only
// its own segment. Original blocks keep their positions, which native
// compute-graph fragments use to name blocks; split blocks and copies go to
// the end of the body.
//
//===----------------------------------------------------------------------===//

#include "../../../Conversion/SimulationToSchedule/NativePipeline.h"
#include "../../Simulation/SimulationVerifiers.h"
#include "KnownCasts.h"
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Analysis/KnownVersioningAnalysis.h"
#include "obelisk/Analysis/LocalUnknownAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"

#include "mlir/Analysis/CFGLoopInfo.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/GenericIteratedDominanceFrontier.h"

using namespace mlir;
namespace obelisk {
#define GEN_PASS_DEF_VERSIONKNOWNACTIVATIONSPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"

namespace {
using analysis::LocalUnknownAnalysis;

// The copy is the expected path once reset has cleared X/Z.
constexpr int32_t likelyWeight = (1 << 20) - 1;
constexpr int32_t unlikelyWeight = 1;

/// Read-only design facts shared by the function workers.
struct DesignInputs {
  DesignInputs(sim::SimDesignOp design, bool variants)
      : handles(design),
        callees(LocalUnknownAnalysis::summarize(design, handles)) {
    for (auto storage :
         design.getBody().front().getOps<sim::SimStorageDeclOp>()) {
      storageTypes[storage.getId()] = storage.getType();
      if (auto ranges = sim::get<sim::metadata::SteadyUnknown>(storage))
        steadyUnknown[storage.getId()] = ranges;
    }
    // A guarded two-state variant already loads only the value planes of
    // its inductively two-state reads; those need no check.
    if (variants &&
        llvm::any_of(design.getBody().front().getOps<sim::SimFuncOp>(),
                     [](sim::SimFuncOp function) {
                       return schedule::has<
                           schedule::Field::EvalInductiveTwoState>(function);
                     })) {
      auto proofs = StateDomainAnalysis::computeForSpecialization(design);
      if (succeeded(proofs))
        domains.emplace(std::move(*proofs));
    }
  }
  /// Whether the variant `function` proves `read` known.
  bool provesKnown(sim::SimFuncOp function, const analysis::HandleFacts &facts,
                   Operation *read) const {
    if (!domains ||
        !schedule::has<schedule::Field::EvalInductiveTwoState>(function))
      return false;
    const StateDomainAnalysis &guarded =
        schedule::has<schedule::Field::EvalConditionallyTwoState>(function)
            ? domains->second
            : domains->first;
    auto fact = facts.find(read->getOperand(0));
    return fact != facts.end() && fact->second.descriptor &&
           !fact->second.dynamic &&
           guarded.isTwoStateWithInductiveRoots(read->getResult(0)) &&
           guarded.isInductivelyTwoState(fact->second.resource,
                                         *fact->second.descriptor);
  }
  /// Whether bits [low, high) of root `descriptor` include bits of its
  /// steady pattern.
  bool staysUnknown(uint64_t descriptor, uint64_t low, uint64_t high) const {
    auto ranges = steadyUnknown.lookup(descriptor);
    if (!ranges)
      return false;
    for (size_t index = 0; index + 1 < size_t(ranges.size()); index += 2)
      if (uint64_t(ranges[index]) < high &&
          low < uint64_t(ranges[index] + ranges[index + 1]))
        return true;
    return false;
  }
  analysis::HandleDataflowAnalysis handles;
  LocalUnknownAnalysis::CalleeSummaries callees;
  DenseMap<uint64_t, Type> storageTypes;
  DenseMap<uint64_t, DenseI64ArrayAttr> steadyUnknown;
  std::optional<std::pair<StateDomainAnalysis, StateDomainAnalysis>> domains;
};

/// The segments of a body. `begins[k]` is segment k's first operation; a
/// segment ends where the next begins.
struct Segments {
  LocalUnknownAnalysis::Segmentation segmentation;
  SmallVector<Operation *> begins;
};

unsigned countOperations(Operation *op) {
  unsigned count = 0;
  op->walk([&](Operation *) { ++count; });
  return count;
}

/// Cut the body along its chain of post-dominators: every path from the
/// entry passes each chain block in turn, so an operation of a chain block
/// outside every loop starts a segment entered only there. A segment closes
/// at the first such cut after `targetSize` operations; zero keeps one
/// segment.
Segments cut(sim::SimFuncOp function, uint64_t targetSize) {
  Region &body = function.getBody();
  Block *entry = &body.front();
  Segments result;
  auto &segments = result.segmentation.segments;
  auto assign = [&](Operation *op, unsigned segment) {
    op->walk([&](Operation *nested) { segments[nested] = segment; });
  };
  result.begins.push_back(&entry->front());

  SmallVector<Block *> chain{entry};
  DenseMap<Block *, SmallVector<Block *>> owned;
  std::unique_ptr<DominanceInfo> dominance;
  std::unique_ptr<CFGLoopInfo> loops;
  if (!body.hasOneBlock()) {
    dominance = std::make_unique<DominanceInfo>(function);
    PostDominanceInfo postDominance(function);
    loops = std::make_unique<CFGLoopInfo>(dominance->getDomTree(&body));
    for (Block *block = entry;;) {
      auto *node = postDominance.getNode(block);
      auto *parent = node ? node->getIDom() : nullptr;
      Block *next = parent ? parent->getBlock() : nullptr;
      if (!next || !dominance->dominates(block, next))
        break;
      chain.push_back(next);
      block = next;
    }
    // Every other block belongs to the nearest chain block dominating it.
    llvm::SmallPtrSet<Block *, 16> inChain(chain.begin(), chain.end());
    for (Block &block : body) {
      if (inChain.contains(&block))
        continue;
      auto *node = dominance->getNode(&block);
      while (node && !inChain.contains(node->getBlock()))
        node = node->getIDom();
      if (node)
        owned[node->getBlock()].push_back(&block);
    }
  }

  unsigned segment = 0;
  uint64_t size = 0;
  for (Block *block : chain) {
    bool inLoop = loops && loops->getLoopFor(block);
    for (Operation &op : *block) {
      if (targetSize && size >= targetSize && !inLoop &&
          &op != &entry->front() && !op.hasTrait<OpTrait::IsTerminator>()) {
        ++segment;
        size = 0;
        result.begins.push_back(&op);
        result.segmentation.begins.insert(&op);
      }
      assign(&op, segment);
      size += countOperations(&op);
    }
    for (Block *other : owned.lookup(block))
      for (Operation &op : *other) {
        assign(&op, segment);
        size += countOperations(&op);
      }
  }
  // Blocks the dominator tree does not reach never execute.
  for (Block &block : body)
    for (Operation &op : block)
      if (!segments.contains(&op))
        assign(&op, 0);
  result.segmentation.count = segment + 1;
  return result;
}

/// Whether a block argument may merge values of `type`: neither a reference,
/// whose provenance must stay a static chain of views, nor a managed-heap
/// word.
bool isMergeable(Type type) {
  if (sim::isSimulationHandleType(type))
    return false;
  SmallVector<sim::ManagedHandleSlot> slots;
  return sim::getManagedHandleSlots(type, slots) && slots.empty();
}

/// Whether `sim::materializeDefaultValue` supports `type`.
bool hasDefaultValue(Type type) {
  return sim::isAggregateType(type) ||
         isa<IntegerType, FloatType, sim::LogicType, sim::TimeType>(type);
}

/// Versions the profitable segments of one body, in program order.
class Versioner {
public:
  Versioner(sim::SimFuncOp function, const LocalUnknownAnalysis &analysis,
            const DesignInputs &inputs)
      : function(function), analysis(analysis), inputs(inputs),
        builder(function.getContext()), dominance(function) {}

  /// Version the segment from `begin` up to, excluding, `end` (null: the end
  /// of the body). Leaves the IR unchanged and fails when a value the copy
  /// must hand to the original cannot reach it.
  bool version(Operation *begin, Operation *end,
               const LocalUnknownAnalysis::Segment &segment) {
    SmallVector<Operation *> checked;
    llvm::SetVector<sim::SimRefAllocOp> hoisted;
    if (!prepare(begin, end, segment, checked, hoisted))
      return false;
    for (sim::SimRefAllocOp allocation : hoisted) {
      Operation *store = hoist(allocation);
      if (allocation == begin)
        begin = store;
    }
    Location loc = begin->getLoc();
    Block *head = begin->getBlock();
    Block *entry = splitBefore(begin);
    builder.setInsertionPointToEnd(head);
    auto enter = cf::BranchOp::create(builder, loc, entry);
    Block *exit = nullptr;
    if (end) {
      exit = end->getBlock();
      if (&exit->front() != end) {
        Block *last = exit;
        exit = splitBefore(end);
        builder.setInsertionPointToEnd(last);
        cf::BranchOp::create(builder, loc, exit);
      }
    }
    llvm::SetVector<Block *> blocks = collectBlocks(entry, exit);
    // Live-outs need a merge point entered only from the two copies.
    if (exit && llvm::any_of(exit->getPredecessors(), [&](Block *pred) {
          return !blocks.contains(pred);
        }))
      exit = dedicatedExit(exit, blocks.getArrayRef());

    // The check: the gated roots, then the slots and live-ins only the copy
    // treats as known.
    builder.setInsertionPoint(enter);
    Value known;
    auto conjoin = [&](Value next) {
      known = known ? arith::AndIOp::create(builder, loc, known, next)
                          .getResult()
                    : next;
    };
    Value activation = function.getBody().front().getArgument(0);
    if (!segment.gate.empty()) {
      SmallVector<Value> references;
      for (uint64_t root : segment.gate)
        references.push_back(sim::SimContextStorageOp::create(
            builder, loc,
            sim::RefType::get(function.getContext(),
                              inputs.storageTypes.lookup(root)),
            activation, root));
      conjoin(schedule::NativeKnownRootsOp::create(
          builder, loc, builder.getI1Type(), references));
    }
    for (sim::SimRefAllocOp allocation : segment.slotChecks) {
      Value slot = slotOf(allocation);
      Value contents = sim::SimRefLoadOp::create(
          builder, loc, cast<sim::RefType>(slot.getType()).getElementType(),
          slot);
      conjoin(schedule::NativeKnownValueOp::create(
          builder, loc, builder.getI1Type(), contents));
    }
    SmallVector<Value> liveIns = checkedLiveIns(blocks.getArrayRef(), segment);
    for (Value liveIn : liveIns)
      conjoin(schedule::NativeKnownValueOp::create(
          builder, loc, builder.getI1Type(), liveIn));

    // The copy uses checked live-ins through their two-state view.
    SmallVector<Value> casts;
    for (Value liveIn : liveIns)
      if (analysis::KnownVersioningAnalysis::isCastable(liveIn.getType()))
        casts.push_back(liveIn);
    Block *castBlock = nullptr;
    IRMapping mapping;
    if (!casts.empty()) {
      castBlock = new Block;
      function.getBody().push_back(castBlock);
      builder.setInsertionPointToEnd(castBlock);
      for (Value liveIn : casts)
        mapping.map(liveIn, detail::castKnown(builder, loc, liveIn));
    }
    SmallVector<Block *> copies = cloneBlocks(blocks.getArrayRef(), mapping);
    if (castBlock) {
      builder.setInsertionPointToEnd(castBlock);
      cf::BranchOp::create(builder, loc, copies.front());
    }
    builder.setInsertionPoint(enter);
    Block *copy = castBlock ? castBlock : copies.front();
    if (!known) {
      cf::BranchOp::create(builder, loc, copy);
    } else {
      auto branch = cf::CondBranchOp::create(builder, loc, known, copy,
                                             ValueRange(), entry, ValueRange());
      branch.setBranchWeights(ArrayRef<int32_t>{likelyWeight, unlikelyWeight});
    }
    enter.erase();
    markKnownAccesses(blocks.getArrayRef(), mapping);
    if (exit)
      mergeLiveOuts(blocks.getArrayRef(), copies, mapping, exit);
    if (!checked.empty())
      transfer(checked, mapping, head, entry, exit);
    transfers += checked.size();
    dominance.invalidate();
    return true;
  }

  /// Checked reads versioned so far, and two-state views inserted.
  unsigned transfers = 0, knownCasts = 0;

private:
  /// The value an exit argument merges, for analysis queries.
  Value original(Value value) const {
    for (auto it = merged.find(value); it != merged.end();
         it = merged.find(value))
      value = it->second;
    return value;
  }

  /// The body operation holding `op`.
  Operation *anchorOf(Operation *op) {
    return function.getBody().findAncestorOpInRegion(*op);
  }
  /// The body block holding `op`'s outermost enclosing operation.
  Block *bodyBlock(Operation *op) {
    Operation *ancestor = anchorOf(op);
    return ancestor ? ancestor->getBlock() : nullptr;
  }
  Block *bodyBlock(Value value) {
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      Block *owner = argument.getOwner();
      return owner->getParent() == &function.getBody()
                 ? owner
                 : bodyBlock(owner->getParentOp());
    }
    return bodyBlock(value.getDefiningOp());
  }

  /// The blocks control reaches from `entry` before `exit`.
  static llvm::SetVector<Block *> collectBlocks(Block *entry, Block *exit) {
    llvm::SetVector<Block *> blocks;
    SmallVector<Block *> pending{entry};
    while (!pending.empty()) {
      Block *block = pending.pop_back_val();
      if (block == exit || !blocks.insert(block))
        continue;
      llvm::append_range(pending, block->getSuccessors());
    }
    return blocks;
  }

  /// Whether `allocation` can move to the entry block, leaving a store of
  /// its initializer: it runs at most once per activation, so one lifetime
  /// serves both copies.
  bool isHoistable(sim::SimRefAllocOp allocation) {
    Block *block = allocation->getBlock();
    return block->getParent() == &function.getBody() &&
           hasDefaultValue(allocation.getType().getElementType()) &&
           llvm::none_of(block->getPredecessors(), [&](Block *predecessor) {
             return block->isReachable(predecessor);
           });
  }

  /// The reference to `allocation`'s storage, which may have moved to the
  /// entry block.
  Value slotOf(sim::SimRefAllocOp allocation) const {
    auto found = hoistedSlots.find(allocation);
    return found != hoistedSlots.end() ? found->second
                                       : allocation.getResult();
  }

  /// Move `allocation` to the entry block; returns the store of its
  /// initializer that takes its place.
  Operation *hoist(sim::SimRefAllocOp allocation) {
    Location loc = allocation.getLoc();
    OpBuilder entry = OpBuilder::atBlockBegin(&function.getBody().front());
    auto hoisted = sim::SimRefAllocOp::create(
        entry, loc, allocation.getType(),
        sim::materializeDefaultValue(entry, loc,
                                     allocation.getType().getElementType()));
    OpBuilder here(allocation);
    auto store = sim::SimRefStoreOp::create(
        here, loc, allocation.getInitialValue(), hoisted);
    allocation.replaceAllUsesWith(hoisted.getResult());
    hoistedSlots[allocation] = hoisted.getResult();
    allocation.erase();
    return store;
  }

  /// Decide whether the segment from `begin` to `end` can be versioned, and
  /// collect its checked reads and the allocations that must move to the
  /// entry. Every reference the segment defines and later code uses needs a
  /// pure producer chain from values defined before the segment; at each
  /// checked read, every value defined before and used after it must reach
  /// the original body from the copy: plain values merge at block
  /// arguments, references are recomputed from pure views.
  bool prepare(Operation *begin, Operation *end,
               const LocalUnknownAnalysis::Segment &segment,
               SmallVectorImpl<Operation *> &checked,
               llvm::SetVector<sim::SimRefAllocOp> &hoisted) {
    // The entry check loads each tested slot before the segment.
    if (!llvm::all_of(segment.slotChecks, [&](sim::SimRefAllocOp slot) {
          return dominance.properlyDominates(slotOf(slot), begin);
        }))
      return false;
    llvm::SmallPtrSet<Operation *, 32> inSegment;
    llvm::SmallPtrSet<Block *, 16> segmentBlocks;
    SmallVector<Operation *> order;
    llvm::SmallPtrSet<Block *, 16> visited{begin->getBlock()};
    SmallVector<Operation *> pending{begin};
    while (!pending.empty()) {
      Operation *first = pending.pop_back_val();
      Block *block = first->getBlock();
      // The head block keeps its arguments.
      if (first == &block->front() && first != begin && first != end)
        segmentBlocks.insert(block);
      bool stopped = false;
      for (Operation *op = first; op; op = op->getNextNode()) {
        if (op == end) {
          stopped = true;
          break;
        }
        inSegment.insert(op);
        order.push_back(op);
        if (analysis.getKind(op) == LocalUnknownAnalysis::ReadKind::Checked)
          checked.push_back(op);
      }
      if (stopped)
        continue;
      for (Block *successor : block->getSuccessors())
        if (visited.insert(successor).second && !successor->empty())
          pending.push_back(&successor->front());
    }
    auto definedInside = [&](Value value) {
      if (Operation *producer = value.getDefiningOp())
        return inSegment.contains(producer);
      Block *owner = cast<BlockArgument>(value).getOwner();
      return owner->getParent() == &function.getBody() &&
             segmentBlocks.contains(owner);
    };
    // `outside`: recompute from values defined before the segment only.
    std::function<bool(Value, bool)> recomputable = [&](Value value,
                                                        bool outside) -> bool {
      if (!definedInside(value))
        return true;
      if (!sim::isSimulationHandleType(value.getType()))
        return !outside && isMergeable(value.getType());
      Operation *producer = value.getDefiningOp();
      if (!producer)
        return false;
      if (auto allocation = dyn_cast<sim::SimRefAllocOp>(producer)) {
        if (!isHoistable(allocation))
          return false;
        hoisted.insert(allocation);
        return true;
      }
      if (producer->getNumRegions() || !isPure(producer))
        return false;
      return llvm::all_of(producer->getOperands(), [&](Value operand) {
        return recomputable(operand, outside);
      });
    };
    for (Operation *op : order)
      for (Value result : op->getResults())
        if (sim::isSimulationHandleType(result.getType()) &&
            llvm::any_of(result.getUsers(),
                         [&](Operation *user) {
                           Operation *ancestor = anchorOf(user);
                           return !ancestor || !inSegment.contains(ancestor);
                         }) &&
            !recomputable(result, /*outside=*/true))
          return false;
    if (checked.empty())
      return true;

    // The blocks each checked read reaches after it.
    SmallVector<llvm::SmallPtrSet<Block *, 16>> after(checked.size());
    for (auto [index, read] : llvm::enumerate(checked)) {
      SmallVector<Block *> worklist(read->getBlock()->getSuccessors());
      while (!worklist.empty()) {
        Block *block = worklist.pop_back_val();
        if (segmentBlocks.contains(block) || block == read->getBlock())
          if (after[index].insert(block).second)
            llvm::append_range(worklist, block->getSuccessors());
      }
    }
    auto crosses = [&](Value value) {
      for (auto [index, read] : llvm::enumerate(checked)) {
        if (!dominance.properlyDominates(value, read))
          continue;
        for (Operation *user : value.getUsers()) {
          Operation *ancestor = anchorOf(user);
          if (!ancestor || !inSegment.contains(ancestor) ||
              after[index].contains(ancestor->getBlock()) ||
              (ancestor->getBlock() == read->getBlock() &&
               read->isBeforeInBlock(ancestor)))
            return true;
        }
      }
      return false;
    };
    auto reaches = [&](Value value) {
      return isMergeable(value.getType()) || !crosses(value) ||
             recomputable(value, /*outside=*/false);
    };
    for (Block *block : segmentBlocks)
      if (!llvm::all_of(block->getArguments(), reaches))
        return false;
    for (Operation *op : order)
      if (!llvm::all_of(op->getResults(), reaches))
        return false;
    return true;
  }

  /// The values the segment uses but does not define that only the copy
  /// proves known (LocalUnknownAnalysis::Segment::liveIns): the check must
  /// test them.
  SmallVector<Value>
  checkedLiveIns(ArrayRef<Block *> blocks,
                 const LocalUnknownAnalysis::Segment &segment) {
    llvm::SmallPtrSet<Block *, 16> inSegment(blocks.begin(), blocks.end());
    llvm::SetVector<Value> liveIns;
    for (Block *block : blocks)
      block->walk([&](Operation *op) {
        for (Value operand : op->getOperands()) {
          Block *owner = bodyBlock(operand);
          if (owner && !inSegment.contains(owner) &&
              segment.liveIns.contains(original(operand)))
            liveIns.insert(operand);
        }
      });
    return liveIns.takeVector();
  }

  /// Gated reads in the copy load only their value planes, whose unknown
  /// plane is then a constant zero for every later operation; known stores
  /// write only theirs. A value the analysis proves known although its
  /// operands may hold X/Z elsewhere in their bits (a field of a partially
  /// assigned aggregate, a bit of a private slot) takes its two-state view,
  /// so later operations see a zero plane as well.
  void markKnownAccesses(ArrayRef<Block *> blocks, IRMapping &mapping) {
    UnitAttr unit = UnitAttr::get(function.getContext());
    SmallVector<Operation *> casts;
    for (Block *block : blocks)
      block->walk([&](Operation *op) {
        if (isa<sim::SimRefLoadOp>(op) && analysis.isGated(op)) {
          schedule::set<schedule::Field::NativeKnownRead>(mapping.lookup(op),
                                                          unit);
          return;
        }
        if (analysis.isKnownStore(op)) {
          schedule::set<schedule::Field::NativeKnownStore>(mapping.lookup(op),
                                                           unit);
          return;
        }
        if (op->getNumResults() != 1 || op->getNumRegions() != 0)
          return;
        Value result = op->getResult(0);
        if (!hasUnknownPlane(result.getType()) ||
            !analysis.isKnownInCopy(result) ||
            !analysis::KnownVersioningAnalysis::isCastable(result.getType()))
          return;
        bool fromUnknown =
            isa<sim::SimRefLoadOp>(op)
                ? analysis.getKind(op) == LocalUnknownAnalysis::ReadKind::Local
                : llvm::any_of(op->getOperands(), [&](Value operand) {
                    return hasUnknownPlane(operand.getType()) &&
                           !analysis.isKnownInCopy(operand);
                  });
        if (fromUnknown)
          casts.push_back(op);
      });
    knownCasts += casts.size();
    for (Operation *op : casts) {
      Operation *copy = mapping.lookup(op);
      Value result = copy->getResult(0);
      builder.setInsertionPointAfter(copy);
      Value cast = detail::castKnown(builder, copy->getLoc(), result);
      Operation *first = cast.getDefiningOp();
      while (first->getOperand(0) != result)
        first = first->getOperand(0).getDefiningOp();
      result.replaceAllUsesExcept(cast, first);
    }
  }

  /// The copy tests each checked read where it loads. On X/Z it continues
  /// in the original body right after the same read: both copies perform
  /// the same effects with the same values up to there (LocalUnknownAnalysis),
  /// so the activation runs every operation once, in order (IEEE 1800-2023
  /// 4.3). Later copy operations use the read's two-state view.
  ///
  ///   copy:  %v = load; %ok = known.value %v; cond_br %ok, ^more, ^leave
  ///   ^leave: br ^rest(...)       original: %v = load; br ^rest(...)
  ///
  /// Values the original defines before such a read and uses after it gain
  /// the copy's counterparts as second definitions. References are
  /// recomputed where they are used; other values merge at block arguments
  /// placed on the iterated dominance frontier of their definitions, as
  /// SSA construction places them.
  void transfer(ArrayRef<Operation *> checked, IRMapping &mapping, Block *head,
                Block *entry, Block *exit) {
    DenseMap<Block *, Operation *> leaves;
    for (Operation *read : checked) {
      Block *block = read->getBlock();
      Block *rest = splitBefore(read->getNextNode());
      builder.setInsertionPointToEnd(block);
      cf::BranchOp::create(builder, read->getLoc(), rest);

      Operation *copy = mapping.lookup(read);
      Value result = copy->getResult(0);
      Block *copyBlock = copy->getBlock();
      Block *more = copyBlock->splitBlock(copy->getNextNode());
      Block *leave = new Block;
      function.getBody().push_back(leave);
      builder.setInsertionPointToEnd(copyBlock);
      Value known = schedule::NativeKnownValueOp::create(
          builder, copy->getLoc(), builder.getI1Type(), result);
      auto branch = cf::CondBranchOp::create(builder, copy->getLoc(), known,
                                             more, ValueRange(), leave,
                                             ValueRange());
      branch.setBranchWeights(ArrayRef<int32_t>{likelyWeight, unlikelyWeight});
      builder.setInsertionPointToEnd(leave);
      cf::BranchOp::create(builder, copy->getLoc(), rest);
      builder.setInsertionPointToStart(more);
      Value cast = detail::castKnown(builder, copy->getLoc(), result);
      // The cast's first operation is the only other use that keeps it.
      SmallPtrSet<Operation *, 4> keep{known.getDefiningOp(), &more->front()};
      result.replaceAllUsesExcept(cast, keep);
      leaves[leave] = read;
    }

    dominance.invalidate();
    llvm::SetVector<Block *> original = collectBlocks(entry, exit);
    auto badUses = [&](Value value, auto &&each) {
      for (OpOperand &use : llvm::make_early_inc_range(value.getUses()))
        if (Operation *anchor = anchorOf(use.getOwner());
            anchor && !dominance.properlyDominates(value, anchor))
          each(use, anchor);
    };
    auto forEachValue = [&](auto &&each) {
      for (Block *block : original) {
        for (BlockArgument argument : block->getArguments())
          each(argument);
        for (Operation &op : *block)
          for (Value result : op.getResults())
            each(result);
      }
    };

    // Recompute references where their definition no longer dominates.
    SmallVector<std::pair<OpOperand *, Operation *>> references;
    forEachValue([&](Value value) {
      if (sim::isSimulationHandleType(value.getType()))
        badUses(value, [&](OpOperand &use, Operation *anchor) {
          references.push_back({&use, anchor});
        });
    });
    DenseMap<Operation *, IRMapping> recomputed;
    for (auto [use, anchor] : references) {
      OpBuilder at(anchor);
      use->set(recompute(
          at, use->get(),
          [&](Value handle) {
            return !dominance.properlyDominates(handle, anchor);
          },
          recomputed[anchor]));
    }

    // Merge the other values.
    SmallVector<Value> values;
    DenseMap<Value, unsigned> indices;
    DenseMap<Operation *, SmallVector<std::pair<OpOperand *, unsigned>, 1>>
        uses;
    forEachValue([&](Value value) {
      if (!isMergeable(value.getType()))
        return;
      badUses(value, [&](OpOperand &use, Operation *anchor) {
        auto [it, inserted] = indices.try_emplace(value, values.size());
        if (inserted)
          values.push_back(value);
        uses[anchor].push_back({&use, it->second});
      });
    });
    if (values.empty())
      return;
    Region &body = function.getBody();
    auto &tree = dominance.getDomTree(&body);
    DenseMap<Block *, SmallVector<unsigned, 4>> merges, defined;
    for (auto [index, value] : llvm::enumerate(values)) {
      llvm::SmallPtrSet<Block *, 16> definitions{bodyBlock(value)};
      if (Value counterpart = mapping.lookupOrNull(value))
        for (auto [leave, read] : leaves)
          if (dominance.properlyDominates(counterpart, leave->getTerminator())) {
            definitions.insert(leave);
            defined[leave].push_back(index);
          }
      // A use the definition no longer dominates lies after a transfer;
      // each path to it passes the definition or one of those transfers.
      llvm::SmallPtrSet<Block *, 16> liveIn;
      SmallVector<Block *> worklist;
      badUses(value, [&](OpOperand &, Operation *anchor) {
        worklist.push_back(anchor->getBlock());
      });
      while (!worklist.empty()) {
        Block *block = worklist.pop_back_val();
        if (!liveIn.insert(block).second)
          continue;
        for (Block *predecessor : block->getPredecessors())
          if (!definitions.contains(predecessor) &&
              (original.contains(predecessor) || leaves.count(predecessor)))
            worklist.push_back(predecessor);
      }
      llvm::IDFCalculatorBase<Block, false> frontier(tree);
      frontier.setLiveInBlocks(liveIn);
      frontier.setDefiningBlocks(definitions);
      SmallVector<Block *> blocks;
      frontier.calculate(blocks);
      for (Block *block : blocks)
        merges[block].push_back(index);
    }

    // Rename along the dominator tree from the segment's head. The exit and
    // everything it dominates use none of these values.
    auto rename = [&](Block *block, SmallVectorImpl<Value> &reaching) {
      if (auto found = merges.find(block); found != merges.end())
        for (unsigned index : found->second)
          reaching[index] = block->addArgument(values[index].getType(),
                                               values[index].getLoc());
      for (BlockArgument argument : block->getArguments())
        if (auto found = indices.find(argument); found != indices.end())
          reaching[found->second] = argument;
      if (auto found = defined.find(block); found != defined.end())
        for (unsigned index : found->second)
          reaching[index] = mapping.lookup(values[index]);
      for (Operation &op : *block) {
        if (auto found = uses.find(&op); found != uses.end())
          for (auto [use, index] : found->second)
            use->set(reaching[index]);
        for (Value result : op.getResults())
          if (auto found = indices.find(result); found != indices.end())
            reaching[found->second] = result;
      }
      auto terminator = dyn_cast<BranchOpInterface>(block->getTerminator());
      if (!terminator)
        return;
      for (BlockOperand &successor : terminator->getBlockOperands())
        if (auto found = merges.find(successor.get()); found != merges.end()) {
          SuccessorOperands operands =
              terminator.getSuccessorOperands(successor.getOperandNumber());
          for (unsigned index : found->second)
            operands.append(reaching[index] ? reaching[index] : values[index]);
        }
    };
    struct Job {
      llvm::DomTreeNodeBase<Block> *node;
      SmallVector<Value> reaching;
    };
    SmallVector<Job> stack;
    stack.push_back({tree.getNode(head), SmallVector<Value>(values.size())});
    while (!stack.empty()) {
      Job job = stack.pop_back_val();
      rename(job.node->getBlock(), job.reaching);
      for (auto *child : job.node->children())
        if (child->getBlock() != exit)
          stack.push_back({child, job.reaching});
    }
  }

  /// Recompute `handle` at the builder's insertion point, cloning the view
  /// producers of every `stale` reference it depends on; other operands keep
  /// their values. `prepare` admitted only pure producer chains.
  static Value recompute(OpBuilder &builder, Value handle,
                         function_ref<bool(Value)> stale, IRMapping &cloned) {
    if (Value existing = cloned.lookupOrNull(handle))
      return existing;
    if (!stale(handle))
      return handle;
    Operation *producer = handle.getDefiningOp();
    for (Value operand : producer->getOperands())
      if (sim::isSimulationHandleType(operand.getType()))
        cloned.map(operand, recompute(builder, operand, stale, cloned));
    Operation *copy = builder.clone(*producer, cloned);
    for (auto [from, to] :
         llvm::zip_equal(producer->getResults(), copy->getResults()))
      cloned.map(from, to);
    return cloned.lookup(handle);
  }

  /// Split `op`'s block before `op`. The tail goes to the end of the body,
  /// so every existing block keeps its position.
  Block *splitBefore(Operation *op) {
    Block *tail = op->getBlock()->splitBlock(op);
    Region &body = function.getBody();
    body.getBlocks().splice(body.end(), body.getBlocks(), tail->getIterator());
    return tail;
  }

  /// Route the segment's edges into `exit` through a block entered only from
  /// the segment.
  Block *dedicatedExit(Block *exit, ArrayRef<Block *> blocks) {
    Block *dedicated = new Block;
    function.getBody().push_back(dedicated);
    SmallVector<Value> arguments;
    for (BlockArgument argument : exit->getArguments())
      arguments.push_back(
          dedicated->addArgument(argument.getType(), argument.getLoc()));
    OpBuilder exitBuilder = OpBuilder::atBlockEnd(dedicated);
    cf::BranchOp::create(exitBuilder, exit->front().getLoc(), exit, arguments);
    for (Block *block : blocks) {
      Operation *terminator = block->getTerminator();
      for (unsigned index = 0; index < terminator->getNumSuccessors(); ++index)
        if (terminator->getSuccessor(index) == exit)
          terminator->setSuccessor(dedicated, index);
    }
    return dedicated;
  }

  /// Copy `blocks`; a value used before its definition is copied (a block
  /// listed before one that dominates it) is remapped afterwards.
  SmallVector<Block *> cloneBlocks(ArrayRef<Block *> blocks,
                                   IRMapping &mapping) {
    SmallVector<Block *> copies;
    for (Block *block : blocks) {
      Block *copy = new Block;
      function.getBody().push_back(copy);
      mapping.map(block, copy);
      for (BlockArgument argument : block->getArguments())
        mapping.map(argument,
                    copy->addArgument(argument.getType(), argument.getLoc()));
      copies.push_back(copy);
    }
    for (auto [block, copy] : llvm::zip_equal(blocks, copies)) {
      builder.setInsertionPointToEnd(copy);
      for (Operation &op : *block)
        builder.clone(op, mapping);
    }
    for (Block *copy : copies)
      copy->walk([&](Operation *op) {
        for (OpOperand &operand : op->getOpOperands())
          if (Value mapped = mapping.lookupOrNull(operand.get()))
            operand.set(mapped);
      });
    return copies;
  }

  /// Merge the values the segment defines and later code uses at its exit.
  /// References are recomputed after it instead: their provenance must stay
  /// a static chain of views for state layout, NBA planning and alias proofs.
  void mergeLiveOuts(ArrayRef<Block *> blocks, ArrayRef<Block *> copies,
                     IRMapping &mapping, Block *exit) {
    llvm::SmallPtrSet<Block *, 16> inSegment(blocks.begin(), blocks.end());
    llvm::SmallPtrSet<Block *, 16> inCopy(copies.begin(), copies.end());
    llvm::SetVector<Value> values;
    SmallVector<OpOperand *> uses;
    OpBuilder exitBuilder = OpBuilder::atBlockBegin(exit);
    IRMapping rematerialized;
    auto definedInSegment = [&](Value handle) {
      Operation *producer = handle.getDefiningOp();
      return producer && inSegment.contains(producer->getBlock());
    };
    auto collect = [&](Value value) {
      for (OpOperand &use : llvm::make_early_inc_range(value.getUses())) {
        Block *user = bodyBlock(use.getOwner());
        if (!user || inSegment.contains(user) || inCopy.contains(user))
          continue;
        if (sim::isSimulationHandleType(value.getType())) {
          use.set(recompute(exitBuilder, value, definedInSegment,
                            rematerialized));
          continue;
        }
        values.insert(value);
        uses.push_back(&use);
      }
    };
    for (Block *block : blocks) {
      for (BlockArgument argument : block->getArguments())
        collect(argument);
      for (Operation &op : *block)
        for (Value result : op.getResults())
          collect(result);
    }
    if (values.empty())
      return;
    DenseMap<Value, Value> mergedValues;
    for (Value value : values) {
      Value argument = exit->addArgument(value.getType(), value.getLoc());
      mergedValues[value] = argument;
      merged[argument] = value;
    }
    // A block with several edges into `exit` is listed once per edge.
    llvm::SetVector<Block *> preds(exit->pred_begin(), exit->pred_end());
    for (Block *pred : preds) {
      auto terminator = cast<BranchOpInterface>(pred->getTerminator());
      bool fromOriginal = inSegment.contains(pred);
      for (unsigned index = 0; index < terminator->getNumSuccessors();
           ++index) {
        if (terminator->getSuccessor(index) != exit)
          continue;
        SuccessorOperands operands = terminator.getSuccessorOperands(index);
        for (Value value : values)
          operands.append(fromOriginal ? value : mapping.lookup(value));
      }
    }
    for (OpOperand *use : uses)
      use->set(mergedValues.lookup(use->get()));
  }

  sim::SimFuncOp function;
  const LocalUnknownAnalysis &analysis;
  const DesignInputs &inputs;
  OpBuilder builder;
  /// Exit arguments merging a segment's copies, mapped to the merged value.
  DenseMap<Value, Value> merged;
  /// Allocations moved to the entry block, by their original operation.
  DenseMap<Operation *, Value> hoistedSlots;
  /// Valid until the next rewrite.
  DominanceInfo dominance;
};

/// Versions one function. Each worker reads and rewrites only its own body.
class VersionKnownActivationsFunctionPass final
    : public PassWrapper<VersionKnownActivationsFunctionPass,
                         OperationPass<sim::SimFuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      VersionKnownActivationsFunctionPass)
  VersionKnownActivationsFunctionPass(
      std::shared_ptr<const DesignInputs> inputs, uint64_t minimumBenefit,
      uint64_t segmentSize, bool variants, bool checkedReads, bool diagnostics)
      : inputs(std::move(inputs)), minimumBenefit(minimumBenefit),
        segmentSize(segmentSize), variants(variants),
        checkedReads(checkedReads), diagnostics(diagnostics) {}
  StringRef getArgument() const final {
    return "schedule-version-known-activations-function";
  }

  void runOnOperation() override {
    sim::SimFuncOp function = getOperation();
    // Repeating nonsuspending bodies only, as for region versioning. A source
    // with a two-state variant runs only while its inputs hold X/Z, and a
    // path predicate only classifies an activation.
    if (function.isExternal() ||
        function.getEntryKind() != sim::EntryKind::Function ||
        schedule::has<schedule::Field::EvalPathKnownPredicate>(function) ||
        schedule::has<schedule::metadata::evalTwoStateVariant>(function) ||
        schedule::has<schedule::Field::NativeKnownActivation>(function) ||
        (!variants &&
         schedule::has<schedule::Field::EvalInductiveTwoState>(function)))
      return markAllAnalysesPreserved();
    Block &entry = function.getBody().front();
    // Suspension would carry a copy across activations.
    if (entry.getNumArguments() == 0 ||
        !isa<sim::ContextType>(entry.getArgument(0).getType()) ||
        function
            .walk([](Operation *op) {
              return sim::isSuspensionOp(op) ? WalkResult::interrupt()
                                             : WalkResult::advance();
            })
            .wasInterrupted())
      return markAllAnalysesPreserved();
    Segments segments = cut(function, segmentSize);
    analysis::HandleDataflowResult handles = inputs->handles.analyze(function);
    auto provenKnown = [&](Operation *read) {
      return inputs->provesKnown(function, handles.facts, read);
    };
    auto staysUnknown = [&](uint64_t descriptor, uint64_t low, uint64_t high) {
      return inputs->staysUnknown(descriptor, low, high);
    };
    LocalUnknownAnalysis analysis(
        function, handles,
        LocalUnknownAnalysis::Options{&segments.segmentation, &inputs->callees,
                                      false, provenKnown, checkedReads,
                                      staysUnknown});
    // A copy must save clearly more than its checks cost: about three
    // instructions per tested summary word, slot or live-in and two per
    // tested read. A segment's roots are numbered together, so a word covers
    // several of them.
    auto profitable = [&](const LocalUnknownAnalysis::Segment &segment) {
      uint64_t words = (segment.gate.size() + 7) / 8;
      uint64_t check = 3 * words + 2 * segment.checks +
                       3 * (segment.slotChecks.size() +
                            segment.liveIns.size()) +
                       4;
      return !segment.untestable && (!segment.gate.empty() || segment.checks) &&
             segment.benefit >= minimumBenefit &&
             segment.benefit > 2 * check;
    };
    ArrayRef<LocalUnknownAnalysis::Segment> results = analysis.getSegments();
    unsigned versioned = 0;
    uint64_t benefit = 0, roots = 0;
    Versioner versioner(function, analysis, *inputs);
    for (auto [index, segment] : llvm::enumerate(results)) {
      if (!profitable(segment))
        continue;
      Operation *end = index + 1 < segments.begins.size()
                           ? segments.begins[index + 1]
                           : nullptr;
      if (!versioner.version(segments.begins[index], end, segment))
        continue;
      ++versioned;
      benefit += segment.benefit;
      roots += segment.gate.size();
    }
    if (diagnostics && versioned) {
      std::string text;
      llvm::raw_string_ostream os(text);
      os << "obelisk known activation: " << function.getSymName()
         << " segments=" << versioned << '/' << results.size()
         << " checks=" << versioner.transfers << " benefit=" << benefit
         << " gate-roots=" << roots << " casts=" << versioner.knownCasts
         << '\n';
      llvm::errs() << text;
    }
    if (!versioned)
      return markAllAnalysesPreserved();
    MLIRContext *context = function.getContext();
    schedule::set<schedule::Field::NativeKnownVersioned>(
        function, UnitAttr::get(context));
    schedule::set<schedule::Field::NativeKnownActivation>(
        function, UnitAttr::get(context));
  }

private:
  std::shared_ptr<const DesignInputs> inputs;
  uint64_t minimumBenefit;
  uint64_t segmentSize;
  bool variants;
  bool checkedReads;
  bool diagnostics;
};

class VersionKnownActivationsPass final
    : public impl::VersionKnownActivationsPassBase<
          VersionKnownActivationsPass> {
public:
  using Base::Base;

  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto level = native::get<native::OptimizationLevel>(module);
    if (!level || level.getInt() < 1)
      return markAllAnalysesPreserved();
    bool diagnostics = native::has<native::DebugTiming>(module);
    for (auto design : module.getOps<sim::SimDesignOp>()) {
      if (design.getBody().empty())
        continue;
      auto inputs = std::make_shared<const DesignInputs>(design, variants);
      OpPassManager designs(sim::SimDesignOp::getOperationName());
      designs.addNestedPass<sim::SimFuncOp>(
          std::make_unique<VersionKnownActivationsFunctionPass>(
              inputs, minimumBenefit, segmentSize, variants, checkedReads,
              diagnostics));
      if (failed(runPipeline(designs, design)))
        return signalPassFailure();
    }
    // Only nonsuspending bodies change, by adding equivalent code paths.
    // Actor identity, capture ABI, canonical storage, frames and the frozen
    // bytecode stay valid.
    markAnalysesPreserved<detail::NativePipelineAnalysis>();
  }
};
} // namespace
} // namespace obelisk
