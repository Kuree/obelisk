//===- LowerUnitLValues.cpp - Lower assignments and port lvalues ------===//

#include "LowerUnit.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

static bool containsSequentialContainer(Type type) {
  if (isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(type))
    return true;
  if (auto array = dyn_cast<sim::UnpackedArrayType>(type))
    return containsSequentialContainer(array.getElementType());
  if (isa<sim::UnpackedStructType>(type))
    for (unsigned index = 0, count = sim::getAggregateNumElements(type);
         index != count; ++index)
      if (containsSequentialContainer(
              sim::getAggregateElementType(type, index)))
        return true;
  return false;
}

static bool isUserNetDriver(Value value) {
  while (value) {
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      auto function =
          dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
      return function &&
             static_cast<bool>(function.getArgAttr(
                 argument.getArgNumber(), "obelisk_sim.user_net_driver"));
    }
    Operation *definition = value.getDefiningOp();
    if (auto extract = dyn_cast_or_null<sim::SimDriverExtractOp>(definition))
      value = extract.getInput();
    else if (auto extract =
                 dyn_cast_or_null<sim::SimDriverDynExtractOp>(definition))
      value = extract.getInput();
    else if (auto subelement =
                 dyn_cast_or_null<sim::SimDriverSubelementOp>(definition))
      value = subelement.getInput();
    else if (auto element =
                 dyn_cast_or_null<sim::SimDriverArrayElementOp>(definition))
      value = element.getInput();
    else
      return false;
  }
  return false;
}
namespace {

constexpr StringLiteral continuousStoreAttrName =
    "obelisk_sim.continuous_store";

Operation *getSingleRegionRoot(Region &region) {
  if (region.empty() || region.front().empty())
    return nullptr;
  return &region.front().front();
}

bool drivesDelayedNet(sim::SimFuncOp function, Value driver) {
  while (driver) {
    Operation *definition = driver.getDefiningOp();
    if (auto extract = dyn_cast_or_null<sim::SimDriverExtractOp>(definition)) {
      driver = extract.getInput();
      continue;
    }
    if (auto extract =
            dyn_cast_or_null<sim::SimDriverDynExtractOp>(definition)) {
      driver = extract.getInput();
      continue;
    }
    if (auto subelement =
            dyn_cast_or_null<sim::SimDriverSubelementOp>(definition)) {
      driver = subelement.getInput();
      continue;
    }
    if (auto element =
            dyn_cast_or_null<sim::SimDriverArrayElementOp>(definition)) {
      driver = element.getInput();
      continue;
    }
    auto argument = dyn_cast<BlockArgument>(driver);
    return argument && static_cast<bool>(function.getArgAttrOfType<UnitAttr>(
                           argument.getArgNumber(), "obelisk_sim.delayed_net"));
  }
  return false;
}

/// Whether a dynamic container's element sits under this designator. IEEE
/// 1800-2017 7.5, 7.8, and 7.10 give queues, associative arrays, and dynamic
/// arrays storage the container itself owns, which it hands out only by value,
/// so naming a part of one needs a write that rebuilds the whole element.
bool isContainerElementSubvalue(Operation *expression) {
  SmallVector<Operation *> children = getChildren(expression);
  if (isa<semantic::SVElementSelectExpressionOp>(expression) &&
      children.size() == 2) {
    FailureOr<Type> baseType = getNormalizedSemanticType(children.front());
    if (succeeded(baseType) &&
        isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
            *baseType))
      return true;
    return isContainerElementSubvalue(children.front());
  }
  if (isa<semantic::SVMemberAccessExpressionOp>(expression) &&
      children.size() == 1)
    return isContainerElementSubvalue(children.front());
  return false;
}

/// Whether a class property sits under this designator. IEEE 1800-2017 8.3
/// makes a property a variable of the object, so a part of one is as
/// assignable as a part of any other variable (7.2 for a structure member,
/// 7.4.6 for an array element). The object's storage is managed and exposes no
/// interior reference of its own, so the write rebuilds the whole property the
/// way a container element's does.
bool isClassPropertySubvalue(Operation *expression) {
  SmallVector<Operation *> children = getChildren(expression);
  // 8.11 lets a method of the owning class name a property without `this`,
  // which the frontend spells as a plain reference to the property rather
  // than as a member access. The storage it names is the same.
  if (isa<semantic::SVNamedValueExpressionOp>(expression))
    return expression->hasAttr("obelisk_sim.class_field");
  if (isa<semantic::SVMemberAccessExpressionOp>(expression) &&
      children.size() == 1) {
    if (expression->hasAttr("obelisk_sim.class_field"))
      return true;
    return isClassPropertySubvalue(children.front());
  }
  if (isa<semantic::SVElementSelectExpressionOp,
          semantic::SVRangeSelectExpressionOp>(expression) &&
      !children.empty())
    return isClassPropertySubvalue(children.front());
  return false;
}

/// Peel the selects that SystemVerilog permits on the destination of a
/// clocking output drive and return the clocking-variable access at their
/// root. Other lvalue shapes are not clocking drives.
Operation *findClockingOutputRoot(Operation *destination) {
  while (isa<semantic::SVElementSelectExpressionOp,
             semantic::SVRangeSelectExpressionOp>(destination)) {
    SmallVector<Operation *> children = getChildren(destination);
    if (children.empty())
      return nullptr;
    destination = children.front();
  }
  if (destination->hasAttr(clockingVariableAttrName))
    return destination;
  auto member = dyn_cast<semantic::SVMemberAccessExpressionOp>(destination);
  return member && member->hasAttr("virtual_interface_clocking") ? destination
                                                                 : nullptr;
}

} // namespace

FailureOr<UnitLowering::CapturedLValue>
UnitLowering::captureLValue(Operation *destination, Location location) {
  CapturedLValue captured;
  captured.semanticNode = destination;
  FailureOr<Type> destinationType = getNormalizedSemanticType(destination);
  if (failed(destinationType))
    return failure();
  captured.type = *destinationType;

  if (isa<semantic::SVConcatenationExpressionOp>(destination)) {
    SmallVector<Operation *> children = getChildren(destination);
    if (children.empty())
      return failure();
    captured.kind = CapturedLValue::Kind::Concatenation;
    for (Operation *child : children) {
      FailureOr<CapturedLValue> element = captureLValue(child, location);
      if (failed(element))
        return failure();
      captured.children.push_back(std::move(*element));
    }
    return captured;
  }

  if (auto range = dyn_cast<semantic::SVRangeSelectExpressionOp>(destination)) {
    SmallVector<Operation *> selection = getChildren(destination);
    FailureOr<Type> baseType =
        selection.empty() ? FailureOr<Type>(failure())
                          : getNormalizedSemanticType(selection.front());
    auto sourceArray = succeeded(baseType)
                           ? dyn_cast<sim::UnpackedArrayType>(*baseType)
                           : sim::UnpackedArrayType{};
    auto resultArray = dyn_cast<sim::UnpackedArrayType>(*destinationType);
    auto sourceQueue = succeeded(baseType) ? dyn_cast<sim::QueueType>(*baseType)
                                           : sim::QueueType{};
    auto resultQueue = dyn_cast<sim::QueueType>(*destinationType);
    if (selection.size() == 3 && sourceQueue && resultQueue) {
      if (range.getSelectionKind() != semantic::SVRangeSelectionKind::Simple) {
        emitError(location) << "queue slices require a simple range";
        return failure();
      }
      FailureOr<Value> base = lowerExpression(selection.front(), true);
      if (failed(base) || getReferenceElementType(*base) != sourceQueue) {
        emitError(location)
            << "queue slice destination has no owning reference";
        return failure();
      }
      FailureOr<Value> queue = loadReference(*base, location);
      FailureOr<QueueSliceAddress> address =
          succeeded(queue)
              ? lowerQueueSliceAddress(range, ArrayRef(selection).drop_front(),
                                       *queue, location)
              : FailureOr<QueueSliceAddress>(failure());
      if (failed(address))
        return failure();
      CapturedLValue baseCapture;
      baseCapture.semanticNode = selection.front();
      baseCapture.type = sourceQueue;
      baseCapture.reference = *base;
      captured.kind = CapturedLValue::Kind::ContainerSlice;
      captured.container = *queue;
      captured.index = address->start;
      captured.limit = address->count;
      captured.children.push_back(std::move(baseCapture));
      return captured;
    }
    if (selection.size() == 3 && sourceArray && resultArray) {
      FailureOr<Value> base = lowerExpression(selection.front(), true);
      if (failed(base))
        return failure();
      bool reference = isa<sim::RefType>((*base).getType());
      bool driver = isa<sim::DriverType>((*base).getType());
      unsigned count = sim::getAggregateNumElements(resultArray);
      FailureOr<SmallVector<Value>> indices =
          unpackedSliceIndices(range, ArrayRef(selection).drop_front(),
                               sourceArray, count, location);
      if (failed(indices))
        return failure();

      captured.kind = CapturedLValue::Kind::AggregateSlice;
      if (!reference && !driver) {
        if (getReferenceElementType(*base) != sourceArray) {
          emitError(location)
              << "unpacked array slice destination has no owning reference";
          return failure();
        }
        CapturedLValue baseCapture;
        baseCapture.semanticNode = selection.front();
        baseCapture.type = sourceArray;
        baseCapture.reference = *base;
        captured.indices = std::move(*indices);
        captured.children.push_back(std::move(baseCapture));
        return captured;
      }
      captured.children.reserve(count);
      for (unsigned ordinal = 0; ordinal < count; ++ordinal) {
        Value index = (*indices)[ordinal];
        Type elementType = sim::getAggregateElementType(resultArray, ordinal);
        CapturedLValue element;
        element.semanticNode = destination;
        element.type = elementType;
        if (reference)
          element.reference = sim::SimRefArrayElementOp::create(
              builder, location,
              sim::RefType::get(function.getContext(), elementType), *base,
              index);
        else
          element.reference = sim::SimDriverArrayElementOp::create(
              builder, location,
              sim::DriverType::get(function.getContext(), elementType), *base,
              index);
        captured.children.push_back(std::move(element));
      }
      return captured;
    }
  }

  if (isa<semantic::SVElementSelectExpressionOp>(destination)) {
    SmallVector<Operation *> selection = getChildren(destination);
    if (selection.size() == 2) {
      FailureOr<Type> baseType = getNormalizedSemanticType(selection.front());
      if (failed(baseType))
        return failure();
      if (isa<sim::DynamicArrayType, sim::QueueType>(*baseType)) {
        FailureOr<CapturedLValue> base =
            captureLValue(selection.front(), location);
        FailureOr<Value> container = succeeded(base)
                                         ? loadCapturedLValue(*base, location)
                                         : FailureOr<Value>(failure());
        FailureOr<Value> index = lowerExpression(selection[1]);
        if (failed(base) || failed(container) || failed(index))
          return failure();
        FailureOr<Value> index64 =
            toContainerIndex(*index, isSignedNode(selection[1]), location);
        if (failed(index64))
          return failure();
        captured.kind = CapturedLValue::Kind::ContainerElement;
        captured.container = *container;
        captured.index = *index64;
        captured.children.push_back(std::move(*base));
        return captured;
      }
      if (auto array = dyn_cast<sim::AssocArrayType>(*baseType)) {
        FailureOr<CapturedLValue> base =
            captureLValue(selection.front(), location);
        FailureOr<Value> container = succeeded(base)
                                         ? loadCapturedLValue(*base, location)
                                         : FailureOr<Value>(failure());
        FailureOr<Value> key = lowerExpression(selection[1]);
        if (failed(base) || failed(container) || failed(key))
          return failure();
        FailureOr<Value> convertedKey =
            convert(*key, array.getKeyType(), isSignedNode(selection[1]),
                    location, array.getSignedKey());
        if (failed(convertedKey))
          return failure();
        captured.kind = CapturedLValue::Kind::AssociativeElement;
        captured.container = *container;
        captured.index = *convertedKey;
        captured.children.push_back(std::move(*base));
        return captured;
      }
      if (isa<sim::StringType>(*baseType)) {
        FailureOr<Value> base = lowerExpression(selection.front(), true);
        FailureOr<Value> index = lowerExpression(selection[1]);
        if (failed(base) || failed(index) ||
            !isa<sim::StringType>(getReferenceElementType(*base)))
          return failure();
        FailureOr<Value> index64 = convert(
            *index, builder.getI64Type(), isSignedNode(selection[1]), location);
        if (failed(index64))
          return failure();
        captured.kind = CapturedLValue::Kind::StringCharacter;
        captured.reference = *base;
        captured.index = *index64;
        return captured;
      }

      if (isa<sim::PackedArrayType, sim::UnpackedArrayType>(*baseType) &&
          getConstantSpelling(selection[1]).has_value() &&
          (isContainerElementSubvalue(selection.front()) ||
           isClassPropertySubvalue(selection.front()))) {
        FailureOr<Type> indexType = getNormalizedSemanticType(selection[1]);
        std::optional<unsigned> indexWidth =
            succeeded(indexType) ? sim::getPackedWidth(*indexType)
                                 : std::nullopt;
        if (failed(indexType) || !indexWidth)
          return failure();
        FailureOr<ParsedConstant> parsed = parseSVInteger(
            *getConstantSpelling(selection[1]), *indexWidth, location);
        if (failed(parsed) || !parsed->unknown.isZero())
          return failure();
        APInt index = isSignedNode(selection[1])
                          ? parsed->value.sextOrTrunc(65)
                          : parsed->value.zextOrTrunc(65);
        if (!index.isSignedIntN(64))
          return failure();
        std::optional<unsigned> ordinal =
            sim::getArrayElementOrdinal(*baseType, index.getSExtValue());
        if (!ordinal)
          return failure();
        FailureOr<CapturedLValue> base =
            captureLValue(selection.front(), location);
        if (failed(base))
          return failure();
        captured.kind = CapturedLValue::Kind::AggregateElement;
        captured.ordinal = *ordinal;
        captured.children.push_back(std::move(*base));
        return captured;
      }

      // IEEE 1800-2017 7.4.6 makes one element of an unpacked array an
      // assignment target whether or not the index is constant, and 8.3 makes
      // a class property a variable of the object. Managed storage exposes no
      // stable interior reference, so a variable index reads the whole array,
      // replaces the addressed element, and stores the array back -- the same
      // treatment the constant index above gets.
      if (isa<sim::UnpackedArrayType>(*baseType) &&
          !getConstantSpelling(selection[1]).has_value() &&
          sim::getAggregateElementType(*baseType, 0) == *destinationType &&
          (isContainerElementSubvalue(selection.front()) ||
           isClassPropertySubvalue(selection.front()))) {
        FailureOr<CapturedLValue> base =
            captureLValue(selection.front(), location);
        FailureOr<Value> index = lowerExpression(selection[1]);
        if (failed(base) || failed(index))
          return failure();
        FailureOr<Value> normalized =
            toArrayIndex(*index, isSignedNode(selection[1]), location);
        if (failed(normalized))
          return failure();
        captured.kind = CapturedLValue::Kind::AggregateDynamicElement;
        captured.index = *normalized;
        captured.children.push_back(std::move(*base));
        return captured;
      }
    }
  }

  SmallVector<Operation *> memberChildren;
  bool virtualInterfaceMember = false;
  if (isa<semantic::SVMemberAccessExpressionOp>(destination)) {
    memberChildren = getChildren(destination);
    if (memberChildren.size() == 1) {
      FailureOr<Type> receiverType =
          getNormalizedSemanticType(memberChildren.front());
      virtualInterfaceMember = succeeded(receiverType) &&
                               isa<sim::VirtualInterfaceType>(*receiverType);
    }
  }
  if (isa<semantic::SVMemberAccessExpressionOp>(destination) &&
      !destination->hasAttr("obelisk_sim.class_field") &&
      !destination->hasAttr(staticClassPropertyAttrName) &&
      !virtualInterfaceMember) {
    ArrayRef<Operation *> members = memberChildren;
    auto ordinalAttr = destination->getAttrOfType<IntegerAttr>("field_ordinal");
    if (members.size() != 1 || !ordinalAttr ||
        ordinalAttr.getValue().isNegative() ||
        ordinalAttr.getValue().getActiveBits() > 32)
      return failure();
    FailureOr<Type> memberBaseType = getNormalizedSemanticType(members.front());
    if (failed(memberBaseType))
      return failure();
    auto unpackedUnion = dyn_cast<sim::UnpackedUnionType>(*memberBaseType);
    Value frozenReceiver;
    if (auto named =
            dyn_cast<semantic::SVNamedValueExpressionOp>(members.front())) {
      if (auto node = named->getAttrOfType<IntegerAttr>("node_id"))
        frozenReceiver = nodeLvalues.lookup(node.getValue().getZExtValue());
      if (!frozenReceiver)
        frozenReceiver = lvalues.lookup(named.getReferencedPath());
    }
    bool indirectReference =
        frozenReceiver &&
        isa<sim::ArgumentRefType, sim::ManagedRefType, sim::ReferencePathType>(
            frozenReceiver.getType());
    if (isContainerElementSubvalue(destination) ||
        isClassPropertySubvalue(destination) ||
        sim::isManagedHandleType(*destinationType) || indirectReference ||
        (unpackedUnion && !unpackedUnion.getIsTagged())) {
      FailureOr<CapturedLValue> base = captureLValue(members.front(), location);
      if (failed(base))
        return failure();
      Type baseType = base->type;
      unsigned ordinal = ordinalAttr.getValue().getZExtValue();
      if (isa<sim::PackedUnionType>(baseType) ||
          (unpackedUnion && unpackedUnion.getIsTagged()) ||
          sim::getAggregateElementType(baseType, ordinal) != *destinationType)
        return failure();
      captured.kind = CapturedLValue::Kind::AggregateElement;
      captured.ordinal = ordinal;
      captured.children.push_back(std::move(*base));
      return captured;
    }
  }

  // Managed fields, ref formals, and escaping container paths cannot expose a
  // stable interior reference across a safepoint. Capture their packed parent
  // and rebuild it on a blocking write. Ordinary references and drivers keep
  // using the first-class subreference operations in lowerSelection.
  if (isa<semantic::SVElementSelectExpressionOp,
          semantic::SVRangeSelectExpressionOp>(destination)) {
    SmallVector<Operation *> selection = getChildren(destination);
    if (!selection.empty()) {
      FailureOr<Type> baseType = getNormalizedSemanticType(selection.front());
      if (succeeded(baseType) && sim::getPackedWidth(*baseType) &&
          sim::getPackedWidth(*destinationType)) {
        FailureOr<CapturedLValue> base =
            captureLValue(selection.front(), location);
        if (failed(base))
          return failure();
        // A select wider than the storage it names reaches outside it, so no
        // subreference describes it. IEEE 1800-2017 11.5.1 keeps only the bits
        // that are in range, which the read-modify-write path below does by
        // discarding the padding it wrote through.
        bool reachesOutsideStorage = *sim::getPackedWidth(*destinationType) >
                                     *sim::getPackedWidth(*baseType);
        bool hasDirectView =
            base->kind == CapturedLValue::Kind::Reference &&
            isa<sim::RefType, sim::DriverType>(base->reference.getType()) &&
            !(reachesOutsideStorage &&
              isa<sim::RefType>(base->reference.getType()));
        if (hasDirectView) {
          Operation *root = base->semanticNode;
          auto nodeID = root ? root->getAttrOfType<IntegerAttr>("node_id")
                             : IntegerAttr{};
          auto plan =
              nodeID
                  ? timingPathMaskedPlans.find(nodeID.getValue().getZExtValue())
                  : timingPathMaskedPlans.end();
          if (plan != timingPathMaskedPlans.end() &&
              plan->second.proceduralStorage)
            hasDirectView = false;
        }
        if (!hasDirectView) {
          FailureOr<PackedSelectionAddress> address =
              lowerPackedSelectionAddress(destination, *baseType,
                                          *destinationType);
          if (failed(address))
            return failure();
          captured.kind = CapturedLValue::Kind::PackedValueSlice;
          captured.lowBit = address->lowBit;
          captured.index = address->dynamicLow;
          captured.padding = address->padding;
          captured.children.push_back(std::move(*base));
          return captured;
        }
      }
    }
  }

  FailureOr<Value> reference = lowerExpression(destination, true);
  if (failed(reference))
    return failure();
  Type elementType = getReferenceElementType(*reference);
  if (!elementType) {
    if (auto driver = dyn_cast<sim::DriverType>((*reference).getType()))
      elementType = driver.getElementType();
  }
  if (!elementType) {
    emitError(location)
        << "assignment destination is not a reference or driver";
    return failure();
  }
  captured.reference = *reference;
  captured.type = elementType;
  if (auto slice = (*reference).getDefiningOp<sim::SimRefDynExtractOp>()) {
    captured.kind = CapturedLValue::Kind::PackedDynamicSlice;
    captured.reference = slice.getInput();
    captured.index = slice.getLowBit();
  }
  return captured;
}

