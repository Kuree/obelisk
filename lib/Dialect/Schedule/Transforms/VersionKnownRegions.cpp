//===- VersionKnownRegions.cpp - Two-state copies of known regions
//---------===//
//
// A region found by KnownVersioningAnalysis is entered through a check of its
// four-state inputs. When they are known, a two-state copy runs: its live-ins
// and checked reads become `from_bits(to_bits(x))`, which is `x` for a known
// value, and its reads load only value planes. Otherwise the original code
// runs unchanged.
//
// A run (acyclic, single entry and exit) checks once:
//
//   head:  ... ; cond_br %known, ^cast, ^run          ^run:  original
//   ^cast: casts ; br ^copy                            ^copy: two-state copy
//   ^run, ^copy -> ^exit(live-outs)
//
// A loop checks at every header visit, so it leaves the four-state copy as
// soon as its carried values become known and returns to it for an iteration
// that reads X/Z:
//
//   preheader, latches -> ^check(args): cond_br %known, ^cast(args), ^header
//   ^cast(args): casts ; br ^header2(casts)
//   two-state latches -> ^recheck(args): cond_br %reads, ^header2, ^header
//
//===----------------------------------------------------------------------===//

#include "../../../Conversion/SimulationToSchedule/NativePipeline.h"
#include "mlir/Analysis/CFGLoopInfo.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/RegionGraphTraits.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Pass/PassManager.h"
#include "obelisk/Analysis/KnownVersioningAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include <functional>
#include <optional>

using namespace mlir;
namespace obelisk {
#define GEN_PASS_DEF_VERSIONKNOWNREGIONSPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"

namespace {
using analysis::KnownRegion;
using analysis::KnownRegionKind;
using analysis::KnownVersioningAnalysis;

// The two-state copy is the expected path once reset clears X/Z.
constexpr int32_t likelyWeight = (1 << 20) - 1;
constexpr int32_t unlikelyWeight = 1;

/// The LRM two-state conversion, exact for a known value (IEEE 1800-2023
/// 6.11.2 maps only X/Z bits to 0).
Value castKnown(OpBuilder &builder, Location loc, Value value) {
  Type type = value.getType();
  if (auto logic = dyn_cast<sim::LogicType>(type)) {
    Value bits = sim::SimLogicToBitsOp::create(
        builder, loc, builder.getIntegerType(logic.getWidth()), value);
    return sim::SimLogicFromBitsOp::create(builder, loc, logic, bits);
  }
  Type scalar = sim::getPackedScalarType(type);
  Value flat = sim::SimPackedFlattenOp::create(builder, loc, scalar, value);
  return sim::SimPackedUnflattenOp::create(builder, loc, type,
                                           castKnown(builder, loc, flat));
}

Value conjoin(OpBuilder &builder, Location loc, Value lhs, Value rhs) {
  return lhs ? arith::AndIOp::create(builder, loc, lhs, rhs).getResult() : rhs;
}

class Versioner {
public:
  explicit Versioner(sim::SimFuncOp function) : function(function) {}

  /// The region's four-state live-ins in the current IR. An earlier region
  /// may have merged an analyzed live-in at its exit, and the region now uses
  /// that merge, which joins the two-state copy with four-state code, so the
  /// merge is checked instead. A merge of a value the facts prove known is
  /// known on both paths and needs no check.
  SmallVector<Value> currentLiveIns(const KnownRegion &region,
                                    ArrayRef<Block *> blocks) {
    llvm::SmallPtrSet<Block *, 16> inRegion(blocks.begin(), blocks.end());
    llvm::DenseSet<Value> analyzed(region.liveIns.begin(),
                                   region.liveIns.end());
    auto isAnalyzed = [&](Value value) {
      for (auto it = merged.find(value); it != merged.end();
           it = merged.find(value))
        value = it->second;
      return analyzed.contains(value);
    };
    llvm::SetVector<Value> liveIns;
    for (Block *block : blocks)
      for (Operation &op : *block)
        for (Value operand : op.getOperands()) {
          Block *owner = isa<BlockArgument>(operand)
                             ? cast<BlockArgument>(operand).getOwner()
                             : operand.getDefiningOp()->getBlock();
          if (!inRegion.contains(owner) && isAnalyzed(operand))
            liveIns.insert(operand);
        }
    return liveIns.takeVector();
  }

