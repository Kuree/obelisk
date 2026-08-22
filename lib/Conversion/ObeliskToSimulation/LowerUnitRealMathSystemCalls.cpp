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

  // IEEE 1800-2017 Table 20-4 pairs each real math function with the C library
  // function whose behavior it takes. Every one-argument entry has the same
  // shape -- read the argument as a real, apply the operation, hand the result
  // back in the call's own type -- so only the operation differs.
  Type realType = builder.getF64Type();
  auto lowerUnary =
      [&](llvm::function_ref<Value(Value)> apply) -> FailureOr<Value> {
    if (children.size() != 1) {
      emitError(location) << name << " requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, realType, isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    return convertResult(apply(*real));
  };
  auto unary = [&](auto tag) {
    using MathOp = decltype(tag);
    return lowerUnary([&](Value operand) -> Value {
      return MathOp::create(builder, location, realType, operand);
    });
  };

  if (name == "$ceil")
    return unary(math::CeilOp{});
  if (name == "$floor")
    return unary(math::FloorOp{});
  if (name == "$sqrt")
    return unary(math::SqrtOp{});
  if (name == "$exp")
    return unary(math::ExpOp{});
  if (name == "$ln")
    return unary(math::LogOp{});
  if (name == "$log10")
    return unary(math::Log10Op{});
  if (name == "$sin")
    return unary(math::SinOp{});
  if (name == "$cos")
    return unary(math::CosOp{});
  if (name == "$tan")
    return unary(math::TanOp{});
  if (name == "$asin")
    return unary(math::AsinOp{});
  if (name == "$acos")
    return unary(math::AcosOp{});
  if (name == "$atan")
    return unary(math::AtanOp{});
  if (name == "$sinh")
    return unary(math::SinhOp{});
  if (name == "$cosh")
    return unary(math::CoshOp{});
  if (name == "$tanh")
    return unary(math::TanhOp{});
  if (name == "$asinh")
    return unary(math::AsinhOp{});
  if (name == "$acosh")
    return unary(math::AcoshOp{});
  if (name == "$atanh")
    return unary(math::AtanhOp{});

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
