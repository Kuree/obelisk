//===- SimulationControlLowering.cpp - Process control rewrites ----------===//

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

void reportRuntimeControlStatus(ConversionPatternRewriter &rewriter,
                                Location location, Value context,
                                Value status) {
  LLVM::CallOp::create(
      rewriter, location, TypeRange{},
      SymbolRefAttr::get(rewriter.getContext(), "obelisk_rt_v1_scheduler_fail"),
      ValueRange{context, status});
}

class ProcessNullConversion final
    : public OpConversionPattern<sim::SimProcessNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimProcessNullOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<arith::ConstantOp>(
        operation, rewriter.getI64Type(), rewriter.getI64IntegerAttr(0));
    return success();
  }
};

class ProcessCurrentConversion final
    : public OpConversionPattern<sim::SimProcessCurrentOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimProcessCurrentOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Value context = loadCurrentRuntimeContext(rewriter, operation.getLoc());
    auto call = LLVM::CallOp::create(
        rewriter, operation.getLoc(), TypeRange{rewriter.getI64Type()},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_process_current"),
        ValueRange{context});
    rewriter.replaceOp(operation, call.getResults());
    return success();
  }
};

class ProcessEqualConversion final
    : public OpConversionPattern<sim::SimProcessEqualOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimProcessEqualOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getLhs().size() != 1 || adaptor.getRhs().size() != 1)
      return failure();
    rewriter.replaceOpWithNewOp<arith::CmpIOp>(
        operation, arith::CmpIPredicate::eq, adaptor.getLhs().front(),
        adaptor.getRhs().front());
    return success();
  }
};

class ProcessStatusConversion final
    : public OpConversionPattern<sim::SimProcessStatusOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimProcessStatusOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getProcess().size() != 1)
      return failure();
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Type i32 = rewriter.getI32Type();
    Value outState = entryAlloca(rewriter, location, i32, 1, 4);
    LLVM::StoreOp::create(rewriter, location,
                          llvmConstant(rewriter, location, i32, 0), outState,
                          4);
    Value status = LLVM::CallOp::create(
                       rewriter, location, TypeRange{i32},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_process_status"),
                       ValueRange{context, adaptor.getProcess().front(),
                                  outState})
                       .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.replaceOpWithNewOp<LLVM::LoadOp>(operation, i32, outState, 4);
    return success();
  }
};

class ProcessRandomStateConversion final
    : public OpConversionPattern<sim::SimProcessRandomStateOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimProcessRandomStateOp operation,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getProcess().size() != 1)
      return failure();
    Location location = operation.getLoc();
    Type i64 = rewriter.getI64Type();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value outState = entryAlloca(rewriter, location, i64, 2, 8);
    Value zero = llvmConstant(rewriter, location, i64, 0);
    LLVM::StoreOp::create(rewriter, location, zero, outState, 8);
    Value incrementAddress = byteGEP(rewriter, location, outState, 8);
    LLVM::StoreOp::create(rewriter, location, zero, incrementAddress, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, location, TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_process_random_get"),
                       ValueRange{context, adaptor.getProcess().front(),
                                  outState})
                       .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    Value state = LLVM::LoadOp::create(rewriter, location, i64, outState, 8);
    Value increment =
        LLVM::LoadOp::create(rewriter, location, i64, incrementAddress, 8);
    rewriter.replaceOp(operation, ValueRange{state, increment});
    return success();
  }
};

class ProcessSetRandomStateConversion final
    : public OpConversionPattern<sim::SimProcessSetRandomStateOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimProcessSetRandomStateOp operation,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getProcess().size() != 1 || adaptor.getState().size() != 1 ||
        adaptor.getIncrement().size() != 1)
      return failure();
    Location location = operation.getLoc();
    Type i64 = rewriter.getI64Type();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value state = entryAlloca(rewriter, location, i64, 2, 8);
    LLVM::StoreOp::create(rewriter, location, adaptor.getState().front(), state,
                          8);
    Value incrementAddress = byteGEP(rewriter, location, state, 8);
    LLVM::StoreOp::create(rewriter, location, adaptor.getIncrement().front(),
                          incrementAddress, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, location, TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_process_random_set"),
                       ValueRange{context, adaptor.getProcess().front(), state})
                       .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(operation);
    return success();
  }
};

