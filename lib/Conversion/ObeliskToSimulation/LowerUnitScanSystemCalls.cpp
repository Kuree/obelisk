//===- LowerUnitScanSystemCalls.cpp - Lower $sscanf and $fscanf ---------===//
//
// IEEE 1800 21.3.4. Both tasks walk a format string, matching literal text and
// extracting one field per conversion, and return how many destinations they
// filled. The format is known at compile time, so the conversions are split
// here: each destination gets one scan-field op carrying the literal text that
// precedes it, and the field it yields is parsed with the same string
// primitives the language's own conversion methods use.
//
// A conversion that fails to match ends the scan, so every store is guarded by
// the running success flag rather than by a branch.
//
//===---------------------------------------------------------------------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

using namespace mlir;

namespace obelisk::simlowering {

namespace {

// One conversion: the literal format text before it, and its letter.
struct ScanConversion {
  std::string prefix;
  char specifier = 0;
};

// Split a format string into its conversions. Returns std::nullopt on a
// specifier the scanner does not implement, leaving the caller to report it
// against the format's own location.
std::optional<SmallVector<ScanConversion>>
splitScanFormat(StringRef format, std::string &unsupported) {
  SmallVector<ScanConversion> conversions;
  std::string pending;
  for (size_t index = 0; index < format.size(); ++index) {
    if (format[index] != '%') {
      pending.push_back(format[index]);
      continue;
    }
    if (index + 1 >= format.size()) {
      unsupported = "%";
      return std::nullopt;
    }
    char specifier = format[++index];
    if (specifier == '%') {
      pending.push_back('%');
      continue;
    }
    // Assignment suppression consumes a field without a destination, which
    // would desynchronize the destination list built here.
    if (specifier == '*' || !StringRef("bBoOdDhHxXeEfFgGsScC").contains(
                                specifier)) {
      unsupported = std::string("%") + specifier;
      return std::nullopt;
    }
    conversions.push_back({pending, specifier});
    pending.clear();
  }
  return conversions;
}

// The radix a conversion parses in; zero for the text conversions and one for
// the real ones.
constexpr unsigned kTextRadix = 0;
constexpr unsigned kRealRadix = 1;

unsigned scanRadix(char specifier) {
  switch (specifier) {
  case 'b':
  case 'B':
    return 2;
  case 'o':
  case 'O':
    return 8;
  case 'd':
  case 'D':
    return 10;
  case 'h':
  case 'H':
  case 'x':
  case 'X':
    return 16;
  case 'e':
  case 'E':
  case 'f':
  case 'F':
  case 'g':
  case 'G':
    return kRealRadix;
  default:
    return kTextRadix;
  }
}

} // namespace

FailureOr<Value>
UnitLowering::lowerScanSystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef name = op.getCalleeName();
  Value context = function.getBody().front().getArgument(0);
  auto i32 = builder.getI32Type();
  Type stringType = sim::StringType::get(function.getContext());

