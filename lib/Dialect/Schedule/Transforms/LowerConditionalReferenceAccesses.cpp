//===- LowerConditionalReferenceAccesses.cpp - Reference choices --------===//

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Interfaces/ViewLikeInterface.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/SetVector.h"

using namespace mlir;
namespace obelisk {
#define GEN_PASS_DEF_LOWERCONDITIONALREFERENCEACCESSESPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"

namespace {
static bool isPureReferenceView(Operation *operation) {
  return isa<sim::SimRefExtractOp, sim::SimRefDynExtractOp,
             sim::SimRefSubelementOp, sim::SimRefArrayElementOp>(operation) &&
         isMemoryEffectFree(operation) && isSpeculatable(operation);
}

static arith::SelectOp
findSelectedReference(Value root, SmallVectorImpl<Operation *> &views) {
  while (auto *operation = root.getDefiningOp()) {
    if (!isPureReferenceView(operation))
      break;
    views.push_back(operation);
    root = cast<ViewLikeOpInterface>(operation).getViewSource();
  }
  auto select = root.getDefiningOp<arith::SelectOp>();
  return select && select.getCondition().getType().isInteger(1) ? select
                                                                : nullptr;
}

template <typename AccessOp>
class ConditionalAccess final : public OpRewritePattern<AccessOp> {
public:
  using OpRewritePattern<AccessOp>::OpRewritePattern;
  LogicalResult matchAndRewrite(AccessOp access,
                                PatternRewriter &rewriter) const override {
    auto function = access->template getParentOfType<sim::SimFuncOp>();
    if (!function)
      return failure();
    SmallVector<Operation *> views;
    auto select = findSelectedReference(access.getReference(), views);
    if (!select)
      return failure();

    // IEEE 1800-2023 4.6(a): the access observes the memory version at this
    // statement, not at reference selection. Keep intervening stores in
    // place, and execute exactly one access; the other handle can be invalid.
    // Keep address alternatives separate until reference lowering selects
    // their native representations. CFG block merging would immediately
    // recreate a mixed reference phi from two identical access blocks.
    auto conditional =
        scf::IfOp::create(rewriter, access.getLoc(), access->getResultTypes(),
                          select.getCondition(),
                          /*withElseRegion=*/true);
    auto emitAccess = [&](Block *block, Value reference) {
      rewriter.setInsertionPointToStart(block);
      IRMapping mapping;
      mapping.map(select.getResult(), reference);
      for (Operation *view : llvm::reverse(views))
        rewriter.clone(*view, mapping);
      auto *copy = rewriter.clone(*access, mapping);
      if constexpr (std::is_same_v<AccessOp, sim::SimRefLoadOp>)
        scf::YieldOp::create(rewriter, access.getLoc(), copy->getResults());
    };
    emitAccess(&conditional.getThenRegion().front(), select.getTrueValue());
    emitAccess(&conditional.getElseRegion().front(), select.getFalseValue());
    rewriter.replaceOp(access, conditional.getResults());
    return success();
  }
};

class LowerConditionalReferenceAccessesPass final
    : public impl::LowerConditionalReferenceAccessesPassBase<
          LowerConditionalReferenceAccessesPass> {
public:
  using Base::Base;
  void runOnOperation() override {
    if (nativeBodiesOnly &&
        !schedule::has<schedule::Field::EvalBorrowedCaptures>(getOperation())) {
      markAllAnalysesPreserved();
      return;
    }
    RewritePatternSet patterns(&getContext());
    patterns.add<ConditionalAccess<sim::SimRefLoadOp>,
                 ConditionalAccess<sim::SimRefStoreOp>>(&getContext());
    llvm::SetVector<Operation *> candidates;
    getOperation()->walk([&](Operation *access) {
      Value reference;
      if (auto read = dyn_cast<sim::SimRefLoadOp>(access))
        reference = read.getReference();
      else if (auto write = dyn_cast<sim::SimRefStoreOp>(access))
        reference = write.getReference();
      else
        return;
      SmallVector<Operation *> views;
      if (!findSelectedReference(reference, views))
        return;
      candidates.insert(access);
      SmallVector<Value> pending{reference};
      while (!pending.empty()) {
        auto *operation = pending.pop_back_val().getDefiningOp();
        if (!operation)
          continue;
        if (auto select = dyn_cast<arith::SelectOp>(operation)) {
          if (candidates.insert(operation)) {
            pending.push_back(select.getTrueValue());
            pending.push_back(select.getFalseValue());
          }
        } else if (isPureReferenceView(operation) &&
                   candidates.insert(operation)) {
          pending.push_back(
              cast<ViewLikeOpInterface>(operation).getViewSource());
        }
      }
    });
    if (candidates.empty()) {
      markAllAnalysesPreserved();
      return;
    }
    // Clean up the now-unused reference expressions, without folding or
    // deleting unrelated operations in an already planned evaluator body.
    GreedyRewriteConfig config;
    config.setRegionSimplificationLevel(GreedySimplifyRegionLevel::Disabled);
    config.setStrictness(GreedyRewriteStrictness::ExistingAndNewOps);
    config.enableFolding(false);
    config.enableConstantCSE(false);
    if (failed(applyOpPatternsGreedily(candidates.getArrayRef(),
                                       std::move(patterns), config)))
      signalPassFailure();
  }
};
} // namespace
} // namespace obelisk