FailureOr<Value>
UnitLowering::loadCapturedLValue(const CapturedLValue &destination,
                                 Location location) {
  switch (destination.kind) {
  case CapturedLValue::Kind::Reference:
    return loadReference(destination.reference, location);
  case CapturedLValue::Kind::PackedDynamicSlice: {
    Type selected = sim::RefType::get(function.getContext(), destination.type);
    Value reference = sim::SimRefDynExtractOp::create(
        builder, location, selected, destination.reference, destination.index);
    return loadReference(reference, location);
  }
  case CapturedLValue::Kind::PackedValueSlice: {
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> base =
        loadCapturedLValue(destination.children.front(), location);
    FailureOr<Value> scalar = succeeded(base) ? toPackedScalar(*base, location)
                                              : FailureOr<Value>(failure());
    Type resultScalarType = sim::getPackedScalarType(destination.type);
    if (failed(scalar) || !resultScalarType)
      return failure();
    if (destination.padding) {
      FailureOr<Value> padded =
          padSelectionWindow(*scalar, destination.padding, location);
      if (failed(padded))
        return failure();
      scalar = *padded;
    }
    Value selected;
    if (destination.index) {
      if (isa<sim::LogicType>((*scalar).getType()))
        selected = sim::SimLogicDynExtractOp::create(
            builder, location, resultScalarType, *scalar, destination.index);
      else
        selected = sim::SimBitsDynExtractOp::create(
            builder, location, resultScalarType, *scalar, destination.index);
    } else if (isa<sim::LogicType>((*scalar).getType())) {
      selected = sim::SimLogicExtractOp::create(
          builder, location, resultScalarType, *scalar,
          builder.getI64IntegerAttr(destination.lowBit));
    } else {
      auto inputType = cast<IntegerType>((*scalar).getType());
      Value shifted = *scalar;
      if (destination.lowBit != 0) {
        Value amount = arith::ConstantOp::create(
            builder, location, inputType,
            builder.getIntegerAttr(inputType, destination.lowBit));
        shifted = arith::ShRUIOp::create(builder, location, shifted, amount);
      }
      auto resultType = cast<IntegerType>(resultScalarType);
      selected = resultType == inputType
                     ? shifted
                     : Value(arith::TruncIOp::create(builder, location,
                                                     resultType, shifted));
    }
    return convert(selected, destination.type, false, location,
                   isSignedNode(destination.semanticNode));
  }
  case CapturedLValue::Kind::ContainerElement: {
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> container =
        loadCapturedLValue(destination.children.front(), location);
    if (failed(container))
      return failure();
    return sim::SimContainerReadOp::create(builder, location, destination.type,
                                           *container, destination.index)
        .getResult();
  }
  case CapturedLValue::Kind::ContainerSlice: {
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> queue =
        loadCapturedLValue(destination.children.front(), location);
    auto resultQueue = dyn_cast<sim::QueueType>(destination.type);
    if (failed(queue) || !resultQueue)
      return failure();
    FailureOr<ContainerElementDescriptor> descriptor =
        describeContainerElement(resultQueue.getElementType(), location);
    if (failed(descriptor))
      return failure();
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    Value one = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(1));
    uint64_t bound =
        resultQueue.getBound() ? resultQueue.getBound() : UINT64_MAX;
    Value result = sim::SimContainerCreateOp::create(
        builder, location, destination.type, zero, descriptor->typeID,
        descriptor->kind, descriptor->flags, descriptor->valueSize,
        descriptor->alignment, descriptor->bitWidth,
        builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(descriptor->traceKinds),
        OBELISK_RT_CONTAINER_QUEUE, bound);
    Block *header = addBlock();
    header->addArgument(builder.getI64Type(), location);
    Block *body = addBlock();
    Block *done = addBlock();
    cf::BranchOp::create(builder, location, header, ValueRange{zero});
    setCurrent(header);
    Value ordinal = header->getArgument(0);
    Value more =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                              ordinal, destination.limit);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, done,
                             ValueRange{});
    setCurrent(body);
    Value index =
        arith::AddIOp::create(builder, location, destination.index, ordinal);
    Value element = sim::SimContainerReadOp::create(
        builder, location, resultQueue.getElementType(), *queue, index);
    sim::SimContainerWriteOp::create(builder, location, result, ordinal,
                                     element);
    Value next = arith::AddIOp::create(builder, location, ordinal, one);
    cf::BranchOp::create(builder, location, header, ValueRange{next});
    setCurrent(done);
    return result;
  }
  case CapturedLValue::Kind::AssociativeElement: {
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> container =
        loadCapturedLValue(destination.children.front(), location);
    if (failed(container))
      return failure();
    Value isNull = sim::SimManagedIsNullOp::create(
        builder, location, builder.getI1Type(), *container);
    Block *missing = addBlock();
    Block *present = addBlock();
    Block *resume = addBlock();
    resume->addArgument(destination.type, location);
    cf::CondBranchOp::create(builder, location, isNull, missing, ValueRange{},
                             present, ValueRange{});
    setCurrent(missing);
    Value defaultValue =
        createDefaultValue(builder, location, destination.type);
    if (!defaultValue)
      return failure();
    cf::BranchOp::create(builder, location, resume, ValueRange{defaultValue});
    setCurrent(present);
    Value value = sim::SimAssocReadOp::create(
        builder, location, destination.type, *container, destination.index);
    cf::BranchOp::create(builder, location, resume, ValueRange{value});
    setCurrent(resume);
    return resume->getArgument(0);
  }
  case CapturedLValue::Kind::AggregateElement: {
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> aggregate =
        loadCapturedLValue(destination.children.front(), location);
    if (failed(aggregate) ||
        sim::getAggregateElementType((*aggregate).getType(),
                                     destination.ordinal) != destination.type)
      return failure();
    if (isa<sim::UnpackedUnionType>((*aggregate).getType())) {
      Value extracted = sim::SimUnionExtractOp::create(
          builder, location, destination.type, *aggregate, destination.ordinal);
      if (isa<sim::ClassHandleType>(destination.type))
        extracted = sim::SimClassCastOp::create(builder, location,
                                                destination.type, extracted);
      return extracted;
    }
    return sim::SimAggregateExtractOp::create(builder, location,
                                              destination.type, *aggregate,
                                              destination.ordinal)
        .getResult();
  }
  case CapturedLValue::Kind::AggregateDynamicElement: {
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> aggregate =
        loadCapturedLValue(destination.children.front(), location);
    if (failed(aggregate) || sim::getAggregateElementType(
                                 (*aggregate).getType(), 0) != destination.type)
      return failure();
    return sim::SimArrayDynExtractOp::create(builder, location,
                                             destination.type, *aggregate,
                                             destination.index)
        .getResult();
  }
  case CapturedLValue::Kind::AggregateSlice: {
    if (!destination.indices.empty()) {
      if (destination.children.size() != 1 ||
          destination.indices.size() !=
              sim::getAggregateNumElements(destination.type))
        return failure();
      FailureOr<Value> aggregate =
          loadCapturedLValue(destination.children.front(), location);
      auto source =
          succeeded(aggregate)
              ? dyn_cast<sim::UnpackedArrayType>((*aggregate).getType())
              : sim::UnpackedArrayType{};
      if (!source)
        return failure();
      SmallVector<Value> elements;
      elements.reserve(destination.indices.size());
      for (auto [ordinal, index] : llvm::enumerate(destination.indices)) {
        Value element = sim::SimArrayDynExtractOp::create(
            builder, location, source.getElementType(), *aggregate, index);
        FailureOr<Value> converted = convert(
            element, sim::getAggregateElementType(destination.type, ordinal),
            false, location);
        if (failed(converted))
          return failure();
        elements.push_back(*converted);
      }
      return sim::SimAggregateConstructOp::create(builder, location,
                                                  destination.type, elements)
          .getResult();
    }
    SmallVector<Value> elements;
    elements.reserve(destination.children.size());
    for (const CapturedLValue &child : destination.children) {
      FailureOr<Value> element = loadCapturedLValue(child, location);
      if (failed(element))
        return failure();
      elements.push_back(*element);
    }
    return sim::SimAggregateConstructOp::create(builder, location,
                                                destination.type, elements)
        .getResult();
  }
  case CapturedLValue::Kind::StringCharacter: {
    if (!destination.reference)
      return failure();
    FailureOr<Value> string = loadReference(destination.reference, location);
    if (failed(string))
      return failure();
    Value character = sim::SimStringGetcOp::create(
        builder, location, builder.getI8Type(), *string, destination.index);
    return convert(character, destination.type, false, location,
                   isSignedNode(destination.semanticNode));
  }
  case CapturedLValue::Kind::Concatenation: {
    Type scalarResultType = sim::getPackedScalarType(destination.type);
    if (!scalarResultType || destination.children.empty())
      return failure();
    SmallVector<Value> inputs;
    for (const CapturedLValue &child : destination.children) {
      FailureOr<Value> input = loadCapturedLValue(child, location);
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
      Value result = sim::SimLogicConcatOp::create(builder, location,
                                                   resultLogic, logicInputs);
      return convert(result, destination.type, false, location,
                     isSignedNode(destination.semanticNode));
    }
    auto resultInteger = dyn_cast<IntegerType>(scalarResultType);
    if (!resultInteger)
      return failure();
    Value combined =
        arith::ConstantOp::create(builder, location, resultInteger,
                                  builder.getIntegerAttr(resultInteger, 0));
    unsigned trailingWidth = resultInteger.getWidth();
    for (Value input : inputs) {
      auto inputInteger = dyn_cast<IntegerType>(input.getType());
      if (!inputInteger || inputInteger.getWidth() > trailingWidth)
        return failure();
      trailingWidth -= inputInteger.getWidth();
      FailureOr<Value> extended =
          convert(input, resultInteger, false, location);
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
    return convert(combined, destination.type, false, location,
                   isSignedNode(destination.semanticNode));
  }
  }
  llvm_unreachable("unknown captured lvalue kind");
}

bool UnitLowering::haveSameCapturedStorage(const CapturedLValue &lhs,
                                           const CapturedLValue &rhs) const {
  if (lhs.kind != rhs.kind)
    return false;
  switch (lhs.kind) {
  case CapturedLValue::Kind::Reference: {
    if (lhs.reference == rhs.reference)
      return true;
    auto lhsField = lhs.reference.getDefiningOp<sim::SimClassFieldRefOp>();
    auto rhsField = rhs.reference.getDefiningOp<sim::SimClassFieldRefOp>();
    return lhsField && rhsField &&
           lhsField.getObject() == rhsField.getObject() &&
           lhsField.getFieldAttr() == rhsField.getFieldAttr();
  }
  case CapturedLValue::Kind::PackedDynamicSlice:
  case CapturedLValue::Kind::PackedValueSlice:
    return false;
  case CapturedLValue::Kind::AggregateElement:
    return lhs.ordinal == rhs.ordinal && lhs.children.size() == 1 &&
           rhs.children.size() == 1 &&
           haveSameCapturedStorage(lhs.children.front(), rhs.children.front());
  case CapturedLValue::Kind::AggregateDynamicElement:
  case CapturedLValue::Kind::ContainerElement:
  case CapturedLValue::Kind::AssociativeElement: {
    if (lhs.children.size() != 1 || rhs.children.size() != 1 ||
        !haveSameCapturedStorage(lhs.children.front(), rhs.children.front()))
      return false;
    if (lhs.index == rhs.index)
      return true;
    Attribute lhsConstant;
    Attribute rhsConstant;
    return matchPattern(lhs.index, m_Constant(&lhsConstant)) &&
           matchPattern(rhs.index, m_Constant(&rhsConstant)) &&
           lhsConstant == rhsConstant;
  }
  case CapturedLValue::Kind::StringCharacter:
  case CapturedLValue::Kind::AggregateSlice:
  case CapturedLValue::Kind::ContainerSlice:
  case CapturedLValue::Kind::Concatenation:
    return false;
  }
  llvm_unreachable("unknown captured lvalue kind");
}

void UnitLowering::propagateCapturedContainers(const CapturedLValue &source,
                                               CapturedLValue &destination) {
  // Concatenation leaves commit in source order. When two leaves target the
  // same captured container storage, feed the first leaf's rebuilt container
  // into the second so its write cannot restore the encounter-time snapshot
  // and discard the earlier update.
  if ((source.kind == CapturedLValue::Kind::ContainerElement ||
       source.kind == CapturedLValue::Kind::AssociativeElement) &&
      source.kind == destination.kind && source.children.size() == 1 &&
      destination.children.size() == 1 &&
      haveSameCapturedStorage(source.children.front(),
                              destination.children.front()))
    destination.container = source.container;
  for (CapturedLValue &child : destination.children)
    propagateCapturedContainers(source, child);
  for (const CapturedLValue &child : source.children)
    propagateCapturedContainers(child, destination);
}

