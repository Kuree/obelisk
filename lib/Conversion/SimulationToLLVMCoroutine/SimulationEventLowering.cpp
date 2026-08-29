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

class EventCreateConversion final
    : public OpConversionPattern<sim::SimEventCreateOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimEventCreateOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value output = entryAlloca(rewriter, location, rewriter.getI64Type(), 1, 8);
    LLVM::StoreOp::create(
        rewriter, location,
        llvmConstant(rewriter, location, rewriter.getI64Type(), UINT64_MAX),
        output, 8);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_event_create"),
            ValueRange{context, output})
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    rewriter.replaceOp(operation,
                       LLVM::LoadOp::create(rewriter, location,
                                            rewriter.getI64Type(), output, 8));
    return success();
  }
};

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
    if (operation.getReplaceable()) {
      LLVM::CallOp::create(
          rewriter, location, TypeRange{},
          SymbolRefAttr::get(
              rewriter.getContext(),
              "obelisk_rt_v1_scheduler_event_replace_after"),
          ValueRange{loadCurrentRuntimeContext(rewriter, location),
                     adaptor.getEvent().front(),
                     llvmConstant(rewriter, location, rewriter.getI32Type(),
                                  adaptor.getDelay().empty() ? 0 : 1),
                     delay});
      rewriter.eraseOp(operation);
      return success();
    }
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

class WaitOrderFailedConversion final
    : public OpConversionPattern<sim::SimWaitOrderFailedOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimWaitOrderFailedOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value failed =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_wait_order_failed"),
            ValueRange{loadCurrentRuntimeContext(rewriter, location)})
            .getResult();
    rewriter.replaceOpWithNewOp<LLVM::TruncOp>(operation, rewriter.getI1Type(),
                                               failed);
    return success();
  }
};

class ClockOccurrenceConsumeConversion final
    : public OpConversionPattern<sim::SimClockOccurrenceConsumeOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimClockOccurrenceConsumeOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value site = LLVM::ConstantOp::create(
        rewriter, location, rewriter.getI64Type(),
        operation.getOccurrenceSiteAttr());
    rewriter.replaceOpWithNewOp<LLVM::CallOp>(
        operation, TypeRange{rewriter.getI64Type()},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_clock_occurrence_consume"),
        ValueRange{loadCurrentRuntimeContext(rewriter, location), site});
    return success();
  }
};

class NoChangeUpdateConversion final
    : public OpConversionPattern<sim::SimNoChangeUpdateOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimNoChangeUpdateOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getMask().size() != 1 || adaptor.getStartOffset().size() != 1 ||
        adaptor.getEndOffset().size() != 1)
      return failure();
    Location location = operation.getLoc();
    Value site =
        LLVM::ConstantOp::create(rewriter, location, rewriter.getI64Type(),
                                 operation.getOccurrenceSiteAttr());
    rewriter.replaceOpWithNewOp<LLVM::CallOp>(
        operation, TypeRange{rewriter.getI64Type()},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_nochange_update"),
        ValueRange{loadCurrentRuntimeContext(rewriter, location), site,
                   adaptor.getMask().front(), adaptor.getStartOffset().front(),
                   adaptor.getEndOffset().front()});
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
  patterns.add<EventCreateConversion, EventTriggerConversion,
               EventTriggeredConversion, WaitOrderFailedConversion,
               ClockOccurrenceConsumeConversion, NoChangeUpdateConversion,
               EventEqualConversion>(converter, patterns.getContext());
}

} // namespace obelisk::detail
