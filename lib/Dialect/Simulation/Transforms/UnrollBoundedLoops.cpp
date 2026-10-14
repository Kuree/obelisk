//===- UnrollBoundedLoops.cpp - Expose bounded combinational CFGs --------===//

#include "obelisk/Analysis/BoundedLoopAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"

#include "mlir/Analysis/CFGLoopInfo.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Transforms/CSE.h"
#include "mlir/Transforms/FoldUtils.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/Mem2Reg.h"
#include "mlir/Transforms/SROA.h"
#include "llvm/ADT/DenseSet.h"

#include <optional>
#include <vector>

using namespace mlir;

namespace obelisk {
#define GEN_PASS_DEF_OBELISKSIMUNROLLBOUNDEDLOOPSPASS
#define GEN_PASS_DEF_OBELISKSIMMARKBOUNDEDLOOPSPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {

using analysis::BoundedLoop;
using analysis::recognizeBoundedLoop;

// The closed-form proof establishes termination; replication additionally
// needs an exact count of at most 64 fixed-width steps. For a dependent inner
// loop, the envelope gives a conservative count for budgeting the whole nest.
std::optional<unsigned> countTrips(const BoundedLoop &loop) {
  APInt value = loop.init;
  unsigned trips = 0;
  while (arith::applyCmpPredicate(loop.predicate, value, loop.limit)) {
    if (trips == 64)
      return std::nullopt;
    ++trips;
    value = loop.isAdd ? value + loop.stride : value - loop.stride;
  }
  return trips;
}

struct LoopNest {
  Block *header;
  uint64_t operations;
  uint64_t construction;
  uint64_t originalCost;
  std::vector<LoopNest> children;
};

std::optional<LoopNest> planNest(CFGLoop *natural, ArrayRef<BoundedLoop> bounds,
                                 const DenseSet<Operation *> &latches,
                                 uint64_t maximumSize) {
  auto loop = recognizeBoundedLoop(natural->getHeader(), latches,
                                   /*forMarking=*/false, bounds);
  if (!loop)
    return std::nullopt;
  auto trips = countTrips(*loop);
  if (!trips)
    return std::nullopt;
  LoopNest nest{loop->header, loop->operations, 0, 0, {}};
  for (Block *block : loop->blocks)
    for (Operation &op : *block)
      nest.originalCost += analysis::getSimulationOperationCost(op);
  // Each replicated header also gets an explicit induction constant.
  uint64_t body = loop->operations + 1;
  for (CFGLoop *child : natural->getSubLoops()) {
    auto inner = planNest(child, bounds, latches, maximumSize);
    if (!inner || inner->operations > body)
      return std::nullopt;
    body -= inner->operations;
    // Outer-first replication initially copies the original child, before
    // expanding it. A zero-trip child can shrink dramatically; budgeting only
    // its final expansion would miss that intermediate construction. Bound
    // both forms, including descendants, before addition/multiplication.
    uint64_t childSize = std::max(inner->operations, inner->construction);
    if (childSize > maximumSize || body > maximumSize - childSize)
      return std::nullopt;
    body += childSize;
    nest.children.push_back(std::move(*inner));
  }
  // Source blocks coexist with their copies until remapping is complete. A
  // conditional entry needs a funnel, and replacing the test briefly retains
  // both terminators. Include those operations in the construction ceiling.
  uint64_t overhead = loop->operations + loop->header->getOperations().size() +
                      1 + static_cast<unsigned>(!loop->entry) + 1;
  if (overhead > maximumSize ||
      (*trips && body > (maximumSize - overhead) / *trips))
    return std::nullopt;
  nest.construction = *trips * body + overhead;
  return nest;
}

SmallVector<IRMapping> replicateLoop(BoundedLoop &loop, unsigned trips) {
  Block *header = loop.header;
  cf::BranchOp entry = loop.entry, latch = loop.latch;
  ArrayRef<Block *> blocks = loop.blocks;
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
  APInt induction = loop.init;
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
    // Make each induction binding explicit before cloning uses. This exposes
    // dependent inner bounds without relying on block layout or CFG cleanup.
    OpBuilder constants(header->getParent()->getContext());
    constants.setInsertionPointToStart(mapping.lookup(header));
    auto constant = arith::ConstantOp::create(
        constants, loop.induction.getLoc(), loop.induction.getType(),
        constants.getIntegerAttr(loop.induction.getType(), induction));
    mapping.map(loop.induction, constant.getResult());
    induction = loop.isAdd ? induction + loop.stride : induction - loop.stride;
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
  return mappings;
}

// Work on a disposable function so a nest is either completely replicated or
// left intact. This also covers dependent bounds that fail to become constants
// after outer replication; a termination envelope alone never removes a test.
bool unrollNest(sim::SimFuncOp function, const LoopNest &nest,
                const IRMapping &mapping) {
  // Fold only expressions whose operands are all constants. General
  // canonicalization can change the recurrence shape (e.g. i + 0) or merge a
  // pending child header. Keep those structures until the whole nest commits.
  OperationFolder folder(function.getContext());
  bool changed;
  do {
    changed = false;
    for (Block &block : function.getBody())
      for (Operation &op : llvm::make_early_inc_range(block))
        if (op.getNumResults() && op.getNumOperands() &&
            llvm::all_of(op.getOperands(), [](Value operand) {
              auto *definition = operand.getDefiningOp();
              return definition &&
                     definition->hasTrait<OpTrait::ConstantLike>();
            }))
          changed |= succeeded(folder.tryToFold(&op));
  } while (changed);
  auto bounds = analysis::analyzeBoundedLoops(function.getBody());
  DenseSet<Operation *> latches;
  for (const auto &bound : bounds)
    latches.insert(bound.latch);
  auto loop = recognizeBoundedLoop(mapping.lookup(nest.header), latches);
  if (!loop || loop->initValue || loop->limitValue)
    return false;
  auto trips = countTrips(*loop);
  if (!trips)
    return false;
  // Save all descendant header mappings before erasing this copy of the nest.
  std::vector<LoopNest> children = nest.children;
  auto remap = [&](LoopNest &node, const auto &self) -> void {
    node.header = mapping.lookup(node.header);
    for (auto &child : node.children)
      self(child, self);
  };
  for (auto &child : children)
    remap(child, remap);
  auto copies = replicateLoop(*loop, *trips);
  for (unsigned iteration = 0; iteration < *trips; ++iteration)
    for (const auto &child : children)
      if (!unrollNest(function, child, copies[iteration]))
        return false;
  return true;
}

// Evaluate the actual simplifications enabled by replication: sparse CFG
// constants, dead guards, declared-index views, scalar replacement, promotion,
// and CSE. Both the baseline and candidate use this same normalization, so
// unrelated pre-existing dead code cannot subsidize an expensive nest.
LogicalResult simplifyCandidate(sim::SimFuncOp function,
                                const FrozenRewritePatternSet &patterns,
                                const DataLayout &layout) {
  {
    DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
    solver.load<dataflow::DeadCodeAnalysis>();
    solver.load<dataflow::SparseConstantPropagation>();
    if (failed(solver.initializeAndRun(function)))
      return failure();
    SmallVector<std::pair<Value, dataflow::ConstantValue>> constants;
    auto record = [&](Value value) {
      auto *state =
          solver.lookupState<dataflow::Lattice<dataflow::ConstantValue>>(value);
      if (state && !state->getValue().isUninitialized() &&
          state->getValue().getConstantValue())
        constants.emplace_back(value, state->getValue());
    };
    function.walk([&](Block *block) {
      for (Value argument : block->getArguments())
        record(argument);
      for (Operation &op : *block)
        if (!op.hasTrait<OpTrait::ConstantLike>())
          for (Value result : op.getResults())
            record(result);
    });
    OperationFolder folder(function.getContext());
    for (auto &[value, fact] : constants) {
      Value constant = folder.getOrCreateConstant(
          value.getParentBlock(), fact.getConstantDialect(),
          fact.getConstantValue(), value.getType());
      if (constant)
        value.replaceAllUsesWith(constant);
    }
  }
  // Match the production canonicalizer. Aggressive block merging can replace
  // distinct static reference accesses with a reference phi, obscuring their
  // roots from scheduling and undoing the purpose of exposing static indices.
  GreedyRewriteConfig config;
  config.setRegionSimplificationLevel(GreedySimplifyRegionLevel::Normal);
  if (failed(applyPatternsGreedily(function, patterns, config)))
    return failure();
  OpBuilder builder(function.getContext());
  SmallVector<DestructurableAllocationOpInterface> aggregates;
  function.walk([&](DestructurableAllocationOpInterface op) {
    aggregates.push_back(op);
  });
  (void)tryToDestructureMemorySlots(aggregates, builder, layout);
  SmallVector<PromotableAllocationOpInterface> slots;
  function.walk(
      [&](PromotableAllocationOpInterface op) { slots.push_back(op); });
  DominanceInfo dominance(function);
  (void)tryToPromoteMemorySlots(slots, builder, layout, dominance);
  if (failed(applyPatternsGreedily(function, patterns, config)))
    return failure();
  IRRewriter rewriter(function.getContext());
  dominance.invalidate();
  eliminateCommonSubExpressions(rewriter, dominance, function);
  return success();
}

class ObeliskSimUnrollBoundedLoopsPass
    : public impl::ObeliskSimUnrollBoundedLoopsPassBase<
          ObeliskSimUnrollBoundedLoopsPass> {
  using Base::Base;
  Statistic unrolledNests{this, "unrolled-nests", "Complete nests replicated"};
  Statistic simplifiedCost{this, "simplified-cost",
                           "Weighted operation cost removed after replication"};
  void runOnOperation() override {
    sim::SimFuncOp function = getOperation();
    if (!maximumLoopSize || function.isExternal() ||
        function.getBody().hasOneBlock() ||
        llvm::is_contained({sim::EntryKind::RootInitializer,
                            sim::EntryKind::Initial, sim::EntryKind::Final,
                            sim::EntryKind::PortInitialize},
                           function.getEntryKind()))
      return;
    RewritePatternSet canonical(function.getContext());
    for (RegisteredOperationName name :
         function.getContext()->getRegisteredOperations())
      if (name.getDialectNamespace() == "arith" ||
          name.getDialectNamespace() == "cf" ||
          name.getDialectNamespace() == "simulation")
        name.getCanonicalizationPatterns(canonical, function.getContext());
    FrozenRewritePatternSet patterns(std::move(canonical));
    DataLayout layout = DataLayout::closest(function);
    DenseSet<Block *> refused;
    while (!function.getBody().hasOneBlock()) {
      auto bounds = analysis::analyzeBoundedLoops(function.getBody());
      DenseSet<Operation *> latches;
      for (const auto &loop : bounds)
        latches.insert(loop.latch);
      DominanceInfo dominance(function);
      CFGLoopInfo loops(dominance.getDomTree(&function.getBody()));
      SmallVector<CFGLoop *> roots;
      auto collect = [&](CFGLoop *loop, const auto &self) -> void {
        // A process resume cycle contains separate activations. Its nested
        // procedural loops are independent decisions; an ordinary unbounded
        // outer loop remains a single refused nest.
        if (llvm::any_of(loop->getBlocks(), [](Block *block) {
              return sim::isSuspensionOp(block->getTerminator());
            })) {
          for (CFGLoop *child : loop->getSubLoops())
            self(child, self);
        } else
          roots.push_back(loop);
      };
      for (CFGLoop *loop : loops)
        collect(loop, collect);
      // Construction has a separate hard ceiling, not a profitability cutoff.
      // It bounds a disposable expansion even if all of it could fold away.
      // Verilator's --unroll-stmts default bounds a nest's unrolled output at
      // 30000 AST nodes; one replicated operation is about one such node.
      constexpr uint64_t maximumConstruction = 30000;
      std::optional<uint64_t> baselineCost;
      bool changed = false;
      for (CFGLoop *root : roots) {
        if (refused.contains(root->getHeader()))
          continue;
        auto nest = planNest(root, bounds, latches, maximumConstruction);
        if (!nest) {
          refused.insert(root->getHeader());
          continue;
        }
        if (!baselineCost) {
          OwningOpRef<sim::SimFuncOp> baseline(
              cast<sim::SimFuncOp>(function->clone()));
          if (failed(simplifyCandidate(*baseline, patterns, layout)))
            return signalPassFailure();
          baselineCost = analysis::getSimulationOperationCost(*baseline);
        }
        IRMapping mapping;
        OwningOpRef<sim::SimFuncOp> copy(
            cast<sim::SimFuncOp>(function->clone(mapping)));
        if (!unrollNest(*copy, *nest, mapping)) {
          refused.insert(root->getHeader());
          continue;
        }
        uint64_t expandedCost = analysis::getSimulationOperationCost(*copy);
        if (failed(simplifyCandidate(*copy, patterns, layout)))
          return signalPassFailure();
        uint64_t reducedCost = analysis::getSimulationOperationCost(*copy);
        uint64_t cost = nest->originalCost;
        if (reducedCost >= *baselineCost)
          cost += reducedCost - *baselineCost;
        else
          cost -= std::min(cost, *baselineCost - reducedCost);
        if (cost > maximumLoopSize) {
          refused.insert(root->getHeader());
          continue;
        }
        ++unrolledNests;
        simplifiedCost += expandedCost - std::min(expandedCost, reducedCost);
        function.getBody().takeBody(copy->getBody());
        // Cleanup may fold a sibling header or expose another dependent bound.
        // All decisions refer to this new revision of the body.
        refused.clear();
        changed = true;
        break;
      }
      if (!changed)
        break;
    }
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
    auto loops = analysis::analyzeBoundedLoops(function.getBody());
    for (const auto &loop : loops) {
      ::obelisk::schedule::set<schedule::metadata::boundedLoopLatch>(loop.latch,
                                                                     marker);
      ::obelisk::schedule::set<schedule::metadata::boundedLoopHeader>(
          loop.condition, marker);
    }
    if (!loops.empty() && function->getParentOfType<ModuleOp>()->hasAttr(
                              "debug.native_timing")) {
      // The diagnostic engine serializes output from concurrent function
      // passes.
      emitRemark(function.getLoc())
          << "obelisk bounded loops: proven=" << loops.size()
          << " actor=" << function.getSymName();
    }
  }
};
} // namespace
} // namespace obelisk