LogicalResult UnitLowering::writeCapturedLValue(CapturedLValue &destination,
                                                Value value, bool sourceSigned,
                                                bool nonblocking,
                                                Location location,
                                                Value delay) {
  if (!nonblocking || observeNonblockingWrites)
    recordImplicitWrite(destination.reference);
  switch (destination.kind) {
  case CapturedLValue::Kind::Reference: {
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(converted))
      return failure();
    Value published = *converted;
    if (isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
            destination.type))
      published = sim::SimContainerCloneOp::create(builder, location,
                                                   destination.type, published);
    Type referenceType = destination.reference.getType();
    if (isa<sim::ManagedRefType>(referenceType)) {
      if (nonblocking)
        sim::SimManagedNBAEnqueueOp::create(builder, location, published,
                                            destination.reference, delay);
      else
        sim::SimManagedStoreOp::create(builder, location, published,
                                       destination.reference);
    } else if (isa<sim::RefType>(referenceType)) {
      TimingPathMaskedPlan *storagePlan = nullptr;
      Operation *root = destination.semanticNode;
      while (isa_and_nonnull<semantic::SVElementSelectExpressionOp,
                             semantic::SVRangeSelectExpressionOp,
                             semantic::SVMemberAccessExpressionOp>(root)) {
        SmallVector<Operation *> children = getChildren(root);
        if (children.empty())
          break;
        root = children.front();
      }
      if (auto nodeID = root ? root->getAttrOfType<IntegerAttr>("node_id")
                             : IntegerAttr{}) {
        auto found =
            timingPathMaskedPlans.find(nodeID.getValue().getZExtValue());
        if (found != timingPathMaskedPlans.end() &&
            found->second.proceduralStorage) {
          storagePlan = &found->second;
          usedTimingPathMaskedPlans.insert(nodeID.getValue().getZExtValue());
        }
      }
      if (storagePlan) {
        std::optional<unsigned> width =
            sim::getPackedWidth(published.getType());
        auto maskType =
            dyn_cast<IntegerType>(storagePlan->coverageMask.getType());
        if (!width || !maskType || maskType.getWidth() != *width ||
            storagePlan->groups.empty() ||
            storagePlan->groups.size() > UINT32_MAX)
          return function.emitError("invalid procedural timing path plan");
        Value previous = sim::SimRefLoadOp::create(
            builder, location, published.getType(), destination.reference);
        auto logicType = sim::LogicType::get(function.getContext(), *width);
        auto toLogic = [&](Value scalar) -> FailureOr<Value> {
          if (!isa<IntegerType, sim::LogicType>(scalar.getType())) {
            FailureOr<Value> packed = toPackedScalar(scalar, location);
            if (failed(packed))
              return failure();
            scalar = *packed;
          }
          if (isa<sim::LogicType>(scalar.getType()))
            return scalar;
          return Value(sim::SimLogicFromBitsOp::create(builder, location,
                                                       logicType, scalar));
        };
        FailureOr<Value> previousLogic = toLogic(previous);
        FailureOr<Value> publishedLogic = toLogic(published);
        if (failed(previousLogic) || failed(publishedLogic))
          return function.emitError(
              "procedural timing path value is not a packed scalar");
        APInt zero = APInt::getZero(*width);
        APInt ones = APInt::getAllOnes(*width);
        Value zeroMask =
            arith::ConstantOp::create(builder, location, maskType,
                                      builder.getIntegerAttr(maskType, zero));
        Value onesMask =
            arith::ConstantOp::create(builder, location, maskType,
                                      builder.getIntegerAttr(maskType, ones));
        auto packPulseTransitions = [&](const std::array<Value, 12> &masks) {
          IntegerType packedType = builder.getIntegerType(*width * 12);
          if (llvm::all_equal(masks)) {
            APInt replicate = APInt::getZero(packedType.getWidth());
            for (unsigned transition = 0; transition != 12; ++transition)
              replicate.setBit(transition * *width);
            Value extended = arith::ExtUIOp::create(builder, location,
                                                    packedType, masks.front());
            Value factor = arith::ConstantOp::create(
                builder, location, packedType,
                builder.getIntegerAttr(packedType, replicate));
            return Value(
                arith::MulIOp::create(builder, location, extended, factor));
          }
          Value packed =
              arith::ConstantOp::create(builder, location, packedType,
                                        builder.getIntegerAttr(packedType, 0));
          for (auto [transition, mask] : llvm::enumerate(masks)) {
            Value extended =
                arith::ExtUIOp::create(builder, location, packedType, mask);
            if (transition != 0) {
              Value shift = arith::ConstantOp::create(
                  builder, location, packedType,
                  builder.getIntegerAttr(packedType, transition * *width));
              extended =
                  arith::ShLIOp::create(builder, location, extended, shift);
            }
            packed = arith::OrIOp::create(builder, location, packed, extended);
          }
          return packed;
        };
        auto remainingPathDelay = [&](const TimingPathDelayGroup &group) {
          if (!storagePlan->qualificationTime)
            return group.delay;
          Value now = sim::SimTimeNowOp::create(
              builder, location, builder.getI64Type(),
              function.getBody().front().getArgument(0));
          Value elapsed = arith::SubIOp::create(builder, location, now,
                                                storagePlan->qualificationTime);
          Value declared = arith::ConstantOp::create(
              builder, location, builder.getI64Type(),
              builder.getI64IntegerAttr(group.delayTicks));
          Value expired = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::uge, elapsed, declared);
          Value remaining =
              arith::SubIOp::create(builder, location, declared, elapsed);
          remaining = arith::SelectOp::create(
              builder, location, expired,
              arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                        builder.getI64IntegerAttr(0)),
              remaining);
          // IEEE 1800-2017 30.4 and 30.5 measure a module-path delay from
          // the qualifying source transition. A procedural destination may
          // execute after an explicit delay, so schedule only the unelapsed
          // remainder instead of incorrectly adding both delays.
          return Value(sim::SimTimeScaleOp::create(
              builder, location, sim::TimeType::get(function.getContext()),
              remaining, builder.getI64IntegerAttr(1),
              builder.getBoolAttr(false)));
        };
        Value writeMask = proceduralTimingWriteMask.value_or(onesMask);
        auto consume = [&](Value changed) {
          Value consumed =
              arith::AndIOp::create(builder, location, changed, writeMask);
          Value retained =
              arith::XOrIOp::create(builder, location, consumed, onesMask);
          for (Value pendingRef : storagePlan->edgePending) {
            Value pending = sim::SimRefLoadOp::create(builder, location,
                                                      maskType, pendingRef);
            pending =
                arith::AndIOp::create(builder, location, pending, retained);
            sim::SimRefStoreOp::create(builder, location, pending, pendingRef);
          }
        };
        uint32_t groupCount = storagePlan->groups.size();
        if (storagePlan->transitionIndependent) {
          Value changed = sim::SimLogicCaseDifferenceMaskOp::create(
              builder, location, maskType, *previousLogic, *publishedLogic);
          consume(changed);
          for (auto [index, group] : llvm::enumerate(storagePlan->groups)) {
            Value active = group.masks.front();
            Value pulseTransitions = group.pulseControlled
                                         ? packPulseTransitions(group.masks)
                                         : Value{};
            Value delay = remainingPathDelay(group);
            sim::SimRefStoreInertialPathOp::create(
                builder, location, destination.reference, published, writeMask,
                storagePlan->coverageMask, active, active, active,
                pulseTransitions, delay, delay, delay,
                builder.getI64IntegerAttr(storagePlan->siteID),
                builder.getI32IntegerAttr(0),
                builder.getI32IntegerAttr(static_cast<uint32_t>(index)),
                builder.getI32IntegerAttr(groupCount),
                builder.getBoolAttr(nonblocking),
                builder.getI64IntegerAttr(
                    group.pulseControlled ? group.pulseReject : -1),
                builder.getI64IntegerAttr(
                    group.pulseControlled ? group.pulseError : -1),
                builder.getBoolAttr(group.pulseControlled &&
                                    group.pulseOnDetect),
                builder.getBoolAttr(group.pulseControlled &&
                                    group.pulseShowCancelled));
          }
          return success();
        }
        std::array<Value, 4> oldSymbols;
        std::array<Value, 4> newSymbols;
        for (unsigned symbol = 0; symbol != 3; ++symbol) {
          bool one = symbol == 1;
          bool unknown = symbol == 2;
          Value constant = sim::SimLogicConstantOp::create(
              builder, location, logicType,
              builder.getIntegerAttr(maskType, one ? ones : zero),
              builder.getIntegerAttr(maskType, unknown ? ones : zero));
          auto equalMask = [&](Value value) -> Value {
            Value difference = sim::SimLogicCaseDifferenceMaskOp::create(
                builder, location, maskType, value, constant);
            return arith::XOrIOp::create(builder, location, difference,
                                         onesMask);
          };
          oldSymbols[symbol] = equalMask(*previousLogic);
          newSymbols[symbol] = equalMask(*publishedLogic);
        }
        auto remaining = [&](const std::array<Value, 4> &symbols) {
          Value used =
              arith::OrIOp::create(builder, location, symbols[0], symbols[1]);
          used = arith::OrIOp::create(builder, location, used, symbols[2]);
          return Value(
              arith::XOrIOp::create(builder, location, used, onesMask));
        };
        oldSymbols[3] = remaining(oldSymbols);
        newSymbols[3] = remaining(newSymbols);
        constexpr std::array<unsigned, 12> from = {0, 1, 0, 3, 1, 3,
                                                   0, 2, 1, 2, 2, 3};
        constexpr std::array<unsigned, 12> to = {1, 0, 3, 1, 3, 0,
                                                 2, 1, 2, 0, 3, 2};
        std::array<Value, 12> transitions;
        Value changed = zeroMask;
        for (unsigned index = 0; index != 12; ++index) {
          transitions[index] =
              arith::AndIOp::create(builder, location, oldSymbols[from[index]],
                                    newSymbols[to[index]]);
          changed = arith::OrIOp::create(builder, location, changed,
                                         transitions[index]);
        }
        consume(changed);
        for (auto [index, group] : llvm::enumerate(storagePlan->groups)) {
          std::array<Value, 3> masks{zeroMask, zeroMask, zeroMask};
          auto add = [&](unsigned bank, unsigned transition) {
            Value selected = arith::AndIOp::create(builder, location,
                                                   group.masks[transition],
                                                   transitions[transition]);
            masks[bank] =
                arith::OrIOp::create(builder, location, masks[bank], selected);
          };
          for (unsigned transition : {0u, 3u, 7u})
            add(0, transition);
          for (unsigned transition : {1u, 5u, 9u})
            add(1, transition);
          for (unsigned transition : {2u, 4u, 10u})
            add(2, transition);
          for (unsigned transition : {6u, 8u, 11u})
            add(0, transition);
          Value pulseTransitions = group.pulseControlled
                                       ? packPulseTransitions(group.masks)
                                       : Value{};
          Value delay = remainingPathDelay(group);
          sim::SimRefStoreInertialPathOp::create(
              builder, location, destination.reference, published, writeMask,
              storagePlan->coverageMask, masks[0], masks[1], masks[2],
              pulseTransitions, delay, delay, delay,
              builder.getI64IntegerAttr(storagePlan->siteID),
              builder.getI32IntegerAttr(0),
              builder.getI32IntegerAttr(static_cast<uint32_t>(index)),
              builder.getI32IntegerAttr(groupCount),
              builder.getBoolAttr(nonblocking),
              builder.getI64IntegerAttr(
                  group.pulseControlled ? group.pulseReject : -1),
              builder.getI64IntegerAttr(group.pulseControlled ? group.pulseError
                                                              : -1),
              builder.getBoolAttr(group.pulseControlled && group.pulseOnDetect),
              builder.getBoolAttr(group.pulseControlled &&
                                  group.pulseShowCancelled));
        }
        return success();
      }
      if (nonblocking)
        sim::SimNBAEnqueueOp::create(builder, location, published,
                                     destination.reference, delay,
                                     sim::NBASiteAttr{}, IntegerAttr{});
      else {
        auto store = sim::SimRefStoreOp::create(builder, location, published,
                                                destination.reference);
        if (continuousStore)
          store->setAttr(continuousStoreAttrName, builder.getUnitAttr());
      }
    } else if (isa<sim::ArgumentRefType>(referenceType)) {
      if (nonblocking) {
        emitError(location)
            << "nonblocking assignment cannot target a ref formal";
        return failure();
      }
      sim::SimArgumentRefStoreOp::create(builder, location, published,
                                         destination.reference);
    } else if (isa<sim::ReferencePathType>(referenceType)) {
      if (nonblocking)
        sim::SimReferencePathNBAEnqueueOp::create(builder, location, published,
                                                  destination.reference, delay);
      else if (failed(
                   storeReference(destination.reference, published, location)))
        return failure();
    } else if (isa<sim::DriverType>(referenceType)) {
      if (nonblocking) {
        emitError(location) << "nonblocking assignment cannot target a driver";
        return failure();
      }
      bool userRaw = isUserNetDriver(destination.reference);
      auto delays = function->getAttrOfType<DenseI64ArrayAttr>(
          "obelisk_sim.propagation_delays");
      TimingPathMaskedPlan *maskedPlan = nullptr;
      Operation *driverNode = destination.semanticNode;
      while (
          isa_and_nonnull<semantic::SVElementSelectExpressionOp,
                          semantic::SVRangeSelectExpressionOp,
                          semantic::SVMemberAccessExpressionOp>(driverNode)) {
        SmallVector<Operation *> children = getChildren(driverNode);
        if (children.empty())
          break;
        driverNode = children.front();
      }
      if (auto nodeID = driverNode
                            ? driverNode->getAttrOfType<IntegerAttr>("node_id")
                            : IntegerAttr{}) {
        auto found =
            timingPathMaskedPlans.find(nodeID.getValue().getZExtValue());
        if (found != timingPathMaskedPlans.end()) {
          maskedPlan = &found->second;
          usedTimingPathMaskedPlans.insert(nodeID.getValue().getZExtValue());
        }
      }
      if (!maskedPlan && timingPathMaskedPlan)
        maskedPlan = &*timingPathMaskedPlan;
      if (maskedPlan) {
        auto codeUnitID = function->getAttrOfType<IntegerAttr>("code_unit_id");
        std::optional<unsigned> drivenWidth =
            sim::getPackedWidth(published.getType());
        auto maskType =
            dyn_cast<IntegerType>(maskedPlan->coverageMask.getType());
        if (!codeUnitID || !drivenWidth || !maskType ||
            maskType.getWidth() != *drivenWidth || maskedPlan->groups.empty() ||
            maskedPlan->groups.size() > UINT32_MAX ||
            nextInertialDriveComponent > UINT32_MAX)
          return function.emitError("invalid masked timing path drive plan");
        uint32_t component =
            static_cast<uint32_t>(nextInertialDriveComponent++);
        uint32_t groupCount = static_cast<uint32_t>(maskedPlan->groups.size());
        Value previous = sim::SimDriverReadOp::create(
            builder, location, published.getType(), destination.reference);
        auto logicType =
            sim::LogicType::get(function.getContext(), *drivenWidth);
        auto toLogic = [&](Value value) -> FailureOr<Value> {
          if (!isa<IntegerType, sim::LogicType>(value.getType())) {
            FailureOr<Value> scalar = toPackedScalar(value, location);
            if (failed(scalar))
              return failure();
            value = *scalar;
          }
          if (isa<sim::LogicType>(value.getType()))
            return value;
          return Value(sim::SimLogicFromBitsOp::create(builder, location,
                                                       logicType, value));
        };
        FailureOr<Value> previousLogic = toLogic(previous);
        FailureOr<Value> publishedLogic = toLogic(published);
        if (failed(previousLogic) || failed(publishedLogic))
          return function.emitError(
              "timing path driver value is not a packed scalar");
        APInt zero = APInt::getZero(*drivenWidth);
        APInt ones = APInt::getAllOnes(*drivenWidth);
        Value zeroMask =
            arith::ConstantOp::create(builder, location, maskType,
                                      builder.getIntegerAttr(maskType, zero));
        Value onesMask =
            arith::ConstantOp::create(builder, location, maskType,
                                      builder.getIntegerAttr(maskType, ones));
        auto packDriverPulseTransitions =
            [&](const std::array<Value, 12> &masks) {
              IntegerType packedType =
                  builder.getIntegerType(*drivenWidth * 12);
              if (llvm::all_equal(masks)) {
                APInt replicate = APInt::getZero(packedType.getWidth());
                for (unsigned transition = 0; transition != 12; ++transition)
                  replicate.setBit(transition * *drivenWidth);
                Value extended = arith::ExtUIOp::create(
                    builder, location, packedType, masks.front());
                Value factor = arith::ConstantOp::create(
                    builder, location, packedType,
                    builder.getIntegerAttr(packedType, replicate));
                return Value(
                    arith::MulIOp::create(builder, location, extended, factor));
              }
              Value packed = arith::ConstantOp::create(
                  builder, location, packedType,
                  builder.getIntegerAttr(packedType, 0));
              for (auto [transition, mask] : llvm::enumerate(masks)) {
                Value extended =
                    arith::ExtUIOp::create(builder, location, packedType, mask);
                if (transition != 0) {
                  Value shift = arith::ConstantOp::create(
                      builder, location, packedType,
                      builder.getIntegerAttr(packedType,
                                             transition * *drivenWidth));
                  extended =
                      arith::ShLIOp::create(builder, location, extended, shift);
                }
                packed =
                    arith::OrIOp::create(builder, location, packed, extended);
              }
              return packed;
            };
        auto consumeEdgePending = [&](Value consumed) {
          if (maskedPlan->edgePending.empty())
            return;
          Value retainedMask =
              arith::XOrIOp::create(builder, location, consumed, onesMask);
          for (Value pendingRef : maskedPlan->edgePending) {
            Value pending = sim::SimRefLoadOp::create(builder, location,
                                                      maskType, pendingRef);
            Value retained =
                arith::AndIOp::create(builder, location, pending, retainedMask);
            sim::SimRefStoreOp::create(builder, location, retained, pendingRef);
          }
        };
        if (maskedPlan->transitionIndependent) {
          Value consumed = sim::SimLogicCaseDifferenceMaskOp::create(
              builder, location, maskType, *previousLogic, *publishedLogic);
          consumeEdgePending(consumed);
          for (auto [index, group] : llvm::enumerate(maskedPlan->groups)) {
            Value mask = group.masks.front();
            Value pulseTransitions =
                group.pulseControlled ? packDriverPulseTransitions(group.masks)
                                      : Value{};
            auto drive = sim::SimDriverDriveInertialPathOp::create(
                builder, location, destination.reference, published,
                maskedPlan->coverageMask, mask, mask, mask, pulseTransitions,
                group.delay, group.delay, group.delay, codeUnitID,
                builder.getI32IntegerAttr(component),
                builder.getI32IntegerAttr(static_cast<uint32_t>(index)),
                builder.getI32IntegerAttr(groupCount),
                builder.getBoolAttr(deferDriverResolution || userRaw),
                builder.getI64IntegerAttr(
                    group.pulseControlled ? group.pulseReject : -1),
                builder.getI64IntegerAttr(
                    group.pulseControlled ? group.pulseError : -1),
                builder.getBoolAttr(group.pulseControlled &&
                                    group.pulseOnDetect),
                builder.getBoolAttr(group.pulseControlled &&
                                    group.pulseShowCancelled));
            if (userRaw)
              drive->setAttr("obelisk_sim.user_net_raw_drive",
                             builder.getUnitAttr());
          }
          return success();
        }
        std::array<Value, 12> transitionMasks;
        {
          // Edge-sensitive path qualification needs to consume only bits
          // which actually publish.  Ordinary paths deliberately avoid this
          // four-state transition expansion: the runtime already owns the
          // pending target and IEEE 1800-2017 30.7 requires that target when
          // classifying the trailing edge of a pulse.
          std::array<Value, 4> previousSymbols;
          std::array<Value, 4> publishedSymbols;
          for (unsigned symbol = 0; symbol != 3; ++symbol) {
            bool one = symbol == 1;
            bool unknown = symbol == 2;
            Value constant = sim::SimLogicConstantOp::create(
                builder, location, logicType,
                builder.getIntegerAttr(maskType, one ? ones : zero),
                builder.getIntegerAttr(maskType, unknown ? ones : zero));
            auto symbolMask = [&](Value value) -> Value {
              Value difference = sim::SimLogicCaseDifferenceMaskOp::create(
                  builder, location, maskType, value, constant);
              return arith::XOrIOp::create(builder, location, difference,
                                           onesMask);
            };
            previousSymbols[symbol] = symbolMask(*previousLogic);
            publishedSymbols[symbol] = symbolMask(*publishedLogic);
          }
          auto remainingSymbol = [&](const std::array<Value, 4> &symbols) {
            Value used =
                arith::OrIOp::create(builder, location, symbols[0], symbols[1]);
            used = arith::OrIOp::create(builder, location, used, symbols[2]);
            return Value(
                arith::XOrIOp::create(builder, location, used, onesMask));
          };
          previousSymbols[3] = remainingSymbol(previousSymbols);
          publishedSymbols[3] = remainingSymbol(publishedSymbols);
          constexpr std::array<unsigned, 12> fromSymbols = {0, 1, 0, 3, 1, 3,
                                                            0, 2, 1, 2, 2, 3};
          constexpr std::array<unsigned, 12> toSymbols = {1, 0, 3, 1, 3, 0,
                                                          2, 1, 2, 0, 3, 2};
          Value consumed = zeroMask;
          for (unsigned transition = 0; transition != 12; ++transition) {
            transitionMasks[transition] = arith::AndIOp::create(
                builder, location, previousSymbols[fromSymbols[transition]],
                publishedSymbols[toSymbols[transition]]);
            consumed = arith::OrIOp::create(builder, location, consumed,
                                            transitionMasks[transition]);
          }
          consumeEdgePending(consumed);
        }
        for (auto [index, group] : llvm::enumerate(maskedPlan->groups)) {
          std::array<Value, 3> runtimeMasks{zeroMask, zeroMask, zeroMask};
          auto addTransition = [&](unsigned bank, unsigned transition) {
            Value selected = arith::AndIOp::create(builder, location,
                                                   group.masks[transition],
                                                   transitionMasks[transition]);
            runtimeMasks[bank] = arith::OrIOp::create(
                builder, location, runtimeMasks[bank], selected);
          };
          for (unsigned transition : {0u, 3u, 7u})
            addTransition(0, transition);
          for (unsigned transition : {1u, 5u, 9u})
            addTransition(1, transition);
          for (unsigned transition : {2u, 4u, 10u})
            addTransition(2, transition);
          // The runtime's X-target branch accepts any of its three masks and
          // selects the minimum candidate. Keep the exact X classes in one
          // bank because this group already represents one distinct delay.
          for (unsigned transition : {6u, 8u, 11u})
            addTransition(0, transition);
          Value pulseTransitions = group.pulseControlled
                                       ? packDriverPulseTransitions(group.masks)
                                       : Value{};
          auto drive = sim::SimDriverDriveInertialPathOp::create(
              builder, location, destination.reference, published,
              maskedPlan->coverageMask, runtimeMasks[0], runtimeMasks[1],
              runtimeMasks[2], pulseTransitions, group.delay, group.delay,
              group.delay, codeUnitID, builder.getI32IntegerAttr(component),
              builder.getI32IntegerAttr(static_cast<uint32_t>(index)),
              builder.getI32IntegerAttr(groupCount),
              builder.getBoolAttr(deferDriverResolution || userRaw),
              builder.getI64IntegerAttr(
                  group.pulseControlled ? group.pulseReject : -1),
              builder.getI64IntegerAttr(group.pulseControlled ? group.pulseError
                                                              : -1),
              builder.getBoolAttr(group.pulseControlled && group.pulseOnDetect),
              builder.getBoolAttr(group.pulseControlled &&
                                  group.pulseShowCancelled));
          if (userRaw)
            drive->setAttr("obelisk_sim.user_net_raw_drive",
                           builder.getUnitAttr());
        }
        return success();
      }
      if (delays || timingPathDelays) {
        if (delays && (delays.empty() || delays.size() > 3))
          return function.emitError("invalid frozen propagation delays");
        auto timeConstant = [&](int64_t ticks) {
          return sim::SimTimeConstantOp::create(
              builder, location, sim::TimeType::get(function.getContext()),
              builder.getI64IntegerAttr(ticks));
        };
        auto codeUnitID = function->getAttrOfType<IntegerAttr>("code_unit_id");
        if (!codeUnitID)
          return function.emitError(
              "delayed drive has no stable code unit identity");
        std::optional<unsigned> drivenWidth =
            sim::getPackedWidth(published.getType());
        if (auto floating = dyn_cast<FloatType>(published.getType()))
          drivenWidth = floating.getWidth();
        if (!drivenWidth)
          return function.emitError(
              "delayed drive value has no fixed packed width");
        bool vectorDelay = !function->hasAttr("obelisk_sim.primitive_name") &&
                           *drivenWidth != 1;
        if (nextInertialDriveComponent > UINT32_MAX)
          return function.emitError("too many delayed drive sites");
        Value riseDelay;
        Value fallDelay;
        Value turnoffDelay;
        if (timingPathDelays) {
          riseDelay = (*timingPathDelays)[0];
          fallDelay = (*timingPathDelays)[1];
          turnoffDelay = (*timingPathDelays)[2];
        } else {
          ArrayRef<int64_t> values = delays.asArrayRef();
          int64_t rise = values[0];
          int64_t fall = values.size() == 1 ? rise : values[1];
          int64_t turnoff = values.size() == 1   ? rise
                            : values.size() == 2 ? std::min(rise, fall)
                                                 : values[2];
          riseDelay = timeConstant(rise);
          fallDelay = timeConstant(fall);
          turnoffDelay = timeConstant(turnoff);
        }
        auto drive = sim::SimDriverDriveInertialOp::create(
            builder, location, destination.reference, published, riseDelay,
            fallDelay, turnoffDelay, codeUnitID,
            builder.getI32IntegerAttr(
                static_cast<uint32_t>(nextInertialDriveComponent++)),
            builder.getBoolAttr(vectorDelay),
            builder.getBoolAttr(deferDriverResolution || userRaw));
        if (userRaw)
          drive->setAttr("obelisk_sim.user_net_raw_drive",
                         builder.getUnitAttr());
        return success();
      }
      if (drivesDelayedNet(function, destination.reference)) {
        auto drive = sim::SimDriverDriveDelayedNetOp::create(
            builder, location, destination.reference, published,
            builder.getBoolAttr(deferDriverResolution || userRaw));
        if (userRaw)
          drive->setAttr("obelisk_sim.user_net_raw_drive",
                         builder.getUnitAttr());
      } else {
        auto drive = sim::SimDriverDriveOp::create(
            builder, location, destination.reference, published);
        if (deferDriverResolution || userRaw)
          drive->setAttr("obelisk_sim.defer_net_resolution",
                         builder.getUnitAttr());
        if (userRaw)
          drive->setAttr("obelisk_sim.user_net_raw_drive",
                         builder.getUnitAttr());
      }
    } else {
      return failure();
    }
    return success();
  }
  case CapturedLValue::Kind::PackedDynamicSlice: {
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    FailureOr<Value> scalar = succeeded(converted)
                                  ? toPackedScalar(*converted, location)
                                  : FailureOr<Value>(failure());
    auto baseReference =
        dyn_cast<sim::RefType>(destination.reference.getType());
    std::optional<unsigned> baseWidth =
        baseReference ? sim::getPackedWidth(baseReference.getElementType())
                      : std::nullopt;
    std::optional<unsigned> resultWidth = sim::getPackedWidth(destination.type);
    if (failed(converted) || failed(scalar) || !baseWidth || !resultWidth)
      return failure();
    const unsigned baseBitWidth = baseWidth.value();
    const unsigned selectedBitWidth = resultWidth.value();
    if (selectedBitWidth == 0 || selectedBitWidth > baseBitWidth)
      return failure();

    // Preserve the exact input-view boundary for an NBA. Native lowering
    // clips this dynamic view once in the scheduler, and bytecode references
    // already carry the same begin/end bounds. This keeps generated IR
    // independent of the selection width while capturing both the index and
    // RHS at the point where the NBA is encountered.
    if (nonblocking) {
      CapturedLValue selected;
      selected.semanticNode = destination.semanticNode;
      selected.type = destination.type;
      selected.reference = sim::SimRefDynExtractOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), destination.type),
          destination.reference, destination.index);
      return writeCapturedLValue(selected, *converted, false, true, location,
                                 delay);
    }

    // A dynamic packed reference cannot by itself carry its declaration
    // boundary when its stable handle is global. Split partial overlaps into
    // clipped, in-range references. The surviving lvalue is rebased to the
    // low bits of the replacement, matching indexed part-select assignment
    // behavior at either declaration boundary. Sliced NBAs remain narrow and
    // therefore compose with other sliced NBAs.
    Value low = destination.index;
    Value known = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    if (auto logic = dyn_cast<sim::LogicType>(low.getType())) {
      auto bitsType = builder.getIntegerType(logic.getWidth());
      Value bits =
          sim::SimLogicToBitsOp::create(builder, location, bitsType, low);
      Value roundTrip =
          sim::SimLogicFromBitsOp::create(builder, location, logic, bits);
      known = sim::SimLogicCompareOp::create(
          builder, location, builder.getI1Type(), sim::CompareKind::CaseEq, low,
          roundTrip);
      low = bits;
    }
    auto lowType = dyn_cast<IntegerType>(low.getType());
    if (!lowType)
      return failure();

    auto constant = [&](int64_t value) -> Value {
      return arith::ConstantOp::create(
          builder, location, lowType,
          builder.getIntegerAttr(
              lowType,
              APInt(lowType.getWidth(), static_cast<uint64_t>(value), true)));
    };
    auto branchToResume = [&](Block *resume) {
      if (current->empty() ||
          !current->back().hasTrait<OpTrait::IsTerminator>())
        cf::BranchOp::create(builder, location, resume);
    };
    // `sourceLow` is the bit of the assigned value that lands on `baseLow`.
    // A selection clipped at the low end starts partway into the value, so
    // taking its lowest bits would shift the whole assignment down.
    auto writePart = [&](unsigned baseLow, unsigned sourceLow,
                         unsigned width) -> LogicalResult {
      Value replacement;
      Type replacementType;
      if (auto logic = dyn_cast<sim::LogicType>((*scalar).getType())) {
        replacementType = sim::LogicType::get(function.getContext(), width);
        replacement = sim::SimLogicExtractOp::create(
            builder, location, replacementType, *scalar,
            builder.getI64IntegerAttr(sourceLow));
      } else if (auto integer = dyn_cast<IntegerType>((*scalar).getType())) {
        replacementType = builder.getIntegerType(width);
        Value shifted = *scalar;
        if (sourceLow != 0)
          shifted = arith::ShRUIOp::create(
              builder, location, shifted,
              arith::ConstantOp::create(
                  builder, location, integer,
                  builder.getIntegerAttr(integer, sourceLow)));
        replacement = replacementType == integer
                          ? shifted
                          : Value(arith::TruncIOp::create(
                                builder, location, replacementType, shifted));
      } else {
        return failure();
      }
      Type referenceType =
          sim::RefType::get(function.getContext(), replacementType);
      Value reference = sim::SimRefExtractOp::create(
          builder, location, referenceType, destination.reference,
          builder.getI64IntegerAttr(baseLow));
      CapturedLValue part;
      part.semanticNode = destination.semanticNode;
      part.type = replacementType;
      part.reference = reference;
      return writeCapturedLValue(part, replacement, false, nonblocking,
                                 location, delay);
    };

    Block *dispatch = addBlock();
    Block *resume = addBlock();
    cf::CondBranchOp::create(builder, location, known, dispatch, ValueRange{},
                             resume, ValueRange{});
    setCurrent(dispatch);
    Value nonnegative = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sge, low, constant(0));
    Value atMostFull = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sle, low,
        constant(static_cast<int64_t>(baseBitWidth - selectedBitWidth)));
    Value fullyInRange =
        arith::AndIOp::create(builder, location, nonnegative, atMostFull);
    Block *full = addBlock();
    Block *partial = addBlock();
    cf::CondBranchOp::create(builder, location, fullyInRange, full,
                             ValueRange{}, partial, ValueRange{});

    setCurrent(full);
    Type selectedType =
        sim::RefType::get(function.getContext(), destination.type);
    CapturedLValue selected;
    selected.semanticNode = destination.semanticNode;
    selected.type = destination.type;
    selected.reference = sim::SimRefDynExtractOp::create(
        builder, location, selectedType, destination.reference,
        destination.index);
    if (failed(writeCapturedLValue(selected, *converted, false, nonblocking,
                                   location, delay)))
      return failure();
    branchToResume(resume);

    setCurrent(partial);
    for (unsigned clipped = 1; clipped < selectedBitWidth; ++clipped) {
      Value matches =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                low, constant(-static_cast<int64_t>(clipped)));
      Block *write = addBlock();
      Block *next = addBlock();
      cf::CondBranchOp::create(builder, location, matches, write, ValueRange{},
                               next, ValueRange{});
      setCurrent(write);
      if (failed(writePart(/*baseLow=*/0, /*sourceLow=*/clipped,
                           selectedBitWidth - clipped)))
        return failure();
      branchToResume(resume);
      setCurrent(next);
    }
    for (unsigned overlap = 1; overlap < selectedBitWidth; ++overlap) {
      unsigned selectedLow = baseBitWidth - overlap;
      Value matches = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, low,
          constant(static_cast<int64_t>(selectedLow)));
      Block *write = addBlock();
      Block *next = addBlock();
      cf::CondBranchOp::create(builder, location, matches, write, ValueRange{},
                               next, ValueRange{});
      setCurrent(write);
      if (failed(writePart(selectedLow, /*sourceLow=*/0, overlap)))
        return failure();
      branchToResume(resume);
      setCurrent(next);
    }
    branchToResume(resume);
    setCurrent(resume);
    return success();
  }
  case CapturedLValue::Kind::PackedValueSlice: {
    if (destination.children.size() != 1)
      return failure();
    bool proceduralPath = false;
    if (!destination.children.empty()) {
      Operation *root = destination.children.front().semanticNode;
      auto nodeID =
          root ? root->getAttrOfType<IntegerAttr>("node_id") : IntegerAttr{};
      auto found =
          nodeID ? timingPathMaskedPlans.find(nodeID.getValue().getZExtValue())
                 : timingPathMaskedPlans.end();
      proceduralPath = found != timingPathMaskedPlans.end() &&
                       found->second.proceduralStorage;
    }
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(converted))
      return failure();
    FailureOr<Value> replacement = toPackedScalar(*converted, location);
    if (failed(replacement))
      return failure();

    // Reify a statically selected fixed-aggregate path as an exact ordinary
    // reference. Its element type is the view boundary used by the bounded
    // packed-slice NBA path, so an overhang cannot reach an adjacent member.
    std::function<FailureOr<Value>(CapturedLValue &)> exactReference =
        [&](CapturedLValue &captured) -> FailureOr<Value> {
      if (captured.kind == CapturedLValue::Kind::Reference &&
          isa<sim::RefType>(captured.reference.getType()))
        return captured.reference;
      if (captured.kind != CapturedLValue::Kind::AggregateElement ||
          captured.children.size() != 1 ||
          isa<sim::UnpackedUnionType>(captured.children.front().type))
        return failure();
      FailureOr<Value> parent = exactReference(captured.children.front());
      if (failed(parent) ||
          sim::getAggregateElementType(captured.children.front().type,
                                       captured.ordinal) != captured.type)
        return failure();
      return sim::SimRefSubelementOp::create(
                 builder, location,
                 sim::RefType::get(function.getContext(), captured.type),
                 *parent,
                 builder.getDenseI64ArrayAttr(
                     {static_cast<int64_t>(captured.ordinal)}))
          .getResult();
    };
    FailureOr<Value> boundedBase =
        nonblocking && !proceduralPath
            ? exactReference(destination.children.front())
            : FailureOr<Value>(failure());
    if (succeeded(boundedBase)) {
      Value low = destination.index;
      if (!low)
        low = arith::ConstantOp::create(
            builder, location, builder.getI64Type(),
            builder.getI64IntegerAttr(destination.lowBit));
      if (destination.padding) {
        if (auto integer = dyn_cast<IntegerType>(low.getType())) {
          Value padding = arith::ConstantOp::create(
              builder, location, integer,
              builder.getIntegerAttr(integer, destination.padding));
          low = arith::SubIOp::create(builder, location, low, padding);
        } else if (auto logic = dyn_cast<sim::LogicType>(low.getType())) {
          auto plane = builder.getIntegerType(logic.getWidth());
          Value padding = sim::SimLogicConstantOp::create(
              builder, location, logic,
              builder.getIntegerAttr(plane, destination.padding),
              builder.getIntegerAttr(plane, 0));
          low = sim::SimLogicBinaryOp::create(
              builder, location, logic, sim::BinaryKind::Sub, low, padding);
        } else {
          return failure();
        }
      }
      Type selectedReference =
          sim::RefType::get(function.getContext(), destination.type);
      CapturedLValue selected;
      selected.semanticNode = destination.semanticNode;
      selected.type = destination.type;
      selected.reference = sim::SimRefDynExtractOp::create(
          builder, location, selectedReference, *boundedBase, low);
      return writeCapturedLValue(selected, *converted, false, true, location,
                                 delay);
    }
    if (nonblocking && !proceduralPath) {
      emitError(location)
          << "nonblocking packed selection assignment requires a captured "
             "whole-storage partial-update path";
      return failure();
    }
    CapturedLValue &base = destination.children.front();
    FailureOr<Value> baseValue = loadCapturedLValue(base, location);
    if (failed(baseValue))
      return failure();
    FailureOr<Value> baseScalar = toPackedScalar(*baseValue, location);
    if (failed(baseScalar))
      return failure();
    Type unpaddedType = (*baseScalar).getType();
    if (destination.padding) {
      FailureOr<Value> padded =
          padSelectionWindow(*baseScalar, destination.padding, location);
      if (failed(padded))
        return failure();
      baseScalar = *padded;
    }

    Value updated;
    if (destination.index) {
      if (isa<sim::LogicType>((*baseScalar).getType()))
        updated = sim::SimLogicDynInsertOp::create(
            builder, location, (*baseScalar).getType(), *baseScalar,
            *replacement, destination.index);
      else
        updated = sim::SimBitsDynInsertOp::create(
            builder, location, (*baseScalar).getType(), *baseScalar,
            *replacement, destination.index);
    } else if (isa<sim::LogicType>((*baseScalar).getType())) {
      updated = sim::SimLogicInsertOp::create(
          builder, location, (*baseScalar).getType(), *baseScalar, *replacement,
          builder.getI64IntegerAttr(destination.lowBit));
    } else {
      Value low = arith::ConstantOp::create(
          builder, location, builder.getI64Type(),
          builder.getI64IntegerAttr(destination.lowBit));
      updated = sim::SimBitsDynInsertOp::create(builder, location,
                                                (*baseScalar).getType(),
                                                *baseScalar, *replacement, low);
    }
    if (destination.padding) {
      // Whatever the write placed outside the value stays in the padding and
      // is dropped here, leaving only the bits that were in range.
      FailureOr<Value> narrowed = unpadSelectionWindow(
          updated, destination.padding, unpaddedType, location);
      if (failed(narrowed))
        return failure();
      updated = *narrowed;
    }
    FailureOr<Value> rebuilt = convert(updated, base.type, false, location,
                                       isSignedNode(base.semanticNode));
    if (failed(rebuilt))
      return failure();
    std::optional<Value> savedMask = proceduralTimingWriteMask;
    if (proceduralPath) {
      if (destination.index || destination.padding)
        return function.emitError(
            "procedural timing path requires an in-range fixed selection");
      std::optional<unsigned> baseWidth = sim::getPackedWidth(base.type);
      std::optional<unsigned> selectedWidth =
          sim::getPackedWidth(destination.type);
      if (!baseWidth || !selectedWidth || destination.lowBit >= *baseWidth ||
          *selectedWidth > *baseWidth - destination.lowBit)
        return function.emitError("invalid procedural timing path selection");
      auto maskType = builder.getIntegerType(*baseWidth);
      APInt bits = APInt::getBitsSet(*baseWidth, destination.lowBit,
                                     destination.lowBit + *selectedWidth);
      proceduralTimingWriteMask = arith::ConstantOp::create(
          builder, location, maskType, builder.getIntegerAttr(maskType, bits));
    }
    LogicalResult result = writeCapturedLValue(base, *rebuilt, false,
                                               nonblocking, location, delay);
    proceduralTimingWriteMask = savedMask;
    return result;
  }
  case CapturedLValue::Kind::ContainerElement: {
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(converted))
      return failure();
    CapturedLValue &base = destination.children.front();
    if (nonblocking) {
      if (base.kind != CapturedLValue::Kind::Reference)
        return failure();
      FailureOr<Value> owner = toArgumentReference(
          base.reference, destination.container.getType(), location);
      if (failed(owner))
        return failure();
      Type pathType =
          sim::ReferencePathType::get(function.getContext(), destination.type);
      Value path = sim::SimReferencePathIndexOp::create(
          builder, location, pathType,
          function.getBody().front().getArgument(0), destination.container,
          destination.index, *owner);
      sim::SimReferencePathNBAEnqueueOp::create(builder, location, *converted,
                                                path, delay);
      return success();
    }

    FailureOr<Value> currentContainer = loadCapturedLValue(base, location);
    if (failed(currentContainer))
      return failure();
    bool directStorage =
        base.kind == CapturedLValue::Kind::Reference &&
        isa<sim::RefType, sim::ManagedRefType, sim::ArgumentRefType>(
            base.reference.getType());
    Value wasNull;
    Value mutableContainer = *currentContainer;
    if (directStorage) {
      wasNull = sim::SimManagedIsNullOp::create(
          builder, location, builder.getI1Type(), *currentContainer);
      FailureOr<Value> allocated =
          ensureSequentialContainer(*currentContainer, location);
      if (failed(allocated))
        return failure();
      mutableContainer = *allocated;
    }
    bool queue = isa<sim::QueueType>((*currentContainer).getType());
    Block *write = addBlock();
    Block *resume = addBlock();
    resume->addArgument(mutableContainer.getType(), location);
    if (queue) {
      // The queue write intrinsic already owns the append, bound, and invalid
      // index decisions. Keeping them there avoids duplicating checks in every
      // caller and lets its exceptional branch issue the Clause 7.10 warning.
      cf::BranchOp::create(builder, location, write);
    } else {
      Value size = sim::SimContainerSizeOp::create(
          builder, location, builder.getI64Type(), mutableContainer);
      Value zero =
          arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                    builder.getI64IntegerAttr(0));
      Value nonnegative =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::sge,
                                destination.index, zero);
      Value inRange =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                                destination.index, size);
      Value valid =
          arith::AndIOp::create(builder, location, nonnegative, inRange);
      cf::CondBranchOp::create(builder, location, valid, write, ValueRange{},
                               resume, ValueRange{mutableContainer});
    }
    setCurrent(write);
    Value updated = directStorage
                        ? mutableContainer
                        : cloneSequentialValue(mutableContainer, location);
    sim::SimContainerWriteOp::create(builder, location, updated,
                                     destination.index, *converted);
    if (!directStorage) {
      if (failed(writeCapturedLValue(base, updated, false, false, location)))
        return failure();
    } else {
      Block *publish = addBlock();
      Block *published = addBlock();
      cf::CondBranchOp::create(builder, location, wasNull, publish,
                               ValueRange{}, published, ValueRange{});
      setCurrent(publish);
      if (failed(writeCapturedLValue(base, updated, false, false, location)))
        return failure();
      cf::BranchOp::create(builder, location, published);
      setCurrent(published);
    }
    if (current->empty() || !current->back().hasTrait<OpTrait::IsTerminator>())
      cf::BranchOp::create(builder, location, resume, ValueRange{updated});
    setCurrent(resume);
    destination.container = resume->getArgument(0);
    return success();
  }
  case CapturedLValue::Kind::ContainerSlice: {
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(converted))
      return failure();
    Value source = cloneSequentialValue(*converted, location);
    CapturedLValue &base = destination.children.front();
    FailureOr<Value> queue = loadCapturedLValue(base, location);
    if (failed(queue) || !isa<sim::QueueType>((*queue).getType()))
      return failure();
    Value sourceSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), source);
    Value sameSize =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              sourceSize, destination.limit);
    Block *writeHeader = addBlock();
    writeHeader->addArgument(builder.getI64Type(), location);
    Block *writeBody = addBlock();
    Block *mismatch = addBlock();
    Block *done = addBlock();
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    Value one = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(1));
    cf::CondBranchOp::create(builder, location, sameSize, writeHeader,
                             ValueRange{zero}, mismatch, ValueRange{});
    setCurrent(mismatch);
    // IEEE 1800-2017 7.6 requires a queue source assigned to a slice to have
    // exactly the slice's run-time element count. A mismatch reports an error
    // and performs no operation.
    sim::SimErrorOp::create(builder, location,
                            function.getBody().front().getArgument(0));
    cf::BranchOp::create(builder, location, done);
    setCurrent(writeHeader);
    Value ordinal = writeHeader->getArgument(0);
    Value more =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                              ordinal, destination.limit);
    cf::CondBranchOp::create(builder, location, more, writeBody, ValueRange{},
                             done, ValueRange{});
    setCurrent(writeBody);
    Value index =
        arith::AddIOp::create(builder, location, destination.index, ordinal);
    Type elementType = cast<sim::QueueType>(destination.type).getElementType();
    Value element = sim::SimContainerReadOp::create(
        builder, location, elementType, source, ordinal);
    if (nonblocking) {
      FailureOr<Value> owner =
          toArgumentReference(base.reference, (*queue).getType(), location);
      if (failed(owner))
        return failure();
      Type pathType =
          sim::ReferencePathType::get(function.getContext(), elementType);
      Value path = sim::SimReferencePathIndexOp::create(
          builder, location, pathType,
          function.getBody().front().getArgument(0), *queue, index, *owner);
      sim::SimReferencePathNBAEnqueueOp::create(builder, location, element,
                                                path, delay);
    } else {
      sim::SimContainerWriteOp::create(builder, location, *queue, index,
                                       element);
    }
    Value next = arith::AddIOp::create(builder, location, ordinal, one);
    cf::BranchOp::create(builder, location, writeHeader, ValueRange{next});
    setCurrent(done);
    return success();
  }
  case CapturedLValue::Kind::AssociativeElement: {
    if (destination.children.size() != 1)
      return failure();
    if (nonblocking) {
      emitError(location)
          << "nonblocking assignment cannot target an associative element";
      return failure();
    }
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(converted))
      return failure();
    CapturedLValue &base = destination.children.front();
    FailureOr<Value> currentContainer = loadCapturedLValue(base, location);
    if (failed(currentContainer))
      return failure();
    CapturedLValue createBase = base;
    CapturedLValue existingBase = base;
    Value isNull = sim::SimManagedIsNullOp::create(
        builder, location, builder.getI1Type(), *currentContainer);
    Block *create = addBlock();
    Block *existing = addBlock();
    Block *resume = addBlock();
    resume->addArgument((*currentContainer).getType(), location);
    cf::CondBranchOp::create(builder, location, isNull, create, ValueRange{},
                             existing, ValueRange{});
    setCurrent(create);
    auto arrayType = cast<sim::AssocArrayType>((*currentContainer).getType());
    FailureOr<Value> allocated = createAssocArray(arrayType, location);
    if (failed(allocated))
      return failure();
    sim::SimAssocWriteOp::create(builder, location, *allocated,
                                 destination.index, *converted);
    if (failed(writeCapturedLValue(createBase, *allocated, false, false,
                                   location)))
      return failure();
    cf::BranchOp::create(builder, location, resume, ValueRange{*allocated});

    setCurrent(existing);
    bool directStorage =
        base.kind == CapturedLValue::Kind::Reference &&
        isa<sim::RefType, sim::ManagedRefType, sim::ArgumentRefType>(
            base.reference.getType());
    Value updated = directStorage
                        ? *currentContainer
                        : Value(sim::SimContainerCloneOp::create(
                              builder, location, (*currentContainer).getType(),
                              *currentContainer));
    sim::SimAssocWriteOp::create(builder, location, updated, destination.index,
                                 *converted);
    if (!directStorage && failed(writeCapturedLValue(existingBase, updated,
                                                     false, false, location)))
      return failure();
    cf::BranchOp::create(builder, location, resume, ValueRange{updated});
    setCurrent(resume);
    destination.container = resume->getArgument(0);
    return success();
  }
  case CapturedLValue::Kind::AggregateElement: {
    if (destination.children.size() != 1)
      return failure();
    CapturedLValue &base = destination.children.front();
    if (!nonblocking && base.kind == CapturedLValue::Kind::Reference &&
        isa<sim::RefType>(base.reference.getType()) &&
        !isa<sim::UnpackedUnionType>(base.type) &&
        !containsSequentialContainer(destination.type) &&
        (!sim::isManagedHandleType(destination.type) ||
         isa<sim::StringType>(destination.type))) {
      // A fixed aggregate member backed by ordinary storage is itself a
      // writable variable. Preserve that subelement identity instead of
      // rebuilding and storing the whole aggregate: IEEE 1800-2017 10.3.2
      // permits separate continuous assignments to disjoint members, and a
      // direct store is also the compact path for procedural member writes.
      CapturedLValue selected;
      selected.kind = CapturedLValue::Kind::Reference;
      selected.semanticNode = destination.semanticNode;
      selected.type = destination.type;
      selected.reference = sim::SimRefSubelementOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), destination.type),
          base.reference,
          builder.getDenseI64ArrayAttr(
              {static_cast<int64_t>(destination.ordinal)}));
      return writeCapturedLValue(selected, value, sourceSigned, false, location,
                                 delay);
    }
    if (nonblocking && base.kind == CapturedLValue::Kind::Reference) {
      if (auto array = dyn_cast<sim::UnpackedArrayType>(base.type)) {
        int64_t sourceIndex = array.getLeft() <= array.getRight()
                                  ? array.getLeft() + destination.ordinal
                                  : array.getLeft() - destination.ordinal;
        CapturedLValue selected;
        selected.kind = CapturedLValue::Kind::AggregateDynamicElement;
        selected.semanticNode = destination.semanticNode;
        selected.type = destination.type;
        selected.index =
            arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                      builder.getI64IntegerAttr(sourceIndex));
        selected.children.push_back(base);
        return writeCapturedLValue(selected, value, sourceSigned, true,
                                   location, delay);
      }
    }
    FailureOr<Value> aggregate = loadCapturedLValue(base, location);
    FailureOr<Value> replacement =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(aggregate) || failed(replacement) ||
        sim::getAggregateElementType((*aggregate).getType(),
                                     destination.ordinal) != destination.type)
      return failure();
    Value updated = sim::SimAggregateInsertOp::create(
        builder, location, (*aggregate).getType(), *aggregate, *replacement,
        destination.ordinal);
    return writeCapturedLValue(base, updated, false, nonblocking, location,
                               delay);
  }
  case CapturedLValue::Kind::AggregateDynamicElement: {
    if (destination.children.size() != 1)
      return failure();
    CapturedLValue &base = destination.children.front();
    FailureOr<Value> replacement =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(replacement))
      return failure();
    if (nonblocking && base.kind == CapturedLValue::Kind::Reference) {
      auto array = dyn_cast<sim::UnpackedArrayType>(base.type);
      if (!array)
        return failure();
      FailureOr<Value> owner =
          toArgumentReference(base.reference, base.type, location);
      FailureOr<ContainerElementDescriptor> descriptor =
          describeContainerElement(array.getElementType(), location);
      std::optional<uint64_t> span =
          sim::getProvenanceSpan(array.getElementType());
      if (failed(owner) || failed(descriptor) || !span)
        return failure();
      uint64_t elementSpan = *span;
      if (elementSpan == 0)
        return failure();
      FailureOr<Value> index = convert(destination.index, builder.getI64Type(),
                                       true, location, true);
      if (failed(index))
        return failure();
      Type pathType =
          sim::ReferencePathType::get(function.getContext(), destination.type);
      Value path = sim::SimReferencePathAggregateElementOp::create(
          builder, location, pathType,
          function.getBody().front().getArgument(0), *owner, *index,
          array.getLeft(), array.getRight(), elementSpan, descriptor->typeID,
          descriptor->kind, descriptor->flags, descriptor->valueSize,
          descriptor->alignment, descriptor->bitWidth,
          builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
          builder.getDenseI32ArrayAttr(descriptor->traceKinds));
      sim::SimReferencePathNBAEnqueueOp::create(builder, location, *replacement,
                                                path, delay);
      return success();
    }
    FailureOr<Value> aggregate = loadCapturedLValue(base, location);
    if (failed(aggregate) || failed(replacement) ||
        sim::getAggregateElementType((*aggregate).getType(), 0) !=
            destination.type)
      return failure();
    Value updated = sim::SimArrayDynInsertOp::create(
        builder, location, (*aggregate).getType(), *aggregate, *replacement,
        destination.index);
    return writeCapturedLValue(base, updated, false, nonblocking, location,
                               delay);
  }
  case CapturedLValue::Kind::AggregateSlice: {
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(converted))
      return failure();
    if (!destination.indices.empty()) {
      if (destination.children.size() != 1 ||
          destination.indices.size() !=
              sim::getAggregateNumElements(destination.type))
        return failure();
      CapturedLValue &base = destination.children.front();
      FailureOr<Value> aggregate = loadCapturedLValue(base, location);
      auto source =
          succeeded(aggregate)
              ? dyn_cast<sim::UnpackedArrayType>((*aggregate).getType())
              : sim::UnpackedArrayType{};
      if (!source)
        return failure();
      if (nonblocking && base.kind == CapturedLValue::Kind::Reference) {
        for (auto [ordinal, index] : llvm::enumerate(destination.indices)) {
          Type elementType =
              sim::getAggregateElementType(destination.type, ordinal);
          Value element = sim::SimAggregateExtractOp::create(
              builder, location, elementType, *converted, ordinal);
          CapturedLValue selected;
          selected.kind = CapturedLValue::Kind::AggregateDynamicElement;
          selected.semanticNode = destination.semanticNode;
          selected.type = source.getElementType();
          selected.index = index;
          selected.children.push_back(base);
          if (failed(writeCapturedLValue(selected, element, false, true,
                                         location, delay)))
            return failure();
        }
        return success();
      }
      Value updated = *aggregate;
      for (auto [ordinal, index] : llvm::enumerate(destination.indices)) {
        Type elementType =
            sim::getAggregateElementType(destination.type, ordinal);
        Value element = sim::SimAggregateExtractOp::create(
            builder, location, elementType, *converted, ordinal);
        FailureOr<Value> replacement =
            convert(element, source.getElementType(), false, location);
        if (failed(replacement))
          return failure();
        updated = sim::SimArrayDynInsertOp::create(
            builder, location, source, updated, *replacement, index);
      }
      return writeCapturedLValue(base, updated, false, nonblocking, location,
                                 delay);
    }
    if (destination.children.size() !=
        sim::getAggregateNumElements(destination.type))
      return failure();
    for (auto [ordinal, child] : llvm::enumerate(destination.children)) {
      Type elementType =
          sim::getAggregateElementType(destination.type, ordinal);
      Value element = sim::SimAggregateExtractOp::create(
          builder, location, elementType, *converted, ordinal);
      if (failed(writeCapturedLValue(child, element, false, nonblocking,
                                     location, delay)))
        return failure();
    }
    return success();
  }
  case CapturedLValue::Kind::StringCharacter: {
    if (!destination.reference) {
      return failure();
    }
    FailureOr<Value> string = loadReference(destination.reference, location);
    FailureOr<Value> character =
        convert(value, builder.getI8Type(), sourceSigned, location);
    if (failed(string) || failed(character))
      return failure();
    if (nonblocking) {
      FailureOr<Value> owner = toArgumentReference(
          destination.reference, sim::StringType::get(function.getContext()),
          location);
      if (failed(owner))
        return failure();
      Type pathType = sim::ReferencePathType::get(function.getContext(),
                                                  builder.getI8Type());
      Value path = sim::SimReferencePathStringCharacterOp::create(
          builder, location, pathType,
          function.getBody().front().getArgument(0), *string, destination.index,
          *owner);
      sim::SimReferencePathNBAEnqueueOp::create(builder, location, *character,
                                                path, delay);
      return success();
    }
    Value updated = sim::SimStringPutcOp::create(
        builder, location, sim::StringType::get(function.getContext()), *string,
        destination.index, *character);
    return storeReference(destination.reference, updated, location);
  }
  case CapturedLValue::Kind::Concatenation: {
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(converted))
      return failure();
    FailureOr<Value> scalar = toPackedScalar(*converted, location);
    if (failed(scalar))
      return failure();
    std::optional<unsigned> totalWidth =
        sim::getPackedWidth((*scalar).getType());
    if (!totalWidth)
      return failure();
    uint64_t trailing = *totalWidth;
    for (auto [childIndex, child] : llvm::enumerate(destination.children)) {
      for (CapturedLValue &previous :
           MutableArrayRef(destination.children).take_front(childIndex))
        propagateCapturedContainers(previous, child);
      std::optional<unsigned> childWidth = sim::getPackedWidth(child.type);
      if (!childWidth || *childWidth > trailing) {
        emitError(location) << "concatenation lvalue width is inconsistent";
        return failure();
      }
      trailing -= *childWidth;
      Value part;
      if (isa<sim::LogicType>((*scalar).getType())) {
        auto selected = sim::LogicType::get(function.getContext(), *childWidth);
        part =
            sim::SimLogicExtractOp::create(builder, location, selected, *scalar,
                                           builder.getI64IntegerAttr(trailing));
      } else {
        auto integer = dyn_cast<IntegerType>((*scalar).getType());
        if (!integer)
          return failure();
        Value amount = arith::ConstantOp::create(
            builder, location, integer,
            builder.getIntegerAttr(integer, trailing));
        Value shifted =
            arith::ShRUIOp::create(builder, location, *scalar, amount);
        auto selected = IntegerType::get(function.getContext(), *childWidth);
        part = selected == integer ? shifted
                                   : Value(arith::TruncIOp::create(
                                         builder, location, selected, shifted));
      }
      if (failed(writeCapturedLValue(child, part, false, nonblocking, location,
                                     delay)))
        return failure();
    }
    if (trailing != 0) {
      emitError(location) << "concatenation lvalue does not consume its value";
      return failure();
    }
    return success();
  }
  }
  llvm_unreachable("unknown captured lvalue kind");
}

