//===- SimulationManagedRootSupport.cpp - Managed root range support ----===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "mlir/Transforms/DialectConversion.h"
#include "talos/Dialect/Native/NativeDialect.h"
#include "talos/Dialect/Schedule/ScheduleOps.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "talos/Runtime/Runtime.h"

#include <cstddef>

using namespace mlir;

namespace talos::detail {

LLVM::AllocaOp findManagedRootRangeRecord(Operation *scope) {
  LLVM::AllocaOp record;
  scope->walk([&](LLVM::AllocaOp allocation) {
    if (!native::has<native::ManagedRootRangeRecord>(allocation))
      return WalkResult::advance();
    record = allocation;
    return WalkResult::interrupt();
  });
  return record;
}

void emitManagedRootRangePop(OpBuilder &builder, Location location,
                             Operation *scope) {
  emitManagedRootRangePop(builder, location, scope,
                          findManagedRootRangeRecord(scope));
}

namespace {
/// Whether `record` is registered with the runtime: a registered managed root
/// range always carries a nonzero cookie, and an unregistered one zero.
Value isManagedRootRangeRegistered(OpBuilder &builder, Location location,
                                   Value record) {
  Type i64 = builder.getI64Type();
  Value cookieAddress = LLVM::GEPOp::create(
      builder, location, LLVM::LLVMPointerType::get(builder.getContext()),
      builder.getI8Type(), record,
      ArrayRef<LLVM::GEPArg>{static_cast<int32_t>(
          offsetof(talos_rt_gc_managed_root_range_v1, cookie))},
      LLVM::GEPNoWrapFlags::inbounds);
  Value cookie = LLVM::LoadOp::create(builder, location, i64, cookieAddress, 8);
  Value zero = LLVM::ConstantOp::create(builder, location, i64,
                                        builder.getI64IntegerAttr(0));
  return LLVM::ICmpOp::create(builder, location, LLVM::ICmpPredicate::ne,
                              cookie, zero);
}

/// Unregister `record` if it is registered.
void emitGuardedManagedRootRangePop(OpBuilder &builder, Location location,
                                    Value runtimeContext, Value record,
                                    Block *registered, Block *rest) {
  MLIRContext *context = builder.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  LLVM::CondBrOp::create(builder, location,
                         isManagedRootRangeRegistered(builder, location, record),
                         registered, rest);
  builder.setInsertionPointToEnd(registered);
  if (!runtimeContext) {
    Value contextAddress = LLVM::AddressOfOp::create(
        builder, location, pointer, "__talos_current_context");
    runtimeContext =
        LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  }
  Value lane = LLVM::CallOp::create(
                   builder, location, TypeRange{pointer},
                   SymbolRefAttr::get(context, "talos_rt_v1_gc_current_lane"),
                   runtimeContext)
                   .getResult();
  Value status = LLVM::CallOp::create(
                     builder, location, TypeRange{builder.getI32Type()},
                     SymbolRefAttr::get(
                         context, "talos_rt_v1_gc_managed_root_range_pop"),
                     ValueRange{lane, record})
                     .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "talos_rt_v1_scheduler_fail"),
      ValueRange{runtimeContext, status});
  LLVM::BrOp::create(builder, location, rest);
}
} // namespace

void emitManagedRootRangePop(OpBuilder &builder, Location location,
                             Operation *scope, LLVM::AllocaOp record) {
  if (!record)
    return;
  if (isa<sim::SimFuncOp>(scope)) {
    schedule::NativeManagedRootPopOp::create(builder, location, record);
    return;
  }
  Block *block = builder.getInsertionBlock();
  Block *rest = block->splitBlock(builder.getInsertionPoint());
  Block *registered = new Block;
  block->getParent()->getBlocks().insert(rest->getIterator(), registered);
  builder.setInsertionPointToEnd(block);
  emitGuardedManagedRootRangePop(builder, location, Value{}, record,
                                 registered, rest);
  builder.setInsertionPointToStart(rest);
}

namespace {
class NativeManagedRootPushLowering final
    : public OpRewritePattern<schedule::NativeManagedRootPushOp> {
public:
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(schedule::NativeManagedRootPushOp op,
                                PatternRewriter &rewriter) const override {
    Location location = op.getLoc();
    auto push = [&]() {
      auto [context, lane] = managedContextAndLane(rewriter, location);
      return LLVM::CallOp::create(
                 rewriter, location, TypeRange{rewriter.getI32Type()},
                 SymbolRefAttr::get(rewriter.getContext(),
                                    "talos_rt_v1_gc_managed_root_range_push"),
                 ValueRange{lane, op.getRecord(), op.getSlots(), op.getCount()})
          .getResult();
    };
    if (!op.getLazy()) {
      rewriter.replaceOp(op, push());
      return success();
    }
    // block: registered ? join(ok) : pushBlock
    // pushBlock: join(push())
    // join(status): rest
    Type i32 = rewriter.getI32Type();
    Block *block = op->getBlock();
    Block *rest = rewriter.splitBlock(block, std::next(op->getIterator()));
    Block *join = rewriter.createBlock(rest, {i32}, {location});
    LLVM::BrOp::create(rewriter, location, rest);
    Block *pushBlock = rewriter.createBlock(join);
    rewriter.setInsertionPointToEnd(pushBlock);
    LLVM::BrOp::create(rewriter, location, ValueRange{push()}, join);
    rewriter.setInsertionPoint(op);
    Value ok = LLVM::ConstantOp::create(
        rewriter, location, i32, rewriter.getI32IntegerAttr(TALOS_RT_OK));
    LLVM::CondBrOp::create(
        rewriter, location,
        isManagedRootRangeRegistered(rewriter, location, op.getRecord()), join,
        ValueRange{ok}, pushBlock, ValueRange{});
    rewriter.replaceOp(op, join->getArgument(0));
    return success();
  }
};
class NativeManagedRootPopLowering final
    : public OpRewritePattern<schedule::NativeManagedRootPopOp> {
public:
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(schedule::NativeManagedRootPopOp op,
                                PatternRewriter &rewriter) const override {
    Location location = op.getLoc();
    Block *block = op->getBlock();
    Block *rest = rewriter.splitBlock(block, std::next(op->getIterator()));
    Block *registered = rewriter.createBlock(rest);
    rewriter.setInsertionPoint(op);
    Value context = managedContext(rewriter, location);
    emitGuardedManagedRootRangePop(rewriter, location, context, op.getRecord(),
                                   registered, rest);
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

} // namespace talos::detail
