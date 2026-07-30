//===- SimulationEventLowering.cpp - Event rewrite patterns --------------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

Value loadCurrentRuntimeContext(ConversionPatternRewriter &rewriter,
                                Location location) {
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  Value address = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                            "__obelisk_current_context");
  return LLVM::LoadOp::create(rewriter, location, pointer, address, 8);
}

class EventTriggerConversion final
    : public OpConversionPattern<sim::SimEventTriggerOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimEventTriggerOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getEvent().size() != 1 || adaptor.getDelay().size() > 1)
      return failure();
    Location location = operation.getLoc();
    Value delay =
        adaptor.getDelay().empty()
            ? llvmConstant(rewriter, location, rewriter.getI64Type(), 0)
            : adaptor.getDelay().front();
    LLVM::CallOp::create(
        rewriter, location, TypeRange{},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_scheduler_event_after"),
        ValueRange{loadCurrentRuntimeContext(rewriter, location),
                   adaptor.getEvent().front(),
                   llvmConstant(rewriter, location, rewriter.getI32Type(),
                                operation.getNonblocking() ? 1 : 0),
                   delay});
    rewriter.eraseOp(operation);
    return success();
  }
};

class EventTriggeredConversion final
    : public OpConversionPattern<sim::SimEventTriggeredOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimEventTriggeredOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getEvent().size() != 1)
      return failure();
    Location location = operation.getLoc();
    Value triggered =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_event_triggered"),
            ValueRange{loadCurrentRuntimeContext(rewriter, location),
                       adaptor.getEvent().front()})
            .getResult();
    rewriter.replaceOpWithNewOp<LLVM::TruncOp>(operation, rewriter.getI1Type(),
                                               triggered);
    return success();
  }
};

class EventEqualConversion final
    : public OpConversionPattern<sim::SimEventEqualOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimEventEqualOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getLhs().size() != 1 || adaptor.getRhs().size() != 1)
      return failure();
    rewriter.replaceOpWithNewOp<arith::CmpIOp>(
        operation, arith::CmpIPredicate::eq, adaptor.getLhs().front(),
        adaptor.getRhs().front());
    return success();
  }
};

} // namespace

void populateEventToLLVMConversionPatterns(RewritePatternSet &patterns,
                                           TypeConverter &converter) {
  patterns.add<EventTriggerConversion, EventTriggeredConversion,
               EventEqualConversion>(converter, patterns.getContext());
}

} // namespace obelisk::detail