  void versionRun(const KnownRegion &region) {
    Operation *begin = region.begin;
    Operation *end = region.end;
    Location loc = begin->getLoc();
    OpBuilder builder(function.getContext());
    Block *head = begin->getBlock();
    Block *entry = splitBefore(begin);
    builder.setInsertionPointToEnd(head);
    auto enter = cf::BranchOp::create(builder, loc, entry);
    Block *exit = end->getBlock();
    if (&exit->front() != end) {
      Block *last = exit;
      exit = splitBefore(end);
      builder.setInsertionPointToEnd(last);
      cf::BranchOp::create(builder, loc, exit);
    }

    // Collect the run, which reaches only `exit`.
    llvm::SetVector<Block *> blocks;
    SmallVector<Block *> pending{entry};
    while (!pending.empty()) {
      Block *block = pending.pop_back_val();
      if (block == exit || !blocks.insert(block))
        continue;
      llvm::append_range(pending, block->getSuccessors());
    }
    // Live-outs need a merge point entered only from the two copies.
    if (llvm::any_of(exit->getPredecessors(),
                     [&](Block *pred) { return !blocks.contains(pred); }))
      exit = dedicatedExit(exit, blocks.getArrayRef());

    // Check, cast and copy.
    SmallVector<Value> liveIns = currentLiveIns(region, blocks.getArrayRef());
    builder.setInsertionPoint(enter);
    Value known = checkInputs(builder, loc, liveIns, region, {});
    Block *cast = new Block;
    function.getBody().push_back(cast);
    IRMapping mapping;
    builder.setInsertionPointToEnd(cast);
    mapLiveIns(builder, loc, liveIns, mapping);
    SmallVector<Block *> copies = cloneBlocks(blocks.getArrayRef(), mapping);
    builder.setInsertionPointToEnd(cast);
    cf::BranchOp::create(builder, loc, copies.front());
    builder.setInsertionPoint(enter);
    auto branch = cf::CondBranchOp::create(builder, loc, known, cast,
                                           ValueRange(), entry, ValueRange());
    branch.setBranchWeights(ArrayRef<int32_t>{likelyWeight, unlikelyWeight});
    enter.erase();
    markKnownCopy(region, mapping);
    mergeLiveOuts(blocks.getArrayRef(), copies, mapping, exit);
  }

