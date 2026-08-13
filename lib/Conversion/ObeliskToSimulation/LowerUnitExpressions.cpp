//===- LowerUnitExpressions.cpp - Lower values and selections ---------===//

#include "LowerUnit.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <functional>
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
  if (auto objectField =
          op->getAttrOfType<FlatSymbolRefAttr>(randomNestedStateFieldAttrName)) {
    auto concreteTypeAttr = op->getAttrOfType<TypeAttr>(
        randomNestedStateConcreteTypeAttrName);
    auto storageTypeAttr =
        op->getAttrOfType<TypeAttr>(randomNestedStateStorageTypeAttrName);
    auto pathAttr =
        op->getAttrOfType<ArrayAttr>(randomNestedStatePathAttrName);
    auto childField =
        op->getAttrOfType<FlatSymbolRefAttr>("obelisk_sim.class_field");
    auto parentType =
        thisObject ? dyn_cast<sim::ClassHandleType>(thisObject.getType())
                   : sim::ClassHandleType{};
    auto concreteType = concreteTypeAttr
                            ? dyn_cast<sim::ClassHandleType>(
                                  concreteTypeAttr.getValue())
                            : sim::ClassHandleType{};
    auto storageType = storageTypeAttr
                           ? dyn_cast<sim::ClassHandleType>(
                                 storageTypeAttr.getValue())
                           : sim::ClassHandleType{};
    FailureOr<Type> elementType = getNormalizedSemanticType(op);
    if (lvalue || !parentType || !concreteType || !storageType ||
        !childField || failed(elementType)) {
      emitError(getSemanticLocation(op))
          << "nested randomization state capture is malformed";
      return failure();
    }
    Location location = getSemanticLocation(op);
    Type objectReferenceType = sim::ManagedRefType::get(
        function.getContext(), storageType, parentType.getClassName());
    Value objectReference = sim::SimClassFieldRefOp::create(
        builder, location, objectReferenceType, thisObject, objectField);
    recordManagedRead(objectReference, location);
    Value object = sim::SimManagedLoadOp::create(
        builder, location, storageType, objectReference);
    Value isNull = sim::SimManagedIsNullOp::create(
        builder, location, builder.getI1Type(), object);
    Block *missing = addBlock();
    Block *present = addBlock();
    Block *resume = addBlock();
    resume->addArgument(*elementType, location);
    // A null random child contributes neither variables nor constraints. Its
    // constraint-mode bits are disabled by lowerRandomize; provide a default
    // SSA capture here solely to keep eagerly materialized solver operands
    // null-safe.
    cf::CondBranchOp::create(builder, location, isNull, missing, ValueRange{},
                             present, ValueRange{});
    setCurrent(missing);
    Value defaultValue = createDefaultValue(builder, location, *elementType);
    if (!defaultValue)
      return failure();
    cf::BranchOp::create(builder, location, resume, ValueRange{defaultValue});
    setCurrent(present);
    if (object.getType() != concreteType)
      object = sim::SimClassCastOp::create(builder, location, concreteType,
                                           object);
    if (pathAttr) {
      for (Attribute pathElementAttr : pathAttr) {
        auto pathElement = dyn_cast<DictionaryAttr>(pathElementAttr);
        auto pathField =
            pathElement
                ? pathElement.getAs<FlatSymbolRefAttr>("field")
                : FlatSymbolRefAttr{};
        auto pathConcreteTypeAttr =
            pathElement ? pathElement.getAs<TypeAttr>("concrete_type")
                        : TypeAttr{};
        auto pathStorageTypeAttr =
            pathElement ? pathElement.getAs<TypeAttr>("storage_type")
                        : TypeAttr{};
        auto pathConcreteType =
            pathConcreteTypeAttr
                ? dyn_cast<sim::ClassHandleType>(
                      pathConcreteTypeAttr.getValue())
                : sim::ClassHandleType{};
        auto pathStorageType =
            pathStorageTypeAttr
                ? dyn_cast<sim::ClassHandleType>(
                      pathStorageTypeAttr.getValue())
                : sim::ClassHandleType{};
        if (!pathField || !pathConcreteType || !pathStorageType) {
          emitError(location)
              << "nested randomization state path is malformed";
          return failure();
        }
        Type pathReferenceType = sim::ManagedRefType::get(
            function.getContext(), pathStorageType,
            concreteType.getClassName());
        Value pathReference = sim::SimClassFieldRefOp::create(
            builder, location, pathReferenceType, object, pathField);
        recordManagedRead(pathReference, location);
        Value nextObject = sim::SimManagedLoadOp::create(
            builder, location, pathStorageType, pathReference);
        Value pathNull = sim::SimManagedIsNullOp::create(
            builder, location, builder.getI1Type(), nextObject);
        Block *nextPresent = addBlock();
        cf::CondBranchOp::create(builder, location, pathNull, missing,
                                 ValueRange{}, nextPresent, ValueRange{});
        setCurrent(nextPresent);
        object = nextObject;
        if (object.getType() != pathConcreteType)
          object = sim::SimClassCastOp::create(
              builder, location, pathConcreteType, object);
        concreteType = pathConcreteType;
      }
    }
    Type childReferenceType = sim::ManagedRefType::get(
        function.getContext(), *elementType, concreteType.getClassName());
    Value childReference = sim::SimClassFieldRefOp::create(
        builder, location, childReferenceType, object, childField);
    recordManagedRead(childReference, location);
    Value value = sim::SimManagedLoadOp::create(
        builder, location, *elementType, childReference);
    cf::BranchOp::create(builder, location, resume, ValueRange{value});
    setCurrent(resume);
    return resume->getArgument(0);
  }
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
    recordManagedRead(reference, getSemanticLocation(op));
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
    if (sampleAssertionValues) {
      // Preserve an aggregate reference until selections have narrowed it to
      // packed storage. sampled_read snapshots packed state ranges; sampling
      // the containing unpacked array both loses the selected identity and is
      // outside the operation's contract.
      if (!sim::getPackedWidth(ref.getElementType()))
        return value;
      Value context = function.getBody().front().getArgument(0);
      return sim::SimSampledReadOp::create(builder, location,
                                           ref.getElementType(), context, value)
          .getResult();
    }
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
    if (sampleAssertionValues) {
      Value context = function.getBody().front().getArgument(0);
      return sim::SimSampledReadOp::create(builder, location,
                                           net.getElementType(), context, value)
          .getResult();
    }
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

bool UnitLowering::streamContainsFourState(Type type) const {
  Type scalar = sim::getPackedScalarType(type);
  if (scalar && isa<sim::LogicType>(scalar))
    return true;
  if (auto array = dyn_cast<sim::DynamicArrayType>(type))
    return streamContainsFourState(array.getElementType());
  if (auto queue = dyn_cast<sim::QueueType>(type))
    return streamContainsFourState(queue.getElementType());
  if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type)) {
    if (unionType.getIsTagged() ||
        sim::getAggregateNumElements(unionType) == 0)
      return false;
    return streamContainsFourState(
        sim::getAggregateElementType(unionType, 0));
  }
  if (isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type)) {
    unsigned count = sim::getAggregateNumElements(type);
    for (unsigned ordinal = 0; ordinal < count; ++ordinal)
      if (streamContainsFourState(
              sim::getAggregateElementType(type, ordinal)))
        return true;
  }
  return false;
}

bool UnitLowering::streamNodeContainsFourState(Operation *node) const {
  if (auto streaming =
          dyn_cast<semantic::SVStreamingConcatenationExpressionOp>(node)) {
    ArrayRef<int64_t> flags = streaming.getStreamWithFlags();
    SmallVector<Operation *> children = getChildren(streaming);
    size_t next = 0;
    for (int64_t flag : flags) {
      if (next >= children.size())
        return false;
      if (streamNodeContainsFourState(children[next++]))
        return true;
      if (flag != 0)
        ++next;
    }
    return false;
  }
  FailureOr<Type> type = getNormalizedSemanticType(node);
  return succeeded(type) && streamContainsFourState(*type);
}

