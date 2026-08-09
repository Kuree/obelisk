//===- LowerUnitExpressions.cpp - Lower values and selections ---------===//

#include "LowerUnit.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <limits>
#include <optional>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

bool isIntegerConstant(Operation *operation) {
  return getConstantSpelling(operation).has_value();
}

bool containsUnboundedLiteral(Operation *operation) {
  bool found = false;
  operation->walk([&](semantic::SVUnboundedLiteralOp) {
    found = true;
    return WalkResult::interrupt();
  });
  return found;
}

bool isTaggedUnionType(Type type) {
  if (auto packed = dyn_cast<sim::PackedUnionType>(type))
    return packed.getIsTagged();
  if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(type))
    return unpacked.getIsTagged();
  return false;
}

} // namespace

//===----------------------------------------------------------------------===//
// Expressions
//===----------------------------------------------------------------------===//

FailureOr<Value>
UnitLowering::lowerNamedValue(semantic::SVNamedValueExpressionOp op,
                              bool lvalue) {
  if (auto field =
          op->getAttrOfType<FlatSymbolRefAttr>("obelisk_sim.class_field")) {
    if (!thisObject) {
      emitError(getSemanticLocation(op))
          << "instance property reference has no this object";
      return failure();
    }
    FailureOr<Type> elementType = getNormalizedSemanticType(op);
    auto objectType = dyn_cast<sim::ClassHandleType>(thisObject.getType());
    if (failed(elementType) || !objectType)
      return failure();
    Type referenceType = sim::ManagedRefType::get(
        function.getContext(), *elementType, objectType.getClassName());
    Value reference = sim::SimClassFieldRefOp::create(
        builder, getSemanticLocation(op), referenceType, thisObject, field);
    if (lvalue)
      return reference;
    return sim::SimManagedLoadOp::create(builder, getSemanticLocation(op),
                                         *elementType, reference)
        .getResult();
  }
  return lowerReferencedValue(op, op.getReferencedPath(), lvalue);
}

FailureOr<Value>
UnitLowering::lowerReferencedValue(Operation *op, StringRef path, bool lvalue) {
  Location location = getSemanticLocation(op);
  Value value;
  if (lvalue)
    if (auto node = op->getAttrOfType<IntegerAttr>("node_id"))
      value = nodeLvalues.lookup(node.getValue().getZExtValue());
  if (!value)
    value = lvalue ? lvalues.lookup(path) : values.lookup(path);
  if (!value && thisObject && path.ends_with(".this"))
    value = thisObject;
  if (!value) {
    emitError(location) << "named value has no frozen unit-local binding: "
                        << path;
    return failure();
  }
  if (lvalue)
    return value;

  // Only immutable process captures describe design sensitivity. An automatic
  // local is an implementation detail and must remain eligible for promotion
  // instead of escaping through a suspend operation.
  if (auto ref = dyn_cast<sim::RefType>(value.getType())) {
    recordSensitivity(value);
    return sim::SimRefLoadOp::create(builder, location, ref.getElementType(),
                                     value)
        .getResult();
  }
  if (auto ref = dyn_cast<sim::ArgumentRefType>(value.getType()))
    return sim::SimArgumentRefLoadOp::create(builder, location,
                                             ref.getElementType(), value)
        .getResult();
  if (auto net = dyn_cast<sim::NetType>(value.getType())) {
    recordSensitivity(value);
    return sim::SimNetReadOp::create(builder, location, net.getElementType(),
                                     value)
        .getResult();
  }
  if (isa<sim::DriverType>(value.getType())) {
    emitError(location) << "driver handle cannot be used as an rvalue";
    return failure();
  }
  return value;
}

FailureOr<Value> UnitLowering::lowerLiteral(Operation *op) {
  Location location = getSemanticLocation(op);
  FailureOr<Type> type = getNormalizedSemanticType(op);
  if (failed(type))
    return failure();
  Type scalarType = sim::getPackedScalarType(*type);
  if (!scalarType) {
    unsupported(op) << " (integer literal has an unpacked result type)";
    return failure();
  }
  std::optional<unsigned> width = sim::getPackedWidth(scalarType);
  std::optional<StringRef> spelling = getConstantSpelling(op);
  if (!width || !spelling) {
    unsupported(op) << " (integer literal representation)";
    return failure();
  }
  FailureOr<ParsedConstant> parsed =
      parseSVInteger(*spelling, *width, location);
  if (failed(parsed))
    return failure();
  Value value;
  if (auto integer = dyn_cast<IntegerType>(scalarType))
    value = arith::ConstantOp::create(
        builder, location, integer,
        builder.getIntegerAttr(integer, parsed->value));
  else {
    auto planeType = IntegerType::get(op->getContext(), *width);
    value = sim::SimLogicConstantOp::create(
        builder, location, scalarType,
        builder.getIntegerAttr(planeType, parsed->value),
        builder.getIntegerAttr(planeType, parsed->unknown));
  }
  return convert(value, *type, isSignedNode(op), location);
}

FailureOr<Value> lowerStringLiteralValue(OpBuilder &builder, Operation *op,
                                         Type type, Location location) {
  auto spelling = op->getAttrOfType<StringAttr>("constant_value");
  if (!spelling)
    return emitError(location) << "string literal has no byte payload",
           failure();
  if (isa<sim::StringType>(type))
    return sim::SimStringLiteralOp::create(builder, location, type, spelling)
        .getResult();

  Type scalar = sim::getPackedScalarType(type);
  std::optional<unsigned> width =
      scalar ? sim::getPackedWidth(scalar) : std::nullopt;
  if (!scalar || !width)
    return emitError(location)
               << "string literal has a non-packed, non-string result type",
           failure();
  APInt bits(*width, 0);
  for (uint8_t byte : spelling.getValue().bytes()) {
    bits <<= std::min<unsigned>(8, *width);
    bits |= APInt(*width, byte);
  }
  Value value;
  auto planeType = IntegerType::get(type.getContext(), *width);
  if (isa<IntegerType>(scalar))
    value = arith::ConstantOp::create(builder, location, scalar,
                                      builder.getIntegerAttr(scalar, bits));
  else
    value = sim::SimLogicConstantOp::create(
        builder, location, scalar, builder.getIntegerAttr(planeType, bits),
        builder.getIntegerAttr(planeType, APInt(*width, 0)));
  if (scalar != type)
    value = sim::SimPackedUnflattenOp::create(builder, location, type, value);
  return value;
}

