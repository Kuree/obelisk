//===- SimulationFunctionBodyLowering.cpp - Native body rewrites ----------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/WalkPatternRewriteDriver.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

class NativeCallPattern final : public OpRewritePattern<sim::SimCallOp> {
public:
  NativeCallPattern(MLIRContext *context, NativeCallResultLowering lowering)
      : OpRewritePattern(context), lowering(lowering) {}

  LogicalResult matchAndRewrite(sim::SimCallOp operation,
                                PatternRewriter &rewriter) const override {
    SmallVector<Type> resultTypes;
    for (Type type : operation.getResultTypes())
      resultTypes.push_back(
          lowering == NativeCallResultLowering::ConvertProcessTypes
              ? convertProcessType(type, rewriter.getContext())
              : type);
    auto call = func::CallOp::create(rewriter, operation.getLoc(),
                                     operation.getCallee(), resultTypes,
                                     operation.getOperands());
    rewriter.replaceOp(operation, call.getResults());
    return success();
  }

private:
  NativeCallResultLowering lowering;
};

class NativeSpawnPattern final : public OpRewritePattern<sim::SimSpawnOp> {
public:
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(sim::SimSpawnOp operation,
                                PatternRewriter &rewriter) const override {
    auto call = LLVM::CallOp::create(
        rewriter, operation.getLoc(), TypeRange{rewriter.getI64Type()},
        SymbolRefAttr::get(rewriter.getContext(),
                           (operation.getCallee() + ".__obelisk_spawn").str()),
        operation.getOperands());
    rewriter.replaceOp(operation, call.getResults());
    return success();
  }
};

class NativeReturnPattern final : public OpRewritePattern<sim::SimReturnOp> {
public:
  NativeReturnPattern(MLIRContext *context, NativeReturnLowering lowering)
      : OpRewritePattern(context), lowering(lowering) {}

  LogicalResult matchAndRewrite(sim::SimReturnOp operation,
                                PatternRewriter &rewriter) const override {
    if (lowering == NativeReturnLowering::SuccessStatus) {
      if (!operation.getOperands().empty())
        return rewriter.notifyMatchFailure(
            operation, "process success return must not carry values");
      Value zero = arith::ConstantOp::create(rewriter, operation.getLoc(),
                                             rewriter.getI32Type(),
                                             rewriter.getI32IntegerAttr(0));
      rewriter.replaceOpWithNewOp<func::ReturnOp>(operation, zero);
      return success();
    }
    rewriter.replaceOpWithNewOp<func::ReturnOp>(operation,
                                                operation.getOperands());
    return success();
  }

private:
  NativeReturnLowering lowering;
};

} // namespace

LogicalResult
lowerNativeFunctionBody(Operation *root, NativeReturnLowering returnLowering,
                        NativeCallResultLowering callResultLowering) {
  RewritePatternSet patterns(root->getContext());
  patterns.add<NativeCallPattern>(root->getContext(), callResultLowering);
  patterns.add<NativeSpawnPattern>(root->getContext());
  if (returnLowering != NativeReturnLowering::None)
    patterns.add<NativeReturnPattern>(root->getContext(), returnLowering);
  walkAndApplyPatterns(root, FrozenRewritePatternSet(std::move(patterns)));
  WalkResult leftovers = root->walk([&](Operation *operation) {
    bool illegal = isa<sim::SimCallOp, sim::SimSpawnOp>(operation) ||
                   (returnLowering != NativeReturnLowering::None &&
                    isa<sim::SimReturnOp>(operation));
    return illegal ? WalkResult::interrupt() : WalkResult::advance();
  });
  if (leftovers.wasInterrupted())
    return root->emitError("native function-body rewrite left an illegal op");
  return success();
}

} // namespace obelisk::detail