void UnitLowering::appendCapturedValues(const CapturedLValue &destination,
                                        SmallVectorImpl<Value> &values) {
  if (destination.kind == CapturedLValue::Kind::Reference ||
      destination.kind == CapturedLValue::Kind::PackedDynamicSlice ||
      destination.kind == CapturedLValue::Kind::StringCharacter)
    values.push_back(destination.reference);
  for (const CapturedLValue &child : destination.children)
    appendCapturedValues(child, values);
  if (destination.kind == CapturedLValue::Kind::ContainerElement ||
      destination.kind == CapturedLValue::Kind::AssociativeElement) {
    values.push_back(destination.container);
    values.push_back(destination.index);
  } else if (destination.kind == CapturedLValue::Kind::StringCharacter ||
             destination.kind == CapturedLValue::Kind::PackedDynamicSlice ||
             destination.kind ==
                 CapturedLValue::Kind::AggregateDynamicElement) {
    values.push_back(destination.index);
  } else if (destination.kind == CapturedLValue::Kind::PackedValueSlice &&
             destination.index) {
    values.push_back(destination.index);
  }
  if (destination.kind == CapturedLValue::Kind::ContainerSlice) {
    values.push_back(destination.container);
    values.push_back(destination.index);
    values.push_back(destination.limit);
  }
  if (destination.kind == CapturedLValue::Kind::AggregateSlice)
    llvm::append_range(values, destination.indices);
}