  void versionLoop(const KnownRegion &region, ArrayRef<Block *> blocks) {
    Block *header = region.header;
    Location loc = header->getTerminator()->getLoc();
    OpBuilder builder(function.getContext());
    DominanceInfo dominance(function);
    llvm::SmallPtrSet<Block *, 16> inLoop(blocks.begin(), blocks.end());
    SmallVector<Block *> exits;
    for (Block *block : blocks)
      for (Block *successor : block->getSuccessors())
        if (!inLoop.contains(successor) &&
            !llvm::is_contained(exits, successor))
          exits.push_back(successor);
    // Assign each use after the loop to the exit that dominates it before
    // any edge changes.
    DenseMap<OpOperand *, Block *> liveOutExits;
    auto assign = [&](Value value) {
      for (OpOperand &use : value.getUses())
        if (!inLoop.contains(use.getOwner()->getBlock()))
          for (Block *exit : exits)
            if (dominance.dominates(exit, use.getOwner()->getBlock())) {
              liveOutExits[&use] = exit;
              break;
            }
    };
    for (Block *block : blocks) {
      for (BlockArgument argument : block->getArguments())
        assign(argument);
      for (Operation &op : *block)
        for (Value result : op.getResults())
          assign(result);
    }

    auto makeHeaderLike = [&]() {
      Block *block = new Block;
      function.getBody().push_back(block);
      for (BlockArgument argument : header->getArguments())
        block->addArgument(argument.getType(), argument.getLoc());
      return block;
    };
    // Copy the loop while its back edges still target the header, so the
    // copy's back edges target the copied header.
    SmallVector<Block *> entries(header->getPredecessors());
    Block *check = makeHeaderLike();
    Block *cast = makeHeaderLike();
    Block *recheck = region.checkedReads.empty() ? nullptr : makeHeaderLike();
    IRMapping mapping;
    builder.setInsertionPointToEnd(cast);
    SmallVector<Value> castArguments;
    for (BlockArgument argument : cast->getArguments())
      castArguments.push_back(hasUnknownPlane(argument.getType())
                                  ? castKnown(builder, loc, argument)
                                  : Value(argument));
    SmallVector<Value> liveIns = currentLiveIns(region, blocks);
    mapLiveIns(builder, loc, liveIns, mapping);
    SmallVector<Block *> copies = cloneBlocks(blocks, mapping);
    Block *header2 = copies.front();
    // Intercept every entry into the original header with the check.
    for (Block *pred : llvm::make_early_inc_range(entries)) {
      Operation *terminator = pred->getTerminator();
      for (unsigned index = 0; index < terminator->getNumSuccessors(); ++index)
        if (terminator->getSuccessor(index) == header)
          terminator->setSuccessor(check, index);
    }
    builder.setInsertionPointToEnd(cast);
    cf::BranchOp::create(builder, loc, header2, castArguments);
    // The copy's back edges recheck only the next iteration's reads.
    if (recheck)
      for (Block *copy : copies) {
        Operation *terminator = copy->getTerminator();
        for (unsigned index = 0; index < terminator->getNumSuccessors();
             ++index)
          if (terminator->getSuccessor(index) == header2)
            terminator->setSuccessor(recheck, index);
      }

    // Four-state entry: carried values, live-ins and this iteration's reads.
    builder.setInsertionPointToEnd(check);
    SmallVector<Value> carried;
    for (BlockArgument argument : check->getArguments())
      if (hasUnknownPlane(argument.getType()))
        carried.push_back(argument);
    llvm::append_range(carried, liveIns);
    Value known = checkInputs(builder, loc, carried, region,
                              headerArguments(header, check));
    auto toCast = cf::CondBranchOp::create(builder, loc, known, cast,
                                           check->getArguments(), header,
                                           check->getArguments());
    toCast.setBranchWeights(ArrayRef<int32_t>{likelyWeight, unlikelyWeight});
    if (recheck) {
      builder.setInsertionPointToEnd(recheck);
      Value reads = checkInputs(builder, loc, {}, region,
                                headerArguments(header, recheck));
      auto stay = cf::CondBranchOp::create(builder, loc, reads, header2,
                                           recheck->getArguments(), header,
                                           recheck->getArguments());
      stay.setBranchWeights(ArrayRef<int32_t>{likelyWeight, unlikelyWeight});
    }

    markKnownCopy(region, mapping);
    // Merge values leaving the loop at the exit that dominated their uses.
    for (Block *exit : exits) {
      SmallVector<OpOperand *> uses;
      for (auto [use, target] : liveOutExits)
        if (target == exit)
          uses.push_back(use);
      if (uses.empty())
        continue;
      OpBuilder exitBuilder = OpBuilder::atBlockBegin(exit);
      IRMapping rematerialized;
      auto inRegion = [&](Block *block) { return inLoop.contains(block); };
      llvm::SetVector<Value> values;
      SmallVector<OpOperand *> merges;
      for (OpOperand *use : uses) {
        if (sim::isSimulationHandleType(use->get().getType())) {
          use->set(
              rematerialize(exitBuilder, use->get(), inRegion, rematerialized));
          continue;
        }
        values.insert(use->get());
        merges.push_back(use);
      }
      if (values.empty())
        continue;
      DenseMap<Value, Value> mergedValues;
      for (Value value : values) {
        Value argument = exit->addArgument(value.getType(), value.getLoc());
        mergedValues[value] = argument;
        if (hasUnknownPlane(value.getType()))
          merged[argument] = value;
      }
      appendExitOperands(exit, values.getArrayRef(), inLoop, mapping);
      for (OpOperand *use : merges)
        use->set(mergedValues.lookup(use->get()));
    }
  }

private:
  /// A mapping from the loop header's arguments to a check block's.
  static IRMapping headerArguments(Block *header, Block *check) {
    IRMapping mapping;
    for (auto [from, to] :
         llvm::zip_equal(header->getArguments(), check->getArguments()))
      mapping.map(from, to);
    return mapping;
  }

