//===- SemanticUtils.cpp - Shared semantic-to-simulation helpers --------===//

#include "Detail.h"

#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringExtras.h"

#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <string>

using namespace mlir;

namespace obelisk::simlowering {

StringAttr getSimulationClassSymbol(SymbolRefAttr semanticClass) {
  std::string name = "__obelisk_class_";
  StringRef leaf = semanticClass.getLeafReference();
  name.reserve(name.size() + leaf.size());
  for (char character : leaf)
    name.push_back(
        std::isalnum(static_cast<unsigned char>(character)) ? character : '_');
  return StringAttr::get(semanticClass.getContext(), name);
}

StringAttr getSimulationCovergroupSymbol(SymbolRefAttr semanticCovergroup) {
  std::string name = "__obelisk_covergroup_";
  StringRef leaf = semanticCovergroup.getLeafReference();
  name.reserve(name.size() + leaf.size());
  for (char character : leaf)
    name.push_back(
        std::isalnum(static_cast<unsigned char>(character)) ? character : '_');
  return StringAttr::get(semanticCovergroup.getContext(), name);
}

bool isSemanticOp(Operation *op) {
  return op->hasTrait<OpTrait::SemanticASTNode>();
}

bool isCodeUnit(Operation *op) {
  return isa<
      semantic::SVProceduralBlockSymbolOp, semantic::SVContinuousAssignSymbolOp,
      semantic::SVPrimitiveInstanceSymbolOp, semantic::SVSubroutineSymbolOp>(
      op);
}

bool isCompileTimeOnlyInstanceMember(Operation *op) {
  for (Operation *cursor = op; cursor; cursor = cursor->getParentOp()) {
    if (isa<semantic::SVClassTypeOp, semantic::SVCovergroupTypeOp>(cursor))
      return false;
    auto instance = dyn_cast<semantic::SVInstanceSymbolOp>(cursor);
    if (instance &&
        instance.getIsVirtualInterfaceTypeInstance().value_or(false))
      return true;
  }
  return false;
}

Location getSemanticLocation(Operation *op) {
  if (auto typeAttr = op->getAttrOfType<TypeAttr>("source_range")) {
    if (auto range = dyn_cast<semantic::SourceRangeType>(typeAttr.getValue()))
      return FileLineColLoc::get(op->getContext(), range.getStartFile(),
                                 range.getStartLine(), range.getStartColumn());
  }
  if (auto file = op->getAttrOfType<StringAttr>("source_file"))
    return FileLineColLoc::get(op->getContext(), file.getValue(), 1, 1);
  return op->getLoc();
}

SmallVector<Operation *> getChildren(Operation *op) {
  SmallVector<Operation *> children;
  if (op->getNumRegions() && !op->getRegion(0).empty())
    for (Operation &child : op->getRegion(0).front())
      children.push_back(&child);
  return children;
}

std::optional<StringRef> getConstantSpelling(Operation *operation) {
  if (auto literal = dyn_cast<semantic::SVIntegerLiteralOp>(operation))
    return literal.getConstantValue();
  if (auto literal =
          dyn_cast<semantic::SVUnbasedUnsizedIntegerLiteralOp>(operation))
    return literal.getConstantValue();
  if (auto constant =
          operation->getAttrOfType<StringAttr>("obelisk_sim.constant_value"))
    return constant.getValue();
  if (auto constant =
          operation->getAttrOfType<StringAttr>(staticNetConstantAttrName))
    return constant.getValue();
  return std::nullopt;
}

Attribute foldConstantValue(Value value) {
  llvm::DenseMap<Value, Attribute> constants;
  llvm::DenseSet<Value> active;
  std::function<Attribute(Value)> foldValue = [&](Value current) -> Attribute {
    if (auto found = constants.find(current); found != constants.end())
      return found->second;
    if (!active.insert(current).second)
      return {};

    auto finish = [&](Attribute result) {
      active.erase(current);
      if (result)
        constants.try_emplace(current, result);
      return result;
    };

    Attribute direct;
    if (matchPattern(current, m_Constant(&direct)))
      return finish(direct);

    auto result = dyn_cast<OpResult>(current);
    if (!result)
      return finish({});
    Operation *producer = result.getOwner();
    SmallVector<Attribute> operands;
    operands.reserve(producer->getNumOperands());
    for (Value operand : producer->getOperands()) {
      Attribute constant = foldValue(operand);
      if (!constant)
        return finish({});
      operands.push_back(constant);
    }

    SmallVector<OpFoldResult> folded;
    if (failed(producer->fold(operands, folded)) ||
        folded.size() != producer->getNumResults())
      return finish({});
    OpFoldResult replacement = folded[result.getResultNumber()];
    if (!replacement)
      return finish({});
    if (auto attribute = dyn_cast<Attribute>(replacement))
      return finish(attribute);
    Value replacementValue = cast<Value>(replacement);
    if (replacementValue == current)
      return finish({});
    return finish(foldValue(replacementValue));
  };
  return foldValue(value);
}

std::optional<bool> foldConstantTruth(Value value) {
  auto integer = dyn_cast_or_null<IntegerAttr>(foldConstantValue(value));
  if (!integer)
    return std::nullopt;
  return !integer.getValue().isZero();
}

bool isAddressableExpression(Operation *operation) {
  if (isa<semantic::SVNamedValueExpressionOp,
          semantic::SVHierarchicalValueExpressionOp>(operation))
    return true;
  if (isa<semantic::SVMemberAccessExpressionOp>(operation)) {
    SmallVector<Operation *> children = getChildren(operation);
    return !children.empty() && isAddressableExpression(children.front());
  }
  if (!isa<semantic::SVElementSelectExpressionOp,
           semantic::SVRangeSelectExpressionOp>(operation))
    return false;
  SmallVector<Operation *> children = getChildren(operation);
  size_t expected =
      isa<semantic::SVElementSelectExpressionOp>(operation) ? 2u : 3u;
  if (children.size() != expected || !isAddressableExpression(children.front()))
    return false;
  return llvm::all_of(
      ArrayRef<Operation *>(children).drop_front(),
      [](Operation *index) { return getConstantSpelling(index).has_value(); });
}

bool isUnboundedEndpoint(Operation *operation) {
  while (isa<semantic::SVConversionExpressionOp>(operation)) {
    SmallVector<Operation *> children = getChildren(operation);
    if (children.size() != 1)
      return false;
    operation = children.front();
  }
  return isa<semantic::SVUnboundedLiteralOp>(operation);
}

uint64_t stableCodeUnitID(StringRef key) {
  uint64_t hash = obelisk_stable_hash(key.data(), key.size());
  hash &= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  return hash == 0 ? 1 : hash;
}

bool isStaticallyAllocatedOverrideTarget(Value value) {
  while (value) {
    if (auto extract = value.getDefiningOp<sim::SimRefExtractOp>()) {
      value = extract.getInput();
      continue;
    }
    if (auto extract = value.getDefiningOp<sim::SimNetExtractOp>()) {
      value = extract.getInput();
      continue;
    }
    if (value.getDefiningOp<sim::SimRefAllocOp>())
      return false;
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      auto function =
          dyn_cast_or_null<sim::SimFuncOp>(argument.getOwner()->getParentOp());
      return function &&
             !function.getArgAttr(argument.getArgNumber(),
                                  "obelisk_sim.automatic_reference_capture");
    }
    return true;
  }
  return false;
}

