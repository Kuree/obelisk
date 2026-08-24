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

struct LiteralPlusargFormat {
  std::string prefix;
  unsigned conversion = 0;
};

std::optional<LiteralPlusargFormat> splitLiteralPlusargFormat(StringRef format,
                                                              StringRef &bad) {
  size_t percent = format.rfind('%');
  size_t specifier = percent == StringRef::npos ? percent : percent + 1;
  while (specifier != StringRef::npos && specifier < format.size() &&
         format[specifier] == '0')
    ++specifier;
  if (percent == StringRef::npos || specifier + 1 != format.size()) {
    bad = format;
    return std::nullopt;
  }
  std::optional<unsigned> conversion = conversionRadix(format[specifier]);
  if (!conversion) {
    bad = format.substr(percent, specifier + 1 - percent);
    return std::nullopt;
  }

  LiteralPlusargFormat result;
  result.conversion = *conversion;
  result.prefix.reserve(percent);
  for (size_t index = 0; index < percent; ++index) {
    if (format[index] == '%' && index + 1 < percent &&
        format[index + 1] == '%')
      ++index;
    result.prefix.push_back(format[index]);
  }
  return result;
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

  // Keep literal formats on the compact static path. A string-like runtime
  // expression uses plusarg.scan to split and validate its trailing
  // conversion without recompiling or speculatively matching every prefix.
  Operation *spelling = children[0];
  while (isa<semantic::SVConversionExpressionOp>(spelling)) {
    SmallVector<Operation *> converted = getChildren(spelling);
    if (converted.size() != 1)
      break;
    spelling = converted.front();
  }
  auto literal = dyn_cast<semantic::SVStringLiteralOp>(spelling);
  std::optional<LiteralPlusargFormat> literalFormat;
  Value dynamicFormat;
  if (literal) {
    StringRef bad;
    literalFormat = splitLiteralPlusargFormat(literal.getConstantValue(), bad);
    if (!literalFormat) {
      emitError(getSemanticLocation(children[0]))
          << "invalid $value$plusargs format '" << bad << "'";
      return failure();
    }
  } else {
    FailureOr<Value> lowered = lowerExpression(children[0]);
    if (failed(lowered))
      return failure();
    FailureOr<Value> converted =
        convert(*lowered, stringType, isSignedNode(children[0]), location);
    if (failed(converted))
      return failure();
    dynamicFormat = *converted;
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

  Value tail;
  Value conversion;
  Value queryFound;
  if (literalFormat) {
    Value prefix = sim::SimStringLiteralOp::create(
        builder, location, stringType, literalFormat->prefix);
    auto query = sim::SimPlusargValueOp::create(
        builder, location, TypeRange{stringType, i32}, context, prefix);
    tail = query.getTail();
    queryFound = query.getFound();
  } else {
    auto query = sim::SimPlusargScanOp::create(
        builder, location, TypeRange{stringType, i32, i32}, context,
        dynamicFormat);
    tail = query.getTail();
    conversion = query.getConversion();
    queryFound = query.getFound();
  }

  auto parseAndConvert = [&](unsigned radix) -> FailureOr<Value> {
    Value parsed;
    if (radix == kStringRadix)
      parsed = tail;
    else if (radix == kRealRadix)
      parsed = sim::SimStringParseRealOp::create(
          builder, location, builder.getF64Type(), tail);
    else
      parsed = sim::SimStringParseLogicOp::create(
          builder, location,
          sim::LogicType::get(function.getContext(), 64), tail, radix);
    return convert(parsed, destinationType, radix != kStringRadix, location);
  };

  FailureOr<Value> converted;
  Value validConversion;
  auto kindIs = [&](unsigned kind) -> Value {
    Value expected = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(kind));
    return arith::CmpIOp::create(builder, location,
                                 arith::CmpIPredicate::eq, conversion,
                                 expected);
  };
  if (literalFormat) {
    converted = parseAndConvert(literalFormat->conversion);
  } else if (isa<FloatType>(destinationType)) {
    converted = parseAndConvert(kRealRadix);
    validConversion = kindIs(kRealRadix);
  } else if (isa<sim::StringType>(destinationType)) {
    converted = parseAndConvert(kStringRadix);
    validConversion = kindIs(kStringRadix);
  } else {
    converted = parseAndConvert(kStringRadix);
    if (failed(converted))
      return failure();
    for (unsigned radix : {2u, 8u, 10u, 16u, kRealRadix}) {
      FailureOr<Value> candidate = parseAndConvert(radix);
      if (failed(candidate))
        return failure();
      converted = arith::SelectOp::create(builder, location, kindIs(radix),
                                          *candidate, *converted)
                      .getResult();
    }
  }
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
                                      arith::CmpIPredicate::ne, queryFound,
                                      zero);
  if (validConversion)
    found = arith::AndIOp::create(builder, location, found, validConversion);
  Value updated = arith::SelectOp::create(builder, location, found, *converted,
                                          *current);
  if (failed(storeReference(*destination, updated, location)))
    return failure();
  Value result = arith::ExtUIOp::create(builder, location, i32, found);
  return convertResult(result);
}

} // namespace obelisk::simlowering
