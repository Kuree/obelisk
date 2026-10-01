//===- SimulationManagedRootSupport.cpp - Managed root range support ----===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "mlir/Transforms/DialectConversion.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

LLVM::AllocaOp findManagedRootRangeRecord(Operation *scope) {
  LLVM::AllocaOp record;
  scope->walk([&](LLVM::AllocaOp allocation) {
    if (!record && allocation->hasAttr(managedRootRangeRecordAttr))
      record = allocation;
  });
  return record;
}

} // namespace

void emitManagedRootRangePop(OpBuilder &builder, Location location,
                             Operation *scope) {
  LLVM::AllocaOp record = findManagedRootRangeRecord(scope);
  if (!record)
    return;
  if (isa<sim::SimFuncOp>(scope)) {
    schedule::NativeManagedRootPopOp::create(builder, location, record);
    return;
  }
  MLIRContext *context = builder.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Value contextAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  Value runtimeContext =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  Value lane = LLVM::CallOp::create(
                   builder, location, TypeRange{pointer},
                   SymbolRefAttr::get(context, "obelisk_rt_v1_gc_current_lane"),
                   runtimeContext)
                   .getResult();
  Value status = LLVM::CallOp::create(
                     builder, location, TypeRange{builder.getI32Type()},
                     SymbolRefAttr::get(
                         context, "obelisk_rt_v1_gc_managed_root_range_pop"),
                     ValueRange{lane, record})
                     .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
      ValueRange{runtimeContext, status});
}

namespace {
class NativeManagedRootPushLowering final
    : public OpRewritePattern<schedule::NativeManagedRootPushOp> {
public:
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(schedule::NativeManagedRootPushOp op,
                                PatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_gc_managed_root_range_push"),
            ValueRange{lane, op.getRecord(), op.getSlots(), op.getCount()})
            .getResult();
    rewriter.replaceOp(op, status);
    return success();
  }
};
class NativeManagedRootPopLowering final
    : public OpRewritePattern<schedule::NativeManagedRootPopOp> {
public:
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(schedule::NativeManagedRootPopOp op,
                                PatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_gc_managed_root_range_pop"),
            ValueRange{lane, op.getRecord()})
            .getResult();
    LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{context, status});
    rewriter.eraseOp(op);
    return success();
  }
};
} // namespace
void populateNativeManagedRootToLLVMConversionPatterns(
    RewritePatternSet &patterns) {
  patterns.add<NativeManagedRootPushLowering, NativeManagedRootPopLowering>(
      patterns.getContext());
}

} // namespace obelisk::detail
