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
    if (auto body = dyn_cast<semantic::SVInstanceBodySymbolOp>(cursor))
      if (auto marker = body->getAttrOfType<BoolAttr>(
              "is_virtual_interface_type_instance");
          marker && marker.getValue())
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

SmallVector<Operation *> getNetInitializerExpressions(Operation *op) {
  SmallVector<Operation *> expressions;
  for (Operation *child : getChildren(op))
    if (!isa<semantic::SVDelayControlOp, semantic::SVDelay3ControlOp>(child))
      expressions.push_back(child);
  return expressions;
}

bool storageDecidesTruth(Operation *expression) {
  // IEEE 1800-2017 12.4 reads a condition as a comparison against zero, and
  // only a packed integral value answers that from its stored bits. A handle
  // compares against null, whose representation need not be all-zero, and a
  // real compares as a float, which -0.0 and NaN both get wrong when read as
  // bits.
  FailureOr<Type> type = getNormalizedSemanticType(expression);
  return succeeded(type) && bool(sim::getPackedScalarType(*type));
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
  // Elaboration folded this expression to a constant. A bound or a count
  // written as parameter arithmetic is as constant as a literal is. This comes
  // last so that a spelling any earlier pass established still wins.
  if (auto folded =
          operation->getAttrOfType<StringAttr>(foldedConstantAttrName))
    return folded.getValue();
  return std::nullopt;
}

std::optional<int64_t> getTypeReferenceIdentity(Operation *operation) {
  if (!isa<semantic::SVTypeReferenceExpressionOp>(operation))
    return std::nullopt;
  auto identity =
      operation->getAttrOfType<IntegerAttr>(typeReferenceIdentityAttrName);
  if (!identity)
    return std::nullopt;
  return identity.getInt();
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
  if (operation->hasAttr(clockingBlockEventAttrName))
    return operation->hasAttr(clockingEventPathAttrName);
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

  if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type)) {
    // Packed aggregates are necessarily fixed-size, and the elaborator's
    // width includes tagged-union discriminants and padding.  For unpacked
    // aggregates, however, Slang reports zero for a dynamically sized field.
    // Recurse through the source inventory so callers can distinguish an
    // actual zero-width type from a value-dependent bitstream.
    if (aggregate.getIsPacked())
      return aggregate.getBitstreamWidth();
    for (Attribute fieldAttr : aggregate.getFields()) {
      auto field = dyn_cast<DictionaryAttr>(fieldAttr);
      auto fieldType = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
      if (!fieldType)
        return std::nullopt;
      if (isa<semantic::VoidType>(fieldType.getValue()))
        continue;
      if (!getSemanticBitstreamWidth(fieldType.getValue()))
        return std::nullopt;
    }
    // Preserve the elaborator's exact fixed layout, including tags and
    // padding, once recursion has established that every field is fixed.
    return aggregate.getBitstreamWidth();
  }

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