class DisableChildrenConversion final
    : public OpConversionPattern<sim::SimDisableChildrenOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimDisableChildrenOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_disable_children"),
            ValueRange{context})
            .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(operation);
    return success();
  }
};

class ControlEnterConversion final
    : public OpConversionPattern<sim::SimControlEnterOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimControlEnterOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Value out = entryAlloca(rewriter, location, i64, 1, 8);
    LLVM::StoreOp::create(rewriter, location,
                          llvmConstant(rewriter, location, i64, 0), out, 8);
    Value status =
        LLVM::CallOp::create(rewriter, location, TypeRange{i32},
                             SymbolRefAttr::get(rewriter.getContext(),
                                                "obelisk_rt_v1_control_enter"),
                             ValueRange{context,
                                        llvmConstant(rewriter, location, i64,
                                                     operation.getTargetId()),
                                        out})
            .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    Value activation = LLVM::LoadOp::create(rewriter, location, i64, out, 8);
    rewriter.replaceOp(operation, activation);
    return success();
  }
};

class ControlLeaveConversion final
    : public OpConversionPattern<sim::SimControlLeaveOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimControlLeaveOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getControl().size() != 1)
      return failure();
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value status = LLVM::CallOp::create(
                       rewriter, location, TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_control_leave"),
                       ValueRange{context, adaptor.getControl().front()})
                       .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(operation);
    return success();
  }
};

class ControlDisableConversion final
    : public OpConversionPattern<sim::SimControlDisableOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimControlDisableOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value activation =
        adaptor.getActivation().empty()
            ? llvmConstant(rewriter, location, rewriter.getI64Type(), 0)
            : adaptor.getActivation().front();
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_control_disable"),
            ValueRange{context,
                       llvmConstant(rewriter, location, rewriter.getI64Type(),
                                    operation.getTargetId()),
                       activation,
                       llvmConstant(rewriter, location, rewriter.getI32Type(),
                                    operation.getHierarchical() ? 1 : 0)})
            .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(operation);
    return success();
  }
};

template <typename Op>
class OnceConversion final : public OpConversionPattern<Op> {
public:
  OnceConversion(const TypeConverter &converter, MLIRContext *context,
                 StringRef runtimeFunction)
      : OpConversionPattern<Op>(converter, context),
        runtimeFunction(runtimeFunction) {}

  LogicalResult
  matchAndRewrite(Op operation,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value claimed =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), runtimeFunction),
            ValueRange{context,
                       llvmConstant(rewriter, location, rewriter.getI64Type(),
                                    operation.getId())})
            .getResult();
    rewriter.replaceOpWithNewOp<arith::TruncIOp>(operation,
                                                 rewriter.getI1Type(), claimed);
    return success();
  }

private:
  StringRef runtimeFunction;
};

class DeferredEnqueueConversion final
    : public OpConversionPattern<sim::SimDeferredEnqueueOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimDeferredEnqueueOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    SmallVector<Value> arguments{
        context, llvmConstant(rewriter, location, rewriter.getI64Type(),
                              operation.getId())};
    StringRef runtimeFunction = "obelisk_rt_v1_deferred_enqueue";
    if (auto assertionID = operation->getAttrOfType<IntegerAttr>(
            "obelisk_sim.assertion_control_target_id")) {
      runtimeFunction = "obelisk_rt_v1_deferred_enqueue_for_assertion";
      arguments.push_back(llvmConstant(rewriter, location,
                                       rewriter.getI64Type(),
                                       assertionID.getValue().getZExtValue()));
    }
    rewriter.replaceOpWithNewOp<LLVM::CallOp>(
        operation, TypeRange{rewriter.getI64Type()},
        SymbolRefAttr::get(rewriter.getContext(), runtimeFunction), arguments);
    return success();
  }
};

