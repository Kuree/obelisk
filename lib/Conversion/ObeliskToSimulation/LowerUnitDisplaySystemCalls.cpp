//===- LowerUnitDisplaySystemCalls.cpp - Lower display semantics --------===//

#include "ContainerElementDescriptor.h"
#include "LowerUnit.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/OutputItemFlags.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"

#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

namespace {

bool isUnpackedAggregateType(Type type) {
  return isa<sim::UnpackedArrayType, sim::UnpackedStructType,
             sim::UnpackedUnionType>(type);
}

// Escape the characters a generated format string would otherwise interpret.
void appendFormatText(std::string &format, StringRef text) {
  for (char character : text) {
    if (character == '%')
      format.push_back('%');
    format.push_back(character);
  }
}

/// The `#simulation.enum_format` of source enum type `type` in `formats`.
sim::EnumFormatAttr findEnumFormat(ArrayAttr formats, Type type) {
  if (!formats || !isa_and_nonnull<semantic::EnumType>(type))
    return {};
  for (Attribute attribute : formats)
    if (auto format = dyn_cast<sim::EnumFormatAttr>(attribute);
        format && format.getType() == type)
      return format;
  return {};
}

/// Whether the IEEE 1800-2023 21.2.1.6 rendering of a source type needs
/// names only the source type has: structure members, enum names, or a
/// chandle, which the runtime's container rendering cannot know.
bool needsSourcePattern(Type type) {
  if (!type)
    return false;
  if (isa<semantic::EnumType, semantic::SourceAggregateType,
          semantic::ChandleType>(type))
    return true;
  if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(type))
    return needsSourcePattern(array.getElementType());
  if (auto array = dyn_cast<semantic::UnpackedArrayType>(type))
    return needsSourcePattern(array.getElementType());
  if (auto queue = dyn_cast<semantic::QueueType>(type))
    return needsSourcePattern(queue.getElementType());
  if (auto array = dyn_cast<semantic::DynArrayType>(type))
    return needsSourcePattern(array.getElementType());
  if (auto assoc = dyn_cast<semantic::AssocArrayType>(type))
    return needsSourcePattern(assoc.getKeyType()) ||
           needsSourcePattern(assoc.getElementType());
  return false;
}

} // namespace

// The decimal exponent, in seconds, of one design-precision tick. %t needs it
// to rescale when $timeformat has overridden the display units.
IntegerAttr UnitLowering::designTimePrecisionExponent() {
  auto design = function->getParentOfType<sim::SimDesignOp>();
  IntegerAttr precisionFs = design ? design.getTimePrecisionFsAttr() : nullptr;
  if (!precisionFs)
    return {};
  int32_t exponent = -15;
  for (uint64_t scale = precisionFs.getValue().getZExtValue(); scale > 1;
       scale /= 10)
    ++exponent;
  return builder.getI32IntegerAttr(exponent);
}

FailureOr<Value> UnitLowering::currentTimeInUnits(Location location) {
  auto scale = sim::get<sim::metadata::DelayScale>(function);
  if (!scale || !scale.getValue().isStrictlyPositive()) {
    function.emitError("code unit has no valid frozen time scale");
    return failure();
  }
  Type i64 = builder.getI64Type();
  Value context = function.getBody().front().getArgument(0);
  Value now = sim::SimTimeNowOp::create(builder, location, i64, context);
  // IEEE 1800-2017 20.3.1: $time returns the precision-tick count scaled to the
  // enclosing scope's time unit and rounded, rather than truncated, to an
  // integer.
  Value scaleValue = arith::ConstantOp::create(builder, location, i64, scale);
  Value quotient = arith::DivUIOp::create(builder, location, now, scaleValue);
  Value remainder = arith::RemUIOp::create(builder, location, now, scaleValue);
  uint64_t ticks = scale.getValue().getZExtValue();
  Value halfway = arith::ConstantOp::create(
      builder, location, i64, builder.getI64IntegerAttr(ticks / 2 + ticks % 2));
  Value increment = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::uge, remainder, halfway);
  Value extended = arith::ExtUIOp::create(builder, location, i64, increment);
  return arith::AddIOp::create(builder, location, quotient, extended)
      .getResult();
}

LogicalResult UnitLowering::emitTerminationDiagnostic(StringRef name,
                                                      Value verbosity,
                                                      Location location) {
  // IEEE 1800-2017 20.2, Table 20-1: verbosity 0 prints nothing, and 1 and 2
  // print the simulation time and the location of the call. The extra memory
  // and CPU statistics 2 asks for have no defined content and no source in the
  // runtime, so 2 prints what 1 does.
  APInt folded;
  bool isConstant = matchPattern(verbosity, m_ConstantInt(&folded));
  if (isConstant && folded.isZero())
    return success();

  StringAttr lexicalScope = sim::get<sim::metadata::HierarchicalName>(function);
  if (!lexicalScope) {
    function.emitError("code unit has no lexical scope");
    return failure();
  }

  Block *merge = nullptr;
  if (!isConstant) {
    // A run-time verbosity picks its Table 20-1 row on the way past. Branching
    // rather than selecting a rendered message keeps the diagnostic free of
    // managed string state, which would push the whole design out of native
    // scheduling for the sake of a message that usually is not printed.
    Value zero =
        arith::ConstantOp::create(builder, location, verbosity.getType(),
                                  builder.getZeroAttr(verbosity.getType()));
    Value wanted = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, verbosity, zero);
    Block *report = addBlock();
    merge = addBlock();
    cf::CondBranchOp::create(builder, location, wanted, report, ValueRange{},
                             merge, ValueRange{});
    setCurrent(report);
  }

  // currentTimeInUnits has already rejected a missing or unusable scale.
  FailureOr<Value> time = currentTimeInUnits(location);
  if (failed(time))
    return failure();
  auto timeMultiplier = sim::get<sim::metadata::DelayScale>(function);

  std::string file = "<unknown>";
  unsigned line = 0;
  if (auto source = location->findInstanceOf<FileLineColLoc>()) {
    file = source.getFilename().str();
    line = source.getLine();
  }
  // The leading item of a display list is read as the format for the items
  // after it, so the fixed text is escaped and the time becomes its argument.
  std::string format;
  appendFormatText(format, name);
  appendFormatText(format, ": " + file + ":" + std::to_string(line) + ": ");
  format += "simulation time %0t\n";

  // Table 20-1's diagnostic is simulator commentary, not design output, so it
  // joins the severity tasks on standard error rather than in the design's
  // stdout stream.
  Value context = function.getBody().front().getArgument(0);
  Value descriptor = arith::ConstantOp::create(
      builder, location, builder.getI32Type(),
      builder.getI32IntegerAttr(static_cast<int32_t>(0x80000002u)));
  Value text = sim::SimBytesConstantOp::create(builder, location, format);
  sim::SimDisplayOp::create(
      builder, location, context, descriptor, ValueRange{text, *time},
      /*newline=*/false, sim::Radix::Decimal,
      builder.getDenseI32ArrayAttr({0, 0}), lexicalScope, StringAttr{},
      timeMultiplier, designTimePrecisionExponent());
  if (merge) {
    cf::BranchOp::create(builder, location, merge, ValueRange{});
    setCurrent(merge);
  }
  return success();
}