FailureOr<Value> UnitLowering::createBitStream(bool fourState,
                                               Location location) {
  Type bitType = fourState
                     ? Type(sim::LogicType::get(function.getContext(), 1))
                     : Type(builder.getI1Type());
  Type streamType = sim::QueueType::get(function.getContext(), bitType, 0);
  FailureOr<ContainerElementDescriptor> descriptor =
      describeContainerElement(bitType, location);
  if (failed(descriptor))
    return failure();
  Value zero = arith::ConstantOp::create(
      builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
  return sim::SimContainerCreateOp::create(
             builder, location, streamType, zero, descriptor->typeID,
             descriptor->kind, descriptor->flags, descriptor->valueSize,
             descriptor->alignment, descriptor->bitWidth,
             builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
             builder.getDenseI32ArrayAttr(descriptor->traceKinds),
             OBELISK_RT_CONTAINER_QUEUE, UINT64_MAX)
      .getResult();
}

FailureOr<Value> UnitLowering::appendToBitStream(
    Value value, Value stream, Value outputIndex, bool fourState,
    Location location) {
  auto i64Constant = [&](uint64_t constant) -> Value {
    return arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                     builder.getI64IntegerAttr(constant));
  };
  Value zero = i64Constant(0);
  Value one = i64Constant(1);
  Type type = value.getType();
  if (isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type)) {
    unsigned count = sim::getAggregateNumElements(type);
    for (unsigned ordinal = 0; ordinal < count; ++ordinal) {
      Type elementType = sim::getAggregateElementType(type, ordinal);
      Value element = sim::SimAggregateExtractOp::create(
          builder, location, elementType, value, ordinal);
      FailureOr<Value> next = appendToBitStream(
          element, stream, outputIndex, fourState, location);
      if (failed(next))
        return failure();
      outputIndex = *next;
    }
    return outputIndex;
  }
  if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type)) {
    if (unionType.getIsTagged() ||
        sim::getAggregateNumElements(unionType) == 0)
      return emitError(location)
                 << "streams cannot flatten an empty or tagged unpacked union",
             failure();
    Type elementType = sim::getAggregateElementType(unionType, 0);
    Value element = sim::SimUnionExtractOp::create(
        builder, location, elementType, value, 0);
    return appendToBitStream(element, stream, outputIndex, fourState, location);
  }

  Type elementType;
  if (auto array = dyn_cast<sim::DynamicArrayType>(type))
    elementType = array.getElementType();
  else if (auto queue = dyn_cast<sim::QueueType>(type))
    elementType = queue.getElementType();
  if (elementType) {
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), value);
    Block *header = addBlock();
    header->addArgument(builder.getI64Type(), location);
    header->addArgument(builder.getI64Type(), location);
    Block *body = addBlock();
    Block *exit = addBlock();
    exit->addArgument(builder.getI64Type(), location);
    cf::BranchOp::create(builder, location, header,
                         ValueRange{zero, outputIndex});
    setCurrent(header);
    Value inputIndex = header->getArgument(0);
    Value currentOutput = header->getArgument(1);
    Value more = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, inputIndex, size);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                             ValueRange{currentOutput});
    setCurrent(body);
    Value element = sim::SimContainerReadOp::create(
        builder, location, elementType, value, inputIndex);
    FailureOr<Value> next = appendToBitStream(
        element, stream, currentOutput, fourState, location);
    if (failed(next))
      return failure();
    Value nextInput =
        arith::AddIOp::create(builder, location, inputIndex, one);
    cf::BranchOp::create(builder, location, header,
                         ValueRange{nextInput, *next});
    setCurrent(exit);
    return exit->getArgument(0);
  }

  FailureOr<Value> scalar = toPackedScalar(value, location);
  std::optional<unsigned> width =
      succeeded(scalar) ? sim::getPackedWidth((*scalar).getType())
                        : std::nullopt;
  if (failed(scalar) || !width || *width == 0)
    return emitError(location)
               << "streaming operands must recursively contain bit-stream "
                  "values or sequential containers",
           failure();
  Type bitType = cast<sim::QueueType>(stream.getType()).getElementType();
  Block *header = addBlock();
  header->addArgument(builder.getI64Type(), location);
  header->addArgument(builder.getI64Type(), location);
  Block *body = addBlock();
  Block *exit = addBlock();
  exit->addArgument(builder.getI64Type(), location);
  cf::BranchOp::create(builder, location, header,
                       ValueRange{zero, outputIndex});
  setCurrent(header);
  Value ordinal = header->getArgument(0);
  Value currentOutput = header->getArgument(1);
  Value more = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ult, ordinal,
      i64Constant(*width));
  cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                           ValueRange{currentOutput});
  setCurrent(body);
  Value low = arith::SubIOp::create(builder, location,
                                    i64Constant(*width - 1), ordinal);
  Value bit;
  if (isa<sim::LogicType>((*scalar).getType())) {
    bit = sim::SimLogicDynExtractOp::create(builder, location, bitType, *scalar,
                                            low);
  } else {
    bit = sim::SimBitsDynExtractOp::create(builder, location,
                                           builder.getI1Type(), *scalar, low);
    if (fourState) {
      FailureOr<Value> logic = toLogic(bit, location);
      if (failed(logic))
        return failure();
      bit = *logic;
    }
  }
  sim::SimContainerWriteOp::create(builder, location, stream, currentOutput,
                                   bit);
  Value nextOrdinal = arith::AddIOp::create(builder, location, ordinal, one);
  Value nextOutput =
      arith::AddIOp::create(builder, location, currentOutput, one);
  cf::BranchOp::create(builder, location, header,
                       ValueRange{nextOrdinal, nextOutput});
  setCurrent(exit);
  return exit->getArgument(0);
}

FailureOr<Value> UnitLowering::reorderBitStream(Value stream, uint64_t slice,
                                                Location location) {
  if (slice == 0)
    return stream;
  auto streamType = dyn_cast<sim::QueueType>(stream.getType());
  if (!streamType)
    return emitError(location) << "internal bit stream is not a queue",
           failure();
  bool fourState = isa<sim::LogicType>(streamType.getElementType());
  FailureOr<Value> reordered = createBitStream(fourState, location);
  if (failed(reordered))
    return failure();
  auto i64Constant = [&](uint64_t constant) -> Value {
    return arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                     builder.getI64IntegerAttr(constant));
  };
  Value zero = i64Constant(0);
  Value one = i64Constant(1);
  Value totalWidth = sim::SimContainerSizeOp::create(
      builder, location, builder.getI64Type(), stream);
  Value sliceValue = i64Constant(slice);
  Value fullBlocks = arith::DivUIOp::create(builder, location, totalWidth,
                                            sliceValue);
  Value fullBits =
      arith::MulIOp::create(builder, location, fullBlocks, sliceValue);
  Block *header = addBlock();
  header->addArgument(builder.getI64Type(), location);
  Block *body = addBlock();
  Block *full = addBlock();
  Block *partial = addBlock();
  Block *copy = addBlock();
  copy->addArgument(builder.getI64Type(), location);
  Block *exit = addBlock();
  cf::BranchOp::create(builder, location, header, ValueRange{zero});
  setCurrent(header);
  Value destination = header->getArgument(0);
  Value more = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ult, destination, totalWidth);
  cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                           ValueRange{});
  setCurrent(body);
  Value inFull = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ult, destination, fullBits);
  cf::CondBranchOp::create(builder, location, inFull, full, ValueRange{},
                           partial, ValueRange{});
  setCurrent(full);
  Value block =
      arith::DivUIOp::create(builder, location, destination, sliceValue);
  Value within =
      arith::RemUIOp::create(builder, location, destination, sliceValue);
  Value blockEnd = arith::MulIOp::create(
      builder, location,
      arith::AddIOp::create(builder, location, block, one), sliceValue);
  Value source = arith::AddIOp::create(
      builder, location,
      arith::SubIOp::create(builder, location, totalWidth, blockEnd), within);
  cf::BranchOp::create(builder, location, copy, ValueRange{source});
  setCurrent(partial);
  Value partialSource =
      arith::SubIOp::create(builder, location, destination, fullBits);
  cf::BranchOp::create(builder, location, copy, ValueRange{partialSource});
  setCurrent(copy);
  Value sourceIndex = copy->getArgument(0);
  Value bit = sim::SimContainerReadOp::create(
      builder, location, streamType.getElementType(), stream, sourceIndex);
  sim::SimContainerWriteOp::create(builder, location, *reordered, destination,
                                   bit);
  Value next = arith::AddIOp::create(builder, location, destination, one);
  cf::BranchOp::create(builder, location, header, ValueRange{next});
  setCurrent(exit);
  return *reordered;
}

