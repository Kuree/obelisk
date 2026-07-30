//===- SimulationTimeLowering.cpp - Normalize simulation time -----------===//

#include "obelisk/Conversion/SimulationTimeLowering.h"

#include "obelisk/Conversion/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#include "llvm/ADT/APInt.h"

#include <cstdint>
#include <limits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMLOWERREALCONVERSIONSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

constexpr unsigned kSimulationTimeWidth = 64;
constexpr double kSimulationTimeUpperBound = 0x1p64;

struct IEEE754Double {
  static constexpr unsigned fractionBits = 52;
  static constexpr unsigned exponentBits = 11;
  static constexpr unsigned signBit = fractionBits + exponentBits;
  static constexpr uint64_t exponentMask = (uint64_t{1} << exponentBits) - 1;
  static constexpr uint64_t exponentBias =
      (uint64_t{1} << (exponentBits - 1)) - 1;
  static constexpr uint64_t implicitBit = uint64_t{1} << fractionBits;
  static constexpr uint64_t fractionMask = implicitBit - 1;
  static constexpr unsigned overflowThresholdWidth = exponentBias + 1;
  // Round-to-nearest overflows at max-finite plus half of its ULP.
  static constexpr unsigned overflowThresholdLowBit =
      overflowThresholdWidth - fractionBits - 2;
};

class TimeConstantLowering final
    : public OpRewritePattern<sim::SimTimeConstantOp> {
public:
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(sim::SimTimeConstantOp op,
                                PatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<arith::ConstantOp>(
        op, rewriter.getI64Type(), rewriter.getI64IntegerAttr(op.getValue()));
    return success();
  }
};

class TimeAddLowering final : public OpRewritePattern<sim::SimTimeAddOp> {
public:
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(sim::SimTimeAddOp op,
                                PatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<arith::AddIOp>(op, rewriter.getI64Type(),
                                               op.getLhs(), op.getRhs());
    return success();
  }
};

class TimeToRealLowering final : public OpRewritePattern<sim::SimTimeToRealOp> {
public:
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(sim::SimTimeToRealOp op,
                                PatternRewriter &rewriter) const override {
    Location location = op.getLoc();
    Value input = arith::UIToFPOp::create(rewriter, location,
                                          rewriter.getF64Type(), op.getInput());
    Value scale = arith::ConstantOp::create(
        rewriter, location, rewriter.getF64Type(),
        rewriter.getF64FloatAttr(static_cast<double>(op.getScale())));
    rewriter.replaceOpWithNewOp<arith::DivFOp>(op, input, scale);
    return success();
  }
};

class TimeFromRealLowering final
    : public OpRewritePattern<sim::SimTimeFromRealOp> {
public:
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(sim::SimTimeFromRealOp op,
                                PatternRewriter &rewriter) const override {
    Location location = op.getLoc();
    Type f64 = rewriter.getF64Type();
    Type i64 = rewriter.getI64Type();
    double factor = static_cast<double>(op.getScale()) /
                    static_cast<double>(op.getQuantum());
    Value zero = arith::ConstantOp::create(rewriter, location, f64,
                                           rewriter.getF64FloatAttr(0.0));
    Value nonnegative = arith::CmpFOp::create(
        rewriter, location, arith::CmpFPredicate::OGE, op.getInput(), zero);
    Value clamped = arith::SelectOp::create(rewriter, location, nonnegative,
                                            op.getInput(), zero);
    Value factorValue = arith::ConstantOp::create(
        rewriter, location, f64, rewriter.getF64FloatAttr(factor));
    Value steps =
        arith::MulFOp::create(rewriter, location, clamped, factorValue);
    Value half = arith::ConstantOp::create(rewriter, location, f64,
                                           rewriter.getF64FloatAttr(0.5));
    Value rounded = arith::AddFOp::create(rewriter, location, steps, half);
    Value upperBound = arith::ConstantOp::create(
        rewriter, location, f64,
        rewriter.getF64FloatAttr(kSimulationTimeUpperBound));
    Value representable = arith::CmpFOp::create(
        rewriter, location, arith::CmpFPredicate::OLT, rounded, upperBound);
    Value safeRounded = arith::SelectOp::create(rewriter, location,
                                                representable, rounded, zero);
    Value ticks = arith::FPToUIOp::create(rewriter, location, i64, safeRounded);
    uint64_t maximumSteps = UINT64_MAX / op.getQuantum();
    Value maximum = arith::ConstantOp::create(
        rewriter, location, i64,
        rewriter.getIntegerAttr(i64,
                                APInt(kSimulationTimeWidth, maximumSteps)));
    Value exceedsMaximum = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::ugt, ticks, maximum);
    ticks = arith::SelectOp::create(rewriter, location, representable, ticks,
                                    maximum);
    ticks = arith::SelectOp::create(rewriter, location, exceedsMaximum, maximum,
                                    ticks);
    if (op.getQuantum() != 1) {
      Value quantum = arith::ConstantOp::create(
          rewriter, location, i64, rewriter.getI64IntegerAttr(op.getQuantum()));
      ticks = arith::MulIOp::create(rewriter, location, ticks, quantum);
    }
    rewriter.replaceOp(op, ticks);
    return success();
  }
};