FailureOr<Value> UnitLowering::formatAssignmentPattern(Value value,
                                                       Type semanticType,
                                                       ArrayAttr enumFormats,
                                                       Location location) {
  auto timeMultiplier = sim::get<sim::metadata::DelayScale>(function);
  StringAttr lexicalScope = sim::get<sim::metadata::HierarchicalName>(function);
  if (!timeMultiplier || !lexicalScope)
    return failure();

  std::string format;
  SmallVector<Value> items;
  SmallVector<int32_t> flags;
  // A leading empty format item is what SimStringOutputFormatOp expects to
  // find the designated format in; it is filled in once the walk knows the
  // complete pattern text.
  items.push_back(Value{});
  flags.push_back(OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT);
  auto appendRendered = [&](Value rendered) {
    format.append("%p");
    items.push_back(rendered);
    flags.push_back(OBELISK_RT_OUTPUT_ITEM_STRING);
  };

  // IEEE 1800-2023 21.2.1.6: an unpacked type is traversed until a singular
  // type is reached. Structures print their element names, arrays print bare
  // elements, and a union prints only its first declared element. Among the
  // singular elements, a packed structure prints as a named pattern, an enum
  // by its name and a string in quotes.
  std::function<LogicalResult(Value, Type)> traverse =
      [&](Value element, Type source) -> LogicalResult {
    Type type = element.getType();
    auto aggregate = dyn_cast_or_null<semantic::SourceAggregateType>(source);
    // The source type of the member named `name`.
    auto fieldSource = [&](StringAttr name) -> Type {
      if (!aggregate)
        return {};
      for (Attribute attribute : aggregate.getFields()) {
        auto field = dyn_cast<DictionaryAttr>(attribute);
        auto fieldType = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
        if (fieldType && field.get("name") == name)
          return fieldType.getValue();
      }
      return {};
    };
    if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type)) {
      if (unionType.getIsTagged()) {
        FailureOr<Value> rendered =
            formatTaggedUnionPattern(element, source, enumFormats, location);
        if (failed(rendered))
          return failure();
        appendRendered(*rendered);
        return success();
      }
      auto first = unionType.getFields().empty()
                       ? sim::FieldAttr{}
                       : dyn_cast<sim::FieldAttr>(unionType.getFields()[0]);
      if (!first)
        return failure();
      format.append("'{");
      appendFormatText(format, first.getName().getValue());
      format.push_back(':');
      Type fieldType = sim::getAggregateElementType(type, 0);
      if (failed(traverse(sim::SimUnionExtractOp::create(builder, location,
                                                         fieldType, element, 0),
                          fieldSource(first.getName()))))
        return failure();
      format.push_back('}');
      return success();
    }
    if (isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type)) {
      auto structType = dyn_cast<sim::UnpackedStructType>(type);
      ArrayAttr fields = structType ? structType.getFields() : ArrayAttr{};
      Type elementSource;
      if (auto array =
              dyn_cast_or_null<semantic::RangedUnpackedArrayType>(source))
        elementSource = array.getElementType();
      else if (auto array =
                   dyn_cast_or_null<semantic::UnpackedArrayType>(source))
        elementSource = array.getElementType();
      unsigned count = sim::getAggregateNumElements(type);
      format.append("'{");
      for (unsigned index = 0; index != count; ++index) {
        if (index != 0)
          format.append(", ");
        Type memberSource = elementSource;
        if (fields && index < fields.size())
          if (auto field = dyn_cast<sim::FieldAttr>(fields[index])) {
            appendFormatText(format, field.getName().getValue());
            format.push_back(':');
            memberSource = fieldSource(field.getName());
          }
        Type elementType = sim::getAggregateElementType(type, index);
        if (failed(traverse(sim::SimAggregateExtractOp::create(
                                builder, location, elementType, element, index),
                            memberSource)))
          return failure();
      }
      format.push_back('}');
      return success();
    }
    if (isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(type)) {
      if (needsSourcePattern(source)) {
        FailureOr<Value> rendered =
            formatContainerPattern(element, source, enumFormats, location);
        if (failed(rendered))
          return failure();
        appendRendered(*rendered);
        return success();
      }
      format.append("%p");
      items.push_back(element);
      flags.push_back(OBELISK_RT_OUTPUT_ITEM_CONTAINER);
      return success();
    }
    if (aggregate && aggregate.getIsPacked() && !aggregate.getIsUnion()) {
      FailureOr<Value> scalar = toPackedScalar(element, location);
      if (failed(scalar))
        return failure();
      format.append("'{");
      for (auto [index, attribute] : llvm::enumerate(aggregate.getFields())) {
        auto field = dyn_cast<DictionaryAttr>(attribute);
        auto name = field ? field.getAs<StringAttr>("name") : StringAttr{};
        auto offset =
            field ? field.getAs<IntegerAttr>("packed_offset") : IntegerAttr{};
        auto memberTypeAttr =
            field ? field.getAs<TypeAttr>("type") : TypeAttr{};
        Type memberSource = memberTypeAttr ? memberTypeAttr.getValue() : Type{};
        FailureOr<Type> memberType =
            memberSource
                ? normalizeSemanticType(memberSource, location, function)
                : FailureOr<Type>(failure());
        std::optional<unsigned> width =
            succeeded(memberType)
                ? sim::getPackedWidth(sim::getPackedScalarType(*memberType))
                : std::nullopt;
        if (!name || !offset || !width)
          return failure();
        if (index != 0)
          format.append(", ");
        appendFormatText(format, name.getValue());
        format.push_back(':');
        Value bits;
        if (auto logic = dyn_cast<sim::LogicType>((*scalar).getType())) {
          bits = sim::SimLogicExtractOp::create(
              builder, location,
              sim::LogicType::get(builder.getContext(), *width), *scalar,
              builder.getI64IntegerAttr(offset.getInt()));
        } else {
          Value shifted = *scalar;
          if (offset.getInt() != 0)
            shifted = arith::ShRUIOp::create(
                builder, location, *scalar,
                arith::ConstantOp::create(
                    builder, location, (*scalar).getType(),
                    builder.getIntegerAttr((*scalar).getType(),
                                           offset.getInt())));
          bits = shifted;
          if (shifted.getType() != builder.getIntegerType(*width))
            bits = arith::TruncIOp::create(
                builder, location, builder.getIntegerType(*width), shifted);
        }
        FailureOr<Value> member = convert(bits, *memberType, false, location);
        if (failed(member) || failed(traverse(*member, memberSource)))
          return failure();
      }
      format.push_back('}');
      return success();
    }
    if (auto enumFormat = findEnumFormat(enumFormats, source)) {
      FailureOr<Value> scalar = toPackedScalar(element, location);
      FailureOr<Value> name =
          succeeded(scalar)
              ? lowerEnumFormatName(*scalar, enumFormat.getValues(),
                                    enumFormat.getNames(), location)
              : FailureOr<Value>(failure());
      if (failed(name))
        return failure();
      format.append("%p");
      items.append({*scalar, *name});
      flags.push_back(
          OBELISK_RT_OUTPUT_ITEM_ENUM |
          (isSignedSemanticType(source) ? OBELISK_RT_OUTPUT_ITEM_SIGNED : 0));
      return success();
    }
    if (isa<sim::StringType>(type)) {
      format.append("\"%p\"");
      items.push_back(element);
      flags.push_back(OBELISK_RT_OUTPUT_ITEM_STRING);
      return success();
    }
    if (isa<sim::ChandleType>(type)) {
      // A chandle has no value spelling of its own; only null is required
      // to print as the word null.
      Type stringType = sim::StringType::get(function.getContext());
      Value isNull = sim::SimChandleEqualOp::create(
          builder, location, builder.getI1Type(), element,
          sim::SimChandleNullOp::create(builder, location));
      appendRendered(arith::SelectOp::create(
          builder, location, isNull,
          sim::SimStringLiteralOp::create(builder, location, stringType,
                                          "null"),
          sim::SimStringLiteralOp::create(builder, location, stringType,
                                          "chandle")));
      return success();
    }
    format.append("%p");
    if (isa<FloatType>(type)) {
      FailureOr<Value> real =
          convert(element, builder.getF64Type(), false, location);
      if (failed(real))
        return failure();
      items.push_back(*real);
      flags.push_back(OBELISK_RT_OUTPUT_ITEM_REAL);
      return success();
    }
    if (isa<sim::ClassHandleType>(type)) {
      items.push_back(element);
      flags.push_back(OBELISK_RT_OUTPUT_ITEM_CLASS);
      return success();
    }
    if (isa<sim::ProcessType>(type)) {
      items.push_back(element);
      flags.push_back(OBELISK_RT_OUTPUT_ITEM_PROCESS);
      return success();
    }
    if (isa<sim::VirtualInterfaceType>(type)) {
      items.push_back(element);
      flags.push_back(OBELISK_RT_OUTPUT_ITEM_VIRTUAL_INTERFACE);
      return success();
    }
    FailureOr<Value> scalar = toPackedScalar(element, location);
    if (failed(scalar))
      return failure();
    items.push_back(*scalar);
    flags.push_back(source && isSignedSemanticType(source)
                        ? OBELISK_RT_OUTPUT_ITEM_SIGNED
                        : 0);
    return success();
  };
  if (failed(traverse(value, semanticType)))
    return failure();
  // A value rendered as a whole needs no further formatting.
  if (format == "%p" && flags.back() == OBELISK_RT_OUTPUT_ITEM_STRING)
    return items.back();

  items.front() = sim::SimBytesConstantOp::create(builder, location, format);
  return sim::SimStringOutputFormatOp::create(
             builder, location, sim::StringType::get(function.getContext()),
             function.getBody().front().getArgument(0), items,
             sim::Radix::Decimal, flags, lexicalScope, StringAttr{},
             timeMultiplier, designTimePrecisionExponent())
      .getResult();
}