FailureOr<Value>
UnitLowering::sliceStreamingContainer(Value container, Operation *withRange,
                                      Location location) {
  Type elementType;
  sim::UnpackedArrayType fixedArray;
  if (auto array = dyn_cast<sim::DynamicArrayType>(container.getType()))
    elementType = array.getElementType();
  else if (auto queue = dyn_cast<sim::QueueType>(container.getType()))
    elementType = queue.getElementType();
  else if ((fixedArray = dyn_cast<sim::UnpackedArrayType>(container.getType())))
    elementType = fixedArray.getElementType();
  if (!elementType)
    return emitError(location)
               << "streaming with requires a one-dimensional unpacked array",
           failure();
  SmallVector<Operation *> children = getChildren(withRange);
  bool elementRange = isa<semantic::SVElementSelectExpressionOp>(withRange);
  auto range = dyn_cast<semantic::SVRangeSelectExpressionOp>(withRange);
  if ((!elementRange && !range) ||
      children.size() != (elementRange ? 2u : 3u))
    return emitError(location) << "malformed streaming with range", failure();
  auto lowerIndex = [&](Operation *node) -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(node);
    if (failed(value))
      return failure();
    FailureOr<Value> scalar = toPackedScalar(*value, getSemanticLocation(node));
    if (failed(scalar))
      return failure();
    return convert(*scalar, builder.getI64Type(), isSignedNode(node),
                   getSemanticLocation(node));
  };
  FailureOr<Value> first = lowerIndex(children[1]);
  if (failed(first))
    return failure();
  Value zero = arith::ConstantOp::create(
      builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
  Value one = arith::ConstantOp::create(
      builder, location, builder.getI64Type(), builder.getI64IntegerAttr(1));
  auto requireRuntime = [&](Value condition,
                            StringRef detail) -> LogicalResult {
    Block *accepted = addBlock();
    Block *rejected = addBlock();
    cf::CondBranchOp::create(builder, location, condition, accepted,
                             ValueRange{}, rejected, ValueRange{});
    setCurrent(rejected);
    if (failed(emitRuntimeFatal(location, detail)))
      return failure();
    setCurrent(accepted);
    return success();
  };
  if (!fixedArray) {
    Value firstNonnegative = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sge, *first, zero);
    if (failed(requireRuntime(
            firstNonnegative,
            "streaming with range has a negative first index")))
      return failure();
  }

  Value count = one;
  Value ascends;
  semantic::SVRangeSelectionKind kind =
      semantic::SVRangeSelectionKind::IndexedUp;
  if (range) {
    kind = range.getSelectionKind();
    FailureOr<Value> second = lowerIndex(children[2]);
    if (failed(second))
      return failure();
    if (kind == semantic::SVRangeSelectionKind::Simple) {
      if (!fixedArray) {
        Value secondNonnegative = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::sge, *second, zero);
        if (failed(requireRuntime(
                secondNonnegative,
                "streaming with range has a negative second index")))
          return failure();
      }
      ascends = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::slt, *first,
                                      *second);
      Value high = arith::SelectOp::create(builder, location, ascends, *second,
                                           *first);
      Value low = arith::SelectOp::create(builder, location, ascends, *first,
                                          *second);
      count = arith::AddIOp::create(
          builder, location,
          arith::SubIOp::create(builder, location, high, low), one);
    } else {
      count = *second;
      Value positive = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::sgt, count, zero);
      if (failed(requireRuntime(
              positive,
              "streaming with indexed range has a nonpositive width")))
        return failure();
    }
  }

  Type resultType =
      sim::DynamicArrayType::get(function.getContext(), elementType);
  FailureOr<ContainerElementDescriptor> descriptor =
      describeContainerElement(elementType, location);
  if (failed(descriptor))
    return failure();
  Value result = sim::SimContainerCreateOp::create(
      builder, location, resultType, count, descriptor->typeID,
      descriptor->kind, descriptor->flags, descriptor->valueSize,
      descriptor->alignment, descriptor->bitWidth,
      builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
      builder.getDenseI32ArrayAttr(descriptor->traceKinds),
      OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 0);
  Value sourceSize;
  if (!fixedArray)
    sourceSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), container);
  Value defaultElement = createDefaultValue(builder, location, elementType);
  if (!defaultElement)
    return emitError(location)
               << "cannot create a nonexistent streaming array entry",
           failure();

  Block *header = addBlock();
  header->addArgument(builder.getI64Type(), location);
  Block *body = addBlock();
  Block *present = fixedArray ? nullptr : addBlock();
  Block *missing = fixedArray ? nullptr : addBlock();
  Block *resume = addBlock();
  resume->addArgument(elementType, location);
  Block *exit = addBlock();
  cf::BranchOp::create(builder, location, header, ValueRange{zero});
  setCurrent(header);
  Value ordinal = header->getArgument(0);
  Value more = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ult, ordinal, count);
  cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                           ValueRange{});
  setCurrent(body);
  Value above = arith::AddIOp::create(builder, location, *first, ordinal);
  Value below = arith::SubIOp::create(builder, location, *first, ordinal);
  Value sourceIndex;
  switch (kind) {
  case semantic::SVRangeSelectionKind::Simple:
    sourceIndex =
        arith::SelectOp::create(builder, location, ascends, above, below);
    break;
  case semantic::SVRangeSelectionKind::IndexedUp:
    sourceIndex = above;
    break;
  case semantic::SVRangeSelectionKind::IndexedDown:
    sourceIndex = below;
    break;
  }
  if (fixedArray) {
    Value selected = defaultElement;
    unsigned sourceCount = sim::getAggregateNumElements(fixedArray);
    int64_t declaredIndex = fixedArray.getLeft();
    int64_t step = fixedArray.getLeft() <= fixedArray.getRight() ? 1 : -1;
    for (unsigned sourceOrdinal = 0; sourceOrdinal < sourceCount;
         ++sourceOrdinal) {
      Value index = arith::ConstantOp::create(
          builder, location, builder.getI64Type(),
          builder.getI64IntegerAttr(declaredIndex));
      Value matches = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, sourceIndex, index);
      Block *matched = addBlock();
      Block *nextCandidate = addBlock();
      nextCandidate->addArgument(elementType, location);
      cf::CondBranchOp::create(builder, location, matches, matched,
                               ValueRange{}, nextCandidate,
                               ValueRange{selected});
      setCurrent(matched);
      Value element = sim::SimAggregateExtractOp::create(
          builder, location, elementType, container, sourceOrdinal);
      cf::BranchOp::create(builder, location, nextCandidate,
                           ValueRange{element});
      setCurrent(nextCandidate);
      selected = nextCandidate->getArgument(0);
      if (sourceOrdinal + 1 < sourceCount)
        declaredIndex += step;
    }
    cf::BranchOp::create(builder, location, resume, ValueRange{selected});
  } else {
    Value nonnegative = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sge, sourceIndex, zero);
    Value inBounds = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, sourceIndex, sourceSize);
    Value valid =
        arith::AndIOp::create(builder, location, nonnegative, inBounds);
    cf::CondBranchOp::create(builder, location, valid, present, ValueRange{},
                             missing, ValueRange{});
    setCurrent(present);
    Value selected = sim::SimContainerReadOp::create(
        builder, location, elementType, container, sourceIndex);
    cf::BranchOp::create(builder, location, resume, ValueRange{selected});
    setCurrent(missing);
    cf::BranchOp::create(builder, location, resume, ValueRange{defaultElement});
  }
  setCurrent(resume);
  sim::SimContainerWriteOp::create(builder, location, result, ordinal,
                                   resume->getArgument(0));
  Value next = arith::AddIOp::create(builder, location, ordinal, one);
  cf::BranchOp::create(builder, location, header, ValueRange{next});
  setCurrent(exit);
  return result;
}

