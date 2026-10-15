//===- LowerUnitRealConversionSystemCalls.cpp - Lower real conversions ---===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

using namespace mlir;

namespace obelisk::simlowering {

Value UnitLowering::truncateReal(Value real, Location location) {
  // Clear the IEEE fractional bits using operations shared by native and
  // bytecode execution. Preserve NaN/Inf so the result conversion retains
  // the destination's unknown plane; small values become signed zero.
  auto constant = [&](uint64_t bits) {
    return arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                     builder.getI64IntegerAttr(bits));
  };
  Value payload =
      arith::BitcastOp::create(builder, location, builder.getI64Type(), real);
  Value exponent = arith::AndIOp::create(
      builder, location,
      arith::ShRUIOp::create(builder, location, payload, constant(52)),
      constant(2047));
  Value small = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ult, exponent, constant(1023));
  Value fractional = arith::AndIOp::create(
      builder, location,
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::uge,
                            exponent, constant(1023)),
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                            exponent, constant(1075)));
  Value shift = arith::SelectOp::create(
      builder, location, fractional,
      arith::SubIOp::create(builder, location, constant(1075), exponent),
      constant(0));
  Value integral = arith::AndIOp::create(
      builder, location, payload,
      arith::ShLIOp::create(builder, location, constant(UINT64_MAX), shift));
  Value signedZero = arith::AndIOp::create(builder, location, payload,
                                           constant(UINT64_C(1) << 63));
  Value truncatedBits =
      arith::SelectOp::create(builder, location, small, signedZero, integral);
  return arith::BitcastOp::create(builder, location, builder.getF64Type(),
                                  truncatedBits);
}

FailureOr<Value>
UnitLowering::lowerRealConversionSystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef name = op.getCalleeName();

  auto convertResult = [&](Value value) -> FailureOr<Value> {
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type))
      return failure();
    return convert(value, *type, true, location);
  };

  if (name == "$itor") {
    if (children.size() != 1) {
      emitError(location) << "$itor requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    Location argument = getSemanticLocation(children.front());
    bool argumentSigned = isSignedNode(children.front());
    // The argument is declared as an integer, so a real one is rounded to an
    // integer -- and a non-finite one to zero -- before it converts back.
    if (isa<FloatType>((*input).getType())) {
      FailureOr<Value> rounded =
          convert(*input, builder.getI32Type(), argumentSigned, argument,
                  /*targetSigned=*/true);
      if (failed(rounded))
        return failure();
      input = *rounded;
      argumentSigned = true;
    }
    FailureOr<Value> result =
        convert(*input, builder.getF64Type(), argumentSigned, argument);
    if (failed(result))
      return failure();
    return convertResult(*result);
  }

  if (name == "$rtoi") {
    if (children.size() != 1) {
      emitError(location) << "$rtoi requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    return convertResult(truncateReal(*real, location));
  }

  if (name == "$bitstoreal") {
    if (children.size() != 1) {
      emitError(location) << "$bitstoreal requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> bits =
        convert(*input, builder.getI64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(bits))
      return failure();
    Value result = arith::BitcastOp::create(builder, location,
                                            builder.getF64Type(), *bits);
    return convertResult(result);
  }

  if (name == "$realtobits") {
    if (children.size() != 1) {
      emitError(location) << "$realtobits requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    Value result = arith::BitcastOp::create(builder, location,
                                            builder.getI64Type(), *real);
    return convertResult(result);
  }

  if (name == "$bitstoshortreal") {
    if (children.size() != 1) {
      emitError(location) << "$bitstoshortreal requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> bits =
        convert(*input, builder.getI32Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(bits))
      return failure();
    Value result = arith::BitcastOp::create(builder, location,
                                            builder.getF32Type(), *bits);
    return convertResult(result);
  }

  if (name == "$shortrealtobits") {
    if (children.size() != 1) {
      emitError(location) << "$shortrealtobits requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF32Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    Value result = arith::BitcastOp::create(builder, location,
                                            builder.getI32Type(), *real);
    return convertResult(result);
  }

  op.emitOpError("is not a supported real conversion system call");
  return failure();
}

} // namespace obelisk::simlowering