FailureOr<Value> UnitLowering::lowerConcatenation(Operation *op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  if (children.empty() &&
      !isa<sim::DynamicArrayType, sim::QueueType>(*resultType)) {
    unsupported(op) << " (empty concatenation)";
    return failure();
  }
  if (isa<sim::StringType>(*resultType)) {
    SmallVector<Value> inputs;
    for (Operation *child : children) {
      FailureOr<Value> input = lowerExpression(child);
      if (failed(input))
        return failure();
      FailureOr<Value> converted =
          convert(*input, *resultType, isSignedNode(child), location);
      if (failed(converted))
        return failure();
      inputs.push_back(*converted);
    }
    return sim::SimStringConcatOp::create(builder, location, *resultType,
                                          inputs)
        .getResult();
  }
  if (isa<sim::DynamicArrayType, sim::QueueType>(*resultType)) {
    Type elementType =
        isa<sim::DynamicArrayType>(*resultType)
            ? cast<sim::DynamicArrayType>(*resultType).getElementType()
            : cast<sim::QueueType>(*resultType).getElementType();
    struct Part {
      Operation *node;
      Value value;
      Value size;
      Type elementType;
      unsigned fixedSize;
    };
    auto i64Constant = [&](int64_t value) -> Value {
      return arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                       builder.getI64IntegerAttr(value));
    };
    SmallVector<Part> parts;
    Value totalSize = i64Constant(0);
    Value one = i64Constant(1);
    for (Operation *child : children) {
      FailureOr<Value> input = lowerExpression(child);
      if (failed(input))
        return failure();
      Type inputType = (*input).getType();
      Part part{child, *input, one, {}, 0};
      if (inputType != elementType) {
        if (auto array = dyn_cast<sim::DynamicArrayType>(inputType)) {
          part.elementType = array.getElementType();
          part.size = sim::SimContainerSizeOp::create(
              builder, getSemanticLocation(child), builder.getI64Type(),
              *input);
        } else if (auto queue = dyn_cast<sim::QueueType>(inputType)) {
          part.elementType = queue.getElementType();
          part.size = sim::SimContainerSizeOp::create(
              builder, getSemanticLocation(child), builder.getI64Type(),
              *input);
        } else if (auto array = dyn_cast<sim::UnpackedArrayType>(inputType)) {
          part.elementType = array.getElementType();
          part.fixedSize = sim::getAggregateNumElements(array);
          part.size = i64Constant(static_cast<int64_t>(part.fixedSize));
        }
      }
      totalSize =
          arith::AddIOp::create(builder, location, totalSize, part.size);
      parts.push_back(part);
    }

    FailureOr<ContainerElementDescriptor> descriptor =
        describeContainerElement(elementType, location);
    if (failed(descriptor))
      return failure();
    uint32_t containerKind = isa<sim::DynamicArrayType>(*resultType)
                                 ? OBELISK_RT_CONTAINER_DYNAMIC_ARRAY
                                 : OBELISK_RT_CONTAINER_QUEUE;
    uint64_t bound = 0;
    if (auto queue = dyn_cast<sim::QueueType>(*resultType))
      bound = queue.getBound() ? queue.getBound() : UINT64_MAX;
    Value allocationSize = containerKind == OBELISK_RT_CONTAINER_DYNAMIC_ARRAY
                               ? totalSize
                               : i64Constant(0);
    Value result = sim::SimContainerCreateOp::create(
        builder, location, *resultType, allocationSize, descriptor->typeID,
        descriptor->kind, descriptor->flags, descriptor->valueSize,
        descriptor->alignment, descriptor->bitWidth,
        builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(descriptor->traceKinds), containerKind,
        bound);

    Value outputIndex = i64Constant(0);
    for (const Part &part : parts) {
      if (!part.elementType) {
        FailureOr<Value> converted =
            convert(part.value, elementType, isSignedNode(part.node),
                    getSemanticLocation(part.node));
        if (failed(converted))
          return failure();
        sim::SimContainerWriteOp::create(builder,
                                         getSemanticLocation(part.node), result,
                                         outputIndex, *converted);
        outputIndex =
            arith::AddIOp::create(builder, location, outputIndex, one);
        continue;
      }
      if (part.fixedSize) {
        for (unsigned index = 0; index < part.fixedSize; ++index) {
          Value value = sim::SimAggregateExtractOp::create(
              builder, getSemanticLocation(part.node), part.elementType,
              part.value, index);
          FailureOr<Value> converted =
              convert(value, elementType, isSignedNode(part.node),
                      getSemanticLocation(part.node));
          if (failed(converted))
            return failure();
          sim::SimContainerWriteOp::create(builder,
                                           getSemanticLocation(part.node),
                                           result, outputIndex, *converted);
          outputIndex =
              arith::AddIOp::create(builder, location, outputIndex, one);
        }
        continue;
      }

      Block *header = addBlock();
      header->addArgument(builder.getI64Type(), location);
      header->addArgument(builder.getI64Type(), location);
      Block *body = addBlock();
      Block *exit = addBlock();
      exit->addArgument(builder.getI64Type(), location);
      Value zero = i64Constant(0);
      cf::BranchOp::create(builder, location, header,
                           ValueRange{zero, outputIndex});
      setCurrent(header);
      Value inputIndex = header->getArgument(0);
      Value destinationIndex = header->getArgument(1);
      Value more = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, inputIndex, part.size);
      cf::CondBranchOp::create(builder, location, more, body, ValueRange{},
                               exit, ValueRange{destinationIndex});
      setCurrent(body);
      Value value = sim::SimContainerReadOp::create(
          builder, getSemanticLocation(part.node), part.elementType, part.value,
          inputIndex);
      FailureOr<Value> converted =
          convert(value, elementType, isSignedNode(part.node),
                  getSemanticLocation(part.node));
      if (failed(converted))
        return failure();
      sim::SimContainerWriteOp::create(builder, getSemanticLocation(part.node),
                                       result, destinationIndex, *converted);
      Value nextInput =
          arith::AddIOp::create(builder, location, inputIndex, one);
      Value nextDestination =
          arith::AddIOp::create(builder, location, destinationIndex, one);
      cf::BranchOp::create(builder, location, header,
                           ValueRange{nextInput, nextDestination});
      setCurrent(exit);
      outputIndex = exit->getArgument(0);
    }
    return result;
  }
  Type scalarResultType = sim::getPackedScalarType(*resultType);
  if (!scalarResultType) {
    unsupported(op) << " (unpacked concatenation result)";
    return failure();
  }
  SmallVector<Value> inputs;
  for (Operation *child : children) {
    FailureOr<Value> input = lowerExpression(child);
    if (failed(input))
      return failure();
    FailureOr<Value> scalar = toPackedScalar(*input, location);
    if (failed(scalar))
      return failure();
    inputs.push_back(*scalar);
  }
  if (auto resultLogic = dyn_cast<sim::LogicType>(scalarResultType)) {
    SmallVector<Value> logicInputs;
    for (Value input : inputs) {
      FailureOr<Value> logic = toLogic(input, location);
      if (failed(logic))
        return failure();
      logicInputs.push_back(*logic);
    }
    Value result = sim::SimLogicConcatOp::create(builder, location, resultLogic,
                                                 logicInputs);
    return convert(result, *resultType, false, location);
  }
  auto resultInteger = cast<IntegerType>(scalarResultType);
  Value combined =
      arith::ConstantOp::create(builder, location, resultInteger,
                                builder.getIntegerAttr(resultInteger, 0));
  unsigned trailingWidth = resultInteger.getWidth();
  for (Value input : inputs) {
    auto inputInteger = cast<IntegerType>(input.getType());
    trailingWidth -= inputInteger.getWidth();
    FailureOr<Value> extended = convert(input, resultInteger, false, location);
    if (failed(extended))
      return failure();
    Value shifted = *extended;
    if (trailingWidth) {
      Value amount = arith::ConstantOp::create(
          builder, location, resultInteger,
          builder.getIntegerAttr(resultInteger, trailingWidth));
      shifted = arith::ShLIOp::create(builder, location, shifted, amount);
    }
    combined = arith::OrIOp::create(builder, location, combined, shifted);
  }
  return convert(combined, *resultType, false, location);
}