FailureOr<Value> UnitLowering::lowerStreaming(
    semantic::SVStreamingConcatenationExpressionOp op, Type assignmentType) {
  Location location = getSemanticLocation(op);
  ArrayRef<int64_t> withFlags = op.getStreamWithFlags();
  SmallVector<Operation *> inventory = getChildren(op);
  size_t withCount = llvm::count(withFlags, int64_t{1});
  if (op.getStreamCount() == 0 || withFlags.size() != op.getStreamCount() ||
      llvm::any_of(withFlags,
                   [](int64_t flag) { return flag != 0 && flag != 1; }) ||
      inventory.size() != op.getStreamCount() + withCount)
    return op.emitError("malformed streaming child inventory"),
           failure();
  SmallVector<std::pair<Operation *, Operation *>> children;
  size_t nextChild = 0;
  for (int64_t withFlag : withFlags) {
    Operation *child = inventory[nextChild++];
    Operation *withRange = withFlag ? inventory[nextChild++] : nullptr;
    children.emplace_back(child, withRange);
  }

  bool hasDynamicTarget =
      assignmentType &&
      isa<sim::DynamicArrayType, sim::QueueType>(assignmentType);
  if (!op.getIsFixedSize() || hasDynamicTarget || withCount != 0) {
    if (assignmentType &&
        !isa<sim::DynamicArrayType, sim::QueueType>(assignmentType))
      return emitError(location)
                 << "a dynamically sized stream requires a dynamic array or "
                    "queue assignment target",
             failure();

    auto i64Constant = [&](uint64_t value) -> Value {
      return arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                       builder.getI64IntegerAttr(value));
    };
    Value zero = i64Constant(0);
    Value one = i64Constant(1);
    bool fourState = false;
    for (auto &part : children)
      fourState |= streamNodeContainsFourState(part.first);

    FailureOr<Value> generic = createBitStream(fourState, location);
    if (failed(generic))
      return failure();

    Value outputIndex = zero;
    for (auto [child, withRange] : children) {
      FailureOr<Value> value = lowerExpression(child);
      if (failed(value))
        return failure();
      Value streamedValue = *value;
      if (withRange) {
        FailureOr<Value> selected = sliceStreamingContainer(
            streamedValue, withRange, getSemanticLocation(child));
        if (failed(selected))
          return failure();
        streamedValue = *selected;
      }
      FailureOr<Value> next = appendToBitStream(
          streamedValue, *generic, outputIndex, fourState,
          getSemanticLocation(child));
      if (failed(next))
        return failure();
      outputIndex = *next;
    }
    Value totalWidth = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *generic);

    FailureOr<Value> stream =
        reorderBitStream(*generic, op.getSliceSize(), location);
    if (failed(stream))
      return failure();
    Type bitType = cast<sim::QueueType>((*stream).getType()).getElementType();

    // A dynamic stream can itself be a stream_expression in an enclosing
    // streaming concatenation. Preserve the ordered bit queue as the internal
    // value in that case; the outer recursive flattener consumes it exactly as
    // any other sequential container.
    if (!assignmentType)
      return *stream;

    Type targetElement = isa<sim::DynamicArrayType>(assignmentType)
                             ? cast<sim::DynamicArrayType>(assignmentType)
                                   .getElementType()
                             : cast<sim::QueueType>(assignmentType)
                                   .getElementType();
    Type targetScalar = sim::getPackedScalarType(targetElement);
    std::optional<unsigned> targetWidth = sim::getPackedWidth(targetElement);
    if (!targetScalar || !targetWidth || *targetWidth == 0)
      return emitError(location)
                 << "a dynamic stream target must have fixed-size bit-stream "
                    "elements",
             failure();
    Value elementWidth = i64Constant(*targetWidth);
    Value fullElements = arith::DivUIOp::create(builder, location, totalWidth,
                                                elementWidth);
    Value trailingBits = arith::RemUIOp::create(builder, location, totalWidth,
                                                elementWidth);
    Value hasTrailing = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, trailingBits, zero);
    Value trailingElement = arith::ExtUIOp::create(
        builder, location, builder.getI64Type(), hasTrailing);
    Value targetSize = arith::AddIOp::create(builder, location, fullElements,
                                             trailingElement);
    FailureOr<ContainerElementDescriptor> targetDescriptor =
        describeContainerElement(targetElement, location);
    if (failed(targetDescriptor))
      return failure();
    uint32_t containerKind = isa<sim::DynamicArrayType>(assignmentType)
                                 ? OBELISK_RT_CONTAINER_DYNAMIC_ARRAY
                                 : OBELISK_RT_CONTAINER_QUEUE;
    uint64_t bound = 0;
    if (auto queue = dyn_cast<sim::QueueType>(assignmentType))
      bound = queue.getBound() ? queue.getBound() : UINT64_MAX;
    Value allocationSize = containerKind == OBELISK_RT_CONTAINER_DYNAMIC_ARRAY
                               ? targetSize
                               : zero;
    Value result = sim::SimContainerCreateOp::create(
        builder, location, assignmentType, allocationSize,
        targetDescriptor->typeID, targetDescriptor->kind,
        targetDescriptor->flags, targetDescriptor->valueSize,
        targetDescriptor->alignment, targetDescriptor->bitWidth,
        builder.getDenseI64ArrayAttr(targetDescriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(targetDescriptor->traceKinds),
        containerKind, bound);

    Block *elementHeader = addBlock();
    elementHeader->addArgument(builder.getI64Type(), location);
    Block *elementBody = addBlock();
    Block *elementExit = addBlock();
    cf::BranchOp::create(builder, location, elementHeader, ValueRange{zero});
    setCurrent(elementHeader);
    Value elementIndex = elementHeader->getArgument(0);
    Value moreElements = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, elementIndex,
        targetSize);
    cf::CondBranchOp::create(builder, location, moreElements, elementBody,
                             ValueRange{}, elementExit, ValueRange{});
    setCurrent(elementBody);
    Value assembled;
    if (isa<sim::LogicType>(targetScalar)) {
      auto logicType = cast<sim::LogicType>(targetScalar);
      auto planeType = IntegerType::get(function.getContext(), *targetWidth);
      assembled = sim::SimLogicConstantOp::create(
          builder, location, logicType, builder.getIntegerAttr(planeType, 0),
          builder.getIntegerAttr(planeType, 0));
    } else {
      assembled = arith::ConstantOp::create(
          builder, location, targetScalar,
          builder.getIntegerAttr(targetScalar, 0));
    }
    for (unsigned ordinal = 0; ordinal < *targetWidth; ++ordinal) {
      Value sourceIndex = arith::AddIOp::create(
          builder, location,
          arith::MulIOp::create(builder, location, elementIndex, elementWidth),
          i64Constant(ordinal));
      Block *present = addBlock();
      Block *resume = addBlock();
      resume->addArgument(targetScalar, location);
      Value inRange = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, sourceIndex,
          totalWidth);
      cf::CondBranchOp::create(builder, location, inRange, present,
                               ValueRange{}, resume, ValueRange{assembled});
      setCurrent(present);
      Value bit = sim::SimContainerReadOp::create(
          builder, location, bitType, *stream, sourceIndex);
      Value inserted;
      unsigned low = *targetWidth - ordinal - 1;
      if (isa<sim::LogicType>(targetScalar)) {
        FailureOr<Value> logicBit = toLogic(bit, location);
        if (failed(logicBit))
          return failure();
        inserted = sim::SimLogicInsertOp::create(
            builder, location, targetScalar, assembled, *logicBit, low);
      } else {
        FailureOr<Value> bits = convert(bit, builder.getI1Type(), false,
                                        location);
        if (failed(bits))
          return failure();
        Value extended =
            arith::ExtUIOp::create(builder, location, targetScalar, *bits);
        if (low) {
          Value amount = arith::ConstantOp::create(
              builder, location, targetScalar,
              builder.getIntegerAttr(targetScalar, low));
          extended =
              arith::ShLIOp::create(builder, location, extended, amount);
        }
        inserted =
            arith::OrIOp::create(builder, location, assembled, extended);
      }
      cf::BranchOp::create(builder, location, resume, ValueRange{inserted});
      setCurrent(resume);
      assembled = resume->getArgument(0);
    }
    Value element = assembled;
    if (targetScalar != targetElement)
      element = sim::SimPackedUnflattenOp::create(builder, location,
                                                  targetElement, assembled);
    sim::SimContainerWriteOp::create(builder, location, result, elementIndex,
                                     element);
    Value nextElement =
        arith::AddIOp::create(builder, location, elementIndex, one);
    cf::BranchOp::create(builder, location, elementHeader,
                         ValueRange{nextElement});
    setCurrent(elementExit);
    return result;
  }

  SmallVector<Value> inputs;
  SmallVector<unsigned> widths;
  bool fourState = false;
  uint64_t totalWidth = 0;
  std::function<LogicalResult(Value, Location)> appendFixedValue;
  appendFixedValue = [&](Value value, Location valueLocation) -> LogicalResult {
    Type type = value.getType();
    if (isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type)) {
      unsigned count = sim::getAggregateNumElements(type);
      for (unsigned ordinal = 0; ordinal < count; ++ordinal) {
        Type elementType = sim::getAggregateElementType(type, ordinal);
        Value element = sim::SimAggregateExtractOp::create(
            builder, valueLocation, elementType, value, ordinal);
        if (failed(appendFixedValue(element, valueLocation)))
          return failure();
      }
      return success();
    }
    if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type)) {
      if (unionType.getIsTagged() ||
          sim::getAggregateNumElements(unionType) == 0)
        return emitError(valueLocation)
                   << "fixed streams cannot flatten an empty or tagged "
                      "unpacked union",
               failure();
      Type elementType = sim::getAggregateElementType(unionType, 0);
      Value element = sim::SimUnionExtractOp::create(
          builder, valueLocation, elementType, value, 0);
      return appendFixedValue(element, valueLocation);
    }
    FailureOr<Value> scalar = toPackedScalar(value, valueLocation);
    std::optional<unsigned> width =
        succeeded(scalar) ? sim::getPackedWidth((*scalar).getType())
                          : std::nullopt;
    if (failed(scalar) || !width || *width == 0)
      return emitError(valueLocation)
                 << "fixed streaming operands must recursively contain "
                    "nonempty bit-stream values",
             failure();
    if (totalWidth > std::numeric_limits<unsigned>::max() - *width)
      return emitError(location) << "fixed stream width is not representable",
             failure();
    totalWidth += *width;
    fourState |= isa<sim::LogicType>((*scalar).getType());
    inputs.push_back(*scalar);
    widths.push_back(*width);
    return success();
  };
  for (auto [child, withRange] : children) {
    (void)withRange;
    FailureOr<Value> value = lowerExpression(child);
    if (failed(value))
      return failure();
    if (failed(appendFixedValue(*value, getSemanticLocation(child))))
      return failure();
  }
  if (totalWidth != op.getBitstreamWidth())
    return op.emitError("fixed stream operand widths do not match metadata"),
           failure();

  Type streamScalar = fourState
                          ? Type(sim::LogicType::get(function.getContext(),
                                                    totalWidth))
                          : Type(IntegerType::get(function.getContext(),
                                                  totalWidth));
  Value generic;
  if (fourState) {
    SmallVector<Value> logicInputs;
    for (Value input : inputs) {
      FailureOr<Value> logic = toLogic(input, location);
      if (failed(logic))
        return failure();
      logicInputs.push_back(*logic);
    }
    generic = sim::SimLogicConcatOp::create(builder, location, streamScalar,
                                             logicInputs);
  } else {
    auto resultType = cast<IntegerType>(streamScalar);
    generic = arith::ConstantOp::create(builder, location, resultType,
                                        builder.getIntegerAttr(resultType, 0));
    unsigned trailing = totalWidth;
    for (auto [input, width] : llvm::zip_equal(inputs, widths)) {
      trailing -= width;
      FailureOr<Value> extended =
          convert(input, resultType, false, location);
      if (failed(extended))
        return failure();
      Value placed = *extended;
      if (trailing) {
        Value amount = arith::ConstantOp::create(
            builder, location, resultType,
            builder.getIntegerAttr(resultType, trailing));
        placed = arith::ShLIOp::create(builder, location, placed, amount);
      }
      generic = arith::OrIOp::create(builder, location, generic, placed);
    }
  }

  // Slang encodes >> as slice size zero. IEEE 1800-2023 11.4.14.2 says >>
  // preserves the generic stream, while << takes blocks from the right and
  // appends them left-to-right. The final partial block is therefore the most
  // significant block and is emitted last without padding.
  uint64_t slice = op.getSliceSize();
  Value reordered = generic;
  if (slice != 0 && totalWidth > slice) {
    SmallVector<Value> chunks;
    for (uint64_t low = 0; low < totalWidth; low += slice) {
      unsigned width = std::min<uint64_t>(slice, totalWidth - low);
      if (fourState) {
        chunks.push_back(sim::SimLogicExtractOp::create(
            builder, location,
            sim::LogicType::get(function.getContext(), width), generic, low));
      } else {
        auto fullType = cast<IntegerType>(streamScalar);
        Value shifted = generic;
        if (low) {
          Value amount = arith::ConstantOp::create(
              builder, location, fullType,
              builder.getIntegerAttr(fullType, low));
          shifted = arith::ShRUIOp::create(builder, location, generic, amount);
        }
        chunks.push_back(arith::TruncIOp::create(
            builder, location,
            IntegerType::get(function.getContext(), width), shifted));
      }
    }
    if (fourState) {
      reordered = sim::SimLogicConcatOp::create(builder, location, streamScalar,
                                                 chunks);
    } else {
      auto fullType = cast<IntegerType>(streamScalar);
      reordered = arith::ConstantOp::create(
          builder, location, fullType, builder.getIntegerAttr(fullType, 0));
      unsigned trailing = totalWidth;
      for (Value chunk : chunks) {
        unsigned width = cast<IntegerType>(chunk.getType()).getWidth();
        trailing -= width;
        Value extended = arith::ExtUIOp::create(builder, location, fullType,
                                                chunk);
        if (trailing) {
          Value amount = arith::ConstantOp::create(
              builder, location, fullType,
              builder.getIntegerAttr(fullType, trailing));
          extended =
              arith::ShLIOp::create(builder, location, extended, amount);
        }
        reordered =
            arith::OrIOp::create(builder, location, reordered, extended);
      }
    }
  }

  if (!assignmentType)
    return reordered;
  Type targetScalar = sim::getPackedScalarType(assignmentType);
  std::optional<unsigned> targetWidth = sim::getPackedWidth(assignmentType);
  if (!targetScalar || !targetWidth)
    return emitError(location)
               << "a fixed streaming source requires a fixed bit-stream "
                  "assignment target",
           failure();
  if (*targetWidth < totalWidth)
    return emitError(location)
               << "streaming assignment target is narrower than its source",
           failure();

  // 11.4.14 requires source streams to be left-aligned: widening adds zeros
  // on the right, unlike an ordinary integral assignment conversion.
  if (*targetWidth != totalWidth) {
    unsigned padding = *targetWidth - totalWidth;
    if (isa<sim::LogicType>(targetScalar)) {
      FailureOr<Value> logic = toLogic(reordered, location);
      if (failed(logic))
        return failure();
      Value zeros = sim::SimLogicConstantOp::create(
          builder, location,
          sim::LogicType::get(function.getContext(), padding),
          builder.getIntegerAttr(IntegerType::get(function.getContext(), padding),
                                 0),
          builder.getIntegerAttr(IntegerType::get(function.getContext(), padding),
                                 0));
      reordered = sim::SimLogicConcatOp::create(builder, location, targetScalar,
                                                 ValueRange{*logic, zeros});
    } else {
      auto integerTarget = cast<IntegerType>(targetScalar);
      FailureOr<Value> extended =
          convert(reordered, integerTarget, false, location);
      if (failed(extended))
        return failure();
      Value amount = arith::ConstantOp::create(
          builder, location, integerTarget,
          builder.getIntegerAttr(integerTarget, padding));
      reordered =
          arith::ShLIOp::create(builder, location, *extended, amount);
    }
  } else if (reordered.getType() != targetScalar) {
    FailureOr<Value> converted =
        convert(reordered, targetScalar, false, location);
    if (failed(converted))
      return failure();
    reordered = *converted;
  }
  return convert(reordered, assignmentType, false, location);
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

