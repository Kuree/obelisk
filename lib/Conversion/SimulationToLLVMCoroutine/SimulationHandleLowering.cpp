//===- SimulationHandleLowering.cpp - Native handle rewrites ------------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

#include <limits>

using namespace mlir;

namespace obelisk::detail {
namespace {

Value offsetNativeHandle(OpBuilder &builder, Location location, Value handle,
                         Value offset) {
  auto handleConstant = handle.getDefiningOp<arith::ConstantOp>();
  auto offsetConstant = offset.getDefiningOp<arith::ConstantOp>();
  auto handleValue = handleConstant
                         ? dyn_cast<IntegerAttr>(handleConstant.getValue())
                         : IntegerAttr{};
  auto offsetValue = offsetConstant
                         ? dyn_cast<IntegerAttr>(offsetConstant.getValue())
                         : IntegerAttr{};
  if (handleValue && offsetValue) {
    uint64_t folded =
        obelisk_rt_stable_handle_offset(handleValue.getValue().getZExtValue(),
                                        offsetValue.getValue().getSExtValue());
    return arith::ConstantOp::create(
        builder, location, builder.getI64Type(),
        builder.getI64IntegerAttr(static_cast<int64_t>(folded)));
  }
  return LLVM::CallOp::create(
             builder, location, TypeRange{builder.getI64Type()},
             SymbolRefAttr::get(builder.getContext(),
                                "obelisk_rt_v1_native_handle_offset"),
             ValueRange{handle, offset})
      .getResult();
}

Value isValidHandle(OpBuilder &builder, Location location, Value handle) {
  Value invalid = arith::ConstantOp::create(
      builder, location, builder.getI64Type(),
      builder.getI64IntegerAttr(static_cast<int64_t>(UINT64_MAX)));
  return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                               handle, invalid);
}

template <typename Op>
class ContextHandleConversion final : public OpConversionPattern<Op> {
public:
  ContextHandleConversion(const TypeConverter &converter, MLIRContext *context,
                          const DenseMap<uint64_t, uint64_t> &handles)
      : OpConversionPattern<Op>(converter, context), handles(handles) {}

  LogicalResult
  matchAndRewrite(Op op, typename OpConversionPattern<Op>::OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto found = handles.find(op.getId());
    if (found == handles.end())
      return rewriter.notifyMatchFailure(op, "descriptor has no native layout");
    Value value =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(found->second));
    rewriter.replaceOp(op, value);
    return success();
  }

private:
  const DenseMap<uint64_t, uint64_t> &handles;
};

class EventHandleConversion final
    : public OpConversionPattern<sim::SimContextEventOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimContextEventOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Value value =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(op.getId()));
    rewriter.replaceOp(op, value);
    return success();
  }
};

template <typename Op>
class StaticHandleExtractConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Value offset =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(op.getLowBit()));
    rewriter.replaceOp(op,
                       offsetNativeHandle(rewriter, op.getLoc(),
                                          adaptor.getInput().front(), offset));
    return success();
  }
};

template <typename Op>
class DynamicHandleExtractConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1 || adaptor.getLowBit().empty())
      return failure();
    Location location = op.getLoc();
    SignedI64Index convertedLow =
        resizeSignedIndexToI64(rewriter, location, adaptor.getLowBit().front());
    Value low = convertedLow.value;
    unsigned inputWidth =
        *sim::getPackedWidth(op.getInput().getType().getElementType());
    unsigned resultWidth =
        *sim::getPackedWidth(op.getResult().getType().getElementType());
    Value minimum = arith::ConstantOp::create(
        rewriter, location, rewriter.getI64Type(),
        rewriter.getI64IntegerAttr(-static_cast<int64_t>(resultWidth - 1)));
    Value maximum =
        arith::ConstantOp::create(rewriter, location, rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(inputWidth - 1));
    Value overlapsLow = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::sge, low, minimum);
    Value inRange = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::sle, low, maximum);
    Value valid =
        arith::AndIOp::create(rewriter, location, overlapsLow, inRange);
    valid = arith::AndIOp::create(rewriter, location, valid,
                                  convertedLow.representable);
    valid = arith::AndIOp::create(
        rewriter, location, valid,
        isValidHandle(rewriter, location, adaptor.getInput().front()));
    if (adaptor.getLowBit().size() == 2) {
      Value known = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::eq, adaptor.getLowBit()[1],
          arith::ConstantOp::create(
              rewriter, location, adaptor.getLowBit()[1].getType(),
              rewriter.getZeroAttr(adaptor.getLowBit()[1].getType())));
      valid = arith::AndIOp::create(rewriter, location, valid, known);
    }
    Value selected =
        offsetNativeHandle(rewriter, location, adaptor.getInput().front(), low);
    Value invalid = arith::ConstantOp::create(
        rewriter, location, rewriter.getI64Type(),
        rewriter.getI64IntegerAttr(static_cast<int64_t>(UINT64_MAX)));
    rewriter.replaceOp(op, arith::SelectOp::create(rewriter, location, valid,
                                                   selected, invalid));
    return success();
  }
};