FailureOr<Value> UnitLowering::lowerReplication(Operation *op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 2) {
    unsupported(op) << " (replication arity)";
    return failure();
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  if (isa<sim::StringType>(*resultType)) {
    FailureOr<Value> count = lowerExpression(children.front());
    FailureOr<Value> input = lowerExpression(children[1]);
    if (failed(count) || failed(input))
      return failure();
    FailureOr<Value> count64 = convert(
        *count, builder.getI64Type(), isSignedNode(children.front()), location);
    FailureOr<Value> string =
        convert(*input, *resultType, isSignedNode(children[1]), location);
    if (failed(count64) || failed(string))
      return failure();
    return sim::SimStringRepeatOp::create(builder, location, *resultType,
                                          *string, *count64)
        .getResult();
  }
  if (!isIntegerConstant(children.front())) {
    unsupported(op) << " (nonconstant replication count)";
    return failure();
  }
  std::optional<StringRef> spelling = getConstantSpelling(children.front());
  if (!spelling) {
    unsupported(op) << " (nonconstant replication count)";
    return failure();
  }
  FailureOr<ParsedConstant> count = parseSVInteger(*spelling, 64, location);
  FailureOr<Value> input = lowerExpression(children[1]);
  if (failed(count) || failed(input) || failed(resultType))
    return failure();
  FailureOr<Value> scalarInput = toPackedScalar(*input, location);
  Type scalarResultType = sim::getPackedScalarType(*resultType);
  if (failed(scalarInput) || !scalarResultType) {
    if (succeeded(scalarInput))
      unsupported(op) << " (unpacked replication result)";
    return failure();
  }
  input = *scalarInput;
  if (!count->unknown.isZero() || count->value.isZero() ||
      count->value.isNegative()) {
    emitError(location) << "replication count must be a known positive value";
    return failure();
  }
  uint64_t repetitions = count->value.getZExtValue();
  if (auto resultLogic = dyn_cast<sim::LogicType>(scalarResultType)) {
    FailureOr<Value> logicInput = toLogic(*input, location);
    if (failed(logicInput))
      return failure();
    Value result = sim::SimLogicReplicateOp::create(
        builder, location, resultLogic, *logicInput,
        builder.getI64IntegerAttr(repetitions));
    return convert(result, *resultType, false, location);
  }
  auto resultInteger = cast<IntegerType>(scalarResultType);
  auto inputInteger = cast<IntegerType>((*input).getType());
  Value combined =
      arith::ConstantOp::create(builder, location, resultInteger,
                                builder.getIntegerAttr(resultInteger, 0));
  FailureOr<Value> extended = convert(*input, resultInteger, false, location);
  if (failed(extended))
    return failure();
  for (uint64_t index = 0; index < repetitions; ++index) {
    unsigned shift = inputInteger.getWidth() * (repetitions - index - 1);
    Value piece = *extended;
    if (shift) {
      Value amount = arith::ConstantOp::create(
          builder, location, resultInteger,
          builder.getIntegerAttr(resultInteger, shift));
      piece = arith::ShLIOp::create(builder, location, piece, amount);
    }
    combined = arith::OrIOp::create(builder, location, combined, piece);
  }
  return convert(combined, *resultType, false, location);
}

FailureOr<Value>
UnitLowering::lowerMember(semantic::SVMemberAccessExpressionOp op,
                          bool lvalue) {
  Location location = getSemanticLocation(op);
  if (getConstantSpelling(op)) {
    if (lvalue) {
      emitError(location) << "constant member access is not an lvalue";
      return failure();
    }
    return lowerLiteral(op);
  }
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 1) {
    unsupported(op) << " (member access arity)";
    return failure();
  }
  if (auto field =
          op->getAttrOfType<FlatSymbolRefAttr>("obelisk_sim.class_field")) {
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    FailureOr<Value> object = lowerExpression(children.front());
    auto objectType = succeeded(object)
                          ? dyn_cast<sim::ClassHandleType>((*object).getType())
                          : sim::ClassHandleType{};
    if (failed(resultType) || failed(object) || !objectType)
      return failure();
    Type referenceType = sim::ManagedRefType::get(
        function.getContext(), *resultType, objectType.getClassName());
    Value reference = sim::SimClassFieldRefOp::create(
        builder, location, referenceType, *object, field);
    if (lvalue)
      return reference;
    return sim::SimManagedLoadOp::create(builder, location, *resultType,
                                         reference)
        .getResult();
  }
  auto ordinalAttr = op->getAttrOfType<IntegerAttr>("field_ordinal");
  if (!ordinalAttr || ordinalAttr.getValue().isNegative() ||
      ordinalAttr.getValue().getActiveBits() > 32) {
    emitError(location) << "member access has no valid declaration ordinal";
    return failure();
  }
  unsigned ordinal = ordinalAttr.getValue().getZExtValue();
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  FailureOr<Value> input = lowerExpression(children.front(), lvalue);
  if (failed(resultType) || failed(input))
    return failure();
  Type inputValueType = (*input).getType();
  if (auto reference = dyn_cast<sim::RefType>(inputValueType)) {
    Type aggregateType = reference.getElementType();
    if (sim::getAggregateElementType(aggregateType, ordinal) != *resultType) {
      emitError(location) << "member ordinal does not match the aggregate type";
      return failure();
    }
    if (isTaggedUnionType(aggregateType)) {
      Value aggregate =
          sim::SimRefLoadOp::create(builder, location, aggregateType, *input);
      if (failed(guardTaggedUnionMember(aggregate, ordinal, location)))
        return failure();
    }
    Type selected = sim::RefType::get(function.getContext(), *resultType);
    return sim::SimRefSubelementOp::create(
               builder, location, selected, *input,
               builder.getDenseI64ArrayAttr({static_cast<int64_t>(ordinal)}))
        .getResult();
  }
  if (auto driver = dyn_cast<sim::DriverType>(inputValueType)) {
    if (sim::getAggregateElementType(driver.getElementType(), ordinal) !=
        *resultType) {
      emitError(location) << "member ordinal does not match the aggregate type";
      return failure();
    }
    Type selected = sim::DriverType::get(function.getContext(), *resultType);
    return sim::SimDriverSubelementOp::create(
               builder, location, selected, *input,
               builder.getDenseI64ArrayAttr({static_cast<int64_t>(ordinal)}))
        .getResult();
  }
  if (sim::getAggregateElementType(inputValueType, ordinal) != *resultType) {
    emitError(location) << "member access input is not a matching aggregate";
    return failure();
  }
  if (isa<sim::PackedUnionType, sim::UnpackedUnionType>(inputValueType)) {
    if (isTaggedUnionType(inputValueType) &&
        failed(guardTaggedUnionMember(*input, ordinal, location)))
      return failure();
    return sim::SimUnionExtractOp::create(builder, location, *resultType,
                                          *input, ordinal)
        .getResult();
  }
  return sim::SimAggregateExtractOp::create(builder, location, *resultType,
                                            *input, ordinal)
      .getResult();
}

LogicalResult UnitLowering::guardTaggedUnionMember(Value input,
                                                   unsigned ordinal,
                                                   Location location) {
  Value active = sim::SimUnionIsActiveOp::create(
      builder, location, builder.getI1Type(), input, ordinal);
  Block *valid = addBlock();
  Block *invalid = addBlock();
  cf::CondBranchOp::create(builder, location, active, valid, ValueRange{},
                           invalid, ValueRange{});
  setCurrent(invalid);
  if (failed(emitRuntimeFatal(
          location, "tagged union member access selected an inactive member.")))
    return failure();
  setCurrent(valid);
  return success();
}