  auto constant = [&](int64_t value) -> Value {
    return arith::ConstantOp::create(builder, location, i32,
                                     builder.getIntegerAttr(i32, value));
  };
  auto convertResult = [&](Value value) -> FailureOr<Value> {
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type))
      return failure();
    return convert(value, *type, true, location);
  };

  if (children.size() < 2) {
    emitError(location) << name << " requires a source and a format string";
    return failure();
  }

  // The source: $sscanf takes the text directly, $fscanf reads one line. A
  // read that returned nothing is end of file, which $fscanf reports as -1
  // rather than as zero conversions.
  Value text;
  Value endOfFile;
  if (name == "$sscanf") {
    FailureOr<Value> source = lowerExpression(children[0]);
    if (failed(source))
      return failure();
    FailureOr<Value> converted =
        convert(*source, stringType, isSignedNode(children[0]), location);
    if (failed(converted))
      return failure();
    text = *converted;
  } else {
    FailureOr<Value> descriptor = lowerExpression(children[0]);
    if (failed(descriptor))
      return failure();
    FailureOr<Value> descriptor32 =
        convert(*descriptor, i32, isSignedNode(children[0]), location);
    if (failed(descriptor32))
      return failure();
    auto line = sim::SimFileGetlineStringOp::create(
        builder, location, TypeRange{stringType, i32}, context, *descriptor32);
    text = line.getData();
    endOfFile = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::eq,
                                      line.getCount(), constant(0));
  }

  Operation *spelling = children[1];
  while (isa<semantic::SVConversionExpressionOp>(spelling)) {
    SmallVector<Operation *> converted = getChildren(spelling);
    if (converted.size() != 1)
      break;
    spelling = converted.front();
  }
  auto literal = dyn_cast<semantic::SVStringLiteralOp>(spelling);
  if (!literal) {
    emitError(getSemanticLocation(children[1]))
        << name << " requires a literal format string";
    return failure();
  }
  std::string unsupported;
  std::optional<SmallVector<ScanConversion>> conversions =
      splitScanFormat(literal.getConstantValue(), unsupported);
  if (!conversions) {
    emitError(getSemanticLocation(children[1]))
        << "unsupported " << name << " conversion '" << unsupported << "'";
    return failure();
  }
  if (conversions->size() != children.size() - 2) {
    emitError(location) << name << " format has " << conversions->size()
                        << " conversions but " << (children.size() - 2)
                        << " destinations";
    return failure();
  }

  Value cursor = constant(0);
  Value assigned = constant(0);
  // Set once a conversion fails; every later store keeps its destination.
  Value live = arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(true));
  for (auto [index, conversion] : llvm::enumerate(*conversions)) {
    Operation *actual = children[index + 2];
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
      SmallVector<Operation *> outputChildren = getChildren(assignment);
      if (outputChildren.size() == 2 &&
          isa<semantic::SVEmptyArgumentExpressionOp>(outputChildren[1]))
        actual = outputChildren.front();
    }
    FailureOr<Value> destination = lowerExpression(actual, true);
    if (failed(destination)) {
      emitError(getSemanticLocation(actual))
          << name << " destination must be a writable variable";
      return failure();
    }
    Type destinationType = getReferenceElementType(*destination);

    auto scan = sim::SimStringScanFieldOp::create(
        builder, location, TypeRange{stringType, i32, i32}, text, cursor,
        conversion.prefix,
        static_cast<uint32_t>(static_cast<unsigned char>(conversion.specifier)));

    unsigned radix = scanRadix(conversion.specifier);
    Value parsed;
    if (radix == kTextRadix)
      parsed = scan.getField();
    else if (radix == kRealRadix)
      parsed = sim::SimStringParseRealOp::create(
          builder, location, builder.getF64Type(), scan.getField());
    else
      parsed = sim::SimStringParseIntegerOp::create(
          builder, location, builder.getI64Type(), scan.getField(), radix);
    FailureOr<Value> value =
        convert(parsed, destinationType, radix != kTextRadix, location);
    if (failed(value))
      return failure();

    Value matched = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, scan.getOk(), constant(0));
    live = arith::AndIOp::create(builder, location, live, matched);
    FailureOr<Value> current = loadReference(*destination, location);
    if (failed(current))
      return failure();
    Value updated =
        arith::SelectOp::create(builder, location, live, *value, *current);
    if (failed(storeReference(*destination, updated, location)))
      return failure();

    cursor =
        arith::SelectOp::create(builder, location, live, scan.getNextCursor(),
                                cursor);
    Value increment = arith::ExtUIOp::create(builder, location, i32, live);
    assigned = arith::AddIOp::create(builder, location, assigned, increment);
  }
  if (endOfFile)
    assigned = arith::SelectOp::create(builder, location, endOfFile,
                                       constant(-1), assigned);
  return convertResult(assigned);
}

} // namespace obelisk::simlowering
