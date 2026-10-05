#include "NativeWordPredication.h"
#include "mlir/Conversion/ControlFlowToSCF/ControlFlowToSCF.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "obelisk/Analysis/NativeWordAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

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
// Keep join dispatch in the same structured conditional form as source
// branches. The generic lifter can introduce switches even for an all-cond_br
// input CFG. Index switches would introduce target-sized selector arithmetic
// into an otherwise one-bit lane computation.
struct WordControlFlowToSCF : ControlFlowToSCFTransformation {
  FailureOr<Operation *>
  createStructuredBranchRegionOp(OpBuilder &builder, Operation *op,
                                 TypeRange types,
                                 MutableArrayRef<Region> regions) override {
    auto dispatch = dyn_cast<cf::SwitchOp>(op);
    if (!dispatch)
      return ControlFlowToSCFTransformation::createStructuredBranchRegionOp(
          builder, op, types, regions);
    if (!dispatch.getCaseValues() || dispatch.getCaseValues()->empty()) {
      auto execute = scf::ExecuteRegionOp::create(builder, op->getLoc(), types);
      execute.getRegion().takeBody(regions[0]);
      return execute.getOperation();
    }
    OpBuilder::InsertionGuard guard(builder);
    SmallVector<scf::IfOp> branches;
    for (auto [i, value] :
         llvm::enumerate(dispatch.getCaseValues()->getValues<APInt>())) {
      Value constant = arith::ConstantOp::create(
          builder, op->getLoc(), dispatch.getFlag().getType(),
          builder.getIntegerAttr(dispatch.getFlag().getType(), value));
      Value matches =
          arith::CmpIOp::create(builder, op->getLoc(), arith::CmpIPredicate::eq,
                                dispatch.getFlag(), constant);
      auto branch = scf::IfOp::create(builder, op->getLoc(), types, matches);
      branch.getThenRegion().takeBody(regions[i + 1]);
      branches.push_back(branch);
      builder.createBlock(&branch.getElseRegion());
    }
    if (branches.empty())
      return failure();
    branches.back().getElseRegion().takeBody(regions[0]);
    for (unsigned i = 0; i + 1 < branches.size(); ++i) {
      builder.setInsertionPointToEnd(&branches[i].getElseRegion().front());
      scf::YieldOp::create(builder, op->getLoc(), branches[i + 1].getResults());
    }
    return branches.front().getOperation();
  }
  Value getUndefValue(Location loc, OpBuilder &builder, Type type) override {
    // These values only fill SSA slots on paths on which the original CFG
    // never defined or observed them. Choose a defined representative before
    // if-conversion, so speculative four-state operations cannot propagate
    // LLVM poison out of an inactive arm.
    if (auto integer = dyn_cast<IntegerType>(type))
      return arith::ConstantOp::create(builder, loc, type,
                                       builder.getIntegerAttr(type, 0));
    if (sim::getPackedWidth(type))
      return sim::SimAggregateDefaultOp::create(builder, loc, type);
    return ControlFlowToSCFTransformation::getUndefValue(loc, builder, type);
  }
};
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

  // Give MLIR the nonsuspending iteration as a single-entry, single-exit
  // region. Its generic CFG-to-SCF conversion owns branch joins, nested loop
  // structure and SSA repair. The original counted-loop header remains the
  // boundary for the fixed-width induction and memory-independence proofs.
  // This entire function operates on a disposable candidate; the caller only
  // retains it after those proofs, including every speculated access, pass.
  auto *flat = new Block;
  Region *parent = loop.header->getParent();
  Operation *function = parent->getParentOp();
  parent->getBlocks().insert(loop.exit->getIterator(), flat);
  OpBuilder builder(flat, flat->end());
  Location loc = loop.condition.getLoc();
  auto execute = scf::ExecuteRegionOp::create(builder, loc,
                                              loop.header->getArgumentTypes());
  Region &region = execute.getRegion();
  auto *entry = new Block;
  region.push_back(entry);
  IRMapping mapping;
  for (Block *block : loop.blocks) {
    if (block == loop.header)
      continue;
    auto *copy = new Block;
    region.push_back(copy);
    mapping.map(block, copy);
    for (BlockArgument arg : block->getArguments())
      mapping.map(arg, copy->addArgument(arg.getType(), arg.getLoc()));
  }
  builder.setInsertionPointToEnd(entry);
  cf::BranchOp::create(builder, loc,
                       mapping.lookup(loop.condition.getTrueDest()),
                       loop.condition.getTrueDestOperands());
  SmallVector<Operation *> cloned;
  for (Block *block : loop.blocks) {
    if (block == loop.header)
      continue;
    builder.setInsertionPointToEnd(mapping.lookup(block));
    for (Operation &op : *block) {
      if (&op == loop.latch.getOperation()) {
        cloned.push_back(scf::YieldOp::create(builder, op.getLoc(),
                                              loop.latch.getDestOperands()));
        continue;
      }
      Operation *copy = op.clone();
      builder.insert(copy);
      for (auto [from, to] : llvm::zip(op.getResults(), copy->getResults()))
        mapping.map(from, to);
      cloned.push_back(copy);
    }
  }
  for (Operation *op : cloned) {
    for (OpOperand &operand : op->getOpOperands())
      operand.set(mapping.lookupOrDefault(operand.get()));
    for (BlockOperand &successor : op->getBlockOperands())
      successor.set(mapping.lookupOrDefault(successor.get()));
  }
  builder.setInsertionPointToEnd(flat);
  cf::BranchOp::create(builder, loc, loop.header, execute.getResults());

  WordControlFlowToSCF structure;
  DominanceInfo dominance(function);
  if (failed(transformCFGToSCF(region, structure, dominance)))
    return false;
  RewritePatternSet simplifySCF(builder.getContext());
  // The generic lifter may encode a loop guard as trunc(select(cond, 1, 0)).
  // Simplify that before SCF's WhileMoveIfDown canonicalization, so the guard
  // becomes scf.condition and the body stays in the after region. Otherwise
  // predication would speculate a final, out-of-range iteration and lose the
  // counted-loop bounds proof.
  arith::TruncIOp::getCanonicalizationPatterns(simplifySCF,
                                               builder.getContext());
  arith::SelectOp::getCanonicalizationPatterns(simplifySCF,
                                               builder.getContext());
  scf::WhileOp::getCanonicalizationPatterns(simplifySCF, builder.getContext());
  scf::IfOp::getCanonicalizationPatterns(simplifySCF, builder.getContext());
  if (failed(applyPatternsGreedily(execute, std::move(simplifySCF))))
    return false;

  auto select = [&](Value condition, Value a, Value b) -> Value {
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
          builder, loc, sim::LogicType::get(builder.getContext(), 1),
          condition);
      value =
          sim::SimLogicMuxOp::create(builder, loc, a.getType(), known, a, b);
    }
    if (value && original != value.getType())
      value = sim::SimPackedUnflattenOp::create(builder, loc, original, value);
    return value;
  };
  SmallVector<scf::IfOp> branches;
  execute.walk<WalkOrder::PostOrder>(
      [&](scf::IfOp branch) { branches.push_back(branch); });
  for (scf::IfOp branch : branches) {
    builder.setInsertionPoint(branch);
    Value yes = arith::ConstantIntOp::create(builder, loc, 1, 1);
    Value inverse =
        arith::XOrIOp::create(builder, loc, branch.getCondition(), yes);
    SmallVector<Value> yielded[2];
    for (unsigned i = 0; i != 2; ++i) {
      Region &arm = branch->getRegion(i);
      if (arm.empty())
        continue;
      if (!arm.hasOneBlock())
        return false;
      Value active = i == 0 ? branch.getCondition() : inverse;
      bool invalid = false;
      arm.walk([&](sim::SimRefStoreOp store) {
        builder.setInsertionPoint(store);
        Value previous = sim::SimRefLoadOp::create(
            builder, loc, store.getValue().getType(), store.getReference());
        Value value = select(active, store.getValue(), previous);
        if (!value)
          invalid = true;
        else
          store.getValueMutable().assign(value);
      });
      if (invalid)
        return false;
      auto yield = dyn_cast<scf::YieldOp>(arm.front().getTerminator());
      if (!yield)
        return false;
      llvm::append_range(yielded[i], yield.getResults());
      for (Operation &op :
           llvm::make_early_inc_range(arm.front().without_terminator()))
        op.moveBefore(branch);
    }
    builder.setInsertionPoint(branch);
    if (yielded[0].size() != branch.getNumResults() ||
        yielded[1].size() != branch.getNumResults())
      return false;
    for (auto [result, a, b] :
         llvm::zip(branch.getResults(), yielded[0], yielded[1])) {
      Value value = select(branch.getCondition(), a, b);
      if (!value)
        return false;
      result.replaceAllUsesWith(value);
    }
    branch.erase();
  }

  builder.setInsertionPoint(loop.condition);
  cf::CondBranchOp::create(builder, loc, loop.condition.getCondition(), flat,
                           ValueRange{}, loop.exit,
                           loop.condition.getFalseDestOperands());
  loop.condition->erase();
  for (Block *block : loop.blocks)
    if (block != loop.header)
      block->dropAllReferences();
  for (Block *block : loop.blocks)
    if (block != loop.header)
      block->dropAllDefinedValueUses();
  for (Block *block : loop.blocks)
    if (block != loop.header)
      block->erase();

  // Return to the existing sparse/dense CFG proof consumers after structured
  // predication. Uniform nested loops retain their control; a lane-dependent
  // loop condition still fails the subsequent independence proof.
  RewritePatternSet lower(builder.getContext());
  populateSCFToControlFlowConversionPatterns(lower);
  ConversionTarget target(*builder.getContext());
  target.addIllegalDialect<scf::SCFDialect>();
  target.markUnknownOpDynamicallyLegal([](Operation *) { return true; });
  if (failed(applyPartialConversion(function, target, std::move(lower))))
    return false;
  RewritePatternSet simplifyCFG(builder.getContext());
  simplifyCFG.add<CompareSelectedCase>(builder.getContext());
  arith::CmpIOp::getCanonicalizationPatterns(simplifyCFG, builder.getContext());
  arith::SelectOp::getCanonicalizationPatterns(simplifyCFG,
                                               builder.getContext());
  arith::ExtUIOp::getCanonicalizationPatterns(simplifyCFG,
                                              builder.getContext());
  arith::TruncIOp::getCanonicalizationPatterns(simplifyCFG,
                                               builder.getContext());
  cf::BranchOp::getCanonicalizationPatterns(simplifyCFG, builder.getContext());
  cf::CondBranchOp::getCanonicalizationPatterns(simplifyCFG,
                                                builder.getContext());
  return succeeded(applyPatternsGreedily(function, std::move(simplifyCFG)));
}
} // namespace obelisk
