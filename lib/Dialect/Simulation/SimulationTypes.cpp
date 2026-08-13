//===- SimulationTypes.cpp - Type verifiers and type interfaces ===//
//
// Verifiers for the simulation dialect types and the field/binding attributes
// that describe them, plus the destructurable type interface implementations.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "SimulationVerifiers.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Transforms/InliningUtils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/ADT/bit.h"

#include <algorithm>
#include <limits>
#include <optional>

using namespace mlir;

namespace obelisk::sim {

bool isNormalizedValueType(Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.isSignless();
  return isa<FloatType>(type) || isa<LogicType, EventType>(type) ||
         isa<CovergroupHandleType, VirtualInterfaceType, ChandleType,
             ProcessType>(type) ||
         isManagedHandleType(type) || isAggregateType(type);
}

LogicalResult
DynamicArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         Type elementType) {
  if (!isNormalizedValueType(elementType))
    return emitError() << "element must be a normalized simulation value";
  return success();
}

LogicalResult
QueueType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                  Type elementType, uint32_t) {
  if (!isNormalizedValueType(elementType))
    return emitError() << "element must be a normalized simulation value";
  return success();
}

LogicalResult
MailboxType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                    Type elementType) {
  if (!isNormalizedValueType(elementType))
    return emitError() << "element must be a normalized simulation value";
  return success();
}

LogicalResult
AssocArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                       Type keyType, Type elementType, bool signedKey,
                       bool wildcardIndex) {
  if (!isNormalizedValueType(elementType))
    return emitError() << "element must be a normalized simulation value";
  if (wildcardIndex)
    return emitError()
           << "wildcard associative-array indices are not executable";
  bool supportedKey = isa<StringType, ClassHandleType, ProcessType>(keyType);
  if (auto integer = dyn_cast<IntegerType>(keyType))
    supportedKey = integer.isSignless() && integer.getWidth() != 0;
  if (auto logic = dyn_cast<LogicType>(keyType))
    supportedKey = logic.getWidth() != 0;
  if (!supportedKey)
    return emitError()
           << "key must be a string, class handle, process, or normalized "
              "integral type";
  if (isa<StringType, ClassHandleType, ProcessType>(keyType) && signedKey)
    return emitError() << "string, class, or process key cannot be signed";
  return success();
}

LogicalResult
ReferencePathType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          Type elementType) {
  if (!isNormalizedValueType(elementType))
    return emitError() << "element must be a normalized simulation value";
  return success();
}

LogicalResult
FrozenConstantAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                           Type type, Attribute value, bool) {
  if (!type)
    return emitError() << "frozen constant type must not be null";
  if (!value)
    return emitError() << "frozen constant payload must not be null";
  if (isa<StringType>(type)) {
    if (!isa<StringAttr>(value))
      return emitError()
             << "string frozen constant requires a string attribute payload";
    return success();
  }
  if (isa<FloatType>(type)) {
    auto floating = dyn_cast<FloatAttr>(value);
    if (!floating || floating.getType() != type)
      return emitError()
             << "floating frozen constant requires a matching payload";
    return success();
  }
  if (auto array = dyn_cast<UnpackedArrayType>(type)) {
    auto elements = dyn_cast<ArrayAttr>(value);
    unsigned count = getAggregateNumElements(array);
    if (!elements || elements.size() != count)
      return emitError()
             << "fixed unpacked-array frozen constant requires exactly "
             << count << " element payloads";
    for (Attribute elementAttr : elements) {
      auto element = dyn_cast<FrozenConstantAttr>(elementAttr);
      if (!element || element.getType() != array.getElementType())
        return emitError()
               << "fixed unpacked-array frozen constant requires matching "
                  "typed element payloads";
    }
    return success();
  }

  Type scalar = getPackedScalarType(type);
  if (!scalar)
    return emitError() << "frozen constant type must be floating, a fixed "
                          "unpacked array, or a fixed packed value, got "
                       << type;
  std::optional<unsigned> width = getPackedWidth(scalar);
  auto planes = dyn_cast<ArrayAttr>(value);
  if (!width || !planes || planes.size() != 2)
    return emitError() << "packed frozen constant requires exactly two integer "
                          "planes matching its scalar width";
  auto valuePlane = dyn_cast<IntegerAttr>(planes[0]);
  auto unknownPlane = dyn_cast<IntegerAttr>(planes[1]);
  if (!valuePlane || !unknownPlane ||
      valuePlane.getValue().getBitWidth() != *width ||
      unknownPlane.getValue().getBitWidth() != *width ||
      !valuePlane.getType().isSignlessInteger(*width) ||
      !unknownPlane.getType().isSignlessInteger(*width))
    return emitError() << "packed frozen constant planes must be signless i"
                       << *width << " integer attributes";
  if (isa<IntegerType>(scalar) && !unknownPlane.getValue().isZero())
    return emitError()
           << "two-state frozen constant must have a zero unknown plane";
  return success();
}