FailureOr<Value> UnitLowering::formatContainerPattern(Value container,
                                                      Type semanticType,
                                                      ArrayAttr enumFormats,
                                                      Location location) {
  // IEEE 1800-2023 21.2.1.6 renders a queue or dynamic array like an
  // unpacked array and an associative array with its index labels. Element
  // names come from the source type, so render each element in turn.
  Type stringType = sim::StringType::get(function.getContext());
  Type i64 = builder.getI64Type();
  auto literal = [&](StringRef text) {
    return sim::SimStringLiteralOp::create(builder, location, stringType, text)
        .getResult();
  };
  auto concat = [&](ValueRange parts) {
    return sim::SimStringConcatOp::create(builder, location, stringType, parts)
        .getResult();
  };
  Block *header = addBlock();
  Block *body = addBlock();
  Block *exit = addBlock();
  exit->addArgument(stringType, location);
  if (auto assoc = dyn_cast<sim::AssocArrayType>(container.getType())) {
    auto source = dyn_cast_or_null<semantic::AssocArrayType>(semanticType);
    if (!source)
      return failure();
    Value noKey = createDefaultValue(builder, location, assoc.getKeyType());
    if (!noKey)
      return failure();
    FailureOr<std::pair<Value, Value>> first = traverseAssoc(
        container, noKey, /*direction=*/1, /*endpoint=*/true, location);
    if (failed(first))
      return failure();
    header->addArgument(assoc.getKeyType(), location);
    header->addArgument(builder.getI1Type(), location);
    header->addArgument(stringType, location);
    header->addArgument(stringType, location);
    cf::BranchOp::create(
        builder, location, header,
        ValueRange{first->first, first->second, literal("'{"), literal("")});
    setCurrent(header);
    cf::CondBranchOp::create(builder, location, header->getArgument(1), body,
                             ValueRange{}, exit,
                             ValueRange{header->getArgument(2)});
    setCurrent(body);
    Value key = header->getArgument(0);
    Value element = sim::SimAssocReadOp::create(
        builder, location, assoc.getElementType(), container, key);
    FailureOr<Value> keyText = formatAssignmentPattern(key, source.getKeyType(),
                                                       enumFormats, location);
    if (failed(keyText))
      return failure();
    FailureOr<Value> elementText = formatAssignmentPattern(
        element, source.getElementType(), enumFormats, location);
    if (failed(elementText))
      return failure();
    Value text = concat({header->getArgument(2), header->getArgument(3),
                         *keyText, literal(":"), *elementText});
    FailureOr<std::pair<Value, Value>> next = traverseAssoc(
        container, key, /*direction=*/1, /*endpoint=*/false, location);
    if (failed(next))
      return failure();
    cf::BranchOp::create(
        builder, location, header,
        ValueRange{next->first, next->second, text, literal(", ")});
  } else {
    Type elementSource;
    if (auto queue = dyn_cast_or_null<semantic::QueueType>(semanticType))
      elementSource = queue.getElementType();
    else if (auto array =
                 dyn_cast_or_null<semantic::DynArrayType>(semanticType))
      elementSource = array.getElementType();
    Type elementType;
    if (auto queue = dyn_cast<sim::QueueType>(container.getType()))
      elementType = queue.getElementType();
    else if (auto array = dyn_cast<sim::DynamicArrayType>(container.getType()))
      elementType = array.getElementType();
    if (!elementSource || !elementType)
      return failure();
    Value size =
        sim::SimContainerSizeOp::create(builder, location, i64, container);
    header->addArgument(i64, location);
    header->addArgument(stringType, location);
    header->addArgument(stringType, location);
    cf::BranchOp::create(
        builder, location, header,
        ValueRange{arith::ConstantOp::create(builder, location, i64,
                                             builder.getI64IntegerAttr(0)),
                   literal("'{"), literal("")});
    setCurrent(header);
    Value index = header->getArgument(0);
    Value more = arith::CmpIOp::create(builder, location,
                                       arith::CmpIPredicate::ult, index, size);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                             ValueRange{header->getArgument(1)});
    setCurrent(body);
    Value element = sim::SimContainerReadOp::create(
        builder, location, elementType, container, index);
    FailureOr<Value> elementText =
        formatAssignmentPattern(element, elementSource, enumFormats, location);
    if (failed(elementText))
      return failure();
    Value text =
        concat({header->getArgument(1), header->getArgument(2), *elementText});
    Value nextIndex = arith::AddIOp::create(
        builder, location, index,
        arith::ConstantOp::create(builder, location, i64,
                                  builder.getI64IntegerAttr(1)));
    cf::BranchOp::create(builder, location, header,
                         ValueRange{nextIndex, text, literal(", ")});
  }
  setCurrent(exit);
  return concat({exit->getArgument(0), literal("}")});
}