class RealFromIntegerLowering final
    : public OpRewritePattern<sim::SimRealFromIntegerOp> {
public:
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(sim::SimRealFromIntegerOp op,
                                PatternRewriter &rewriter) const override {
    Location location = op.getLoc();
    Value input = op.getInput();
    if (op.getResult().getType().isF32()) {
      Value converted =
          op.getIsSigned()
              ? Value(arith::SIToFPOp::create(rewriter, location,
                                              rewriter.getF32Type(), input))
              : Value(arith::UIToFPOp::create(rewriter, location,
                                              rewriter.getF32Type(), input));
      rewriter.replaceOp(op, converted);
      return success();
    }

    auto inputType = cast<IntegerType>(input.getType());
    Value zero = arith::ConstantOp::create(
        rewriter, location, inputType,
        rewriter.getIntegerAttr(inputType, APInt(inputType.getWidth(), 0)));
    Value magnitude = input;
    Value negative;
    if (op.getIsSigned()) {
      negative = arith::CmpIOp::create(rewriter, location,
                                       arith::CmpIPredicate::slt, input, zero);
      Value negated = arith::SubIOp::create(rewriter, location, zero, input);
      magnitude =
          arith::SelectOp::create(rewriter, location, negative, negated, input);
    }

    bool canOverflow =
        inputType.getWidth() > IEEE754Double::overflowThresholdWidth ||
        (!op.getIsSigned() &&
         inputType.getWidth() == IEEE754Double::overflowThresholdWidth);
    Value overflow;
    if (canOverflow) {
      APInt threshold = APInt::getBitsSet(
          inputType.getWidth(), IEEE754Double::overflowThresholdLowBit,
          IEEE754Double::overflowThresholdWidth);
      Value thresholdValue = arith::ConstantOp::create(
          rewriter, location, inputType,
          rewriter.getIntegerAttr(inputType, threshold));
      overflow =
          arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::uge,
                                magnitude, thresholdValue);
      magnitude = arith::SelectOp::create(rewriter, location, overflow, zero,
                                          magnitude);
    }

    Value converted = arith::UIToFPOp::create(rewriter, location,
                                              rewriter.getF64Type(), magnitude);
    if (op.getIsSigned()) {
      Value negated = arith::NegFOp::create(rewriter, location, converted);
      converted = arith::SelectOp::create(rewriter, location, negative, negated,
                                          converted);
    }
    if (canOverflow) {
      Value infinity = arith::ConstantOp::create(
          rewriter, location, rewriter.getF64Type(),
          rewriter.getF64FloatAttr(std::numeric_limits<double>::infinity()));
      if (op.getIsSigned()) {
        Value negativeInfinity =
            arith::NegFOp::create(rewriter, location, infinity);
        infinity = arith::SelectOp::create(rewriter, location, negative,
                                           negativeInfinity, infinity);
      }
      converted = arith::SelectOp::create(rewriter, location, overflow,
                                          infinity, converted);
    }
    rewriter.replaceOp(op, converted);
    return success();
  }
};