FailureOr<Value> UnitLowering::lowerVirtualInterfaceClock(
    semantic::SVMemberAccessExpressionOp op, Value interface) {
  Location location = getSemanticLocation(op);
  auto interfaceType = dyn_cast<sim::VirtualInterfaceType>(interface.getType());
  auto clockMember =
      op->getAttrOfType<StringAttr>("virtual_interface_clock_member");
  if (!interfaceType || !clockMember) {
    emitError(location) << "clocking variable has no static clock member";
    return failure();
  }
  if (auto required =
          op->getAttrOfType<StringAttr>("virtual_interface_modport")) {
    StringRef selected = interfaceType.getModport().getValue();
    if (selected.empty() || selected != required.getValue()) {
      emitError(location) << "clocking block requires modport '"
                          << required.getValue() << "' but the handle selects '"
                          << selected << "'";
      return failure();
    }
  }
  std::string key = (Twine(interfaceType.getInterfaceName().getValue()) + "\n" +
                     clockMember.getValue())
                        .str();
  VirtualMemberTargets *targets = nullptr;
  bool isNet = false;
  if (auto found = virtualInterfaceStorageMembers.find(key);
      found != virtualInterfaceStorageMembers.end())
    targets = &found->second;
  else if (auto found = virtualInterfaceNetMembers.find(key);
           found != virtualInterfaceNetMembers.end()) {
    targets = &found->second;
    isNet = true;
  }
  if (!targets || targets->empty()) {
    emitError(location) << "clocking variable clock '" << clockMember.getValue()
                        << "' has no elaborated descriptor";
    return failure();
  }
  llvm::sort(*targets);
  DenseMap<uint64_t, Value> &cache =
      isNet ? virtualInterfaceNetHandles : virtualInterfaceStorageHandles;
  DenseMap<uint64_t, Type> &types =
      isNet ? virtualInterfaceNetTypes : virtualInterfaceStorageTypes;
  SmallVector<Value> handles;
  for (auto [scopeID, descriptorID] : *targets) {
    (void)scopeID;
    Value handle = cache.lookup(descriptorID);
    if (!handle) {
      Type elementType = types.lookup(descriptorID);
      if (!elementType)
        return emitError(location) << "clocking event descriptor has no type",
               failure();
      Type handleType =
          isNet ? Type(sim::NetType::get(function.getContext(), elementType))
                : Type(sim::RefType::get(function.getContext(), elementType));
      OpBuilder entryBuilder(function.getContext());
      entryBuilder.setInsertionPointToStart(&function.getBody().front());
      Value context = function.getBody().front().getArgument(0);
      handle = isNet ? Value(sim::SimContextNetOp::create(
                           entryBuilder, location, handleType, context,
                           entryBuilder.getI64IntegerAttr(descriptorID)))
                     : Value(sim::SimContextStorageOp::create(
                           entryBuilder, location, handleType, context,
                           entryBuilder.getI64IntegerAttr(descriptorID)));
      cache[descriptorID] = handle;
    }
    handles.push_back(handle);
  }
  if (handles.empty())
    return failure();
  Type selectedType = handles.front().getType();
  for (Value handle : handles)
    if (handle.getType() != selectedType) {
      emitError(location)
          << "clocking event has inconsistent types across interface instances";
      return failure();
    }
  Value scope = sim::SimVirtualInterfaceScopeOp::create(
      builder, location, builder.getI64Type(), interface);
  Block *merge = addBlock();
  merge->addArgument(selectedType, location);
  for (auto [index, target] : llvm::enumerate(*targets)) {
    Block *matched = addBlock();
    Block *next = addBlock();
    Value expected =
        arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                  builder.getI64IntegerAttr(target.first));
    Value equal = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, scope, expected);
    cf::CondBranchOp::create(builder, location, equal, matched, ValueRange{},
                             next, ValueRange{});
    setCurrent(matched);
    cf::BranchOp::create(builder, location, merge, ValueRange{handles[index]});
    setCurrent(next);
  }
  if (failed(emitRuntimeFatal(
          location,
          "clocking variable access used a null or invalid interface handle.")))
    return failure();
  setCurrent(merge);
  return merge->getArgument(0);
}