static std::optional<uint64_t> getRangeExtent(int64_t left, int64_t right) {
  uint64_t lhs = static_cast<uint64_t>(left);
  uint64_t rhs = static_cast<uint64_t>(right);
  uint64_t distance = left >= right ? lhs - rhs : rhs - lhs;
  if (distance == std::numeric_limits<uint64_t>::max())
    return std::nullopt;
  return distance + 1;
}

static std::optional<uint64_t>
checkedArrayWidth(std::optional<uint64_t> elementWidth, uint64_t count) {
  if (!elementWidth ||
      (count && *elementWidth > std::numeric_limits<uint64_t>::max() / count))
    return std::nullopt;
  return *elementWidth * count;
}

std::optional<uint64_t> getSemanticBitstreamWidth(Type type) {
  if (auto integral = dyn_cast<semantic::IntegralType>(type))
    return integral.getWidth();
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.getWidth();
  if (auto logic = dyn_cast<semantic::LogicType>(type))
    return logic.getWidth();
  if (isa<semantic::TimeType>(type))
    return 64;
  if (isa<semantic::RealType, semantic::RealtimeType>(type) || type.isF64())
    return 64;
  if (isa<semantic::ShortRealType>(type) || type.isF32())
    return 32;
  if (auto enumeration = dyn_cast<semantic::EnumType>(type))
    return getSemanticBitstreamWidth(enumeration.getBaseType());

  if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type)) {
    auto count = getRangeExtent(array.getLeft(), array.getRight());
    return count
               ? checkedArrayWidth(
                     getSemanticBitstreamWidth(array.getElementType()), *count)
               : std::nullopt;
  }
  if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(type)) {
    auto count = getRangeExtent(array.getLeft(), array.getRight());
    return count
               ? checkedArrayWidth(
                     getSemanticBitstreamWidth(array.getElementType()), *count)
               : std::nullopt;
  }
  if (auto array = dyn_cast<semantic::PackedArrayType>(type))
    return checkedArrayWidth(getSemanticBitstreamWidth(array.getElementType()),
                             array.getSize());
  if (auto array = dyn_cast<semantic::UnpackedArrayType>(type))
    return checkedArrayWidth(getSemanticBitstreamWidth(array.getElementType()),
                             array.getSize());

  // The elaborator computes this from the full source field inventory,
  // including tagged-union discriminants and unpacked aggregate members.
  if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type))
    return aggregate.getBitstreamWidth();

  auto dictionaryWidth = [&](DictionaryAttr fields,
                             bool isUnion) -> std::optional<uint64_t> {
    uint64_t width = 0;
    for (NamedAttribute field : fields) {
      auto fieldType = dyn_cast<TypeAttr>(field.getValue());
      std::optional<uint64_t> fieldWidth =
          fieldType ? getSemanticBitstreamWidth(fieldType.getValue())
                    : std::nullopt;
      if (!fieldWidth)
        return std::nullopt;
      if (isUnion) {
        width = std::max(width, *fieldWidth);
      } else {
        if (width > std::numeric_limits<uint64_t>::max() - *fieldWidth)
          return std::nullopt;
        width += *fieldWidth;
      }
    }
    return width;
  };
  if (auto structure = dyn_cast<semantic::PackedStructType>(type))
    return dictionaryWidth(structure.getFields(), false);
  if (auto structure = dyn_cast<semantic::UnpackedStructType>(type))
    return dictionaryWidth(structure.getFields(), false);
  if (auto unionType = dyn_cast<semantic::PackedUnionType>(type))
    return dictionaryWidth(unionType.getFields(), true);
  if (auto unionType = dyn_cast<semantic::UnpackedUnionType>(type))
    return dictionaryWidth(unionType.getFields(), true);
  return std::nullopt;
}

