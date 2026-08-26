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

#include "obelisk/Runtime/Runtime.h"

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
    if (!StringRef("bBoOdDhHxXeEfFgGsScCmMtTvVuUzZ").contains(specifier)) {
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
  case 'v':
  case 'V':
    return 2;
  default:
    return kTextRadix;
  }
}

} // namespace

std::optional<uint64_t> UnitLowering::rawScanByteSize(Type type,
                                                      bool fourState) {
  if (sim::getPackedScalarType(type)) {
    std::optional<unsigned> width = sim::getPackedWidth(type);
    if (!width || *width == 0)
      return std::nullopt;
    return ((static_cast<uint64_t>(*width) + 31) / 32) *
           (fourState ? 8 : 4);
  }
  unsigned count = 0;
  if (isa<sim::UnpackedStructType>(type))
    count = sim::getAggregateNumElements(type);
  else if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type)) {
    if (unionType.getIsTagged() || unionType.getFields().empty())
      return std::nullopt;
    count = 1;
  } else {
    return std::nullopt;
  }
  if (count == 0)
    return std::nullopt;
  uint64_t total = 0;
  for (unsigned ordinal = 0; ordinal != count; ++ordinal) {
    std::optional<uint64_t> element = rawScanByteSize(
        sim::getAggregateElementType(type, ordinal), fourState);
    if (!element || *element > std::numeric_limits<uint64_t>::max() - total)
      return std::nullopt;
    total += *element;
  }
  return total ? std::optional<uint64_t>(total) : std::nullopt;
}

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
    FailureOr<Value> loweredFormat = lowerExpression(children[1]);
    if (failed(loweredFormat))
      return failure();
    FailureOr<Value> dynamicFormat =
        convert(*loweredFormat, stringType, isSignedNode(children[1]), location);
    if (failed(dynamicFormat))
      return failure();

    auto maskFor = [](StringRef letters) {
      uint64_t mask = 0;
      for (char letter : letters) {
        char normalized = letter >= 'A' && letter <= 'Z'
                              ? static_cast<char>(letter - 'A' + 'a')
                              : letter;
        mask |= UINT64_C(1) << static_cast<unsigned>(normalized - 'a');
      }
      return mask;
    };
    StringAttr hierarchy = op.getSystemScopePathAttr();
    if (!hierarchy)
      hierarchy =
          function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName);
    IntegerAttr timeMultiplier =
        function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
    IntegerAttr timePrecision = designTimePrecisionExponent();

    struct DynamicDestination {
      CapturedLValue captured;
      bool packed;
      bool string;
      bool real;
      uint64_t allowed;
      std::optional<unsigned> packedWidth;
      uint64_t rawTwoStateBytes;
      uint64_t rawFourStateBytes;
    };
    SmallVector<DynamicDestination> destinations;
    destinations.reserve(children.size() - 2);
    for (size_t destinationIndex = 2; destinationIndex != children.size();
         ++destinationIndex) {
      Operation *actual = children[destinationIndex];
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

      Type destinationType = captured->type;
      bool packed = static_cast<bool>(sim::getPackedScalarType(destinationType));
      bool string = isa<sim::StringType>(destinationType);
      bool real = isa<FloatType>(destinationType);
      std::optional<uint64_t> rawTwoStateBytes =
          rawScanByteSize(destinationType, false);
      std::optional<uint64_t> rawFourStateBytes =
          rawScanByteSize(destinationType, true);
      uint64_t allowed = 0;
      if (packed)
        allowed = maskFor("bodhxefgscmtvuz");
      else if (string)
        allowed = maskFor("scm");
      else if (real)
        allowed = maskFor("efgt");
      else if (rawTwoStateBytes && rawFourStateBytes)
        allowed = maskFor("uz");
      else {
        emitError(getSemanticLocation(actual))
            << name << " dynamic-format destination must be packed, real, "
                        "string, or a recursively integral unpacked "
                        "struct/union";
        return failure();
      }
      std::optional<unsigned> packedWidth =
          packed ? sim::getPackedWidth(destinationType) : std::nullopt;
      if (packed && (!packedWidth || *packedWidth == 0))
        return failure();
      if ((packed || string) && !hierarchy) {
        emitError(location) << name
                            << " dynamic %m conversion has no elaborated scope";
        return failure();
      }
      if ((packed || real) && (!timeMultiplier || !timePrecision)) {
        emitError(location) << name
                            << " dynamic %t conversion has no frozen time scale";
        return failure();
      }
      destinations.push_back({std::move(*captured), packed, string, real,
                              allowed, packedWidth,
                              rawTwoStateBytes.value_or(0),
                              rawFourStateBytes.value_or(0)});
    }

    // Validate the complete cached plan and destination shape before the
    // scanner can consume input. This preserves the LRM's unconditional
    // argument checking even when the first field mismatches or a file is
    // already at EOF. The cursor makes this pass linear in conversions plus
    // destinations rather than rescanning the plan for every destination.
    Value validationCursor = constant(0);
    for (const DynamicDestination &destination : destinations) {
      validationCursor = sim::SimScanDynamicValidateOp::create(
          builder, location, i32, *dynamicFormat, validationCursor,
          name == "$fscanf", false, destination.allowed);
    }
    validationCursor = sim::SimScanDynamicValidateOp::create(
        builder, location, i32, *dynamicFormat, validationCursor,
        name == "$fscanf", true, 0);

    Value cursor = constant(0);
    Value planCursor = constant(0);
    Value assigned = constant(0);
    Value live = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    Value eofSeen = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));

    for (DynamicDestination &destination : destinations) {
      CapturedLValue &captured = destination.captured;
      Type destinationType = captured.type;
      bool packed = destination.packed;
      bool string = destination.string;
      bool real = destination.real;
      uint64_t allowed = destination.allowed;
      std::optional<unsigned> packedWidth = destination.packedWidth;
      uint64_t rawTwoStateBytes = destination.rawTwoStateBytes;
      uint64_t rawFourStateBytes = destination.rawFourStateBytes;

      Value enabled =
          arith::ExtUIOp::create(builder, location, i32, live).getResult();
      Value field;
      Value conversionKind;
      Value scanOk;
      Value nextCursor = cursor;
      Value nextPlanCursor;
      if (name == "$sscanf") {
        auto scan = sim::SimStringScanDynamicOp::create(
            builder, location, TypeRange{stringType, i32, i32, i32, i32}, text,
            cursor, *dynamicFormat, planCursor, enabled, false, allowed,
            rawTwoStateBytes, rawFourStateBytes);
        field = scan.getField();
        nextCursor = scan.getNextCursor();
        nextPlanCursor = scan.getNextPlanCursor();
        conversionKind = scan.getConversionKind();
        scanOk = scan.getOk();
      } else {
        auto scan = sim::SimFileScanDynamicOp::create(
            builder, location,
            TypeRange{stringType, i32, i32, i32, i32}, context, fileDescriptor,
            *dynamicFormat, planCursor, enabled, false, allowed,
            rawTwoStateBytes, rawFourStateBytes);
        field = scan.getField();
        nextPlanCursor = scan.getNextPlanCursor();
        conversionKind = scan.getConversionKind();
        scanOk = scan.getOk();
        Value eof = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, scan.getEof(),
            constant(0));
        eofSeen = arith::OrIOp::create(builder, location, eofSeen, eof);
      }
      Value matched = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, scanOk, constant(0));
      live = arith::AndIOp::create(builder, location, live, matched);
      cursor = arith::SelectOp::create(builder, location, live, nextCursor,
                                       cursor);
      planCursor = arith::SelectOp::create(builder, location, live,
                                           nextPlanCursor, planCursor);

      Block *dispatch = addBlock();
      Block *resume = addBlock();
      cf::CondBranchOp::create(builder, location, live, dispatch, ValueRange{},
                               resume, ValueRange{});
      setCurrent(dispatch);

      SmallVector<int32_t> kinds;
      SmallVector<Block *> targets;
      auto addConversion = [&](int32_t kind, unsigned radix,
                               bool hierarchyConversion, bool timeConversion) {
        Block *target = addBlock();
        kinds.push_back(kind);
        targets.push_back(target);
        setCurrent(target);
        Value parsed;
        bool numeric = false;
        if (hierarchyConversion) {
          parsed = sim::SimStringLiteralOp::create(builder, location, stringType,
                                                   hierarchy);
        } else if (timeConversion) {
          Value parsedReal = sim::SimStringParseRealOp::create(
              builder, location, builder.getF64Type(), field);
          parsed = sim::SimTimeScanScaleOp::create(
              builder, location, builder.getF64Type(), context, parsedReal,
              timeMultiplier, timePrecision);
          numeric = true;
        } else if (radix == kTextRadix) {
          parsed = field;
        } else if (radix == kRealRadix) {
          parsed = sim::SimStringParseRealOp::create(
              builder, location, builder.getF64Type(), field);
          numeric = true;
        } else {
          parsed = sim::SimStringParseLogicOp::create(
              builder, location,
              sim::LogicType::get(function.getContext(), *packedWidth), field,
              radix);
          numeric = true;
        }
        FailureOr<Value> converted =
            parsed.getType() == destinationType
                ? FailureOr<Value>(parsed)
                : convert(parsed, destinationType, numeric, location);
        if (failed(converted))
          return failure();
        if (failed(writeCapturedLValue(captured, *converted, false, false,
                                       location)))
          return failure();
        cf::BranchOp::create(builder, location, resume);
        return success();
      };
      if (string || packed) {
        if (failed(addConversion(OBELISK_RT_SCAN_DYNAMIC_TEXT, kTextRadix,
                                 false, false)) ||
            failed(addConversion(OBELISK_RT_SCAN_DYNAMIC_HIERARCHY,
                                 kTextRadix, true, false)))
          return failure();
      }
      if (real || packed) {
        if (failed(addConversion(OBELISK_RT_SCAN_DYNAMIC_REAL, kRealRadix,
                                 false, false)) ||
            failed(addConversion(OBELISK_RT_SCAN_DYNAMIC_TIME, kRealRadix,
                                 false, true)))
          return failure();
      }
      if (packed) {
        if (failed(addConversion(OBELISK_RT_SCAN_DYNAMIC_LOGIC2, 2, false,
                                 false)) ||
            failed(addConversion(OBELISK_RT_SCAN_DYNAMIC_LOGIC8, 8, false,
                                 false)) ||
            failed(addConversion(OBELISK_RT_SCAN_DYNAMIC_LOGIC10, 10, false,
                                 false)) ||
            failed(addConversion(OBELISK_RT_SCAN_DYNAMIC_LOGIC16, 16, false,
                                 false)))
          return failure();
      }
      auto addRawConversion = [&](int32_t kind,
                                  bool fourState) -> LogicalResult {
        Block *target = addBlock();
        kinds.push_back(kind);
        targets.push_back(target);
        setCurrent(target);
        Value rawCursor = constant(0);
        std::function<FailureOr<Value>(Type)> scanType =
            [&](Type type) -> FailureOr<Value> {
          if (Type scalar = sim::getPackedScalarType(type)) {
            std::optional<unsigned> width = sim::getPackedWidth(type);
            if (!width || *width == 0)
              return failure();
            auto scan = sim::SimStringScanRawOp::create(
                builder, location,
                TypeRange{sim::LogicType::get(function.getContext(), *width),
                          i32, i32},
                field, rawCursor, "", fourState, 0);
            rawCursor = scan.getNextCursor();
            FailureOr<Value> converted =
                convert(scan.getData(), scalar, true, location);
            if (failed(converted))
              return failure();
            if (scalar == type)
              return *converted;
            return sim::SimPackedUnflattenOp::create(builder, location, type,
                                                     *converted)
                .getResult();
          }
          bool isUnion = isa<sim::UnpackedUnionType>(type);
          unsigned count = isUnion ? 1 : sim::getAggregateNumElements(type);
          SmallVector<Value> elements;
          elements.reserve(count);
          for (unsigned ordinal = 0; ordinal != count; ++ordinal) {
            FailureOr<Value> element =
                scanType(sim::getAggregateElementType(type, ordinal));
            if (failed(element))
              return failure();
            elements.push_back(*element);
          }
          if (isUnion)
            return sim::SimUnionConstructOp::create(
                       builder, location, type, elements.front(), 0)
                .getResult();
          return sim::SimAggregateConstructOp::create(builder, location, type,
                                                       elements)
              .getResult();
        };
        FailureOr<Value> parsed = scanType(destinationType);
        if (failed(parsed) ||
            failed(writeCapturedLValue(captured, *parsed, false, false,
                                       location)))
          return failure();
        cf::BranchOp::create(builder, location, resume);
        return success();
      };
      if (rawTwoStateBytes != 0 &&
          failed(addRawConversion(OBELISK_RT_SCAN_DYNAMIC_RAW2, false)))
        return failure();
      if (rawFourStateBytes != 0 &&
          failed(addRawConversion(OBELISK_RT_SCAN_DYNAMIC_RAW4, true)))
        return failure();
      setCurrent(dispatch);
      auto caseType = RankedTensorType::get(
          {static_cast<int64_t>(kinds.size())}, i32);
      DenseIntElementsAttr caseValues =
          DenseIntElementsAttr::get(caseType, ArrayRef<int32_t>(kinds));
      SmallVector<ValueRange> caseOperands(kinds.size(), ValueRange{});
      cf::SwitchOp::create(builder, location, conversionKind, resume,
                           ValueRange{}, caseValues, targets, caseOperands);
      setCurrent(resume);

      Value increment = arith::ExtUIOp::create(builder, location, i32, live);
      assigned = arith::AddIOp::create(builder, location, assigned, increment);
    }

    Value enabled =
        arith::ExtUIOp::create(builder, location, i32, live).getResult();
    if (name == "$sscanf") {
      auto finish = sim::SimStringScanDynamicOp::create(
          builder, location, TypeRange{stringType, i32, i32, i32, i32}, text,
          cursor, *dynamicFormat, planCursor, enabled, true, 0, 0, 0);
      cursor = arith::SelectOp::create(builder, location, live,
                                       finish.getNextCursor(), cursor);
    } else {
      auto finish = sim::SimFileScanDynamicOp::create(
          builder, location, TypeRange{stringType, i32, i32, i32, i32},
          context, fileDescriptor, *dynamicFormat, planCursor, enabled, true, 0,
          0, 0);
      Value eof = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, finish.getEof(),
          constant(0));
      eofSeen = arith::OrIOp::create(builder, location, eofSeen, eof);
      Value noneAssigned = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, assigned, constant(0));
      Value inputFailure =
          arith::AndIOp::create(builder, location, noneAssigned, eofSeen);
      assigned = arith::SelectOp::create(builder, location, inputFailure,
                                         constant(-1), assigned);
    }
    return convertResult(assigned);
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
  Value eofSeen = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(false));
  StringAttr hierarchy = op.getSystemScopePathAttr();
  if (!hierarchy)
    hierarchy =
        function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName);
  IntegerAttr timeMultiplier =
      function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
  IntegerAttr timePrecision = designTimePrecisionExponent();
  size_t destinationIndex = 0;
  for (const ScanConversion &conversion : *conversions) {
    bool raw = conversion.specifier == 'u' || conversion.specifier == 'U' ||
               conversion.specifier == 'z' || conversion.specifier == 'Z';
    if (raw && conversion.suppressed && conversion.width == 0) {
      emitError(location) << name << " assignment suppression for raw %"
                          << conversion.specifier
                          << " requires an explicit byte count";
      return failure();
    }
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

    unsigned rawWidth = 0;
    bool rawAggregate = false;
    uint64_t aggregateRawSize = 0;
    if (raw && destination) {
      if (std::optional<unsigned> width = sim::getPackedWidth(destination->type))
        rawWidth = *width;
      else if (std::optional<uint64_t> bytes = rawScanByteSize(
                   destination->type, conversion.specifier == 'z' ||
                                          conversion.specifier == 'Z')) {
        rawAggregate = true;
        aggregateRawSize = *bytes;
      } else {
        emitError(location) << name << " %" << conversion.specifier
                            << " destination must be an integral value or an "
                               "unpacked struct/union of integral values";
        return failure();
      }
    }

    Value field;
    Value rawData;
    Value aggregateData;
    Value scanOk;
    Value nextCursor;
    auto scanRawAggregate = [&](bool file, Value &aggregateCursor,
                                Value &aggregateLive,
                                Value &aggregateEOF) -> FailureOr<Value> {
      bool firstLeaf = true;
      bool fourState = conversion.specifier == 'z' ||
                       conversion.specifier == 'Z';
      bool forceMismatch = conversion.width != 0 &&
                           aggregateRawSize > conversion.width;
      std::function<FailureOr<Value>(Type)> scanType =
          [&](Type type) -> FailureOr<Value> {
        if (Type scalar = sim::getPackedScalarType(type)) {
          std::optional<unsigned> width = sim::getPackedWidth(type);
          if (!width || *width == 0)
            return failure();
          uint64_t bytes = ((static_cast<uint64_t>(*width) + 31) / 32) *
                           (fourState ? 8 : 4);
          uint64_t maxWidth = forceMismatch && firstLeaf ? bytes - 1 : 0;
          StringRef prefix = firstLeaf ? StringRef(conversion.prefix) : "";
          firstLeaf = false;
          Value data;
          Value ok;
          if (file) {
            Value enabled =
                arith::ExtUIOp::create(builder, location, i32, aggregateLive);
            auto scan = sim::SimFileScanRawOp::create(
                builder, location,
                TypeRange{sim::LogicType::get(function.getContext(), *width),
                          i32, i32},
                context, fileDescriptor, enabled, prefix, fourState, maxWidth);
            data = scan.getData();
            ok = scan.getOk();
            Value eof = arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::ne, scan.getEof(),
                constant(0));
            aggregateEOF =
                arith::OrIOp::create(builder, location, aggregateEOF, eof);
          } else {
            auto scan = sim::SimStringScanRawOp::create(
                builder, location,
                TypeRange{sim::LogicType::get(function.getContext(), *width),
                          i32, i32},
                text, aggregateCursor, prefix, fourState, maxWidth);
            data = scan.getData();
            ok = scan.getOk();
            Value matched = arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::ne, ok, constant(0));
            Value nextLive = arith::AndIOp::create(builder, location,
                                                   aggregateLive, matched);
            aggregateCursor = arith::SelectOp::create(
                builder, location, nextLive, scan.getNextCursor(),
                aggregateCursor);
          }
          Value matched = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::ne, ok, constant(0));
          aggregateLive = arith::AndIOp::create(builder, location,
                                                aggregateLive, matched);
          FailureOr<Value> converted = convert(data, scalar, true, location);
          if (failed(converted))
            return failure();
          if (scalar == type)
            return *converted;
          return sim::SimPackedUnflattenOp::create(builder, location, type,
                                                   *converted)
              .getResult();
        }
        bool isUnion = isa<sim::UnpackedUnionType>(type);
        unsigned count = isUnion ? 1 : sim::getAggregateNumElements(type);
        SmallVector<Value> elements;
        for (unsigned ordinal = 0; ordinal != count; ++ordinal) {
          FailureOr<Value> element =
              scanType(sim::getAggregateElementType(type, ordinal));
          if (failed(element))
            return failure();
          elements.push_back(*element);
        }
        if (isUnion)
          return sim::SimUnionConstructOp::create(builder, location, type,
                                                  elements.front(), 0)
              .getResult();
        return sim::SimAggregateConstructOp::create(builder, location, type,
                                                    elements)
            .getResult();
      };
      return scanType(destination->type);
    };
    if (name == "$sscanf") {
      if (raw && conversion.suppressed) {
        auto scan = sim::SimStringSkipRawOp::create(
            builder, location, TypeRange{i32, i32}, text, cursor,
            conversion.prefix, conversion.width);
        scanOk = scan.getOk();
        nextCursor = scan.getNextCursor();
      } else if (rawAggregate) {
        Value aggregateCursor = cursor;
        Value aggregateLive = live;
        Value aggregateEOF = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(false));
        FailureOr<Value> scanned = scanRawAggregate(
            false, aggregateCursor, aggregateLive, aggregateEOF);
        if (failed(scanned))
          return failure();
        aggregateData = *scanned;
        scanOk = arith::ExtUIOp::create(builder, location, i32, aggregateLive);
        nextCursor = aggregateCursor;
      } else if (raw) {
        auto scan = sim::SimStringScanRawOp::create(
            builder, location,
            TypeRange{sim::LogicType::get(function.getContext(), rawWidth), i32,
                      i32},
            text, cursor, conversion.prefix,
            conversion.specifier == 'z' || conversion.specifier == 'Z',
            conversion.width);
        rawData = scan.getData();
        scanOk = scan.getOk();
        nextCursor = scan.getNextCursor();
      } else {
        auto scan = sim::SimStringScanFieldOp::create(
            builder, location, TypeRange{stringType, i32, i32}, text, cursor,
            conversion.prefix,
            static_cast<uint32_t>(
                static_cast<unsigned char>(conversion.specifier)),
            conversion.width);
        field = scan.getField();
        scanOk = scan.getOk();
        nextCursor = scan.getNextCursor();
      }
    } else {
      Value enabled = arith::ExtUIOp::create(builder, location, i32, live);
      Value eofValue;
      if (raw && conversion.suppressed) {
        auto scan = sim::SimFileSkipRawOp::create(
            builder, location, TypeRange{i32, i32}, context, fileDescriptor,
            enabled, conversion.prefix, conversion.width);
        scanOk = scan.getOk();
        eofValue = scan.getEof();
      } else if (rawAggregate) {
        Value aggregateCursor;
        Value aggregateLive = live;
        Value aggregateEOF = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(false));
        FailureOr<Value> scanned = scanRawAggregate(
            true, aggregateCursor, aggregateLive, aggregateEOF);
        if (failed(scanned))
          return failure();
        aggregateData = *scanned;
        scanOk = arith::ExtUIOp::create(builder, location, i32, aggregateLive);
        eofValue = arith::ExtUIOp::create(builder, location, i32, aggregateEOF);
      } else if (raw) {
        auto scan = sim::SimFileScanRawOp::create(
            builder, location,
            TypeRange{sim::LogicType::get(function.getContext(), rawWidth), i32,
                      i32},
            context, fileDescriptor, enabled, conversion.prefix,
            conversion.specifier == 'z' || conversion.specifier == 'Z',
            conversion.width);
        rawData = scan.getData();
        scanOk = scan.getOk();
        eofValue = scan.getEof();
      } else {
        auto scan = sim::SimFileScanFieldOp::create(
            builder, location, TypeRange{stringType, i32, i32}, context,
            fileDescriptor, enabled, conversion.prefix,
            static_cast<uint32_t>(
                static_cast<unsigned char>(conversion.specifier)),
            conversion.width);
        field = scan.getField();
        scanOk = scan.getOk();
        eofValue = scan.getEof();
      }
      Value eof = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, eofValue, constant(0));
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
      if (raw) {
        parsed = rawAggregate ? aggregateData : rawData;
      } else if (conversion.specifier == 'm' || conversion.specifier == 'M') {
        if (!hierarchy) {
          emitError(location)
              << name << " %m conversion has no elaborated scope";
          return failure();
        }
        parsed = sim::SimStringLiteralOp::create(builder, location, stringType,
                                                 hierarchy);
      } else if (conversion.specifier == 't' || conversion.specifier == 'T') {
        if (!timeMultiplier || !timePrecision) {
          emitError(location) << name
                              << " %t conversion has no frozen time "
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
      else {
        std::optional<unsigned> width =
            sim::getPackedWidth(destination->type);
        if (!width || *width == 0) {
          emitError(location) << name << " %" << conversion.specifier
                              << " destination has no packed width";
          return failure();
        }
        parsed = sim::SimStringParseLogicOp::create(
            builder, location,
            sim::LogicType::get(function.getContext(), *width),
            field, radix);
      }
      FailureOr<Value> value =
          parsed.getType() == destination->type
              ? FailureOr<Value>(parsed)
              : convert(parsed, destination->type, raw || radix != kTextRadix,
                        location);
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