std::optional<uint64_t> getSemanticPackedWidth(Type type) {
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

bool isFourStateSemanticType(Type type) {
  if (auto integral = dyn_cast<semantic::IntegralType>(type))
    return integral.getIsFourState();
  if (isa<semantic::LogicType, semantic::TimeType>(type))
    return true;
  if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type))
    return isFourStateSemanticType(array.getElementType());
  if (auto array = dyn_cast<semantic::PackedArrayType>(type))
    return isFourStateSemanticType(array.getElementType());
  if (auto enumeration = dyn_cast<semantic::EnumType>(type))
    return isFourStateSemanticType(enumeration.getBaseType());
  if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type))
    return aggregate.getIsPacked() && aggregate.getIsFourState();
  auto dictionaryIsFourState = [&](DictionaryAttr fields) {
    return llvm::any_of(fields, [&](NamedAttribute field) {
      auto fieldType = dyn_cast<TypeAttr>(field.getValue());
      return fieldType && isFourStateSemanticType(fieldType.getValue());
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
  if (auto classHandle = dyn_cast<semantic::ClassHandleType>(type)) {
    SymbolRefAttr className = classHandle.getClassName();
    auto sourceName = [](StringRef symbol) {
      size_t separator = symbol.find('.');
      return separator == StringRef::npos ? symbol
                                          : symbol.drop_front(separator + 1);
    };
    if (className.getNestedReferences().size() == 1 &&
        sourceName(className.getRootReference().getValue()) == "std" &&
        sourceName(className.getLeafReference().getValue()) == "process")
      return sim::ProcessType::get(context);
    if (className.getNestedReferences().size() == 1 &&
        sourceName(className.getRootReference().getValue()) == "std" &&
        sourceName(className.getLeafReference().getValue()) == "semaphore")
      return sim::SemaphoreType::get(context);
    return sim::ClassHandleType::get(
        context, FlatSymbolRefAttr::get(
                     getSimulationClassSymbol(classHandle.getClassName())));
  }
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
  if (auto array = dyn_cast<semantic::OpenArrayType>(type)) {
    FailureOr<Type> element = normalizeType(array.getElementType(), location,
                                            /*allowRealScalar=*/true);
    if (failed(element))
      return failure();
    return sim::DPIOpenArrayType::get(context, *element, array.getIsPacked());
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
    // An integral key indexes on its value, not on its declared packed shape:
    // `bit [63:0]` and `logic [63:0]` denote the same lookup domain as their
    // 64-bit scalar. Packed aggregates therefore collapse to that scalar so a
    // vector-typed key stays a normalized integral key. Non-packed keys
    // (string, class handle, process) have no scalar and pass through.
    Type keyType = *key;
    if (Type scalar = sim::getPackedScalarType(keyType))
      keyType = scalar;
    return sim::AssocArrayType::get(context, keyType, *element,
                                    isSignedSemanticType(array.getKeyType()),
                                    array.getWildcardIndex());
  }
  if (auto enumeration = dyn_cast<semantic::EnumType>(type);
      enumeration && isa<semantic::TimeType>(enumeration.getBaseType()))
    return normalizeType(enumeration.getBaseType(), location, allowRealScalar);
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
    if (isFourStateSemanticType(type))
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
  if (isa<semantic::ProcessType>(type))
    return sim::ProcessType::get(context);
  if (isa<semantic::ChandleType>(type))
    return sim::ChandleType::get(context);
  if (auto covergroup = dyn_cast<semantic::CovergroupHandleType>(type))
    return sim::CovergroupHandleType::get(
        context, SymbolRefAttr::get(context, getSimulationCovergroupSymbol(
                                                 covergroup.getCovergroupName())
                                                 .getValue()));
  if (auto interface = dyn_cast<semantic::VirtualInterfaceType>(type)) {
    std::string identity;
    llvm::raw_string_ostream stream(identity);
    stream << interface.getInterfaceName();
    return sim::VirtualInterfaceType::get(
        context, StringAttr::get(context, identity), interface.getModport());
  }
  if (isa<semantic::StringType>(type))
    return sim::StringType::get(context);
  if (isa<semantic::UntypedType>(type))
    return sim::BoxType::get(context);
  if (type.isF64() || type.isF32())
    return type;
  if (isa<sim::LogicType, sim::TimeType, sim::ContextType, sim::RefType,
          sim::NetType, sim::DriverType, sim::EventType, sim::ProcessType,
          sim::ClassHandleType, sim::VirtualInterfaceType, sim::ChandleType,
          sim::StringType, sim::DynamicArrayType, sim::QueueType,
          sim::MailboxType, sim::BoxType, sim::SemaphoreType,
          sim::AssocArrayType, sim::ManagedRefType>(type) ||
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
  Type semanticType = typeAttr.getValue();
  if (auto handle = dyn_cast<semantic::ClassHandleType>(semanticType)) {
    SymbolRefAttr name = handle.getClassName();
    auto sourceName = [](StringRef symbol) {
      size_t separator = symbol.find('.');
      return separator == StringRef::npos ? symbol
                                          : symbol.drop_front(separator + 1);
    };
    bool mailbox = name.getNestedReferences().size() == 1 &&
                   sourceName(name.getRootReference().getValue()) == "std" &&
                   sourceName(name.getLeafReference().getValue()) == "mailbox";
    if (mailbox) {
      semantic::SVClassTypeOp specialization;
      Operation *root = op;
      while (root->getParentOp())
        root = root->getParentOp();
      root->walk([&](semantic::SVClassTypeOp candidate) {
        if (candidate.getSemanticType() == semanticType) {
          specialization = candidate;
          return WalkResult::interrupt();
        }
        return WalkResult::advance();
      });
      if (!specialization) {
        emitError(getSemanticLocation(op))
            << "mailbox specialization has no semantic class definition";
        return failure();
      }
      Type elementType;
      for (Operation *child : getChildren(specialization)) {
        auto parameter = dyn_cast<semantic::SVTypeParameterSymbolOp>(child);
        if (!parameter)
          continue;
        if (elementType) {
          emitError(getSemanticLocation(op))
              << "mailbox specialization has multiple type parameters";
          return failure();
        }
        elementType = parameter.getSemanticType().value_or(Type{});
      }
      if (!elementType) {
        emitError(getSemanticLocation(op))
            << "mailbox specialization has no resolved element type";
        return failure();
      }
      FailureOr<Type> normalized =
          normalizeType(elementType, getSemanticLocation(op));
      return failed(normalized) ? FailureOr<Type>(failure())
                                : FailureOr<Type>(sim::MailboxType::get(
                                      semanticType.getContext(), *normalized));
    }
  }
  return normalizeType(semanticType, getSemanticLocation(op));
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
  // A formal is an open array when any packed or unpacked dimension is
  // unsized. The open dimension can be nested below sized dimensions, but the
  // whole formal is still passed as one svOpenArrayHandle.
  std::function<Type(Type)> findOpenElement = [&](Type current) -> Type {
    if (auto array = dyn_cast<semantic::OpenArrayType>(current)) {
      current = array.getElementType();
      while (true) {
        Type next = llvm::TypeSwitch<Type, Type>(current)
                        .Case<semantic::RangedPackedArrayType,
                              semantic::RangedUnpackedArrayType,
                              semantic::PackedArrayType,
                              semantic::UnpackedArrayType,
                              semantic::OpenArrayType>(
                            [](auto nested) {
                              return nested.getElementType();
                            })
                        .Default([](Type) { return Type{}; });
        if (!next)
          return current;
        current = next;
      }
    }
    if (auto array = dyn_cast<semantic::RangedPackedArrayType>(current))
      return findOpenElement(array.getElementType());
    if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(current))
      return findOpenElement(array.getElementType());
    if (auto array = dyn_cast<semantic::PackedArrayType>(current))
      return findOpenElement(array.getElementType());
    if (auto array = dyn_cast<semantic::UnpackedArrayType>(current))
      return findOpenElement(array.getElementType());
    if (auto array = dyn_cast<sim::DPIOpenArrayType>(current))
      return array.getElementType();
    if (auto array = dyn_cast<sim::PackedArrayType>(current))
      return findOpenElement(array.getElementType());
    if (auto array = dyn_cast<sim::UnpackedArrayType>(current))
      return findOpenElement(array.getElementType());
    return {};
  };
  if (Type element = findOpenElement(type)) {
    FailureOr<DPIABIType> classified =
        classifyDPIABIType(element, location);
    if (failed(classified))
      return failure();
    return DPIABIType{DPIABIKind::OpenArray, classified->width,
                      classified->fourState, false};
  }
  if (isa<semantic::DynArrayType, semantic::QueueType, semantic::AssocArrayType,
          sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(type)) {
    emitError(location)
        << "DPI-C dynamic-array, queue, and associative-array marshalling is "
           "unsupported";
    return failure();
  }
  if (auto enumeration = dyn_cast<semantic::EnumType>(type))
    return classifyDPIABIType(enumeration.getBaseType(), location);
  if (isa<semantic::StringType, sim::StringType>(type))
    return DPIABIType{DPIABIKind::String, 64, false, false};
  if (isa<semantic::ChandleType, sim::ChandleType>(type))
    return DPIABIType{DPIABIKind::Chandle, 64, false, false};
  if (isa<semantic::ShortRealType>(type) || type.isF32())
    return DPIABIType{DPIABIKind::ShortReal, 32, false, false};
  if (isa<semantic::RealType, semantic::RealtimeType>(type) || type.isF64())
    return DPIABIType{DPIABIKind::Real, 64, false, false};
  if (isa<semantic::TimeType>(type))
    return DPIABIType{DPIABIKind::LogicVector, 64, true, false};
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
      kind = DPIABIKind::LogicVector;
      break;
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

  bool unpackedArray =
      isa<semantic::RangedUnpackedArrayType, semantic::UnpackedArrayType,
          sim::UnpackedArrayType>(type);
  bool unpackedStruct =
      isa<semantic::UnpackedStructType, sim::UnpackedStructType>(type);
  bool unpackedUnion =
      isa<semantic::UnpackedUnionType, sim::UnpackedUnionType>(type);
  if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type)) {
    unpackedStruct = !aggregate.getIsPacked() && !aggregate.getIsUnion();
    unpackedUnion = !aggregate.getIsPacked() && aggregate.getIsUnion();
  }
  if (unpackedUnion) {
    emitError(location)
        << "DPI-C permits unions in formal aggregate types only when packed";
    return failure();
  }
  if (unpackedArray || unpackedStruct) {
    std::optional<uint64_t> width;
    bool normalizedAggregate =
        isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type);
    if (normalizedAggregate)
      width = sim::getProvenanceSpan(type);
    bool aggregateFourState = false;
    uint64_t semanticWidth = 0;
    auto validateChild = [&](Type child, uint64_t count = 1) -> LogicalResult {
      FailureOr<DPIABIType> childABI = classifyDPIABIType(child, location);
      if (failed(childABI))
        return failure();
      aggregateFourState |= childABI->fourState;
      if (!normalizedAggregate) {
        if (count != 0 && childABI->width > UINT64_MAX / count)
          return failure();
        uint64_t contribution = uint64_t{childABI->width} * count;
        if (semanticWidth > UINT64_MAX - contribution)
          return failure();
        semanticWidth += contribution;
      }
      return success();
    };
    if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(type)) {
      std::optional<uint64_t> count =
          getRangeExtent(array.getLeft(), array.getRight());
      if (!count || failed(validateChild(array.getElementType(), *count)))
        return failure();
    } else if (auto array = dyn_cast<semantic::UnpackedArrayType>(type)) {
      if (failed(validateChild(array.getElementType(), array.getSize())))
        return failure();
    } else if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
      if (failed(validateChild(array.getElementType())))
        return failure();
    } else if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type)) {
      for (Attribute fieldAttr : aggregate.getFields()) {
        auto field = dyn_cast<DictionaryAttr>(fieldAttr);
        auto fieldType = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
        if (!fieldType || failed(validateChild(fieldType.getValue())))
          return failure();
      }
    } else if (auto structure = dyn_cast<semantic::UnpackedStructType>(type)) {
      for (NamedAttribute field : structure.getFields()) {
        auto fieldType = dyn_cast<TypeAttr>(field.getValue());
        if (!fieldType || failed(validateChild(fieldType.getValue())))
          return failure();
      }
    } else {
      for (Attribute fieldAttr :
           cast<sim::UnpackedStructType>(type).getFields()) {
        auto field = dyn_cast<sim::FieldAttr>(fieldAttr);
        if (!field || failed(validateChild(field.getType())))
          return failure();
      }
    }
    if (!normalizedAggregate)
      width = semanticWidth;
    if (!width || *width == 0 ||
        *width > std::numeric_limits<uint32_t>::max()) {
      emitError(location)
          << "DPI sized aggregate has no bounded transport representation";
      return failure();
    }
    return DPIABIType{DPIABIKind::UnpackedAggregate,
                      static_cast<uint32_t>(*width), aggregateFourState, false};
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
        << "DPI-C supports only string, chandle, scalar predefined "
           "integers, scalar bit/logic, enums, and fixed packed integral "
           "values";
    return failure();
  }
  bool fourState = isFourStateSemanticType(type);
  return DPIABIType{fourState ? DPIABIKind::LogicVector : DPIABIKind::BitVector,
                    static_cast<uint32_t>(*width), fourState,
                    simlowering::isSignedSemanticType(type)};
}

