//===- SimulationToRuntime.cpp - Lower simulation I/O to runtime calls ---===//

#include "obelisk/Conversion/SimulationToRuntime.h"

#include "obelisk/Conversion/SimulationToStandard.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTOBELISKSIMTORUNTIMEPASS
#include "obelisk/Conversion/Passes.h.inc"

void addSimulationToRuntimeTypeConversions(TypeConverter &converter) {
  converter.addConversion([&converter](sim::BytesType type,
                                       SmallVectorImpl<Type> &results) {
    Type converted = converter.convertType(
        runtime::ByteSpanType::get(type.getContext()));
    if (!converted)
      return failure();
    results.push_back(converted);
    return success();
  });
}

namespace {

static Value iConstant(OpBuilder &builder, Location location, IntegerType type,
                       int64_t value) {
  return arith::ConstantOp::create(builder, location, type,
                                   builder.getIntegerAttr(type, value));
}

static Value runtimeContext(ConversionPatternRewriter &rewriter, Location loc,
                            Value context) {
  return sim::SimContextRuntimeOp::create(
      rewriter, loc, runtime::ContextType::get(rewriter.getContext()), context);
}

static Value descriptor(ConversionPatternRewriter &rewriter, Location loc,
                        Value bits) {
  return runtime::RTFileDescriptorFromBitsOp::create(
      rewriter, loc, runtime::FileDescriptorType::get(rewriter.getContext()),
      bits);
}

static Value descriptorBits(ConversionPatternRewriter &rewriter, Location loc,
                            Value value) {
  return runtime::RTFileDescriptorToBitsOp::create(
      rewriter, loc, rewriter.getI32Type(), value);
}

static Value statusIs(ConversionPatternRewriter &rewriter, Location loc,
                      Value status, uint32_t value) {
  return runtime::RTStatusIsOp::create(rewriter, loc, rewriter.getI1Type(),
                                       status, value);
}

static Value sentinel(ConversionPatternRewriter &rewriter, Location loc,
                      Value status, Value success, Value failure) {
  Value ok = statusIs(rewriter, loc, status, 0);
  return arith::SelectOp::create(rewriter, loc, ok, success, failure);
}

static std::string displayScope(Operation *operation) {
  if (auto function = operation->getParentOfType<sim::SimFuncOp>()) {
    if (auto hierarchy = function->getAttrOfType<StringAttr>(
            sim::metadata::hierarchicalName))
      return hierarchy.getValue().str();
    return function.getSymName().str();
  }
  if (auto function = operation->getParentOfType<func::FuncOp>()) {
    if (auto hierarchy = function->getAttrOfType<StringAttr>(
            sim::metadata::hierarchicalName))
      return hierarchy.getValue().str();
    return function.getSymName().str();
  }
  return {};
}

template <typename Op>
class SimIOConversion : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
};

class BytesConstantConversion final
    : public SimIOConversion<sim::SimBytesConstantOp> {
public:
  using SimIOConversion::SimIOConversion;

  LogicalResult
  matchAndRewrite(sim::SimBytesConstantOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto type = runtime::ByteSpanType::get(rewriter.getContext());
    rewriter.replaceOpWithNewOp<runtime::RTBytesConstantOp>(op, type,
                                                            op.getValueAttr());
    return success();
  }
};

class DisplayConversion final : public SimIOConversion<sim::SimDisplayOp> {
public:
  using SimIOConversion::SimIOConversion;