template <typename Op>
class SubelementHandleConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Type current = op.getInput().getType().getElementType();
    uint64_t offset = 0;
    for (int64_t rawIndex : op.getIndices()) {
      if (rawIndex < 0 || static_cast<uint64_t>(rawIndex) >=
                              sim::getAggregateNumElements(current))
        return failure();
      unsigned index = static_cast<unsigned>(rawIndex);
      auto subelement = sim::getAggregateProvenanceSubelement(current, index);
      if (!subelement ||
          subelement->first > std::numeric_limits<uint64_t>::max() - offset)
        return failure();
      offset += subelement->first;
      current = sim::getAggregateElementType(current, index);
    }
    Value amount =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(offset));
    rewriter.replaceOp(op,
                       offsetNativeHandle(rewriter, op.getLoc(),
                                          adaptor.getInput().front(), amount));
    return success();
  }
};

template <typename Op>
class ArrayElementHandleConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1 || adaptor.getIndex().empty())
      return failure();
    Type array = op.getInput().getType().getElementType();
    int64_t left;
    int64_t right;
    bool packed;
    Type element;
    if (auto type = dyn_cast<sim::PackedArrayType>(array)) {
      left = type.getLeft();
      right = type.getRight();
      packed = true;
      element = type.getElementType();
    } else if (auto type = dyn_cast<sim::UnpackedArrayType>(array)) {
      left = type.getLeft();
      right = type.getRight();
      packed = false;
      element = type.getElementType();
    } else {
      return failure();
    }
    std::optional<uint64_t> span = sim::getProvenanceSpan(element);
    uint64_t count = sim::getAggregateNumElements(array);
    if (!span || count == 0)
      return failure();

    Location location = op.getLoc();
    IntegerType i64 = rewriter.getI64Type();
    SignedI64Index convertedIndex =
        resizeSignedIndexToI64(rewriter, location, adaptor.getIndex().front());
    Value index = convertedIndex.value;
    Value leftValue = arith::ConstantOp::create(
        rewriter, location, i64, rewriter.getI64IntegerAttr(left));
    Value rightValue = arith::ConstantOp::create(
        rewriter, location, i64, rewriter.getI64IntegerAttr(right));
    Value valid;
    Value ordinal;
    if (left >= right) {
      Value atMostLeft = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::sle, index, leftValue);
      Value atLeastRight = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::sge, index, rightValue);
      valid =
          arith::AndIOp::create(rewriter, location, atMostLeft, atLeastRight);
      ordinal = arith::SubIOp::create(rewriter, location, leftValue, index);
    } else {
      Value atLeastLeft = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::sge, index, leftValue);
      Value atMostRight = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::sle, index, rightValue);
      valid =
          arith::AndIOp::create(rewriter, location, atLeastLeft, atMostRight);
      ordinal = arith::SubIOp::create(rewriter, location, index, leftValue);
    }
    valid = arith::AndIOp::create(rewriter, location, valid,
                                  convertedIndex.representable);
    if (adaptor.getIndex().size() == 2) {
      Value known = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::eq, adaptor.getIndex()[1],
          arith::ConstantOp::create(
              rewriter, location, adaptor.getIndex()[1].getType(),
              rewriter.getZeroAttr(adaptor.getIndex()[1].getType())));
      valid = arith::AndIOp::create(rewriter, location, valid, known);
    }
    valid = arith::AndIOp::create(
        rewriter, location, valid,
        isValidHandle(rewriter, location, adaptor.getInput().front()));
    if (packed) {
      Value last = arith::ConstantOp::create(
          rewriter, location, i64,
          rewriter.getI64IntegerAttr(static_cast<int64_t>(count - 1)));
      ordinal = arith::SubIOp::create(rewriter, location, last, ordinal);
    }
    Value stride = arith::ConstantOp::create(rewriter, location, i64,
                                             rewriter.getI64IntegerAttr(*span));
    Value offset = arith::MulIOp::create(rewriter, location, ordinal, stride);
    Value selected = offsetNativeHandle(rewriter, location,
                                        adaptor.getInput().front(), offset);
    Value invalid = arith::ConstantOp::create(
        rewriter, location, i64,
        rewriter.getI64IntegerAttr(static_cast<int64_t>(UINT64_MAX)));
    rewriter.replaceOp(op, arith::SelectOp::create(rewriter, location, valid,
                                                   selected, invalid));
    return success();
  }
};

} // namespace

void populateNativeHandleConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter,
    const DenseMap<uint64_t, uint64_t> &storageHandles,
    const DenseMap<uint64_t, uint64_t> &netHandles,
    const DenseMap<uint64_t, uint64_t> &driverHandles) {
  MLIRContext *context = patterns.getContext();
  patterns.add<ContextHandleConversion<sim::SimContextStorageOp>>(
      converter, context, storageHandles);
  patterns.add<ContextHandleConversion<sim::SimContextNetOp>>(
      converter, context, netHandles);
  patterns.add<ContextHandleConversion<sim::SimContextDriverOp>>(
      converter, context, driverHandles);
  patterns.add<
      EventHandleConversion,
      StaticHandleExtractConversion<sim::SimRefExtractOp>,
      StaticHandleExtractConversion<sim::SimNetExtractOp>,
      StaticHandleExtractConversion<sim::SimDriverExtractOp>,
      DynamicHandleExtractConversion<sim::SimRefDynExtractOp>,
      DynamicHandleExtractConversion<sim::SimDriverDynExtractOp>,
      SubelementHandleConversion<sim::SimRefSubelementOp>,
      SubelementHandleConversion<sim::SimDriverSubelementOp>,
      ArrayElementHandleConversion<sim::SimRefArrayElementOp>,
      ArrayElementHandleConversion<sim::SimDriverArrayElementOp>>(converter,
                                                                  context);
}

} // namespace obelisk::detail