FailureOr<sim::DPIAggregateABIAttr>
simlowering::makeDPIAggregateABI(Type semanticType, Type normalizedType,
                                 Location location, Builder &builder,
                                 bool compactTransport) {
  namespace semantic = ::obelisk::ir;
  struct Node {
    bool array = false;
    bool descending = false;
    uint64_t count = 0;
    Type element;
    SmallVector<Type> fields;
  };
  struct Shape {
    uint64_t size = 0;
    uint64_t alignment = 1;
  };
  auto alignTo = [](uint64_t value,
                    uint64_t alignment) -> std::optional<uint64_t> {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        value > UINT64_MAX - alignment + 1)
      return std::nullopt;
    return (value + alignment - 1) & ~(alignment - 1);
  };
  auto node = [](Type type) -> FailureOr<Node> {
    Node result;
    if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(type)) {
      int64_t left = array.getLeft(), right = array.getRight();
      uint64_t distance =
          left >= right
              ? static_cast<uint64_t>(left) - static_cast<uint64_t>(right)
              : static_cast<uint64_t>(right) - static_cast<uint64_t>(left);
      if (distance == UINT64_MAX)
        return failure();
      result.array = true;
      result.descending = left > right;
      result.count = distance + 1;
      result.element = array.getElementType();
    } else if (auto array = dyn_cast<semantic::UnpackedArrayType>(type)) {
      if (array.getSize() == 0)
        return failure();
      result.array = true;
      result.count = array.getSize();
      result.element = array.getElementType();
    } else if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type)) {
      if (aggregate.getIsPacked())
        return result;
      if (aggregate.getIsUnion())
        return failure();
      for (Attribute attribute : aggregate.getFields()) {
        auto field = dyn_cast<DictionaryAttr>(attribute);
        auto typeAttr = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
        if (!typeAttr)
          return failure();
        result.fields.push_back(typeAttr.getValue());
      }
    } else if (auto structure = dyn_cast<semantic::UnpackedStructType>(type)) {
      for (NamedAttribute field : structure.getFields()) {
        auto typeAttr = dyn_cast<TypeAttr>(field.getValue());
        if (!typeAttr)
          return failure();
        result.fields.push_back(typeAttr.getValue());
      }
    }
    return result;
  };
  auto leafShape = [&](Type type) -> FailureOr<Shape> {
    FailureOr<DPIABIType> abi = classifyDPIABIType(type, location);
    if (failed(abi) || abi->kind == DPIABIKind::OpenArray ||
        abi->kind == DPIABIKind::UnpackedAggregate)
      return failure();
    switch (abi->kind) {
    case DPIABIKind::Bit:
    case DPIABIKind::Logic:
    case DPIABIKind::Byte:
      return Shape{1, 1};
    case DPIABIKind::ShortInt:
      return Shape{2, 2};
    case DPIABIKind::Int:
    case DPIABIKind::ShortReal:
      return Shape{4, 4};
    case DPIABIKind::LongInt:
    case DPIABIKind::Real:
    case DPIABIKind::String:
    case DPIABIKind::Chandle:
      return Shape{8, 8};
    case DPIABIKind::BitVector:
      return Shape{((uint64_t{abi->width} + 31) / 32) * 4, 4};
    case DPIABIKind::LogicVector:
      return Shape{((uint64_t{abi->width} + 31) / 32) * 8, 4};
    case DPIABIKind::OpenArray:
    case DPIABIKind::UnpackedAggregate:
      return failure();
    }
    return failure();
  };
  DenseMap<Type, std::optional<Shape>> shapeCache;
  std::function<FailureOr<Shape>(Type)> shape =
      [&](Type type) -> FailureOr<Shape> {
    auto [cached, inserted] = shapeCache.try_emplace(type, std::nullopt);
    if (!inserted) {
      if (cached->second)
        return *cached->second;
      return failure();
    }
    auto finish = [&](Shape result) -> FailureOr<Shape> {
      cached->second = result;
      return result;
    };
    FailureOr<Node> current = node(type);
    if (failed(current))
      return failure();
    if (current->array) {
      FailureOr<Shape> element = shape(current->element);
      if (failed(element) || element->size > UINT64_MAX / current->count)
        return failure();
      return finish(
          Shape{element->size * current->count, element->alignment});
    }
    if (current->fields.empty()) {
      FailureOr<Shape> leaf = leafShape(type);
      if (failed(leaf))
        return failure();
      return finish(*leaf);
    }
    Shape result;
    for (Type fieldType : current->fields) {
      FailureOr<Shape> field = shape(fieldType);
      if (failed(field))
        return failure();
      std::optional<uint64_t> offset = alignTo(result.size, field->alignment);
      if (!offset || field->size > UINT64_MAX - *offset)
        return failure();
      result.size = *offset + field->size;
      result.alignment = std::max(result.alignment, field->alignment);
    }
    std::optional<uint64_t> final = alignTo(result.size, result.alignment);
    if (!final)
      return failure();
    result.size = *final;
    return finish(result);
  };
  DenseMap<Type, std::optional<uint64_t>> compactWidthCache;
  std::function<std::optional<uint64_t>(Type)> compactWidth =
      [&](Type type) -> std::optional<uint64_t> {
    auto [cached, inserted] =
        compactWidthCache.try_emplace(type, std::nullopt);
    if (!inserted)
      return cached->second;
    auto finish = [&](uint64_t result) -> std::optional<uint64_t> {
      cached->second = result;
      return result;
    };
    FailureOr<Node> current = node(type);
    if (failed(current))
      return std::nullopt;
    if (current->array) {
      std::optional<uint64_t> element = compactWidth(current->element);
      if (!element || *element > UINT64_MAX / current->count)
        return std::nullopt;
      return finish(*element * current->count);
    }
    if (!current->fields.empty()) {
      uint64_t total = 0;
      for (Type field : current->fields) {
        std::optional<uint64_t> width = compactWidth(field);
        if (!width || *width > UINT64_MAX - total)
          return std::nullopt;
        total += *width;
      }
      return finish(total);
    }
    FailureOr<DPIABIType> abi = classifyDPIABIType(type, location);
    return succeeded(abi) ? finish(abi->width) : std::nullopt;
  };
  FailureOr<Shape> cShape = shape(semanticType);
  if (failed(cShape) || cShape->size == 0 || cShape->size > INT64_MAX ||
      cShape->alignment > UINT32_MAX)
    return emitError(location)
               << "DPI aggregate C layout is not representable for "
               << semanticType,
           failure();
  SmallVector<int64_t> records;
  uint64_t stringCount = 0;
  std::function<LogicalResult(Type, Type, uint64_t, uint64_t)> emit =
      [&](Type source, Type normalized, uint64_t bitBase,
          uint64_t byteBase) -> LogicalResult {
    FailureOr<Node> current = node(source);
    if (failed(current))
      return failure();
    if (current->array || !current->fields.empty()) {
      uint64_t count = current->array ? current->count : current->fields.size();
      if (!isa<sim::UnpackedArrayType, sim::UnpackedStructType>(normalized) ||
          sim::getAggregateNumElements(normalized) != count)
        return failure();
      if (current->array) {
        FailureOr<Shape> elementShape = shape(current->element);
        std::optional<uint64_t> compactElement =
            compactTransport ? compactWidth(current->element) : std::nullopt;
        uint64_t firstOrdinal = current->descending ? count - 1 : 0;
        auto locationOf = [&](uint64_t ordinal)
            -> std::optional<std::pair<uint64_t, uint64_t>> {
          if (!compactTransport)
            return sim::getAggregateProvenanceSubelement(
                normalized, static_cast<unsigned>(ordinal));
          if (!compactElement || ordinal >= count ||
              count - 1 - ordinal > UINT64_MAX / *compactElement)
            return std::nullopt;
          return std::pair<uint64_t, uint64_t>{
              (count - 1 - ordinal) * *compactElement, *compactElement};
        };
        auto first = locationOf(firstOrdinal);
        auto second =
            count > 1 ? locationOf(current->descending ? firstOrdinal - 1 : 1)
                      : std::optional<std::pair<uint64_t, uint64_t>>{};
        if (failed(elementShape) || !first)
          return failure();
        int64_t stride = second ? static_cast<int64_t>(second->first) -
                                      static_cast<int64_t>(first->first)
                                : static_cast<int64_t>(first->second);
        size_t repeat = records.size();
        records.append({1, static_cast<int64_t>(bitBase + first->first),
                        static_cast<int64_t>(byteBase),
                        static_cast<int64_t>(count), stride,
                        static_cast<int64_t>(elementShape->size), 0, 0});
        size_t bodyStart = records.size();
        uint64_t stringsBefore = stringCount;
        if (failed(emit(current->element,
                        sim::getAggregateElementType(
                            normalized, static_cast<unsigned>(firstOrdinal)),
                        0, 0)))
          return failure();
        uint64_t bodyRecords = (records.size() - bodyStart) / 8;
        uint64_t bodyStrings = stringCount - stringsBefore;
        if (bodyRecords == 0 || bodyRecords > INT64_MAX ||
            (bodyStrings && count > (UINT64_MAX - stringsBefore) / bodyStrings))
          return failure();
        stringCount = stringsBefore + bodyStrings * count;
        records[repeat + 7] = static_cast<int64_t>(bodyRecords);
        return success();
      }
      uint64_t cursor = 0;
      std::optional<uint64_t> compactCursor =
          compactTransport ? compactWidth(source) : std::nullopt;
      if (compactTransport && !compactCursor)
        return failure();
      for (auto [ordinal, field] : llvm::enumerate(current->fields)) {
        FailureOr<Shape> fieldShape = shape(field);
        auto provenance = sim::getAggregateProvenanceSubelement(
            normalized, static_cast<unsigned>(ordinal));
        std::optional<uint64_t> fieldBits =
            compactTransport ? compactWidth(field) : std::nullopt;
        if (failed(fieldShape) || !provenance ||
            (compactTransport && (!fieldBits || *fieldBits > *compactCursor)))
          return failure();
        if (compactTransport)
          *compactCursor -= *fieldBits;
        std::optional<uint64_t> offset = alignTo(cursor, fieldShape->alignment);
        if (!offset ||
            failed(emit(field,
                        sim::getAggregateElementType(normalized, ordinal),
                        bitBase + (compactTransport ? *compactCursor
                                                    : provenance->first),
                        byteBase + *offset)))
          return failure();
        cursor = *offset + fieldShape->size;
      }
      return success();
    }
    FailureOr<DPIABIType> abi = classifyDPIABIType(source, location);
    if (failed(abi) || abi->kind == DPIABIKind::OpenArray ||
        abi->kind == DPIABIKind::UnpackedAggregate || bitBase > INT64_MAX ||
        byteBase > INT64_MAX)
      return failure();
    if (abi->kind == DPIABIKind::String) {
      if (stringCount == UINT64_MAX)
        return failure();
      ++stringCount;
    }
    records.append(
        {0, static_cast<int64_t>(bitBase), static_cast<int64_t>(byteBase),
         static_cast<int64_t>(abi->kind), static_cast<int64_t>(abi->width),
         abi->fourState ? 1 : 0, abi->isSigned ? 1 : 0, 0});
    return success();
  };
  if (failed(emit(semanticType, normalizedType, 0, 0)))
    return emitError(location) << "cannot construct DPI aggregate leaf layout",
           failure();
  return sim::DPIAggregateABIAttr::get(builder.getContext(), cShape->size,
                                       static_cast<uint32_t>(cShape->alignment),
                                       stringCount,
                                       builder.getDenseI64ArrayAttr(records));
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
  case DPIABIKind::String:
    return "const char *";
  case DPIABIKind::Chandle:
    return "void *";
  case DPIABIKind::ShortReal:
    return "float";
  case DPIABIKind::Real:
    return "double";
  case DPIABIKind::OpenArray:
    return "svOpenArrayHandle";
  case DPIABIKind::UnpackedAggregate:
    llvm_unreachable("unpacked aggregate spelling requires a generated type");
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
  unsigned literalWidth = width;
  bool explicitlySized = false;
  bool basedSigned = false;
  StringRef digits = text;
  if (quote != StringRef::npos) {
    StringRef size = text.take_front(quote);
    if (!size.empty()) {
      uint64_t parsedSize = 0;
      if (size.getAsInteger(10, parsedSize) || parsedSize == 0 ||
          parsedSize > std::numeric_limits<unsigned>::max()) {
        emitError(location) << "invalid size in SystemVerilog integer literal '"
                            << spelling << "'";
        return failure();
      }
      literalWidth = static_cast<unsigned>(parsedSize);
      explicitlySized = true;
    }
    StringRef suffix = text.drop_front(quote + 1);
    basedSigned = suffix.consume_front("s");
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

  bool signedLiteral = quote == StringRef::npos || basedSigned;
  bool hasUnknown =
      digits.contains('x') || digits.contains('z') || digits.contains('?');

  // Validate the digit string before using it to derive an unsized literal's
  // width. IEEE 1800-2017 5.7.1 permits exactly one x, z, or ? digit in a
  // decimal based literal; nondecimal based literals admit those digits in
  // any position.
  if (radix == 10 && hasUnknown) {
    if (quote == StringRef::npos || digits.size() != 1 ||
        (digits.front() != 'x' && digits.front() != 'z' &&
         digits.front() != '?')) {
      emitError(location) << "invalid decimal X/Z integer literal '" << spelling
                          << "'";
      return failure();
    }
  } else {
    for (char c : digits) {
      if (c == 'x' || c == 'z' || c == '?')
        continue;
      unsigned digit = llvm::hexDigitValue(c);
      if (digit == static_cast<unsigned>(-1) || digit >= radix) {
        emitError(location)
            << "invalid digit in integer literal '" << spelling << "'";
        return failure();
      }
    }
  }

  // A based number without a size is self-determined and at least 32 bits
  // wide. For binary/octal/hexadecimal spellings, every nonleading digit
  // contributes a complete digit group while the leading known digit
  // contributes only its significant bits. A leading X/Z contributes no
  // significant bits but retains every following group. This is the rule
  // used by the source frontend and follows 5.7.1's minimum-width and
  // leading-padding requirements.
  bool unsizedBased = quote == 0;
  if (unsizedBased) {
    uint64_t requiredWidth = 0;
    if (radix == 10) {
      if (!hasUnknown) {
        requiredWidth = APInt::getSufficientBitsNeeded(digits, radix);
        // An unsized signed decimal based literal needs a separate sign bit;
        // unlike a nondecimal digit string, its token denotes a magnitude.
        if (basedSigned)
          ++requiredWidth;
        requiredWidth = std::min<uint64_t>(
            requiredWidth, std::numeric_limits<unsigned>::max());
      }
    } else {
      unsigned group = radix == 2 ? 1 : radix == 8 ? 3 : 4;
      SmallVector<char> normalizedDigits;
      normalizedDigits.reserve(digits.size());
      for (char c : digits) {
        bool unknown = c == 'x' || c == 'z' || c == '?';
        unsigned digit = unknown ? 0 : llvm::hexDigitValue(c);
        if (!unknown && digit == 0 && normalizedDigits.size() == 1 &&
            normalizedDigits.front() == '0')
          continue;
        if (!unknown && digit != 0 && normalizedDigits.size() == 1 &&
            normalizedDigits.front() == '0')
          normalizedDigits.clear();
        normalizedDigits.push_back(c);
      }
      if (normalizedDigits.empty())
        normalizedDigits.push_back('0');

      uint64_t trailingDigits = normalizedDigits.size() - 1;
      uint64_t maximum = std::numeric_limits<unsigned>::max();
      requiredWidth =
          trailingDigits > maximum / group ? maximum : trailingDigits * group;
      char leading = normalizedDigits.front();
      if (leading != 'x' && leading != 'z' && leading != '?') {
        unsigned digit = llvm::hexDigitValue(leading);
        unsigned leadingWidth = APInt(4, digit).getActiveBits();
        requiredWidth =
            std::min<uint64_t>(maximum, requiredWidth + leadingWidth);
      }
    }
    literalWidth = static_cast<unsigned>(std::max<uint64_t>(32, requiredWidth));
  }

  // Unary minus is applied to the declared or self-determined literal before
  // its result is resized for the caller. Therefore an X/Z bit that survives
  // literal-width truncation poisons the entire arithmetic result even when
  // that bit would sit above the requested result width. Determine this from
  // digit positions rather than allocating a potentially enormous declared
  // width. Conversely, an unknown group wholly above the literal width is
  // discarded before the operator and cannot affect the result.
  if (negative && hasUnknown) {
    bool operandHasUnknown = radix == 10;
    if (radix != 10) {
      unsigned group = radix == 2 ? 1 : radix == 8 ? 3 : 4;
      uint64_t bit = 0;
      for (char c : llvm::reverse(digits)) {
        if (bit >= literalWidth)
          break;
        if (c == 'x' || c == 'z' || c == '?') {
          operandHasUnknown = true;
          break;
        }
        bit += group;
      }
    }
    if (operandHasUnknown)
      return ParsedConstant{APInt(width, 0), APInt::getAllOnes(width)};
  }

  auto resize = [&](APInt value, APInt unknown) -> ParsedConstant {
    // 5.7.1 gives an unsized unsigned literal whose high bit is X/Z the same
    // X/Z fill through its wider expression context. Extending both planes
    // from that bit preserves X versus Z.
    bool fillUnsizedUnknown =
        unsizedBased && unknown.isSignBitSet() && width > unknown.getBitWidth();
    if (signedLiteral || fillUnsizedUnknown)
      return {value.sextOrTrunc(width), unknown.sextOrTrunc(width)};
    return {value.zextOrTrunc(width), unknown.zextOrTrunc(width)};
  };

  // If a declared size is wider than the requested result, only its low
  // `width` bits can survive. Materializing no more than those bits avoids an
  // allocation proportional to an otherwise valid but enormous size token.
  unsigned materializedWidth = std::min(literalWidth, width);
  APInt value(materializedWidth, 0), unknown(materializedWidth, 0);
  if (!hasUnknown) {
    // APInt's string constructor requires a width that can hold the literal,
    // and wraps silently in a no-assert build otherwise. Parse at the width
    // the digits need, then apply the explicitly declared size. IEEE
    // 1800-2017 5.7.1 requires excess high bits of a sized literal to be
    // discarded; only an unsized spelling that contradicts its semantic width
    // remains malformed IR.
    unsigned needed = APInt::getSufficientBitsNeeded(digits, radix);
    unsigned parseWidth = std::max(needed, materializedWidth);
    APInt parsed(parseWidth, digits, radix);
    bool fits = explicitlySized || parsed.getActiveBits() <= literalWidth;
    if (negative && quote == StringRef::npos) {
      APInt signedLimit(parseWidth, 1);
      signedLimit <<= literalWidth - 1;
      fits = parsed.ule(signedLimit);
    }
    if (!fits) {
      emitError(location) << "integer literal '" << spelling
                          << "' does not fit " << "in " << literalWidth
                          << " bits";
      return failure();
    }
    parsed = parsed.trunc(materializedWidth);
    if (negative)
      parsed.negate();
    return resize(parsed, unknown);
  }
  if (radix == 10) {
    unknown.setAllBits();
    if (digits.front() == 'z' || digits.front() == '?')
      value.setAllBits();
    return resize(value, unknown);
  }
  unsigned group = radix == 2 ? 1 : radix == 8 ? 3 : 4;
  unsigned bit = 0;
  for (char c : llvm::reverse(digits)) {
    if (bit >= materializedWidth)
      break;
    if (c == 'x' || c == 'z' || c == '?') {
      for (unsigned i = 0; i < group && bit + i < materializedWidth; ++i) {
        unknown.setBit(bit + i);
        if (c == 'z' || c == '?')
          value.setBit(bit + i);
      }
    } else {
      unsigned digit = llvm::hexDigitValue(c);
      for (unsigned i = 0; i < group && bit + i < materializedWidth; ++i)
        if (digit & (1u << i))
          value.setBit(bit + i);
    }
    bit += group;
  }

  // When the written digits do not fill the declared size, 5.7.1 requires
  // the leftmost x or z digit to fill all remaining high bits. Known leading
  // digits retain the ordinary zero extension.
  char leading = digits.front();
  if (bit < materializedWidth &&
      (leading == 'x' || leading == 'z' || leading == '?')) {
    for (unsigned i = bit; i < materializedWidth; ++i) {
      unknown.setBit(i);
      if (leading == 'z' || leading == '?')
        value.setBit(i);
    }
  }
  if (negative)
    value.negate();
  return resize(value, unknown);
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
  std::function<FailureOr<sim::FrozenConstantAttr>(Type, Type, StringRef)>
      freeze = [&](Type normalizedType, Type sourceType,
                   StringRef text) -> FailureOr<sim::FrozenConstantAttr> {
    Attribute payload;
    if (isa<sim::StringType>(normalizedType)) {
      // IEEE 1800-2017 5.9 makes a string literal's value exactly the
      // characters it encloses, so its spelling is taken as written -- padding
      // is part of the value, not layout around it.
      payload = builder.getStringAttr(text);
    } else if (isa<FloatType>(normalizedType)) {
      double value = 0.0;
      StringRef spelling = text.trim();
      if (spelling.getAsDouble(value)) {
        emitError(location) << "invalid real constant '" << spelling << "'";
        return failure();
      }
      payload = builder.getFloatAttr(normalizedType, value);
    } else if (auto array = dyn_cast<sim::UnpackedArrayType>(normalizedType)) {
      text = text.trim();
      Type sourceElementType;
      if (auto source = dyn_cast<semantic::RangedUnpackedArrayType>(sourceType))
        sourceElementType = source.getElementType();
      else if (auto source = dyn_cast<semantic::UnpackedArrayType>(sourceType))
        sourceElementType = source.getElementType();
      if (!sourceElementType || !text.consume_front("[") ||
          !text.consume_back("]")) {
        emitError(location)
            << "malformed unpacked-array constant '" << text << "'";
        return failure();
      }
      unsigned count = sim::getAggregateNumElements(array);
      SmallVector<StringRef> elementSpellings;
      if (isa<sim::StringType>(array.getElementType())) {
        // The elaborated spelling wraps each string element in quotes, so a
        // comma inside a string is not a separator: `["a,b", "c"]` has two
        // elements, not three. Walk the quotes instead -- an element opens at
        // its `"` and closes at the `"` that a separator, or the end of the
        // array, follows.
        StringRef remaining = text;
        for (unsigned element = 0; element != count; ++element) {
          bool last = element + 1 == count;
          size_t end = StringRef::npos;
          if (remaining.consume_front("\"")) {
            if (last) {
              end = remaining.rfind('"');
              if (end != StringRef::npos && end + 1 != remaining.size())
                end = StringRef::npos;
            } else {
              for (size_t quote = remaining.find('"'); quote != StringRef::npos;
                   quote = remaining.find('"', quote + 1))
                if (remaining.drop_front(quote + 1).ltrim().starts_with(",")) {
                  end = quote;
                  break;
                }
            }
          }
          if (end == StringRef::npos) {
            elementSpellings.clear();
            break;
          }
          elementSpellings.push_back(remaining.take_front(end));
          remaining = remaining.drop_front(end + 1);
          if (!last)
            remaining = remaining.ltrim().drop_front(1).ltrim();
        }
        if (!remaining.empty())
          elementSpellings.clear();
      } else {
        unsigned depth = 0;
        size_t begin = 0;
        for (size_t index = 0; index != text.size(); ++index) {
          if (text[index] == '[')
            ++depth;
          else if (text[index] == ']') {
            if (depth == 0) {
              emitError(location)
                  << "malformed unpacked-array constant '" << text << "'";
              return failure();
            }
            --depth;
          } else if (text[index] == ',' && depth == 0) {
            elementSpellings.push_back(text.slice(begin, index));
            begin = index + 1;
          }
        }
        if (depth != 0) {
          emitError(location)
              << "malformed unpacked-array constant '" << text << "'";
          return failure();
        }
        elementSpellings.push_back(text.drop_front(begin));
      }
      if (elementSpellings.size() != count) {
        emitError(location)
            << "unpacked-array constant has " << elementSpellings.size()
            << " elements; expected " << count;
        return failure();
      }
      SmallVector<Attribute> elements;
      elements.reserve(count);
      for (StringRef elementSpelling : elementSpellings) {
        FailureOr<sim::FrozenConstantAttr> element =
            freeze(array.getElementType(), sourceElementType, elementSpelling);
        if (failed(element))
          return failure();
        elements.push_back(*element);
      }
      payload = builder.getArrayAttr(elements);
    } else {
      Type scalar = sim::getPackedScalarType(normalizedType);
      std::optional<unsigned> width =
          scalar ? sim::getPackedWidth(scalar) : std::nullopt;
      if (!scalar || !width) {
        emitError(location)
            << "elaborated constant has unsupported normalized type "
            << normalizedType;
        return failure();
      }
      FailureOr<ParsedConstant> parsed =
          parseSVInteger(text.trim(), *width, location);
      if (failed(parsed))
        return failure();
      auto planeType = builder.getIntegerType(*width);
      payload = builder.getArrayAttr(
          {builder.getIntegerAttr(planeType, parsed->value),
           builder.getIntegerAttr(planeType, parsed->unknown)});
    }
    return sim::FrozenConstantAttr::get(symbol->getContext(), normalizedType,
                                        payload,
                                        isSignedSemanticType(sourceType));
  };
  return freeze(*normalized, semanticType.getValue(), spelling.getValue());
}