FailureOr<Value>
UnitLowering::lowerClockingInputSample(Value source, uint64_t sourceDescriptor,
                                       Value clock, uint64_t clockDescriptor,
                                       sim::EdgeKind edge, bool oneStep,
                                       Location location) {
  Type sourceType = getReferenceElementType(source);
  if (auto net = dyn_cast<sim::NetType>(source.getType()))
    sourceType = net.getElementType();
  if (!sourceType)
    return failure();
  std::string key =
      (Twine("clocking-input|") + Twine(sourceDescriptor) + "|" +
       Twine(clockDescriptor) + "|" + Twine(static_cast<uint32_t>(edge)) + "|" +
       (oneStep ? "1step" : "zero"))
          .str();
  uint64_t siteID = stableCodeUnitID(key);
  if (!alternateClockSamplePlans.contains(key)) {
    alternateClockSamplePlans[key] = {siteID, 1, sourceType};
    MLIRContext *context = function.getContext();
    std::string symbol =
        (function.getSymName() + ".$clocking_input." + Twine(siteID)).str();
    auto parentHierarchy =
        function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName);
    StringRef parentName =
        parentHierarchy ? parentHierarchy.getValue() : function.getSymName();
    std::string hierarchy =
        (Twine(parentName) + ".$clocking_input." + Twine(siteID)).str();
    OpBuilder outlineBuilder(function);
    outlineBuilder.setInsertionPoint(function);
    SmallVector<Type> inputs{function.getArgumentTypes().front(),
                             source.getType(), clock.getType()};
    auto sourceKind = isa<sim::NetType>(source.getType())
                          ? sim::CaptureKind::Net
                          : sim::CaptureKind::Storage;
    auto clockKind = isa<sim::NetType>(clock.getType())
                         ? sim::CaptureKind::Net
                         : sim::CaptureKind::Storage;
    SmallVector<DictionaryAttr> argumentAttrs{
        captureMetadata(outlineBuilder, sim::CaptureKind::Context),
        captureMetadata(outlineBuilder, sourceKind, sourceDescriptor),
        captureMetadata(outlineBuilder, clockKind, clockDescriptor)};
    SmallVector<NamedAttribute> attributes{
        outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(
            "home_region",
            sim::EventRegionAttr::get(context, sim::EventRegion::Active)),
        outlineBuilder.getNamedAttr("domain",
                                    sim::ExecutionDomainAttr::get(
                                        context, sim::ExecutionDomain::Design)),
        outlineBuilder.getNamedAttr(
            "obelisk_sim.clocked_sample_plan",
            outlineBuilder.getDictionaryAttr(
                {outlineBuilder.getNamedAttr("key",
                                             outlineBuilder.getStringAttr(key)),
                 outlineBuilder.getNamedAttr(
                     "id", outlineBuilder.getI64IntegerAttr(siteID)),
                 outlineBuilder.getNamedAttr(
                     "hierarchy", outlineBuilder.getStringAttr(hierarchy))})),
        outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                    outlineBuilder.getStringAttr(hierarchy))};
    sim::SimFuncOp sampler = sim::SimFuncOp::create(
        outlineBuilder, location, symbol,
        FunctionType::get(context, inputs, TypeRange{}), sim::EntryKind::Always,
        attributes, argumentAttrs);
    SymbolTable::setSymbolVisibility(sampler, SymbolTable::Visibility::Private);
    Block &entry = sampler.getBody().front();
    Block *wait = new Block();
    Block *sample = new Block();
    sampler.getBody().push_back(wait);
    sampler.getBody().push_back(sample);
    OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
    cf::BranchOp::create(entryBuilder, location, wait);
    OpBuilder waitBuilder = OpBuilder::atBlockEnd(wait);
    auto suspend = sim::SimSuspendEdgeOp::create(
        waitBuilder, location, edge, entry.getArgument(2), ValueRange{},
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, sample);
    // Clocking inputs are latched in Observed. Clocking-block waiters resume
    // in Reactive, so both #1step and #0 sampling are complete first.
    suspend->setAttr("resume_region", sim::EventRegionAttr::get(
                                          context, sim::EventRegion::Observed));
    OpBuilder sampleBuilder = OpBuilder::atBlockEnd(sample);
    Value sampled;
    if (oneStep)
      sampled = sim::SimSampledReadOp::create(sampleBuilder, location,
                                              sourceType, entry.getArgument(0),
                                              entry.getArgument(1));
    else if (isa<sim::NetType>(source.getType()))
      sampled = sim::SimNetReadOp::create(sampleBuilder, location, sourceType,
                                          entry.getArgument(1));
    else
      sampled = sim::SimRefLoadOp::create(sampleBuilder, location, sourceType,
                                          entry.getArgument(1));
    Value gate = arith::ConstantOp::create(sampleBuilder, location,
                                           sampleBuilder.getI1Type(),
                                           sampleBuilder.getBoolAttr(true));
    sim::SimClockedSampleUpdateOp::create(
        sampleBuilder, location, entry.getArgument(0), sampled, gate,
        sampleBuilder.getI64IntegerAttr(siteID),
        sampleBuilder.getI64IntegerAttr(1));
    cf::BranchOp::create(sampleBuilder, location, wait);
    sampler->setAttr(sim::metadata::lowered, builder.getUnitAttr());
  }
  return sim::SimClockedSampleReadOp::create(
             builder, location, sourceType,
             function.getBody().front().getArgument(0),
             builder.getI64IntegerAttr(siteID), builder.getI64IntegerAttr(1),
             builder.getI64IntegerAttr(0))
      .getResult();
}