FailureOr<std::pair<Value, Value>>
UnitLowering::formatUnpackedAggregateRaw(Value value, Location location,
                                         char mode) {
  Type rootType = value.getType();
  if (!isa<sim::UnpackedStructType, sim::UnpackedUnionType>(rootType))
    return failure();
  auto timeMultiplier = sim::get<sim::metadata::DelayScale>(function);
  StringAttr lexicalScope = sim::get<sim::metadata::HierarchicalName>(function);
  if (!timeMultiplier || !lexicalScope)
    return failure();

  Type stringType = sim::StringType::get(function.getContext());
  SmallVector<Value> leaves;

  // IEEE 1800-2017 21.2.1.5 recursively formats unpacked structures in
  // declaration order. Each singular integral leaf owns its own
  // ceil(width/32) record; an untagged union contributes its first member.
  std::function<LogicalResult(Value)> traverse =
      [&](Value element) -> LogicalResult {
    Type type = element.getType();
    if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type)) {
      if (unionType.getIsTagged() || unionType.getFields().empty())
        return failure();
      Type fieldType = sim::getAggregateElementType(type, 0);
      return traverse(sim::SimUnionExtractOp::create(builder, location,
                                                     fieldType, element, 0));
    }
    if (isa<sim::UnpackedStructType>(type)) {
      unsigned count = sim::getAggregateNumElements(type);
      if (count == 0)
        return failure();
      for (unsigned ordinal = 0; ordinal != count; ++ordinal) {
        Type fieldType = sim::getAggregateElementType(type, ordinal);
        Value field = sim::SimAggregateExtractOp::create(
            builder, location, fieldType, element, ordinal);
        if (failed(traverse(field)))
          return failure();
      }
      return success();
    }
    if (!sim::getPackedScalarType(type))
      return failure();
    FailureOr<Value> scalar = toPackedScalar(element, location);
    if (failed(scalar))
      return failure();
    leaves.push_back(*scalar);
    return success();
  };
  if (failed(traverse(value)) || leaves.empty())
    return failure();

  // One formatting call can carry one conversion per scalar leaf. This keeps
  // each leaf's independent raw-word padding while avoiding one managed-string
  // allocation per leaf and a subsequent concatenation at run time.
  auto formatLeaves = [&](char specifier) -> Value {
    std::string format;
    format.reserve(leaves.size() * 2);
    SmallVector<Value> items;
    SmallVector<int32_t> flags;
    items.reserve(leaves.size() + 1);
    flags.assign(leaves.size() + 1, 0);
    flags.front() = OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT;
    for (Value leaf : leaves) {
      format.push_back('%');
      format.push_back(specifier);
      items.push_back(leaf);
    }
    items.insert(items.begin(),
                 sim::SimBytesConstantOp::create(builder, location, format));
    return sim::SimStringOutputFormatOp::create(
               builder, location, stringType,
               function.getBody().front().getArgument(0), items,
               sim::Radix::Decimal, flags, lexicalScope, StringAttr{},
               timeMultiplier, designTimePrecisionExponent())
        .getResult();
  };
  return std::pair<Value, Value>{mode == 'z' ? Value{} : formatLeaves('u'),
                                 mode == 'u' ? Value{} : formatLeaves('z')};
}

FailureOr<Value> UnitLowering::lowerEnumFormatName(Value receiver,
                                                   ArrayAttr values,
                                                   ArrayAttr names,
                                                   Location location) {
  if (!values || values.empty() || !names || values.size() != names.size() ||
      !sim::getPackedScalarType(receiver.getType()))
    return failure();
  Type stringType = sim::StringType::get(function.getContext());
  Value result = sim::SimStringLiteralOp::create(builder, location, stringType,
                                                 builder.getStringAttr(""));
  for (auto [valueAttribute, nameAttribute] : llvm::zip_equal(values, names)) {
    auto frozen = dyn_cast<sim::FrozenConstantAttr>(valueAttribute);
    auto spelling = dyn_cast<StringAttr>(nameAttribute);
    FailureOr<Value> member =
        frozen && frozen.getType() == receiver.getType()
            ? sim::materializeFrozenConstant(builder, location, frozen)
            : FailureOr<Value>(failure());
    if (failed(member) || !spelling)
      return failure();
    FailureOr<Value> equal =
        conditionalEqual(receiver, *member, receiver.getType(), location,
                         /*caseEquality=*/true);
    if (failed(equal))
      return failure();
    Value candidate = sim::SimStringLiteralOp::create(builder, location,
                                                      stringType, spelling);
    result =
        arith::SelectOp::create(builder, location, *equal, candidate, result);
  }
  return result;
}

