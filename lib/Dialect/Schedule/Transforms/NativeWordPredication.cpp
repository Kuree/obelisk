#include "NativeWordPredication.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/RegionGraphTraits.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "obelisk/Analysis/NativeWordAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SetVector.h"
#include <functional>

using namespace mlir;
namespace obelisk {
namespace {
// Push dispatch comparisons through the integer selects created by
// if-conversion. This exposes the original boolean conditions, allowing the
// normal arith canonicalizations to remove widened 0/1 dispatch flags.
struct CompareSelectedCase : OpRewritePattern<arith::CmpIOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(arith::CmpIOp op,
                                PatternRewriter &rewriter) const override {
    APInt constant;
    if (!matchPattern(op.getRhs(), m_ConstantInt(&constant)))
      return failure();
    Operation *extension = op.getLhs().getDefiningOp();
    if (extension && isa<arith::ExtUIOp, arith::ExtSIOp>(extension) &&
        (op.getPredicate() == arith::CmpIPredicate::eq ||
         op.getPredicate() == arith::CmpIPredicate::ne)) {
      Value input = extension->getOperand(0);
      unsigned width = cast<IntegerType>(input.getType()).getWidth();
      APInt narrow = constant.trunc(width);
      APInt extended = isa<arith::ExtUIOp>(extension)
                           ? narrow.zext(constant.getBitWidth())
                           : narrow.sext(constant.getBitWidth());
      if (extended != constant) {
        rewriter.replaceOpWithNewOp<arith::ConstantIntOp>(
            op, op.getPredicate() == arith::CmpIPredicate::ne, 1);
      } else {
        Value rhs = arith::ConstantOp::create(
            rewriter, op.getLoc(), input.getType(),
            rewriter.getIntegerAttr(input.getType(), narrow));
        rewriter.replaceOpWithNewOp<arith::CmpIOp>(op, op.getPredicate(), input,
                                                   rhs);
      }
      return success();
    }
    auto select = op.getLhs().getDefiningOp<arith::SelectOp>();
    if (!select)
      return failure();
    Value yes = arith::CmpIOp::create(rewriter, op.getLoc(), op.getPredicate(),
                                      select.getTrueValue(), op.getRhs());
    Value no = arith::CmpIOp::create(rewriter, op.getLoc(), op.getPredicate(),
                                     select.getFalseValue(), op.getRhs());
    rewriter.replaceOpWithNewOp<arith::SelectOp>(op, select.getCondition(), yes,
                                                 no);
    return success();
  }
};

/// Select between two values of a branch-carried type: integers directly,
/// four-state values with a known condition, and packed aggregates through
/// their flattened scalar.
Value selectValue(OpBuilder &builder, Location loc, Value condition, Value a,
                  Value b) {
  if (a == b)
    return a;
  Type original = a.getType();
  if (sim::getPackedWidth(original) &&
      !isa<IntegerType, sim::LogicType>(original)) {
    Type scalar = sim::getPackedScalarType(original);
    a = sim::SimPackedFlattenOp::create(builder, loc, scalar, a);
    b = sim::SimPackedFlattenOp::create(builder, loc, scalar, b);
  }
  Value value;
  if (isa<IntegerType>(a.getType()))
    value = arith::SelectOp::create(builder, loc, condition, a, b);
  else if (isa<sim::LogicType>(a.getType())) {
    Value known = sim::SimLogicFromBitsOp::create(
        builder, loc, sim::LogicType::get(builder.getContext(), 1), condition);
    value = sim::SimLogicMuxOp::create(builder, loc, a.getType(), known, a, b);
  }
  if (value && original != value.getType())
    value = sim::SimPackedUnflattenOp::create(builder, loc, original, value);
  return value;
}

bool isTrue(Value predicate) {
  APInt value;
  return matchPattern(predicate, m_ConstantInt(&value)) && value.isOne();
}

/// Rewrite every store in `blocks` to keep its previous value unless
/// `predicate` holds. Predicated stores always execute, so their addresses
/// are speculated like the loads; the caller proves them in bounds.
bool maskStores(OpBuilder &builder, Location loc, ArrayRef<Block *> blocks,
                Value predicate) {
  if (isTrue(predicate))
    return true;
  for (Block *block : blocks)
    for (Operation &op : *block) {
      auto store = dyn_cast<sim::SimRefStoreOp>(op);
      if (!store)
        continue;
      builder.setInsertionPoint(store);
      Value previous = sim::SimRefLoadOp::create(
          builder, loc, store.getValue().getType(), store.getReference());
      Value value =
          selectValue(builder, loc, predicate, store.getValue(), previous);
      if (!value)
        return false;
      store.getValueMutable().assign(value);
    }
  return true;
}

/// Rewrite a switch as a chain of two-way branches: each link tests one case
/// value and falls through to the next, the last one to the default. Returns
/// the new blocks.
SmallVector<Block *> lowerSwitch(cf::SwitchOp choice) {
  OpBuilder builder(choice);
  Location loc = choice.getLoc();
  Region &region = *choice->getBlock()->getParent();
  Value flag = choice.getFlag();
  Block *defaultDest = choice.getDefaultDestination();
  SmallVector<Value> defaultOperands(choice.getDefaultOperands());
  SmallVector<APInt> values;
  if (auto cases = choice.getCaseValues())
    values = llvm::to_vector(cases->getValues<APInt>());
  SmallVector<Block *> created;
  Block *current = choice->getBlock();
  if (values.empty())
    cf::BranchOp::create(builder, loc, defaultDest, defaultOperands);
  for (auto [i, value] : llvm::enumerate(values)) {
    Value constant = arith::ConstantOp::create(
        builder, loc, flag.getType(),
        builder.getIntegerAttr(flag.getType(), value));
    Value equal = arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::eq,
                                        flag, constant);
    SmallVector<Value> caseOperands(choice.getCaseOperands(i));
    if (i + 1 == values.size()) {
      cf::CondBranchOp::create(builder, loc, equal,
                               choice.getCaseDestinations()[i], caseOperands,
                               defaultDest, defaultOperands);
      break;
    }
    auto *next = new Block;
    region.getBlocks().insert(std::next(current->getIterator()), next);
    created.push_back(next);
    cf::CondBranchOp::create(builder, loc, equal,
                             choice.getCaseDestinations()[i], caseOperands,
                             next, ValueRange{});
    builder.setInsertionPointToEnd(next);
    current = next;
  }
  choice->erase();
  return created;
}