LogicalResult UnitLowering::replaceCapturedValues(CapturedLValue &destination,
                                                  ValueRange values,
                                                  unsigned &next) {
  if (destination.kind == CapturedLValue::Kind::Reference ||
      destination.kind == CapturedLValue::Kind::PackedDynamicSlice ||
      destination.kind == CapturedLValue::Kind::StringCharacter) {
    if (next >= values.size())
      return failure();
    destination.reference = values[next++];
  }
  for (CapturedLValue &child : destination.children) {
    if (failed(replaceCapturedValues(child, values, next)))
      return failure();
  }
  if (destination.kind == CapturedLValue::Kind::ContainerElement ||
      destination.kind == CapturedLValue::Kind::AssociativeElement) {
    if (next > values.size() || values.size() - next < 2)
      return failure();
    destination.container = values[next++];
    destination.index = values[next++];
  } else if (destination.kind == CapturedLValue::Kind::StringCharacter ||
             destination.kind == CapturedLValue::Kind::PackedDynamicSlice ||
             destination.kind ==
                 CapturedLValue::Kind::AggregateDynamicElement) {
    if (next >= values.size())
      return failure();
    destination.index = values[next++];
  } else if (destination.kind == CapturedLValue::Kind::PackedValueSlice &&
             destination.index) {
    if (next >= values.size())
      return failure();
    destination.index = values[next++];
  }
  if (destination.kind == CapturedLValue::Kind::AggregateSlice) {
    if (next > values.size() ||
        destination.indices.size() > values.size() - next)
      return failure();
    for (Value &index : destination.indices)
      index = values[next++];
  }
  if (destination.kind == CapturedLValue::Kind::ContainerSlice) {
    if (next > values.size() || values.size() - next < 3)
      return failure();
    destination.container = values[next++];
    destination.index = values[next++];
    destination.limit = values[next++];
  }
  return success();
}

LogicalResult UnitLowering::writeLValue(Operation *destination, Value value,
                                        bool sourceSigned, bool nonblocking,
                                        Location location, Value delay) {
  FailureOr<CapturedLValue> captured = captureLValue(destination, location);
  if (failed(captured))
    return failure();
  return writeCapturedLValue(*captured, value, sourceSigned, nonblocking,
                             location, delay);
}

FailureOr<Value> UnitLowering::lowerClockingOutputTarget(
    Operation *destination, Operation *clockingVariable, Value target) {
  if (destination == clockingVariable)
    return target;
  assert(!lvalueExpressionCaptures.contains(clockingVariable));
  lvalueExpressionCaptures[clockingVariable] = target;
  FailureOr<Value> selected = lowerExpression(destination, /*lvalue=*/true);
  lvalueExpressionCaptures.erase(clockingVariable);
  return selected;
}

LogicalResult UnitLowering::emitCapturedClockingOutputDrive(
    Operation *destination, CapturedLValue &target, Value clock, Value value,
    Location location, semantic::SVCycleDelayControlOp cycleDelay,
    Value edgeSkewClock, unsigned &component) {
  if (target.kind == CapturedLValue::Kind::Reference) {
    FailureOr<Value> converted = convert(value, target.type, false, location,
                                         isSignedNode(target.semanticNode));
    if (failed(converted))
      return failure();
    return emitClockingOutputDrive(
        destination, target.reference, clock, *converted,
        /*virtualInterface=*/false, location, Value{}, edgeSkewClock,
        cycleDelay, ++component);
  }
  if (target.kind != CapturedLValue::Kind::Concatenation ||
      target.children.empty())
    return emitError(location)
           << "clocking output expression has no first-class lvalue target";

  FailureOr<Value> converted = convert(value, target.type, false, location,
                                       isSignedNode(target.semanticNode));
  FailureOr<Value> scalar = succeeded(converted)
                                ? toPackedScalar(*converted, location)
                                : FailureOr<Value>(failure());
  std::optional<unsigned> totalWidth =
      succeeded(scalar) ? sim::getPackedWidth((*scalar).getType())
                        : std::nullopt;
  if (failed(scalar) || !totalWidth)
    return failure();
  uint64_t trailing = *totalWidth;
  for (CapturedLValue &child : target.children) {
    std::optional<unsigned> childWidth = sim::getPackedWidth(child.type);
    if (!childWidth || *childWidth > trailing)
      return emitError(location)
             << "clocking output concatenation width is inconsistent";
    trailing -= *childWidth;
    Value part;
    if (auto logic = dyn_cast<sim::LogicType>((*scalar).getType())) {
      part = sim::SimLogicExtractOp::create(
          builder, location,
          sim::LogicType::get(function.getContext(), *childWidth), *scalar,
          builder.getI64IntegerAttr(trailing));
    } else {
      auto integer = dyn_cast<IntegerType>((*scalar).getType());
      if (!integer)
        return failure();
      Value amount =
          arith::ConstantOp::create(builder, location, integer,
                                    builder.getIntegerAttr(integer, trailing));
      Value shifted =
          arith::ShRUIOp::create(builder, location, *scalar, amount);
      Type selected = builder.getIntegerType(*childWidth);
      part = selected == integer ? shifted
                                 : Value(arith::TruncIOp::create(
                                       builder, location, selected, shifted));
    }
    if (failed(emitCapturedClockingOutputDrive(destination, child, clock, part,
                                               location, cycleDelay,
                                               edgeSkewClock, component)))
      return failure();
  }
  if (trailing)
    return emitError(location)
           << "clocking output concatenation does not consume its value";
  return success();
}

LogicalResult UnitLowering::lowerClockingOutputAssignment(
    semantic::SVMemberAccessExpressionOp clockingVariable,
    Operation *destination, Value value, Location location,
    semantic::SVCycleDelayControlOp cycleDelay) {
  SmallVector<Operation *> children = getChildren(clockingVariable);
  bool hasIff =
      clockingVariable->hasAttr("virtual_interface_clock_event_has_iff");
  size_t expectedChildren = hasIff ? 3 : 1;
  if (children.size() != expectedChildren)
    return emitError(location) << "clocking output has no interface receiver";
  FailureOr<Value> interface = lowerExpression(children.front());
  FailureOr<Type> elementType = getNormalizedSemanticType(clockingVariable);
  if (failed(interface) || failed(elementType))
    return failure();
  FailureOr<Value> target = lowerVirtualInterfaceMember(
      clockingVariable, *interface, *elementType, /*lvalue=*/true);
  FailureOr<Value> clock =
      lowerVirtualInterfaceClock(clockingVariable, *interface);
  if (failed(target) || failed(clock))
    return failure();
  target = lowerClockingOutputTarget(destination, clockingVariable, *target);
  if (failed(target))
    return failure();
  Value edgeSkewClock;
  if (auto rawMember = clockingVariable->getAttrOfType<StringAttr>(
          "virtual_interface_clock_raw_member")) {
    FailureOr<Value> raw =
        lowerVirtualInterfaceSignal(*interface, rawMember.getValue(), location);
    if (failed(raw))
      return failure();
    edgeSkewClock = *raw;
  }
  return emitClockingOutputDrive(clockingVariable, *target, *clock, value,
                                 /*virtualInterface=*/true, location,
                                 *interface, edgeSkewClock, cycleDelay);
}

LogicalResult UnitLowering::lowerStaticClockingOutputAssignment(
    Operation *clockingVariable, Operation *destination, Value value,
    Location location, semantic::SVCycleDelayControlOp cycleDelay) {
  auto direction =
      clockingVariable->getAttrOfType<semantic::SVArgumentDirectionAttr>(
          clockingAccessDirectionAttrName);
  if (!direction || direction.getValue() == semantic::SVArgumentDirection::In)
    return emitError(location) << "cannot write an input clocking variable";
  auto sourcePath =
      clockingVariable->getAttrOfType<StringAttr>(clockingSourcePathAttrName);
  auto clockPath =
      clockingVariable->getAttrOfType<StringAttr>(clockingEventPathAttrName);
  if (!clockPath)
    return emitError(location) << "clocking output has no addressable clock";
  FailureOr<Value> target = failure();
  std::optional<CapturedLValue> expressionTarget;
  if (sourcePath) {
    target = lowerReferencedValue(clockingVariable, sourcePath.getValue(),
                                  /*lvalue=*/true);
  } else {
    auto reference =
        clockingVariable->getAttrOfType<SymbolRefAttr>("referenced_symbol");
    semantic::SVClockVarSymbolOp declaration;
    if (reference)
      clockingVariable->getParentOfType<ModuleOp>().walk(
          [&](semantic::SVClockVarSymbolOp candidate) {
            if (!declaration && candidate.getSymName() ==
                                    reference.getLeafReference().getValue())
              declaration = candidate;
          });
    SmallVector<Operation *> declarationChildren =
        declaration ? getChildren(declaration) : SmallVector<Operation *>{};
    if (declarationChildren.size() != 1)
      return emitError(location)
             << "clocking output has no addressable output expression";
    FailureOr<CapturedLValue> captured =
        captureLValue(declarationChildren.front(), location);
    if (failed(captured))
      return failure();
    expressionTarget = std::move(*captured);
  }
  // A net output has a node-specific driver binding. Resolve the separate
  // event clock by path so that binding cannot substitute for the clock.
  Value clock = lvalues.lookup(clockPath.getValue());
  if (!clock) {
    Value event = values.lookup(clockPath.getValue());
    if (event && isa<sim::EventType>(event.getType()))
      clock = event;
  }
  if ((!expressionTarget && failed(target)) || !clock) {
    if (!clock)
      emitError(location) << "clocking output has no frozen clock binding: "
                          << clockPath.getValue();
    return failure();
  }
  if (!expressionTarget) {
    target = lowerClockingOutputTarget(destination, clockingVariable, *target);
    if (failed(target))
      return failure();
  } else if (destination != clockingVariable) {
    return emitError(location)
           << "a selected clocking output requires a direct output lvalue";
  }
  Value edgeSkewClock;
  if (auto rawPath = clockingVariable->getAttrOfType<StringAttr>(
          clockingEventRawPathAttrName)) {
    edgeSkewClock = lvalues.lookup(rawPath.getValue());
    if (!edgeSkewClock)
      edgeSkewClock = values.lookup(rawPath.getValue());
    if (!edgeSkewClock)
      return emitError(location)
             << "clocking output has no raw edge clock binding: "
             << rawPath.getValue();
  }
  if (expressionTarget) {
    unsigned component = 0;
    Operation *cycleCountExpression = nullptr;
    if (cycleDelay) {
      SmallVector<Operation *> cycleChildren = getChildren(cycleDelay);
      if (!cycleChildren.empty()) {
        cycleCountExpression = cycleChildren.front();
        FailureOr<Value> count = lowerExpression(cycleCountExpression);
        if (failed(count))
          return failure();
        expressionCaptures[cycleCountExpression] = *count;
      }
    }
    LogicalResult result = emitCapturedClockingOutputDrive(
        clockingVariable, *expressionTarget, clock, value, location, cycleDelay,
        edgeSkewClock, component);
    if (cycleCountExpression)
      expressionCaptures.erase(cycleCountExpression);
    return result;
  }
  return emitClockingOutputDrive(clockingVariable, *target, clock, value,
                                 /*virtualInterface=*/false, location, Value{},
                                 edgeSkewClock, cycleDelay);
}