FailureOr<UnitLowering::LoweredOutputList>
UnitLowering::lowerOutputListItems(ArrayRef<Operation *> operations,
                                   bool interpretLiteralsAsFormats,
                                   std::optional<unsigned> designatedFormat) {
  LoweredOutputList output;
  output.sourceValues.resize(operations.size());
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
  // Track which source operand a statically known format conversion consumes.
  // This lets ordinary/default aggregate output keep its old single pattern
  // string, and lets literal %u or %z build only the representation that can
  // actually be selected. A dynamic format conservatively keeps all three.
  auto parseConversions = [](StringRef format,
                             SmallVectorImpl<char> &result) -> bool {
    auto parseUnsigned = [&](size_t &position) -> bool {
      uint64_t value = 0;
      size_t start = position;
      while (position < format.size() &&
             std::isdigit(static_cast<unsigned char>(format[position]))) {
        value = value * 10 + static_cast<unsigned>(format[position] - '0');
        if (value > std::numeric_limits<uint32_t>::max())
          return false;
        ++position;
      }
      return position != start;
    };
    for (size_t position = 0; position < format.size();) {
      if (format[position++] != '%')
        continue;
      if (position < format.size() && format[position] == '%') {
        ++position;
        continue;
      }
      bool left = false;
      bool zero = false;
      while (position < format.size()) {
        if (format[position] == '-' && !left) {
          left = true;
          ++position;
        } else if (format[position] == '0' && !zero) {
          zero = true;
          ++position;
        } else {
          break;
        }
      }
      bool width = false;
      if (position < format.size() &&
          std::isdigit(static_cast<unsigned char>(format[position]))) {
        if (!parseUnsigned(position))
          return false;
        width = true;
      }
      bool precision = false;
      if (position < format.size() && format[position] == '.') {
        ++position;
        precision = true;
        if (position < format.size() &&
            std::isdigit(static_cast<unsigned char>(format[position])) &&
            !parseUnsigned(position))
          return false;
      }
      if (position == format.size())
        return false;
      char specifier = format[position++];
      char spec = static_cast<char>(
          std::tolower(static_cast<unsigned char>(specifier)));
      bool integer = spec == 'b' || spec == 'o' || spec == 'd' || spec == 'h' ||
                     spec == 'x';
      bool floating = spec == 'e' || spec == 'f' || spec == 'g';
      bool widthAllowed = integer || floating || spec == 's' || spec == 't';
      bool nonConsuming = spec == 'm' || spec == 'l';
      bool recognized = widthAllowed || nonConsuming || spec == 'c' ||
                        spec == 'v' || spec == 'u' || spec == 'z' ||
                        spec == 'p';
      if (!recognized || ((width || left) && !widthAllowed && spec != 'p') ||
          (precision && !floating))
        return false;
      if (!nonConsuming)
        result.push_back(spec);
    }
    return true;
  };
  SmallVector<char> pendingConversions;
  unsigned pendingConversion = 0;
  bool dynamicFormatOwnsRemainder = false;

  for (auto [index, child] : llvm::enumerate(operations)) {
    bool isFormat = designatedFormat && index == *designatedFormat;
    char consumingSpecifier = 0;
    if (pendingConversion < pendingConversions.size())
      consumingSpecifier = pendingConversions[pendingConversion++];
    else if (dynamicFormatOwnsRemainder)
      consumingSpecifier = '?';
    bool unconsumed = consumingSpecifier == 0;
    if (isa<semantic::SVEmptyArgumentExpressionOp>(child)) {
      if (isFormat) {
        emitError(getSemanticLocation(child))
            << "format string cannot be omitted";
        return failure();
      }
      output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_OMITTED);
      continue;
    }
    // A string literal consumed by %p prints in quotes (IEEE 1800-2023
    // 21.2.1.6), so it is rendered below rather than passed as text.
    if (auto literal = getStringLiteral(child);
        literal && (isFormat || interpretLiteralsAsFormats) &&
        (isFormat || consumingSpecifier != 'p')) {
      output.items.push_back(sim::SimBytesConstantOp::create(
          builder, getSemanticLocation(literal), literal.getConstantValue()));
      output.flags.push_back(isFormat ? OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT
                                      : 0);
      if (unconsumed) {
        pendingConversions.clear();
        pendingConversion = 0;
        (void)parseConversions(literal.getConstantValue(), pendingConversions);
      }
      continue;
    }

    FailureOr<Value> value = lowerExpression(child);
    if (failed(value))
      return failure();
    output.sourceValues[index] = *value;
    if (isFormat) {
      Value format = *value;
      if (!isa<sim::StringType>(format.getType())) {
        FailureOr<Value> bytes =
            lowerByteArrayString(format, getSemanticLocation(child));
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
      output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_STRING |
                             OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT);
      dynamicFormatOwnsRemainder = true;
      continue;
    }
    auto semanticTypeAttr = child->getAttrOfType<TypeAttr>("semantic_type");
    Type sourceType = semanticTypeAttr ? semanticTypeAttr.getValue() : Type{};
    ArrayAttr enumFormats = sim::get<sim::metadata::EnumFormats>(child);
    if (consumingSpecifier == 'p') {
      // IEEE 1800-2023 21.2.1.6 formats a singular %p operand as an element
      // of an assignment pattern: a packed structure prints as a named
      // pattern, a string or string literal in quotes and a null chandle as
      // null. These, and containers whose elements need source names, are
      // rendered here; other operands keep their runtime rendering.
      Value rendered = *value;
      auto sourceAggregate =
          dyn_cast_or_null<semantic::SourceAggregateType>(sourceType);
      bool packedStructure = sourceAggregate && sourceAggregate.getIsPacked() &&
                             !sourceAggregate.getIsUnion();
      bool render =
          packedStructure ||
          isa<sim::StringType, sim::ChandleType>(rendered.getType()) ||
          (isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
               rendered.getType()) &&
           needsSourcePattern(sourceType));
      if (getStringLiteral(child)) {
        FailureOr<Value> packed =
            toPackedScalar(rendered, getSemanticLocation(child));
        if (failed(packed))
          return failure();
        rendered = sim::SimStringFromPackedOp::create(
            builder, getSemanticLocation(child), stringType, *packed);
        render = true;
      }
      if (render) {
        FailureOr<Value> pattern = formatAssignmentPattern(
            rendered, sourceType, enumFormats, getSemanticLocation(child));
        if (failed(pattern))
          return failure();
        output.items.push_back(*pattern);
        output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_STRING);
        continue;
      }
    }
    if (isa<FloatType>((*value).getType())) {
      FailureOr<Value> real = convert(*value, builder.getF64Type(), false,
                                      getSemanticLocation(child));
      if (failed(real))
        return failure();
      output.items.push_back(*real);
      Operation *spelling = child;
      while (isa<semantic::SVConversionExpressionOp>(spelling)) {
        SmallVector<Operation *> converted = getChildren(spelling);
        if (converted.size() != 1)
          break;
        spelling = converted.front();
      }
      auto system = dyn_cast<semantic::SVCallExpressionOp>(spelling);
      bool isRealtime = system && system.getIsSystemCall() &&
                        system.getCalleeName() == "$realtime";
      output.flags.push_back(
          OBELISK_RT_OUTPUT_ITEM_REAL |
          (isRealtime ? OBELISK_RT_OUTPUT_ITEM_REAL_TIME : 0));
    } else if (isa<sim::StringType>((*value).getType())) {
      output.items.push_back(*value);
      // Unconsumed strings in a display output list are format strings just
      // like literal byte strings. A string consumed by an earlier format
      // conversion is never inspected by the runtime, so marking every
      // dynamic string here preserves both cases without statically parsing
      // a runtime value.
      output.flags.push_back(
          OBELISK_RT_OUTPUT_ITEM_STRING |
          (interpretLiteralsAsFormats ? OBELISK_RT_OUTPUT_ITEM_FORMAT : 0));
      if (unconsumed && interpretLiteralsAsFormats)
        dynamicFormatOwnsRemainder = true;
    } else if (isa<sim::UnpackedArrayType>((*value).getType()) &&
               isByteArrayType((*value).getType())) {
      FailureOr<Value> bytes =
          lowerByteArrayString(*value, getSemanticLocation(child));
      if (failed(bytes))
        return failure();
      output.items.push_back(*bytes);
      output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_STRING);
    } else if (isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
                   (*value).getType())) {
      output.items.push_back(*value);
      output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_CONTAINER);
    } else if (isa<sim::ClassHandleType>((*value).getType())) {
      // IEEE 1800-2017 21.2.1.7 gives class handles singular assignment-
      // pattern formatting: null is spelled "null" and non-null rendering is
      // implementation dependent. Preserve the typed handle for the runtime
      // instead of pretending it is a packed integer.
      output.items.push_back(*value);
      output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_CLASS);
    } else if (isa<sim::ProcessType>((*value).getType())) {
      // IEEE 1800-2017 21.2.1.7 gives a process handle the same singular
      // treatment as any other handle: null spells "null" and a live handle
      // renders implementation dependently.
      output.items.push_back(*value);
      output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_PROCESS);
    } else if (isa<sim::VirtualInterfaceType>((*value).getType())) {
      // Preserve the typed handle through formatted output. Its runtime
      // representation is a stable elaborated scope identity, but treating
      // that identity as a packed vector would give the handle integer
      // assignment-pattern semantics rather than handle semantics for %p.
      output.items.push_back(*value);
      output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_VIRTUAL_INTERFACE);
    } else if (auto unionType =
                   dyn_cast<sim::UnpackedUnionType>((*value).getType());
               unionType && unionType.getIsTagged()) {
      auto semanticType = child->getAttrOfType<TypeAttr>("semantic_type");
      if (!semanticType) {
        emitError(getSemanticLocation(child))
            << "tagged-union output operand has no semantic type";
        return failure();
      }
      FailureOr<Value> pattern =
          formatTaggedUnionPattern(*value, semanticType.getValue(), enumFormats,
                                   getSemanticLocation(child));
      if (failed(pattern))
        return failure();
      output.items.push_back(*pattern);
      output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_STRING);
    } else if ((consumingSpecifier == 'u' || consumingSpecifier == 'z' ||
                consumingSpecifier == '?') &&
               isa<sim::UnpackedStructType, sim::UnpackedUnionType>(
                   (*value).getType())) {
      char rawMode = consumingSpecifier == '?' ? '?' : consumingSpecifier;
      FailureOr<std::pair<Value, Value>> raw = formatUnpackedAggregateRaw(
          *value, getSemanticLocation(child), rawMode);
      if (failed(raw)) {
        FailureOr<Value> pattern = formatAssignmentPattern(
            *value, sourceType, enumFormats, getSemanticLocation(child));
        if (failed(pattern))
          return failure();
        output.items.push_back(*pattern);
        output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_STRING);
        continue;
      }
      Value pattern;
      if (consumingSpecifier == '?') {
        FailureOr<Value> rendered = formatAssignmentPattern(
            *value, sourceType, enumFormats, getSemanticLocation(child));
        if (failed(rendered))
          return failure();
        pattern = *rendered;
      } else {
        pattern = raw->first ? raw->first : raw->second;
      }
      Value twoState = raw->first ? raw->first : pattern;
      Value fourState = raw->second ? raw->second : pattern;
      output.items.append({pattern, twoState, fourState});
      output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_RAW_AGGREGATE);
    } else if (isUnpackedAggregateType((*value).getType())) {
      // An unpacked aggregate has no packed value to hand the runtime; its
      // only formatted rendering is the IEEE 1800-2023 21.2.1.6 assignment
      // pattern, which the compiler can build because the shape is static.
      FailureOr<Value> pattern = formatAssignmentPattern(
          *value, sourceType, enumFormats, getSemanticLocation(child));
      if (failed(pattern))
        return failure();
      output.items.push_back(*pattern);
      output.flags.push_back(OBELISK_RT_OUTPUT_ITEM_STRING);
    } else if (auto enumValues =
                   sim::get<sim::metadata::EnumFormatValues>(child);
               enumValues &&
               (consumingSpecifier == 'p' || consumingSpecifier == '?')) {
      auto enumNames = sim::get<sim::metadata::EnumFormatNames>(child);
      FailureOr<Value> scalar =
          toPackedScalar(*value, getSemanticLocation(child));
      FailureOr<Value> name =
          succeeded(scalar)
              ? lowerEnumFormatName(*scalar, enumValues, enumNames,
                                    getSemanticLocation(child))
              : FailureOr<Value>(failure());
      if (failed(scalar) || failed(name)) {
        emitError(getSemanticLocation(child))
            << "formatted enum has malformed frozen inventory";
        return failure();
      }
      output.items.push_back(*scalar);
      output.items.push_back(*name);
      output.flags.push_back(
          OBELISK_RT_OUTPUT_ITEM_ENUM |
          (isSignedNode(child) ? OBELISK_RT_OUTPUT_ITEM_SIGNED : 0));
    } else {
      auto semanticType = child->getAttrOfType<TypeAttr>("semantic_type");
      if (semanticType && isa<semantic::EnumType>(semanticType.getValue()) &&
          (consumingSpecifier == 'p' || consumingSpecifier == '?')) {
        emitError(getSemanticLocation(child))
            << "enum assignment-pattern format has no declaration inventory";
        return failure();
      }
      FailureOr<Value> scalar =
          toPackedScalar(*value, getSemanticLocation(child));
      if (failed(scalar))
        return failure();
      output.items.push_back(*scalar);
      int32_t flags = isSignedNode(child) ? OBELISK_RT_OUTPUT_ITEM_SIGNED : 0;
      // Preserve the identity of a direct net read for IEEE 1800-2017 %v.
      // Arithmetic derived from a net is a value, not a net, and therefore
      // intentionally keeps the ordinary packed-argument representation.
      if (auto read = (*value).getDefiningOp<sim::SimNetReadOp>()) {
        output.items.push_back(read.getNet());
        flags |= OBELISK_RT_OUTPUT_ITEM_NET;
      }
      output.flags.push_back(flags);
    }
  }
  return output;
}