class RealToIntegerLowering final
    : public OpRewritePattern<sim::SimRealToIntegerOp> {
public:
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(sim::SimRealToIntegerOp op,
                                PatternRewriter &rewriter) const override {
    Location location = op.getLoc();
    auto resultType = cast<IntegerType>(op.getResult().getType());
    Type i64 = rewriter.getI64Type();
    auto integer = [&](Type type, uint64_t value) -> Value {
      return arith::ConstantOp::create(
          rewriter, location, type,
          rewriter.getIntegerAttr(
              type, APInt(cast<IntegerType>(type).getWidth(), value)));
    };
    auto compare = [&](arith::CmpIPredicate predicate, Value lhs,
                       Value rhs) -> Value {
      return arith::CmpIOp::create(rewriter, location, predicate, lhs, rhs);
    };
    auto select = [&](Value condition, Value trueValue,
                      Value falseValue) -> Value {
      return arith::SelectOp::create(rewriter, location, condition, trueValue,
                                     falseValue);
    };
    auto convertInteger = [&](Value value, IntegerType target) -> Value {
      auto source = cast<IntegerType>(value.getType());
      if (source == target)
        return value;
      if (source.getWidth() < target.getWidth())
        return arith::ExtUIOp::create(rewriter, location, target, value);
      return arith::TruncIOp::create(rewriter, location, target, value);
    };

    Value encoded =
        arith::BitcastOp::create(rewriter, location, i64, op.getInput());
    Value sign = arith::ShRUIOp::create(rewriter, location, encoded,
                                        integer(i64, IEEE754Double::signBit));
    sign = compare(arith::CmpIPredicate::ne, sign, integer(i64, 0));
    Value exponentBits = arith::AndIOp::create(
        rewriter, location,
        arith::ShRUIOp::create(rewriter, location, encoded,
                               integer(i64, IEEE754Double::fractionBits)),
        integer(i64, IEEE754Double::exponentMask));
    Value exponent =
        arith::SubIOp::create(rewriter, location, exponentBits,
                              integer(i64, IEEE754Double::exponentBias));
    Value mantissa = arith::OrIOp::create(
        rewriter, location,
        arith::AndIOp::create(rewriter, location, encoded,
                              integer(i64, IEEE754Double::fractionMask)),
        integer(i64, IEEE754Double::implicitBit));
    Value finite = compare(arith::CmpIPredicate::ne, exponentBits,
                           integer(i64, IEEE754Double::exponentMask));
    Value exponentIsMinusOne =
        compare(arith::CmpIPredicate::eq, exponent, integer(i64, -1));
    Value exponentNonnegative =
        compare(arith::CmpIPredicate::sge, exponent, integer(i64, 0));
    Value exponentBelowMantissa =
        compare(arith::CmpIPredicate::slt, exponent,
                integer(i64, IEEE754Double::fractionBits));
    Value small = arith::AndIOp::create(
        rewriter, location, finite,
        arith::AndIOp::create(rewriter, location, exponentNonnegative,
                              exponentBelowMantissa));
    Value smallShift = arith::SubIOp::create(
        rewriter, location, integer(i64, IEEE754Double::fractionBits),
        exponent);
    smallShift = select(small, smallShift, integer(i64, 1));
    Value truncated =
        arith::ShRUIOp::create(rewriter, location, mantissa, smallShift);
    Value roundShift =
        arith::SubIOp::create(rewriter, location, smallShift, integer(i64, 1));
    Value round = arith::AndIOp::create(
        rewriter, location,
        arith::ShRUIOp::create(rewriter, location, mantissa, roundShift),
        integer(i64, 1));
    Value smallMagnitude =
        arith::AddIOp::create(rewriter, location, truncated, round);
    smallMagnitude = select(exponentIsMinusOne, integer(i64, 1),
                            select(small, smallMagnitude, integer(i64, 0)));
    smallMagnitude = convertInteger(smallMagnitude, resultType);

    Value large = arith::AndIOp::create(
        rewriter, location, finite,
        compare(arith::CmpIPredicate::sge, exponent,
                integer(i64, IEEE754Double::fractionBits)));
    Value largeShift =
        arith::SubIOp::create(rewriter, location, exponent,
                              integer(i64, IEEE754Double::fractionBits));
    Value shiftFits = compare(arith::CmpIPredicate::ult, largeShift,
                              integer(i64, resultType.getWidth()));
    Value useLarge =
        arith::AndIOp::create(rewriter, location, large, shiftFits);
    largeShift = select(useLarge, largeShift, integer(i64, 0));
    Value resultMantissa = convertInteger(mantissa, resultType);
    Value resultShift = convertInteger(largeShift, resultType);
    Value largeMagnitude =
        arith::ShLIOp::create(rewriter, location, resultMantissa, resultShift);
    Value resultZero = integer(resultType, 0);
    largeMagnitude = select(useLarge, largeMagnitude, resultZero);

    Value useSmall =
        arith::OrIOp::create(rewriter, location, exponentIsMinusOne, small);
    Value magnitude = select(useSmall, smallMagnitude, largeMagnitude);
    Value negated =
        arith::SubIOp::create(rewriter, location, resultZero, magnitude);
    rewriter.replaceOp(op, select(sign, negated, magnitude));
    return success();
  }
};