/// If-convert the acyclic part of one loop iteration. `members` are the loop's
/// blocks other than `header`; `header` ends in `condition`, whose true edge
/// enters the iteration. Inner loops are converted first and then executed as
/// units: their stores are masked by the predicate of the enclosing path, so
/// an inactive inner loop runs to completion without changing state.
/// Afterwards the iteration is straight-line code apart from inner loops.
bool convertIteration(Block *header, cf::CondBranchOp condition,
                      llvm::SetVector<Block *> members) {
  Region &region = *header->getParent();
  Location loc = condition.getLoc();
  OpBuilder builder(condition.getContext());

  // Units of the iteration: plain blocks and inner loops (nontrivial SCCs of
  // the iteration subgraph). Inner loops are converted bottom-up.
  auto innerLoops = [&]() {
    DenseMap<Block *, unsigned> index;
    for (auto [i, block] : llvm::enumerate(members))
      index[block] = i;
    SmallVector<SmallVector<Block *>> loops;
    // Tarjan over the subgraph induced by `members`.
    SmallVector<unsigned> order(members.size(), ~0u), low(members.size());
    SmallVector<bool> onStack(members.size());
    SmallVector<unsigned> stack;
    unsigned counter = 0;
    std::function<void(unsigned)> visit = [&](unsigned v) {
      order[v] = low[v] = counter++;
      stack.push_back(v);
      onStack[v] = true;
      for (Block *next : members[v]->getSuccessors()) {
        auto found = index.find(next);
        if (found == index.end())
          continue;
        unsigned w = found->second;
        if (order[w] == ~0u) {
          visit(w);
          low[v] = std::min(low[v], low[w]);
        } else if (onStack[w])
          low[v] = std::min(low[v], order[w]);
      }
      if (low[v] != order[v])
        return;
      SmallVector<Block *> component;
      unsigned w;
      do {
        w = stack.pop_back_val();
        onStack[w] = false;
        component.push_back(members[w]);
      } while (w != v);
      bool cyclic = component.size() > 1 ||
                    llvm::is_contained(component.front()->getSuccessors(),
                                       component.front());
      if (cyclic)
        loops.push_back(std::move(component));
    };
    for (unsigned v = 0; v < members.size(); ++v)
      if (order[v] == ~0u)
        visit(v);
    return loops;
  };
  struct Inner {
    Block *header;
    cf::CondBranchOp condition;
    llvm::SetVector<Block *> blocks;
  };
  auto describe = [&](ArrayRef<Block *> component) -> std::optional<Inner> {
    llvm::SetVector<Block *> blocks(component.begin(), component.end());
    Block *innerHeader = nullptr;
    for (Block *block : component)
      for (Block *pred : block->getPredecessors())
        if (!blocks.contains(pred)) {
          if (innerHeader && innerHeader != block)
            return std::nullopt;
          innerHeader = block;
        }
    if (!innerHeader)
      return std::nullopt;
    auto innerCondition =
        dyn_cast<cf::CondBranchOp>(innerHeader->getTerminator());
    if (!innerCondition || !blocks.contains(innerCondition.getTrueDest()) ||
        blocks.contains(innerCondition.getFalseDest()))
      return std::nullopt;
    for (Block *block : component)
      if (block != innerHeader)
        for (Block *next : block->getSuccessors())
          if (!blocks.contains(next))
            return std::nullopt;
    blocks.remove(innerHeader);
    return Inner{innerHeader, innerCondition, std::move(blocks)};
  };
  for (const auto &component : innerLoops()) {
    auto inner = describe(component);
    if (!inner ||
        !convertIteration(inner->header, inner->condition, inner->blocks))
      return false;
  }
  // Conversion replaced inner iterations; rebuild this iteration's units.
  members.clear();
  {
    SmallVector<Block *> pending{condition.getTrueDest()};
    while (!pending.empty()) {
      Block *block = pending.pop_back_val();
      if (block == header || !members.insert(block))
        continue;
      llvm::append_range(pending, block->getSuccessors());
    }
  }
  DenseMap<Block *, unsigned> unitOf;
  struct Unit {
    Block *entry;
    SmallVector<Block *> blocks;
    std::optional<Inner> loop;
  };
  SmallVector<Unit> units;
  DenseSet<Block *> looped;
  for (const auto &component : innerLoops()) {
    auto inner = describe(component);
    if (!inner)
      return false;
    unsigned id = units.size();
    units.push_back({inner->header, component, inner});
    for (Block *block : component) {
      unitOf[block] = id;
      looped.insert(block);
    }
  }
  for (Block *block : members)
    if (!looped.contains(block)) {
      unitOf[block] = units.size();
      units.push_back({block, {block}, std::nullopt});
    }
  // Unit-level edges: plain blocks branch to units or back to `header`; an
  // inner loop leaves only through its header's false edge.
  Block *latch = nullptr;
  auto exits = [&](Unit &unit) {
    SmallVector<std::pair<Block *, OperandRange>> result;
    if (unit.loop) {
      cf::CondBranchOp loopCondition = unit.loop->condition;
      result.push_back(
          {loopCondition.getFalseDest(), loopCondition.getFalseDestOperands()});
      return result;
    }
    auto *term = unit.entry->getTerminator();
    if (auto branch = dyn_cast<cf::BranchOp>(term))
      result.push_back({branch.getDest(), branch.getDestOperands()});
    else if (auto branch = dyn_cast<cf::CondBranchOp>(term)) {
      result.push_back({branch.getTrueDest(), branch.getTrueDestOperands()});
      result.push_back({branch.getFalseDest(), branch.getFalseDestOperands()});
    } else if (auto branch = dyn_cast<cf::SwitchOp>(term)) {
      result.push_back(
          {branch.getDefaultDestination(), branch.getDefaultOperands()});
      for (unsigned i = 0, e = branch.getCaseDestinations().size(); i != e; ++i)
        result.push_back(
            {branch.getCaseDestinations()[i], branch.getCaseOperands(i)});
    }
    return result;
  };
  SmallVector<unsigned> incoming(units.size(), 0);
  for (Unit &unit : units) {
    if (!unit.loop && !isa<cf::BranchOp, cf::CondBranchOp, cf::SwitchOp>(
                          unit.entry->getTerminator()))
      return false;
    for (auto [next, operands] : exits(unit)) {
      if (next == header) {
        if (unit.loop || latch)
          return false;
        latch = unit.entry;
        continue;
      }
      if (!unitOf.contains(next))
        return false;
      ++incoming[unitOf[next]];
    }
  }
  Block *start = condition.getTrueDest();
  if (!latch || !isa<cf::BranchOp>(latch->getTerminator()) ||
      !unitOf.contains(start) || incoming[unitOf[start]])
    return false;
  SmallVector<unsigned> order{unitOf[start]};
  for (size_t i = 0; i < order.size(); ++i)
    for (auto [next, operands] : exits(units[order[i]]))
      if (next != header && !--incoming[unitOf[next]])
        order.push_back(unitOf[next]);
  if (order.size() != units.size())
    return false;

  // Emit the iteration as a chain of blocks before the loop exit.
  Block *exit = condition.getFalseDest();
  auto *flat = new Block;
  region.getBlocks().insert(exit->getIterator(), flat);
  Block *current = flat;
  builder.setInsertionPointToEnd(current);
  Value yes = arith::ConstantIntOp::create(builder, loc, 1, 1);
  struct Edge {
    Value predicate;
    SmallVector<Value> operands;
  };
  DenseMap<Block *, SmallVector<Edge>> edges;
  edges[start].push_back(
      {yes, llvm::to_vector(condition.getTrueDestOperands())});
  SmallVector<Value> latchOperands;
  SmallVector<Block *> erased;
  for (unsigned id : order) {
    Unit &unit = units[id];
    builder.setInsertionPointToEnd(current);
    auto &in = edges[unit.entry];
    Value active = in.front().predicate;
    for (const Edge &edge : ArrayRef(in).drop_front())
      active = arith::OrIOp::create(builder, loc, active, edge.predicate);
    if (llvm::any_of(in,
                     [&](const Edge &edge) { return isTrue(edge.predicate); }))
      active = yes;
    SmallVector<Value> arguments;
    for (unsigned i = 0, e = unit.entry->getNumArguments(); i != e; ++i) {
      Value value = in.front().operands[i];
      for (const Edge &edge : ArrayRef(in).drop_front()) {
        value =
            selectValue(builder, loc, edge.predicate, edge.operands[i], value);
        if (!value)
          return false;
      }
      arguments.push_back(value);
    }
    if (unit.loop) {
      // Enter the inner loop on every path; its stores keep their values
      // unless this unit's predicate holds.
      if (!maskStores(builder, loc, unit.blocks, active))
        return false;
      builder.setInsertionPointToEnd(current);
      cf::BranchOp::create(builder, loc, unit.entry, arguments);
      auto *after = new Block;
      region.getBlocks().insert(exit->getIterator(), after);
      Block *target = unit.loop->condition.getFalseDest();
      SmallVector<Value> operands(unit.loop->condition.getFalseDestOperands());
      unit.loop->condition.getFalseDestOperandsMutable().clear();
      unit.loop->condition->setSuccessor(after, 1);
      edges[target].push_back({active, std::move(operands)});
      current = after;
      continue;
    }
    Block *block = unit.entry;
    for (auto [argument, value] :
         llvm::zip_equal(block->getArguments(), arguments))
      argument.replaceAllUsesWith(value);
    if (!maskStores(builder, loc, {block}, active))
      return false;
    for (Operation &op :
         llvm::make_early_inc_range(block->without_terminator()))
      op.moveBefore(current, current->end());
    builder.setInsertionPointToEnd(current);
    Operation *term = block->getTerminator();
    if (auto branch = dyn_cast<cf::BranchOp>(term)) {
      if (branch.getDest() == header)
        latchOperands = llvm::to_vector(branch.getDestOperands());
      else
        edges[branch.getDest()].push_back(
            {active, llvm::to_vector(branch.getDestOperands())});
    } else if (auto choice = dyn_cast<cf::SwitchOp>(term)) {
      // Each case edge is taken when the flag equals its value; the default
      // edge when it equals none of them.
      Value flag = choice.getFlag();
      Value none = active;
      auto guard = [&](Value predicate) {
        return isTrue(active)
                   ? predicate
                   : arith::AndIOp::create(builder, loc, active, predicate)
                         .getResult();
      };
      if (auto values = choice.getCaseValues())
        for (auto [i, value] : llvm::enumerate(values->getValues<APInt>())) {
          Value constant = arith::ConstantOp::create(
              builder, loc, flag.getType(),
              builder.getIntegerAttr(flag.getType(), value));
          Value equal = arith::CmpIOp::create(
              builder, loc, arith::CmpIPredicate::eq, flag, constant);
          Value differ = arith::CmpIOp::create(
              builder, loc, arith::CmpIPredicate::ne, flag, constant);
          edges[choice.getCaseDestinations()[i]].push_back(
              {guard(equal), llvm::to_vector(choice.getCaseOperands(i))});
          none = arith::AndIOp::create(builder, loc, none, differ);
        }
      edges[choice.getDefaultDestination()].push_back(
          {none, llvm::to_vector(choice.getDefaultOperands())});
    } else {
      auto conditional = cast<cf::CondBranchOp>(term);
      Value taken = conditional.getCondition();
      Value notTaken =
          arith::XOrIOp::create(builder, loc, conditional.getCondition(), yes);
      if (!isTrue(active)) {
        taken = arith::AndIOp::create(builder, loc, active, taken);
        notTaken = arith::AndIOp::create(builder, loc, active, notTaken);
      }
      edges[conditional.getTrueDest()].push_back(
          {taken, llvm::to_vector(conditional.getTrueDestOperands())});
      edges[conditional.getFalseDest()].push_back(
          {notTaken, llvm::to_vector(conditional.getFalseDestOperands())});
    }
    erased.push_back(block);
  }
  builder.setInsertionPointToEnd(current);
  cf::BranchOp::create(builder, loc, header, latchOperands);
  builder.setInsertionPoint(condition);
  cf::CondBranchOp::create(builder, loc, condition.getCondition(), flat,
                           ValueRange{}, exit,
                           condition.getFalseDestOperands());
  condition->erase();
  for (Block *block : erased)
    block->dropAllReferences();
  for (Block *block : erased)
    block->erase();
  return true;
}
} // namespace