  /// Conjoin the knownness of `values` and of every checked read location,
  /// recomputing each reference through `arguments`.
  Value checkInputs(OpBuilder &builder, Location loc, ArrayRef<Value> values,
                    const KnownRegion &region, IRMapping arguments) {
    Value known;
    for (Value value : values)
      known = conjoin(builder, loc, known,
                      schedule::NativeKnownValueOp::create(
                          builder, loc, builder.getI1Type(), value));
    for (const auto &read : region.checkedReads) {
      for (Operation *op : read.slice)
        if (!arguments.contains(op->getResult(0)))
          builder.clone(*op, arguments);
      Operation *probe = builder.clone(*read.read, arguments);
      // The probe needs only the unknown plane.
      known =
          conjoin(builder, loc, known,
                  schedule::NativeKnownValueOp::create(
                      builder, loc, builder.getI1Type(), probe->getResult(0)));
    }
    if (!known)
      known =
          arith::ConstantOp::create(builder, loc, builder.getBoolAttr(true));
    return known;
  }

  /// Make every castable four-state live-in known inside the copy.
  void mapLiveIns(OpBuilder &builder, Location loc, ArrayRef<Value> liveIns,
                  IRMapping &mapping) {
    for (Value value : liveIns)
      if (KnownVersioningAnalysis::isCastable(value.getType()))
        mapping.map(value, castKnown(builder, loc, value));
  }

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
    OpBuilder builder(function.getContext());
    for (auto [block, copy] : llvm::zip_equal(blocks, copies)) {
      builder.setInsertionPointToEnd(copy);
      for (Operation &op : *block)
        builder.clone(op, mapping);
    }
    return copies;
  }

  /// Reads in the copy observe known locations; selections keep their range
  /// proof. Both become explicitly two-state values.
  void markKnownCopy(const KnownRegion &region, IRMapping &mapping) {
    OpBuilder builder(function.getContext());
    auto castResult = [&](Operation *op) {
      Value result = op->getResult(0);
      builder.setInsertionPointAfter(op);
      Value known = castKnown(builder, op->getLoc(), result);
      // The cast's first operation is the only use that keeps `result`.
      result.replaceAllUsesExcept(known, op->getNextNode());
    };
    auto markRead = [&](Operation *original) {
      Operation *copy = mapping.lookup(original);
      schedule::set<schedule::Field::NativeKnownRead>(
          copy, UnitAttr::get(function.getContext()));
      castResult(copy);
    };
    for (const auto &read : region.checkedReads)
      markRead(read.read);
    for (Operation *read : region.ownedReads)
      markRead(read);
    for (Operation *selection : region.rangeProved)
      castResult(mapping.lookup(selection));
    schedule::set<schedule::Field::NativeKnownVersioned>(
        function, UnitAttr::get(function.getContext()));
  }

