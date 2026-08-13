//===- LowerUnitDisplaySystemCalls.cpp - Lower display semantics --------===//

#include "LowerUnit.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

#include <optional>
#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

FailureOr<UnitLowering::LoweredOutputList>
UnitLowering::lowerOutputListItems(ArrayRef<Operation *> operations,
                                   bool interpretLiteralsAsFormats,
                                   std::optional<unsigned> designatedFormat) {
  LoweredOutputList output;
  Type stringType = sim::StringType::get(function.getContext());
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
  auto lowerByteArray = [&](Operation *child, Value value) -> FailureOr<Value> {
    auto array = dyn_cast<sim::UnpackedArrayType>(value.getType());
    if (!array || !isa<IntegerType>(array.getElementType()) ||
        cast<IntegerType>(array.getElementType()).getWidth() != 8)
      return failure();
    SmallVector<Value> characters;
    unsigned count = sim::getAggregateNumElements(array);
    Type logicType = sim::LogicType::get(function.getContext(), 8);
    characters.reserve(count);
    for (unsigned index = 0; index != count; ++index) {
      Value character = sim::SimAggregateExtractOp::create(
          builder, getSemanticLocation(child), array.getElementType(), value,
          index);
      FailureOr<Value> converted =
          convert(character, logicType, false, getSemanticLocation(child));
      if (failed(converted))
        return failure();
      characters.push_back(*converted);
    }
    Value packed = sim::SimLogicConcatOp::create(
        builder, getSemanticLocation(child),
        sim::LogicType::get(function.getContext(), count * 8), characters);
    // IEEE 1800-2017 21.2.1 and 21.3.3 define unpacked byte-array text in
    // left-bound-to-right-bound order. Aggregate operands use that logical
    // order, and string.from_packed reads its most-significant byte first.
    return sim::SimStringFromPackedOp::create(
               builder, getSemanticLocation(child), stringType, packed)
        .getResult();
  };

  for (auto [index, child] : llvm::enumerate(operations)) {
    bool isFormat = designatedFormat && index == *designatedFormat;
    if (isa<semantic::SVEmptyArgumentExpressionOp>(child)) {
      if (isFormat) {
        emitError(getSemanticLocation(child))
            << "format string cannot be omitted";
        return failure();
      }
      output.flags.push_back(2);
      continue;
    }
    if (auto literal = getStringLiteral(child)) {
      if (isFormat || interpretLiteralsAsFormats) {
        output.items.push_back(sim::SimBytesConstantOp::create(
            builder, getSemanticLocation(literal), literal.getConstantValue()));
        output.flags.push_back(isFormat ? 32 : 0);
      } else {
        output.items.push_back(sim::SimStringLiteralOp::create(
            builder, getSemanticLocation(literal), stringType,
            literal.getConstantValue()));
        output.flags.push_back(8);
      }
      continue;
    }

    FailureOr<Value> value = lowerExpression(child);
    if (failed(value))
      return failure();
    if (isFormat) {
      Value format = *value;
      if (!isa<sim::StringType>(format.getType())) {
        FailureOr<Value> bytes = lowerByteArray(child, format);
        if (succeeded(bytes))
          format = *bytes;
        else {
          FailureOr<Value> packed =
              toPackedScalar(format, getSemanticLocation(child));
          if (failed(packed)) {
            emitError(getSemanticLocation(child))
                << "format string must have integral, unpacked byte array, "
                   "or string type";
            return failure();
          }
          format = sim::SimStringFromPackedOp::create(
              builder, getSemanticLocation(child), stringType, *packed);
        }
      }
      output.items.push_back(format);
      output.flags.push_back(40);
      continue;
    }
    if (isa<FloatType>((*value).getType())) {
      FailureOr<Value> real = convert(*value, builder.getF64Type(), false,
                                      getSemanticLocation(child));
      if (failed(real))
        return failure();
      output.items.push_back(*real);
      output.flags.push_back(4);
    } else if (isa<sim::StringType>((*value).getType())) {
      output.items.push_back(*value);
      output.flags.push_back(8);
    } else if (auto bytes = lowerByteArray(child, *value); succeeded(bytes)) {
      output.items.push_back(*bytes);
      output.flags.push_back(8);
    } else if (isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
                   (*value).getType())) {
      output.items.push_back(*value);
      output.flags.push_back(16);
    } else if (auto unionType =
                   dyn_cast<sim::UnpackedUnionType>((*value).getType());
               unionType && unionType.getIsTagged()) {
      auto semanticType = child->getAttrOfType<TypeAttr>("semantic_type");
      if (!semanticType) {
        emitError(getSemanticLocation(child))
            << "tagged-union output operand has no semantic type";
        return failure();
      }
      FailureOr<Value> pattern = formatTaggedUnionPattern(
          *value, semanticType.getValue(), getSemanticLocation(child));
      if (failed(pattern))
        return failure();
      output.items.push_back(*pattern);
      output.flags.push_back(8);
    } else {
      FailureOr<Value> scalar =
          toPackedScalar(*value, getSemanticLocation(child));
      if (failed(scalar))
        return failure();
      output.items.push_back(*scalar);
      output.flags.push_back(isSignedNode(child) ? 1 : 0);
    }
  }
  return output;
}

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

  FailureOr<LoweredOutputList> output = lowerOutputListItems(
      ArrayRef(children).drop_front(formatIndex), false, 0);
  if (failed(output))
    return failure();
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
    op.emitError("string formatting call has no elaborated lexical scope");
    return failure();
  }
  IntegerAttr timePrecision;
  if (auto design = function->getParentOfType<sim::SimDesignOp>()) {
    if (IntegerAttr precisionFs = design.getTimePrecisionFsAttr()) {
      int32_t exponent = -15;
      for (uint64_t scale = precisionFs.getValue().getZExtValue(); scale > 1;
           scale /= 10)
        ++exponent;
      timePrecision = builder.getI32IntegerAttr(exponent);
    }
  }
  Type stringType = sim::StringType::get(function.getContext());
  Value result = sim::SimStringOutputFormatOp::create(
      builder, location, stringType, function.getBody().front().getArgument(0),
      output->items, 10, output->flags, lexicalScope,
      op.getSystemLibraryCellAttr(), timeMultiplier, timePrecision);
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
  if (failed(destination)) {
    emitError(getSemanticLocation(destinationNode))
        << "$sformat destination must be a writable variable";
    return failure();
  }
  Type destinationType = getReferenceElementType(*destination);
  bool integral =
      isa<IntegerType, sim::LogicType, sim::PackedArrayType,
          sim::PackedStructType, sim::PackedUnionType>(destinationType);
  bool byteArray = false;
  if (auto array = dyn_cast<sim::UnpackedArrayType>(destinationType))
    if (auto element = dyn_cast<IntegerType>(array.getElementType()))
      byteArray = element.getWidth() == 8;
  if (!isa<sim::StringType>(destinationType) && !integral && !byteArray) {
    emitError(getSemanticLocation(destinationNode))
        << "$sformat destination must have integral, unpacked byte array, or "
           "string type";
    return failure();
  }
  FailureOr<Value> converted = failure();
  if (byteArray) {
    // IEEE 1800-2017 5.9 and 21.3.3: string assignment to an unpacked
    // byte array is left-justified, in left-bound-to-right-bound order.
    // getc returns zero beyond the string, providing the required fill.
    SmallVector<Value> bytes;
    unsigned count = sim::getAggregateNumElements(destinationType);
    Type elementType =
        cast<sim::UnpackedArrayType>(destinationType).getElementType();
    bytes.reserve(count);
    for (unsigned index = 0; index != count; ++index) {
      Value position =
          arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                    builder.getI64IntegerAttr(index));
      bytes.push_back(sim::SimStringGetcOp::create(
          builder, location, elementType, result, position));
    }
    converted = sim::SimAggregateConstructOp::create(builder, location,
                                                     destinationType, bytes)
                    .getResult();
  } else {
    converted = convert(result, destinationType, false, location);
  }
  if (failed(converted) ||
      failed(storeReference(*destination, *converted, location)))
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
    bool stringOutput = false;
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
  else if (name == "$swrite") {
    display = DisplayKind{false, false, 10};
    display->stringOutput = true;
  } else if (name == "$swriteb") {
    display = DisplayKind{false, false, 2};
    display->stringOutput = true;
  } else if (name == "$swriteo") {
    display = DisplayKind{false, false, 8};
    display->stringOutput = true;
  } else if (name == "$swriteh") {
    display = DisplayKind{false, false, 16};
    display->stringOutput = true;
  } else if (name == "$info")
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
    size_t firstItem =
        display->file || display->stringOutput ? 1 : display->skippedArguments;
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
    FailureOr<LoweredOutputList> output =
        lowerOutputListItems(ArrayRef(children).drop_front(firstItem), true);
    if (failed(output))
      return failure();
    llvm::append_range(items, output->items);
    llvm::append_range(flags, output->flags);
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
    if (display->stringOutput) {
      Operation *destinationNode = children.front();
      if (auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(
              destinationNode)) {
        SmallVector<Operation *> assignmentChildren = getChildren(assignment);
        if (!assignmentChildren.empty())
          destinationNode = assignmentChildren.front();
      }
      FailureOr<Value> destination = lowerExpression(destinationNode, true);
      if (failed(destination)) {
        emitError(getSemanticLocation(destinationNode))
            << name << " destination must be a writable variable";
        return failure();
      }
      Type destinationType = getReferenceElementType(*destination);
      bool integral =
          isa<IntegerType, sim::LogicType, sim::PackedArrayType,
              sim::PackedStructType, sim::PackedUnionType>(destinationType);
      bool byteArray = false;
      if (auto array = dyn_cast<sim::UnpackedArrayType>(destinationType))
        if (auto element = dyn_cast<IntegerType>(array.getElementType()))
          byteArray = element.getWidth() == 8;
      if (!isa<sim::StringType>(destinationType) && !integral && !byteArray) {
        emitError(getSemanticLocation(destinationNode))
            << name
            << " destination must have integral, unpacked byte array, or "
               "string type";
        return failure();
      }
      Value result = sim::SimStringOutputFormatOp::create(
          builder, location, sim::StringType::get(function.getContext()),
          context, items, display->radix, flags, lexicalScope,
          op.getSystemLibraryCellAttr(), timeMultiplier, timePrecision);
      FailureOr<Value> converted = failure();
      if (byteArray) {
        // IEEE 1800-2017 5.9 and 21.3.3: string assignment to an unpacked
        // byte array is left-justified, in left-bound-to-right-bound order.
        // getc returns zero beyond the string, providing the required fill.
        SmallVector<Value> bytes;
        unsigned count = sim::getAggregateNumElements(destinationType);
        Type elementType =
            cast<sim::UnpackedArrayType>(destinationType).getElementType();
        bytes.reserve(count);
        for (unsigned index = 0; index != count; ++index) {
          Value position =
              arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                        builder.getI64IntegerAttr(index));
          bytes.push_back(sim::SimStringGetcOp::create(
              builder, location, elementType, result, position));
        }
        converted = sim::SimAggregateConstructOp::create(
                        builder, location, destinationType, bytes)
                        .getResult();
      } else {
        converted = convert(result, destinationType, false, location);
      }
      if (failed(converted) ||
          failed(storeReference(*destination, *converted, location)))
        return failure();
      return dummyTaskResult();
    }
    if (name == "$error")
      sim::SimErrorOp::create(builder, location, context);
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