Value createDefaultValue(OpBuilder &builder, Location location, Type type) {
  if (isa<sim::ProcessType>(type))
    return sim::SimProcessNullOp::create(builder, location);
  if (isa<sim::ClassHandleType>(type))
    return sim::SimClassNullOp::create(builder, location, type);
  if (isa<sim::CovergroupHandleType>(type))
    return sim::SimCovergroupNullOp::create(builder, location, type);
  if (isa<sim::VirtualInterfaceType>(type))
    return sim::SimVirtualInterfaceNullOp::create(builder, location, type);
  if (isa<sim::ChandleType>(type))
    return sim::SimChandleNullOp::create(builder, location);
  if (isa<sim::EventType>(type))
    return sim::SimEventCreateOp::create(builder, location, type);
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
      .Case<sim::SimSuspendEventOrderOp>(
          [](auto) { return sim::ComputeActionKind::SuspendEvent; })
      .Case<sim::SimSuspendMailboxOp>(
          [](auto) { return sim::ComputeActionKind::SuspendMailbox; })
      .Case<sim::SimSuspendSemaphoreOp>(
          [](auto) { return sim::ComputeActionKind::SuspendSemaphore; })
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
            sim::SimSuspendEventOp, sim::SimSuspendEventOrderOp,
            sim::SimSuspendMailboxOp, sim::SimSuspendSemaphoreOp,
            sim::SimSuspendObserveOp, sim::SimSuspendForeverOp,
            sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp,
            sim::SimSuspendChildrenOp, sim::SimTaskCallOp,
            sim::SimClassVirtualTaskCallOp, sim::SimProcessControlOp>(
          [&](auto op) { site = op.getSiteAttr(); });
  return site;
}

void setContinuationSite(Operation *operation, sim::ContinuationSiteAttr site) {
  llvm::TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
            sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
            sim::SimSuspendLevelOp, sim::SimSuspendAnyOp,
            sim::SimSuspendEventOp, sim::SimSuspendEventOrderOp,
            sim::SimSuspendMailboxOp, sim::SimSuspendSemaphoreOp,
            sim::SimSuspendObserveOp, sim::SimSuspendForeverOp,
            sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp,
            sim::SimSuspendChildrenOp, sim::SimTaskCallOp,
            sim::SimClassVirtualTaskCallOp, sim::SimProcessControlOp>(
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