  /// Recompute a handle defined in the region from values defined before it.
  /// The analysis admitted only handles with such a pure producer chain.
  static Value rematerialize(OpBuilder &builder, Value handle,
                             function_ref<bool(Block *)> inRegion,
                             IRMapping &cloned) {
    if (Value existing = cloned.lookupOrNull(handle))
      return existing;
    Operation *producer = handle.getDefiningOp();
    if (!producer || !inRegion(producer->getBlock()))
      return handle;
    for (Value operand : producer->getOperands())
      cloned.map(operand, rematerialize(builder, operand, inRegion, cloned));
    Operation *copy = builder.clone(*producer, cloned);
    return copy->getResult(cast<OpResult>(handle).getResultNumber());
  }

  /// Split `op`'s block before `op`. New blocks go to the end of the body, so
  /// every existing block keeps its position, which native compute-graph
  /// fragments use to name blocks.
  Block *splitBefore(Operation *op) {
    Block *tail = op->getBlock()->splitBlock(op);
    Region &body = function.getBody();
    body.getBlocks().splice(body.end(), body.getBlocks(), tail->getIterator());
    return tail;
  }

  /// Route the region's edges into `exit` through a new block entered only
  /// from the region.
  Block *dedicatedExit(Block *exit, ArrayRef<Block *> blocks) {
    Block *dedicated = new Block;
    function.getBody().push_back(dedicated);
    SmallVector<Value> arguments;
    for (BlockArgument argument : exit->getArguments())
      arguments.push_back(
          dedicated->addArgument(argument.getType(), argument.getLoc()));
    OpBuilder builder = OpBuilder::atBlockEnd(dedicated);
    cf::BranchOp::create(builder, exit->front().getLoc(), exit, arguments);
    for (Block *block : blocks) {
      Operation *terminator = block->getTerminator();
      for (unsigned index = 0; index < terminator->getNumSuccessors(); ++index)
        if (terminator->getSuccessor(index) == exit)
          terminator->setSuccessor(dedicated, index);
    }
    return dedicated;
  }

