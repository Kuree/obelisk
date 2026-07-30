//===- LowerUnitRealMathSystemCalls.cpp - Lower real math semantics ------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Math/IR/Math.h"

using namespace mlir;

namespace obelisk::simlowering {

FailureOr<Value>
UnitLowering::lowerRealMathSystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef name = op.getCalleeName();

  auto convertResult = [&](Value value) -> FailureOr<Value> {
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type))
      return failure();
    return convert(value, *type, true, location);
  };

  if (name == "$ceil") {
    if (children.size() != 1) {
      emitError(location) << "$ceil requires exactly one argument";
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
    Value result =
        math::CeilOp::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$floor") {
    if (children.size() != 1) {
      emitError(location) << "$floor requires exactly one argument";
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
    Value result =
        math::FloorOp::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$sqrt") {
    if (children.size() != 1) {
      emitError(location) << "$sqrt requires exactly one argument";
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
    Value result =
        math::SqrtOp::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$exp") {
    if (children.size() != 1) {
      emitError(location) << "$exp requires exactly one argument";
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
    Value result =
        math::ExpOp::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$ln") {
    if (children.size() != 1) {
      emitError(location) << "$ln requires exactly one argument";
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
    Value result =
        math::LogOp::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$log10") {
    if (children.size() != 1) {
      emitError(location) << "$log10 requires exactly one argument";
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
    Value result =
        math::Log10Op::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$pow") {
    if (children.size() != 2) {
      emitError(location) << "$pow requires exactly two arguments";
      return failure();
    }
    FailureOr<Value> base = lowerExpression(children[0]);
    if (failed(base))
      return failure();
    FailureOr<Value> exponent = lowerExpression(children[1]);
    if (failed(exponent))
      return failure();
    FailureOr<Value> realBase =
        convert(*base, builder.getF64Type(), isSignedNode(children[0]),
                getSemanticLocation(children[0]));
    if (failed(realBase))
      return failure();
    FailureOr<Value> realExponent =
        convert(*exponent, builder.getF64Type(), isSignedNode(children[1]),
                getSemanticLocation(children[1]));
    if (failed(realExponent))
      return failure();
    Value result = math::PowFOp::create(builder, location, builder.getF64Type(),
                                        *realBase, *realExponent);
    return convertResult(result);
  }

  if (name == "$atan2") {
    if (children.size() != 2) {
      emitError(location) << "$atan2 requires exactly two arguments";
      return failure();
    }
    FailureOr<Value> y = lowerExpression(children[0]);
    if (failed(y))
      return failure();
    FailureOr<Value> x = lowerExpression(children[1]);
    if (failed(x))
      return failure();
    FailureOr<Value> realY =
        convert(*y, builder.getF64Type(), isSignedNode(children[0]),
                getSemanticLocation(children[0]));
    if (failed(realY))
      return failure();
    FailureOr<Value> realX =
        convert(*x, builder.getF64Type(), isSignedNode(children[1]),
                getSemanticLocation(children[1]));
    if (failed(realX))
      return failure();
    Value result = math::Atan2Op::create(builder, location,
                                         builder.getF64Type(), *realY, *realX);
    return convertResult(result);
  }

  if (name == "$hypot") {
    if (children.size() != 2) {
      emitError(location) << "$hypot requires exactly two arguments";
      return failure();
    }
    FailureOr<Value> x = lowerExpression(children[0]);
    if (failed(x))
      return failure();
    FailureOr<Value> y = lowerExpression(children[1]);
    if (failed(y))
      return failure();
    FailureOr<Value> realX =
        convert(*x, builder.getF64Type(), isSignedNode(children[0]),
                getSemanticLocation(children[0]));
    if (failed(realX))
      return failure();
    FailureOr<Value> realY =
        convert(*y, builder.getF64Type(), isSignedNode(children[1]),
                getSemanticLocation(children[1]));
    if (failed(realY))
      return failure();
    Value xSquared = arith::MulFOp::create(builder, location, *realX, *realX);
    Value ySquared = arith::MulFOp::create(builder, location, *realY, *realY);
    Value sum = arith::AddFOp::create(builder, location, xSquared, ySquared);
    Value result =
        math::SqrtOp::create(builder, location, builder.getF64Type(), sum);
    return convertResult(result);
  }

  op.emitOpError("is not a supported real math system call");
  return failure();
}

} // namespace obelisk::simlowering
