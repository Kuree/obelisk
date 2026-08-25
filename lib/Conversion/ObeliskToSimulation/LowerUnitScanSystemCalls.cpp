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
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include <limits>

using namespace mlir;

namespace obelisk::simlowering {

namespace {

// One conversion: the literal format text before it, and its letter.
struct ScanConversion {
  std::string prefix;
  char specifier = 0;
  uint64_t width = 0;
  bool suppressed = false;
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
    size_t conversionStart = index;
    char specifier = format[++index];
    if (specifier == '%') {
      pending.push_back('%');
      continue;
    }
    bool suppressed = specifier == '*';
    if (suppressed) {
      if (++index >= format.size()) {
        unsupported = format.substr(conversionStart).str();
        return std::nullopt;
      }
      specifier = format[index];
    }
    uint64_t width = 0;
    while (specifier >= '0' && specifier <= '9') {
      uint64_t digit = static_cast<uint64_t>(specifier - '0');
      if (width > (std::numeric_limits<uint64_t>::max() - digit) / 10)
        width = std::numeric_limits<uint64_t>::max();
      else
        width = width * 10 + digit;
      if (++index >= format.size()) {
        unsupported = format.substr(conversionStart).str();
        return std::nullopt;
      }
      specifier = format[index];
    }
    if (!StringRef("bBoOdDhHxXeEfFgGsScCmMtT").contains(specifier)) {
      unsupported =
          format.substr(conversionStart, index - conversionStart + 1).str();
      return std::nullopt;
    }
    conversions.push_back({pending, specifier, width, suppressed});
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
  case 't':
  case 'T':
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

  // $sscanf walks one managed string with an explicit cursor. $fscanf instead
  // consumes fields directly from the descriptor's current stream position;
  // reading a whole line here would incorrectly discard unconverted text
  // between separate calls (IEEE 1800-2017 21.3.4.3).
  Value text;
  Value fileDescriptor;
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
    fileDescriptor = *descriptor32;
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
  size_t destinationCount =
      llvm::count_if(*conversions, [](const ScanConversion &conversion) {
        return !conversion.suppressed;
      });
  if (destinationCount != children.size() - 2) {
    emitError(location) << name << " format has " << destinationCount
                        << " conversions but " << (children.size() - 2)
                        << " destinations";
    return failure();
  }

  Value cursor = constant(0);
  Value assigned = constant(0);
  // Set once a conversion fails; every later store keeps its destination.
  Value live = arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(true));
  Value eofSeen = arith::ConstantOp::create(builder, location,
                                            builder.getI1Type(),
                                            builder.getBoolAttr(false));
  StringAttr hierarchy = op.getSystemScopePathAttr();
  if (!hierarchy)
    hierarchy =
        function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName);
  IntegerAttr timeMultiplier =
      function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
  IntegerAttr timePrecision = designTimePrecisionExponent();
  size_t destinationIndex = 0;
  for (const ScanConversion &conversion : *conversions) {
    std::optional<CapturedLValue> destination;
    if (!conversion.suppressed) {
      Operation *actual = children[destinationIndex++ + 2];
      if (auto assignment =
              dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
        SmallVector<Operation *> outputChildren = getChildren(assignment);
        if (outputChildren.size() == 2 &&
            isa<semantic::SVEmptyArgumentExpressionOp>(outputChildren[1]))
          actual = outputChildren.front();
      }
      FailureOr<CapturedLValue> captured =
          captureLValue(actual, getSemanticLocation(actual));
      if (failed(captured)) {
        emitError(getSemanticLocation(actual))
            << name << " destination must be a writable variable";
        return failure();
      }
      destination = std::move(*captured);
    }

    Value field;
    Value scanOk;
    Value nextCursor;
    if (name == "$sscanf") {
      auto scan = sim::SimStringScanFieldOp::create(
          builder, location, TypeRange{stringType, i32, i32}, text, cursor,
          conversion.prefix,
          static_cast<uint32_t>(
              static_cast<unsigned char>(conversion.specifier)),
          conversion.width);
      field = scan.getField();
      scanOk = scan.getOk();
      nextCursor = scan.getNextCursor();
    } else {
      Value enabled = arith::ExtUIOp::create(builder, location, i32, live);
      auto scan = sim::SimFileScanFieldOp::create(
          builder, location, TypeRange{stringType, i32, i32}, context,
          fileDescriptor, enabled, conversion.prefix,
          static_cast<uint32_t>(
              static_cast<unsigned char>(conversion.specifier)),
          conversion.width);
      field = scan.getField();
      scanOk = scan.getOk();
      Value eof =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                scan.getEof(), constant(0));
      eofSeen = arith::OrIOp::create(builder, location, eofSeen, eof);
    }

    Value matched = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, scanOk, constant(0));
    live = arith::AndIOp::create(builder, location, live, matched);
    // IEEE 1800-2017 21.3.4 updates only successfully matched conversion
    // destinations. Captured lvalues cover container elements, aggregate
    // slices, class properties, and string characters without manufacturing
    // an unstable interior pointer.
    if (destination) {
      unsigned radix = scanRadix(conversion.specifier);
      Value parsed;
      if (conversion.specifier == 'm' || conversion.specifier == 'M') {
        if (!hierarchy) {
          emitError(location)
              << name << " %m conversion has no elaborated scope";
          return failure();
        }
        parsed = sim::SimStringLiteralOp::create(builder, location, stringType,
                                                 hierarchy);
      } else if (conversion.specifier == 't' || conversion.specifier == 'T') {
        if (!timeMultiplier || !timePrecision) {
          emitError(location) << name << " %t conversion has no frozen time "
                                         "scale";
          return failure();
        }
        Value real = sim::SimStringParseRealOp::create(
            builder, location, builder.getF64Type(), field);
        parsed = sim::SimTimeScanScaleOp::create(
            builder, location, builder.getF64Type(), context, real,
            timeMultiplier, timePrecision);
      } else if (radix == kTextRadix)
        parsed = field;
      else if (radix == kRealRadix)
        parsed = sim::SimStringParseRealOp::create(builder, location,
                                                   builder.getF64Type(), field);
      else
        parsed = sim::SimStringParseLogicOp::create(
            builder, location, sim::LogicType::get(function.getContext(), 64),
            field, radix);
      FailureOr<Value> value =
          convert(parsed, destination->type, radix != kTextRadix, location);
      if (failed(value))
        return failure();

      Block *store = addBlock();
      Block *resume = addBlock();
      cf::CondBranchOp::create(builder, location, live, store, ValueRange{},
                               resume, ValueRange{});
      setCurrent(store);
      if (failed(writeCapturedLValue(*destination, *value, false, false,
                                     location)))
        return failure();
      cf::BranchOp::create(builder, location, resume);
      setCurrent(resume);
    }

    if (name == "$sscanf")
      cursor =
          arith::SelectOp::create(builder, location, live, nextCursor, cursor);
    if (!conversion.suppressed) {
      Value increment = arith::ExtUIOp::create(builder, location, i32, live);
      assigned = arith::AddIOp::create(builder, location, assigned, increment);
    }
  }
  if (name == "$fscanf") {
    Value noneAssigned = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, assigned, constant(0));
    Value inputFailure =
        arith::AndIOp::create(builder, location, noneAssigned, eofSeen);
    assigned = arith::SelectOp::create(builder, location, inputFailure,
                                       constant(-1), assigned);
  }
  return convertResult(assigned);
}

} // namespace obelisk::simlowering