SmallVector<SemanticDimension> getSemanticDimensions(Type type) {
  SmallVector<SemanticDimension> dimensions;
  auto appendFixed = [&](bool unpacked, int64_t left, int64_t right) {
    dimensions.push_back(
        {SemanticDimensionKind::Fixed, unpacked, left, right, {}});
  };
  auto appendRuntime = [&](SemanticDimensionKind kind, bool unpacked,
                           Type indexType = {}) {
    dimensions.push_back({kind, unpacked, 0, 0, indexType});
  };

  while (type) {
    if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type)) {
      appendFixed(false, array.getLeft(), array.getRight());
      type = array.getElementType();
      continue;
    }
    if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(type)) {
      appendFixed(true, array.getLeft(), array.getRight());
      type = array.getElementType();
      continue;
    }
    if (auto array = dyn_cast<semantic::PackedArrayType>(type)) {
      appendFixed(false, static_cast<int64_t>(array.getSize()) - 1, 0);
      type = array.getElementType();
      continue;
    }
    if (auto array = dyn_cast<semantic::UnpackedArrayType>(type)) {
      appendFixed(true, static_cast<int64_t>(array.getSize()) - 1, 0);
      type = array.getElementType();
      continue;
    }
    if (auto array = dyn_cast<semantic::DynArrayType>(type)) {
      appendRuntime(SemanticDimensionKind::DynamicArray, true);
      type = array.getElementType();
      continue;
    }
    if (auto queue = dyn_cast<semantic::QueueType>(type)) {
      appendRuntime(SemanticDimensionKind::Queue, true);
      type = queue.getElementType();
      continue;
    }
    if (auto array = dyn_cast<semantic::AssocArrayType>(type)) {
      appendRuntime(SemanticDimensionKind::AssociativeArray, true,
                    array.getKeyType());
      type = array.getElementType();
      continue;
    }
    if (auto array = dyn_cast<semantic::OpenArrayType>(type)) {
      appendRuntime(SemanticDimensionKind::OpenArray, !array.getIsPacked());
      type = array.getElementType();
      continue;
    }
    if (isa<semantic::StringType>(type)) {
      appendRuntime(SemanticDimensionKind::String, false);
      break;
    }
    if (auto enumeration = dyn_cast<semantic::EnumType>(type)) {
      std::optional<uint64_t> width =
          getSemanticBitstreamWidth(enumeration.getBaseType());
      if (width && *width)
        appendFixed(false, static_cast<int64_t>(*width - 1), 0);
      break;
    }
    if (auto integral = dyn_cast<semantic::IntegralType>(type)) {
      // Scalar bit / logic / reg types have no dimensions. An explicit
      // one-element packed range is represented by RangedPackedArrayType and
      // therefore remains distinguishable here.
      switch (integral.getFlavor()) {
      case semantic::SVIntegralFlavor::Bit:
      case semantic::SVIntegralFlavor::Logic:
      case semantic::SVIntegralFlavor::Reg:
        break;
      default:
        appendFixed(false, integral.getLeft(), integral.getRight());
        break;
      }
      break;
    }
    if (isa<semantic::TimeType>(type)) {
      appendFixed(false, 63, 0);
      break;
    }
    if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type)) {
      if (aggregate.getIsPacked() && aggregate.getBitWidth())
        appendFixed(false, static_cast<int64_t>(aggregate.getBitWidth() - 1),
                    0);
      break;
    }
    if (auto structure = dyn_cast<semantic::PackedStructType>(type)) {
      std::optional<uint64_t> width = getSemanticBitstreamWidth(structure);
      if (width && *width)
        appendFixed(false, static_cast<int64_t>(*width - 1), 0);
      break;
    }
    if (auto unionType = dyn_cast<semantic::PackedUnionType>(type)) {
      std::optional<uint64_t> width = getSemanticBitstreamWidth(unionType);
      if (width && *width)
        appendFixed(false, static_cast<int64_t>(*width - 1), 0);
      break;
    }
    if (auto integer = dyn_cast<IntegerType>(type)) {
      if (integer.getWidth() > 1)
        appendFixed(false, integer.getWidth() - 1, 0);
      break;
    }
    if (auto logic = dyn_cast<semantic::LogicType>(type)) {
      if (logic.getWidth() > 1)
        appendFixed(false, logic.getWidth() - 1, 0);
      break;
    }
    break;
  }
  return dimensions;
}

static std::optional<uint64_t> getSemanticPackedWidth(Type type) {
  if (auto integral = dyn_cast<semantic::IntegralType>(type))
    return integral.getWidth();
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.getWidth();
  if (auto logic = dyn_cast<semantic::LogicType>(type))
    return logic.getWidth();
  if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type)) {
    auto count = getRangeExtent(array.getLeft(), array.getRight());
    return count ? checkedArrayWidth(
                       getSemanticPackedWidth(array.getElementType()), *count)
                 : std::nullopt;
  }
  if (auto array = dyn_cast<semantic::PackedArrayType>(type)) {
    return checkedArrayWidth(getSemanticPackedWidth(array.getElementType()),
                             array.getSize());
  }
  if (auto enumeration = dyn_cast<semantic::EnumType>(type))
    return getSemanticPackedWidth(enumeration.getBaseType());
  if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type))
    return aggregate.getIsPacked()
               ? std::optional<uint64_t>(aggregate.getBitWidth())
               : std::nullopt;
  auto dictionaryWidth = [&](DictionaryAttr fields,
                             bool isUnion) -> std::optional<uint64_t> {
    uint64_t width = 0;
    for (NamedAttribute field : fields) {
      auto fieldType = dyn_cast<TypeAttr>(field.getValue());
      std::optional<uint64_t> fieldWidth =
          fieldType ? getSemanticPackedWidth(fieldType.getValue())
                    : std::nullopt;
      if (!fieldWidth)
        return std::nullopt;
      if (isUnion) {
        width = std::max(width, *fieldWidth);
      } else {
        if (width > std::numeric_limits<uint64_t>::max() - *fieldWidth)
          return std::nullopt;
        width += *fieldWidth;
      }
    }
    return width;
  };
  if (auto structure = dyn_cast<semantic::PackedStructType>(type))
    return dictionaryWidth(structure.getFields(), false);
  if (auto unionType = dyn_cast<semantic::PackedUnionType>(type))
    return dictionaryWidth(unionType.getFields(), true);
  return std::nullopt;
}

static bool isFourState(Type type) {
  if (auto integral = dyn_cast<semantic::IntegralType>(type))
    return integral.getIsFourState();
  if (isa<semantic::LogicType, semantic::TimeType>(type))
    return true;
  if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type))
    return isFourState(array.getElementType());
  if (auto array = dyn_cast<semantic::PackedArrayType>(type))
    return isFourState(array.getElementType());
  if (auto enumeration = dyn_cast<semantic::EnumType>(type))
    return isFourState(enumeration.getBaseType());
  if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type))
    return aggregate.getIsPacked() && aggregate.getIsFourState();
  auto dictionaryIsFourState = [&](DictionaryAttr fields) {
    return llvm::any_of(fields, [&](NamedAttribute field) {
      auto fieldType = dyn_cast<TypeAttr>(field.getValue());
      return fieldType && isFourState(fieldType.getValue());
    });
  };
  if (auto structure = dyn_cast<semantic::PackedStructType>(type))
    return dictionaryIsFourState(structure.getFields());
  if (auto unionType = dyn_cast<semantic::PackedUnionType>(type))
    return dictionaryIsFourState(unionType.getFields());
  return false;
}

