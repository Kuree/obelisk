//===- SimulationAggregateLowering.cpp - Aggregate rewrite patterns --===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/MathExtras.h"

#include <cstdint>
#include <optional>

using namespace mlir;

namespace obelisk::detail {

namespace {

std::optional<uint64_t> unionPayloadSpan(Type type) {
  if (auto packed = dyn_cast<sim::PackedUnionType>(type)) {
    std::optional<unsigned> width = sim::getPackedWidth(type);
    if (!width || packed.getTagBits() > *width)
      return std::nullopt;
    return static_cast<uint64_t>(*width - packed.getTagBits());
  }
  if (isa<sim::UnpackedUnionType>(type))
    return sim::getProvenanceSpan(type);
  return std::nullopt;
}

class PackedAggregateExtractConversion final
    : public OpConversionPattern<sim::SimAggregateExtractOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAggregateExtractOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto subelement = sim::getAggregateProvenanceSubelement(
        op.getInput().getType(), static_cast<unsigned>(op.getIndex()));
    std::optional<unsigned> resultWidth =
        nativeStateWidth(op.getResult().getType());
    if (!subelement || !resultWidth || adaptor.getInput().empty())
      return failure();
    SmallVector<Value> results;
    for (Value plane : adaptor.getInput()) {
      auto inputType = dyn_cast<IntegerType>(plane.getType());
      if (!inputType)
        return failure();
      Value selected = plane;
      if (subelement->first != 0) {
        Value amount = arith::ConstantOp::create(
            rewriter, op.getLoc(), inputType,
            rewriter.getIntegerAttr(
                inputType, APInt(inputType.getWidth(), subelement->first)));
        selected = arith::ShRUIOp::create(rewriter, op.getLoc(), plane, amount);
      }
      IntegerType outputType = rewriter.getIntegerType(*resultWidth);
      results.push_back(inputType == outputType
                            ? selected
                            : arith::TruncIOp::create(rewriter, op.getLoc(),
                                                      outputType, selected)
                                  .getResult());
    }
    if (containsLogic(op.getResult().getType())) {
      if (results.size() != 2)
        return failure();
    } else if (results.size() > 1) {
      results.resize(1);
    }
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class PackedAggregateInsertConversion final
    : public OpConversionPattern<sim::SimAggregateInsertOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAggregateInsertOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    std::optional<unsigned> resultWidth =
        nativeStateWidth(op.getResult().getType());
    auto subelement = sim::getAggregateProvenanceSubelement(
        op.getInput().getType(), static_cast<unsigned>(op.getIndex()));
    if (!resultWidth || !subelement || adaptor.getInput().empty() ||
        adaptor.getReplacement().empty() || subelement->first > *resultWidth ||
        subelement->second > *resultWidth - subelement->first)
      return failure();
    IntegerType planeType = rewriter.getIntegerType(*resultWidth);
    APInt fieldMask = APInt::getBitsSet(*resultWidth, subelement->first,
                                        subelement->first + subelement->second);
    Value keepMask = arith::ConstantOp::create(
        rewriter, op.getLoc(), planeType,
        rewriter.getIntegerAttr(planeType, ~fieldMask));
    Value shift = arith::ConstantOp::create(
        rewriter, op.getLoc(), planeType,
        rewriter.getIntegerAttr(planeType,
                                APInt(*resultWidth, subelement->first)));
    Value zero = arith::ConstantOp::create(
        rewriter, op.getLoc(), planeType,
        rewriter.getIntegerAttr(planeType, APInt::getZero(*resultWidth)));
    SmallVector<Value> results;
    for (auto [index, input] : llvm::enumerate(adaptor.getInput())) {
      auto inputType = dyn_cast<IntegerType>(input.getType());
      if (!inputType || inputType != planeType)
        return failure();
      Value replacement = zero;
      if (index < adaptor.getReplacement().size()) {
        Value source = adaptor.getReplacement()[index];
        auto sourceType = dyn_cast<IntegerType>(source.getType());
        if (!sourceType || sourceType.getWidth() > *resultWidth)
          return failure();
        replacement = sourceType == planeType
                          ? source
                          : arith::ExtUIOp::create(rewriter, op.getLoc(),
                                                   planeType, source)
                                .getResult();
        if (subelement->first != 0)
          replacement =
              arith::ShLIOp::create(rewriter, op.getLoc(), replacement, shift);
      }
      Value preserved =
          arith::AndIOp::create(rewriter, op.getLoc(), input, keepMask);
      results.push_back(
          arith::OrIOp::create(rewriter, op.getLoc(), preserved, replacement));
    }
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class PackedAggregateConstructConversion final
    : public OpConversionPattern<sim::SimAggregateConstructOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAggregateConstructOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    std::optional<unsigned> resultWidth =
        nativeStateWidth(op.getResult().getType());
    if (!resultWidth || adaptor.getElements().size() != op.getElements().size())
      return failure();
    IntegerType outputType = rewriter.getIntegerType(*resultWidth);
    auto zero = [&] {
      return arith::ConstantOp::create(
          rewriter, op.getLoc(), outputType,
          rewriter.getIntegerAttr(outputType, APInt::getZero(*resultWidth)));
    };
    auto place = [&](Value destination, Value source,
                     uint64_t offset) -> FailureOr<Value> {
      auto sourceType = dyn_cast<IntegerType>(source.getType());
      if (!sourceType || sourceType.getWidth() > outputType.getWidth())
        return failure();
      Value extended = sourceType == outputType
                           ? source
                           : arith::ExtUIOp::create(rewriter, op.getLoc(),
                                                    outputType, source);
      if (offset != 0) {
        Value amount = arith::ConstantOp::create(
            rewriter, op.getLoc(), outputType,
            rewriter.getIntegerAttr(outputType,
                                    APInt(outputType.getWidth(), offset)));
        extended =
            arith::ShLIOp::create(rewriter, op.getLoc(), extended, amount);
      }
      return arith::OrIOp::create(rewriter, op.getLoc(), destination, extended)
          .getResult();
    };