LogicalResult
ArgumentBindingAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            StringAttr path, uint64_t, UnitArgumentKind kind,
                            bool copyOut, IntegerAttr lvalueNode) {
  if (!path || path.getValue().empty())
    return emitError() << "argument binding path must not be empty";
  if (copyOut && kind != UnitArgumentKind::FormalLocal)
    return emitError()
           << "copy-out is valid only for a formal-local argument binding";
  if (lvalueNode && kind != UnitArgumentKind::LValueOnly)
    return emitError()
           << "lvalue node ID is valid only for an lvalue-only binding";
  if (lvalueNode && (lvalueNode.getValue().isNegative() ||
                     lvalueNode.getValue().getActiveBits() > 64))
    return emitError() << "lvalue node ID must be an unsigned 64-bit value";
  return success();
}

LogicalResult
LocalBindingAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         StringAttr path, Type type, bool automatic,
                         bool patternVariable, bool) {
  if (!path || path.getValue().empty())
    return emitError() << "local binding path must not be empty";
  if (!type || !isNormalizedValueType(type))
    return emitError()
           << "local binding type must be a normalized simulation value";
  if (patternVariable && !automatic)
    return emitError() << "pattern-variable binding must be automatic";
  return success();
}

LogicalResult
ConstantBindingAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            StringAttr path, FrozenConstantAttr value) {
  if (!path || path.getValue().empty())
    return emitError() << "constant binding path must not be empty";
  if (!value)
    return emitError() << "constant binding value must not be null";
  return success();
}

StringRef getUnitBindingPath(Attribute binding) {
  if (auto argument = dyn_cast<ArgumentBindingAttr>(binding))
    return argument.getPath().getValue();
  if (auto local = dyn_cast<LocalBindingAttr>(binding))
    return local.getPath().getValue();
  if (auto constant = dyn_cast<ConstantBindingAttr>(binding))
    return constant.getPath().getValue();
  return {};
}

FailureOr<Value> materializeFrozenConstant(OpBuilder &builder,
                                           Location location,
                                           FrozenConstantAttr constant) {
  if (!constant)
    return failure();
  Type type = constant.getType();
  if (isa<StringType>(type)) {
    auto value = dyn_cast<StringAttr>(constant.getValue());
    if (!value)
      return failure();
    return SimStringLiteralOp::create(builder, location, type, value)
        .getResult();
  }
  if (isa<FloatType>(type)) {
    auto value = dyn_cast<FloatAttr>(constant.getValue());
    if (!value)
      return failure();
    return arith::ConstantOp::create(builder, location, type, value)
        .getResult();
  }
  if (auto array = dyn_cast<UnpackedArrayType>(type)) {
    auto elements = dyn_cast<ArrayAttr>(constant.getValue());
    unsigned count = getAggregateNumElements(array);
    if (!elements || elements.size() != count)
      return failure();
    SmallVector<Value> values;
    values.reserve(count);
    for (Attribute elementAttr : elements) {
      auto element = dyn_cast<FrozenConstantAttr>(elementAttr);
      if (!element || element.getType() != array.getElementType())
        return failure();
      FailureOr<Value> value =
          materializeFrozenConstant(builder, location, element);
      if (failed(value))
        return failure();
      values.push_back(*value);
    }
    return SimAggregateConstructOp::create(builder, location, type, values)
        .getResult();
  }

  Type scalar = getPackedScalarType(type);
  auto planes = dyn_cast<ArrayAttr>(constant.getValue());
  if (!scalar || !planes || planes.size() != 2)
    return failure();
  auto valuePlane = dyn_cast<IntegerAttr>(planes[0]);
  auto unknownPlane = dyn_cast<IntegerAttr>(planes[1]);
  if (!valuePlane || !unknownPlane)
    return failure();

  Value value;
  if (auto integer = dyn_cast<IntegerType>(scalar))
    value = arith::ConstantOp::create(builder, location, integer, valuePlane);
  else if (isa<LogicType>(scalar))
    value = SimLogicConstantOp::create(builder, location, scalar, valuePlane,
                                       unknownPlane);
  else
    return failure();
  if (type != scalar)
    value = SimPackedUnflattenOp::create(builder, location, type, value);
  return value;
}

