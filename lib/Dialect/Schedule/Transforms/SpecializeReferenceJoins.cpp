//===- SpecializeReferenceJoins.cpp - Expose native storage identities
//-----===//

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk {
#define GEN_PASS_DEF_SPECIALIZEREFERENCEJOINSPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"

namespace {
class SpecializeReferenceJoinsPass final
    : public impl::SpecializeReferenceJoinsPassBase<
          SpecializeReferenceJoinsPass> {
public:
  using Base::Base;
  void runOnOperation() override {
    auto function = getOperation();
    if (nativeBodiesOnly &&
        !schedule::has<schedule::Field::EvalBorrowedCaptures>(function)) {
      markAllAnalysesPreserved();
      return;
    }
    IRRewriter rewriter(&getContext());
    unsigned remaining = 1024;
    bool changed = false;
    for (Block &block : llvm::make_early_inc_range(function.getBody())) {
      if (block.isEntryBlock() ||
          llvm::none_of(block.getArgumentTypes(),
                        [](Type type) { return isa<sim::RefType>(type); }) ||
          !isa<cf::BranchOp, cf::CondBranchOp>(block.getTerminator()))
        continue;
      unsigned size = block.getOperations().size();
      if (size > 32 || llvm::any_of(block, [](Operation &op) {
            return op.getNumRegions() != 0;
          }))
        continue;

      // Scalar definitions used by successors need a shared continuation. Its
      // arguments merge the executed values, not their storage addresses.
      SmallVector<Value> escaping;
      auto collectEscaping = [&](Value value) {
        if (llvm::any_of(value.getUsers(), [&](Operation *user) {
              return user->getBlock() != &block;
            }))
          escaping.push_back(value);
      };
      for (BlockArgument argument : block.getArguments())
        collectEscaping(argument);
      for (Operation &op : block)
        for (Value result : op.getResults())
          collectEscaping(result);
      SmallVector<Value> forwarded = escaping;
      if (!escaping.empty()) {
        for (Value operand : block.getTerminator()->getOperands())
          if (operand.getParentBlock() == &block &&
              !llvm::is_contained(forwarded, operand))
            forwarded.push_back(operand);
        if (llvm::any_of(forwarded, [](Value value) {
              return isa<sim::RefType>(value.getType());
            }))
          continue;
      }

      SmallVector<std::pair<Operation *, unsigned>> edges;
      bool supported = true;
      for (BlockOperand &use : block.getUses()) {
        Operation *terminator = use.getOwner();
        if (terminator->getBlock() == &block ||
            !isa<cf::BranchOp, cf::CondBranchOp>(terminator)) {
          supported = false;
          break;
        }
        edges.emplace_back(terminator, use.getOperandNumber());
      }
      unsigned cost = size + !escaping.empty();
      if (!supported || edges.size() < 2 || edges.size() > remaining / cost)
        continue;
      // A common incoming reference already has one storage identity. Avoid
      // duplicating the block merely because it also carries scalar phis.
      bool different = false;
      for (auto argument : block.getArguments()) {
        if (!isa<sim::RefType>(argument.getType()))
          continue;
        Value first;
        for (auto [terminator, successor] : edges) {
          Value incoming =
              cast<BranchOpInterface>(terminator)
                  .getSuccessorOperands(successor)[argument.getArgNumber()];
          if (first && first != incoming)
            different = true;
          first = incoming;
        }
      }
      if (!different)
        continue;

      // IEEE 1800-2023 4.6(a): execute the same statements, once and in source
      // order, on the selected edge. Nothing is hoisted across a predecessor's
      // effects. Merge any escaping scalar definitions before the original
      // terminator, which continues to dominate their external users.
      if (!escaping.empty()) {
        Operation *terminator = block.getTerminator();
        Block *continuation = rewriter.createBlock(
            block.getParent(), std::next(block.getIterator()));
        IRMapping mapping;
        for (Value value : forwarded)
          mapping.map(value, continuation->addArgument(value.getType(),
                                                      value.getLoc()));
        rewriter.clone(*terminator, mapping);
        for (Value value : escaping)
          rewriter.replaceUsesWithIf(
              value, mapping.lookup(value), [&](OpOperand &use) {
                return use.getOwner()->getBlock() != &block;
              });
        rewriter.setInsertionPoint(terminator);
        cf::BranchOp::create(rewriter, terminator->getLoc(), continuation,
                             forwarded);
        rewriter.eraseOp(terminator);
      }
      for (auto [terminator, successor] : edges) {
        auto branch = cast<BranchOpInterface>(terminator);
        auto operands = branch.getSuccessorOperands(successor);
        IRMapping mapping;
        for (auto [argument, incoming] :
             llvm::zip(block.getArguments(), operands.getForwardedOperands()))
          mapping.map(argument, incoming);
        Block *clone =
            rewriter.createBlock(block.getParent(), block.getIterator());
        for (Operation &operation : block)
          rewriter.clone(operation, mapping);
        rewriter.modifyOpInPlace(terminator, [&] {
          operands.erase(0, operands.size());
          terminator->setSuccessor(clone, successor);
        });
      }
      remaining -= edges.size() * cost;
      rewriter.eraseBlock(&block);
      changed = true;
    }
    if (!changed)
      markAllAnalysesPreserved();
  }
};
} // namespace
} // namespace obelisk