  LogicalResult
  matchAndRewrite(sim::SimDisplayOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    Value context = runtimeContext(rewriter, loc, adaptor.getContext().front());
    Value fd = descriptor(rewriter, loc, adaptor.getDescriptor().front());
    SmallVector<Value> arguments;
    unsigned itemIndex = 0;
    for (int32_t flags : op.getItemFlags()) {
      if ((flags & 2) != 0) {
        arguments.push_back(runtime::RTArgumentEmptyOp::create(
            rewriter, loc, runtime::ArgumentType::get(rewriter.getContext())));
        continue;
      }
      ValueRange converted = adaptor.getItems()[itemIndex];
      Type sourceType = op.getItems()[itemIndex++].getType();
      if (isa<sim::BytesType>(sourceType)) {
        if (converted.size() != 1)
          return rewriter.notifyMatchFailure(op,
                                             "literal byte item did not convert 1:1");
        arguments.push_back(runtime::RTArgumentBytesOp::create(
            rewriter, loc, runtime::ArgumentType::get(rewriter.getContext()),
            converted.front(), true));
        continue;
      }
      if (converted.size() != 1 && converted.size() != 2)
        return rewriter.notifyMatchFailure(
            op, "packed display item must convert to one or two planes");
      Value unknown = converted.size() == 2 ? converted[1] : Value();
      arguments.push_back(runtime::RTArgumentPackedOp::create(
          rewriter, loc, runtime::ArgumentType::get(rewriter.getContext()),
          converted.front(), unknown, (flags & 1) != 0));
    }
    Value array = runtime::RTArgumentArrayOp::create(
        rewriter, loc, runtime::ArgumentArrayType::get(rewriter.getContext()),
        arguments);
    std::string scope = op.getScope().value_or("").empty()
                            ? displayScope(op)
                            : op.getScope()->str();
    Value environment = runtime::RTFormatEnvironmentOp::create(
        rewriter, loc,
        runtime::FormatEnvironmentType::get(rewriter.getContext()),
        scope, op.getLibraryCell().value_or(""), 0, "",
        op.getTimeMultiplier().value_or(1));
    Value newline = iConstant(rewriter, loc, rewriter.getI1Type(),
                              op.getAppendNewline() ? 1 : 0);
    auto radix = static_cast<runtime::Radix>(op.getDefaultRadix());
    Value status = runtime::RTDisplayOp::create(
        rewriter, loc, runtime::StatusType::get(rewriter.getContext()), context,
        fd, newline, array, environment, radix);
    sim::SimStatusCheckOp::create(rewriter, loc, status);
    rewriter.eraseOp(op);
    return success();
  }
};

template <typename Op, typename RuntimeOp>
class TerminationConversion final : public SimIOConversion<Op> {
public:
  using SimIOConversion<Op>::SimIOConversion;