class DeferredMatureConversion final
    : public OpConversionPattern<sim::SimDeferredMatureOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimDeferredMatureOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value current = LLVM::CallOp::create(
                        rewriter, location, TypeRange{rewriter.getI32Type()},
                        SymbolRefAttr::get(rewriter.getContext(),
                                           "obelisk_rt_v1_deferred_mature"),
                        ValueRange{context, operation.getTicket()})
                        .getResult();
    rewriter.replaceOpWithNewOp<arith::TruncIOp>(operation,
                                                 rewriter.getI1Type(), current);
    return success();
  }
};

class AssertionControlConversion final
    : public OpConversionPattern<sim::SimAssertionControlOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimAssertionControlOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assertion_control"),
            ValueRange{context,
                       llvmConstant(rewriter, location, rewriter.getI32Type(),
                                    operation.getAction()),
                       llvmConstant(rewriter, location, rewriter.getI64Type(),
                                    operation.getAssertionId())})
            .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(operation);
    return success();
  }
};

class AssertionEnabledConversion final
    : public OpConversionPattern<sim::SimAssertionEnabledOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimAssertionEnabledOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value enabled =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assertion_enabled"),
            ValueRange{context,
                       llvmConstant(rewriter, location, rewriter.getI64Type(),
                                    operation.getAssertionId())})
            .getResult();
    rewriter.replaceOpWithNewOp<arith::TruncIOp>(operation,
                                                 rewriter.getI1Type(), enabled);
    return success();
  }
};

class AssertionActionStateConversion final
    : public OpConversionPattern<sim::SimAssertionActionStateOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimAssertionActionStateOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value state =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assertion_action_state"),
            ValueRange{context,
                       llvmConstant(rewriter, location, rewriter.getI64Type(),
                                    operation.getAssertionId())})
            .getResult();
    rewriter.replaceOp(operation, state);
    return success();
  }
};

class MonitorRegisterConversion final
    : public OpConversionPattern<sim::SimMonitorRegisterOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimMonitorRegisterOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getProcess().size() != 1)
      return failure();
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value status = LLVM::CallOp::create(
                       rewriter, location, TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_monitor_register_logical"),
                       ValueRange{context, adaptor.getProcess().front()})
                       .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(operation);
    return success();
  }
};

class MonitorControlConversion final
    : public OpConversionPattern<sim::SimMonitorControlOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimMonitorControlOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_monitor_control"),
            ValueRange{context,
                       llvmConstant(rewriter, location, rewriter.getI32Type(),
                                    operation.getEnabled() ? 1 : 0)})
            .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(operation);
    return success();
  }
};

class MonitorCurrentConversion final
    : public OpConversionPattern<sim::SimMonitorCurrentOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimMonitorCurrentOp operation, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation.getLoc();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    Value current = LLVM::CallOp::create(
                        rewriter, location, TypeRange{rewriter.getI32Type()},
                        SymbolRefAttr::get(rewriter.getContext(),
                                           "obelisk_rt_v1_monitor_current"),
                        ValueRange{context})
                        .getResult();
    rewriter.replaceOpWithNewOp<arith::TruncIOp>(operation,
                                                 rewriter.getI1Type(), current);
    return success();
  }
};

} // namespace

void populateControlToLLVMConversionPatterns(RewritePatternSet &patterns,
                                             TypeConverter &converter) {
  MLIRContext *context = patterns.getContext();
  patterns.add<DisableChildrenConversion, ControlEnterConversion,
               ControlLeaveConversion, ControlDisableConversion,
               ProcessNullConversion, ProcessCurrentConversion,
               ProcessEqualConversion, ProcessStatusConversion,
               ProcessRandomStateConversion, ProcessSetRandomStateConversion,
               MonitorRegisterConversion, MonitorControlConversion,
               MonitorCurrentConversion>(converter, context);
  patterns.add<OnceConversion<sim::SimStaticOnceOp>>(
      converter, context, "obelisk_rt_v1_static_once");
  patterns.add<OnceConversion<sim::SimDeferredOnceOp>>(
      converter, context, "obelisk_rt_v1_deferred_once");
  patterns.add<DeferredEnqueueConversion, DeferredMatureConversion,
               AssertionControlConversion, AssertionEnabledConversion,
               AssertionActionStateConversion>(converter, context);
}

} // namespace obelisk::detail