FailureOr<Value>
UnitLowering::lowerTaggedUnion(semantic::SVTaggedUnionExpressionOp op) {
  Location location = getSemanticLocation(op);
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  bool tagged = false;
  if (auto packed = dyn_cast<sim::PackedUnionType>(*resultType))
    tagged = packed.getIsTagged();
  else if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(*resultType))
    tagged = unpacked.getIsTagged();
  else {
    unsupported(op) << " (result is not a union)";
    return failure();
  }
  if (!tagged) {
    emitError(location) << "tagged union expression has an untagged result";
    return failure();
  }

  auto ordinalAttr = op->getAttrOfType<IntegerAttr>("field_ordinal");
  if (!ordinalAttr || ordinalAttr.getValue().isNegative() ||
      ordinalAttr.getValue().getActiveBits() > 32) {
    emitError(location)
        << "tagged union expression has no valid declaration ordinal";
    return failure();
  }
  unsigned ordinal = ordinalAttr.getValue().getZExtValue();
  Type fieldType = sim::getAggregateElementType(*resultType, ordinal);
  if (!fieldType) {
    emitError(location) << "tagged union member ordinal is out of range";
    return failure();
  }

  SmallVector<Operation *> children = getChildren(op);
  if (children.empty()) {
    Value placeholder = createDefaultValue(builder, location, fieldType);
    if (!placeholder) {
      emitError(location)
          << "void tagged-union member has no physical placeholder";
      return failure();
    }
    return sim::SimUnionConstructOp::create(builder, location, *resultType,
                                            placeholder, ordinal)
        .getResult();
  }
  if (children.size() != 1) {
    emitError(location) << "malformed tagged-union value inventory";
    return failure();
  }
  FailureOr<Value> value = lowerExpression(children.front());
  if (failed(value))
    return failure();
  FailureOr<Value> converted =
      convert(*value, fieldType, isSignedNode(children.front()), location);
  if (failed(converted))
    return failure();
  return sim::SimUnionConstructOp::create(builder, location, *resultType,
                                          *converted, ordinal)
      .getResult();
}

FailureOr<Value> UnitLowering::lowerAssignmentPattern(Operation *op) {
  Location location = getSemanticLocation(op);
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  if (auto array = dyn_cast<sim::AssocArrayType>(*resultType)) {
    auto structured =
        dyn_cast<semantic::SVStructuredAssignmentPatternExpressionOp>(op);
    if (!structured) {
      emitError(location)
          << "associative assignment patterns require keyed setters";
      return failure();
    }
    if (structured.getMemberSetterCount() != 0 ||
        structured.getTypeSetterCount() != 0) {
      emitError(location)
          << "associative assignment pattern contains a non-index setter";
      return failure();
    }
    SmallVector<Operation *> children = getChildren(op);
    uint64_t indexCount = structured.getIndexSetterCount();
    uint64_t expected =
        indexCount * 2 + (structured.getHasDefaultSetter() ? 1 : 0);
    if (children.size() != expected) {
      emitError(location)
          << "malformed associative assignment-pattern setter inventory";
      return failure();
    }
    FailureOr<Value> created = createAssocArray(array, location);
    if (failed(created))
      return failure();
    Value result = *created;
    for (uint64_t index = 0; index < indexCount; ++index) {
      Operation *keyNode = children[index * 2];
      Operation *valueNode = children[index * 2 + 1];
      FailureOr<Value> key = lowerExpression(keyNode);
      FailureOr<Value> value = lowerExpression(valueNode);
      if (failed(key) || failed(value))
        return failure();
      FailureOr<Value> convertedKey =
          convert(*key, array.getKeyType(), isSignedNode(keyNode),
                  getSemanticLocation(keyNode), array.getSignedKey());
      FailureOr<Value> convertedValue =
          convert(*value, array.getElementType(), isSignedNode(valueNode),
                  getSemanticLocation(valueNode));
      if (failed(convertedKey) || failed(convertedValue))
        return failure();
      sim::SimAssocWriteOp::create(builder, getSemanticLocation(valueNode),
                                   result, *convertedKey, *convertedValue);
    }
    if (structured.getHasDefaultSetter()) {
      Operation *defaultNode = children.back();
      FailureOr<Value> value = lowerExpression(defaultNode);
      if (failed(value))
        return failure();
      FailureOr<Value> converted =
          convert(*value, array.getElementType(), isSignedNode(defaultNode),
                  getSemanticLocation(defaultNode));
      if (failed(converted))
        return failure();
      sim::SimAssocSetDefaultOp::create(
          builder, getSemanticLocation(defaultNode), result, *converted);
    }
    return result;
  }
  if (auto array = dyn_cast<sim::DynamicArrayType>(*resultType)) {
    SmallVector<Operation *> children = getChildren(op);
    FailureOr<ContainerElementDescriptor> descriptor =
        describeContainerElement(array.getElementType(), location);
    if (failed(descriptor))
      return failure();
    Value size =
        arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                  builder.getI64IntegerAttr(children.size()));
    Value result = sim::SimContainerCreateOp::create(
        builder, location, *resultType, size, descriptor->typeID,
        descriptor->kind, descriptor->flags, descriptor->valueSize,
        descriptor->alignment, descriptor->bitWidth,
        builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(descriptor->traceKinds),
        OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 0);
    for (auto [index, child] : llvm::enumerate(children)) {
      FailureOr<Value> value = lowerExpression(child);
      if (failed(value))
        return failure();
      FailureOr<Value> converted =
          convert(*value, array.getElementType(), isSignedNode(child),
                  getSemanticLocation(child));
      if (failed(converted))
        return failure();
      Value ordinal =
          arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                    builder.getI64IntegerAttr(index));
      sim::SimContainerWriteOp::create(builder, location, result, ordinal,
                                       *converted);
    }
    return result;
  }
  if (!sim::isAggregateType(*resultType)) {
    unsupported(op) << " (non-aggregate assignment pattern)";
    return failure();
  }
  SmallVector<Operation *> children = getChildren(op);
  if (isa<sim::PackedUnionType, sim::UnpackedUnionType>(*resultType)) {
    if (children.size() != 1) {
      unsupported(op) << " (union assignment pattern arity)";
      return failure();
    }
    FailureOr<Value> value = lowerExpression(children.front());
    Type fieldType = sim::getAggregateElementType(*resultType, 0);
    if (failed(value))
      return failure();
    FailureOr<Value> converted =
        convert(*value, fieldType, isSignedNode(children.front()), location);
    if (failed(converted))
      return failure();
    return sim::SimUnionConstructOp::create(builder, location, *resultType,
                                            *converted, 0)
        .getResult();
  }
  if (children.size() != sim::getAggregateNumElements(*resultType)) {
    unsupported(op) << " (assignment pattern element inventory)";
    return failure();
  }
  SmallVector<Value> elements;
  for (auto [index, child] : llvm::enumerate(children)) {
    FailureOr<Value> value = lowerExpression(child);
    if (failed(value))
      return failure();
    FailureOr<Value> converted =
        convert(*value, sim::getAggregateElementType(*resultType, index),
                isSignedNode(child), location);
    if (failed(converted))
      return failure();
    elements.push_back(*converted);
  }
  return sim::SimAggregateConstructOp::create(builder, location, *resultType,
                                              elements)
      .getResult();
}