LogicalResult UnitLowering::emitClockingOutputDrive(
    Operation *destination, Value target, Value clock, Value value,
    bool virtualInterface, Location location, Value virtualInterfaceHandle,
    Value edgeSkewClock, semantic::SVCycleDelayControlOp cycleDelay,
    unsigned component) {
  auto targetRef = dyn_cast<sim::RefType>(target.getType());
  auto targetDriver = dyn_cast<sim::DriverType>(target.getType());
  if (!targetRef && !targetDriver)
    return emitError(location)
           << "clocking output target is not variable storage or a net driver";

  StringRef eventEdgeName = virtualInterface
                                ? "virtual_interface_clock_event_edge"
                                : StringRef(clockingEventEdgeAttrName);
  StringRef eventIffName = virtualInterface
                               ? "virtual_interface_clock_event_has_iff"
                               : StringRef(clockingEventHasIffAttrName);
  StringRef rawEventEdgeName = virtualInterface
                                   ? "virtual_interface_clock_raw_event_edge"
                                   : StringRef(clockingEventRawEdgeAttrName);
  StringRef skewEdgeName = virtualInterface
                               ? "virtual_interface_clock_output_skew_edge"
                               : StringRef(clockingOutputSkewEdgeAttrName);
  StringRef skewOneStepName =
      virtualInterface ? "virtual_interface_clock_output_skew_one_step"
                       : StringRef(clockingOutputSkewOneStepAttrName);
  StringRef skewDelayName = virtualInterface
                                ? "virtual_interface_clock_output_skew_delay"
                                : StringRef(clockingOutputSkewDelayAttrName);
  StringRef skewDelayIsRealName =
      virtualInterface ? "virtual_interface_clock_output_skew_delay_is_real"
                       : StringRef(clockingOutputSkewDelayIsRealAttrName);
  StringRef timeUnitName = virtualInterface
                               ? "virtual_interface_clock_time_unit_fs"
                               : StringRef(clockingTimeUnitAttrName);
  StringRef timePrecisionName =
      virtualInterface ? "virtual_interface_clock_time_precision_fs"
                       : StringRef(clockingTimePrecisionAttrName);
  auto eventEdge =
      destination->getAttrOfType<semantic::EdgeKindAttr>(eventEdgeName);
  auto skewEdge =
      destination->getAttrOfType<semantic::EdgeKindAttr>(skewEdgeName);
  auto rawEventEdge =
      destination->getAttrOfType<semantic::EdgeKindAttr>(rawEventEdgeName);
  bool hasIff = destination->hasAttr(eventIffName);
  if (!eventEdge) {
    emitError(location) << "clocking output has no supported static event";
    return failure();
  }
  if (destination->hasAttr(skewOneStepName))
    return emitError(location) << "#1step is not a valid clocking output skew";
  semantic::EdgeKind selectedEdge =
      skewEdge && skewEdge.getValue() != semantic::EdgeKind::Change
          ? skewEdge.getValue()
          : eventEdge.getValue();
  semantic::EdgeKind baseSignalEdge =
      rawEventEdge ? rawEventEdge.getValue() : eventEdge.getValue();
  bool distinctEdgeSkew = skewEdge &&
                          skewEdge.getValue() != semantic::EdgeKind::Change &&
                          skewEdge.getValue() != baseSignalEdge;
  Value selectedEdgeClock = edgeSkewClock ? edgeSkewClock : clock;
  if (distinctEdgeSkew && isa<sim::EventType>(selectedEdgeClock.getType()))
    return emitError(location)
           << "an edge-only output skew requires a single-signal clocking "
              "event";
  if (hasIff && distinctEdgeSkew)
    return emitError(location)
           << "legacy clocking outputs with iff cannot use a distinct edge "
              "skew";
  sim::EdgeKind edge = static_cast<sim::EdgeKind>(selectedEdge);
  std::optional<Value> currentOccurrence = getCurrentClockingOccurrence(
      current, virtualInterface ? Value{} : clock,
      static_cast<sim::EdgeKind>(baseSignalEdge), hasIff);
  bool alwaysSynchronized = currentOccurrence && !*currentOccurrence;
  Value synchronizationPredicate =
      currentOccurrence ? *currentOccurrence : Value{};
  std::optional<unsigned> trackedClockWidth;
  std::optional<uint64_t> trackedClockDescriptor;
  bool trackRuntimeOccurrence = false;
  // IEEE 1800-2017 14.16 uses the current clocking event when a zero-skew
  // drive executes anywhere in that event's time step. Static CFG provenance
  // handles direct @(cb) continuations above. For an indirect wakeup in the
  // same slot, register the exact descriptor in the root initializer and let
  // the outlined helper query its sparse occurrence history before waiting.
  if (!currentOccurrence && !virtualInterface && !hasIff && !cycleDelay &&
      !distinctEdgeSkew) {
    Type clockElement;
    if (auto reference = dyn_cast<sim::RefType>(clock.getType()))
      clockElement = reference.getElementType();
    else if (auto net = dyn_cast<sim::NetType>(clock.getType()))
      clockElement = net.getElementType();
    trackedClockWidth =
        clockElement ? sim::getPackedWidth(clockElement) : std::nullopt;
    auto clockPath =
        destination->getAttrOfType<StringAttr>(clockingEventPathAttrName);
    auto descriptor = clockPath ? descriptorIDs.find(clockPath.getValue())
                                : descriptorIDs.end();
    if (trackedClockWidth && *trackedClockWidth != 0 && clockPath &&
        descriptor != descriptorIDs.end()) {
      trackedClockDescriptor = descriptor->second;
      trackRuntimeOccurrence = true;
    }
  }
  struct ObserverPlan {
    FlatSymbolRefAttr evaluator;
    sim::ObserverType type;
    IntegerAttr captureCount;
    SmallVector<Value> values;
    SmallVector<unsigned> argumentIndices;
    bool eventPrimary = false;
  };
  auto saveObserverPlan = [&](Value observer) -> ObserverPlan {
    auto binding = observer.getDefiningOp<sim::SimObserverBindOp>();
    assert(binding && "bound observer must be produced by observer.bind");
    ObserverPlan plan{binding.getEvaluatorAttr(),
                      cast<sim::ObserverType>(observer.getType()),
                      binding.getCaptureCountAttr(),
                      SmallVector<Value>(binding.getValues()),
                      {},
                      binding->hasAttr(observerEventPrimaryAttrName)};
    binding.erase();
    return plan;
  };
  std::optional<ObserverPlan> primaryObserver;
  std::optional<ObserverPlan> conditionObserver;
  if (hasIff && (!alwaysSynchronized || cycleDelay)) {
    SmallVector<Operation *> children = getChildren(destination);
    size_t expectedChildren = virtualInterface ? 3 : 2;
    if (children.size() != expectedChildren)
      return emitError(location)
             << "clocking output with iff has no frozen clock and condition "
                "expressions";
    FailureOr<Value> primary = failure();
    FailureOr<Value> condition = failure();
    if (virtualInterface) {
      auto access = dyn_cast<semantic::SVMemberAccessExpressionOp>(destination);
      if (!access || !virtualInterfaceHandle)
        return emitError(location)
               << "virtual clocking output has no selected interface";
      primary = bindVirtualClockingObserver(children[1], access,
                                            virtualInterfaceHandle, clock);
      condition = bindVirtualClockingObserver(children[2], access,
                                              virtualInterfaceHandle, clock);
    } else {
      primary = bindObserver(children[0]);
      condition = bindObserver(children[1]);
    }
    if (failed(primary) || failed(condition))
      return failure();
    primaryObserver = saveObserverPlan(*primary);
    conditionObserver = saveObserverPlan(*condition);
  }

  Value cycleClock;
  Value cycleCount;
  bool cycleCountSigned = false;
  bool cycleHasIff = false;
  semantic::EdgeKind cycleEdge = semantic::EdgeKind::Change;
  std::optional<ObserverPlan> cyclePrimaryObserver;
  std::optional<ObserverPlan> cycleConditionObserver;
  bool cycleMatchesOutput = false;
  if (cycleDelay) {
    SmallVector<Operation *> cycleChildren = getChildren(cycleDelay);
    cycleHasIff = cycleDelay->hasAttr(clockingEventHasIffAttrName);
    size_t expectedChildren = cycleHasIff ? 3 : 1;
    if (cycleChildren.size() != expectedChildren)
      return emitError(location)
             << "clocking output cycle delay has no frozen event";
    auto cyclePath =
        cycleDelay->getAttrOfType<StringAttr>(clockingEventPathAttrName);
    auto cycleEventEdge = cycleDelay->getAttrOfType<semantic::EdgeKindAttr>(
        clockingEventEdgeAttrName);
    if (!cyclePath || !cycleEventEdge)
      return emitError(location)
             << "clocking output cycle delay has no supported event";
    FailureOr<Value> loweredClock =
        lowerReferencedValue(cycleDelay, cyclePath.getValue(), /*lvalue=*/true);
    FailureOr<Value> loweredCount = lowerExpression(cycleChildren.front());
    FailureOr<Value> scalarCount = succeeded(loweredCount)
                                       ? toPackedScalar(*loweredCount, location)
                                       : FailureOr<Value>(failure());
    std::optional<unsigned> countWidth =
        succeeded(scalarCount) ? sim::getPackedWidth((*scalarCount).getType())
                               : std::nullopt;
    if (failed(loweredClock) || failed(scalarCount) || !countWidth ||
        *countWidth == 0)
      return failure();
    Type countType = builder.getIntegerType(*countWidth);
    FailureOr<Value> normalizedCount = convert(
        *scalarCount, countType, isSignedNode(cycleChildren.front()), location);
    if (failed(normalizedCount))
      return failure();
    cycleClock = *loweredClock;
    cycleCount = *normalizedCount;
    cycleCountSigned = isSignedNode(cycleChildren.front());
    cycleEdge = cycleEventEdge.getValue();
    if (cycleHasIff) {
      FailureOr<Value> primary = bindObserver(cycleChildren[1]);
      FailureOr<Value> condition = bindObserver(cycleChildren[2]);
      if (failed(primary) || failed(condition))
        return failure();
      cyclePrimaryObserver = saveObserverPlan(*primary);
      cycleConditionObserver = saveObserverPlan(*condition);
    }
    cycleMatchesOutput = cycleClock == clock &&
                         cycleEdge == eventEdge.getValue() &&
                         cycleHasIff == hasIff;
  }

  uint64_t delayTicks = 0;
  if (auto spelling = destination->getAttrOfType<StringAttr>(skewDelayName)) {
    auto unit = destination->getAttrOfType<IntegerAttr>(timeUnitName);
    auto precision = destination->getAttrOfType<IntegerAttr>(timePrecisionName);
    if (!unit || !precision || unit.getValue().isZero() ||
        precision.getValue().isZero()) {
      emitError(location) << "clocking output has invalid time scale";
      return failure();
    }
    uint64_t unitFS = unit.getValue().getZExtValue();
    uint64_t precisionFS = precision.getValue().getZExtValue();
    auto isRealAttr = destination->getAttrOfType<BoolAttr>(skewDelayIsRealName);
    bool isReal = isRealAttr && isRealAttr.getValue();
    long double scaled = 0;
    if (isReal) {
      double amount = 0;
      if (spelling.getValue().getAsDouble(amount) || !std::isfinite(amount) ||
          amount < 0) {
        emitError(location)
            << "clocking output skew is not finite and nonnegative";
        return failure();
      }
      scaled = std::round(amount * static_cast<long double>(unitFS) /
                          static_cast<long double>(precisionFS)) *
               precisionFS;
    } else {
      FailureOr<ParsedConstant> parsed =
          parseSVInteger(spelling.getValue(), 64, location);
      if (failed(parsed))
        return failure();
      if (!parsed->unknown.isZero() || parsed->value.isNegative())
        scaled = 0;
      else
        scaled =
            static_cast<long double>(parsed->value.getZExtValue()) * unitFS;
    }
    // IEEE 1800-2017 14.4 counts a bare skew in the clocking scope's own time
    // units, which `scaled` has resolved to femtoseconds. Simulation delays are
    // counted in design-precision ticks.
    FailureOr<uint64_t> designPrecisionFS =
        designTimePrecisionFemtoseconds(location);
    if (failed(designPrecisionFS))
      return failure();
    scaled /= static_cast<long double>(*designPrecisionFS);
    if (scaled > std::numeric_limits<uint64_t>::max()) {
      emitError(location) << "clocking output skew exceeds simulation time";
      return failure();
    }
    delayTicks = static_cast<uint64_t>(scaled);
  }

  uint64_t node = destination->getAttrOfType<IntegerAttr>("node_id")
                      ? destination->getAttrOfType<IntegerAttr>("node_id")
                            .getValue()
                            .getZExtValue()
                      : 0;
  std::string identity =
      (function.getSymName() + ".$clocking_output." + Twine(node)).str();
  if (component)
    identity += (Twine(".") + Twine(component)).str();
  uint64_t codeUnitID = stableCodeUnitID(identity);
  auto clockingOutputPath =
      destination->getAttrOfType<StringAttr>("referenced_path");
  if (!clockingOutputPath)
    return emitError(location)
           << "clocking output has no stable clock-variable identity";
  uint64_t clockingOutputID = stableCodeUnitID(
      (Twine("clocking-output-resolution|") + clockingOutputPath.getValue())
          .str());
  uint64_t scopeID = 0;
  if (auto parentID = function.getCodeUnitId())
    for (sim::SimCodeUnitDeclOp declaration :
         function->getParentOfType<sim::SimDesignOp>()
             .getBody()
             .front()
             .getOps<sim::SimCodeUnitDeclOp>())
      if (declaration.getId() == *parentID) {
        scopeID = declaration.getScopeId();
        break;
      }

  OpBuilder outlineBuilder(function);
  outlineBuilder.setInsertionPoint(function);
  sim::SimCodeUnitDeclOp::create(
      outlineBuilder, location, codeUnitID, scopeID, sim::EntryKind::Fork,
      outlineBuilder.getStringAttr(identity),
      outlineBuilder.getStringAttr("clocking output drive"),
      outlineBuilder.getUnitAttr());
  MLIRContext *context = function.getContext();
  SmallVector<Type> inputs{function.getBody().front().getArgument(0).getType(),
                           target.getType(), clock.getType(), value.getType()};
  SmallVector<DictionaryAttr> argumentAttrs{
      captureMetadata(outlineBuilder, sim::CaptureKind::Context),
      captureMetadata(outlineBuilder, sim::CaptureKind::Formal),
      captureMetadata(outlineBuilder, sim::CaptureKind::Formal),
      captureMetadata(outlineBuilder, sim::CaptureKind::Value)};
  if (cycleDelay && !synchronizationPredicate)
    synchronizationPredicate =
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(alwaysSynchronized));
  std::optional<unsigned> synchronizationPredicateIndex;
  if (synchronizationPredicate) {
    synchronizationPredicateIndex = inputs.size();
    inputs.push_back(synchronizationPredicate.getType());
    argumentAttrs.push_back(
        captureMetadata(outlineBuilder, sim::CaptureKind::Value));
  }
  std::optional<unsigned> edgeSkewClockIndex;
  if (distinctEdgeSkew) {
    edgeSkewClockIndex = inputs.size();
    inputs.push_back(selectedEdgeClock.getType());
    argumentAttrs.push_back(
        captureMetadata(outlineBuilder, sim::CaptureKind::Formal));
  }
  std::optional<unsigned> cycleClockIndex;
  std::optional<unsigned> cycleCountIndex;
  if (cycleDelay) {
    cycleClockIndex = inputs.size();
    inputs.push_back(cycleClock.getType());
    argumentAttrs.push_back(
        captureMetadata(outlineBuilder, sim::CaptureKind::Formal));
    cycleCountIndex = inputs.size();
    inputs.push_back(cycleCount.getType());
    argumentAttrs.push_back(
        captureMetadata(outlineBuilder, sim::CaptureKind::Value));
  }
  SmallVector<std::pair<Value, unsigned>> observerArguments{{target, 1},
                                                            {clock, 2}};
  if (cycleDelay)
    observerArguments.emplace_back(cycleClock, *cycleClockIndex);
  SmallVector<Value> observerSpawnOperands;
  auto appendObserverArguments = [&](ObserverPlan &plan) {
    for (Value operand : plan.values) {
      auto existing = llvm::find_if(observerArguments, [&](const auto &entry) {
        return entry.first == operand;
      });
      if (existing != observerArguments.end()) {
        plan.argumentIndices.push_back(existing->second);
        continue;
      }
      plan.argumentIndices.push_back(inputs.size());
      observerArguments.emplace_back(operand, inputs.size());
      observerSpawnOperands.push_back(operand);
      inputs.push_back(operand.getType());
      argumentAttrs.push_back(
          captureMetadata(outlineBuilder, sim::CaptureKind::Formal));
    }
  };
  if (primaryObserver) {
    appendObserverArguments(*primaryObserver);
    appendObserverArguments(*conditionObserver);
  }
  if (cyclePrimaryObserver) {
    appendObserverArguments(*cyclePrimaryObserver);
    appendObserverArguments(*cycleConditionObserver);
  }
  SmallVector<NamedAttribute> attributes{
      outlineBuilder.getNamedAttr("code_unit_id",
                                  outlineBuilder.getI64IntegerAttr(codeUnitID)),
      outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
      outlineBuilder.getNamedAttr(
          "home_region",
          sim::EventRegionAttr::get(context, sim::EventRegion::Reactive)),
      outlineBuilder.getNamedAttr("domain", function.getDomainAttr()),
      outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                  outlineBuilder.getStringAttr(identity))};
  sim::SimFuncOp driver =
      sim::SimFuncOp::create(outlineBuilder, location, identity,
                             FunctionType::get(context, inputs, TypeRange{}),
                             sim::EntryKind::Fork, attributes, argumentAttrs);
  SymbolTable::setSymbolVisibility(driver, SymbolTable::Visibility::Private);
  Block &entry = driver.getBody().front();
  Block *cycleWait = cycleDelay ? new Block() : nullptr;
  Block *cycleResume = cycleDelay ? new Block() : nullptr;
  Block *cycleZero = cycleDelay ? new Block() : nullptr;
  Block *wait = alwaysSynchronized && !cycleDelay ? nullptr : new Block();
  Block *edgeWait = distinctEdgeSkew ? new Block() : nullptr;
  Block *drive = new Block();
  if (cycleWait) {
    cycleWait->addArgument(cycleCount.getType(), location);
    cycleResume->addArgument(cycleCount.getType(), location);
    driver.getBody().push_back(cycleWait);
    driver.getBody().push_back(cycleResume);
    driver.getBody().push_back(cycleZero);
  }
  if (wait)
    driver.getBody().push_back(wait);
  if (edgeWait)
    driver.getBody().push_back(edgeWait);
  driver.getBody().push_back(drive);
  Block *afterOccurrence = edgeWait ? edgeWait : drive;
  OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
  auto bindInDriver = [&](ObserverPlan &plan) {
    SmallVector<Value> operands;
    for (unsigned index : plan.argumentIndices)
      operands.push_back(entry.getArgument(index));
    auto binding = sim::SimObserverBindOp::create(entryBuilder, location,
                                                  plan.type, plan.evaluator,
                                                  operands, plan.captureCount);
    if (plan.eventPrimary)
      binding->setAttr(observerEventPrimaryAttrName,
                       entryBuilder.getUnitAttr());
    return binding.getResult();
  };
  Value localPrimaryObserver;
  Value localConditionObserver;
  if (primaryObserver) {
    localPrimaryObserver = bindInDriver(*primaryObserver);
    localConditionObserver = bindInDriver(*conditionObserver);
  }
  Value localCyclePrimaryObserver;
  Value localCycleConditionObserver;
  if (cyclePrimaryObserver) {
    localCyclePrimaryObserver = bindInDriver(*cyclePrimaryObserver);
    localCycleConditionObserver = bindInDriver(*cycleConditionObserver);
  }
  if (cycleDelay) {
    Value zero = arith::ConstantOp::create(
        entryBuilder, location, cycleCount.getType(),
        entryBuilder.getZeroAttr(cycleCount.getType()));
    Value positive = arith::CmpIOp::create(
        entryBuilder, location,
        cycleCountSigned ? arith::CmpIPredicate::sgt : arith::CmpIPredicate::ne,
        entry.getArgument(*cycleCountIndex), zero);
    cf::CondBranchOp::create(entryBuilder, location, positive, cycleWait,
                             ValueRange{entry.getArgument(*cycleCountIndex)},
                             cycleZero, ValueRange{});

    OpBuilder zeroBuilder = OpBuilder::atBlockEnd(cycleZero);
    cf::CondBranchOp::create(zeroBuilder, location,
                             entry.getArgument(*synchronizationPredicateIndex),
                             afterOccurrence, ValueRange{}, wait, ValueRange{});

    OpBuilder cycleWaitBuilder = OpBuilder::atBlockEnd(cycleWait);
    ValueRange forwarded{cycleWait->getArgument(0)};
    sim::EventRegionAttr reactive =
        sim::EventRegionAttr::get(context, sim::EventRegion::Reactive);
    sim::EdgeKind delayedEdge = static_cast<sim::EdgeKind>(cycleEdge);
    if (localCyclePrimaryObserver) {
      auto primaryType =
          cast<sim::ObserverType>(localCyclePrimaryObserver.getType());
      Value initial;
      Type clockType = cycleClock.getType();
      if (auto reference = dyn_cast<sim::RefType>(clockType))
        initial = sim::SimRefLoadOp::create(
            cycleWaitBuilder, location, reference.getElementType(),
            entry.getArgument(*cycleClockIndex));
      else if (auto net = dyn_cast<sim::NetType>(clockType))
        initial = sim::SimNetReadOp::create(
            cycleWaitBuilder, location, net.getElementType(),
            entry.getArgument(*cycleClockIndex));
      if (!initial)
        return emitError(location)
               << "clocking output cycle-delay clock is not directly "
                  "readable";
      if (initial.getType() != primaryType.getResultType())
        initial = sim::SimPackedFlattenOp::create(
            cycleWaitBuilder, location, primaryType.getResultType(), initial);
      sim::SimSuspendObserveOp::create(
          cycleWaitBuilder, location,
          ValueRange{localCyclePrimaryObserver, initial,
                     localCycleConditionObserver, cycleWait->getArgument(0)},
          1, ArrayRef<int32_t>{static_cast<int32_t>(delayedEdge)},
          ArrayRef<int32_t>{0}, sim::ContinuationSiteAttr{}, reactive,
          cycleResume);
    } else if (isa<sim::EventType>(cycleClock.getType())) {
      sim::SimSuspendEventOp::create(
          cycleWaitBuilder, location, entry.getArgument(*cycleClockIndex),
          forwarded, sim::ContinuationSiteAttr{}, reactive, cycleResume);
    } else if (delayedEdge == sim::EdgeKind::Change) {
      sim::SimSuspendChangeOp::create(
          cycleWaitBuilder, location, entry.getArgument(*cycleClockIndex),
          forwarded, sim::ContinuationSiteAttr{}, reactive, cycleResume);
    } else {
      sim::SimSuspendEdgeOp::create(cycleWaitBuilder, location, delayedEdge,
                                    entry.getArgument(*cycleClockIndex),
                                    forwarded, sim::ContinuationSiteAttr{},
                                    reactive, cycleResume);
    }

    OpBuilder resumeBuilder = OpBuilder::atBlockEnd(cycleResume);
    Value one = arith::ConstantOp::create(
        resumeBuilder, location, cycleCount.getType(),
        resumeBuilder.getIntegerAttr(cycleCount.getType(), 1));
    Value remaining = arith::SubIOp::create(resumeBuilder, location,
                                            cycleResume->getArgument(0), one);
    Value more = arith::CmpIOp::create(
        resumeBuilder, location, arith::CmpIPredicate::ne, remaining, zero);
    Block *completedCycle = cycleMatchesOutput ? afterOccurrence : wait;
    cf::CondBranchOp::create(resumeBuilder, location, more, cycleWait,
                             ValueRange{remaining}, completedCycle,
                             ValueRange{});
  } else if (wait) {
    if (synchronizationPredicate)
      cf::CondBranchOp::create(
          entryBuilder, location,
          entry.getArgument(*synchronizationPredicateIndex), afterOccurrence,
          ValueRange{}, wait, ValueRange{});
    else if (trackRuntimeOccurrence) {
      Value current = sim::SimClockingOutputCurrentOp::create(
          entryBuilder, location, entry.getArgument(2),
          static_cast<sim::EdgeKind>(baseSignalEdge), *trackedClockWidth,
          *trackedClockDescriptor);
      cf::CondBranchOp::create(entryBuilder, location, current, afterOccurrence,
                               wait);
    } else
      cf::BranchOp::create(entryBuilder, location, wait);
  }
  if (wait) {
    OpBuilder waitBuilder = OpBuilder::atBlockEnd(wait);
    sim::EdgeKind occurrenceEdge =
        static_cast<sim::EdgeKind>(eventEdge.getValue());
    if (localPrimaryObserver) {
      auto primaryType =
          cast<sim::ObserverType>(localPrimaryObserver.getType());
      Value initial;
      if (auto reference = dyn_cast<sim::RefType>(clock.getType()))
        initial = sim::SimRefLoadOp::create(waitBuilder, location,
                                            reference.getElementType(),
                                            entry.getArgument(2));
      else if (auto net = dyn_cast<sim::NetType>(clock.getType()))
        initial = sim::SimNetReadOp::create(
            waitBuilder, location, net.getElementType(), entry.getArgument(2));
      if (!initial)
        return emitError(location)
               << "clocking output clock is not directly readable";
      if (initial.getType() != primaryType.getResultType())
        initial = sim::SimPackedFlattenOp::create(
            waitBuilder, location, primaryType.getResultType(), initial);
      sim::SimSuspendObserveOp::create(
          waitBuilder, location,
          ValueRange{localPrimaryObserver, initial, localConditionObserver}, 1,
          ArrayRef<int32_t>{static_cast<int32_t>(occurrenceEdge)},
          ArrayRef<int32_t>{0}, sim::ContinuationSiteAttr{},
          sim::EventRegionAttr::get(context, sim::EventRegion::Reactive),
          afterOccurrence);
    } else if (isa<sim::EventType>(clock.getType())) {
      sim::SimSuspendEventOp::create(
          waitBuilder, location, entry.getArgument(2), ValueRange{},
          sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, afterOccurrence);
    } else if (occurrenceEdge == sim::EdgeKind::Change) {
      sim::SimSuspendChangeOp::create(
          waitBuilder, location, entry.getArgument(2), ValueRange{},
          sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, afterOccurrence);
    } else {
      sim::SimSuspendEdgeOp::create(waitBuilder, location, occurrenceEdge,
                                    entry.getArgument(2), ValueRange{},
                                    sim::ContinuationSiteAttr{},
                                    sim::EventRegionAttr{}, afterOccurrence);
    }
  } else if (!cycleDelay) {
    // A drive issued by a continuation of @(clocking_block) belongs to that
    // clocking occurrence. Waiting here would silently move it one cycle.
    cf::BranchOp::create(entryBuilder, location, afterOccurrence);
  }
  if (edgeWait) {
    OpBuilder edgeBuilder = OpBuilder::atBlockEnd(edgeWait);
    sim::SimSuspendEdgeOp::create(edgeBuilder, location, edge,
                                  entry.getArgument(*edgeSkewClockIndex),
                                  ValueRange{}, sim::ContinuationSiteAttr{},
                                  sim::EventRegionAttr{}, drive);
  }
  OpBuilder driveBuilder = OpBuilder::atBlockEnd(drive);
  Value delay;
  if (delayTicks)
    delay = sim::SimTimeConstantOp::create(
        driveBuilder, location, sim::TimeType::get(context),
        driveBuilder.getI64IntegerAttr(delayTicks));
  sim::SimNBAEnqueueOp::create(
      driveBuilder, location, entry.getArgument(3), entry.getArgument(1), delay,
      sim::NBASiteAttr{}, driveBuilder.getI64IntegerAttr(clockingOutputID));
  sim::SimReturnOp::create(driveBuilder, location, ValueRange{});
  driver->setAttr(sim::metadata::lowered, builder.getUnitAttr());
  Value processContext = function.getBody().front().getArgument(0);
  SmallVector<Value> spawnInputs{processContext, target, clock, value};
  if (synchronizationPredicate)
    spawnInputs.push_back(synchronizationPredicate);
  if (distinctEdgeSkew)
    spawnInputs.push_back(selectedEdgeClock);
  if (cycleDelay) {
    spawnInputs.push_back(cycleClock);
    spawnInputs.push_back(cycleCount);
  }
  llvm::append_range(spawnInputs, observerSpawnOperands);
  sim::SimSpawnOp::create(builder, location, driver.getSymNameAttr(),
                          spawnInputs, ArrayAttr{}, ArrayAttr{});
  return success();
}

FailureOr<Value> UnitLowering::readBitStreamValue(Value stream, Value start,
                                                  Type type,
                                                  Location location) {
  Type scalarType = sim::getPackedScalarType(type);
  std::optional<unsigned> width = sim::getPackedWidth(type);
  auto streamType = dyn_cast<sim::QueueType>(stream.getType());
  if (!scalarType || !width || *width == 0 || !streamType)
    return emitError(location)
               << "streaming assignment target is not a nonempty packed value",
           failure();
  auto i64Constant = [&](uint64_t value) -> Value {
    return arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                     builder.getI64IntegerAttr(value));
  };
  Value assembled;
  if (auto logicType = dyn_cast<sim::LogicType>(scalarType)) {
    Type planeType = IntegerType::get(function.getContext(), *width);
    assembled = sim::SimLogicConstantOp::create(
        builder, location, logicType, builder.getIntegerAttr(planeType, 0),
        builder.getIntegerAttr(planeType, 0));
  } else {
    assembled = arith::ConstantOp::create(
        builder, location, scalarType, builder.getIntegerAttr(scalarType, 0));
  }
  for (unsigned ordinal = 0; ordinal < *width; ++ordinal) {
    Value sourceIndex =
        arith::AddIOp::create(builder, location, start, i64Constant(ordinal));
    Value bit = sim::SimContainerReadOp::create(
        builder, location, streamType.getElementType(), stream, sourceIndex);
    unsigned low = *width - ordinal - 1;
    if (isa<sim::LogicType>(scalarType)) {
      FailureOr<Value> logicBit = toLogic(bit, location);
      if (failed(logicBit))
        return failure();
      assembled = sim::SimLogicInsertOp::create(builder, location, scalarType,
                                                assembled, *logicBit, low);
      continue;
    }
    FailureOr<Value> knownBit =
        convert(bit, builder.getI1Type(), false, location);
    if (failed(knownBit))
      return failure();
    Value placed = *knownBit;
    if (placed.getType() != scalarType)
      placed = arith::ExtUIOp::create(builder, location, scalarType, *knownBit);
    if (low) {
      Value amount =
          arith::ConstantOp::create(builder, location, scalarType,
                                    builder.getIntegerAttr(scalarType, low));
      placed = arith::ShLIOp::create(builder, location, placed, amount);
    }
    assembled = arith::OrIOp::create(builder, location, assembled, placed);
  }
  if (scalarType == type)
    return assembled;
  return sim::SimPackedUnflattenOp::create(builder, location, type, assembled)
      .getResult();
}

// IEEE 1800-2017 11.4.14 makes the bit-stream of an unpacked array or
// structure the concatenation of its elements' bit-streams in left-to-right
// order, so a fixed aggregate of packed leaves has a width the compiler knows
// and a target position for every leaf.
std::optional<uint64_t> fixedBitStreamWidth(Type type) {
  if (sim::getPackedScalarType(type))
    if (std::optional<unsigned> packed = sim::getPackedWidth(type))
      return *packed ? std::optional<uint64_t>{*packed} : std::nullopt;
  if (!isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type))
    return std::nullopt;
  unsigned count = sim::getAggregateNumElements(type);
  if (count == 0)
    return std::nullopt;
  uint64_t total = 0;
  for (unsigned ordinal = 0; ordinal < count; ++ordinal) {
    std::optional<uint64_t> element =
        fixedBitStreamWidth(sim::getAggregateElementType(type, ordinal));
    if (!element || *element > std::numeric_limits<uint64_t>::max() - total)
      return std::nullopt;
    total += *element;
  }
  return total;
}

// Take one fixed-size target's worth of bits off the stream, advancing the
// cursor past what it consumed. An unpacked aggregate is filled element by
// element in the same left-to-right order appendToBitStream flattens it,
// which is what makes `{>>{a}} = {>>{a}}` the identity 11.4.14 describes.
FailureOr<Value> UnitLowering::readBitStreamTarget(Value stream, Value &cursor,
                                                   Type type,
                                                   Location location) {
  auto advance = [&](uint64_t width) {
    Value amount =
        arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                  builder.getI64IntegerAttr(width));
    cursor = arith::AddIOp::create(builder, location, cursor, amount);
  };
  if (sim::getPackedScalarType(type)) {
    std::optional<unsigned> width = sim::getPackedWidth(type);
    if (!width || *width == 0)
      return emitError(location)
                 << "streaming assignment target is not a nonempty packed "
                    "value",
             failure();
    FailureOr<Value> value = readBitStreamValue(stream, cursor, type, location);
    if (failed(value))
      return failure();
    advance(*width);
    return value;
  }
  if (!isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type))
    return emitError(location)
               << "streaming assignment targets must be packed values, fixed "
                  "unpacked aggregates of them, or sequential containers of "
                  "packed values",
           failure();
  SmallVector<Value> elements;
  unsigned count = sim::getAggregateNumElements(type);
  elements.reserve(count);
  for (unsigned ordinal = 0; ordinal < count; ++ordinal) {
    FailureOr<Value> element = readBitStreamTarget(
        stream, cursor, sim::getAggregateElementType(type, ordinal), location);
    if (failed(element))
      return failure();
    elements.push_back(*element);
  }
  return sim::SimAggregateConstructOp::create(builder, location, type, elements)
      .getResult();
}