LogicalResult verifyNormalizedIndex(Operation *op, Type type) {
  if (isa<LogicType>(type))
    return success();
  auto integer = dyn_cast<IntegerType>(type);
  if (!integer)
    return op->emitOpError(
        "index must be a signless builtin integer or four-state logic");
  if (!integer.isSignless())
    return op->emitOpError("builtin integer index must be signless");
  return success();
}

LogicalResult verifyMatchingStateDomain(Operation *op, Type input,
                                               Type result) {
  Type inputScalar = getPackedScalarType(input);
  Type resultScalar = getPackedScalarType(result);
  if (!inputScalar || !resultScalar ||
      isa<LogicType>(inputScalar) != isa<LogicType>(resultScalar))
    return op->emitOpError(
        "input and result element types must use the same state domain");
  return success();
}

LogicalResult
verifyElementType(llvm::function_ref<InFlightDiagnostic()> emitError,
                  Type elementType) {
  if (auto integer = dyn_cast<IntegerType>(elementType);
      integer && !integer.isSignless())
    return emitError() << "builtin integer element types must be signless";
  if (!isNormalizedValueType(elementType))
    return emitError() << "element type must be a normalized scalar or fixed "
                          "aggregate, got "
                       << elementType;
  return success();
}

LogicalResult
FieldAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                  StringAttr name, Type type, uint32_t, uint64_t) {
  if (!name || name.getValue().empty())
    return emitError() << "aggregate field name must not be empty";
  if (!type)
    return emitError() << "aggregate field type must not be null";
  return success();
}

static std::optional<uint64_t> getInclusiveRangeWidth(int64_t left,
                                                      int64_t right) {
  uint64_t distance =
      left >= right
          ? static_cast<uint64_t>(left) - static_cast<uint64_t>(right)
          : static_cast<uint64_t>(right) - static_cast<uint64_t>(left);
  if (distance == std::numeric_limits<uint64_t>::max())
    return std::nullopt;
  return distance + 1;
}

static LogicalResult
verifyArrayType(llvm::function_ref<InFlightDiagnostic()> emitError,
                Type elementType, int64_t left, int64_t right, bool packed) {
  std::optional<uint64_t> width = getInclusiveRangeWidth(left, right);
  if (!width || *width > std::numeric_limits<unsigned>::max())
    return emitError() << "fixed array range is too large";
  if (failed(verifyElementType(emitError, elementType)))
    return failure();
  if (packed && isa<FloatType>(elementType))
    return emitError() << "real-valued aggregate elements are not supported";
  if (packed) {
    std::optional<unsigned> elementWidth = getPackedWidth(elementType);
    if (!elementWidth)
      return emitError() << "packed array element must be packed, got "
                         << elementType;
    if (*width > std::numeric_limits<unsigned>::max() / *elementWidth)
      return emitError() << "packed array width exceeds the supported limit";
  }
  return success();
}

LogicalResult
PackedArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        Type elementType, int64_t left, int64_t right) {
  return verifyArrayType(emitError, elementType, left, right, true);
}

LogicalResult
UnpackedArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          Type elementType, int64_t left, int64_t right) {
  return verifyArrayType(emitError, elementType, left, right, false);
}