bool isSignedSemanticType(Type type) {
  if (auto integral = dyn_cast<semantic::IntegralType>(type))
    return integral.getIsSigned();
  if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type))
    return isSignedSemanticType(array.getElementType());
  if (auto enumeration = dyn_cast<semantic::EnumType>(type))
    return isSignedSemanticType(enumeration.getBaseType());
  if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type))
    return aggregate.getIsPacked() && aggregate.getIsSigned();
  return false;
}

static FailureOr<Type> normalizeType(Type type, Location location,
                                     bool allowRealScalar = true);

static FailureOr<ArrayAttr> normalizeSourceFields(ArrayAttr fields,
                                                  Location location,
                                                  bool allowVoidFields,
                                                  bool allowRealFields) {
  SmallVector<Attribute> normalized;
  normalized.reserve(fields.size());
  for (Attribute attribute : fields) {
    auto field = dyn_cast<DictionaryAttr>(attribute);
    auto name = field ? field.getAs<StringAttr>("name") : StringAttr{};
    auto typeAttr = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
    auto ordinal = field ? field.getAs<IntegerAttr>("ordinal") : IntegerAttr{};
    auto offset =
        field ? field.getAs<IntegerAttr>("packed_offset") : IntegerAttr{};
    if (!name || !typeAttr || !ordinal || !offset ||
        ordinal.getValue().isNegative() || offset.getValue().isNegative()) {
      emitError(location) << "malformed source aggregate field inventory";
      return failure();
    }
    FailureOr<Type> fieldType =
        allowVoidFields && isa<semantic::VoidType>(typeAttr.getValue())
            ? FailureOr<Type>(IntegerType::get(fields.getContext(), 1))
            : normalizeType(typeAttr.getValue(), location, allowRealFields);
    if (failed(fieldType))
      return failure();
    normalized.push_back(sim::FieldAttr::get(
        fields.getContext(), name, *fieldType,
        static_cast<uint32_t>(ordinal.getValue().getZExtValue()),
        offset.getValue().getZExtValue()));
  }
  return ArrayAttr::get(fields.getContext(), normalized);
}

static FailureOr<ArrayAttr> normalizeDictionaryFields(DictionaryAttr fields,
                                                      bool packed, bool isUnion,
                                                      Location location) {
  SmallVector<Type> types;
  SmallVector<StringAttr> names;
  for (NamedAttribute field : fields) {
    auto typeAttr = dyn_cast<TypeAttr>(field.getValue());
    if (!typeAttr) {
      emitError(location) << "aggregate field dictionary contains a non-type";
      return failure();
    }
    FailureOr<Type> type = normalizeType(typeAttr.getValue(), location,
                                         /*allowRealScalar=*/!packed);
    if (failed(type))
      return failure();
    names.push_back(field.getName());
    types.push_back(*type);
  }
  SmallVector<uint64_t> offsets(types.size(), 0);
  if (packed && !isUnion) {
    uint64_t offset = 0;
    for (size_t index = types.size(); index != 0; --index) {
      std::optional<unsigned> width = sim::getPackedWidth(types[index - 1]);
      if (!width || *width > std::numeric_limits<uint64_t>::max() - offset) {
        emitError(location) << "packed aggregate field width overflows";
        return failure();
      }
      offsets[index - 1] = offset;
      offset += *width;
    }
  }
  SmallVector<Attribute> normalized;
  for (auto [index, type] : llvm::enumerate(types))
    normalized.push_back(sim::FieldAttr::get(fields.getContext(), names[index],
                                             type, index, offsets[index]));
  return ArrayAttr::get(fields.getContext(), normalized);
}