FailureOr<Value> UnitLowering::unflattenBitStreamValue(Value packed,
                                                       uint64_t &highBit,
                                                       Type type,
                                                       Location location) {
  if (std::optional<SmallVector<uint64_t>> plan =
          sim::getFixedBitStreamImportPlan(type)) {
    uint64_t width = (*plan)[3];
    if (width == 0 || width > highBit ||
        width > std::numeric_limits<unsigned>::max())
      return emitError(location)
                 << "streaming assignment target has no bit-stream window",
             failure();
    highBit -= width;
    Type windowType =
        isa<sim::LogicType>(packed.getType())
            ? Type(sim::LogicType::get(function.getContext(), width))
            : Type(IntegerType::get(function.getContext(), width));
    Value window;
    if (isa<sim::LogicType>(packed.getType())) {
      window = sim::SimLogicExtractOp::create(builder, location, windowType,
                                              packed, highBit);
    } else {
      auto full = cast<IntegerType>(packed.getType());
      Value shifted = packed;
      if (highBit) {
        Value amount = arith::ConstantOp::create(
            builder, location, full, builder.getIntegerAttr(full, highBit));
        shifted = arith::ShRUIOp::create(builder, location, packed, amount);
      }
      window =
          width == full.getWidth()
              ? shifted
              : arith::TruncIOp::create(builder, location, windowType, shifted)
                    .getResult();
    }
    SmallVector<int64_t> encoded;
    encoded.reserve(plan->size());
    llvm::transform(*plan, std::back_inserter(encoded),
                    [](uint64_t word) { return static_cast<int64_t>(word); });
    return sim::SimAggregateImportBitstreamOp::create(
               builder, location, type, window,
               builder.getDenseI64ArrayAttr(encoded))
        .getResult();
  }
  if (Type scalarType = sim::getPackedScalarType(type)) {
    std::optional<unsigned> width = sim::getPackedWidth(type);
    if (!width || *width == 0 || *width > highBit)
      return emitError(location)
                 << "streaming assignment target has no bit-stream window",
             failure();
    highBit -= *width;
    Value window;
    if (isa<sim::LogicType>(packed.getType())) {
      window = sim::SimLogicExtractOp::create(
          builder, location, sim::LogicType::get(function.getContext(), *width),
          packed, highBit);
    } else {
      auto full = cast<IntegerType>(packed.getType());
      Value shifted = packed;
      if (highBit) {
        Value amount = arith::ConstantOp::create(
            builder, location, full, builder.getIntegerAttr(full, highBit));
        shifted = arith::ShRUIOp::create(builder, location, packed, amount);
      }
      window =
          *width == full.getWidth()
              ? shifted
              : arith::TruncIOp::create(
                    builder, location,
                    IntegerType::get(function.getContext(), *width), shifted)
                    .getResult();
    }
    FailureOr<Value> converted = convert(window, scalarType, false, location);
    if (failed(converted))
      return failure();
    if (scalarType == type)
      return *converted;
    return sim::SimPackedUnflattenOp::create(builder, location, type,
                                             *converted)
        .getResult();
  }
  if (!isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type))
    return emitError(location)
               << "streaming assignment target is not a bit-stream type",
           failure();
  SmallVector<Value> elements;
  unsigned count = sim::getAggregateNumElements(type);
  elements.reserve(count);
  for (unsigned ordinal = 0; ordinal < count; ++ordinal) {
    FailureOr<Value> element = unflattenBitStreamValue(
        packed, highBit, sim::getAggregateElementType(type, ordinal), location);
    if (failed(element))
      return failure();
    elements.push_back(*element);
  }
  return sim::SimAggregateConstructOp::create(builder, location, type, elements)
      .getResult();
}

FailureOr<Value> UnitLowering::lowerStreamingAssignment(
    semantic::SVStreamingConcatenationExpressionOp destination, Value source) {
  Location location = getSemanticLocation(destination);
  ArrayRef<int64_t> withFlags = destination.getStreamWithFlags();
  SmallVector<Operation *> inventory = getChildren(destination);
  size_t withCount = llvm::count(withFlags, int64_t{1});
  if (destination.getStreamCount() == 0 ||
      withFlags.size() != destination.getStreamCount() ||
      llvm::any_of(withFlags,
                   [](int64_t flag) { return flag != 0 && flag != 1; }) ||
      inventory.size() != destination.getStreamCount() + withCount)
    return destination.emitError("malformed streaming target inventory"),
           failure();
  SmallVector<std::pair<Operation *, Operation *>> targets;
  size_t nextChild = 0;
  for (int64_t withFlag : withFlags) {
    Operation *target = inventory[nextChild++];
    Operation *withRange = withFlag ? inventory[nextChild++] : nullptr;
    targets.emplace_back(target, withRange);
  }

  bool fourState = streamContainsFourState(source.getType());
  FailureOr<Value> generic = createBitStream(fourState, location);
  if (failed(generic))
    return failure();
  Value zero = arith::ConstantOp::create(
      builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
  Value one = arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                        builder.getI64IntegerAttr(1));
  if (failed(appendToBitStream(source, *generic, zero, fourState, location)))
    return failure();
  Value sourceSize = sim::SimContainerSizeOp::create(
      builder, location, builder.getI64Type(), *generic);

  struct TargetInfo {
    Operation *node;
    Operation *withRange;
    Type type;
    Type elementType;
    uint64_t width;
    bool dynamic;
  };
  SmallVector<TargetInfo> infos;
  uint64_t fixedWidth = 0;
  for (auto [target, withRange] : targets) {
    FailureOr<Type> type = getNormalizedSemanticType(target);
    if (failed(type))
      return failure();
    Type elementType;
    if (auto array = dyn_cast<sim::DynamicArrayType>(*type))
      elementType = array.getElementType();
    else if (auto queue = dyn_cast<sim::QueueType>(*type))
      elementType = queue.getElementType();
    bool dynamic = static_cast<bool>(elementType);
    std::optional<uint64_t> width;
    if (dynamic) {
      std::optional<unsigned> packed = sim::getPackedWidth(elementType);
      if (packed && *packed && sim::getPackedScalarType(elementType))
        width = *packed;
    } else {
      width = fixedBitStreamWidth(*type);
    }
    if (!width)
      return emitError(getSemanticLocation(target))
                 << "streaming assignment targets must be packed values, "
                    "fixed unpacked aggregates of them, or sequential "
                    "containers of packed values",
             failure();
    if (!dynamic) {
      if (fixedWidth > std::numeric_limits<uint64_t>::max() - *width)
        return emitError(location)
                   << "streaming assignment width is not representable",
               failure();
      fixedWidth += *width;
    }
    if (withRange && !dynamic)
      return emitError(getSemanticLocation(target))
                 << "a streaming with range requires a sequential container",
             failure();
    infos.push_back({target, withRange, *type, elementType, *width, dynamic});
  }

  auto i64Constant = [&](uint64_t value) -> Value {
    return arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                     builder.getI64IntegerAttr(value));
  };
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
  // 11.4.14.3 makes a source with fewer bits than the targets need an error,
  // and this has to settle before the stream is reordered: the reordering
  // below covers the bits the targets consume, which is only a position in the
  // stream once there are that many.
  Value enough =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::uge,
                            sourceSize, i64Constant(fixedWidth));
  if (failed(requireRuntime(enough,
                            "streaming assignment source has too few bits")))
    return failure();
  // IEEE 1800-2017 11.4.14.3: "If the source expression contains more bits
  // than are needed, the appropriate number of bits shall be consumed from its
  // left (most significant) end." The reordering then applies to those bits
  // alone -- reordering the whole source first and taking its front instead
  // would hand a left-to-right stream the source's rightmost bits. Targets
  // that include a dynamic container consume the whole source (11.4.14.4's
  // greedy resize), so there is nothing to leave behind there.
  bool everyTargetFixed =
      llvm::none_of(infos, [](const TargetInfo &info) { return info.dynamic; });
  if (everyTargetFixed && fixedWidth > std::numeric_limits<unsigned>::max())
    return emitError(location)
               << "fixed streaming assignment width is not representable",
           failure();
  FailureOr<Value> reordered =
      reorderBitStream(*generic, destination.getSliceSize(), location,
                       everyTargetFixed ? i64Constant(fixedWidth) : Value{});
  if (failed(reordered))
    return failure();

  Value fixedPacked;
  uint64_t fixedHighBit = fixedWidth;
  if (everyTargetFixed) {
    Type packedType =
        fourState ? Type(sim::LogicType::get(function.getContext(), fixedWidth))
                  : Type(IntegerType::get(function.getContext(), fixedWidth));
    fixedPacked = sim::SimContainerExportBitstreamOp::create(
        builder, location, packedType, *reordered);
  }

  bool usedGreedy = false;
  Value cursor = zero;
  for (size_t targetIndex = 0; targetIndex < infos.size(); ++targetIndex) {
    TargetInfo &info = infos[targetIndex];
    if (!info.dynamic) {
      FailureOr<Value> value =
          fixedPacked
              ? unflattenBitStreamValue(fixedPacked, fixedHighBit, info.type,
                                        getSemanticLocation(info.node))
              : readBitStreamTarget(*reordered, cursor, info.type,
                                    getSemanticLocation(info.node));
      if (failed(value) || failed(writeLValue(info.node, *value, false, false,
                                              getSemanticLocation(info.node))))
        return failure();
      continue;
    }

    Value count = zero;
    Value rangeFirst = zero;
    Value rangeAscends;
    Value resizeSize;
    semantic::SVRangeSelectionKind rangeKind =
        semantic::SVRangeSelectionKind::IndexedUp;
    if (info.withRange) {
      SmallVector<Operation *> rangeChildren = getChildren(info.withRange);
      bool elementRange =
          isa<semantic::SVElementSelectExpressionOp>(info.withRange);
      auto range =
          dyn_cast<semantic::SVRangeSelectExpressionOp>(info.withRange);
      if ((!elementRange && !range) ||
          rangeChildren.size() != (elementRange ? 2u : 3u))
        return emitError(getSemanticLocation(info.withRange))
                   << "malformed streaming with range",
               failure();
      auto lowerRangeIndex = [&](Operation *index) -> FailureOr<Value> {
        FailureOr<Value> value = lowerExpression(index);
        if (failed(value))
          return failure();
        FailureOr<Value> scalar =
            toPackedScalar(*value, getSemanticLocation(index));
        if (failed(scalar))
          return failure();
        return convert(*scalar, builder.getI64Type(), isSignedNode(index),
                       getSemanticLocation(index));
      };
      FailureOr<Value> first = lowerRangeIndex(rangeChildren[1]);
      if (failed(first))
        return failure();
      rangeFirst = *first;
      Value firstNonnegative = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::sge, rangeFirst, zero);
      if (failed(requireRuntime(
              firstNonnegative,
              "streaming with range has a negative first index")))
        return failure();
      if (elementRange) {
        count = one;
        resizeSize = arith::AddIOp::create(builder, location, rangeFirst, one);
      } else {
        rangeKind = range.getSelectionKind();
        FailureOr<Value> second = lowerRangeIndex(rangeChildren[2]);
        if (failed(second))
          return failure();
        if (rangeKind == semantic::SVRangeSelectionKind::Simple) {
          Value secondNonnegative = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::sge, *second, zero);
          if (failed(requireRuntime(
                  secondNonnegative,
                  "streaming with range has a negative second index")))
            return failure();
          rangeAscends = arith::CmpIOp::create(builder, location,
                                               arith::CmpIPredicate::slt,
                                               rangeFirst, *second);
          Value high = arith::SelectOp::create(builder, location, rangeAscends,
                                               *second, rangeFirst);
          Value low = arith::SelectOp::create(builder, location, rangeAscends,
                                              rangeFirst, *second);
          count = arith::AddIOp::create(
              builder, location,
              arith::SubIOp::create(builder, location, high, low), one);
          resizeSize = arith::AddIOp::create(builder, location, high, one);
        } else {
          count = *second;
          Value positive = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::sgt, count, zero);
          if (failed(requireRuntime(
                  positive,
                  "streaming with indexed range has a nonpositive width")))
            return failure();
          if (rangeKind == semantic::SVRangeSelectionKind::IndexedUp) {
            resizeSize =
                arith::AddIOp::create(builder, location, rangeFirst, count);
          } else {
            Value fits = arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::uge,
                arith::AddIOp::create(builder, location, rangeFirst, one),
                count);
            if (failed(requireRuntime(
                    fits,
                    "streaming with indexed-down range is out of bounds")))
              return failure();
            resizeSize =
                arith::AddIOp::create(builder, location, rangeFirst, one);
          }
        }
      }

      uint64_t subsequentFixed = 0;
      for (size_t later = targetIndex + 1; later < infos.size(); ++later)
        if (!infos[later].dynamic)
          subsequentFixed += infos[later].width;
      Value selectedBits = arith::MulIOp::create(builder, location, count,
                                                 i64Constant(info.width));
      Value consumed = arith::AddIOp::create(
          builder, location, cursor,
          arith::AddIOp::create(builder, location, selectedBits,
                                i64Constant(subsequentFixed)));
      Value available = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ule, consumed, sourceSize);
      if (failed(requireRuntime(
              available,
              "streaming with range consumes more bits than the source")))
        return failure();
    } else if (!usedGreedy) {
      uint64_t subsequentFixed = 0;
      for (size_t later = targetIndex + 1; later < infos.size(); ++later)
        if (!infos[later].dynamic)
          subsequentFixed += infos[later].width;
      Value remaining = arith::SubIOp::create(
          builder, location,
          arith::SubIOp::create(builder, location, sourceSize, cursor),
          i64Constant(subsequentFixed));
      Value elementWidth = i64Constant(info.width);
      Value residue =
          arith::RemUIOp::create(builder, location, remaining, elementWidth);
      Value divisible = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, residue, zero);
      if (failed(requireRuntime(
              divisible,
              "streaming assignment dynamic target has a partial element")))
        return failure();
      count =
          arith::DivUIOp::create(builder, location, remaining, elementWidth);
      usedGreedy = true;
    }

    Value previous;
    Value previousSize;
    if (info.withRange) {
      FailureOr<Value> current = lowerExpression(info.node);
      if (failed(current))
        return failure();
      previous = *current;
      previousSize = sim::SimContainerSizeOp::create(
          builder, location, builder.getI64Type(), previous);
    }
    FailureOr<ContainerElementDescriptor> descriptor =
        describeContainerElement(info.elementType, location);
    if (failed(descriptor))
      return failure();
    uint32_t kind = isa<sim::DynamicArrayType>(info.type)
                        ? OBELISK_RT_CONTAINER_DYNAMIC_ARRAY
                        : OBELISK_RT_CONTAINER_QUEUE;
    uint64_t bound = 0;
    if (auto queue = dyn_cast<sim::QueueType>(info.type))
      bound = queue.getBound() ? queue.getBound() : UINT64_MAX;
    Value containerSize = count;
    if (info.withRange) {
      Value needsGrowth =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                                previousSize, resizeSize);
      containerSize = arith::SelectOp::create(builder, location, needsGrowth,
                                              resizeSize, previousSize);
    }
    Value allocationSize =
        kind == OBELISK_RT_CONTAINER_DYNAMIC_ARRAY ? containerSize : zero;
    Value container = sim::SimContainerCreateOp::create(
        builder, location, info.type, allocationSize, descriptor->typeID,
        descriptor->kind, descriptor->flags, descriptor->valueSize,
        descriptor->alignment, descriptor->bitWidth,
        builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(descriptor->traceKinds), kind, bound);

    if (info.withRange && kind == OBELISK_RT_CONTAINER_QUEUE) {
      Value defaultElement =
          createDefaultValue(builder, location, info.elementType);
      if (!defaultElement)
        return emitError(location)
                   << "cannot initialize a streaming with queue target",
               failure();
      Block *initializeHeader = addBlock();
      initializeHeader->addArgument(builder.getI64Type(), location);
      Block *initializeBody = addBlock();
      Block *initializeExit = addBlock();
      cf::BranchOp::create(builder, location, initializeHeader,
                           ValueRange{zero});
      setCurrent(initializeHeader);
      Value initializeIndex = initializeHeader->getArgument(0);
      Value initializeMore =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                                initializeIndex, containerSize);
      cf::CondBranchOp::create(builder, location, initializeMore,
                               initializeBody, ValueRange{}, initializeExit,
                               ValueRange{});
      setCurrent(initializeBody);
      sim::SimContainerWriteOp::create(builder, location, container,
                                       initializeIndex, defaultElement);
      Value initializeNext =
          arith::AddIOp::create(builder, location, initializeIndex, one);
      cf::BranchOp::create(builder, location, initializeHeader,
                           ValueRange{initializeNext});
      setCurrent(initializeExit);
    }

    if (info.withRange) {
      Block *copyHeader = addBlock();
      copyHeader->addArgument(builder.getI64Type(), location);
      Block *copyBody = addBlock();
      Block *copyExit = addBlock();
      cf::BranchOp::create(builder, location, copyHeader, ValueRange{zero});
      setCurrent(copyHeader);
      Value copyIndex = copyHeader->getArgument(0);
      Value copyMore =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                                copyIndex, previousSize);
      cf::CondBranchOp::create(builder, location, copyMore, copyBody,
                               ValueRange{}, copyExit, ValueRange{});
      setCurrent(copyBody);
      Value oldElement = sim::SimContainerReadOp::create(
          builder, location, info.elementType, previous, copyIndex);
      sim::SimContainerWriteOp::create(builder, location, container, copyIndex,
                                       oldElement);
      Value copyNext = arith::AddIOp::create(builder, location, copyIndex, one);
      cf::BranchOp::create(builder, location, copyHeader, ValueRange{copyNext});
      setCurrent(copyExit);
    }

    Block *header = addBlock();
    header->addArgument(builder.getI64Type(), location);
    Block *body = addBlock();
    Block *exit = addBlock();
    cf::BranchOp::create(builder, location, header, ValueRange{zero});
    setCurrent(header);
    Value elementIndex = header->getArgument(0);
    Value more = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, elementIndex, count);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                             ValueRange{});
    setCurrent(body);
    Value elementStart = arith::AddIOp::create(
        builder, location, cursor,
        arith::MulIOp::create(builder, location, elementIndex,
                              i64Constant(info.width)));
    FailureOr<Value> element =
        readBitStreamValue(*reordered, elementStart, info.elementType,
                           getSemanticLocation(info.node));
    if (failed(element))
      return failure();
    Value destinationIndex = elementIndex;
    if (info.withRange) {
      Value above =
          arith::AddIOp::create(builder, location, rangeFirst, elementIndex);
      Value below =
          arith::SubIOp::create(builder, location, rangeFirst, elementIndex);
      switch (rangeKind) {
      case semantic::SVRangeSelectionKind::Simple:
        destinationIndex = arith::SelectOp::create(builder, location,
                                                   rangeAscends, above, below);
        break;
      case semantic::SVRangeSelectionKind::IndexedUp:
        destinationIndex = above;
        break;
      case semantic::SVRangeSelectionKind::IndexedDown:
        destinationIndex = below;
        break;
      }
    }
    sim::SimContainerWriteOp::create(builder, location, container,
                                     destinationIndex, *element);
    Value next = arith::AddIOp::create(builder, location, elementIndex, one);
    cf::BranchOp::create(builder, location, header, ValueRange{next});
    setCurrent(exit);
    if (failed(writeLValue(info.node, container, false, false,
                           getSemanticLocation(info.node))))
      return failure();
    cursor =
        arith::AddIOp::create(builder, location, cursor,
                              arith::MulIOp::create(builder, location, count,
                                                    i64Constant(info.width)));
  }
  return source;
}