static LogicalResult
verifyRecordType(llvm::function_ref<InFlightDiagnostic()> emitError,
                 ArrayAttr fields, bool packed, bool isUnion, bool isTagged,
                 uint32_t tagBits) {
  if (!fields || fields.empty())
    return emitError() << "aggregate requires at least one field";
  if (!packed && tagBits != 0)
    return emitError() << "unpacked union cannot reserve packed tag bits";
  if (!isTagged && tagBits != 0)
    return emitError() << "only a tagged union can reserve tag bits";
  llvm::SmallDenseSet<StringRef, 8> names;
  SmallVector<std::pair<uint64_t, uint64_t>> intervals;
  for (auto [ordinal, attribute] : llvm::enumerate(fields)) {
    auto field = dyn_cast<FieldAttr>(attribute);
    if (!field)
      return emitError() << "aggregate fields must use #obelisk_sim.field";
    if (field.getOrdinal() != ordinal)
      return emitError()
             << "aggregate field ordinals must be dense and ordered";
    if (!names.insert(field.getName().getValue()).second)
      return emitError() << "aggregate field names must be unique";
    if (failed(verifyElementType(emitError, field.getType())))
      return failure();
    if (packed && isa<FloatType>(field.getType()))
      return emitError() << "real-valued aggregate fields are not supported";
    if (!packed) {
      if (field.getPackedOffset() != 0)
        return emitError() << "unpacked aggregate field has a packed offset";
      continue;
    }
    std::optional<unsigned> width = getPackedWidth(field.getType());
    if (!width)
      return emitError() << "packed aggregate field must be packed, got "
                         << field.getType();
    if (field.getPackedOffset() > std::numeric_limits<uint64_t>::max() - *width)
      return emitError() << "packed aggregate field range overflows uint64_t";
    if (field.getPackedOffset() + *width > std::numeric_limits<unsigned>::max())
      return emitError()
             << "packed aggregate width exceeds the supported limit";
    intervals.push_back(
        {field.getPackedOffset(), field.getPackedOffset() + *width});
  }
  if (packed && !isUnion) {
    llvm::sort(intervals);
    if (intervals.front().first != 0)
      return emitError() << "packed struct fields must cover bit zero";
    for (auto [previous, current] :
         llvm::zip(intervals, llvm::drop_begin(intervals)))
      if (previous.second > current.first)
        return emitError() << "packed struct fields overlap";
      else if (previous.second < current.first)
        return emitError() << "packed struct fields must be contiguous";
  }
  if (packed && isUnion) {
    for (auto interval : intervals)
      if (interval.first != 0)
        return emitError() << "packed union fields must start at bit zero";
    if (!isTagged) {
      uint64_t width = intervals.front().second;
      for (auto interval : llvm::drop_begin(intervals))
        if (interval.second != width)
          return emitError()
                 << "untagged packed union fields must have equal widths";
    }
    uint32_t expectedTagBits = static_cast<uint32_t>(
        llvm::bit_width(static_cast<uint64_t>(fields.size() - 1)));
    if (isTagged && tagBits != expectedTagBits)
      return emitError() << "packed tagged union requires " << expectedTagBits
                         << " tag bits";
    uint64_t payloadWidth = 0;
    for (auto interval : intervals)
      payloadWidth = std::max(payloadWidth, interval.second);
    if (tagBits > std::numeric_limits<unsigned>::max() - payloadWidth)
      return emitError()
             << "packed tagged union width exceeds the supported limit";
  }
  return success();
}

LogicalResult
PackedStructType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         ArrayAttr fields) {
  return verifyRecordType(emitError, fields, true, false, false, 0);
}

LogicalResult
UnpackedStructType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                           ArrayAttr fields) {
  return verifyRecordType(emitError, fields, false, false, false, 0);
}

LogicalResult
PackedUnionType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        ArrayAttr fields, bool isTagged, uint32_t tagBits) {
  return verifyRecordType(emitError, fields, true, true, isTagged, tagBits);
}

LogicalResult
UnpackedUnionType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          ArrayAttr fields, bool isTagged, uint32_t tagBits) {
  return verifyRecordType(emitError, fields, false, true, isTagged, tagBits);
}

IntegerAttr getSubelementIndexAttr(MLIRContext *context,
                                          unsigned index) {
  return IntegerAttr::get(IntegerType::get(context, 32), index);
}

static std::optional<DenseMap<Attribute, Type>>
getSubelementIndexMap(Type type, bool limitArray) {
  unsigned count = getAggregateNumElements(type);
  if (limitArray && count > 64)
    return std::nullopt;
  DenseMap<Attribute, Type> elements;
  for (unsigned index = 0; index < count; ++index)
    elements.insert({getSubelementIndexAttr(type.getContext(), index),
                     getAggregateElementType(type, index)});
  return elements;
}

static Type getTypeAtSubelementIndex(Type type, Attribute index) {
  auto integer = dyn_cast<IntegerAttr>(index);
  if (!integer || !integer.getType().isInteger(32) ||
      integer.getValue().isNegative() ||
      integer.getValue().getActiveBits() > 32)
    return {};
  return getAggregateElementType(type, static_cast<unsigned>(integer.getInt()));
}

#define OBELISK_DEFINE_ARRAY_DESTRUCTURABLE(TypeName)                          \
  std::optional<DenseMap<Attribute, Type>> TypeName::getSubelementIndexMap()   \
      const {                                                                  \
    return ::obelisk::sim::getSubelementIndexMap(*this, true);                 \
  }                                                                            \
  Type TypeName::getTypeAtIndex(Attribute index) const {                       \
    return getTypeAtSubelementIndex(*this, index);                             \
  }