static FailureOr<Type> normalizeType(Type type, Location location,
                                     bool allowRealScalar) {
  MLIRContext *context = type.getContext();
  if (auto classHandle = dyn_cast<semantic::ClassHandleType>(type))
    return sim::ClassHandleType::get(
        context, FlatSymbolRefAttr::get(
                     getSimulationClassSymbol(classHandle.getClassName())));
  if (auto integer = dyn_cast<IntegerType>(type)) {
    if (!integer.isSignless()) {
      emitError(location) << "signed or unsigned builtin integer survived "
                             "semantic normalization";
      return failure();
    }
    return type;
  }
  if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type)) {
    FailureOr<Type> element = normalizeType(array.getElementType(), location,
                                            /*allowRealScalar=*/false);
    if (failed(element))
      return failure();
    return sim::PackedArrayType::get(context, *element, array.getLeft(),
                                     array.getRight());
  }
  if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(type)) {
    FailureOr<Type> element = normalizeType(array.getElementType(), location,
                                            /*allowRealScalar=*/true);
    if (failed(element))
      return failure();
    return sim::UnpackedArrayType::get(context, *element, array.getLeft(),
                                       array.getRight());
  }
  if (auto array = dyn_cast<semantic::PackedArrayType>(type)) {
    if (array.getSize() == 0 ||
        array.getSize() >
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      emitError(location) << "fixed array size is outside the supported range";
      return failure();
    }
    FailureOr<Type> element = normalizeType(array.getElementType(), location,
                                            /*allowRealScalar=*/false);
    if (failed(element))
      return failure();
    return sim::PackedArrayType::get(context, *element, array.getSize() - 1, 0);
  }
  if (auto array = dyn_cast<semantic::UnpackedArrayType>(type)) {
    if (array.getSize() == 0 ||
        array.getSize() >
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      emitError(location) << "fixed array size is outside the supported range";
      return failure();
    }
    FailureOr<Type> element = normalizeType(array.getElementType(), location,
                                            /*allowRealScalar=*/true);
    if (failed(element))
      return failure();
    return sim::UnpackedArrayType::get(context, *element, array.getSize() - 1,
                                       0);
  }
  if (auto array = dyn_cast<semantic::DynArrayType>(type)) {
    FailureOr<Type> element = normalizeType(array.getElementType(), location,
                                            /*allowRealScalar=*/true);
    if (failed(element))
      return failure();
    return sim::DynamicArrayType::get(context, *element);
  }
  if (auto queue = dyn_cast<semantic::QueueType>(type)) {
    FailureOr<Type> element = normalizeType(queue.getElementType(), location,
                                            /*allowRealScalar=*/true);
    if (failed(element))
      return failure();
    return sim::QueueType::get(context, *element, queue.getBound());
  }
  if (auto array = dyn_cast<semantic::AssocArrayType>(type)) {
    FailureOr<Type> key = normalizeType(array.getKeyType(), location,
                                        /*allowRealScalar=*/false);
    FailureOr<Type> element = normalizeType(array.getElementType(), location,
                                            /*allowRealScalar=*/true);
    if (failed(key) || failed(element))
      return failure();
    return sim::AssocArrayType::get(context, *key, *element,
                                    isSignedSemanticType(array.getKeyType()),
                                    array.getWildcardIndex());
  }
  if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type)) {
    FailureOr<ArrayAttr> fields =
        normalizeSourceFields(aggregate.getFields(), location,
                              aggregate.getIsUnion() && aggregate.getIsTagged(),
                              !aggregate.getIsPacked());
    if (failed(fields))
      return failure();
    if (aggregate.getIsPacked() && aggregate.getIsUnion())
      return sim::PackedUnionType::get(
          context, *fields, aggregate.getIsTagged(), aggregate.getTagBits());
    if (aggregate.getIsPacked())
      return sim::PackedStructType::get(context, *fields);
    if (aggregate.getIsUnion())
      return sim::UnpackedUnionType::get(context, *fields,
                                         aggregate.getIsTagged(), 0);
    return sim::UnpackedStructType::get(context, *fields);
  }
  if (auto structure = dyn_cast<semantic::PackedStructType>(type)) {
    FailureOr<ArrayAttr> fields =
        normalizeDictionaryFields(structure.getFields(), true, false, location);
    return failed(fields)
               ? FailureOr<Type>(failure())
               : FailureOr<Type>(sim::PackedStructType::get(context, *fields));
  }
  if (auto structure = dyn_cast<semantic::UnpackedStructType>(type)) {
    FailureOr<ArrayAttr> fields = normalizeDictionaryFields(
        structure.getFields(), false, false, location);
    return failed(fields) ? FailureOr<Type>(failure())
                          : FailureOr<Type>(
                                sim::UnpackedStructType::get(context, *fields));
  }
  if (auto unionType = dyn_cast<semantic::PackedUnionType>(type)) {
    FailureOr<ArrayAttr> fields =
        normalizeDictionaryFields(unionType.getFields(), true, true, location);
    return failed(fields) ? FailureOr<Type>(failure())
                          : FailureOr<Type>(sim::PackedUnionType::get(
                                context, *fields, false, 0));
  }
  if (auto unionType = dyn_cast<semantic::UnpackedUnionType>(type)) {
    FailureOr<ArrayAttr> fields =
        normalizeDictionaryFields(unionType.getFields(), false, true, location);
    return failed(fields) ? FailureOr<Type>(failure())
                          : FailureOr<Type>(sim::UnpackedUnionType::get(
                                context, *fields, false, 0));
  }
  if (auto width = getSemanticPackedWidth(type)) {
    if (*width == 0 || *width > std::numeric_limits<unsigned>::max()) {
      emitError(location) << "packed type has unsupported width " << *width;
      return failure();
    }
    if (isFourState(type))
      return sim::LogicType::get(context, static_cast<unsigned>(*width));
    return IntegerType::get(context, static_cast<unsigned>(*width));
  }
  // `time` is a four-state 64-bit unsigned integer, not a two-state one
  // (IEEE 1800-2017 Table 6-8), so an uninitialized one reads as x and its
  // arithmetic propagates unknown bits like any other four-state value.
  if (isa<semantic::TimeType>(type))
    return sim::LogicType::get(context, 64);
  if (isa<semantic::RealType, semantic::RealtimeType>(type)) {
    if (!allowRealScalar) {
      emitError(location)
          << "real and realtime are supported only as scalar variables";
      return failure();
    }
    return Float64Type::get(context);
  }
  if (isa<semantic::ShortRealType>(type)) {
    if (!allowRealScalar) {
      emitError(location) << "shortreal is not permitted in this packed type";
      return failure();
    }
    return Float32Type::get(context);
  }
  if (isa<semantic::EventType>(type))
    return sim::EventType::get(context);
  if (auto covergroup = dyn_cast<semantic::CovergroupHandleType>(type))
    return sim::CovergroupHandleType::get(
        context, SymbolRefAttr::get(context, getSimulationCovergroupSymbol(
                                                 covergroup.getCovergroupName())
                                                 .getValue()));
  if (isa<semantic::StringType>(type))
    return sim::StringType::get(context);
  if (type.isF64() || type.isF32())
    return type;
  if (isa<sim::LogicType, sim::TimeType, sim::ContextType, sim::RefType,
          sim::NetType, sim::DriverType, sim::EventType, sim::ProcessType,
          sim::ClassHandleType, sim::StringType, sim::DynamicArrayType,
          sim::QueueType, sim::AssocArrayType, sim::ManagedRefType>(type) ||
      sim::isAggregateType(type))
    return type;

  emitError(location) << "unsupported semantic type in the first simulation "
                         "slice: "
                      << type;
  return failure();
}

FailureOr<Type> getNormalizedSemanticType(Operation *op) {
  auto typeAttr = op->getAttrOfType<TypeAttr>("semantic_type");
  if (!typeAttr) {
    op->emitError(
        "semantic node requires semantic_type for simulation lowering");
    return failure();
  }
  return normalizeType(typeAttr.getValue(), getSemanticLocation(op));
}

FailureOr<Type> normalizeSemanticType(Type type, Location location) {
  return normalizeType(type, location);
}

FailureOr<DPIABIKind> getDPIABIKind(Type type, Location location) {
  FailureOr<DPIABIType> classified = classifyDPIABIType(type, location);
  if (failed(classified))
    return failure();
  return classified->kind;
}

} // namespace obelisk::simlowering