FailureOr<Value>
UnitLowering::lowerStringFormatSystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef name = op.getCalleeName();
  bool task = op.getSubroutineKind() == semantic::SVSubroutineKind::Task;
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
  auto timeMultiplier = sim::get<sim::metadata::DelayScale>(function);
  if (!timeMultiplier) {
    function.emitError("code unit has no frozen time scale");
    return failure();
  }
  StringAttr lexicalScope = op.getSystemScopePathAttr();
  if (!lexicalScope)
    lexicalScope = sim::get<sim::metadata::HierarchicalName>(function);
  if (!lexicalScope) {
    op.emitError("string formatting call has no elaborated lexical scope");
    return failure();
  }
  IntegerAttr timePrecision = designTimePrecisionExponent();
  Type stringType = sim::StringType::get(function.getContext());
  Value result = sim::SimStringOutputFormatOp::create(
      builder, location, stringType, function.getBody().front().getArgument(0),
      output->items, sim::Radix::Decimal, output->flags, lexicalScope,
      op.getSystemLibraryCellAttr(), timeMultiplier, timePrecision);
  if (!task)
    return result;

  Operation *destinationNode = children.front();
  if (auto assignment =
          dyn_cast<semantic::SVAssignmentExpressionOp>(destinationNode)) {
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
  bool byteArray = isByteContainerType(destinationType);
  if (auto array = dyn_cast<sim::UnpackedArrayType>(destinationType))
    if (auto element = dyn_cast<IntegerType>(array.getElementType()))
      byteArray = element.getWidth() == 8;
  if (!isa<sim::StringType>(destinationType) && !integral && !byteArray) {
    emitError(getSemanticLocation(destinationNode))
        << "$sformat destination must have integral, unpacked byte array, or "
           "string type";
    return failure();
  }
  FailureOr<Value> converted =
      lowerFormattedResult(result, destinationType, location);
  if (failed(converted) ||
      failed(storeReference(*destination, *converted, location)))
    return failure();
  return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                   builder.getBoolAttr(false))
      .getResult();
}

