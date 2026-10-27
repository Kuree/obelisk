//===- NativeAggregates.cpp - Forward SSA array updates --------------------===//

#include "../../../Conversion/SimulationToSchedule/NativePipeline.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/CSE.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "talos/Analysis/ScopedIntegerRangeAnalysis.h"
#include "talos/Dialect/Schedule/ScheduleFields.h"
#include "talos/Dialect/Schedule/Transforms/Passes.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace talos {
#define GEN_PASS_DEF_OPTIMIZENATIVEAGGREGATESPASS
#define GEN_PASS_DEF_PREPARENATIVEAGGREGATESPASS
#include "talos/Dialect/Schedule/Transforms/Passes.h.inc"
namespace {
// A certificate is tied to the original selector and array type. Greedy
// rewriting may change the array's producer, but may not reuse a bounds proof
// for a different selector or type.
struct ReadCertificate {
  Value index;
  Type array;
};
using ReadCertificates = DenseMap<Operation *, ReadCertificate>;

struct ForwardArrayRead : OpRewritePattern<sim::SimArrayDynExtractOp> {
  ForwardArrayRead(MLIRContext *context, const ReadCertificates &certificates)
      : OpRewritePattern(context), certificates(certificates) {}
  LogicalResult matchAndRewrite(sim::SimArrayDynExtractOp op,
                                PatternRewriter &rewriter) const override {
    auto insert = op.getInput().getDefiningOp<sim::SimArrayDynInsertOp>();
    if (!insert || insert.getIndex() != op.getIndex())
      return failure();
    auto proof = certificates.find(op);
    if (proof == certificates.end() || proof->second.index != op.getIndex() ||
        proof->second.array != op.getInput().getType())
      return failure();
    // LRM 7.4.5 / 11.5: invalid writes do nothing and invalid reads return a
    // default. Forward only when the common selector is proved known and in
    // bounds; the inserted element may still contain any X/Z bits.
    rewriter.replaceOp(op, insert.getReplacement());
    return success();
  }
  const ReadCertificates &certificates;
};

struct SimplifyArrayWrite : OpRewritePattern<sim::SimArrayDynInsertOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(sim::SimArrayDynInsertOp op,
                                PatternRewriter &rewriter) const override {
    if (auto read =
            op.getReplacement().getDefiningOp<sim::SimArrayDynExtractOp>();
        read && read.getInput() == op.getInput() &&
        read.getIndex() == op.getIndex()) {
      // Also valid for invalid selectors: the outer write is then a no-op.
      rewriter.replaceOp(op, op.getInput());
      return success();
    }
    auto prior = op.getInput().getDefiningOp<sim::SimArrayDynInsertOp>();
    if (!prior || prior.getIndex() != op.getIndex())
      return failure();
    // Pure SSA updates expose only the resulting value. The last replacement
    // wins at a valid index; both updates are no-ops at an invalid index.
    rewriter.modifyOpInPlace(
        op, [&] { op.getInputMutable().assign(prior.getInput()); });
    return success();
  }
};

class OptimizeNativeAggregatesPass final
    : public impl::OptimizeNativeAggregatesPassBase<
          OptimizeNativeAggregatesPass> {
public:
  using OptimizeNativeAggregatesPassBase::OptimizeNativeAggregatesPassBase;

private:
  void runOnOperation() override {
    auto function = getOperation();
    if (evalOnly &&
        !schedule::has<schedule::Field::EvalRawCaptures>(function)) {
      markAllAnalysesPreserved();
      return;
    }
    bool updates = false;
    function.walk([&](sim::SimArrayDynInsertOp) { updates = true; });
    if (!updates) {
      markAllAnalysesPreserved();
      return;
    }
    // Canonical SSA identities expose repeated selector expressions without
    // scanning or mutating other functions. Each worker owns its own solver.
    IRRewriter rewriter(&getContext());
    DominanceInfo dominance(function);
    eliminateCommonSubExpressions(rewriter, dominance, function);
    DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
    solver.load<dataflow::DeadCodeAnalysis>();
    solver.load<dataflow::SparseConstantPropagation>();
    auto *ranges = solver.load<analysis::ScopedIntegerRangeAnalysis>(function);
    if (failed(solver.initializeAndRun(function))) {
      signalPassFailure();
      return;
    }
    ReadCertificates certificates;
    function.walk([&](sim::SimArrayDynExtractOp read) {
      auto *scope = solver.lookupState<analysis::ScopedRangeLattice>(
          solver.getProgramPointBefore(read));
      if (!scope || !scope->reachable)
        return;
      auto range = ranges->getRange(read.getIndex(), *scope);
      if (!range)
        return;
      int64_t left, right;
      Type type = read.getInput().getType();
      if (auto array = dyn_cast<sim::PackedArrayType>(type)) {
        left = array.getLeft();
        right = array.getRight();
      } else if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
        left = array.getLeft();
        right = array.getRight();
      } else {
        return;
      }
      unsigned width = std::max(65u, range->smin().getBitWidth());
      if (range->smin().sextOrTrunc(width).sge(
              APInt(width, std::min(left, right), true)) &&
          range->smax().sextOrTrunc(width).sle(
              APInt(width, std::max(left, right), true)))
        certificates.try_emplace(read, ReadCertificate{read.getIndex(), type});
    });
    RewritePatternSet patterns(&getContext());
    patterns.add<ForwardArrayRead>(&getContext(), certificates);
    patterns.add<SimplifyArrayWrite>(&getContext());
    sim::SimAggregateExtractOp::getCanonicalizationPatterns(patterns,
                                                            &getContext());
    sim::SimAggregateInsertOp::getCanonicalizationPatterns(patterns,
                                                           &getContext());
    sim::SimPackedFlattenOp::getCanonicalizationPatterns(patterns,
                                                         &getContext());
    sim::SimPackedUnflattenOp::getCanonicalizationPatterns(patterns,
                                                           &getContext());
    // Native admission has separated conditional storage identities. Merging
    // identical blocks here can recreate reference phis that packed lowering
    // cannot represent as a fixed root plus a dynamic offset.
    GreedyRewriteConfig config;
    config.setRegionSimplificationLevel(GreedySimplifyRegionLevel::Normal);
    if (failed(applyPatternsGreedily(function, std::move(patterns), config)))
      signalPassFailure();
  }
};
class PrepareNativeAggregatesPass final
    : public impl::PrepareNativeAggregatesPassBase<
          PrepareNativeAggregatesPass> {
  void runOnOperation() override {
    WalkResult status = getOperation().walk([&](sim::SimDesignOp design) {
      OpPassManager pipeline(sim::SimDesignOp::getOperationName());
      OptimizeNativeAggregatesPassOptions options;
      options.evalOnly = true;
      pipeline.addNestedPass<sim::SimFuncOp>(
          createOptimizeNativeAggregatesPass(options));
      return failed(runPipeline(pipeline, design)) ? WalkResult::interrupt()
                                                   : WalkResult::advance();
    });
    if (status.wasInterrupted())
      signalPassFailure();
    // Original resumable bodies retain their frozen frame/rematerialization
    // plan. Only their nonsuspending eval implementations change; actor
    // identity, capture ABI, state layout and the frozen bytecode stay valid.
    markAnalysesPreserved<detail::NativePipelineAnalysis>();
  }
};
} // namespace
} // namespace talos