namespace obelisk {

FailureOr<DPIABIType> classifyDPIABIType(Type type, Location location) {
  using namespace simlowering;
  namespace semantic = ::obelisk::ir;
  if (isa<semantic::DynArrayType, semantic::QueueType, semantic::AssocArrayType,
          semantic::OpenArrayType, sim::DynamicArrayType, sim::QueueType,
          sim::AssocArrayType>(type)) {
    emitError(location)
        << "DPI-C dynamic-array, queue, and associative-array marshalling is "
           "unsupported";
    return failure();
  }
  if (auto enumeration = dyn_cast<semantic::EnumType>(type))
    return classifyDPIABIType(enumeration.getBaseType(), location);
  auto integral = dyn_cast<semantic::IntegralType>(type);
  if (integral) {
    std::optional<DPIABIKind> kind;
    switch (integral.getFlavor()) {
    case semantic::SVIntegralFlavor::Bit:
      kind = integral.getWidth() == 1 ? DPIABIKind::Bit : DPIABIKind::BitVector;
      break;
    case semantic::SVIntegralFlavor::Logic:
    case semantic::SVIntegralFlavor::Reg:
      kind = integral.getWidth() == 1 ? DPIABIKind::Logic
                                      : DPIABIKind::LogicVector;
      break;
    case semantic::SVIntegralFlavor::Byte:
      kind = DPIABIKind::Byte;
      break;
    case semantic::SVIntegralFlavor::ShortInt:
      kind = DPIABIKind::ShortInt;
      break;
    case semantic::SVIntegralFlavor::Int:
      kind = DPIABIKind::Int;
      break;
    case semantic::SVIntegralFlavor::LongInt:
      kind = DPIABIKind::LongInt;
      break;
    case semantic::SVIntegralFlavor::Generic:
      kind = integral.getIsFourState() ? DPIABIKind::LogicVector
                                       : DPIABIKind::BitVector;
      break;
    case semantic::SVIntegralFlavor::Integer:
      emitError(location) << "DPI type category '"
                          << semantic::stringifySVIntegralFlavor(
                                 integral.getFlavor())
                          << "' is not supported by the initial integral ABI";
      return failure();
    }
    if (integral.getWidth() == 0 ||
        integral.getWidth() > std::numeric_limits<uint32_t>::max()) {
      emitError(location) << "DPI packed width is outside the supported range";
      return failure();
    }
    if (!kind) {
      emitError(location) << "unknown DPI integral type category";
      return failure();
    }
    return DPIABIType{*kind, static_cast<uint32_t>(integral.getWidth()),
                      integral.getIsFourState(), integral.getIsSigned()};
  }

  std::optional<uint64_t> width = getSemanticPackedWidth(type);
  bool packedAggregate =
      isa<semantic::RangedPackedArrayType, semantic::PackedArrayType,
          semantic::PackedStructType, semantic::PackedUnionType>(type);
  if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type))
    packedAggregate = aggregate.getIsPacked();
  if (!packedAggregate || !width || *width == 0 ||
      *width > std::numeric_limits<uint32_t>::max()) {
    emitError(location)
        << "DPI imports support only scalar predefined integers, scalar "
           "bit/logic, enums, and fixed packed integral values";
    return failure();
  }
  bool fourState = isFourState(type);
  return DPIABIType{fourState ? DPIABIKind::LogicVector : DPIABIKind::BitVector,
                    static_cast<uint32_t>(*width), fourState,
                    simlowering::isSignedSemanticType(type)};
}

StringRef getDPICTypeSpelling(const DPIABIType &type) {
  switch (type.kind) {
  case DPIABIKind::Bit:
    return "svBit";
  case DPIABIKind::Logic:
    return "svLogic";
  case DPIABIKind::Byte:
    return type.isSigned ? "int8_t" : "uint8_t";
  case DPIABIKind::ShortInt:
    return type.isSigned ? "int16_t" : "uint16_t";
  case DPIABIKind::Int:
    return type.isSigned ? "int32_t" : "uint32_t";
  case DPIABIKind::LongInt:
    return type.isSigned ? "int64_t" : "uint64_t";
  case DPIABIKind::BitVector:
    return "svBitVecVal";
  case DPIABIKind::LogicVector:
    return "svLogicVecVal";
  }
  llvm_unreachable("unknown DPI ABI kind");
}

} // namespace obelisk