FailureOr<Value> UnitLowering::lowerNewArray(Operation *op) {
  Location location = getSemanticLocation(op);
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  auto array = succeeded(resultType)
                   ? dyn_cast<sim::DynamicArrayType>(*resultType)
                   : sim::DynamicArrayType{};
  SmallVector<Operation *> children = getChildren(op);
  if (!array || children.empty() || children.size() > 2) {
    emitError(location)
        << "new dynamic array requires a size and at most one initializer";
    return failure();
  }
  FailureOr<Value> sizeValue = lowerExpression(children.front());
  if (failed(sizeValue))
    return failure();
  FailureOr<Value> scalarSize = toPackedScalar(*sizeValue, location);
  if (failed(scalarSize))
    return failure();
  FailureOr<Value> size = convert(*scalarSize, builder.getI64Type(),
                                  isSignedNode(children.front()), location);
  FailureOr<ContainerElementDescriptor> descriptor =
      describeContainerElement(array.getElementType(), location);
  if (failed(size) || failed(descriptor))
    return failure();
  Value result = sim::SimContainerCreateOp::create(
      builder, location, *resultType, *size, descriptor->typeID,
      descriptor->kind, descriptor->flags, descriptor->valueSize,
      descriptor->alignment, descriptor->bitWidth,
      builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
      builder.getDenseI32ArrayAttr(descriptor->traceKinds),
      OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 0);
  if (children.size() == 1)
    return result;

  FailureOr<Value> source = lowerExpression(children[1]);
  if (failed(source))
    return failure();
  FailureOr<Value> convertedSource =
      convert(*source, *resultType, isSignedNode(children[1]), location);
  if (failed(convertedSource))
    return failure();
  Value sourceSize = sim::SimContainerSizeOp::create(
      builder, location, builder.getI64Type(), *convertedSource);
  Value sourceShorter = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ult, sourceSize, *size);
  Value copySize = arith::SelectOp::create(builder, location, sourceShorter,
                                           sourceSize, *size);
  Block *header = addBlock();
  header->addArgument(builder.getI64Type(), location);
  Block *body = addBlock();
  Block *exit = addBlock();
  Value zero = arith::ConstantOp::create(
      builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
  cf::BranchOp::create(builder, location, header, ValueRange{zero});
  setCurrent(header);
  Value index = header->getArgument(0);
  Value more = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ult, index, copySize);
  cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                           ValueRange{});
  setCurrent(body);
  Value element = sim::SimContainerReadOp::create(
      builder, location, array.getElementType(), *convertedSource, index);
  sim::SimContainerWriteOp::create(builder, location, result, index, element);
  Value one = arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                        builder.getI64IntegerAttr(1));
  Value next = arith::AddIOp::create(builder, location, index, one);
  cf::BranchOp::create(builder, location, header, ValueRange{next});
  setCurrent(exit);
  return result;
}