bool UnitLowering::isByteContainerType(Type type) {
  Type element;
  if (auto array = dyn_cast<sim::DynamicArrayType>(type))
    element = array.getElementType();
  else if (auto queue = dyn_cast<sim::QueueType>(type))
    element = queue.getElementType();
  auto integer = dyn_cast_or_null<IntegerType>(element);
  return integer && integer.getWidth() == 8;
}

bool UnitLowering::isByteArrayType(Type type) {
  if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
    auto integer = dyn_cast<IntegerType>(array.getElementType());
    return integer && integer.getWidth() == 8;
  }
  return isByteContainerType(type);
}

FailureOr<Value> UnitLowering::lowerByteArrayString(Value value,
                                                    Location location) {
  if (isByteContainerType(value.getType()))
    return lowerByteContainerString(value, location);
  if (!isByteArrayType(value.getType()))
    return failure();
  auto array = cast<sim::UnpackedArrayType>(value.getType());
  SmallVector<Value> characters;
  unsigned count = sim::getAggregateNumElements(array);
  Type logicType = sim::LogicType::get(function.getContext(), 8);
  characters.reserve(count);
  for (unsigned index = 0; index != count; ++index) {
    Value character = sim::SimAggregateExtractOp::create(
        builder, location, array.getElementType(), value, index);
    FailureOr<Value> converted = convert(character, logicType, false, location);
    if (failed(converted))
      return failure();
    characters.push_back(*converted);
  }
  Value packed = sim::SimLogicConcatOp::create(
      builder, location, sim::LogicType::get(function.getContext(), count * 8),
      characters);
  // IEEE 1800-2023 21.2.1.7 and 21.3.3 define unpacked byte-array text in
  // left-bound-to-right-bound order. Aggregate operands use that logical
  // order, and string.from_packed reads its most-significant byte first.
  return sim::SimStringFromPackedOp::create(
             builder, location, sim::StringType::get(function.getContext()),
             packed)
      .getResult();
}

FailureOr<Value> UnitLowering::lowerByteContainerString(Value container,
                                                        Location location) {
  // The runtime formats a byte container under %s as its text in one pass.
  Value format = sim::SimBytesConstantOp::create(builder, location, "%s");
  return sim::SimStringOutputFormatOp::create(
             builder, location, sim::StringType::get(function.getContext()),
             function.getBody().front().getArgument(0),
             ValueRange{format, container}, sim::Radix::Decimal,
             ArrayRef<int32_t>{OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT,
                               OBELISK_RT_OUTPUT_ITEM_CONTAINER},
             StringAttr{}, StringAttr{}, IntegerAttr{}, IntegerAttr{})
      .getResult();
}

