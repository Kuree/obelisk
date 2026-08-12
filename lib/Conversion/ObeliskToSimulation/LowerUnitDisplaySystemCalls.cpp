//===- LowerUnitDisplaySystemCalls.cpp - Lower display semantics --------===//

#include "LowerUnit.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

#include <cctype>
#include <optional>
#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

FailureOr<Value>
UnitLowering::lowerStringFormatSystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef name = op.getCalleeName();
  bool task = name == "$sformat";
  size_t formatIndex = task ? 1 : 0;
  if (children.size() <= formatIndex) {
    emitError(location) << name << " requires "
                        << (task ? "a destination and a format string"
                                 : "a format string");
    return failure();
  }

  Operation *literalNode = children[formatIndex];
  while (isa<semantic::SVConversionExpressionOp>(literalNode)) {
    SmallVector<Operation *> converted = getChildren(literalNode);
    if (converted.size() != 1)
      break;
    literalNode = converted.front();
  }
  auto literal = dyn_cast<semantic::SVStringLiteralOp>(literalNode);
  if (!literal) {
    emitError(getSemanticLocation(children[formatIndex]))
        << name << " currently requires a literal format string";
    return failure();
  }

  Type stringType = sim::StringType::get(function.getContext());
  auto stringLiteral = [&](StringRef text) -> Value {
    return sim::SimStringLiteralOp::create(builder, location, stringType, text);
  };
  SmallVector<Value> parts;
  StringRef format = literal.getConstantValue();
  size_t argumentIndex = formatIndex + 1;
  size_t textStart = 0;
  for (size_t cursor = 0; cursor < format.size();) {
    if (format[cursor] != '%') {
      ++cursor;
      continue;
    }
    if (cursor > textStart)
      parts.push_back(stringLiteral(format.slice(textStart, cursor)));
    if (cursor + 1 < format.size() && format[cursor + 1] == '%') {
      parts.push_back(stringLiteral("%"));
      cursor += 2;
      textStart = cursor;
      continue;
    }

    size_t specifier = cursor + 1;
    while (specifier < format.size() &&
           !std::isalpha(static_cast<unsigned char>(format[specifier])))
      ++specifier;
    if (specifier == format.size()) {
      emitError(location) << name << " has an unterminated format specifier";
      return failure();
    }
    StringRef modifiers = format.slice(cursor + 1, specifier);
    if (!modifiers.empty() && modifiers != "0") {
      emitError(location) << name << " does not yet support format modifier '"
                          << modifiers << "'";
      return failure();
    }
    if (std::isupper(static_cast<unsigned char>(format[specifier]))) {
      emitError(location)
          << name << " does not yet support uppercase format conversion %"
          << format[specifier];
      return failure();
    }
    if (argumentIndex >= children.size()) {
      emitError(location) << name << " has too few formatting arguments";
      return failure();
    }
    Operation *argument = children[argumentIndex++];
    FailureOr<Value> lowered = lowerExpression(argument);
    if (failed(lowered))
      return failure();
    char conversion =
        static_cast<char>(std::tolower(static_cast<unsigned char>(format[specifier])));
    if (conversion == 's') {
      if (isa<sim::StringType>((*lowered).getType()))
        parts.push_back(*lowered);
      else {
        FailureOr<Value> scalar =
            toPackedScalar(*lowered, getSemanticLocation(argument));
        if (failed(scalar))
          return failure();
        parts.push_back(sim::SimStringFromPackedOp::create(
            builder, getSemanticLocation(argument), stringType, *scalar));
      }
    } else {
      int32_t radix = conversion == 'b'   ? 2
                      : conversion == 'o' ? 8
                      : conversion == 'h' || conversion == 'x' ? 16
                                                               : 10;
      if (conversion != 'd' && conversion != 'i' && conversion != 'u' &&
          conversion != 'b' && conversion != 'o' && conversion != 'h' &&
          conversion != 'x') {
        emitError(location) << name << " does not support format conversion %"
                            << format[specifier];
        return failure();
      }
      FailureOr<Value> scalar =
          toPackedScalar(*lowered, getSemanticLocation(argument));
      std::optional<unsigned> width =
          succeeded(scalar) ? sim::getPackedWidth((*scalar).getType())
                            : std::nullopt;
      if (failed(scalar) || !width || *width > 64) {
        emitError(getSemanticLocation(argument))
            << name << " currently requires integer arguments of at most 64 bits";
        return failure();
      }
      bool isSigned = (conversion == 'd' || conversion == 'i') &&
                      isSignedNode(argument);
      FailureOr<Value> integer =
          convert(*scalar, builder.getI64Type(), isSigned,
                  getSemanticLocation(argument));
      if (failed(integer))
        return failure();
      parts.push_back(sim::SimStringFormatIntegerOp::create(
          builder, getSemanticLocation(argument), stringType, *integer,
          builder.getI32IntegerAttr(radix), builder.getBoolAttr(isSigned)));
    }
    cursor = specifier + 1;
    textStart = cursor;
  }
  if (textStart < format.size())
    parts.push_back(stringLiteral(format.drop_front(textStart)));
  if (argumentIndex != children.size()) {
    emitError(location) << name << " has too many formatting arguments";
    return failure();
  }
  Value result = parts.empty()
                     ? stringLiteral("")
                     : Value(sim::SimStringConcatOp::create(
                           builder, location, stringType, parts));
  if (!task)
    return result;

  Operation *destinationNode = children.front();
  if (auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(
          destinationNode)) {
    SmallVector<Operation *> assignmentChildren = getChildren(assignment);
    if (assignmentChildren.empty())
      return failure();
    destinationNode = assignmentChildren.front();
  }
  FailureOr<Value> destination = lowerExpression(destinationNode, true);
  if (failed(destination) ||
      getReferenceElementType(*destination) != stringType) {
    emitError(getSemanticLocation(destinationNode))
        << "$sformat destination must be a string variable";
    return failure();
  }
  if (failed(storeReference(*destination, result, location)))
    return failure();
  return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                   builder.getBoolAttr(false))
      .getResult();
}

