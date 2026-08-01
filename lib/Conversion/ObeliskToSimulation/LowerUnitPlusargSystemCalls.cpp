//===- LowerUnitPlusargSystemCalls.cpp - Lower plusarg queries ----------===//
//
// IEEE 1800 21.6. $test$plusargs matches a prefix against the command line.
// $value$plusargs splits its format string into that same prefix plus a
// trailing conversion specifier: the runtime returns the matched argument's
// remaining text, and the specifier — which is a compile-time property of the
// format string — selects how that text becomes the destination's value.
//
//===---------------------------------------------------------------------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

using namespace mlir;

namespace obelisk::simlowering {

namespace {

// The conversion the trailing specifier of a $value$plusargs format asks for.
// A radix of zero means the text is taken as-is; kRealRadix means it is parsed
// as a real.
constexpr unsigned kStringRadix = 0;
constexpr unsigned kRealRadix = 1;

std::optional<unsigned> conversionRadix(char specifier) {
  switch (specifier) {
  case 'b':
  case 'B':
    return 2u;
  case 'o':
  case 'O':
    return 8u;
  case 'd':
  case 'D':
    return 10u;
  case 'h':
  case 'H':
  case 'x':
  case 'X':
    return 16u;
  case 'e':
  case 'E':
  case 'f':
  case 'F':
  case 'g':
  case 'G':
    return kRealRadix;
  case 's':
  case 'S':
    return kStringRadix;
  default:
    return std::nullopt;
  }
}

} // namespace

FailureOr<Value>
UnitLowering::lowerPlusargSystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef name = op.getCalleeName();
  Value context = function.getBody().front().getArgument(0);
  auto i32 = builder.getI32Type();
  Type stringType = sim::StringType::get(function.getContext());

  auto convertResult = [&](Value value) -> FailureOr<Value> {
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type))
      return failure();
    return convert(value, *type, true, location);
  };

  if (name == "$test$plusargs") {
    if (children.size() != 1) {
      emitError(location) << "$test$plusargs requires exactly one argument";
      return failure();
    }
    FailureOr<Value> argument = lowerExpression(children.front());
    if (failed(argument))
      return failure();
    FailureOr<Value> text = convert(*argument, stringType,
                                    isSignedNode(children.front()), location);
    if (failed(text))
      return failure();
    Value found = sim::SimPlusargTestOp::create(builder, location, i32, context,
                                                *text);
    return convertResult(found);
  }

  if (children.size() != 2) {
    emitError(location)
        << "$value$plusargs requires a format string and a destination";
    return failure();
  }

  // The specifier decides the conversion, so the format has to be known here.
  Operation *spelling = children[0];
  while (isa<semantic::SVConversionExpressionOp>(spelling)) {
    SmallVector<Operation *> converted = getChildren(spelling);
    if (converted.size() != 1)
      break;
    spelling = converted.front();
  }
  auto literal = dyn_cast<semantic::SVStringLiteralOp>(spelling);
  if (!literal) {
    emitError(getSemanticLocation(children[0]))
        << "$value$plusargs requires a literal format string";
    return failure();
  }
  StringRef format = literal.getConstantValue();
  size_t percent = format.rfind('%');
  if (percent == StringRef::npos || percent + 1 >= format.size()) {
    emitError(getSemanticLocation(children[0]))
        << "$value$plusargs format must end with a conversion specifier";
    return failure();
  }
  std::optional<unsigned> radix = conversionRadix(format[percent + 1]);
  if (!radix) {
    emitError(getSemanticLocation(children[0]))
        << "unsupported $value$plusargs conversion specifier '"
        << format.substr(percent, 2) << "'";
    return failure();
  }

  Operation *actual = children[1];
  if (auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
    SmallVector<Operation *> outputChildren = getChildren(assignment);
    if (outputChildren.size() == 2 &&
        isa<semantic::SVEmptyArgumentExpressionOp>(outputChildren[1]))
      actual = outputChildren.front();
  }
  FailureOr<Value> destination = lowerExpression(actual, true);
  if (failed(destination)) {
    emitError(getSemanticLocation(actual))
        << "$value$plusargs destination must be a writable variable";
    return failure();
  }
  Type destinationType = getReferenceElementType(*destination);

  Value prefix = sim::SimStringLiteralOp::create(
      builder, location, stringType, format.substr(0, percent));
  auto query = sim::SimPlusargValueOp::create(
      builder, location, TypeRange{stringType, i32}, context, prefix);

  Value parsed;
  if (*radix == kStringRadix)
    parsed = query.getTail();
  else if (*radix == kRealRadix)
    parsed = sim::SimStringParseRealOp::create(
        builder, location, builder.getF64Type(), query.getTail());
  else
    parsed = sim::SimStringParseIntegerOp::create(
        builder, location, builder.getI64Type(), query.getTail(), *radix);
  FailureOr<Value> converted =
      convert(parsed, destinationType, *radix != kStringRadix, location);
  if (failed(converted))
    return failure();

  // A miss leaves the destination alone, so the store writes back what is
  // already there rather than the value parsed from an empty tail.
  FailureOr<Value> current = loadReference(*destination, location);
  if (failed(current))
    return failure();
  Value zero = arith::ConstantOp::create(builder, location, i32,
                                         builder.getIntegerAttr(i32, 0));
  Value found = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::ne,
                                      query.getFound(), zero);
  Value updated = arith::SelectOp::create(builder, location, found, *converted,
                                          *current);
  if (failed(storeReference(*destination, updated, location)))
    return failure();
  return convertResult(query.getFound());
}

} // namespace obelisk::simlowering