FailureOr<Value> UnitLowering::lowerFormattedResult(Value text,
                                                    Type destinationType,
                                                    Location location) {
  Type i64 = builder.getI64Type();
  if (auto array = dyn_cast<sim::UnpackedArrayType>(destinationType)) {
    // IEEE 1800-2023 5.9 and 21.3.3: string assignment to an unpacked byte
    // array is left-justified, in left-bound-to-right-bound order. getc
    // returns zero beyond the string, providing the required fill.
    SmallVector<Value> bytes;
    unsigned count = sim::getAggregateNumElements(destinationType);
    bytes.reserve(count);
    for (unsigned index = 0; index != count; ++index) {
      Value position = arith::ConstantOp::create(
          builder, location, i64, builder.getI64IntegerAttr(index));
      bytes.push_back(sim::SimStringGetcOp::create(
          builder, location, array.getElementType(), text, position));
    }
    return sim::SimAggregateConstructOp::create(builder, location,
                                                destinationType, bytes)
        .getResult();
  }
  if (!isByteContainerType(destinationType))
    return convert(text, destinationType, false, location);

  // A dynamic array or queue takes the size of the text, except that a
  // bounded queue keeps only its first bound + 1 characters (7.10.5).
  auto queue = dyn_cast<sim::QueueType>(destinationType);
  Type element =
      queue ? queue.getElementType()
            : cast<sim::DynamicArrayType>(destinationType).getElementType();
  FailureOr<ContainerElementDescriptor> descriptor =
      describeContainerElement(element, location);
  if (failed(descriptor))
    return failure();
  Value zero = arith::ConstantOp::create(builder, location, i64,
                                         builder.getI64IntegerAttr(0));
  Value size = sim::SimStringLengthOp::create(builder, location, i64, text);
  if (queue && queue.getBound())
    size = arith::MinUIOp::create(
        builder, location, size,
        arith::ConstantOp::create(
            builder, location, i64,
            builder.getI64IntegerAttr(queue.getBound() + 1)));
  // A queue starts empty and grows by insertion.
  Value container = sim::SimContainerCreateOp::create(
      builder, location, destinationType, queue ? zero : size,
      descriptor->typeID, descriptor->kind, descriptor->flags,
      descriptor->valueSize, descriptor->alignment, descriptor->bitWidth,
      builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
      builder.getDenseI32ArrayAttr(descriptor->traceKinds),
      queue ? sim::ContainerKind::Queue : sim::ContainerKind::DynamicArray,
      queue ? (queue.getBound() ? queue.getBound() : UINT64_MAX) : 0);
  Block *header = addBlock();
  header->addArgument(i64, location);
  Block *body = addBlock();
  Block *exit = addBlock();
  cf::BranchOp::create(builder, location, header, ValueRange{zero});
  setCurrent(header);
  Value index = header->getArgument(0);
  Value more = arith::CmpIOp::create(builder, location,
                                     arith::CmpIPredicate::ult, index, size);
  cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                           ValueRange{});
  setCurrent(body);
  Value character =
      sim::SimStringGetcOp::create(builder, location, element, text, index);
  if (queue)
    sim::SimQueueInsertOp::create(builder, location, container, index,
                                  character);
  else
    sim::SimContainerWriteOp::create(builder, location, container, index,
                                     character);
  Value one = arith::ConstantOp::create(builder, location, i64,
                                        builder.getI64IntegerAttr(1));
  Value next = arith::AddIOp::create(builder, location, index, one);
  cf::BranchOp::create(builder, location, header, ValueRange{next});
  setCurrent(exit);
  return container;
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
    IntegerAttr unit;
    IntegerAttr precision;
    if (targetScope) {
      unit = targetScope->getAttrOfType<IntegerAttr>("dpi_unit_femtoseconds");
      precision =
          targetScope->getAttrOfType<IntegerAttr>("dpi_precision_femtoseconds");
    } else if (!children.empty()) {
      // The frontend froze the time scale of a $unit target.
      unit = op->getAttrOfType<IntegerAttr>("target_time_unit_fs");
      precision = op->getAttrOfType<IntegerAttr>("target_time_precision_fs");
    } else {
      unit = op->getAttrOfType<IntegerAttr>("system_scope_time_unit_fs");
      precision =
          op->getAttrOfType<IntegerAttr>("system_scope_time_precision_fs");
      if (auto path = op->getAttrOfType<StringAttr>("system_time_scope_path"))
        targetPath = path;
    }
    if (!unit || !precision) {
      emitError(location) << "$printtimescale target scope '"
                          << targetPath.getValue()
                          << "' has no simulation descriptor";
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
    // IEEE 1800-2023 20.4.2 names a $root target "$root", not by the
    // escaped hierarchical name of the root scope.
    StringRef targetName = targetScope && !targetScope.getParent()
                               ? StringRef("$root")
                               : targetPath.getValue();
    std::string text = (Twine("Time scale of (") + targetName + ") is " +
                        formatScale(unit.getValue().getZExtValue()) + " / " +
                        formatScale(precision.getValue().getZExtValue()))
                           .str();
    Value item =
        sim::SimBytesConstantOp::create(builder, location, text).getResult();
    Value descriptor = constant(i32, 1);
    sim::SimDisplayOp::create(builder, location, context, descriptor, item,
                              true, sim::Radix::Decimal, ArrayRef<int32_t>{0},
                              targetPath, StringAttr{},
                              builder.getI64IntegerAttr(1), IntegerAttr{});
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
    StringRef severity{};
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
    StringAttr lexicalScope = op.getSystemScopePathAttr();
    if (!lexicalScope)
      lexicalScope = sim::get<sim::metadata::HierarchicalName>(function);
    if (!lexicalScope) {
      op.emitError("display call has no elaborated lexical scope");
      return failure();
    }
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
      prefix += lexicalScope.getValue().str();
      std::string escaped;
      escaped.reserve(prefix.size() + 40);
      appendFormatText(escaped, prefix);
      escaped += ": simulation time %0t: ";
      if (children.size() == firstItem)
        escaped += name.str() + " called.";
      items.push_back(
          sim::SimBytesConstantOp::create(builder, location, escaped)
              .getResult());
      flags.push_back(0);
      FailureOr<Value> time = currentTimeInUnits(location);
      if (failed(time))
        return failure();
      items.push_back(*time);
      flags.push_back(0);
    }
    FailureOr<LoweredOutputList> output =
        lowerOutputListItems(ArrayRef(children).drop_front(firstItem), true);
    if (failed(output))
      return failure();
    if (sim::has<sim::metadata::MonitorCallback>(op)) {
      auto complete = sim::get<sim::metadata::MonitorObservationComplete>(op);
      monitorObservationComplete = complete && complete.getValue();
      if (monitorObservationComplete) {
        ArrayRef<Operation *> monitored =
            ArrayRef(children).drop_front(firstItem);
        for (auto [index, child] : llvm::enumerate(monitored)) {
          if (!sim::has<sim::metadata::Observer>(child))
            continue;
          Value initial = output->sourceValues[index];
          if (!initial) {
            emitError(getSemanticLocation(child))
                << "monitor observer has no evaluated initial value";
            return failure();
          }
          FailureOr<Value> observer = bindObserver(child);
          if (failed(observer))
            return failure();
          Type resultType =
              cast<sim::ObserverType>((*observer).getType()).getResultType();
          if (!isa<FloatType>(resultType)) {
            FailureOr<Value> scalar =
                toPackedScalar(initial, getSemanticLocation(child));
            if (failed(scalar))
              return failure();
            initial = *scalar;
          }
          if (initial.getType() != resultType) {
            FailureOr<Value> converted =
                convert(initial, resultType, isSignedNode(child),
                        getSemanticLocation(child));
            if (failed(converted))
              return failure();
            initial = *converted;
          }
          Value directWatch;
          if (auto named =
                  dyn_cast<semantic::SVNamedValueExpressionOp>(child)) {
            StringRef path = named.getReferencedPath();
            directWatch = values.lookup(path);
            if (!directWatch)
              directWatch = lvalues.lookup(path);
            if (!directWatch ||
                !isa<sim::RefType, sim::NetType>(directWatch.getType()))
              directWatch = {};
          }
          monitorObservations.push_back({*observer, initial, directWatch});
        }
      }
    }
    llvm::append_range(items, output->items);
    llvm::append_range(flags, output->flags);
    auto timeMultiplier = sim::get<sim::metadata::DelayScale>(function);
    if (!timeMultiplier) {
      function.emitError("code unit has no frozen time scale");
      return failure();
    }
    // %t rescales against the design's precision when $timeformat has changed
    // the display units, so the site carries that precision as a decimal
    // exponent in seconds.
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
    if (display->stringOutput) {
      Operation *destinationNode = children.front();
      if (auto assignment =
              dyn_cast<semantic::SVAssignmentExpressionOp>(destinationNode)) {
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
      bool byteArray = isByteContainerType(destinationType);
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
          context, items, static_cast<sim::Radix>(display->radix), flags,
          lexicalScope, op.getSystemLibraryCellAttr(), timeMultiplier,
          timePrecision);
      FailureOr<Value> converted =
          lowerFormattedResult(result, destinationType, location);
      if (failed(converted) ||
          failed(storeReference(*destination, *converted, location)))
        return failure();
      return dummyTaskResult();
    }
    if (name == "$error")
      sim::SimErrorOp::create(builder, location, context);
    if (display->fatal)
      sim::SimFatalOp::create(builder, location, context, verbosity);
    sim::SimDisplayOp::create(
        builder, location, context, descriptor, items, display->newline,
        static_cast<sim::Radix>(display->radix), flags, lexicalScope,
        op.getSystemLibraryCellAttr(), timeMultiplier, timePrecision);
    if (display->fatal) {
      if (failed(emitFunctionReturn(location, std::nullopt, false,
                                    /*emitBlockEventEnd=*/false)))
        return failure();
      setCurrent(addBlock());
    }
    return dummyTaskResult();
  }

  op.emitOpError("is not a supported display system call");
  return failure();
}

} // namespace obelisk::simlowering