FailureOr<Value> UnitLowering::lowerSelection(Operation *op, bool lvalue) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  bool element = isa<semantic::SVElementSelectExpressionOp>(op);
  if (children.size() != (element ? 2u : 3u)) {
    unsupported(op) << " (selection arity)";
    return failure();
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  FailureOr<Value> input = lowerExpression(children.front(), lvalue);
  if (failed(resultType) || failed(input))
    return failure();

  Type sourceValueType = (*input).getType();
  if (auto reference = dyn_cast<sim::RefType>(sourceValueType))
    sourceValueType = reference.getElementType();
  else if (auto net = dyn_cast<sim::NetType>(sourceValueType))
    sourceValueType = net.getElementType();
  else if (auto driver = dyn_cast<sim::DriverType>(sourceValueType))
    sourceValueType = driver.getElementType();

  if (!element && isa<sim::QueueType>(sourceValueType) &&
      isa<sim::QueueType>(*resultType)) {
    if (lvalue) {
      unsupported(op) << " (queue slice lvalue)";
      return failure();
    }
    auto range = cast<semantic::SVRangeSelectExpressionOp>(op);
    if (range.getSelectionKind() != semantic::SVRangeSelectionKind::Simple) {
      unsupported(op) << " (indexed queue slice)";
      return failure();
    }
    Value container = *input;
    if (isa<sim::RefType, sim::ManagedRefType, sim::ArgumentRefType>(
            container.getType())) {
      FailureOr<Value> loaded = loadReference(container, location);
      if (failed(loaded))
        return failure();
      container = *loaded;
    }
    auto i64Constant = [&](int64_t value) -> Value {
      return arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                       builder.getI64IntegerAttr(value));
    };
    Value zero = i64Constant(0);
    Value one = i64Constant(1);
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), container);
    Value last = arith::SubIOp::create(builder, location, size, one);
    auto lowerBound = [&](Operation *bound) -> FailureOr<Value> {
      if (isUnboundedEndpoint(bound))
        return last;
      Value previousPlaceholder = unboundedPlaceholder;
      unboundedPlaceholder = last;
      FailureOr<Value> value = lowerExpression(bound);
      unboundedPlaceholder = previousPlaceholder;
      if (failed(value))
        return failure();
      FailureOr<Value> scalar =
          toPackedScalar(*value, getSemanticLocation(bound));
      if (failed(scalar))
        return failure();
      return convert(*scalar, builder.getI64Type(), isSignedNode(bound),
                     getSemanticLocation(bound));
    };
    FailureOr<Value> first = lowerBound(children[1]);
    FailureOr<Value> second = lowerBound(children[2]);
    if (failed(first) || failed(second))
      return failure();
    Value firstNegative = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::slt, *first, zero);
    Value start =
        arith::SelectOp::create(builder, location, firstNegative, zero, *first);
    Value secondAbove = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sgt, *second, last);
    Value finish =
        arith::SelectOp::create(builder, location, secondAbove, last, *second);
    Value nonempty = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, size, zero);
    Value startInRange = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::slt, start, size);
    Value ordered = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sle, start, finish);
    Value valid =
        arith::AndIOp::create(builder, location, nonempty, startInRange);
    valid = arith::AndIOp::create(builder, location, valid, ordered);

    auto resultQueue = cast<sim::QueueType>(*resultType);
    FailureOr<ContainerElementDescriptor> descriptor =
        describeContainerElement(resultQueue.getElementType(), location);
    if (failed(descriptor))
      return failure();
    uint64_t bound =
        resultQueue.getBound() ? resultQueue.getBound() : UINT64_MAX;
    Value result = sim::SimContainerCreateOp::create(
        builder, location, *resultType, zero, descriptor->typeID,
        descriptor->kind, descriptor->flags, descriptor->valueSize,
        descriptor->alignment, descriptor->bitWidth,
        builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(descriptor->traceKinds),
        OBELISK_RT_CONTAINER_QUEUE, bound);

    Block *header = addBlock();
    header->addArgument(builder.getI64Type(), location);
    Block *body = addBlock();
    Block *exit = addBlock();
    cf::CondBranchOp::create(builder, location, valid, header,
                             ValueRange{start}, exit, ValueRange{});
    setCurrent(header);
    Value index = header->getArgument(0);
    Value inRange = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sle, index, finish);
    cf::CondBranchOp::create(builder, location, inRange, body, ValueRange{},
                             exit, ValueRange{});
    setCurrent(body);
    Type sourceElement = cast<sim::QueueType>(sourceValueType).getElementType();
    Value value = sim::SimContainerReadOp::create(
        builder, location, sourceElement, container, index);
    FailureOr<Value> converted =
        convert(value, resultQueue.getElementType(), false, location);
    if (failed(converted))
      return failure();
    Value outputIndex = arith::SubIOp::create(builder, location, index, start);
    sim::SimContainerWriteOp::create(builder, location, result, outputIndex,
                                     *converted);
    Value next = arith::AddIOp::create(builder, location, index, one);
    cf::BranchOp::create(builder, location, header, ValueRange{next});
    setCurrent(exit);
    return result;
  }

  if (!element && isa<sim::UnpackedArrayType>(sourceValueType) &&
      isa<sim::UnpackedArrayType>(*resultType)) {
    if (lvalue) {
      unsupported(op) << " (unpacked array slice lvalue)";
      return failure();
    }
    auto range = cast<semantic::SVRangeSelectExpressionOp>(op);
    auto resultArray = cast<sim::UnpackedArrayType>(*resultType);
    Value aggregate = *input;
    if (isa<sim::RefType, sim::ManagedRefType, sim::ArgumentRefType>(
            aggregate.getType())) {
      FailureOr<Value> loaded = loadReference(aggregate, location);
      if (failed(loaded))
        return failure();
      aggregate = *loaded;
    }
    auto indexType = IntegerType::get(function.getContext(), 65);
    auto lowerIndex = [&](Operation *index) -> FailureOr<Value> {
      FailureOr<Value> value = lowerExpression(index);
      if (failed(value))
        return failure();
      FailureOr<Value> scalar =
          toPackedScalar(*value, getSemanticLocation(index));
      if (failed(scalar))
        return failure();
      return convert(*scalar, indexType, isSignedNode(index),
                     getSemanticLocation(index));
    };
    FailureOr<Value> first = lowerIndex(children[1]);
    if (failed(first))
      return failure();
    Value ascends;
    if (range.getSelectionKind() == semantic::SVRangeSelectionKind::Simple) {
      FailureOr<Value> second = lowerIndex(children[2]);
      if (failed(second))
        return failure();
      ascends = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::slt, *first, *second);
    }
    SmallVector<Value> elements;
    unsigned count = sim::getAggregateNumElements(resultArray);
    elements.reserve(count);
    for (unsigned ordinal = 0; ordinal < count; ++ordinal) {
      Value offset =
          arith::ConstantOp::create(builder, location, indexType,
                                    builder.getIntegerAttr(indexType, ordinal));
      Value above = arith::AddIOp::create(builder, location, *first, offset);
      Value below = arith::SubIOp::create(builder, location, *first, offset);
      Value index;
      switch (range.getSelectionKind()) {
      case semantic::SVRangeSelectionKind::Simple:
        index =
            arith::SelectOp::create(builder, location, ascends, above, below);
        break;
      case semantic::SVRangeSelectionKind::IndexedUp:
        index = above;
        break;
      case semantic::SVRangeSelectionKind::IndexedDown:
        index = below;
        break;
      }
      Type sourceElement =
          cast<sim::UnpackedArrayType>(sourceValueType).getElementType();
      Value elementValue = sim::SimArrayDynExtractOp::create(
          builder, location, sourceElement, aggregate, index);
      FailureOr<Value> converted =
          convert(elementValue, resultArray.getElementType(), false, location);
      if (failed(converted))
        return failure();
      elements.push_back(*converted);
    }
    return sim::SimAggregateConstructOp::create(builder, location, *resultType,
                                                elements)
        .getResult();
  }

  if (element) {
    if (auto array = dyn_cast<sim::AssocArrayType>(sourceValueType)) {
      Value container = *input;
      bool isReference =
          isa<sim::RefType, sim::ManagedRefType, sim::ArgumentRefType>(
              container.getType());
      if (isReference) {
        FailureOr<Value> loaded = loadReference(container, location);
        if (failed(loaded))
          return failure();
        container = *loaded;
      }
      FailureOr<Value> key = lowerExpression(children[1]);
      FailureOr<Value> convertedKey =
          succeeded(key)
              ? convert(*key, array.getKeyType(), isSignedNode(children[1]),
                        location, array.getSignedKey())
              : FailureOr<Value>(failure());
      FailureOr<Value> materialized =
          succeeded(convertedKey) ? ensureAssocArray(container, location)
                                  : FailureOr<Value>(failure());
      if (failed(convertedKey) || failed(materialized))
        return failure();
      container = *materialized;
      if (lvalue) {
        if (!isReference)
          return emitError(location)
                     << "associative element lvalue has no owning storage",
                 failure();
        if (failed(storeReference(*input, container, location)))
          return failure();
        FailureOr<Value> published = loadReference(*input, location);
        FailureOr<Value> owner =
            toArgumentReference(*input, sourceValueType, location);
        if (failed(published) || failed(owner))
          return failure();
        Type pathType =
            sim::ReferencePathType::get(function.getContext(), *resultType);
        return sim::SimReferencePathAssocOp::create(
                   builder, location, pathType,
                   function.getBody().front().getArgument(0), *published,
                   *convertedKey, *owner)
            .getResult();
      }
      return sim::SimAssocReadOp::create(builder, location, *resultType,
                                         container, *convertedKey)
          .getResult();
    }
  }

  if (element && isa<sim::DynamicArrayType, sim::QueueType>(sourceValueType)) {
    Value container = *input;
    bool isReference =
        isa<sim::RefType, sim::ManagedRefType, sim::ArgumentRefType>(
            container.getType());
    if (isReference) {
      FailureOr<Value> loaded = loadReference(container, location);
      if (failed(loaded))
        return failure();
      container = *loaded;
    }
    bool hasUnboundedIndex = containsUnboundedLiteral(children[1]);
    Value previousPlaceholder = unboundedPlaceholder;
    if (hasUnboundedIndex && isa<sim::QueueType>(sourceValueType)) {
      Value size = sim::SimContainerSizeOp::create(
          builder, location, builder.getI64Type(), container);
      Value one =
          arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                    builder.getI64IntegerAttr(1));
      unboundedPlaceholder =
          arith::SubIOp::create(builder, location, size, one);
    }
    Value resolvedIndex;
    if (isUnboundedEndpoint(children[1]))
      resolvedIndex = unboundedPlaceholder;
    else {
      FailureOr<Value> index = lowerExpression(children[1]);
      unboundedPlaceholder = previousPlaceholder;
      if (failed(index))
        return failure();
      FailureOr<Value> scalarIndex = toPackedScalar(*index, location);
      if (failed(scalarIndex))
        return failure();
      FailureOr<Value> index64 = convert(*scalarIndex, builder.getI64Type(),
                                         isSignedNode(children[1]), location);
      if (failed(index64))
        return failure();
      resolvedIndex = *index64;
    }
    unboundedPlaceholder = previousPlaceholder;
    if (!resolvedIndex)
      return emitError(location)
                 << "unbounded index requires a queue container",
             failure();
    if (lvalue) {
      if (!isReference)
        return emitError(location)
                   << "container element lvalue has no owning storage",
               failure();
      FailureOr<Value> materialized =
          ensureSequentialContainer(container, location);
      if (failed(materialized) ||
          failed(storeReference(*input, *materialized, location)))
        return failure();
      FailureOr<Value> published = loadReference(*input, location);
      Type pathType =
          sim::ReferencePathType::get(function.getContext(), *resultType);
      FailureOr<Value> ownerReference =
          toArgumentReference(*input, sourceValueType, location);
      if (failed(published) || failed(ownerReference))
        return failure();
      return sim::SimReferencePathIndexOp::create(
                 builder, location, pathType,
                 function.getBody().front().getArgument(0), *published,
                 resolvedIndex, *ownerReference)
          .getResult();
    }
    return sim::SimContainerReadOp::create(builder, location, *resultType,
                                           container, resolvedIndex)
        .getResult();
  }

  if (isa<sim::StringType>(sourceValueType)) {
    if (lvalue) {
      unsupported(op)
          << " (escaping string-character lvalues are not yet materialized)";
      return failure();
    }
    if (!element) {
      unsupported(op) << " (string range selection)";
      return failure();
    }
    FailureOr<Value> index = lowerExpression(children[1]);
    if (failed(index))
      return failure();
    FailureOr<Value> index64 = convert(*index, builder.getI64Type(),
                                       isSignedNode(children[1]), location);
    if (failed(index64))
      return failure();
    Value byte = sim::SimStringGetcOp::create(
        builder, location, builder.getI8Type(), *input, *index64);
    return convert(byte, *resultType, false, location);
  }

  // Fixed packed and unpacked arrays remain first-class aggregates. Their
  // dynamic operation consumes a source index, while static views use a
  // declaration-order ordinal.
  if (element &&
      isa<sim::PackedArrayType, sim::UnpackedArrayType>(sourceValueType)) {
    std::optional<unsigned> ordinal;
    if (isIntegerConstant(children[1])) {
      FailureOr<Type> indexType = getNormalizedSemanticType(children[1]);
      std::optional<unsigned> indexWidth =
          succeeded(indexType) ? sim::getPackedWidth(*indexType) : std::nullopt;
      if (failed(indexType) || !indexWidth)
        return failure();
      FailureOr<ParsedConstant> parsed = parseSVInteger(
          *getConstantSpelling(children[1]), *indexWidth, location);
      if (failed(parsed))
        return failure();
      if (parsed->unknown.isZero()) {
        APInt index = isSignedNode(children[1]) ? parsed->value.sextOrTrunc(65)
                                                : parsed->value.zextOrTrunc(65);
        if (index.isSignedIntN(64))
          ordinal = sim::getArrayElementOrdinal(sourceValueType,
                                                index.getSExtValue());
      }
    }
    if (ordinal) {
      if (auto reference = dyn_cast<sim::RefType>((*input).getType()))
        return sim::SimRefSubelementOp::create(
                   builder, location,
                   sim::RefType::get(function.getContext(), *resultType),
                   *input,
                   builder.getDenseI64ArrayAttr(
                       {static_cast<int64_t>(*ordinal)}))
            .getResult();
      if (auto driver = dyn_cast<sim::DriverType>((*input).getType()))
        return sim::SimDriverSubelementOp::create(
                   builder, location,
                   sim::DriverType::get(function.getContext(), *resultType),
                   *input,
                   builder.getDenseI64ArrayAttr(
                       {static_cast<int64_t>(*ordinal)}))
            .getResult();
      return sim::SimAggregateExtractOp::create(builder, location, *resultType,
                                                *input, *ordinal)
          .getResult();
    }

    FailureOr<Value> index = lowerExpression(children[1]);
    if (failed(index))
      return failure();
    FailureOr<Value> scalarIndex = toPackedScalar(*index, location);
    if (failed(scalarIndex))
      return failure();
    index = *scalarIndex;
    std::optional<unsigned> indexWidth =
        sim::getPackedWidth((*index).getType());
    if (!indexWidth || *indexWidth > std::numeric_limits<unsigned>::max() - 1) {
      emitError(location) << "array index is too wide to normalize";
      return failure();
    }
    unsigned widenedWidth = std::max(*indexWidth, 64u) + 1;
    Type widenedType =
        isa<sim::LogicType>((*index).getType())
            ? Type(sim::LogicType::get(function.getContext(), widenedWidth))
            : Type(IntegerType::get(function.getContext(), widenedWidth));
    FailureOr<Value> widened =
        convert(*index, widenedType, isSignedNode(children[1]), location);
    if (failed(widened))
      return failure();
    if (isa<sim::RefType>((*input).getType()))
      return sim::SimRefArrayElementOp::create(
                 builder, location,
                 sim::RefType::get(function.getContext(), *resultType), *input,
                 *widened)
          .getResult();
    if (isa<sim::DriverType>((*input).getType()))
      return sim::SimDriverArrayElementOp::create(
                 builder, location,
                 sim::DriverType::get(function.getContext(), *resultType),
                 *input, *widened)
          .getResult();
    return sim::SimArrayDynExtractOp::create(builder, location, *resultType,
                                             *input, *widened)
        .getResult();
  }

  Type scalarResultType = sim::getPackedScalarType(*resultType);
  if (!scalarResultType) {
    unsupported(op) << " (selection result type)";
    return failure();
  }
  std::optional<unsigned> resultWidth = sim::getPackedWidth(scalarResultType);
  if (!resultWidth) {
    unsupported(op) << " (selection result type)";
    return failure();
  }
  auto sourceTypeAttr =
      children.front()->getAttrOfType<TypeAttr>("semantic_type");
  auto sourceRange =
      sourceTypeAttr
          ? dyn_cast<semantic::RangedPackedArrayType>(sourceTypeAttr.getValue())
          : semantic::RangedPackedArrayType{};
  int64_t sourceRight = sourceRange ? sourceRange.getRight() : 0;
  bool descending = !sourceRange || sourceRange.getLeft() >= sourceRight;
  semantic::SVRangeSelectionKind selectionKind =
      element
          ? semantic::SVRangeSelectionKind::Simple
          : cast<semantic::SVRangeSelectExpressionOp>(op).getSelectionKind();

  std::optional<unsigned> sourceWidth = sim::getPackedWidth(sourceValueType);
  if (!sourceWidth) {
    unsupported(op) << " (selection input type)";
    return failure();
  }
  unsigned elementWidth = 1;
  if (auto array = dyn_cast<sim::PackedArrayType>(sourceValueType)) {
    std::optional<unsigned> width = sim::getPackedWidth(array.getElementType());
    if (!width || *width == 0) {
      unsupported(op) << " (selection element type)";
      return failure();
    }
    elementWidth = *width;
  }
  unsigned scaleBits = APInt(64, elementWidth - 1).getActiveBits();

  // Selection offsets are signless bitvectors in the target IR, so normalize
  // source indices in a type wide enough that signed values, declared bounds,
  // and indexed-part adjustments cannot wrap back into the valid source
  // range. Two extra bits cover an unsigned index plus any signed 64-bit
  // boundary. Logic resizing retains the unknown plane.
  auto getIndexArithmeticType = [&](Type type) -> FailureOr<Type> {
    Type scalarType = sim::getPackedScalarType(type);
    if (!scalarType) {
      emitError(location) << "selection index is not a packed value: " << type;
      return failure();
    }
    std::optional<unsigned> width = sim::getPackedWidth(scalarType);
    if (!width ||
        *width > std::numeric_limits<unsigned>::max() - 2 - scaleBits) {
      emitError(location) << "selection index is too wide to normalize";
      return failure();
    }
    unsigned arithmeticWidth = std::max(*width, 64u) + 2 + scaleBits;
    if (isa<sim::LogicType>(scalarType))
      return sim::LogicType::get(function.getContext(), arithmeticWidth);
    if (isa<IntegerType>(scalarType))
      return IntegerType::get(function.getContext(), arithmeticWidth);
    emitError(location) << "selection index is not a packed value: " << type;
    return failure();
  };
  auto createKnownIndex = [&](Type type, const APInt &value) -> Value {
    unsigned width = *sim::getPackedWidth(type);
    APInt resized = value.sextOrTrunc(width);
    auto planeType = IntegerType::get(function.getContext(), width);
    if (isa<IntegerType>(type))
      return arith::ConstantOp::create(
          builder, location, type, builder.getIntegerAttr(planeType, resized));
    return sim::SimLogicConstantOp::create(
        builder, location, type, builder.getIntegerAttr(planeType, resized),
        builder.getIntegerAttr(planeType, 0));
  };
  auto widenIndex = [&](Value value, Operation *source) -> FailureOr<Value> {
    FailureOr<Value> scalar = toPackedScalar(value, location);
    if (failed(scalar))
      return failure();
    FailureOr<Type> arithmeticType =
        getIndexArithmeticType((*scalar).getType());
    if (failed(arithmeticType))
      return failure();
    return convert(*scalar, *arithmeticType, isSignedNode(source), location);
  };
  auto subtract = [&](Value lhs, Value rhs) -> Value {
    if (isa<IntegerType>(lhs.getType()))
      return arith::SubIOp::create(builder, location, lhs, rhs);
    return sim::SimLogicBinaryOp::create(builder, location, lhs.getType(),
                                         sim::BinaryKind::Sub, lhs, rhs);
  };
  auto multiply = [&](Value lhs, Value rhs) -> Value {
    if (isa<IntegerType>(lhs.getType()))
      return arith::MulIOp::create(builder, location, lhs, rhs);
    return sim::SimLogicBinaryOp::create(builder, location, lhs.getType(),
                                         sim::BinaryKind::Mul, lhs, rhs);
  };

  // Known source bounds and literals are 64-bit. Two signed guard bits hold
  // their exact difference, and scaleBits prevents the packed-element stride
  // from wrapping the resulting flat bit offset.
  unsigned constantOffsetWidth = 66 + scaleBits;
  auto sourceOffset = [&](const APInt &index) -> APInt {
    APInt boundary(constantOffsetWidth, static_cast<uint64_t>(sourceRight),
                   true);
    APInt ordinal = descending ? index - boundary : boundary - index;
    return ordinal * APInt(constantOffsetWidth, elementWidth);
  };
  auto extendLiteral = [&](const ParsedConstant &literal, Operation *source,
                           Type sourceType) -> APInt {
    APInt value = literal.value;
    if (std::optional<unsigned> sourceWidth = sim::getPackedWidth(sourceType);
        sourceWidth && *sourceWidth < value.getBitWidth())
      value = value.trunc(*sourceWidth);
    return isSignedNode(source) ? value.sextOrTrunc(constantOffsetWidth)
                                : value.zextOrTrunc(constantOffsetWidth);
  };

  bool literalIndex = isIntegerConstant(children[1]);
  bool constant = false;
  uint64_t lowBit = 0;
  Value dynamicLow;
  if (literalIndex) {
    FailureOr<Type> firstIndexType = getNormalizedSemanticType(children[1]);
    if (failed(firstIndexType))
      return failure();
    FailureOr<ParsedConstant> first =
        parseSVInteger(*getConstantSpelling(children[1]), 64, location);
    if (failed(first))
      return failure();
    std::optional<APInt> knownLow;
    if (first->unknown.isZero())
      knownLow =
          sourceOffset(extendLiteral(*first, children[1], *firstIndexType));
    if (knownLow && !element) {
      if (selectionKind == semantic::SVRangeSelectionKind::Simple) {
        if (!isIntegerConstant(children[2])) {
          unsupported(op) << " (mixed constant and dynamic selection bounds)";
          return failure();
        }
        FailureOr<ParsedConstant> second =
            parseSVInteger(*getConstantSpelling(children[2]), 64, location);
        if (failed(second))
          return failure();
        if (second->unknown.isZero()) {
          FailureOr<Type> secondIndexType =
              getNormalizedSemanticType(children[2]);
          if (failed(secondIndexType))
            return failure();
          APInt secondLow = sourceOffset(
              extendLiteral(*second, children[2], *secondIndexType));
          if (secondLow.slt(*knownLow))
            knownLow = secondLow;
        } else {
          knownLow.reset();
        }
      } else {
        // The second operand of an indexed part-select is its width, not a
        // second source index.  The elaborated result type already carries
        // that constant width.  Convert a base that names the high physical
        // bit to the low-bit offset expected by the simulation extract ops.
        bool baseNamesHighBit =
            (descending &&
             selectionKind == semantic::SVRangeSelectionKind::IndexedDown) ||
            (!descending &&
             selectionKind == semantic::SVRangeSelectionKind::IndexedUp);
        if (baseNamesHighBit && *resultWidth > elementWidth)
          *knownLow -=
              APInt(constantOffsetWidth, *resultWidth - elementWidth);
      }
    }
    bool inRange =
        knownLow && !knownLow->isNegative() && *resultWidth <= *sourceWidth &&
        knownLow->ule(
            APInt(constantOffsetWidth,
                  static_cast<uint64_t>(*sourceWidth - *resultWidth)));
    if (inRange) {
      constant = true;
      lowBit = knownLow->getZExtValue();
    } else if (knownLow) {
      // A statically out-of-range selection still uses the dynamic operation:
      // its contract preserves valid positions and supplies the appropriate
      // X/zero fallback for invalid positions.
      FailureOr<Type> arithmeticType = getIndexArithmeticType(*firstIndexType);
      if (failed(arithmeticType))
        return failure();
      dynamicLow = createKnownIndex(*arithmeticType, *knownLow);
    }
  }
  if (!constant && !dynamicLow) {
    // Element selects use the same dynamic operation as indexed part-selects.
    // Only a simple part-select with dynamic bounds remains unsupported.
    if (!element && selectionKind == semantic::SVRangeSelectionKind::Simple) {
      unsupported(op) << " (dynamic simple range selection)";
      return failure();
    }
    FailureOr<Value> index = lowerExpression(children[1]);
    if (failed(index))
      return failure();
    FailureOr<Value> widened = widenIndex(*index, children[1]);
    if (failed(widened))
      return failure();
    dynamicLow = *widened;
    unsigned arithmeticWidth = *sim::getPackedWidth(dynamicLow.getType());
    auto createKnownOffset = [&](int64_t value) -> Value {
      return createKnownIndex(
          dynamicLow.getType(),
          APInt(arithmeticWidth, static_cast<uint64_t>(value), true));
    };
    if (sourceRight != 0 || !descending) {
      Value boundary = createKnownOffset(sourceRight);
      dynamicLow = descending ? subtract(dynamicLow, boundary)
                              : subtract(boundary, dynamicLow);
    }
    if (elementWidth > 1) {
      Value scale = createKnownOffset(elementWidth);
      dynamicLow = multiply(dynamicLow, scale);
    }
    bool baseNamesHighBit =
        (descending &&
         selectionKind == semantic::SVRangeSelectionKind::IndexedDown) ||
        (!descending &&
         selectionKind == semantic::SVRangeSelectionKind::IndexedUp);
    if (baseNamesHighBit && *resultWidth > elementWidth) {
      Value adjustment = createKnownOffset(*resultWidth - elementWidth);
      dynamicLow = subtract(dynamicLow, adjustment);
    }
  }

  if (isa<sim::RefType>((*input).getType())) {
    Type selected = sim::RefType::get(function.getContext(), *resultType);
    if (constant)
      return sim::SimRefExtractOp::create(builder, location, selected, *input,
                                          builder.getI64IntegerAttr(lowBit))
          .getResult();
    return sim::SimRefDynExtractOp::create(builder, location, selected, *input,
                                           dynamicLow)
        .getResult();
  }
  if (isa<sim::NetType>((*input).getType())) {
    if (!constant) {
      emitError(location)
          << "force and release require constant built-in net selects";
      return failure();
    }
    Type selected = sim::NetType::get(function.getContext(), *resultType);
    return sim::SimNetExtractOp::create(builder, location, selected, *input,
                                        builder.getI64IntegerAttr(lowBit))
        .getResult();
  }
  if (isa<sim::DriverType>((*input).getType())) {
    Type selected = sim::DriverType::get(function.getContext(), *resultType);
    if (constant)
      return sim::SimDriverExtractOp::create(builder, location, selected,
                                             *input,
                                             builder.getI64IntegerAttr(lowBit))
          .getResult();
    return sim::SimDriverDynExtractOp::create(builder, location, selected,
                                              *input, dynamicLow)
        .getResult();
  }
  FailureOr<Value> scalarInput = toPackedScalar(*input, location);
  if (failed(scalarInput))
    return failure();
  input = *scalarInput;
  if (isa<sim::LogicType>((*input).getType())) {
    auto selected = cast<sim::LogicType>(scalarResultType);
    Value value;
    if (constant)
      value =
          sim::SimLogicExtractOp::create(builder, location, selected, *input,
                                         builder.getI64IntegerAttr(lowBit));
    else
      value = sim::SimLogicDynExtractOp::create(builder, location, selected,
                                                *input, dynamicLow);
    return convert(value, *resultType, false, location);
  }
  auto selected = cast<IntegerType>(scalarResultType);
  auto inputInteger = cast<IntegerType>((*input).getType());
  if (!constant) {
    Value value = sim::SimBitsDynExtractOp::create(builder, location, selected,
                                                   *input, dynamicLow);
    return convert(value, *resultType, false, location);
  }
  Value amount =
      arith::ConstantOp::create(builder, location, inputInteger,
                                builder.getIntegerAttr(inputInteger, lowBit));
  Value shifted = arith::ShRUIOp::create(builder, location, *input, amount);
  Value value = selected == inputInteger
                    ? shifted
                    : Value(arith::TruncIOp::create(builder, location, selected,
                                                    shifted));
  return convert(value, *resultType, false, location);
}

} // namespace obelisk::simlowering
