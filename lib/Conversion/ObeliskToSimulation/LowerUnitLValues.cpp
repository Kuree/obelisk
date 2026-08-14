//===- LowerUnitLValues.cpp - Lower assignments and port lvalues ------===//

#include "LowerUnit.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"

#include "llvm/ADT/STLExtras.h"

#include <cmath>
#include <limits>
#include <optional>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

constexpr StringLiteral continuousStoreAttrName =
    "obelisk_sim.continuous_store";

Operation *getSingleRegionRoot(Region &region) {
  if (region.empty() || region.front().empty())
    return nullptr;
  return &region.front().front();
}

bool isSequentialContainerSubvalue(Operation *expression) {
  SmallVector<Operation *> children = getChildren(expression);
  if (isa<semantic::SVElementSelectExpressionOp>(expression) &&
      children.size() == 2) {
    FailureOr<Type> baseType = getNormalizedSemanticType(children.front());
    if (succeeded(baseType) &&
        isa<sim::DynamicArrayType, sim::QueueType>(*baseType))
      return true;
    return isSequentialContainerSubvalue(children.front());
  }
  if (isa<semantic::SVMemberAccessExpressionOp>(expression) &&
      children.size() == 1)
    return isSequentialContainerSubvalue(children.front());
  return false;
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
    if (selection.size() == 3 && sourceArray && resultArray) {
      FailureOr<Value> base = lowerExpression(selection.front(), true);
      if (failed(base))
        return failure();
      bool reference = isa<sim::RefType>((*base).getType());
      bool driver = isa<sim::DriverType>((*base).getType());
      if (!reference && !driver) {
        emitError(location)
            << "unpacked array slice destination is not a reference or driver";
        return failure();
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
      FailureOr<Value> first = lowerIndex(selection[1]);
      if (failed(first))
        return failure();
      Value ascends;
      if (range.getSelectionKind() == semantic::SVRangeSelectionKind::Simple) {
        FailureOr<Value> second = lowerIndex(selection[2]);
        if (failed(second))
          return failure();
        ascends = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::slt, *first, *second);
      }

      captured.kind = CapturedLValue::Kind::AggregateSlice;
      unsigned count = sim::getAggregateNumElements(resultArray);
      captured.children.reserve(count);
      for (unsigned ordinal = 0; ordinal < count; ++ordinal) {
        Value offset = arith::ConstantOp::create(
            builder, location, indexType,
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
        FailureOr<Value> scalarIndex = toPackedScalar(*index, location);
        if (failed(scalarIndex))
          return failure();
        FailureOr<Value> index64 =
            convert(*scalarIndex, builder.getI64Type(),
                    isSignedNode(selection[1]), location);
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
        FailureOr<CapturedLValue> base =
            captureLValue(selection.front(), location);
        FailureOr<Value> index = lowerExpression(selection[1]);
        if (failed(base) || failed(index))
          return failure();
        FailureOr<Value> index64 = convert(
            *index, builder.getI64Type(), isSignedNode(selection[1]), location);
        if (failed(index64))
          return failure();
        captured.kind = CapturedLValue::Kind::StringCharacter;
        captured.index = *index64;
        captured.children.push_back(std::move(*base));
        return captured;
      }

      if (isa<sim::PackedArrayType, sim::UnpackedArrayType>(*baseType) &&
          getConstantSpelling(selection[1]).has_value() &&
          isSequentialContainerSubvalue(selection.front())) {
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
      !virtualInterfaceMember) {
    ArrayRef<Operation *> members = memberChildren;
    auto ordinalAttr = destination->getAttrOfType<IntegerAttr>("field_ordinal");
    if (members.size() != 1 || !ordinalAttr ||
        ordinalAttr.getValue().isNegative() ||
        ordinalAttr.getValue().getActiveBits() > 32)
      return failure();
    FailureOr<Type> memberBaseType =
        getNormalizedSemanticType(members.front());
    if (failed(memberBaseType))
      return failure();
    auto unpackedUnion = dyn_cast<sim::UnpackedUnionType>(*memberBaseType);
    if (isSequentialContainerSubvalue(destination) ||
        sim::isManagedHandleType(*destinationType) ||
        (unpackedUnion && !unpackedUnion.getIsTagged())) {
      FailureOr<CapturedLValue> base =
          captureLValue(members.front(), location);
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
      FailureOr<Type> baseType =
          getNormalizedSemanticType(selection.front());
      if (succeeded(baseType) && sim::getPackedWidth(*baseType) &&
          sim::getPackedWidth(*destinationType)) {
        FailureOr<CapturedLValue> base =
            captureLValue(selection.front(), location);
        if (failed(base))
          return failure();
        bool hasDirectView =
            base->kind == CapturedLValue::Kind::Reference &&
            isa<sim::RefType, sim::DriverType>(base->reference.getType());
        if (!hasDirectView) {
          FailureOr<PackedSelectionAddress> address =
              lowerPackedSelectionAddress(destination, *baseType,
                                          *destinationType);
          if (failed(address))
            return failure();
          captured.kind = CapturedLValue::Kind::PackedValueSlice;
          captured.lowBit = address->lowBit;
          captured.index = address->dynamicLow;
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
    FailureOr<Value> scalar =
        succeeded(base) ? toPackedScalar(*base, location)
                        : FailureOr<Value>(failure());
    Type resultScalarType = sim::getPackedScalarType(destination.type);
    if (failed(scalar) || !resultScalarType)
      return failure();
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
  case CapturedLValue::Kind::ContainerElement:
    return sim::SimContainerReadOp::create(builder, location, destination.type,
                                           destination.container,
                                           destination.index)
        .getResult();
  case CapturedLValue::Kind::AssociativeElement: {
    Value isNull = sim::SimManagedIsNullOp::create(
        builder, location, builder.getI1Type(), destination.container);
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
    Value value =
        sim::SimAssocReadOp::create(builder, location, destination.type,
                                    destination.container, destination.index);
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
          builder, location, destination.type, *aggregate,
          destination.ordinal);
      if (isa<sim::ClassHandleType>(destination.type))
        extracted = sim::SimClassCastOp::create(
            builder, location, destination.type, extracted);
      return extracted;
    }
    return sim::SimAggregateExtractOp::create(
               builder, location, destination.type, *aggregate,
               destination.ordinal)
        .getResult();
  }
  case CapturedLValue::Kind::AggregateSlice: {
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
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> string =
        loadCapturedLValue(destination.children.front(), location);
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
      if (nonblocking)
        sim::SimNBAEnqueueOp::create(builder, location, published,
                                     destination.reference, delay,
                                     sim::NBASiteAttr{});
      else {
        auto store = sim::SimRefStoreOp::create(
            builder, location, published, destination.reference);
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
      sim::SimDriverDriveOp::create(builder, location, destination.reference,
                                    published);
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
    auto writePart = [&](unsigned baseLow, unsigned width) -> LogicalResult {
      Value replacement;
      Type replacementType;
      if (auto logic = dyn_cast<sim::LogicType>((*scalar).getType())) {
        replacementType = sim::LogicType::get(function.getContext(), width);
        replacement = sim::SimLogicExtractOp::create(
            builder, location, replacementType, *scalar,
            builder.getI64IntegerAttr(0));
      } else if (auto integer = dyn_cast<IntegerType>((*scalar).getType())) {
        replacementType = builder.getIntegerType(width);
        replacement = replacementType == integer
                          ? *scalar
                          : Value(arith::TruncIOp::create(
                                builder, location, replacementType, *scalar));
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
      if (failed(writePart(/*baseLow=*/0, selectedBitWidth - clipped)))
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
      if (failed(writePart(selectedLow, overlap)))
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
    if (nonblocking) {
      emitError(location)
          << "nonblocking packed selection assignment requires a captured "
             "partial-update path";
      return failure();
    }
    CapturedLValue &base = destination.children.front();
    FailureOr<Value> baseValue = loadCapturedLValue(base, location);
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(baseValue) || failed(converted))
      return failure();
    FailureOr<Value> baseScalar = toPackedScalar(*baseValue, location);
    FailureOr<Value> replacement = toPackedScalar(*converted, location);
    if (failed(baseScalar) || failed(replacement))
      return failure();

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
          builder, location, (*baseScalar).getType(), *baseScalar,
          *replacement, builder.getI64IntegerAttr(destination.lowBit));
    } else {
      Value low = arith::ConstantOp::create(
          builder, location, builder.getI64Type(),
          builder.getI64IntegerAttr(destination.lowBit));
      updated = sim::SimBitsDynInsertOp::create(
          builder, location, (*baseScalar).getType(), *baseScalar,
          *replacement, low);
    }
    FailureOr<Value> rebuilt =
        convert(updated, base.type, false, location,
                isSignedNode(base.semanticNode));
    if (failed(rebuilt))
      return failure();
    return writeCapturedLValue(base, *rebuilt, false, false, location);
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

    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), destination.container);
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    Value nonnegative = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sge, destination.index, zero);
    Value inRange = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, destination.index, size);
    Value valid =
        arith::AndIOp::create(builder, location, nonnegative, inRange);
    Block *write = addBlock();
    Block *resume = addBlock();
    resume->addArgument(destination.container.getType(), location);
    cf::CondBranchOp::create(builder, location, valid, write, ValueRange{},
                             resume, ValueRange{destination.container});
    setCurrent(write);
    Value updated = cloneSequentialValue(destination.container, location);
    sim::SimContainerWriteOp::create(builder, location, updated,
                                     destination.index, *converted);
    if (failed(writeCapturedLValue(base, updated, false, false, location)))
      return failure();
    if (current->empty() || !current->back().hasTrait<OpTrait::IsTerminator>())
      cf::BranchOp::create(builder, location, resume, ValueRange{updated});
    setCurrent(resume);
    destination.container = resume->getArgument(0);
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
    CapturedLValue createBase = base;
    CapturedLValue existingBase = base;
    Value isNull = sim::SimManagedIsNullOp::create(
        builder, location, builder.getI1Type(), destination.container);
    Block *create = addBlock();
    Block *existing = addBlock();
    Block *resume = addBlock();
    resume->addArgument(destination.container.getType(), location);
    cf::CondBranchOp::create(builder, location, isNull, create, ValueRange{},
                             existing, ValueRange{});
    setCurrent(create);
    auto arrayType = cast<sim::AssocArrayType>(destination.container.getType());
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
    Value updated = sim::SimContainerCloneOp::create(
        builder, location, destination.container.getType(),
        destination.container);
    sim::SimAssocWriteOp::create(builder, location, updated, destination.index,
                                 *converted);
    if (failed(
            writeCapturedLValue(existingBase, updated, false, false, location)))
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
  case CapturedLValue::Kind::AggregateSlice: {
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(converted) || destination.children.size() !=
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
    if (destination.children.size() != 1) {
      return failure();
    }
    if (nonblocking) {
      emitError(location)
          << "nonblocking string-character assignment requires a captured "
             "element path";
      return failure();
    }
    CapturedLValue &base = destination.children.front();
    FailureOr<Value> string = loadCapturedLValue(base, location);
    FailureOr<Value> character =
        convert(value, builder.getI8Type(), sourceSigned, location);
    if (failed(string) || failed(character))
      return failure();
    Value updated = sim::SimStringPutcOp::create(
        builder, location, sim::StringType::get(function.getContext()), *string,
        destination.index, *character);
    return writeCapturedLValue(base, updated, false, false, location);
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
      destination.kind == CapturedLValue::Kind::PackedDynamicSlice)
    values.push_back(destination.reference);
  for (const CapturedLValue &child : destination.children)
    appendCapturedValues(child, values);
  if (destination.kind == CapturedLValue::Kind::ContainerElement ||
      destination.kind == CapturedLValue::Kind::AssociativeElement) {
    values.push_back(destination.container);
    values.push_back(destination.index);
  } else if (destination.kind == CapturedLValue::Kind::StringCharacter ||
             destination.kind == CapturedLValue::Kind::PackedDynamicSlice) {
    values.push_back(destination.index);
  } else if (destination.kind == CapturedLValue::Kind::PackedValueSlice &&
             destination.index) {
    values.push_back(destination.index);
  }
}

LogicalResult UnitLowering::replaceCapturedValues(CapturedLValue &destination,
                                                  ValueRange values,
                                                  unsigned &next) {
  if (destination.kind == CapturedLValue::Kind::Reference ||
      destination.kind == CapturedLValue::Kind::PackedDynamicSlice) {
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
             destination.kind == CapturedLValue::Kind::PackedDynamicSlice) {
    if (next >= values.size())
      return failure();
    destination.index = values[next++];
  } else if (destination.kind == CapturedLValue::Kind::PackedValueSlice &&
             destination.index) {
    if (next >= values.size())
      return failure();
    destination.index = values[next++];
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

LogicalResult UnitLowering::lowerClockingOutputAssignment(
    semantic::SVMemberAccessExpressionOp destination, Value value,
    Location location) {
  bool synchronizedToCurrentEvent =
      isCurrentClockingOccurrence(current);
  SmallVector<Operation *> children = getChildren(destination);
  if (children.size() != 1)
    return emitError(location) << "clocking output has no interface receiver";
  FailureOr<Value> interface = lowerExpression(children.front());
  FailureOr<Type> elementType = getNormalizedSemanticType(destination);
  if (failed(interface) || failed(elementType))
    return failure();
  FailureOr<Value> target = lowerVirtualInterfaceMember(
      destination, *interface, *elementType, /*lvalue=*/true);
  FailureOr<Value> clock = lowerVirtualInterfaceClock(destination, *interface);
  if (failed(target) || failed(clock))
    return failure();
  auto targetRef = dyn_cast<sim::RefType>((*target).getType());
  if (!targetRef) {
    emitError(location)
        << "clocking output to a resolved net is not yet supported";
    return failure();
  }

  auto eventEdge = destination->getAttrOfType<semantic::EdgeKindAttr>(
      "virtual_interface_clock_event_edge");
  auto skewEdge = destination->getAttrOfType<semantic::EdgeKindAttr>(
      "virtual_interface_clock_output_skew_edge");
  if (!eventEdge ||
      destination->hasAttr("virtual_interface_clock_event_has_iff")) {
    emitError(location) << "clocking output has no supported static event";
    return failure();
  }
  semantic::EdgeKind selectedEdge =
      skewEdge && skewEdge.getValue() != semantic::EdgeKind::Change
          ? skewEdge.getValue()
          : eventEdge.getValue();
  sim::EdgeKind edge = static_cast<sim::EdgeKind>(selectedEdge);

  uint64_t delayTicks = 0;
  if (auto spelling = destination->getAttrOfType<StringAttr>(
          "virtual_interface_clock_output_skew_delay")) {
    auto unit = destination->getAttrOfType<IntegerAttr>(
        "virtual_interface_clock_time_unit_fs");
    auto precision = destination->getAttrOfType<IntegerAttr>(
        "virtual_interface_clock_time_precision_fs");
    if (!unit || !precision || unit.getValue().isZero() ||
        precision.getValue().isZero()) {
      emitError(location) << "clocking output has invalid time scale";
      return failure();
    }
    uint64_t unitFS = unit.getValue().getZExtValue();
    uint64_t precisionFS = precision.getValue().getZExtValue();
    auto isRealAttr = destination->getAttrOfType<BoolAttr>(
        "virtual_interface_clock_output_skew_delay_is_real");
    bool isReal = isRealAttr && isRealAttr.getValue();
    long double scaled = 0;
    if (isReal) {
      double amount = 0;
      if (spelling.getValue().getAsDouble(amount) || !std::isfinite(amount) ||
          amount < 0) {
        emitError(location) << "clocking output skew is not finite and nonnegative";
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
        scaled = static_cast<long double>(parsed->value.getZExtValue()) *
                 unitFS;
    }
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
  uint64_t codeUnitID = stableCodeUnitID(identity);
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
                           (*target).getType(), (*clock).getType(),
                           value.getType()};
  SmallVector<DictionaryAttr> argumentAttrs{
      captureMetadata(outlineBuilder, sim::CaptureKind::Context),
      captureMetadata(outlineBuilder, sim::CaptureKind::Formal),
      captureMetadata(outlineBuilder, sim::CaptureKind::Formal),
      captureMetadata(outlineBuilder, sim::CaptureKind::Value)};
  SmallVector<NamedAttribute> attributes{
      outlineBuilder.getNamedAttr("code_unit_id",
                                  outlineBuilder.getI64IntegerAttr(codeUnitID)),
      outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
      outlineBuilder.getNamedAttr(
          "home_region",
          synchronizedToCurrentEvent
              ? sim::EventRegionAttr::get(context, sim::EventRegion::Reactive)
              : function.getHomeRegionAttr()),
      outlineBuilder.getNamedAttr(
          "domain", function.getDomainAttr()),
      outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                  outlineBuilder.getStringAttr(identity))};
  sim::SimFuncOp driver = sim::SimFuncOp::create(
      outlineBuilder, location, identity,
      FunctionType::get(context, inputs, TypeRange{}), sim::EntryKind::Fork,
      attributes, argumentAttrs);
  SymbolTable::setSymbolVisibility(driver, SymbolTable::Visibility::Private);
  Block &entry = driver.getBody().front();
  Block *wait = synchronizedToCurrentEvent ? nullptr : new Block();
  Block *drive = new Block();
  if (wait)
    driver.getBody().push_back(wait);
  driver.getBody().push_back(drive);
  OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
  if (wait) {
    cf::BranchOp::create(entryBuilder, location, wait);
    OpBuilder waitBuilder = OpBuilder::atBlockEnd(wait);
    sim::SimSuspendEdgeOp::create(
        waitBuilder, location, edge, entry.getArgument(2), ValueRange{},
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, drive);
  } else {
    // A drive issued by a continuation of @(clocking_block) belongs to that
    // clocking occurrence. Waiting here would silently move it one cycle.
    cf::BranchOp::create(entryBuilder, location, drive);
  }
  OpBuilder driveBuilder = OpBuilder::atBlockEnd(drive);
  Value delay;
  if (delayTicks)
    delay = sim::SimTimeConstantOp::create(
        driveBuilder, location, sim::TimeType::get(context),
        driveBuilder.getI64IntegerAttr(delayTicks));
  sim::SimNBAEnqueueOp::create(driveBuilder, location, entry.getArgument(3),
                               entry.getArgument(1), delay,
                               sim::NBASiteAttr{});
  sim::SimReturnOp::create(driveBuilder, location, ValueRange{});
  driver->setAttr(sim::metadata::lowered, builder.getUnitAttr());
  Value processContext = function.getBody().front().getArgument(0);
  sim::SimSpawnOp::create(builder, location, driver.getSymNameAttr(),
                          ValueRange{processContext, *target, *clock, value},
                          ArrayAttr{}, ArrayAttr{});
  return success();
}

FailureOr<Value>
UnitLowering::readBitStreamValue(Value stream, Value start, Type type,
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
        builder, location, scalarType,
        builder.getIntegerAttr(scalarType, 0));
  }
  for (unsigned ordinal = 0; ordinal < *width; ++ordinal) {
    Value sourceIndex = arith::AddIOp::create(
        builder, location, start, i64Constant(ordinal));
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

FailureOr<Value> UnitLowering::lowerStreamingAssignment(
    semantic::SVStreamingConcatenationExpressionOp destination,
    Value source) {
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
  Value one = arith::ConstantOp::create(
      builder, location, builder.getI64Type(), builder.getI64IntegerAttr(1));
  if (failed(appendToBitStream(source, *generic, zero, fourState, location)))
    return failure();
  FailureOr<Value> reordered =
      reorderBitStream(*generic, destination.getSliceSize(), location);
  if (failed(reordered))
    return failure();
  Value sourceSize = sim::SimContainerSizeOp::create(
      builder, location, builder.getI64Type(), *reordered);

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
    std::optional<unsigned> width =
        sim::getPackedWidth(dynamic ? elementType : *type);
    Type scalar = sim::getPackedScalarType(dynamic ? elementType : *type);
    if (!width || *width == 0 || !scalar)
      return emitError(getSemanticLocation(target))
                 << "streaming assignment targets must be packed values or "
                    "sequential containers of packed values",
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
    infos.push_back(
        {target, withRange, *type, elementType, *width, dynamic});
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
  Value enough = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::uge, sourceSize,
      i64Constant(fixedWidth));
  if (failed(requireRuntime(enough,
                            "streaming assignment source has too few bits")))
    return failure();

  bool usedGreedy = false;
  Value cursor = zero;
  for (size_t targetIndex = 0; targetIndex < infos.size(); ++targetIndex) {
    TargetInfo &info = infos[targetIndex];
    if (!info.dynamic) {
      FailureOr<Value> value =
          readBitStreamValue(*reordered, cursor, info.type,
                             getSemanticLocation(info.node));
      if (failed(value) ||
          failed(writeLValue(info.node, *value, false, false,
                             getSemanticLocation(info.node))))
        return failure();
      cursor = arith::AddIOp::create(builder, location, cursor,
                                     i64Constant(info.width));
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
        resizeSize =
            arith::AddIOp::create(builder, location, rangeFirst, one);
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
          rangeAscends = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::slt, rangeFirst,
              *second);
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
            resizeSize = arith::AddIOp::create(builder, location, rangeFirst,
                                               count);
          } else {
            Value fits = arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::uge,
                arith::AddIOp::create(builder, location, rangeFirst, one),
                count);
            if (failed(requireRuntime(
                    fits, "streaming with indexed-down range is out of bounds")))
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
      Value selectedBits = arith::MulIOp::create(
          builder, location, count, i64Constant(info.width));
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
      count = arith::DivUIOp::create(builder, location, remaining, elementWidth);
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
      Value needsGrowth = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, previousSize,
          resizeSize);
      containerSize = arith::SelectOp::create(
          builder, location, needsGrowth, resizeSize, previousSize);
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
      Value initializeMore = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, initializeIndex,
          containerSize);
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
      Value copyMore = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, copyIndex,
          previousSize);
      cf::CondBranchOp::create(builder, location, copyMore, copyBody,
                               ValueRange{}, copyExit, ValueRange{});
      setCurrent(copyBody);
      Value oldElement = sim::SimContainerReadOp::create(
          builder, location, info.elementType, previous, copyIndex);
      sim::SimContainerWriteOp::create(builder, location, container, copyIndex,
                                       oldElement);
      Value copyNext =
          arith::AddIOp::create(builder, location, copyIndex, one);
      cf::BranchOp::create(builder, location, copyHeader,
                           ValueRange{copyNext});
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
    FailureOr<Value> element = readBitStreamValue(
        *reordered, elementStart, info.elementType,
        getSemanticLocation(info.node));
    if (failed(element))
      return failure();
    Value destinationIndex = elementIndex;
    if (info.withRange) {
      Value above = arith::AddIOp::create(builder, location, rangeFirst,
                                          elementIndex);
      Value below = arith::SubIOp::create(builder, location, rangeFirst,
                                          elementIndex);
      switch (rangeKind) {
      case semantic::SVRangeSelectionKind::Simple:
        destinationIndex = arith::SelectOp::create(
            builder, location, rangeAscends, above, below);
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
    Value next =
        arith::AddIOp::create(builder, location, elementIndex, one);
    cf::BranchOp::create(builder, location, header, ValueRange{next});
    setCurrent(exit);
    if (failed(writeLValue(info.node, container, false, false,
                           getSemanticLocation(info.node))))
      return failure();
    cursor = arith::AddIOp::create(
        builder, location, cursor,
        arith::MulIOp::create(builder, location, count,
                              i64Constant(info.width)));
  }
  return source;
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
  if (auto streaming =
          dyn_cast<semantic::SVStreamingConcatenationExpressionOp>(destination)) {
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
  if (auto member = dyn_cast<semantic::SVMemberAccessExpressionOp>(destination);
      member && member->hasAttr("virtual_interface_clocking")) {
    if (timed) {
      emitError(location)
          << "an assignment timing control cannot be combined with a clocking output";
      return failure();
    }
    if (failed(lowerClockingOutputAssignment(member, *value, location)))
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
    unsupported(control)
        << " (nonblocking intra-assignment event/repeat control requires a "
           "scheduler-owned deferred action)";
    return failure();
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
    } else
      sim::SimDriverDriveOp::create(builder, location, destination, *converted);
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
      source = arith::ConstantOp::create(
          builder, location, integer, builder.getIntegerAttr(integer, bits));
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
  return writeLValue(children[0], *converted, isSignedNode(children[1]), false,
                     location);
}

} // namespace obelisk::simlowering
