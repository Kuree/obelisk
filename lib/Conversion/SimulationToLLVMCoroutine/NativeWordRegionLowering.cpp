#include "../SimulationToSchedule/NativePipeline.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/PatternMatch.h"
#include "obelisk/Conversion/Passes.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"

using namespace mlir;
namespace obelisk {
#define GEN_PASS_DEF_LOWERNATIVEWORDREGIONSPASS
#include "obelisk/Conversion/Passes.h.inc"
namespace {
class LowerNativeWordRegionsPass final
    : public impl::LowerNativeWordRegionsPassBase<LowerNativeWordRegionsPass> {
  void runOnOperation() override {
    SmallVector<schedule::NativeWordRegionOp> regions;
    getOperation().walk(
        [&](schedule::NativeWordRegionOp op) { regions.push_back(op); });
    IRRewriter rewriter(&getContext());
    for (auto op : regions) {
      Block *before = op->getBlock();
      Block *after = rewriter.splitBlock(before, Block::iterator(op));
      Block *entry = &op.getBody().front();
      for (Block &block : op.getBody())
        if (auto yield = dyn_cast<schedule::NativeWordYieldOp>(block.back())) {
          rewriter.setInsertionPoint(yield);
          cf::BranchOp::create(rewriter, yield.getLoc(), after);
          rewriter.eraseOp(yield);
        }
      rewriter.inlineRegionBefore(op.getBody(), *before->getParent(),
                                  after->getIterator());
      rewriter.setInsertionPointToEnd(before);
      cf::BranchOp::create(rewriter, op.getLoc(), entry);
      rewriter.eraseOp(op);
    }
    // Executable bodies change without changing frozen actors or fallback ABI.
    markAnalysesPreserved<detail::NativePipelineAnalysis>();
  }
};
} // namespace
} // namespace obelisk