#define OBELISK_DEFINE_RECORD_DESTRUCTURABLE(TypeName)                         \
  std::optional<DenseMap<Attribute, Type>> TypeName::getSubelementIndexMap()   \
      const {                                                                  \
    return ::obelisk::sim::getSubelementIndexMap(*this, false);                \
  }                                                                            \
  Type TypeName::getTypeAtIndex(Attribute index) const {                       \
    return getTypeAtSubelementIndex(*this, index);                             \
  }

OBELISK_DEFINE_ARRAY_DESTRUCTURABLE(PackedArrayType)
OBELISK_DEFINE_ARRAY_DESTRUCTURABLE(UnpackedArrayType)
OBELISK_DEFINE_RECORD_DESTRUCTURABLE(PackedStructType)
OBELISK_DEFINE_RECORD_DESTRUCTURABLE(UnpackedStructType)
OBELISK_DEFINE_RECORD_DESTRUCTURABLE(PackedUnionType)
OBELISK_DEFINE_RECORD_DESTRUCTURABLE(UnpackedUnionType)

#undef OBELISK_DEFINE_ARRAY_DESTRUCTURABLE
#undef OBELISK_DEFINE_RECORD_DESTRUCTURABLE

LogicalResult
LogicType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                  unsigned width) {
  if (width == 0)
    return emitError() << "logic width must be greater than zero";
  return success();
}

LogicalResult
RefType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                Type elementType) {
  return verifyElementType(emitError, elementType);
}

LogicalResult
NetType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                Type elementType) {
  if (elementType.isF64())
    return emitError() << "real-valued nets are not supported";
  return verifyElementType(emitError, elementType);
}

LogicalResult
DriverType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                   Type elementType) {
  if (elementType.isF64())
    return emitError() << "real-valued drivers are not supported";
  return verifyElementType(emitError, elementType);
}

LogicalResult
ClassHandleType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        SymbolRefAttr className) {
  if (!className || className.getRootReference().empty())
    return emitError() << "class handle requires a class symbol";
  return success();
}

LogicalResult
CovergroupHandleType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                             SymbolRefAttr covergroupName) {
  if (!covergroupName || covergroupName.getRootReference().empty())
    return emitError() << "covergroup handle requires a declaration symbol";
  return success();
}

LogicalResult VirtualInterfaceType::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError,
    StringAttr interfaceName, StringAttr modport) {
  if (!interfaceName || interfaceName.empty())
    return emitError() << "virtual interface requires an interface identity";
  if (!modport)
    return emitError() << "virtual interface requires a modport string";
  return success();
}

LogicalResult
ManagedRefType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                       Type elementType, SymbolRefAttr ownerClass) {
  if (!ownerClass || ownerClass.getRootReference().empty())
    return emitError() << "managed reference requires an owner class";
  if (!isNormalizedValueType(elementType))
    return emitError()
           << "managed reference element must be a normalized value";
  return success();
}

LogicalResult
ArgumentRefType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        Type elementType) {
  return verifyElementType(emitError, elementType);
}

LogicalResult
ObserverType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                     Type resultType) {
  if (!isa<IntegerType, LogicType, FloatType>(resultType))
    return emitError() << "observer result must be a scalar value";
  if (auto integer = dyn_cast<IntegerType>(resultType);
      integer && (!integer.isSignless() || integer.getWidth() == 0))
    return emitError()
           << "observer integer result must be nonempty and signless";
  return success();
}

LogicalResult verifyNonnegative(Operation *op, IntegerAttr attr,
                                       StringRef name) {
  if (attr.getValue().isNegative())
    return op->emitOpError() << name << " must be nonnegative";
  return success();
}

LogicalResult verifyPositive(Operation *op, IntegerAttr attr,
                                    StringRef name) {
  if (!attr.getValue().isStrictlyPositive())
    return op->emitOpError() << name << " must be positive";
  return success();
}

std::optional<CaptureKind> getCaptureKind(DictionaryAttr attrs) {
  if (!attrs)
    return std::nullopt;
  auto value =
      dyn_cast_or_null<CaptureKindAttr>(attrs.get(metadata::captureKind));
  if (!value)
    return std::nullopt;
  return value.getValue();
}

} // namespace obelisk::sim