namespace obelisk::simlowering {

StringRef getHierarchyName(Operation *op) {
  if (auto name = op->getAttrOfType<StringAttr>("hierarchical_name"))
    return name.getValue();
  if (auto name = op->getAttrOfType<StringAttr>("name"))
    return name.getValue();
  return {};
}

StringRef getDebugName(Operation *op) {
  if (auto name = op->getAttrOfType<StringAttr>("name"))
    return name.getValue();
  return {};
}

FailureOr<ParsedConstant> parseSVInteger(StringRef spelling, unsigned width,
                                         Location location) {
  if (width == 0) {
    emitError(location) << "cannot parse an integer literal at zero width";
    return failure();
  }
  std::string clean;
  clean.reserve(spelling.size());
  for (char c : spelling)
    if (c != '_')
      clean.push_back(static_cast<char>(std::tolower(c)));
  StringRef text(clean);
  bool negative = false;
  if (text.consume_front("-"))
    negative = true;
  else
    text.consume_front("+");
  size_t quote = text.find('\'');
  unsigned radix = 10;
  StringRef digits = text;
  if (quote != StringRef::npos) {
    StringRef suffix = text.drop_front(quote + 1);
    suffix.consume_front("s");
    if (suffix.empty()) {
      emitError(location) << "invalid SystemVerilog integer literal '"
                          << spelling << "'";
      return failure();
    }
    switch (suffix.front()) {
    case 'b':
      radix = 2;
      break;
    case 'o':
      radix = 8;
      break;
    case 'd':
      radix = 10;
      break;
    case 'h':
      radix = 16;
      break;
    default:
      emitError(location) << "unsupported literal base in '" << spelling << "'";
      return failure();
    }
    digits = suffix.drop_front();
  }
  if (digits.empty()) {
    emitError(location) << "invalid SystemVerilog integer literal '" << spelling
                        << "'";
    return failure();
  }

  APInt value(width, 0), unknown(width, 0);
  if (!digits.contains('x') && !digits.contains('z') && !digits.contains('?')) {
    // APInt's string constructor requires a width that can hold the literal,
    // and wraps silently in a no-assert build otherwise. Parse at the width
    // the digits need, then reject anything that does not fit the target.
    for (char c : digits) {
      unsigned digit = llvm::hexDigitValue(c);
      if (digit == static_cast<unsigned>(-1) || digit >= radix) {
        emitError(location)
            << "invalid digit in integer literal '" << spelling << "'";
        return failure();
      }
    }
    unsigned needed = APInt::getSufficientBitsNeeded(digits, radix);
    unsigned parseWidth = std::max(needed, width);
    APInt parsed(parseWidth, digits, radix);
    bool fits = parsed.getActiveBits() <= width;
    if (negative) {
      APInt signedLimit(parseWidth, 1);
      signedLimit <<= width - 1;
      fits = parsed.ule(signedLimit);
    }
    if (!fits) {
      emitError(location) << "integer literal '" << spelling
                          << "' does not fit " << "in " << width << " bits";
      return failure();
    }
    if (negative)
      parsed.negate();
    return ParsedConstant{parsed.trunc(width), unknown};
  }
  if (negative) {
    emitError(location) << "negative X/Z integer literal '" << spelling
                        << "' is not supported";
    return failure();
  }
  if (radix == 10) {
    emitError(location) << "decimal X/Z integer literals are not yet supported";
    return failure();
  }
  unsigned group = radix == 2 ? 1 : radix == 8 ? 3 : 4;
  unsigned bit = 0;
  for (char c : llvm::reverse(digits)) {
    if (bit >= width)
      break;
    if (c == 'x' || c == 'z' || c == '?') {
      for (unsigned i = 0; i < group && bit + i < width; ++i) {
        unknown.setBit(bit + i);
        if (c == 'z' || c == '?')
          value.setBit(bit + i);
      }
    } else {
      unsigned digit = llvm::hexDigitValue(c);
      if (digit == static_cast<unsigned>(-1) || digit >= radix) {
        emitError(location)
            << "invalid digit in integer literal '" << spelling << "'";
        return failure();
      }
      for (unsigned i = 0; i < group && bit + i < width; ++i)
        if (digit & (1u << i))
          value.setBit(bit + i);
    }
    bit += group;
  }
  return ParsedConstant{value, unknown};
}

FailureOr<sim::FrozenConstantAttr> freezeSemanticConstant(Operation *symbol) {
  Location location = getSemanticLocation(symbol);
  FailureOr<Type> normalized = getNormalizedSemanticType(symbol);
  if (failed(normalized))
    return failure();
  auto semanticType = symbol->getAttrOfType<TypeAttr>("semantic_type");
  auto spelling = symbol->getAttrOfType<StringAttr>("constant_value");
  if (!semanticType || !spelling) {
    symbol->emitError(
        "constant symbol requires semantic_type and constant_value");
    return failure();
  }

  Builder builder(symbol->getContext());
  Attribute payload;
  if (isa<sim::StringType>(*normalized)) {
    payload = spelling;
  } else if (isa<FloatType>(*normalized)) {
    double value = 0.0;
    if (spelling.getValue().getAsDouble(value) || !std::isfinite(value)) {
      emitError(location) << "real constant '" << spelling.getValue()
                          << "' is not finite";
      return failure();
    }
    payload = builder.getFloatAttr(*normalized, value);
  } else {
    Type scalar = sim::getPackedScalarType(*normalized);
    if (!scalar) {
      emitError(location)
          << "elaborated constant has unsupported normalized type "
          << *normalized;
      return failure();
    }
    std::optional<unsigned> width = sim::getPackedWidth(scalar);
    if (!width) {
      emitError(location)
          << "elaborated constant has unsupported normalized type "
          << *normalized;
      return failure();
    }
    FailureOr<ParsedConstant> parsed =
        parseSVInteger(spelling.getValue(), *width, location);
    if (failed(parsed))
      return failure();
    auto planeType = builder.getIntegerType(*width);
    payload = builder.getArrayAttr(
        {builder.getIntegerAttr(planeType, parsed->value),
         builder.getIntegerAttr(planeType, parsed->unknown)});
  }
  return sim::FrozenConstantAttr::get(
      symbol->getContext(), *normalized, payload,
      isSignedSemanticType(semanticType.getValue()));
}

Value createDefaultValue(OpBuilder &builder, Location location, Type type) {
  if (isa<sim::ClassHandleType>(type))
    return sim::SimClassNullOp::create(builder, location, type);
  if (isa<sim::CovergroupHandleType>(type))
    return sim::SimCovergroupNullOp::create(builder, location, type);
  if (isa<sim::EventType>(type))
    return sim::SimEventNullOp::create(builder, location, type);
  if (sim::isManagedHandleType(type))
    return sim::SimManagedNullOp::create(builder, location, type);
  if (sim::isAggregateType(type))
    return sim::SimAggregateDefaultOp::create(builder, location, type);
  if (auto logic = dyn_cast<sim::LogicType>(type)) {
    auto planeType = IntegerType::get(type.getContext(), logic.getWidth());
    return sim::SimLogicConstantOp::create(
        builder, location, logic,
        builder.getIntegerAttr(planeType, APInt(logic.getWidth(), 0)),
        builder.getIntegerAttr(planeType, APInt::getAllOnes(logic.getWidth())));
  }
  if (auto integer = dyn_cast<IntegerType>(type))
    return arith::ConstantOp::create(builder, location, integer,
                                     builder.getIntegerAttr(integer, 0));
  if (isa<FloatType>(type))
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getFloatAttr(type, 0.0));
  return {};
}

DictionaryAttr captureMetadata(OpBuilder &builder, sim::CaptureKind kind,
                               std::optional<uint64_t> descriptorId) {
  SmallVector<NamedAttribute> values;
  values.push_back(builder.getNamedAttr(
      captureKindAttrName,
      sim::CaptureKindAttr::get(builder.getContext(), kind)));
  if (descriptorId)
    values.push_back(builder.getNamedAttr(
        descriptorIdAttrName, builder.getI64IntegerAttr(*descriptorId)));
  return builder.getDictionaryAttr(values);
}

bool isSuspensionTerminator(Operation *op) {
  return getFragmentActionKind(op) != sim::ComputeActionKind::Continue &&
         !isa<sim::SimReturnOp>(op);
}