FailureOr<Value> UnitLowering::lowerVirtualInterfaceMember(
    semantic::SVMemberAccessExpressionOp op, Value interface, Type elementType,
    bool lvalue) {
  Location location = getSemanticLocation(op);
  auto interfaceType = dyn_cast<sim::VirtualInterfaceType>(interface.getType());
  StringAttr member = op->getAttrOfType<StringAttr>("member_name");
  if (!interfaceType || !member) {
    emitError(location) << "virtual interface member has no static identity";
    return failure();
  }
  if (auto required =
          op->getAttrOfType<StringAttr>("virtual_interface_modport")) {
    StringRef selected = interfaceType.getModport().getValue();
    if (selected.empty() || selected != required.getValue()) {
      emitError(location) << "virtual interface member requires modport '"
                          << required.getValue() << "' but the handle selects '"
                          << selected << "'";
      return failure();
    }
  }
  if (auto direction = op->getAttrOfType<semantic::SVArgumentDirectionAttr>(
          "virtual_interface_access_direction")) {
    if (lvalue && direction.getValue() == semantic::SVArgumentDirection::In) {
      emitError(location) << "cannot write an input virtual-interface member";
      return failure();
    }
    if (!lvalue && op->hasAttr("virtual_interface_clocking") &&
        direction.getValue() == semantic::SVArgumentDirection::Out) {
      emitError(location) << "cannot read an output clocking variable";
      return failure();
    }
  }
  std::string key = (Twine(interfaceType.getInterfaceName().getValue()) + "\n" +
                     member.getValue())
                        .str();
  VirtualMemberTargets *targets = nullptr;
  Type selectedType;
  bool isNet = false;
  if (auto found = virtualInterfaceStorageMembers.find(key);
      found != virtualInterfaceStorageMembers.end()) {
    targets = &found->second;
    selectedType = sim::RefType::get(function.getContext(), elementType);
  } else if (auto found = virtualInterfaceNetMembers.find(key);
             found != virtualInterfaceNetMembers.end()) {
    targets = &found->second;
    selectedType = sim::NetType::get(function.getContext(), elementType);
    isNet = true;
  }
  if (!targets || targets->empty()) {
    emitError(location) << "virtual interface member '" << member.getValue()
                        << "' has no elaborated descriptor";
    return failure();
  }
  llvm::sort(*targets);

  bool clockedRead = !lvalue && op->hasAttr("virtual_interface_clocking");
  if (clockedRead) {
    auto eventEdge = op->getAttrOfType<semantic::EdgeKindAttr>(
        "virtual_interface_clock_event_edge");
    if (!eventEdge || op->hasAttr("virtual_interface_clock_event_has_iff")) {
      emitError(location)
          << "clocking variable has no supported static clock event";
      return failure();
    }
    if (!op->hasAttr("virtual_interface_clock_input_skew_one_step")) {
      auto delay = op->getAttrOfType<StringAttr>(
          "virtual_interface_clock_input_skew_delay");
      if (!delay || delay.getValue() != "0") {
        emitError(location)
            << "clocking input skew currently requires #1step or #0";
        return failure();
      }
    }
  }

  SmallVector<Value> staticTargets;
  staticTargets.reserve(targets->size());
  DenseMap<uint64_t, Value> &handleCache =
      isNet ? virtualInterfaceNetHandles : virtualInterfaceStorageHandles;
  for (auto [scopeID, descriptorID] : *targets) {
    (void)scopeID;
    Value selected = handleCache.lookup(descriptorID);
    if (!selected) {
      OpBuilder entryBuilder(function.getContext());
      entryBuilder.setInsertionPointToStart(&function.getBody().front());
      Value context = function.getBody().front().getArgument(0);
      selected = isNet ? Value(sim::SimContextNetOp::create(
                             entryBuilder, location, selectedType, context,
                             entryBuilder.getI64IntegerAttr(descriptorID)))
                       : Value(sim::SimContextStorageOp::create(
                             entryBuilder, location, selectedType, context,
                             entryBuilder.getI64IntegerAttr(descriptorID)));
      handleCache[descriptorID] = selected;
    }
    staticTargets.push_back(selected);
    if (!lvalue)
      virtualInterfaceReadSensitivity.insert(selected);
    else if (!isNet)
      virtualInterfaceWrittenSensitivity.insert(selected);
  }

  SmallVector<Value> clockedSamples;
  if (clockedRead) {
    auto clockMember =
        op->getAttrOfType<StringAttr>("virtual_interface_clock_member");
    std::string clockKey = (Twine(interfaceType.getInterfaceName().getValue()) +
                            "\n" + clockMember.getValue())
                               .str();
    VirtualMemberTargets *clockTargets = nullptr;
    bool clockIsNet = false;
    if (auto found = virtualInterfaceStorageMembers.find(clockKey);
        found != virtualInterfaceStorageMembers.end())
      clockTargets = &found->second;
    else if (auto found = virtualInterfaceNetMembers.find(clockKey);
             found != virtualInterfaceNetMembers.end()) {
      clockTargets = &found->second;
      clockIsNet = true;
    }
    if (!clockTargets)
      return failure();
    llvm::sort(*clockTargets);
    bool oneStep = op->hasAttr("virtual_interface_clock_input_skew_one_step");
    auto edgeAttr = op->getAttrOfType<semantic::EdgeKindAttr>(
        "virtual_interface_clock_event_edge");
    for (auto [index, target] : llvm::enumerate(*targets)) {
      auto clockTarget = llvm::find_if(*clockTargets, [&](auto candidate) {
        return candidate.first == target.first;
      });
      if (clockTarget == clockTargets->end())
        return emitError(location)
                   << "clocking input source and clock instances do not match",
               failure();
      uint64_t clockDescriptor = clockTarget->second;
      DenseMap<uint64_t, Value> &clockCache =
          clockIsNet ? virtualInterfaceNetHandles
                     : virtualInterfaceStorageHandles;
      Value clockHandle = clockCache.lookup(clockDescriptor);
      if (!clockHandle) {
        DenseMap<uint64_t, Type> &clockTypes =
            clockIsNet ? virtualInterfaceNetTypes
                       : virtualInterfaceStorageTypes;
        Type clockElement = clockTypes.lookup(clockDescriptor);
        Type clockType =
            clockIsNet
                ? Type(sim::NetType::get(function.getContext(), clockElement))
                : Type(sim::RefType::get(function.getContext(), clockElement));
        OpBuilder entryBuilder(function.getContext());
        entryBuilder.setInsertionPointToStart(&function.getBody().front());
        Value context = function.getBody().front().getArgument(0);
        clockHandle =
            clockIsNet ? Value(sim::SimContextNetOp::create(
                             entryBuilder, location, clockType, context,
                             entryBuilder.getI64IntegerAttr(clockDescriptor)))
                       : Value(sim::SimContextStorageOp::create(
                             entryBuilder, location, clockType, context,
                             entryBuilder.getI64IntegerAttr(clockDescriptor)));
        clockCache[clockDescriptor] = clockHandle;
      }
      FailureOr<Value> sample = lowerClockingInputSample(
          staticTargets[index], target.second, clockHandle, clockDescriptor,
          static_cast<sim::EdgeKind>(edgeAttr.getValue()), oneStep, location);
      if (failed(sample))
        return failure();
      clockedSamples.push_back(*sample);
    }
  }

  Value scope = sim::SimVirtualInterfaceScopeOp::create(
      builder, location, builder.getI64Type(), interface);
  Block *merge = addBlock();
  merge->addArgument(clockedRead ? elementType : selectedType, location);
  for (auto [index, target] : llvm::enumerate(*targets)) {
    auto [scopeID, descriptorID] = target;
    (void)descriptorID;
    Block *matched = addBlock();
    Block *next = addBlock();
    Value expected =
        arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                  builder.getI64IntegerAttr(scopeID));
    Value equal = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, scope, expected);
    cf::CondBranchOp::create(builder, location, equal, matched, ValueRange{},
                             next, ValueRange{});
    setCurrent(matched);
    Value selected = clockedRead ? clockedSamples[index] : staticTargets[index];
    cf::BranchOp::create(builder, location, merge, ValueRange{selected});
    setCurrent(next);
  }
  if (failed(emitRuntimeFatal(
          location,
          "virtual interface member access used a null or invalid handle.")))
    return failure();
  setCurrent(merge);
  Value selected = merge->getArgument(0);
  if (clockedRead)
    return selected;
  if (lvalue)
    return selected;
  if (isNet)
    return sim::SimNetReadOp::create(builder, location, elementType, selected)
        .getResult();
  return sim::SimRefLoadOp::create(builder, location, elementType, selected)
      .getResult();
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
  if (op->hasAttr(staticClassPropertyAttrName)) {
    // SystemVerilog permits static properties to be selected through an
    // object expression. Evaluate that expression for its side effects, but
    // address the class-wide design storage rather than the object layout.
    if (failed(lowerExpression(children.front())))
      return failure();
    return lowerReferencedValue(op, op.getReferencedPath(), lvalue);
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
  FailureOr<Type> receiverType = getNormalizedSemanticType(children.front());
  if (succeeded(receiverType) &&
      isa<sim::VirtualInterfaceType>(*receiverType)) {
    FailureOr<Value> virtualInput = lowerExpression(children.front());
    if (failed(virtualInput))
      return failure();
    if (op->hasAttr("virtual_interface_clocking_block_event"))
      return lowerVirtualInterfaceClock(op, *virtualInput);
    FailureOr<Type> virtualResultType = getNormalizedSemanticType(op);
    if (failed(virtualResultType))
      return failure();
    return lowerVirtualInterfaceMember(op, *virtualInput, *virtualResultType,
                                       lvalue);
  }
  auto ordinalAttr = op->getAttrOfType<IntegerAttr>("field_ordinal");
  if (!ordinalAttr || ordinalAttr.getValue().isNegative() ||
      ordinalAttr.getValue().getActiveBits() > 32) {
    emitError(location) << "member access has no valid declaration ordinal";
    return failure();
  }
  unsigned ordinal = ordinalAttr.getValue().getZExtValue();
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  FailureOr<Value> input =
      lowerExpression(children.front(), lvalue || sampleAssertionValues);
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
    Value field = sim::SimRefSubelementOp::create(
        builder, location, selected, *input,
        builder.getDenseI64ArrayAttr({static_cast<int64_t>(ordinal)}));
    if (lvalue)
      return field;
    return loadReference(field, location);
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
    Value extracted = sim::SimUnionExtractOp::create(
        builder, location, *resultType, *input, ordinal);
    // An untagged union carries raw overlapping source bits. Validate a class
    // view before any field or direct-call operation can rely on its static
    // class type; unrelated live objects and arbitrary numeric words fail via
    // the ordinary checked-cast status path.
    if (!isTaggedUnionType(inputValueType) &&
        isa<sim::ClassHandleType>(*resultType))
      extracted = sim::SimClassCastOp::create(builder, location, *resultType,
                                              extracted);
    return extracted;
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
  auto structured =
      dyn_cast<semantic::SVStructuredAssignmentPatternExpressionOp>(op);
  if (auto array = dyn_cast<sim::AssocArrayType>(*resultType)) {
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
    uint64_t ordinal = 0;
    if (structured && structured.getMemberSetterCount() == 1) {
      auto ordinals = structured.getMemberSetterOrdinals();
      if (!ordinals || ordinals->size() != 1 ||
          structured.getTypeSetterCount() != 0 ||
          structured.getIndexSetterCount() != 0 ||
          structured.getHasDefaultSetter()) {
        unsupported(op) << " (union assignment-pattern setters)";
        return failure();
      }
      int64_t selected = ordinals->front();
      if (selected < 0 || static_cast<uint64_t>(selected) >=
                              sim::getAggregateNumElements(*resultType)) {
        emitError(location) << "union assignment-pattern member ordinal is "
                               "out of range";
        return failure();
      }
      ordinal = static_cast<uint64_t>(selected);
    }
    FailureOr<Value> value = lowerExpression(children.front());
    Type fieldType = sim::getAggregateElementType(*resultType, ordinal);
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
  uint64_t elementCount = sim::getAggregateNumElements(*resultType);
  if (structured && structured.getHasDefaultSetter()) {
    if (structured.getMemberSetterCount() != 0 ||
        structured.getTypeSetterCount() != 0 ||
        structured.getIndexSetterCount() != 0 || children.size() != 1) {
      unsupported(op) << " (mixed aggregate assignment-pattern setters)";
      return failure();
    }
    Operation *defaultNode = children.front();
    FailureOr<Value> value = lowerExpression(defaultNode);
    if (failed(value))
      return failure();
    SmallVector<Value> elements;
    elements.reserve(elementCount);
    for (uint64_t index = 0; index < elementCount; ++index) {
      FailureOr<Value> converted = convert(
          *value, sim::getAggregateElementType(*resultType, index),
          isSignedNode(defaultNode) ||
              isa<semantic::SVUnbasedUnsizedIntegerLiteralOp>(defaultNode),
          getSemanticLocation(defaultNode));
      if (failed(converted))
        return failure();
      elements.push_back(*converted);
    }
    return sim::SimAggregateConstructOp::create(builder, location, *resultType,
                                                elements)
        .getResult();
  }
  if (structured && structured.getMemberSetterCount() != 0) {
    auto ordinals = structured.getMemberSetterOrdinals();
    if (!ordinals || structured.getMemberSetterCount() != elementCount ||
        structured.getTypeSetterCount() != 0 ||
        structured.getIndexSetterCount() != 0 ||
        structured.getHasDefaultSetter() || children.size() != elementCount) {
      unsupported(op) << " (mixed aggregate assignment-pattern setters)";
      return failure();
    }
    SmallVector<Value> elements(elementCount);
    for (auto [childIndex, child] : llvm::enumerate(children)) {
      int64_t selected = (*ordinals)[childIndex];
      if (selected < 0 || static_cast<uint64_t>(selected) >= elementCount ||
          elements[selected]) {
        emitError(location)
            << "aggregate assignment-pattern member ordinal is invalid";
        return failure();
      }
      Type fieldType = sim::getAggregateElementType(*resultType, selected);
      FailureOr<Value> value = lowerExpression(child);
      if (failed(value))
        return failure();
      FailureOr<Value> converted =
          convert(*value, fieldType, isSignedNode(child), location);
      if (failed(converted))
        return failure();
      elements[selected] = *converted;
    }
    return sim::SimAggregateConstructOp::create(builder, location, *resultType,
                                                elements)
        .getResult();
  }
  if (structured) {
    unsupported(op) << " (aggregate assignment-pattern setters)";
    return failure();
  }
  if (isa<semantic::SVReplicatedAssignmentPatternExpressionOp>(op)) {
    if (children.size() < 2 || !getConstantSpelling(children.front())) {
      emitError(location)
          << "replicated assignment pattern requires a constant count";
      return failure();
    }
    FailureOr<Type> countType = getNormalizedSemanticType(children.front());
    std::optional<unsigned> countWidth =
        succeeded(countType) ? sim::getPackedWidth(*countType) : std::nullopt;
    FailureOr<ParsedConstant> count =
        countWidth ? parseSVInteger(*getConstantSpelling(children.front()),
                                    *countWidth, location)
                   : FailureOr<ParsedConstant>(failure());
    if (failed(count) || !count->unknown.isZero() ||
        count->value.getActiveBits() > 64) {
      emitError(location) << "invalid replicated assignment-pattern count";
      return failure();
    }
    uint64_t repetitions = count->value.getZExtValue();
    ArrayRef<Operation *> items = ArrayRef(children).drop_front();
    if (repetitions > elementCount || items.empty() ||
        repetitions != elementCount / items.size() ||
        elementCount % items.size() != 0) {
      emitError(location)
          << "replicated assignment-pattern inventory does not match target";
      return failure();
    }
    SmallVector<Value> itemValues;
    itemValues.reserve(items.size());
    for (Operation *item : items) {
      FailureOr<Value> value = lowerExpression(item);
      if (failed(value))
        return failure();
      itemValues.push_back(*value);
    }
    SmallVector<Value> elements;
    elements.reserve(elementCount);
    for (uint64_t repetition = 0; repetition < repetitions; ++repetition)
      for (auto [itemIndex, item] : llvm::enumerate(items)) {
        uint64_t elementIndex = elements.size();
        FailureOr<Value> converted = convert(
            itemValues[itemIndex],
            sim::getAggregateElementType(*resultType, elementIndex),
            isSignedNode(item), getSemanticLocation(item));
        if (failed(converted))
          return failure();
        elements.push_back(*converted);
      }
    return sim::SimAggregateConstructOp::create(builder, location, *resultType,
                                                elements)
        .getResult();
  }
  if (children.size() != elementCount) {
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

FailureOr<UnitLowering::PackedSelectionAddress>
UnitLowering::lowerPackedSelectionAddress(Operation *op, Type sourceValueType,
                                          Type resultType) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  bool element = isa<semantic::SVElementSelectExpressionOp>(op);
  if (children.size() != (element ? 2u : 3u))
    return failure();

  PackedSelectionAddress address;
  address.scalarResultType = sim::getPackedScalarType(resultType);
  if (!address.scalarResultType) {
    unsupported(op) << " (selection result type)";
    return failure();
  }
  std::optional<unsigned> resultWidth =
      sim::getPackedWidth(address.scalarResultType);
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
        bool baseNamesHighBit =
            (descending &&
             selectionKind == semantic::SVRangeSelectionKind::IndexedDown) ||
            (!descending &&
             selectionKind == semantic::SVRangeSelectionKind::IndexedUp);
        if (baseNamesHighBit && *resultWidth > elementWidth)
          *knownLow -= APInt(constantOffsetWidth, *resultWidth - elementWidth);
      }
    }
    bool inRange =
        knownLow && !knownLow->isNegative() && *resultWidth <= *sourceWidth &&
        knownLow->ule(
            APInt(constantOffsetWidth,
                  static_cast<uint64_t>(*sourceWidth - *resultWidth)));
    if (inRange) {
      address.constant = true;
      address.lowBit = knownLow->getZExtValue();
    } else if (knownLow) {
      FailureOr<Type> arithmeticType = getIndexArithmeticType(*firstIndexType);
      if (failed(arithmeticType))
        return failure();
      address.dynamicLow = createKnownIndex(*arithmeticType, *knownLow);
    }
  }
  if (!address.constant && !address.dynamicLow) {
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
    address.dynamicLow = *widened;
    unsigned arithmeticWidth =
        *sim::getPackedWidth(address.dynamicLow.getType());
    auto createKnownOffset = [&](int64_t value) -> Value {
      return createKnownIndex(
          address.dynamicLow.getType(),
          APInt(arithmeticWidth, static_cast<uint64_t>(value), true));
    };
    if (sourceRight != 0 || !descending) {
      Value boundary = createKnownOffset(sourceRight);
      address.dynamicLow = descending
                               ? subtract(address.dynamicLow, boundary)
                               : subtract(boundary, address.dynamicLow);
    }
    if (elementWidth > 1) {
      Value scale = createKnownOffset(elementWidth);
      address.dynamicLow = multiply(address.dynamicLow, scale);
    }
    bool baseNamesHighBit =
        (descending &&
         selectionKind == semantic::SVRangeSelectionKind::IndexedDown) ||
        (!descending &&
         selectionKind == semantic::SVRangeSelectionKind::IndexedUp);
    if (baseNamesHighBit && *resultWidth > elementWidth) {
      Value adjustment = createKnownOffset(*resultWidth - elementWidth);
      address.dynamicLow = subtract(address.dynamicLow, adjustment);
    }
  }
  return address;
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
  FailureOr<Value> input =
      lowerExpression(children.front(), lvalue || sampleAssertionValues);
  if (failed(resultType) || failed(input))
    return failure();

  Type sourceValueType = (*input).getType();
  if (Type elementType = getReferenceElementType(*input)) {
    sourceValueType = elementType;
    // Ordinary simulation references have first-class subreference
    // operations below. Managed, formal, and escaping references do not;
    // read their aggregate value before applying an rvalue selection.
    if (!lvalue && !isa<sim::RefType>((*input).getType())) {
      input = loadReference(*input, location);
      if (failed(input))
        return failure();
    }
  } else if (auto net = dyn_cast<sim::NetType>(sourceValueType))
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
      if (isa<sim::RefType>((*input).getType())) {
        Value selected = sim::SimRefSubelementOp::create(
            builder, location,
            sim::RefType::get(function.getContext(), *resultType), *input,
            builder.getDenseI64ArrayAttr({static_cast<int64_t>(*ordinal)}));
        if (lvalue)
          return selected;
        return loadReference(selected, location);
      }
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
    if (isa<sim::RefType>((*input).getType())) {
      Value selected = sim::SimRefArrayElementOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), *resultType), *input,
          *widened);
      if (lvalue)
        return selected;
      return loadReference(selected, location);
    }
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

  FailureOr<PackedSelectionAddress> address =
      lowerPackedSelectionAddress(op, sourceValueType, *resultType);
  if (failed(address))
    return failure();
  Type scalarResultType = address->scalarResultType;
  bool constant = address->constant;
  uint64_t lowBit = address->lowBit;
  Value dynamicLow = address->dynamicLow;

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