  LogicalResult
  matchAndRewrite(
      Op op, typename SimIOConversion<Op>::OneToNOpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    Value context = runtimeContext(rewriter, loc, adaptor.getContext().front());
    Value status = RuntimeOp::create(
        rewriter, loc, runtime::StatusType::get(rewriter.getContext()),
        context, adaptor.getVerbosity().front());
    sim::SimStatusCheckOp::create(rewriter, loc, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class TerminationRequestedConversion final
    : public SimIOConversion<sim::SimTerminationRequestedOp> {
public:
  using SimIOConversion::SimIOConversion;

  LogicalResult
  matchAndRewrite(sim::SimTerminationRequestedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Value context =
        runtimeContext(rewriter, op.getLoc(), adaptor.getContext().front());
    rewriter.replaceOpWithNewOp<runtime::RTTerminationRequestedOp>(
        op, rewriter.getI1Type(), context);
    return success();
  }
};

template <typename Op, typename RuntimeOp>
class OpenConversion final : public SimIOConversion<Op> {
public:
  using SimIOConversion<Op>::SimIOConversion;

  LogicalResult
  matchAndRewrite(
      Op op, typename SimIOConversion<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    Value context = runtimeContext(rewriter, loc, adaptor.getContext().front());
    SmallVector<Value> operands{context, adaptor.getPath().front()};
    if constexpr (std::is_same_v<Op, sim::SimFileOpenOp>)
      operands.push_back(adaptor.getMode().front());
    auto call = RuntimeOp::create(
        rewriter, loc,
        TypeRange{runtime::StatusType::get(rewriter.getContext()),
                  runtime::FileDescriptorType::get(rewriter.getContext())},
        operands);
    Value bits = descriptorBits(rewriter, loc, call.getDescriptor());
    Value zero = iConstant(rewriter, loc, rewriter.getI32Type(), 0);
    rewriter.replaceOp(op,
                       sentinel(rewriter, loc, call.getStatus(), bits, zero));
    return success();
  }
};

template <typename Op, typename RuntimeOp>
class DescriptorStatusConversion final : public SimIOConversion<Op> {
public:
  using SimIOConversion<Op>::SimIOConversion;

  LogicalResult
  matchAndRewrite(
      Op op, typename SimIOConversion<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    Value context = runtimeContext(rewriter, loc, adaptor.getContext().front());
    Value fd = descriptor(rewriter, loc, adaptor.getDescriptor().front());
    Value status = RuntimeOp::create(
        rewriter, loc, runtime::StatusType::get(rewriter.getContext()), context,
        fd);
    Value zero = iConstant(rewriter, loc, rewriter.getI32Type(), 0);
    Value eof = iConstant(rewriter, loc, rewriter.getI32Type(), -1);
    rewriter.replaceOp(op, sentinel(rewriter, loc, status, zero, eof));
    return success();
  }
};

template <typename Op, typename RuntimeOp>
class DescriptorTaskConversion final : public SimIOConversion<Op> {
public:
  using SimIOConversion<Op>::SimIOConversion;

  LogicalResult
  matchAndRewrite(
      Op op, typename SimIOConversion<Op>::OneToNOpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    Value context = runtimeContext(rewriter, loc, adaptor.getContext().front());
    Value fd = descriptor(rewriter, loc, adaptor.getDescriptor().front());
    Value status = RuntimeOp::create(
        rewriter, loc, runtime::StatusType::get(rewriter.getContext()), context,
        fd);
    sim::SimStatusCheckOp::create(rewriter, loc, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class GetcConversion final : public SimIOConversion<sim::SimFileGetcOp> {
public:
  using SimIOConversion::SimIOConversion;

  LogicalResult
  matchAndRewrite(sim::SimFileGetcOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    auto call = runtime::RTFileGetcOp::create(
        rewriter, loc,
        TypeRange{runtime::StatusType::get(rewriter.getContext()),
                  rewriter.getI8Type()},
        runtimeContext(rewriter, loc, adaptor.getContext().front()),
        descriptor(rewriter, loc, adaptor.getDescriptor().front()));
    Value byte = arith::ExtUIOp::create(rewriter, loc, rewriter.getI32Type(),
                                        call.getByte());
    Value eof = iConstant(rewriter, loc, rewriter.getI32Type(), -1);
    rewriter.replaceOp(op,
                       sentinel(rewriter, loc, call.getStatus(), byte, eof));
    return success();
  }
};

class UngetcConversion final
    : public SimIOConversion<sim::SimFileUngetcOp> {
public:
  using SimIOConversion::SimIOConversion;

  LogicalResult
  matchAndRewrite(sim::SimFileUngetcOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    Value byte = adaptor.getByte().front();
    Value narrowed = arith::TruncIOp::create(rewriter, loc, rewriter.getI8Type(),
                                             byte);
    Value status = runtime::RTFileUngetcOp::create(
        rewriter, loc, runtime::StatusType::get(rewriter.getContext()),
        runtimeContext(rewriter, loc, adaptor.getContext().front()),
        descriptor(rewriter, loc, adaptor.getDescriptor().front()), narrowed);
    Value zero = iConstant(rewriter, loc, rewriter.getI32Type(), 0);
    Value eof = iConstant(rewriter, loc, rewriter.getI32Type(), -1);
    rewriter.replaceOp(op, sentinel(rewriter, loc, status, zero, eof));
    return success();
  }
};

class GetlineConversion final
    : public SimIOConversion<sim::SimFileGetlineOp> {
public:
  using SimIOConversion::SimIOConversion;

  LogicalResult
  matchAndRewrite(sim::SimFileGetlineOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    unsigned width = cast<IntegerType>(op.getData().getType()).getWidth();
    // IEEE 1800 excludes a most-significant partial byte when determining a
    // packed $fgets destination's capacity.
    uint64_t maxBytes = width / 8;
    Value limit = iConstant(rewriter, loc, rewriter.getI64Type(), maxBytes);
    auto call = runtime::RTFileGetlineOp::create(
        rewriter, loc,
        TypeRange{runtime::StatusType::get(rewriter.getContext()),
                  runtime::BufferType::get(rewriter.getContext())},
        runtimeContext(rewriter, loc, adaptor.getContext().front()),
        descriptor(rewriter, loc, adaptor.getDescriptor().front()), limit);
    Value count = runtime::RTBytesSizeOp::create(
        rewriter, loc, rewriter.getI64Type(), call.getLine());
    Value packed = runtime::RTPackedFromBytesOp::create(
        rewriter, loc, op.getData().getType(), call.getLine(), count, false);
    runtime::RTBufferReleaseOp::create(rewriter, loc, call.getLine());
    Value count32 = arith::TruncIOp::create(rewriter, loc,
                                            rewriter.getI32Type(), count);
    Value zero = iConstant(rewriter, loc, rewriter.getI32Type(), 0);
    Value result = sentinel(rewriter, loc, call.getStatus(), count32, zero);
    rewriter.replaceOp(op, ValueRange{packed, result});
    return success();
  }
};

class ReadPackedConversion final
    : public SimIOConversion<sim::SimFileReadPackedOp> {
public:
  using SimIOConversion::SimIOConversion;

  LogicalResult
  matchAndRewrite(sim::SimFileReadPackedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    unsigned width = cast<IntegerType>(op.getData().getType()).getWidth();
    uint64_t maxBytes = (static_cast<uint64_t>(width) + 7) / 8;
    Value scratch = runtime::RTScratchOp::create(
        rewriter, loc,
        runtime::MutableByteSpanType::get(rewriter.getContext()), maxBytes);
    auto call = runtime::RTFileReadOp::create(
        rewriter, loc,
        TypeRange{runtime::StatusType::get(rewriter.getContext()),
                  rewriter.getI64Type()},
        runtimeContext(rewriter, loc, adaptor.getContext().front()),
        descriptor(rewriter, loc, adaptor.getDescriptor().front()), scratch);
    Value packed = runtime::RTPackedFromBytesOp::create(
        rewriter, loc, op.getData().getType(), scratch, call.getRead(), true);
    Value count = arith::TruncIOp::create(rewriter, loc, rewriter.getI32Type(),
                                          call.getRead());
    Value zero = iConstant(rewriter, loc, rewriter.getI32Type(), 0);
    Value result = sentinel(rewriter, loc, call.getStatus(), count, zero);
    rewriter.replaceOp(op, ValueRange{packed, result});
    return success();
  }
};

class EofConversion final : public SimIOConversion<sim::SimFileEofOp> {
public:
  using SimIOConversion::SimIOConversion;

  LogicalResult
  matchAndRewrite(sim::SimFileEofOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    auto call = runtime::RTFileEofOp::create(
        rewriter, loc,
        TypeRange{runtime::StatusType::get(rewriter.getContext()),
                  rewriter.getI32Type()},
        runtimeContext(rewriter, loc, adaptor.getContext().front()),
        descriptor(rewriter, loc, adaptor.getDescriptor().front()));
    Value zero = iConstant(rewriter, loc, rewriter.getI32Type(), 0);
    rewriter.replaceOp(
        op, sentinel(rewriter, loc, call.getStatus(), call.getIsEof(), zero));
    return success();
  }
};

class SeekConversion final : public SimIOConversion<sim::SimFileSeekOp> {
public:
  using SimIOConversion::SimIOConversion;

  LogicalResult
  matchAndRewrite(sim::SimFileSeekOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    Value status = runtime::RTFileSeekOp::create(
        rewriter, loc, runtime::StatusType::get(rewriter.getContext()),
        runtimeContext(rewriter, loc, adaptor.getContext().front()),
        descriptor(rewriter, loc, adaptor.getDescriptor().front()),
        adaptor.getOffset().front(), adaptor.getOrigin().front());
    Value zero = iConstant(rewriter, loc, rewriter.getI32Type(), 0);
    Value failure = iConstant(rewriter, loc, rewriter.getI32Type(), -1);
    rewriter.replaceOp(op, sentinel(rewriter, loc, status, zero, failure));
    return success();
  }
};

class TellConversion final : public SimIOConversion<sim::SimFileTellOp> {
public:
  using SimIOConversion::SimIOConversion;

  LogicalResult
  matchAndRewrite(sim::SimFileTellOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    auto call = runtime::RTFileTellOp::create(
        rewriter, loc,
        TypeRange{runtime::StatusType::get(rewriter.getContext()),
                  rewriter.getI64Type()},
        runtimeContext(rewriter, loc, adaptor.getContext().front()),
        descriptor(rewriter, loc, adaptor.getDescriptor().front()));
    Value failure = iConstant(rewriter, loc, rewriter.getI64Type(), -1);
    rewriter.replaceOp(
        op, sentinel(rewriter, loc, call.getStatus(), call.getOffset(), failure));
    return success();
  }
};

class ConvertObeliskSimToRuntimePass final
    : public impl::ConvertObeliskSimToRuntimePassBase<
          ConvertObeliskSimToRuntimePass> {
public:
  void runOnOperation() override {
    MLIRContext &context = getContext();
    SimulationToStandardTypeConverter converter;
    addSimulationPackedAggregateTypeConversions(converter);
    addSimulationToRuntimeTypeConversions(converter);
    RewritePatternSet patterns(&context);
    populateSimulationToStandardPatterns(converter, patterns);
    populateSimulationPackedAggregateViewPatterns(converter, patterns);
    populateSimulationToRuntimePatterns(converter, patterns);

    ConversionTarget target(context);
    target.addIllegalOp<
        sim::SimBytesConstantOp, sim::SimFinishOp, sim::SimStopOp,
        sim::SimFatalOp, sim::SimTerminationRequestedOp, sim::SimDisplayOp,
        sim::SimFileOpenMCDOp, sim::SimFileOpenOp, sim::SimFileCloseOp,
        sim::SimFileFlushOp, sim::SimFileGetcOp, sim::SimFileUngetcOp,
        sim::SimFileGetlineOp, sim::SimFileReadPackedOp, sim::SimFileEofOp,
        sim::SimFileSeekOp, sim::SimFileTellOp, sim::SimFileRewindOp>();
    target.addLegalDialect<runtime::ObeliskRuntimeDialect,
                           arith::ArithDialect>();
    target.addLegalOp<ModuleOp, sim::SimContextRuntimeOp,
                      sim::SimStatusCheckOp>();
    target.addDynamicallyLegalOp<func::FuncOp>([&](func::FuncOp function) {
      return converter.isSignatureLegal(function.getFunctionType()) &&
             converter.isLegal(&function.getBody());
    });
    target.markUnknownOpDynamicallyLegal(
        [&](Operation *operation) { return converter.isLegal(operation); });
    if (failed(applyPartialConversion(getOperation(), target,
                                      std::move(patterns))))
      signalPassFailure();
  }
};

} // namespace

void populateSimulationToRuntimePatterns(const TypeConverter &converter,
                                         RewritePatternSet &patterns) {
  MLIRContext *context = patterns.getContext();
  patterns.add<BytesConstantConversion, DisplayConversion, GetcConversion,
               UngetcConversion, GetlineConversion, ReadPackedConversion,
               EofConversion, SeekConversion, TellConversion>(converter,
                                                               context);
  patterns.add<TerminationConversion<sim::SimFinishOp, runtime::RTFinishOp>,
               TerminationConversion<sim::SimStopOp, runtime::RTFinishOp>,
               TerminationConversion<sim::SimFatalOp, runtime::RTFatalOp>>(
      converter, context);
  patterns.add<TerminationRequestedConversion>(converter, context);
  patterns.add<OpenConversion<sim::SimFileOpenMCDOp,
                              runtime::RTFileOpenMCDOp>,
               OpenConversion<sim::SimFileOpenOp, runtime::RTFileOpenOp>,
               DescriptorTaskConversion<sim::SimFileCloseOp,
                                        runtime::RTFileCloseOp>,
               DescriptorTaskConversion<sim::SimFileFlushOp,
                                        runtime::RTFileFlushOp>,
               DescriptorStatusConversion<sim::SimFileRewindOp,
                                          runtime::RTFileRewindOp>>(converter,
                                                                   context);
}

} // namespace obelisk