FailureOr<Value>
UnitLowering::lowerDisplaySystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef name = op.getCalleeName();
  Value context = function.getBody().front().getArgument(0);
  auto i32 = builder.getI32Type();

  auto constant = [&](IntegerType type, int64_t value) -> Value {
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getIntegerAttr(type, value));
  };
  auto lowerInteger = [&](Operation *child,
                          IntegerType type) -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(child);
    if (failed(value))
      return failure();
    return convert(*value, type, isSignedNode(child), location);
  };
  auto getStringLiteral = [&](Operation *child) {
    Operation *spelling = child;
    while (isa<semantic::SVConversionExpressionOp>(spelling)) {
      SmallVector<Operation *> convertedChildren = getChildren(spelling);
      if (convertedChildren.size() != 1)
        break;
      spelling = convertedChildren.front();
    }
    return dyn_cast<semantic::SVStringLiteralOp>(spelling);
  };
  auto lowerBytes = [&](Operation *child) -> FailureOr<Value> {
    auto literal = getStringLiteral(child);
    if (!literal) {
      emitError(getSemanticLocation(child))
          << "only literal strings are supported by this system call";
      return failure();
    }
    return sim::SimBytesConstantOp::create(builder,
                                           getSemanticLocation(literal),
                                           literal.getConstantValue())
        .getResult();
  };
  auto dummyTaskResult = [&]() -> Value {
    return constant(builder.getI1Type(), 0);
  };

  if (name == "$monitoron" || name == "$monitoroff") {
    if (!children.empty()) {
      emitError(location) << name << " accepts no arguments";
      return failure();
    }
    sim::SimMonitorControlOp::create(builder, location, name == "$monitoron");
    return dummyTaskResult();
  }

  if (name == "$printtimescale") {
    if (children.size() > 1) {
      emitError(location) << "$printtimescale accepts zero or one scope";
      return failure();
    }
    StringAttr targetPath = op.getSystemScopePathAttr();
    if (!children.empty())
      targetPath =
          children.front()->getAttrOfType<StringAttr>("referenced_path");
    if (!targetPath) {
      emitError(location) << "$printtimescale has no elaborated target scope";
      return failure();
    }

    sim::SimDesignOp design = function->getParentOfType<sim::SimDesignOp>();
    sim::SimScopeDeclOp targetScope;
    if (design)
      for (sim::SimScopeDeclOp scope :
           design.getBody().front().getOps<sim::SimScopeDeclOp>())
        if (scope.getHierarchicalName() &&
            *scope.getHierarchicalName() == targetPath.getValue()) {
          targetScope = scope;
          break;
        }
    if (!targetScope) {
      emitError(location) << "$printtimescale target scope '"
                          << targetPath.getValue()
                          << "' has no simulation descriptor";
      return failure();
    }
    auto unit =
        targetScope->getAttrOfType<IntegerAttr>("dpi_unit_femtoseconds");
    auto precision =
        targetScope->getAttrOfType<IntegerAttr>("dpi_precision_femtoseconds");
    if (!unit || !precision) {
      emitError(location) << "$printtimescale target has no frozen time scale";
      return failure();
    }

    auto formatScale = [](uint64_t femtoseconds) -> std::string {
      static constexpr std::pair<uint64_t, StringLiteral> scales[] = {
          {1'000'000'000'000'000ULL, "s"},
          {1'000'000'000'000ULL, "ms"},
          {1'000'000'000ULL, "us"},
          {1'000'000ULL, "ns"},
          {1'000ULL, "ps"},
          {1ULL, "fs"},
      };
      for (auto [factor, suffix] : scales)
        if (femtoseconds >= factor && femtoseconds % factor == 0)
          return (Twine(femtoseconds / factor) + suffix).str();
      return (Twine(femtoseconds) + "fs").str();
    };
    std::string text =
        (Twine("Time scale of (") + targetPath.getValue() + ") is " +
         formatScale(unit.getValue().getZExtValue()) + " / " +
         formatScale(precision.getValue().getZExtValue()))
            .str();
    Value item =
        sim::SimBytesConstantOp::create(builder, location, text).getResult();
    Value descriptor = constant(i32, 1);
    sim::SimDisplayOp::create(builder, location, context, descriptor, item,
                              true, 10, ArrayRef<int32_t>{0}, targetPath,
                              StringAttr{}, builder.getI64IntegerAttr(1),
                              IntegerAttr{});
    return dummyTaskResult();
  }

  StringRef postponedDisplay;
  bool persistentMonitor = false;
  if (name == "$strobe")
    postponedDisplay = "$display";
  else if (name == "$strobeb")
    postponedDisplay = "$displayb";
  else if (name == "$strobeo")
    postponedDisplay = "$displayo";
  else if (name == "$strobeh")
    postponedDisplay = "$displayh";
  else if (name == "$fstrobe")
    postponedDisplay = "$fdisplay";
  else if (name == "$fstrobeb")
    postponedDisplay = "$fdisplayb";
  else if (name == "$fstrobeo")
    postponedDisplay = "$fdisplayo";
  else if (name == "$fstrobeh")
    postponedDisplay = "$fdisplayh";
  else if (name == "$monitor") {
    postponedDisplay = "$display";
    persistentMonitor = true;
  } else if (name == "$monitorb") {
    postponedDisplay = "$displayb";
    persistentMonitor = true;
  } else if (name == "$monitoro") {
    postponedDisplay = "$displayo";
    persistentMonitor = true;
  } else if (name == "$monitorh") {
    postponedDisplay = "$displayh";
    persistentMonitor = true;
  } else if (name == "$fmonitor") {
    postponedDisplay = "$fdisplay";
    persistentMonitor = true;
  } else if (name == "$fmonitorb") {
    postponedDisplay = "$fdisplayb";
    persistentMonitor = true;
  } else if (name == "$fmonitoro") {
    postponedDisplay = "$fdisplayo";
    persistentMonitor = true;
  } else if (name == "$fmonitorh") {
    postponedDisplay = "$fdisplayh";
    persistentMonitor = true;
  }
  if (!postponedDisplay.empty()) {
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
        outlinePostponedDisplay(op, postponedDisplay, persistentMonitor);
    if (failed(callback))
      return failure();
    sim::SimSpawnOp spawned = sim::SimSpawnOp::create(
        builder, location, callback->first.getSymNameAttr(), callback->second,
        ArrayAttr{}, ArrayAttr{});
    if (persistentMonitor)
      sim::SimMonitorRegisterOp::create(builder, location,
                                        spawned.getProcess());
    return dummyTaskResult();
  }

  struct DisplayKind {
    bool file = false;
    bool newline = false;
    int32_t radix = 10;
    uint32_t descriptor = 1;
    size_t skippedArguments = 0;
    StringRef severity;
    bool fatal = false;
  };
  std::optional<DisplayKind> display;
  if (name == "$display")
    display = DisplayKind{false, true, 10};
  else if (name == "$displayb")
    display = DisplayKind{false, true, 2};
  else if (name == "$displayo")
    display = DisplayKind{false, true, 8};
  else if (name == "$displayh")
    display = DisplayKind{false, true, 16};
  else if (name == "$write")
    display = DisplayKind{false, false, 10};
  else if (name == "$writeb")
    display = DisplayKind{false, false, 2};
  else if (name == "$writeo")
    display = DisplayKind{false, false, 8};
  else if (name == "$writeh")
    display = DisplayKind{false, false, 16};
  else if (name == "$fdisplay")
    display = DisplayKind{true, true, 10};
  else if (name == "$fdisplayb")
    display = DisplayKind{true, true, 2};
  else if (name == "$fdisplayo")
    display = DisplayKind{true, true, 8};
  else if (name == "$fdisplayh")
    display = DisplayKind{true, true, 16};
  else if (name == "$fwrite")
    display = DisplayKind{true, false, 10};
  else if (name == "$fwriteb")
    display = DisplayKind{true, false, 2};
  else if (name == "$fwriteo")
    display = DisplayKind{true, false, 8};
  else if (name == "$fwriteh")
    display = DisplayKind{true, false, 16};
  else if (name == "$info")
    display = DisplayKind{false, true, 10, 0x80000002u, 0, "INFO", false};
  else if (name == "$warning")
    display = DisplayKind{false, true, 10, 0x80000002u, 0, "WARNING", false};
  else if (name == "$error")
    display = DisplayKind{false, true, 10, 0x80000002u, 0, "ERROR", false};
  else if (name == "$fatal")
    display =
        DisplayKind{false,   true, 10, 0x80000002u, children.empty() ? 0u : 1u,
                    "FATAL", true};
  if (display) {
    size_t firstItem = display->file ? 1 : display->skippedArguments;
    if (children.size() < firstItem) {
      emitError(location) << name << " has too few arguments";
      return failure();
    }
    Value verbosity;
    if (display->fatal) {
      verbosity = constant(i32, 1);
      if (!children.empty()) {
        FailureOr<Value> lowered = lowerInteger(children.front(), i32);
        if (failed(lowered))
          return failure();
        verbosity = *lowered;
      }
    }
    Value descriptor = constant(i32, static_cast<int32_t>(display->descriptor));
    if (display->file) {
      if (children.empty()) {
        emitError(location) << name << " requires a descriptor";
        return failure();
      }
      FailureOr<Value> lowered = lowerInteger(children.front(), i32);
      if (failed(lowered))
        return failure();
      descriptor = *lowered;
    }
    SmallVector<Value> items;
    SmallVector<int32_t> flags;
    if (!display->severity.empty()) {
      std::string file = "<unknown>";
      unsigned line = 0;
      if (auto source = location->findInstanceOf<FileLineColLoc>()) {
        file = source.getFilename().str();
        line = source.getLine();
      }
      std::string prefix =
          (Twine(display->severity) + ": " + file + ":" + Twine(line) + ": ")
              .str();
      if (children.size() == firstItem)
        prefix += name.str() + " called.";
      std::string escaped;
      escaped.reserve(prefix.size());
      for (char character : prefix) {
        escaped.push_back(character);
        if (character == '%')
          escaped.push_back('%');
      }
      items.push_back(
          sim::SimBytesConstantOp::create(builder, location, escaped)
              .getResult());
      flags.push_back(0);
    }
    for (Operation *child : ArrayRef(children).drop_front(firstItem)) {
      if (isa<semantic::SVEmptyArgumentExpressionOp>(child)) {
        flags.push_back(2);
      } else if (getStringLiteral(child)) {
        FailureOr<Value> value = lowerBytes(child);
        if (failed(value))
          return failure();
        items.push_back(*value);
        flags.push_back(0);
      } else {
        FailureOr<Value> value = lowerExpression(child);
        if (failed(value))
          return failure();
        if (isa<FloatType>((*value).getType())) {
          FailureOr<Value> real = convert(*value, builder.getF64Type(), false,
                                          getSemanticLocation(child));
          if (failed(real))
            return failure();
          items.push_back(*real);
          flags.push_back(4);
        } else if (isa<sim::StringType>((*value).getType())) {
          items.push_back(*value);
          flags.push_back(8);
        } else if (isa<sim::DynamicArrayType, sim::QueueType,
                       sim::AssocArrayType>((*value).getType())) {
          items.push_back(*value);
          flags.push_back(16);
        } else if (auto unionType =
                       dyn_cast<sim::UnpackedUnionType>((*value).getType());
                   unionType && unionType.getIsTagged()) {
          auto semanticType = child->getAttrOfType<TypeAttr>("semantic_type");
          if (!semanticType) {
            emitError(getSemanticLocation(child))
                << "tagged-union display operand has no semantic type";
            return failure();
          }
          FailureOr<Value> pattern = formatTaggedUnionPattern(
              *value, semanticType.getValue(), getSemanticLocation(child));
          if (failed(pattern))
            return failure();
          items.push_back(*pattern);
          flags.push_back(8);
        } else {
          FailureOr<Value> scalar =
              toPackedScalar(*value, getSemanticLocation(child));
          if (failed(scalar))
            return failure();
          items.push_back(*scalar);
          flags.push_back(isSignedNode(child) ? 1 : 0);
        }
      }
    }
    auto timeMultiplier =
        function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
    if (!timeMultiplier) {
      function.emitError("code unit has no frozen time scale");
      return failure();
    }
    StringAttr lexicalScope = op.getSystemScopePathAttr();
    if (!lexicalScope)
      lexicalScope =
          function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName);
    if (!lexicalScope) {
      op.emitError("display call has no elaborated lexical scope");
      return failure();
    }
    // %t rescales against the design's precision when $timeformat has changed
    // the display units, so the site carries that precision as a decimal
    // exponent in seconds.
    IntegerAttr timePrecision;
    if (auto design = function->getParentOfType<sim::SimDesignOp>()) {
      if (IntegerAttr precisionFs = design.getTimePrecisionFsAttr()) {
        int32_t exponent = -15;
        for (uint64_t scale = precisionFs.getValue().getZExtValue();
             scale > 1; scale /= 10)
          ++exponent;
        timePrecision = builder.getI32IntegerAttr(exponent);
      }
    }
    if (display->fatal)
      sim::SimFatalOp::create(builder, location, context, verbosity);
    sim::SimDisplayOp::create(builder, location, context, descriptor, items,
                              display->newline, display->radix, flags,
                              lexicalScope, op.getSystemLibraryCellAttr(),
                              timeMultiplier, timePrecision);
    if (display->fatal) {
      if (failed(emitFunctionReturn(location, std::nullopt, false)))
        return failure();
      setCurrent(addBlock());
    }
    return dummyTaskResult();
  }

  op.emitOpError("is not a supported display system call");
  return failure();
}

} // namespace obelisk::simlowering