sim::ComputeActionKind getFragmentActionKind(Operation *terminator) {
  return llvm::TypeSwitch<Operation *, sim::ComputeActionKind>(terminator)
      .Case<sim::SimSuspendDelayOp>(
          [](auto) { return sim::ComputeActionKind::SuspendDelay; })
      .Case<sim::SimSuspendChangeOp>(
          [](auto) { return sim::ComputeActionKind::SuspendChange; })
      .Case<sim::SimSuspendEdgeOp>(
          [](auto) { return sim::ComputeActionKind::SuspendEdge; })
      .Case<sim::SimSuspendEdgeIffOp>(
          [](auto) { return sim::ComputeActionKind::SuspendEdge; })
      .Case<sim::SimSuspendLevelOp>(
          [](auto) { return sim::ComputeActionKind::SuspendChange; })
      .Case<sim::SimSuspendAnyOp>(
          [](auto) { return sim::ComputeActionKind::SuspendAny; })
      .Case<sim::SimSuspendEventOp>(
          [](auto) { return sim::ComputeActionKind::SuspendEvent; })
      .Case<sim::SimSuspendForeverOp>(
          [](auto) { return sim::ComputeActionKind::SuspendAny; })
      .Case<sim::SimSuspendAwaitOp>(
          [](auto) { return sim::ComputeActionKind::SuspendAwait; })
      .Case<sim::SimSuspendJoinOp>(
          [](auto) { return sim::ComputeActionKind::SuspendJoin; })
      .Case<sim::SimSuspendChildrenOp>(
          [](auto) { return sim::ComputeActionKind::SuspendChildren; })
      .Case<sim::SimSuspendObserveOp>(
          [](auto) { return sim::ComputeActionKind::SuspendObserve; })
      .Case<sim::SimTaskCallOp, sim::SimClassVirtualTaskCallOp>(
          [](auto) { return sim::ComputeActionKind::TaskCall; })
      .Case<sim::SimProcessControlOp>(
          [](auto) { return sim::ComputeActionKind::ProcessControl; })
      .Case<sim::SimReturnOp>(
          [](auto) { return sim::ComputeActionKind::Terminate; })
      .Default([](Operation *) { return sim::ComputeActionKind::Continue; });
}

sim::ContinuationSiteAttr getContinuationSite(Operation *operation) {
  sim::ContinuationSiteAttr site;
  llvm::TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
            sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
            sim::SimSuspendLevelOp, sim::SimSuspendAnyOp,
            sim::SimSuspendEventOp, sim::SimSuspendObserveOp,
            sim::SimSuspendForeverOp, sim::SimSuspendAwaitOp,
            sim::SimSuspendJoinOp, sim::SimSuspendChildrenOp,
            sim::SimTaskCallOp, sim::SimClassVirtualTaskCallOp,
            sim::SimProcessControlOp>(
          [&](auto op) { site = op.getSiteAttr(); });
  return site;
}

void setContinuationSite(Operation *operation, sim::ContinuationSiteAttr site) {
  llvm::TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
            sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
            sim::SimSuspendLevelOp, sim::SimSuspendAnyOp,
            sim::SimSuspendEventOp, sim::SimSuspendObserveOp,
            sim::SimSuspendForeverOp, sim::SimSuspendAwaitOp,
            sim::SimSuspendJoinOp, sim::SimSuspendChildrenOp,
            sim::SimTaskCallOp, sim::SimClassVirtualTaskCallOp,
            sim::SimProcessControlOp>(
          [&](auto op) { op.setSiteAttr(site); });
}

ReexecutingBlockSet getReexecutingBlocks(sim::SimFuncOp function) {
  SmallVector<Block *> blocks;
  DenseMap<Block *, SmallVector<Block *>> successors;
  for (Block &block : function.getBody()) {
    blocks.push_back(&block);
    successors.try_emplace(&block, block.getTerminator()->getSuccessors());
  }

  ReexecutingBlockSet reexecuting;
  for (ArrayRef<Block *> component :
       computeStronglyConnectedComponents<Block *>(blocks, successors)) {
    // A single-block component only re-executes when it branches to itself.
    bool cyclic = component.size() > 1 ||
                  llvm::is_contained(successors.lookup(component.front()),
                                     component.front());
    if (cyclic)
      reexecuting.insert(component.begin(), component.end());
  }
  return reexecuting;
}

bool isConstantTimeValue(Value value) {
  // A value is a compiled-calendar delay when every definition reaching it is
  // the same constant. Carrying an argument around a loop preserves, rather
  // than creates, that proof, so a self-reference contributes no definition.
  // Whether the proof holds must depend only on the definitions reached, never
  // on the order the worklist happens to visit them.
  SmallVector<Value> worklist{value};
  DenseSet<Value> visited;
  std::optional<APInt> constantValue;
  while (!worklist.empty()) {
    Value current = worklist.pop_back_val();
    if (!visited.insert(current).second)
      continue;
    if (auto constant = current.getDefiningOp<sim::SimTimeConstantOp>()) {
      APInt value = constant.getValueAttr().getValue();
      if (constantValue && *constantValue != value)
        return false;
      constantValue = value;
      continue;
    }
    auto argument = dyn_cast<BlockArgument>(current);
    if (!argument)
      return false;
    Block *block = argument.getOwner();
    if (block->isEntryBlock() || block->hasNoPredecessors())
      return false;
    for (Block *predecessor : block->getPredecessors()) {
      auto branch = dyn_cast<BranchOpInterface>(predecessor->getTerminator());
      if (!branch)
        return false;
      for (unsigned successor = 0;
           successor != predecessor->getTerminator()->getNumSuccessors();
           ++successor) {
        if (predecessor->getTerminator()->getSuccessor(successor) != block)
          continue;
        auto forwarded =
            branch.getSuccessorOperands(successor).getForwardedOperands();
        if (argument.getArgNumber() >= forwarded.size())
          return false;
        Value incoming = forwarded[argument.getArgNumber()];
        if (incoming != current)
          worklist.push_back(incoming);
      }
    }
  }
  // An argument defined only by itself reaches no constant at all.
  return constantValue.has_value();
}

} // namespace obelisk::simlowering