    Value value = zero();
    Value unknown = zero();
    for (auto [index, converted] : llvm::enumerate(adaptor.getElements())) {
      auto subelement = sim::getAggregateProvenanceSubelement(
          op.getResult().getType(), index);
      if (!subelement || converted.empty())
        return failure();
      FailureOr<Value> placed =
          place(value, converted.front(), subelement->first);
      if (failed(placed))
        return failure();
      value = *placed;
      if (converted.size() == 2) {
        placed = place(unknown, converted[1], subelement->first);
        if (failed(placed))
          return failure();
        unknown = *placed;
      }
    }
    SmallVector<Value> results{value};
    if (containsLogic(op.getResult().getType()))
      results.push_back(unknown);
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class AggregateDynamicExtractConversion final
    : public OpConversionPattern<sim::SimArrayDynExtractOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimArrayDynExtractOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().empty() || adaptor.getIndex().empty())
      return failure();
    Type array = op.getInput().getType();
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
    std::optional<unsigned> resultWidth = nativeStateWidth(element);
    std::optional<uint64_t> span = sim::getProvenanceSpan(element);
    uint64_t count = sim::getAggregateNumElements(array);
    if (!resultWidth || !span || count == 0)
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
      valid = arith::AndIOp::create(
          rewriter, location,
          arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::sle,
                                index, leftValue),
          arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::sge,
                                index, rightValue));
      ordinal = arith::SubIOp::create(rewriter, location, leftValue, index);
    } else {
      valid = arith::AndIOp::create(
          rewriter, location,
          arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::sge,
                                index, leftValue),
          arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::sle,
                                index, rightValue));
      ordinal = arith::SubIOp::create(rewriter, location, index, leftValue);
    }
    // The declared bounds fit in i64, but truncating a wider source index can
    // wrap an out-of-range value into that range.
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
    if (packed) {
      Value last = arith::ConstantOp::create(
          rewriter, location, i64,
          rewriter.getI64IntegerAttr(static_cast<int64_t>(count - 1)));
      ordinal = arith::SubIOp::create(rewriter, location, last, ordinal);
    }
    Value safeOrdinal = arith::SelectOp::create(
        rewriter, location, valid, ordinal,
        arith::ConstantOp::create(rewriter, location, i64,
                                  rewriter.getI64IntegerAttr(0)));
    Value offset = arith::MulIOp::create(
        rewriter, location, safeOrdinal,
        arith::ConstantOp::create(rewriter, location, i64,
                                  rewriter.getI64IntegerAttr(*span)));
    IntegerType outputType = rewriter.getIntegerType(*resultWidth);
    SmallVector<Value> results;
    for (auto [planeIndex, plane] : llvm::enumerate(adaptor.getInput())) {
      auto planeType = cast<IntegerType>(plane.getType());
      Value amount = resizeNativeInteger(rewriter, location, offset, planeType);
      Value shifted = arith::ShRUIOp::create(rewriter, location, plane, amount);
      Value extracted =
          planeType == outputType
              ? shifted
              : arith::TruncIOp::create(rewriter, location, outputType, shifted)
                    .getResult();
      APInt fallback = planeIndex == 1 && containsLogic(element)
                           ? APInt::getAllOnes(*resultWidth)
                           : APInt::getZero(*resultWidth);
      Value defaultValue = arith::ConstantOp::create(
          rewriter, location, outputType,
          rewriter.getIntegerAttr(outputType, fallback));
      results.push_back(arith::SelectOp::create(rewriter, location, valid,
                                                extracted, defaultValue));
    }
    if (containsLogic(element)) {
      if (results.size() != 2)
        return failure();
    } else if (results.size() > 1) {
      results.resize(1);
    }
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class AggregateDefaultConversion final
    : public OpConversionPattern<sim::SimAggregateDefaultOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAggregateDefaultOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    std::optional<unsigned> width = nativeStateWidth(op.getResult().getType());
    if (!width)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    Value zero = arith::ConstantOp::create(
        rewriter, op.getLoc(), plane,
        rewriter.getIntegerAttr(plane, APInt::getZero(*width)));
    SmallVector<Value> results{zero};
    if (containsLogic(op.getResult().getType()))
      results.push_back(arith::ConstantOp::create(
          rewriter, op.getLoc(), plane,
          rewriter.getIntegerAttr(plane, APInt::getAllOnes(*width))));
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class UnionConstructConversion final
    : public OpConversionPattern<sim::SimUnionConstructOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimUnionConstructOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getValue().empty())
      return failure();
    Type unionType = op.getResult().getType();
    std::optional<unsigned> width = nativeStateWidth(unionType);
    std::optional<uint64_t> payloadSpan = unionPayloadSpan(unionType);
    auto selected = sim::getAggregateProvenanceSubelement(
        unionType, static_cast<unsigned>(op.getIndex()));
    if (!width || !payloadSpan || !selected || *payloadSpan > *width ||
        selected->first > *payloadSpan ||
        selected->second > *payloadSpan - selected->first)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    auto extend = [&](Value value) -> FailureOr<Value> {
      auto type = dyn_cast<IntegerType>(value.getType());
      if (!type || type.getWidth() > plane.getWidth())
        return failure();
      if (type == plane)
        return value;
      return arith::ExtUIOp::create(rewriter, op.getLoc(), plane, value)
          .getResult();
    };
    FailureOr<Value> value = extend(adaptor.getValue().front());
    if (failed(value))
      return failure();
    Value unknown = arith::ConstantOp::create(
        rewriter, op.getLoc(), plane,
        rewriter.getIntegerAttr(plane, APInt::getZero(*width)));
    if (adaptor.getValue().size() == 2) {
      FailureOr<Value> convertedUnknown = extend(adaptor.getValue()[1]);
      if (failed(convertedUnknown))
        return failure();
      unknown = *convertedUnknown;
    }
    if (selected->first != 0) {
      Value amount = arith::ConstantOp::create(
          rewriter, op.getLoc(), plane,
          rewriter.getIntegerAttr(
              plane, APInt(plane.getWidth(), selected->first)));
      *value = arith::ShLIOp::create(rewriter, op.getLoc(), *value, amount);
      unknown =
          arith::ShLIOp::create(rewriter, op.getLoc(), unknown, amount);
    }

    uint64_t tag = 0;
    unsigned tagBits = 0;
    if (auto packed = dyn_cast<sim::PackedUnionType>(unionType);
        packed && packed.getIsTagged()) {
      tag = op.getIndex();
      tagBits = packed.getTagBits();
    } else if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(unionType);
               unpacked && unpacked.getIsTagged()) {
      tag = static_cast<uint64_t>(op.getIndex()) + 1;
      tagBits = llvm::Log2_64_Ceil(
          static_cast<uint64_t>(sim::getAggregateNumElements(unionType)) + 1);
    }
    if (tagBits != 0) {
      APInt tagValue(*width, tag);
      tagValue <<= *payloadSpan;
      Value encoded =
          arith::ConstantOp::create(rewriter, op.getLoc(), plane,
                                    rewriter.getIntegerAttr(plane, tagValue));
      *value = arith::OrIOp::create(rewriter, op.getLoc(), *value, encoded);
    }
    SmallVector<Value> results{*value};
    if (containsLogic(unionType))
      results.push_back(unknown);
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class UnionExtractConversion final
    : public OpConversionPattern<sim::SimUnionExtractOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimUnionExtractOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    std::optional<unsigned> resultWidth =
        nativeStateWidth(op.getResult().getType());
    auto selected = sim::getAggregateProvenanceSubelement(
        op.getInput().getType(), static_cast<unsigned>(op.getIndex()));
    if (!resultWidth || !selected || adaptor.getInput().empty())
      return failure();
    IntegerType resultType = rewriter.getIntegerType(*resultWidth);
    SmallVector<Value> results;
    for (Value plane : adaptor.getInput()) {
      auto inputType = dyn_cast<IntegerType>(plane.getType());
      if (!inputType || inputType.getWidth() < *resultWidth)
        return failure();
      Value selectedPlane = plane;
      if (selected->first != 0) {
        Value amount = arith::ConstantOp::create(
            rewriter, op.getLoc(), inputType,
            rewriter.getIntegerAttr(
                inputType, APInt(inputType.getWidth(), selected->first)));
        selectedPlane =
            arith::ShRUIOp::create(rewriter, op.getLoc(), plane, amount);
      }
      results.push_back(inputType == resultType
                            ? selectedPlane
                            : arith::TruncIOp::create(rewriter, op.getLoc(),
                                                      resultType,
                                                      selectedPlane)
                                  .getResult());
    }
    if (containsLogic(op.getResult().getType())) {
      if (results.size() != 2)
        return failure();
    } else if (results.size() > 1) {
      results.resize(1);
    }
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class UnionIsActiveConversion final
    : public OpConversionPattern<sim::SimUnionIsActiveOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimUnionIsActiveOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type unionType = op.getInput().getType();
    std::optional<uint64_t> payloadSpan = unionPayloadSpan(unionType);
    if (!payloadSpan || adaptor.getInput().empty())
      return failure();
    unsigned tagBits = 0;
    uint64_t expected = 0;
    if (auto packed = dyn_cast<sim::PackedUnionType>(unionType)) {
      tagBits = packed.getTagBits();
      expected = op.getIndex();
    } else if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(unionType)) {
      tagBits = llvm::Log2_64_Ceil(
          static_cast<uint64_t>(sim::getAggregateNumElements(unionType)) + 1);
      expected = static_cast<uint64_t>(op.getIndex()) + 1;
    }
    if (tagBits == 0) {
      Value active =
          arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI1Type(),
                                    rewriter.getBoolAttr(true));
      rewriter.replaceOp(op, active);
      return success();
    }
    auto extractTag = [&](Value plane) -> FailureOr<Value> {
      auto planeType = dyn_cast<IntegerType>(plane.getType());
      if (!planeType || *payloadSpan + tagBits > planeType.getWidth())
        return failure();
      Value shifted = plane;
      if (*payloadSpan != 0) {
        Value amount = arith::ConstantOp::create(
            rewriter, op.getLoc(), planeType,
            rewriter.getIntegerAttr(planeType, *payloadSpan));
        shifted = arith::ShRUIOp::create(rewriter, op.getLoc(), plane, amount);
      }
      auto tagType = rewriter.getIntegerType(tagBits);
      if (tagType != planeType)
        shifted =
            arith::TruncIOp::create(rewriter, op.getLoc(), tagType, shifted);
      return shifted;
    };
    FailureOr<Value> tag = extractTag(adaptor.getInput().front());
    if (failed(tag))
      return failure();
    auto tagType = cast<IntegerType>((*tag).getType());
    Value expectedTag =
        arith::ConstantOp::create(rewriter, op.getLoc(), tagType,
                                  rewriter.getIntegerAttr(tagType, expected));
    Value equal = arith::CmpIOp::create(
        rewriter, op.getLoc(), arith::CmpIPredicate::eq, *tag, expectedTag);
    if (adaptor.getInput().size() == 2) {
      FailureOr<Value> unknownTag = extractTag(adaptor.getInput()[1]);
      if (failed(unknownTag))
        return failure();
      Value zero = arith::ConstantOp::create(
          rewriter, op.getLoc(), tagType, rewriter.getIntegerAttr(tagType, 0));
      Value known = arith::CmpIOp::create(
          rewriter, op.getLoc(), arith::CmpIPredicate::eq, *unknownTag, zero);
      equal = arith::AndIOp::create(rewriter, op.getLoc(), equal, known);
    }
    rewriter.replaceOp(op, equal);
    return success();
  }
};

} // namespace

void populateAggregateToLLVMConversionPatterns(RewritePatternSet &patterns,
                                               TypeConverter &converter) {
  MLIRContext *context = patterns.getContext();
  patterns.add<
      PackedAggregateExtractConversion, PackedAggregateInsertConversion,
      PackedAggregateConstructConversion, AggregateDynamicExtractConversion,
      AggregateDefaultConversion, UnionConstructConversion,
      UnionExtractConversion, UnionIsActiveConversion>(converter, context);
}

} // namespace obelisk::detail