  /// Merge the values a run defines and later code uses at its exit.
  void mergeLiveOuts(ArrayRef<Block *> blocks, ArrayRef<Block *> copies,
                     IRMapping &mapping, Block *exit) {
    llvm::SmallPtrSet<Block *, 16> inRun(blocks.begin(), blocks.end());
    llvm::SmallPtrSet<Block *, 16> inCopy(copies.begin(), copies.end());
    llvm::SetVector<Value> values;
    SmallVector<OpOperand *> uses;
    OpBuilder builder = OpBuilder::atBlockBegin(exit);
    IRMapping rematerialized;
    auto inRegion = [&](Block *block) { return inRun.contains(block); };
    auto collect = [&](Value value) {
      for (OpOperand &use : llvm::make_early_inc_range(value.getUses())) {
        Block *block = use.getOwner()->getBlock();
        if (inRun.contains(block) || inCopy.contains(block))
          continue;
        if (sim::isSimulationHandleType(value.getType())) {
          use.set(rematerialize(builder, value, inRegion, rematerialized));
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
      if (hasUnknownPlane(value.getType()))
        merged[argument] = value;
    }
    appendExitOperands(exit, values.getArrayRef(), inRun, mapping);
    for (OpOperand *use : uses)
      use->set(mergedValues.lookup(use->get()));
  }

  /// Append `values` (original copy) or their clones (two-state copy) to
  /// every edge into `exit`.
  template <typename Set>
  static void appendExitOperands(Block *exit, ArrayRef<Value> values,
                                 const Set &original, IRMapping &mapping) {
    // A block with several edges into `exit` is listed once per edge.
    llvm::SetVector<Block *> preds(exit->pred_begin(), exit->pred_end());
    for (Block *pred : preds) {
      auto branch = cast<BranchOpInterface>(pred->getTerminator());
      bool fromOriginal = original.contains(pred);
      for (unsigned index = 0; index < branch->getNumSuccessors(); ++index) {
        if (branch->getSuccessor(index) != exit)
          continue;
        SuccessorOperands operands = branch.getSuccessorOperands(index);
        for (Value value : values)
          operands.append(fromOriginal ? value : mapping.lookup(value));
      }
    }
  }

  sim::SimFuncOp function;
  /// Exit arguments that merge a region's two copies, mapped to the value
  /// they merge.
  llvm::DenseMap<Value, Value> merged;
};

/// The design's state-domain facts: unconditional and inductive, and the
/// all-roots-known facts of conditional variants when the design has one.
/// Immutable while function workers run; each worker queries only its own
/// values.
struct DesignFacts {
  StateDomainAnalysis inductive;
  std::optional<StateDomainAnalysis> conditional;
};

/// Versions one function. Each worker reads and rewrites only its own body.
class VersionKnownRegionsFunctionPass final
    : public PassWrapper<VersionKnownRegionsFunctionPass,
                         OperationPass<sim::SimFuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(VersionKnownRegionsFunctionPass)
  VersionKnownRegionsFunctionPass(std::shared_ptr<const DesignFacts> facts,
                                  bool diagnostics)
      : facts(std::move(facts)), diagnostics(diagnostics) {}
  StringRef getArgument() const final {
    return "schedule-version-known-regions-function";
  }

  void runOnOperation() override {
    sim::SimFuncOp function = getOperation();
    // Repeating work runs in function bodies: eval bodies, fused kernels and
    // helpers. A process body without suspension runs once, and keeps the
    // block structure its frame and fragments describe. Path predicates run
    // only to classify an activation. A source with a two-state variant runs
    // only while its inputs hold X/Z; its variant carries the steady state and
    // is versioned instead.
    if (function.isExternal() ||
        function.getEntryKind() != sim::EntryKind::Function ||
        schedule::has<schedule::Field::EvalPathKnownPredicate>(function) ||
        schedule::has<schedule::metadata::evalTwoStateVariant>(function))
      return markAllAnalysesPreserved();
    // Values packed lowering already proves two-state need no check and
    // offer no work to remove: the whole-design facts, and for a guarded
    // two-state variant also the facts its inductive roots establish.
    const StateDomainAnalysis &inductive = facts->inductive;
    std::function<bool(Value)> provenKnown = [&](Value value) {
      return inductive.isTwoState(value);
    };
    if (schedule::has<schedule::Field::EvalInductiveTwoState>(function)) {
      const StateDomainAnalysis &guarded =
          schedule::has<schedule::Field::EvalConditionallyTwoState>(function)
              ? *facts->conditional
              : inductive;
      provenKnown = [&guarded](Value value) {
        return guarded.isTwoStateWithInductiveRoots(value);
      };
    }
    // In steady state the design's inductively two-state roots are known; a
    // value four-state even then is expected to carry X/Z at run time.
    auto expectedKnown = [&](Value value) {
      return inductive.isTwoStateWithInductiveRoots(value);
    };
    KnownVersioningAnalysis analysis(function, provenKnown, expectedKnown);
    if (analysis.getRegions().empty())
      return markAllAnalysesPreserved();
    // Loop membership is fixed before any edit; runs never touch loop blocks.
    DenseMap<Block *, SmallVector<Block *>> loopBlocks;
    if (!function.getBody().hasOneBlock()) {
      DominanceInfo dominance(function);
      CFGLoopInfo loops(dominance.getDomTree(&function.getBody()));
      for (CFGLoop *loop : loops.getLoopsInPreorder())
        loopBlocks[loop->getHeader()].assign(loop->getBlocks().begin(),
                                             loop->getBlocks().end());
    }
    if (diagnostics) {
      std::string text;
      llvm::raw_string_ostream os(text);
      for (const KnownRegion &region : analysis.getRegions())
        os << "obelisk known versioning: " << function.getSymName() << " "
           << (region.kind == KnownRegionKind::Loop ? "loop" : "run")
           << " operations=" << region.operations
           << " benefit=" << region.benefit << " check=" << region.checkCost
           << " checked=" << region.checkedReads.size()
           << " live-ins=" << region.liveIns.size() << " "
           << (region.header ? region.header->getTerminator()->getLoc()
                             : region.begin->getLoc())
           << "\n";
      llvm::errs() << text;
    }
    // Adjacent runs share a boundary operation. Version them in program
    // order, so each boundary is split before the following run starts there.
    DenseMap<Block *, unsigned> order;
    {
      unsigned index = 0;
      for (Block *block :
           llvm::ReversePostOrderTraversal<Region *>(&function.getBody()))
        order[block] = index++;
    }
    SmallVector<const KnownRegion *> runs;
    for (const KnownRegion &region : analysis.getRegions())
      if (region.kind == KnownRegionKind::Run && region.end)
        runs.push_back(&region);
    llvm::sort(runs, [&](const KnownRegion *lhs, const KnownRegion *rhs) {
      Block *left = lhs->begin->getBlock(), *right = rhs->begin->getBlock();
      if (left != right)
        return order.lookup(left) < order.lookup(right);
      return lhs->begin->isBeforeInBlock(rhs->begin);
    });
    Versioner versioner(function);
    for (const KnownRegion *region : runs)
      versioner.versionRun(*region);
    for (const KnownRegion &region : analysis.getRegions())
      if (region.kind == KnownRegionKind::Loop)
        versioner.versionLoop(region, loopBlocks.lookup(region.header));
  }

private:
  std::shared_ptr<const DesignFacts> facts;
  bool diagnostics;
};

class VersionKnownRegionsPass final
    : public impl::VersionKnownRegionsPassBase<VersionKnownRegionsPass> {
public:
  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto level = native::get<native::OptimizationLevel>(module);
    if (!level || level.getInt() < 1)
      return markAllAnalysesPreserved();
    // Run on the children, so this module's preserved native pipeline facts
    // are not discarded by the nested function pipeline.
    // Versioning needs the design's facts to predict which inputs are known
    // in steady state, so only functions inside a design are considered.
    bool diagnostics = native::has<native::DebugTiming>(module);
    // Packed lowering proves the copies two-state only where it runs the
    // whole-design solve: within the function limit, or where a guarded
    // variant needs the solve anyway. Versioning keeps to the same designs.
    FailureOr<uint64_t> limit = getNativeStateDomainFunctionLimit(module);
    if (failed(limit))
      return signalPassFailure();
    for (auto design : module.getOps<sim::SimDesignOp>()) {
      if (design.getBody().empty())
        continue;
      uint64_t functions = 0;
      bool guarded = false, conditional = false;
      for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
        if (function.isExternal())
          continue;
        ++functions;
        guarded |=
            schedule::has<schedule::Field::EvalInductiveTwoState>(function);
        conditional |=
            schedule::has<schedule::Field::EvalConditionallyTwoState>(function);
      }
      if (!guarded && !conditional && functions > *limit)
        continue;
      std::shared_ptr<DesignFacts> facts;
      if (conditional) {
        auto proofs = StateDomainAnalysis::computeForSpecialization(design);
        if (failed(proofs))
          return signalPassFailure();
        facts = std::make_shared<DesignFacts>(
            DesignFacts{std::move(proofs->first), std::move(proofs->second)});
      } else {
        auto inductive = StateDomainAnalysis::compute(design);
        if (failed(inductive))
          return signalPassFailure();
        facts = std::make_shared<DesignFacts>(
            DesignFacts{std::move(*inductive), std::nullopt});
      }
      OpPassManager designs(sim::SimDesignOp::getOperationName());
      designs.addNestedPass<sim::SimFuncOp>(
          std::make_unique<VersionKnownRegionsFunctionPass>(facts,
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