bool predicateNativeWordLoop(analysis::BoundedLoop loop) {
  bool conditional = false;
  for (Block *block : loop.blocks) {
    auto escapes = [&](Value value) {
      return llvm::any_of(value.getUsers(), [&](Operation *user) {
        return !loop.members.contains(user->getBlock());
      });
    };
    if (block != loop.header && llvm::any_of(block->getArguments(), escapes))
      return false;
    for (Operation &op : *block) {
      if (op.getNumRegions() ||
          (block != loop.header && llvm::any_of(op.getResults(), escapes)))
        return false;
      if (isa<cf::BranchOp, cf::CondBranchOp, cf::SwitchOp>(op))
        continue;
      if (isa<sim::SimRefLoadOp, sim::SimRefStoreOp>(op))
        continue;
      // Pure integer operations can still be undefined. Exact scalar truth
      // tables establish totality for four-state operations before speculation.
      if (!isMemoryEffectFree(&op) ||
          (!isSpeculatable(&op) && !analysis::computeWordTruthTable(&op)))
        return false;
    }
    if (block == loop.header)
      continue;
    conditional |= block->getNumSuccessors() > 1;
    if (!isa<cf::BranchOp, cf::CondBranchOp, cf::SwitchOp>(
            block->getTerminator()))
      return false;
    for (Block *next : block->getSuccessors())
      if (!loop.members.contains(next))
        return false;
  }
  if (!conditional)
    return false;

  // If-convert the nonsuspending iteration directly on the CFG. The original
  // counted-loop header remains the boundary for the fixed-width induction
  // and memory-independence proofs. This function operates on a disposable
  // candidate; the caller only retains it after those proofs, including every
  // speculated access, pass. Uniform nested loops retain their control; a
  // lane-dependent loop condition still fails the subsequent independence
  // proof.
  // Predication handles two-way branches only; lower switches first.
  llvm::SetVector<Block *> members;
  for (Block *block : loop.blocks)
    if (block != loop.header) {
      members.insert(block);
      if (auto choice = dyn_cast<cf::SwitchOp>(block->getTerminator()))
        members.insert_range(lowerSwitch(choice));
    }
  Operation *function = loop.header->getParentOp();
  if (!convertIteration(loop.header, loop.condition, members))
    return false;
  MLIRContext *context = function->getContext();
  RewritePatternSet simplifyCFG(context);
  simplifyCFG.add<CompareSelectedCase>(context);
  arith::CmpIOp::getCanonicalizationPatterns(simplifyCFG, context);
  arith::SelectOp::getCanonicalizationPatterns(simplifyCFG, context);
  arith::ExtUIOp::getCanonicalizationPatterns(simplifyCFG, context);
  arith::TruncIOp::getCanonicalizationPatterns(simplifyCFG, context);
  arith::AndIOp::getCanonicalizationPatterns(simplifyCFG, context);
  arith::XOrIOp::getCanonicalizationPatterns(simplifyCFG, context);
  cf::BranchOp::getCanonicalizationPatterns(simplifyCFG, context);
  cf::CondBranchOp::getCanonicalizationPatterns(simplifyCFG, context);
  // Keep separately admitted storage alternatives separate outside the loop.
  // Aggressive region cleanup merges identical store blocks into reference
  // phis, undoing native reference specialization elsewhere in a fused body.
  GreedyRewriteConfig config;
  config.setRegionSimplificationLevel(GreedySimplifyRegionLevel::Normal);
  return succeeded(
      applyPatternsGreedily(function, std::move(simplifyCFG), config));
}
} // namespace obelisk