class TimeScaleLowering final : public OpRewritePattern<sim::SimTimeScaleOp> {
public:
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(sim::SimTimeScaleOp op,
                                PatternRewriter &rewriter) const override {
    Value multiplier =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(op.getScale()));
    rewriter.replaceOpWithNewOp<arith::MulIOp>(op, op.getInput(), multiplier);
    return success();
  }
};

void populateSimulationRealConversionPatterns(RewritePatternSet &patterns) {
  patterns
      .add<TimeToRealLowering, RealFromIntegerLowering, RealToIntegerLowering>(
          patterns.getContext());
}

LogicalResult applyPatterns(RewritePatternSet &&patterns,
                            ArrayRef<Operation *> operations) {
  GreedyRewriteConfig config;
  config.setStrictness(GreedyRewriteStrictness::ExistingOps)
      .setRegionSimplificationLevel(GreedySimplifyRegionLevel::Disabled)
      .enableFolding(false)
      .enableConstantCSE(false);
  return applyOpPatternsGreedily(
      operations, FrozenRewritePatternSet(std::move(patterns)), config);
}

class ObeliskSimLowerRealConversionsPass final
    : public impl::ObeliskSimLowerRealConversionsPassBase<
          ObeliskSimLowerRealConversionsPass> {
public:
  void runOnOperation() override {
    RewritePatternSet patterns(&getContext());
    populateSimulationRealConversionPatterns(patterns);
    SmallVector<Operation *> operations;
    getOperation().walk([&](Operation *operation) {
      if (isa<sim::SimTimeToRealOp, sim::SimRealFromIntegerOp,
              sim::SimRealToIntegerOp>(operation))
        operations.push_back(operation);
    });
    if (failed(applyPatterns(std::move(patterns), operations)))
      signalPassFailure();
  }
};

} // namespace

void populateSimulationTimeLoweringPatterns(RewritePatternSet &patterns) {
  patterns.add<TimeConstantLowering, TimeAddLowering, TimeToRealLowering,
               TimeFromRealLowering, RealFromIntegerLowering,
               RealToIntegerLowering, TimeScaleLowering>(patterns.getContext());
}

LogicalResult lowerSimulationTimeOperations(Operation *root) {
  RewritePatternSet patterns(root->getContext());
  populateSimulationTimeLoweringPatterns(patterns);
  SmallVector<Operation *> operations;
  root->walk([&](Operation *operation) {
    if (isa<sim::SimTimeConstantOp, sim::SimTimeAddOp, sim::SimTimeScaleOp,
            sim::SimTimeToRealOp, sim::SimTimeFromRealOp,
            sim::SimRealFromIntegerOp, sim::SimRealToIntegerOp>(operation))
      operations.push_back(operation);
  });
  if (failed(applyPatterns(std::move(patterns), operations)))
    return failure();
  WalkResult remaining = root->walk([](Operation *operation) {
    return isa<sim::SimTimeConstantOp, sim::SimTimeAddOp, sim::SimTimeScaleOp,
               sim::SimTimeToRealOp, sim::SimTimeFromRealOp,
               sim::SimRealFromIntegerOp, sim::SimRealToIntegerOp>(operation)
               ? WalkResult::interrupt()
               : WalkResult::advance();
  });
  return remaining.wasInterrupted() ? failure() : success();
}

} // namespace obelisk