LogicalResult UnitLowering::emitDeferredNBAEvent(
    semantic::SVAssignmentExpressionOp assignment, Operation *control,
    CapturedLValue target, Value value, Location location) {
  auto design = function->getParentOfType<sim::SimDesignOp>();
  if (!design)
    return function.emitError(
        "deferred nonblocking assignment requires a simulation design");

  SmallVector<Operation *> controlChildren = getChildren(control);
  bool repeated = isa<semantic::SVRepeatedEventControlOp>(control);
  if (repeated && controlChildren.size() != 2) {
    unsupported(control) << " (repeated-event inventory)";
    return failure();
  }

  // IEEE 1800-2017 10.4.2 evaluates the assignment operands when the
  // statement is encountered. In particular, the repeat count must not be
  // reread by the later scheduler-owned action.
  Value repeatCount;
  if (repeated) {
    FailureOr<Value> lowered = lowerRepeatedEventCount(control);
    if (failed(lowered))
      return failure();
    repeatCount = *lowered;
  }

  SmallVector<Value> targetValues;
  appendCapturedValues(target, targetValues);
  MLIRContext *context = function.getContext();
  SmallVector<Type> inputs;
  SmallVector<Value> captures;
  SmallVector<DictionaryAttr> argumentAttrs;
  SmallVector<Attribute> bindings;

  auto appendCapture = [&](Value capture, sim::CaptureKind kind,
                           bool retainReference) -> unsigned {
    unsigned argument = inputs.size();
    inputs.push_back(capture.getType());
    captures.push_back(capture);
    DictionaryAttr metadata;
    if (auto blockArgument = dyn_cast<BlockArgument>(capture);
        blockArgument &&
        blockArgument.getOwner() == &function.getBody().front())
      metadata = function.getArgAttrDict(blockArgument.getArgNumber());
    if (!metadata)
      metadata = captureMetadata(builder, kind);
    if (retainReference && !isStaticallyAllocatedOverrideTarget(capture)) {
      SmallVector<NamedAttribute> entries(metadata.begin(), metadata.end());
      if (!metadata.contains("obelisk_sim.automatic_reference_capture"))
        entries.push_back(builder.getNamedAttr(
            "obelisk_sim.automatic_reference_capture", builder.getUnitAttr()));
      metadata = builder.getDictionaryAttr(entries);
    }
    argumentAttrs.push_back(metadata);
    return argument;
  };

  Value processContext = function.getBody().front().getArgument(0);
  appendCapture(processContext, sim::CaptureKind::Context,
                /*retainReference=*/false);
  unsigned targetStart = inputs.size();
  for (Value captured : targetValues)
    appendCapture(captured, sim::CaptureKind::Formal,
                  /*retainReference=*/true);
  unsigned valueArgument = appendCapture(value, sim::CaptureKind::Value,
                                         /*retainReference=*/false);
  std::optional<unsigned> repeatArgument;
  if (repeatCount)
    repeatArgument = appendCapture(repeatCount, sim::CaptureKind::Value,
                                   /*retainReference=*/false);

  // Preserve only the frozen bindings used by the event expression. Design
  // descriptors and constants need no spawn ABI slot; lexical values and
  // automatic references do.
  Operation *eventTree = repeated ? controlChildren[1] : control;
  llvm::StringSet<> referencedPaths;
  bool eventUsesThis = false;
  ArrayAttr parentBindings =
      function->getAttrOfType<ArrayAttr>(bindingsAttrName);
  llvm::StringSet<> thisBindingPaths;
  if (parentBindings && thisObject)
    for (Attribute attribute : parentBindings) {
      auto argument = dyn_cast<sim::ArgumentBindingAttr>(attribute);
      if (argument && argument.getArgument() < function.getNumArguments() &&
          function.getArgument(argument.getArgument()) == thisObject)
        thisBindingPaths.insert(argument.getPath().getValue());
    }
  eventTree->walk([&](Operation *nested) {
    StringRef path;
    if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(nested)) {
      path = named.getReferencedPath();
      eventUsesThis |= named->hasAttr("obelisk_sim.class_field");
    } else if (auto hierarchical =
                   dyn_cast<semantic::SVHierarchicalValueExpressionOp>(nested))
      path = hierarchical.getReferencedPath();
    else if (auto member =
                 dyn_cast<semantic::SVMemberAccessExpressionOp>(nested)) {
      if (member->hasAttr(staticClassPropertyAttrName))
        path = member.getReferencedPath();
      eventUsesThis |= member->hasAttr("obelisk_sim.class_field");
    }
    if (!path.empty())
      referencedPaths.insert(path);
    if (auto call = dyn_cast<semantic::SVCallExpressionOp>(nested);
        call && call->hasAttr("obelisk_sim.class_instance")) {
      auto formals = call->getAttrOfType<ArrayAttr>(calleeFormalsAttrName);
      bool superCall = call->hasAttr("obelisk_sim.class_super");
      eventUsesThis |=
          superCall || (formals && getChildren(call).size() == formals.size());
    }
    if (auto callCaptures =
            nested->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
      for (Attribute capture : callCaptures)
        referencedPaths.insert(cast<StringAttr>(capture).getValue());
    if (auto observerCaptures =
            nested->getAttrOfType<ArrayAttr>(observerCapturesAttrName))
      for (Attribute capture : observerCaptures) {
        StringRef path = cast<StringAttr>(capture).getValue();
        referencedPaths.insert(path);
        eventUsesThis |= thisBindingPaths.contains(path);
      }
  });

  llvm::StringSet<> capturedPaths;
  std::optional<unsigned> outlinedThisArgument;
  if (eventUsesThis && thisObject) {
    outlinedThisArgument = appendCapture(thisObject, sim::CaptureKind::Formal,
                                         /*retainReference=*/false);
    for (const auto &entry : thisBindingPaths)
      if (capturedPaths.insert(entry.getKey()).second)
        bindings.push_back(sim::ArgumentBindingAttr::get(
            context, builder.getStringAttr(entry.getKey()),
            *outlinedThisArgument, sim::UnitArgumentKind::Direct,
            /*copyOut=*/false, IntegerAttr{}, /*copyIn=*/true));
  }
  auto addPathCapture = [&](StringRef path) {
    if (!capturedPaths.insert(path).second)
      return;
    Value capture = values.lookup(path);
    if (!capture)
      capture = lvalues.lookup(path);
    if (!capture)
      return;
    unsigned argument = appendCapture(capture, sim::CaptureKind::Formal,
                                      /*retainReference=*/true);
    bindings.push_back(sim::ArgumentBindingAttr::get(
        context, builder.getStringAttr(path), argument,
        sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{},
        /*copyIn=*/true));
  };
  if (parentBindings)
    for (Attribute attribute : parentBindings) {
      StringRef path = sim::getUnitBindingPath(attribute);
      if (path.empty() || !referencedPaths.contains(path))
        continue;
      if (isa<sim::ConstantBindingAttr, sim::DescriptorBindingAttr>(
              attribute)) {
        if (capturedPaths.insert(path).second)
          bindings.push_back(attribute);
        continue;
      }
      addPathCapture(path);
    }
  SmallVector<StringRef> lexicalPaths;
  for (const auto &entry : referencedPaths)
    if (!capturedPaths.contains(entry.getKey()))
      lexicalPaths.push_back(entry.getKey());
  llvm::sort(lexicalPaths);
  for (StringRef path : lexicalPaths)
    addPathCapture(path);

  auto codeUnitIDAttr = assignment->getAttrOfType<IntegerAttr>(
      "obelisk_sim.nba_event_code_unit_id");
  auto hierarchyAttr =
      assignment->getAttrOfType<StringAttr>("obelisk_sim.nba_event_hierarchy");
  if (!codeUnitIDAttr || !codeUnitIDAttr.getValue().isStrictlyPositive() ||
      !hierarchyAttr)
    return emitError(location)
           << "deferred nonblocking assignment has no prepared code-unit "
              "identity";
  uint64_t codeUnitID = codeUnitIDAttr.getValue().getZExtValue();
  uint64_t parentID = function.getCodeUnitId().value_or(0);
  uint64_t scopeID = 0;
  for (sim::SimCodeUnitDeclOp declaration :
       design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
    if (declaration.getId() == parentID) {
      scopeID = declaration.getScopeId();
      break;
    }

  std::string symbol =
      (function.getSymName() + ".$nba_event." + Twine(codeUnitID)).str();
  OpBuilder outlineBuilder(function);
  outlineBuilder.setInsertionPoint(function);
  auto declaration = sim::SimCodeUnitDeclOp::create(
      outlineBuilder, location, codeUnitID, scopeID, sim::EntryKind::Fork,
      hierarchyAttr, outlineBuilder.getStringAttr("deferred NBA event"),
      outlineBuilder.getUnitAttr());
  SmallVector<NamedAttribute> attributes{
      outlineBuilder.getNamedAttr(bindingsAttrName,
                                  outlineBuilder.getArrayAttr(bindings)),
      outlineBuilder.getNamedAttr("code_unit_id",
                                  outlineBuilder.getI64IntegerAttr(codeUnitID)),
      outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
      outlineBuilder.getNamedAttr("obelisk_sim.detached_controls",
                                  outlineBuilder.getUnitAttr()),
      outlineBuilder.getNamedAttr("obelisk_sim.prime_on_spawn",
                                  outlineBuilder.getUnitAttr()),
      outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                  hierarchyAttr),
  };
  if (outlinedThisArgument)
    attributes.push_back(outlineBuilder.getNamedAttr(
        sim::metadata::thisArgument,
        outlineBuilder.getI32IntegerAttr(*outlinedThisArgument)));
  const StringRef inheritedAttributes[] = {
      delayScaleAttrName, delayQuantumAttrName, "home_region", "domain"};
  for (StringRef name : inheritedAttributes)
    if (Attribute attribute = function->getAttr(name))
      attributes.push_back(outlineBuilder.getNamedAttr(name, attribute));
  auto deferred =
      sim::SimFuncOp::create(outlineBuilder, location, symbol,
                             FunctionType::get(context, inputs, TypeRange{}),
                             sim::EntryKind::Fork, attributes, argumentAttrs);
  SymbolTable::setSymbolVisibility(deferred, SymbolTable::Visibility::Private);

  Block &entry = deferred.getBody().front();
  OpBuilder bodyBuilder = OpBuilder::atBlockEnd(&entry);
  IRMapping mapping;
  Operation *clonedControl = bodyBuilder.clone(*control, mapping);
  UnitLowering nested(deferred);
  if (repeatArgument) {
    Operation *clonedCount = mapping.lookupOrNull(controlChildren[0]);
    if (!clonedCount) {
      deferred.erase();
      declaration.erase();
      return emitError(location)
             << "deferred repeat count is outside its timing control";
    }
    nested.expressionCaptures[clonedCount] = entry.getArgument(*repeatArgument);
  }
  Block *continuation = nested.addBlock();
  LogicalResult suspended =
      repeated ? nested.emitRepeatedEventSuspend(clonedControl, continuation)
               : nested.emitEventSuspend(clonedControl, continuation);
  if (failed(suspended)) {
    deferred.erase();
    declaration.erase();
    return failure();
  }
  nested.setCurrent(continuation);
  CapturedLValue childTarget = target;
  unsigned next = 0;
  if (failed(nested.replaceCapturedValues(
          childTarget,
          entry.getArguments().slice(targetStart, targetValues.size()),
          next)) ||
      next != targetValues.size() ||
      failed(nested.writeCapturedLValue(childTarget,
                                        entry.getArgument(valueArgument), false,
                                        true, location))) {
    deferred.erase();
    declaration.erase();
    return failure();
  }
  if (nested.current->empty() ||
      !nested.current->back().hasTrait<OpTrait::IsTerminator>())
    sim::SimReturnOp::create(nested.builder, location, ValueRange{});
  clonedControl->erase();
  deferred->setAttr(sim::metadata::lowered, outlineBuilder.getUnitAttr());

  auto spawn = [&]() {
    sim::SimSpawnOp::create(builder, location, deferred.getSymNameAttr(),
                            captures, ArrayAttr{}, ArrayAttr{});
  };
  if (!repeatCount) {
    spawn();
    return success();
  }

  // A nonpositive repeat count completes immediately. Enqueue it here, in
  // source order, instead of launching a child that could run after a later
  // nonblocking assignment from the same process.
  Value zero = arith::ConstantOp::create(
      builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
  Value positive = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, repeatCount, zero);
  Block *positiveBlock = addBlock();
  Block *immediateBlock = addBlock();
  Block *resume = addBlock();
  cf::CondBranchOp::create(builder, location, positive, positiveBlock,
                           ValueRange{}, immediateBlock, ValueRange{});
  setCurrent(positiveBlock);
  spawn();
  cf::BranchOp::create(builder, location, resume);
  setCurrent(immediateBlock);
  if (failed(writeCapturedLValue(target, value, false, true, location)))
    return failure();
  if (current->empty() || !current->back().hasTrait<OpTrait::IsTerminator>())
    cf::BranchOp::create(builder, location, resume);
  setCurrent(resume);
  return success();
}

FailureOr<Value>
UnitLowering::lowerAssignment(semantic::SVAssignmentExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  bool timed = op.getHasTimingControl();
  size_t expected = timed ? 3 : 2;
  if (children.size() != expected) {
    unsupported(op) << " (assignment child inventory)";
    return failure();
  }
  Operation *control = timed ? children[0] : nullptr;
  Operation *destination = children[timed ? 1 : 0];
  Operation *source = children[timed ? 2 : 1];
  bool compound = op.getOperatorKind().has_value();
  bool nonblocking =
      op.getAssignmentKind() == semantic::SVAssignmentKind::Nonblocking;
  if (compound && nonblocking) {
    emitError(location)
        << "nonblocking compound assignment is not valid SystemVerilog";
    return failure();
  }

  std::optional<CapturedLValue> captured;
  FailureOr<Value> rhs = failure();
  if (compound) {
    FailureOr<CapturedLValue> destinationCapture =
        captureLValue(destination, location);
    if (failed(destinationCapture))
      return failure();
    captured = std::move(*destinationCapture);
    FailureOr<Value> oldValue = loadCapturedLValue(*captured, location);
    if (failed(oldValue))
      return failure();

    // Slang represents the left operand of a compound assignment's explicit
    // binary subtree with an lvalue-reference placeholder. Resolve it to the
    // value loaded from the already captured destination, so every other
    // binary conversion rule remains shared with ordinary expressions.
    Value previousPlaceholder = lvalueReferencePlaceholder;
    lvalueReferencePlaceholder = *oldValue;
    rhs = lowerExpression(source);
    lvalueReferencePlaceholder = previousPlaceholder;
  } else {
    rhs = lowerExpression(source);
  }
  if (failed(rhs))
    return failure();
  if (auto streaming = dyn_cast<semantic::SVStreamingConcatenationExpressionOp>(
          destination)) {
    if (compound || nonblocking || timed)
      return emitError(location)
                 << "a streaming assignment target requires an untimed "
                    "blocking simple assignment",
             failure();
    return lowerStreamingAssignment(streaming, *rhs);
  }
  FailureOr<Type> destinationType = getNormalizedSemanticType(destination);
  if (failed(destinationType))
    return failure();
  FailureOr<Value> value = convert(*rhs, *destinationType, isSignedNode(source),
                                   location, isSignedNode(destination));
  if (failed(value))
    return failure();
  Operation *clockingRoot = findClockingOutputRoot(destination);
  auto clockingMember =
      dyn_cast_or_null<semantic::SVMemberAccessExpressionOp>(clockingRoot);
  bool virtualClocking =
      clockingMember && clockingMember->hasAttr("virtual_interface_clocking");
  bool staticClocking =
      clockingRoot && clockingRoot->hasAttr(clockingVariableAttrName);
  if (virtualClocking || staticClocking) {
    semantic::SVCycleDelayControlOp cycle;
    if (timed) {
      cycle = dyn_cast<semantic::SVCycleDelayControlOp>(control);
      if (!cycle) {
        emitError(location)
            << "a clocking output assignment timing control must be a cycle "
               "delay";
        return failure();
      }
    }
    LogicalResult driven =
        virtualClocking
            ? lowerClockingOutputAssignment(clockingMember, destination, *value,
                                            location, cycle)
            : lowerStaticClockingOutputAssignment(clockingRoot, destination,
                                                  *value, location, cycle);
    if (failed(driven))
      return failure();
    return *value;
  }
  if (!timed) {
    LogicalResult written =
        compound
            ? writeCapturedLValue(*captured, *value, false, false, location)
            : writeLValue(destination, *value, false, nonblocking, location);
    if (failed(written))
      return failure();
    return *value;
  }

  if (compound) {
    SmallVector<Value> continuationOperands;
    appendCapturedValues(*captured, continuationOperands);
    continuationOperands.push_back(*value);
    Block *continuation = addBlock();
    for (Value operand : continuationOperands)
      continuation->addArgument(operand.getType(), location);

    if (isa<semantic::SVDelayControlOp>(control)) {
      FailureOr<Value> delay = lowerDelayValue(control);
      if (failed(delay))
        return failure();
      sim::SimSuspendDelayOp::create(
          builder, location, *delay, sim::TimingSiteAttr{},
          continuationOperands, sim::ContinuationSiteAttr{},
          sim::EventRegionAttr{}, continuation);
      setCurrent(continuation);
    } else if (isa<semantic::SVRepeatedEventControlOp>(control)) {
      if (failed(emitRepeatedEventSuspend(control, continuation,
                                          continuationOperands)))
        return failure();
    } else {
      if (failed(emitEventSuspend(control, continuation, continuationOperands)))
        return failure();
      setCurrent(continuation);
    }

    unsigned next = 0;
    if (failed(replaceCapturedValues(*captured, continuation->getArguments(),
                                     next)))
      return failure();
    if (next >= continuation->getNumArguments())
      return failure();
    Value storedValue = continuation->getArgument(next);
    if (next + 1 != continuation->getNumArguments() ||
        failed(writeCapturedLValue(*captured, storedValue, false, false,
                                   location)))
      return failure();
    return storedValue;
  }

  if (isa<semantic::SVDelayControlOp>(control)) {
    FailureOr<Value> delay = lowerDelayValue(control);
    if (failed(delay))
      return failure();
    if (nonblocking) {
      // Both the RHS and destination handle are captured at encounter time.
      if (failed(
              writeLValue(destination, *value, false, true, location, *delay)))
        return failure();
      return *value;
    }

    // A blocking intra-assignment delay captures only the RHS. The destination
    // expression is intentionally resolved after resumption at commit time.
    Block *continuation = addBlock();
    continuation->addArgument((*value).getType(), location);
    sim::SimSuspendDelayOp::create(
        builder, location, *delay, sim::TimingSiteAttr{}, ValueRange{*value},
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
    setCurrent(continuation);
    Value capturedValue = continuation->getArgument(0);
    if (failed(writeLValue(destination, capturedValue, false, false, location)))
      return failure();
    return capturedValue;
  }

  if (nonblocking) {
    FailureOr<CapturedLValue> destinationCapture =
        captureLValue(destination, location);
    if (failed(destinationCapture) ||
        failed(emitDeferredNBAEvent(op, control, std::move(*destinationCapture),
                                    *value, location)))
      return failure();
    return *value;
  }

  // Event-controlled blocking assignments capture the RHS at encounter time,
  // suspend the caller, and resolve the destination only on commit.
  Block *continuation = addBlock();
  continuation->addArgument((*value).getType(), location);
  if (isa<semantic::SVRepeatedEventControlOp>(control)) {
    if (failed(emitRepeatedEventSuspend(control, continuation,
                                        ValueRange{*value})))
      return failure();
  } else {
    if (failed(emitEventSuspend(control, continuation, ValueRange{*value})))
      return failure();
    setCurrent(continuation);
  }
  Value capturedValue = continuation->getArgument(0);
  if (failed(writeLValue(destination, capturedValue, false, false, location)))
    return failure();
  return capturedValue;
}

LogicalResult
UnitLowering::lowerPortConnection(semantic::SVPortConnectionOp op) {
  Location location = getSemanticLocation(op);
  Operation *internal = getSingleRegionRoot(op.getInternal());
  Operation *actual = getSingleRegionRoot(op.getActual());

  auto loadPath = [&](StringRef path) -> FailureOr<Value> {
    Value handle = values.lookup(path);
    if (!handle) {
      emitError(location) << "port endpoint has no frozen binding: " << path;
      return failure();
    }
    recordSensitivity(handle);
    if (auto reference = dyn_cast<sim::RefType>(handle.getType()))
      return sim::SimRefLoadOp::create(builder, location,
                                       reference.getElementType(), handle)
          .getResult();
    if (auto net = dyn_cast<sim::NetType>(handle.getType()))
      return sim::SimNetReadOp::create(builder, location, net.getElementType(),
                                       handle)
          .getResult();
    return handle;
  };
  auto endpoint = [&](StringRef path, Operation *expression,
                      bool lvalue) -> FailureOr<Value> {
    if (expression)
      return lowerExpression(expression, lvalue);
    if (lvalue) {
      Value value = lvalues.lookup(path);
      if (!value) {
        emitError(location) << "port endpoint has no lvalue binding: " << path;
        return failure();
      }
      return value;
    }
    return loadPath(path);
  };
  auto write = [&](Value destination, Value source,
                   bool sourceSigned) -> LogicalResult {
    Type elementType;
    if (auto reference = dyn_cast<sim::RefType>(destination.getType()))
      elementType = reference.getElementType();
    else if (auto driver = dyn_cast<sim::DriverType>(destination.getType()))
      elementType = driver.getElementType();
    else {
      emitError(location)
          << "port connection sink is not variable storage or a net driver";
      return failure();
    }
    FailureOr<Value> converted =
        convert(source, elementType, sourceSigned, location);
    if (failed(converted))
      return failure();
    if (isa<sim::RefType>(destination.getType())) {
      auto store = sim::SimRefStoreOp::create(builder, location, *converted,
                                              destination);
      if (continuousStore)
        store->setAttr(continuousStoreAttrName, builder.getUnitAttr());
    } else if (drivesDelayedNet(function, destination)) {
      bool userRaw = isUserNetDriver(destination);
      auto drive = sim::SimDriverDriveDelayedNetOp::create(
          builder, location, destination, *converted,
          builder.getBoolAttr(userRaw));
      if (userRaw)
        drive->setAttr("obelisk_sim.user_net_raw_drive", builder.getUnitAttr());
    } else {
      auto drive = sim::SimDriverDriveOp::create(builder, location, destination,
                                                 *converted);
      if (isUserNetDriver(destination)) {
        drive->setAttr("obelisk_sim.defer_net_resolution",
                       builder.getUnitAttr());
        drive->setAttr("obelisk_sim.user_net_raw_drive", builder.getUnitAttr());
      }
    }
    return success();
  };

  if (!actual) {
    std::optional<bool> pull = op.getUnconnectedDriveValue();
    if (!pull || op.getDirection() != semantic::SVArgumentDirection::In)
      return success();
    FailureOr<Type> formalType =
        normalizeSemanticType(op.getFormalType(), location);
    if (failed(formalType))
      return failure();
    Type scalarType = sim::getPackedScalarType(*formalType);
    std::optional<unsigned> width = sim::getPackedWidth(scalarType);
    if (!scalarType || !width)
      return emitError(location)
                 << "`unconnected_drive requires a packed input port",
             failure();
    APInt bits = *pull ? APInt::getAllOnes(*width) : APInt(*width, 0);
    Value source;
    if (auto integer = dyn_cast<IntegerType>(scalarType))
      source = arith::ConstantOp::create(builder, location, integer,
                                         builder.getIntegerAttr(integer, bits));
    else {
      auto planeType = IntegerType::get(op->getContext(), *width);
      source = sim::SimLogicConstantOp::create(
          builder, location, scalarType,
          builder.getIntegerAttr(planeType, bits),
          builder.getIntegerAttr(planeType, APInt(*width, 0)));
    }
    if (source.getType() != *formalType)
      source = sim::SimPackedUnflattenOp::create(builder, location, *formalType,
                                                 source);
    if (internal)
      return writeLValue(internal, source, false, false, location);
    StringRef internalPath = op.getInternalPath().value_or(StringRef{});
    FailureOr<Value> destination = endpoint(internalPath, nullptr, true);
    if (failed(destination))
      return failure();
    return write(*destination, source, false);
  }

  StringRef internalPath = op.getInternalPath().value_or(StringRef{});
  if (op.getDirection() == semantic::SVArgumentDirection::In) {
    FailureOr<Value> source = lowerExpression(actual);
    if (failed(source))
      return failure();
    bool sourceSigned = actual->getAttrOfType<TypeAttr>("semantic_type") &&
                        isSignedNode(actual);
    // A non-ANSI formal can have an aggregate internal expression such as
    // `{high, low}`. Use the same evaluate-once write plan as assignments so
    // every leaf receives the correct slice of the converted actual.
    if (internal)
      return writeLValue(internal, *source, sourceSigned, false, location);
    FailureOr<Value> destination = endpoint(internalPath, nullptr, true);
    if (failed(destination))
      return failure();
    return write(*destination, *source, sourceSigned);
  }
  if (op.getDirection() != semantic::SVArgumentDirection::Out) {
    emitError(location) << "non-static ref or inout port reached unit lowering";
    return failure();
  }

  auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(actual);
  SmallVector<Operation *> children =
      assignment ? getChildren(assignment) : SmallVector<Operation *>{};
  if (!assignment || children.size() != 2) {
    emitError(location) << "malformed resolved output port expression";
    return failure();
  }
  FailureOr<Value> source = endpoint(internalPath, internal, false);
  if (failed(source))
    return failure();
  Value previousPlaceholder = expressionPlaceholder;
  expressionPlaceholder = *source;
  FailureOr<Value> converted = lowerExpression(children[1]);
  expressionPlaceholder = previousPlaceholder;
  if (failed(converted))
    return failure();
  // IEEE 1800-2017 10.8 and 23.3.3 make this output-port connection an
  // assignment-like continuous assignment. Preserve the ordinary assignment
  // dispatch for a Clause 11.4.14.3 streaming target: its semantic type is
  // intentionally void because the individual stream elements are the
  // lvalues.
  if (auto streaming =
          dyn_cast<semantic::SVStreamingConcatenationExpressionOp>(children[0]))
    return lowerStreamingAssignment(streaming, *converted);
  return writeLValue(children[0], *converted, isSignedNode(children[1]), false,
                     location);
}

} // namespace obelisk::simlowering
