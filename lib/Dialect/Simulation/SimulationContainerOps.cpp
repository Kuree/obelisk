//===- SimulationContainerOps.cpp - Container, string, and managed op verifiers ===//
//
// Verifiers for managed handles, dynamic arrays, queues, associative arrays,
// and string operations, plus the aggregate field helpers they share.
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
#include <functional>
#include <limits>
#include <optional>
#include <tuple>

using namespace mlir;

namespace obelisk::sim {

bool containsFourStateLeaf(Type type) {
  if (isa<LogicType>(type))
    return true;
  if (!isAggregateType(type))
    return false;
  for (unsigned index = 0, end = getAggregateNumElements(type); index < end;
       ++index)
    if (containsFourStateLeaf(getAggregateElementType(type, index)))
      return true;
  return false;
}

Type getPackedScalarType(Type type) {
  std::optional<unsigned> width = getPackedWidth(type);
  if (!width)
    return {};
  if (!isAggregateType(type))
    return type;
  if (containsFourStateLeaf(type))
    return LogicType::get(type.getContext(), *width);
  return IntegerType::get(type.getContext(), *width);
}

bool isAggregateType(Type type) {
  return isa<PackedArrayType, UnpackedArrayType, PackedStructType,
             UnpackedStructType, PackedUnionType, UnpackedUnionType>(type);
}

static ArrayAttr getAggregateFields(Type type) {
  return llvm::TypeSwitch<Type, ArrayAttr>(type)
      .Case<PackedStructType, UnpackedStructType, PackedUnionType,
            UnpackedUnionType>(
          [](auto aggregate) { return aggregate.getFields(); })
      .Default([](Type) { return ArrayAttr{}; });
}

unsigned getAggregateNumElements(Type type) {
  if (auto array = dyn_cast<PackedArrayType>(type)) {
    std::optional<unsigned> last =
        getArrayElementOrdinal(array, array.getRight());
    return last ? *last + 1 : 0;
  }
  if (auto array = dyn_cast<UnpackedArrayType>(type)) {
    std::optional<unsigned> last =
        getArrayElementOrdinal(array, array.getRight());
    return last ? *last + 1 : 0;
  }
  ArrayAttr fields = getAggregateFields(type);
  return fields ? fields.size() : 0;
}

Type getAggregateElementType(Type type, unsigned index) {
  if (auto array = dyn_cast<PackedArrayType>(type))
    return index < getAggregateNumElements(type) ? array.getElementType()
                                                 : Type{};
  if (auto array = dyn_cast<UnpackedArrayType>(type))
    return index < getAggregateNumElements(type) ? array.getElementType()
                                                 : Type{};
  ArrayAttr fields = getAggregateFields(type);
  if (!fields || index >= fields.size())
    return {};
  auto field = dyn_cast<FieldAttr>(fields[index]);
  return field ? field.getType() : Type{};
}

std::optional<unsigned> getArrayElementOrdinal(Type type, int64_t sourceIndex) {
  int64_t left;
  int64_t right;
  if (auto array = dyn_cast<PackedArrayType>(type)) {
    left = array.getLeft();
    right = array.getRight();
  } else if (auto array = dyn_cast<UnpackedArrayType>(type)) {
    left = array.getLeft();
    right = array.getRight();
  } else {
    return std::nullopt;
  }
  uint64_t ordinal;
  if (left >= right) {
    if (sourceIndex > left || sourceIndex < right)
      return std::nullopt;
    ordinal = static_cast<uint64_t>(left) - static_cast<uint64_t>(sourceIndex);
  } else {
    if (sourceIndex < left || sourceIndex > right)
      return std::nullopt;
    ordinal = static_cast<uint64_t>(sourceIndex) - static_cast<uint64_t>(left);
  }
  if (ordinal > std::numeric_limits<unsigned>::max())
    return std::nullopt;
  return static_cast<unsigned>(ordinal);
}

std::optional<uint64_t> getProvenanceSpan(Type type) {
  if (auto reference = dyn_cast<RefType>(type))
    return getProvenanceSpan(reference.getElementType());
  if (auto net = dyn_cast<NetType>(type))
    return getProvenanceSpan(net.getElementType());
  if (auto driver = dyn_cast<DriverType>(type))
    return getProvenanceSpan(driver.getElementType());
  if (isa<EventType>(type))
    return uint64_t{1};
  if (isa<ProcessType>(type))
    return uint64_t{64};
  if (isa<VirtualInterfaceType>(type))
    return uint64_t{64};
  if (isa<ChandleType>(type))
    return uint64_t{64};
  if (std::optional<unsigned> packed = getPackedWidth(type))
    return *packed;
  if (isa<TimeType>(type) || type.isF64())
    return uint64_t{64};
  if (type.isF32())
    return uint64_t{32};
  if (isManagedHandleType(type))
    return uint64_t{64};
  auto checkedAlign = [](uint64_t value,
                         uint64_t alignment) -> std::optional<uint64_t> {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
      return std::nullopt;
    return (value + alignment - 1) & ~(alignment - 1);
  };
  auto checkedAdd = [](uint64_t &total, uint64_t amount) {
    if (amount > std::numeric_limits<uint64_t>::max() - total)
      return false;
    total += amount;
    return true;
  };
  if (isa<UnpackedArrayType>(type)) {
    uint64_t count = getAggregateNumElements(type);
    Type elementType = getAggregateElementType(type, 0);
    std::optional<uint64_t> element = getProvenanceSpan(elementType);
    std::optional<uint64_t> alignment = getProvenanceAlignment(elementType);
    std::optional<uint64_t> stride = element && alignment
                                         ? checkedAlign(*element, *alignment)
                                         : std::nullopt;
    if (!stride ||
        (count && *stride > std::numeric_limits<uint64_t>::max() / count))
      return std::nullopt;
    return count * *stride;
  }
  // Unpacked tagged unions use a disjoint internal payload. Unlike an
  // ordinary union, inactive members have no observable representation and
  // can therefore occupy separate aligned slots selected by the tag. This
  // keeps embedded managed handles permanently well-typed for precise GC.
  bool disjointTaggedUnion = false;
  if (auto unionType = dyn_cast<UnpackedUnionType>(type))
    disjointTaggedUnion = unionType.getIsTagged();
  if (isa<UnpackedStructType>(type) || disjointTaggedUnion) {
    uint64_t total = 0;
    uint64_t alignment = 1;
    for (unsigned index = 0; index < getAggregateNumElements(type); ++index) {
      Type childType = getAggregateElementType(type, index);
      std::optional<uint64_t> child = getProvenanceSpan(childType);
      std::optional<uint64_t> childAlignment =
          getProvenanceAlignment(childType);
      std::optional<uint64_t> offset =
          childAlignment ? checkedAlign(total, *childAlignment) : std::nullopt;
      if (!child || !offset || !checkedAdd(*offset, *child))
        return std::nullopt;
      total = *offset;
      alignment = std::max(alignment, *childAlignment);
    }
    return checkedAlign(total, alignment);
  }
  if (isa<UnpackedUnionType>(type)) {
    uint64_t maximum = 0;
    uint64_t alignment = 1;
    for (unsigned index = 0; index < getAggregateNumElements(type); ++index) {
      Type childType = getAggregateElementType(type, index);
      std::optional<uint64_t> child = getProvenanceSpan(childType);
      std::optional<uint64_t> childAlignment =
          getProvenanceAlignment(childType);
      if (!child || !childAlignment)
        return std::nullopt;
      maximum = std::max(maximum, *child);
      alignment = std::max(alignment, *childAlignment);
    }
    return checkedAlign(maximum, alignment);
  }
  return std::nullopt;
}

std::optional<uint64_t> getProvenanceAlignment(Type type) {
  if (auto reference = dyn_cast<RefType>(type))
    return getProvenanceAlignment(reference.getElementType());
  if (auto net = dyn_cast<NetType>(type))
    return getProvenanceAlignment(net.getElementType());
  if (auto driver = dyn_cast<DriverType>(type))
    return getProvenanceAlignment(driver.getElementType());
  if (type.isF32())
    return uint64_t{32};
  if (type.isF64())
    return uint64_t{64};
  if (isManagedHandleType(type))
    return uint64_t{64};
  if (isa<UnpackedArrayType>(type))
    return getProvenanceAlignment(getAggregateElementType(type, 0));
  if (isa<UnpackedStructType, UnpackedUnionType>(type)) {
    uint64_t alignment = 1;
    for (unsigned index = 0; index < getAggregateNumElements(type); ++index) {
      std::optional<uint64_t> child =
          getProvenanceAlignment(getAggregateElementType(type, index));
      if (!child)
        return std::nullopt;
      alignment = std::max(alignment, *child);
    }
    return alignment;
  }
  return getProvenanceSpan(type) ? std::optional<uint64_t>{1} : std::nullopt;
}

std::optional<std::pair<uint64_t, uint64_t>>
getAggregateProvenanceSubelement(Type type, unsigned index) {
  Type element = getAggregateElementType(type, index);
  std::optional<uint64_t> span = getProvenanceSpan(element);
  if (!element || !span)
    return std::nullopt;
  uint64_t offset = 0;
  if (isa<PackedStructType, PackedUnionType>(type)) {
    auto field = cast<FieldAttr>(getAggregateFields(type)[index]);
    offset = field.getPackedOffset();
  } else if (isa<PackedArrayType>(type)) {
    uint64_t count = getAggregateNumElements(type);
    if (*span &&
        count - index - 1 > std::numeric_limits<uint64_t>::max() / *span)
      return std::nullopt;
    offset = (count - index - 1) * *span;
  } else if (isa<UnpackedArrayType>(type)) {
    std::optional<uint64_t> alignment = getProvenanceAlignment(element);
    if (!alignment || *alignment == 0 ||
        *span > std::numeric_limits<uint64_t>::max() - (*alignment - 1))
      return std::nullopt;
    uint64_t stride = (*span + *alignment - 1) & ~(*alignment - 1);
    if (stride && index > std::numeric_limits<uint64_t>::max() / stride)
      return std::nullopt;
    offset = index * stride;
  } else if (isa<UnpackedStructType>(type) ||
             (isa<UnpackedUnionType>(type) &&
              cast<UnpackedUnionType>(type).getIsTagged())) {
    for (unsigned previous = 0; previous < index; ++previous) {
      Type previousType = getAggregateElementType(type, previous);
      std::optional<uint64_t> previousSpan = getProvenanceSpan(previousType);
      std::optional<uint64_t> previousAlignment =
          getProvenanceAlignment(previousType);
      if (!previousSpan || !previousAlignment ||
          offset >
              std::numeric_limits<uint64_t>::max() - (*previousAlignment - 1))
        return std::nullopt;
      offset = (offset + *previousAlignment - 1) & ~(*previousAlignment - 1);
      if (*previousSpan > std::numeric_limits<uint64_t>::max() - offset)
        return std::nullopt;
      offset += *previousSpan;
    }
    std::optional<uint64_t> alignment = getProvenanceAlignment(element);
    if (!alignment ||
        offset > std::numeric_limits<uint64_t>::max() - (*alignment - 1))
      return std::nullopt;
    offset = (offset + *alignment - 1) & ~(*alignment - 1);
  } else if (!isa<UnpackedUnionType>(type)) {
    return std::nullopt;
  }
  return std::pair<uint64_t, uint64_t>{offset, *span};
}

bool getManagedHandleSlots(Type type,
                           llvm::SmallVectorImpl<ManagedHandleSlot> &slots) {
  auto leafKind = [](Type leaf) -> std::optional<uint32_t> {
    if (isa<ClassHandleType>(leaf))
      return static_cast<uint32_t>(ManagedHandleKind::Class);
    if (isa<StringType>(leaf))
      return static_cast<uint32_t>(ManagedHandleKind::String);
    if (isa<DynamicArrayType, QueueType, MailboxType, SemaphoreType,
            AssocArrayType>(leaf))
      return static_cast<uint32_t>(ManagedHandleKind::Container);
    if (isa<ReferencePathType>(leaf))
      return static_cast<uint32_t>(ManagedHandleKind::ReferencePath);
    return std::nullopt;
  };

  std::function<bool(Type, uint64_t, bool)> collect =
      [&](Type nestedType, uint64_t baseOffset, bool conditional) {
        if (std::optional<uint32_t> kind = leafKind(nestedType)) {
          slots.push_back({baseOffset, *kind, conditional});
          return true;
        }
        if (!isAggregateType(nestedType))
          return true;
        bool overlapping = isa<PackedUnionType>(nestedType);
        if (auto unpacked = dyn_cast<UnpackedUnionType>(nestedType))
          overlapping = !unpacked.getIsTagged();
        for (unsigned index = 0; index < getAggregateNumElements(nestedType);
             ++index) {
          std::optional<std::pair<uint64_t, uint64_t>> child =
              getAggregateProvenanceSubelement(nestedType, index);
          if (!child || child->first >
                            std::numeric_limits<uint64_t>::max() - baseOffset)
            return false;
          if (!collect(getAggregateElementType(nestedType, index),
                       baseOffset + child->first,
                       conditional || overlapping))
            return false;
        }
        return true;
      };
  size_t originalSize = slots.size();
  if (!collect(type, 0, false)) {
    slots.resize(originalSize);
    return false;
  }
  // Four-state overlapping unions need value/unknown-plane pairing before a
  // managed arm can be classified precisely. Keep this first chunk strictly
  // two-state instead of silently treating X/Z bits as an object word.
  if (containsFourStateLeaf(type) &&
      llvm::any_of(llvm::ArrayRef(slots).drop_front(originalSize),
                   [](const ManagedHandleSlot &slot) {
                     return slot.conditional;
                   })) {
    slots.resize(originalSize);
    return false;
  }
  llvm::sort(slots.begin() + originalSize, slots.end(),
             [](const ManagedHandleSlot &lhs, const ManagedHandleSlot &rhs) {
               return std::tie(lhs.bitOffset, lhs.kindMask, lhs.conditional) <
                      std::tie(rhs.bitOffset, rhs.kindMask, rhs.conditional);
             });
  size_t output = originalSize;
  for (size_t index = originalSize; index != slots.size(); ++index) {
    ManagedHandleSlot slot = slots[index];
    if (output != originalSize &&
        slots[output - 1].bitOffset == slot.bitOffset) {
      slots[output - 1].kindMask |= slot.kindMask;
      slots[output - 1].conditional |= slot.conditional;
      continue;
    }
    slots[output++] = slot;
  }
  slots.resize(output);
  return true;
}

std::optional<uint32_t>
getManagedHandleTraceKind(const ManagedHandleSlot &slot) {
  constexpr uint32_t allKinds =
      static_cast<uint32_t>(ManagedHandleKind::Class) |
      static_cast<uint32_t>(ManagedHandleKind::String) |
      static_cast<uint32_t>(ManagedHandleKind::Container) |
      static_cast<uint32_t>(ManagedHandleKind::ReferencePath);
  if (slot.kindMask == 0 || (slot.kindMask & ~allKinds) != 0)
    return std::nullopt;
  if (slot.conditional)
    return managedHandleCandidateFlag | slot.kindMask;
  switch (static_cast<ManagedHandleKind>(slot.kindMask)) {
  case ManagedHandleKind::Class:
    return 1;
  case ManagedHandleKind::String:
    return 2;
  case ManagedHandleKind::Container:
    return 3;
  case ManagedHandleKind::ReferencePath:
    return 4;
  }
  return std::nullopt;
}

static bool isValidManagedTraceKind(int32_t signedKind) {
  uint32_t kind = static_cast<uint32_t>(signedKind);
  if ((kind & managedHandleCandidateFlag) == 0)
    return kind >= 1 && kind <= 4;
  uint32_t mask = kind & ~managedHandleCandidateFlag;
  constexpr uint32_t allKinds =
      static_cast<uint32_t>(ManagedHandleKind::Class) |
      static_cast<uint32_t>(ManagedHandleKind::String) |
      static_cast<uint32_t>(ManagedHandleKind::Container) |
      static_cast<uint32_t>(ManagedHandleKind::ReferencePath);
  return mask != 0 && (mask & ~allKinds) == 0;
}

static LogicalResult
collectExpectedManagedTrace(Type type,
                            SmallVectorImpl<int64_t> &traceOffsets,
                            SmallVectorImpl<int32_t> &traceKinds) {
  SmallVector<ManagedHandleSlot, 2> slots;
  if (!getManagedHandleSlots(type, slots))
    return failure();
  for (const ManagedHandleSlot &slot : slots) {
    std::optional<uint32_t> kind = getManagedHandleTraceKind(slot);
    if (!kind || (slot.bitOffset & 7) != 0 ||
        slot.bitOffset / 8 > uint64_t{INT64_MAX})
      return failure();
    traceOffsets.push_back(static_cast<int64_t>(slot.bitOffset / 8));
    traceKinds.push_back(static_cast<int32_t>(*kind));
  }
  return success();
}

bool getManagedHandleOffsets(Type type,
                             llvm::SmallVectorImpl<uint64_t> &offsets) {
  SmallVector<ManagedHandleSlot, 2> slots;
  if (!getManagedHandleSlots(type, slots))
    return false;
  for (const ManagedHandleSlot &slot : slots)
    offsets.push_back(slot.bitOffset);
  return true;
}

bool isManagedHandleType(Type type) {
  return isa<ClassHandleType, StringType, DynamicArrayType, QueueType,
             MailboxType, SemaphoreType, AssocArrayType, ReferencePathType>(
      type);
}

LogicalResult SimManagedNullOp::verify() {
  if (!isManagedHandleType(getResult().getType()) ||
      isa<ClassHandleType>(getResult().getType()))
    return emitOpError("result must be a non-class managed handle type");
  return success();
}

LogicalResult SimManagedIsNullOp::verify() {
  if (!isManagedHandleType(getInput().getType()))
    return emitOpError("input must be a managed handle type");
  return success();
}

static Type getSequentialContainerElement(Type type) {
  if (auto array = dyn_cast<DynamicArrayType>(type))
    return array.getElementType();
  if (auto queue = dyn_cast<QueueType>(type))
    return queue.getElementType();
  return {};
}

Type getContainerElement(Type type) {
  if (Type element = getSequentialContainerElement(type))
    return element;
  if (auto array = dyn_cast<AssocArrayType>(type))
    return array.getElementType();
  return {};
}

LogicalResult SimContainerSizeOp::verify() {
  if (!getContainerElement(getContainer().getType()))
    return emitOpError(
        "container must be a dynamic array, queue, or associative array");
  return success();
}

LogicalResult SimRandomCycleNextOp::verify() {
  if (getWidth() == 0 || getWidth() > 32)
    return emitOpError("width must be between 1 and 32 bits");
  return success();
}

LogicalResult SimRandomSolveWideOp::verify() {
  auto assignmentType = dyn_cast<IntegerType>(getStart().getType());
  if (!assignmentType)
    return emitOpError("assignment type must be a signless integer");
  if (getMutableMask().getType() != assignmentType ||
      getAssignment().getType() != assignmentType)
    return emitOpError("start, mutable mask, and assignment types must match");
  for (Value capture : getCaptures())
    if (!isa<IntegerType>(capture.getType()))
      return emitOpError("captures must be signless integers");
  return success();
}

LogicalResult SimContainerCreateLikeOp::verify() {
  Type type = getResult().getType();
  if (!getSequentialContainerElement(type))
    return emitOpError("result must be a dynamic array or queue");
  if (getPreferred().getType() != type || getFallback().getType() != type)
    return emitOpError("source and result container types must match");
  return success();
}

LogicalResult SimContainerCreateOp::verify() {
  Type type = getResult().getType();
  Type element = getSequentialContainerElement(type);
  if (!element)
    return emitOpError("result must be a dynamic array or queue");
  if (getTypeId() == 0)
    return emitOpError("element type ID must be nonzero");
  if (getElementKind() < 1 || getElementKind() > 8)
    return emitOpError("element kind is outside the runtime ABI");
  if ((getElementFlags() & ~3u) != 0)
    return emitOpError("element flags contain an unknown runtime ABI bit");
  if (getValueSize() == 0 || getAlignment() == 0 ||
      !llvm::isPowerOf2_64(getAlignment()) ||
      getValueSize() % getAlignment() != 0)
    return emitOpError("element size and alignment are invalid");
  if (getContainerKind() != 1 && getContainerKind() != 2)
    return emitOpError("container kind is outside the runtime ABI");
  ArrayRef<int64_t> traceOffsets = getTraceOffsets();
  ArrayRef<int32_t> traceKinds = getTraceKinds();
  if (traceOffsets.size() != traceKinds.size())
    return emitOpError("trace offset and kind inventories must match");
  if (getElementKind() != 7 && !traceOffsets.empty())
    return emitOpError("only aggregate elements carry explicit trace slots");
  int64_t previousOffset = -1;
  for (auto [offset, kind] : llvm::zip_equal(traceOffsets, traceKinds)) {
    if (offset < 0 || static_cast<uint64_t>(offset) > getValueSize() ||
        sizeof(void *) > getValueSize() - static_cast<uint64_t>(offset))
      return emitOpError("trace slot is outside the element value plane");
    if (static_cast<uint64_t>(offset) % alignof(void *) != 0)
      return emitOpError("trace slot is not pointer aligned");
    if (!isValidManagedTraceKind(kind))
      return emitOpError("trace slot kind is outside the runtime ABI");
    if (offset <= previousOffset)
      return emitOpError("trace slot offsets must be strictly increasing");
    previousOffset = offset;
  }
  if (isa<DynamicArrayType>(type) && getContainerKind() != 1)
    return emitOpError("dynamic-array result requires dynamic-array metadata");
  if (isa<QueueType>(type) && getContainerKind() != 2)
    return emitOpError("queue result requires queue metadata");
  uint32_t expectedKind = 0;
  uint64_t expectedSize = 0;
  uint64_t expectedWidth = 0;
  bool fourState = false;
  SmallVector<int64_t, 2> expectedTraceOffsets;
  SmallVector<int32_t, 2> expectedTraceKinds;
  if (auto integer = dyn_cast<IntegerType>(element)) {
    expectedKind = 1;
    expectedSize = (integer.getWidth() + 7) / 8;
    expectedWidth = integer.getWidth();
  } else if (auto logic = dyn_cast<LogicType>(element)) {
    expectedKind = 2;
    expectedSize = (logic.getWidth() + 7) / 8;
    expectedWidth = logic.getWidth();
    fourState = true;
  } else if (auto real = dyn_cast<FloatType>(element)) {
    expectedKind = 3;
    expectedSize = real.getWidth() / 8;
    expectedWidth = real.getWidth();
  } else if (isa<ClassHandleType>(element)) {
    expectedKind = 4;
    expectedSize = sizeof(void *);
  } else if (isa<StringType>(element)) {
    expectedKind = 5;
    expectedSize = sizeof(void *);
  } else if (isa<DynamicArrayType, QueueType, MailboxType, SemaphoreType,
                 AssocArrayType>(element)) {
    expectedKind = 6;
    expectedSize = sizeof(void *);
  } else if (isa<EventType>(element)) {
    expectedKind = 8;
    expectedSize = sizeof(uint64_t);
  } else if (isa<ProcessType>(element)) {
    expectedKind = 1;
    expectedSize = sizeof(uint64_t);
    expectedWidth = 64;
  } else if (Type scalar = getPackedScalarType(element)) {
    std::optional<unsigned> width = getPackedWidth(element);
    if (!width || *width == 0)
      return emitOpError("packed element has no canonical width");
    fourState = isa<LogicType>(scalar);
    expectedKind = fourState ? 2 : 1;
    expectedSize = (*width + 7) / 8;
    expectedWidth = *width;
  } else if (isAggregateType(element)) {
    std::optional<uint64_t> width = getProvenanceSpan(element);
    if (!width || *width == 0)
      return emitOpError("aggregate element has no canonical layout");
    element.walk([&](LogicType) { fourState = true; });
    expectedKind = 7;
    expectedSize = (*width + 7) / 8;
    expectedWidth = expectedSize * 8;
    if (failed(collectExpectedManagedTrace(
            element, expectedTraceOffsets, expectedTraceKinds)))
      return emitOpError("aggregate element has no canonical trace layout");
  }
  if (expectedKind != 0 &&
      (getElementKind() != expectedKind || getValueSize() != expectedSize ||
       getBitWidth() != expectedWidth ||
       ((getElementFlags() & 1u) != 0) != fourState))
    return emitOpError(
        "element metadata does not match the result container element type");
  if (traceOffsets != ArrayRef<int64_t>(expectedTraceOffsets) ||
      traceKinds != ArrayRef<int32_t>(expectedTraceKinds))
    return emitOpError(
        "trace inventory does not match the result container element type");
  return success();
}

LogicalResult SimContainerCloneOp::verify() {
  if (!getContainerElement(getInput().getType()) ||
      getInput().getType() != getResult().getType())
    return emitOpError("input and result must be the same container type");
  return success();
}

LogicalResult SimContainerDeleteOp::verify() {
  if (!getContainerElement(getContainer().getType()))
    return emitOpError(
        "operand must be a dynamic array, queue, or associative array");
  return success();
}

LogicalResult SimQueueDeleteOp::verify() {
  if (!isa<QueueType>(getQueue().getType()))
    return emitOpError("queue operand must have queue type");
  return success();
}

LogicalResult SimQueueInsertOp::verify() {
  auto queue = dyn_cast<QueueType>(getQueue().getType());
  if (!queue)
    return emitOpError("queue operand must have queue type");
  if (queue.getElementType() != getValue().getType())
    return emitOpError("value type must match the queue element");
  return success();
}

LogicalResult SimMailboxTryPutOp::verify() {
  if (getValue().getType() != getMailbox().getType().getElementType())
    return emitOpError("message type must exactly match the mailbox element");
  return success();
}

LogicalResult SimMailboxCreateOp::verify() {
  Type element = getResult().getType().getElementType();
  if (getTypeId() == 0)
    return emitOpError("element type ID must be nonzero");
  if (getElementKind() < 1 || getElementKind() > 8)
    return emitOpError("element kind is outside the runtime ABI");
  if ((getElementFlags() & ~3u) != 0)
    return emitOpError("element flags contain an unknown runtime ABI bit");
  if (getValueSize() == 0 || getAlignment() == 0 ||
      !llvm::isPowerOf2_64(getAlignment()) ||
      getValueSize() % getAlignment() != 0)
    return emitOpError("element size and alignment are invalid");
  ArrayRef<int64_t> traceOffsets = getTraceOffsets();
  ArrayRef<int32_t> traceKinds = getTraceKinds();
  if (traceOffsets.size() != traceKinds.size())
    return emitOpError("trace offset and kind inventories must match");
  if (getElementKind() != 7 && !traceOffsets.empty())
    return emitOpError("only aggregate elements carry explicit trace slots");
  int64_t previousOffset = -1;
  for (auto [offset, kind] : llvm::zip_equal(traceOffsets, traceKinds)) {
    if (offset < 0 || static_cast<uint64_t>(offset) > getValueSize() ||
        sizeof(void *) > getValueSize() - static_cast<uint64_t>(offset))
      return emitOpError("trace slot is outside the element value plane");
    if (static_cast<uint64_t>(offset) % alignof(void *) != 0)
      return emitOpError("trace slot is not pointer aligned");
    if (!isValidManagedTraceKind(kind))
      return emitOpError("trace slot kind is outside the runtime ABI");
    if (offset <= previousOffset)
      return emitOpError("trace slot offsets must be strictly increasing");
    previousOffset = offset;
  }
  uint32_t expectedKind = 0;
  uint64_t expectedSize = 0;
  uint64_t expectedWidth = 0;
  bool fourState = false;
  SmallVector<int64_t, 2> expectedTraceOffsets;
  SmallVector<int32_t, 2> expectedTraceKinds;
  if (auto integer = dyn_cast<IntegerType>(element)) {
    expectedKind = 1;
    expectedSize = (integer.getWidth() + 7) / 8;
    expectedWidth = integer.getWidth();
  } else if (auto logic = dyn_cast<LogicType>(element)) {
    expectedKind = 2;
    expectedSize = (logic.getWidth() + 7) / 8;
    expectedWidth = logic.getWidth();
    fourState = true;
  } else if (auto real = dyn_cast<FloatType>(element)) {
    expectedKind = 3;
    expectedSize = real.getWidth() / 8;
    expectedWidth = real.getWidth();
  } else if (isa<ClassHandleType>(element)) {
    expectedKind = 4;
    expectedSize = sizeof(void *);
  } else if (isa<StringType>(element)) {
    expectedKind = 5;
    expectedSize = sizeof(void *);
  } else if (isa<DynamicArrayType, QueueType, MailboxType, SemaphoreType,
                 AssocArrayType>(element)) {
    expectedKind = 6;
    expectedSize = sizeof(void *);
  } else if (isa<EventType>(element)) {
    expectedKind = 8;
    expectedSize = sizeof(uint64_t);
  } else if (isa<ProcessType>(element)) {
    expectedKind = 1;
    expectedSize = sizeof(uint64_t);
    expectedWidth = 64;
  } else if (Type scalar = getPackedScalarType(element)) {
    std::optional<unsigned> width = getPackedWidth(element);
    if (!width || *width == 0)
      return emitOpError("packed element has no canonical width");
    fourState = isa<LogicType>(scalar);
    expectedKind = fourState ? 2 : 1;
    expectedSize = (*width + 7) / 8;
    expectedWidth = *width;
  } else if (isAggregateType(element)) {
    std::optional<uint64_t> width = getProvenanceSpan(element);
    if (!width || *width == 0)
      return emitOpError("aggregate element has no canonical layout");
    element.walk([&](LogicType) { fourState = true; });
    expectedKind = 7;
    expectedSize = (*width + 7) / 8;
    expectedWidth = expectedSize * 8;
    if (failed(collectExpectedManagedTrace(
            element, expectedTraceOffsets, expectedTraceKinds)))
      return emitOpError("aggregate element has no canonical trace layout");
  }
  if (expectedKind == 0 || getElementKind() != expectedKind ||
      getValueSize() != expectedSize || getBitWidth() != expectedWidth ||
      ((getElementFlags() & 1u) != 0) != fourState)
    return emitOpError(
        "element metadata does not match the mailbox element type");
  if (traceOffsets != ArrayRef<int64_t>(expectedTraceOffsets) ||
      traceKinds != ArrayRef<int32_t>(expectedTraceKinds))
    return emitOpError(
        "trace inventory does not match the mailbox element type");
  return success();
}

static LogicalResult verifyMailboxRead(Operation *operation,
                                       MailboxType mailbox, Type valueType) {
  if (valueType != mailbox.getElementType())
    return operation->emitOpError(
        "message result must exactly match the mailbox element");
  return success();
}

LogicalResult SimMailboxTryPeekOp::verify() {
  return verifyMailboxRead(*this, getMailbox().getType(), getValue().getType());
}

LogicalResult SimMailboxTryGetOp::verify() {
  return verifyMailboxRead(*this, getMailbox().getType(), getValue().getType());
}

LogicalResult SimContainerReadOp::verify() {
  Type element = getSequentialContainerElement(getContainer().getType());
  if (!element)
    return emitOpError("container must be a dynamic array or queue");
  if (element != getResult().getType())
    return emitOpError("result type must match the container element");
  return success();
}

LogicalResult SimContainerWriteOp::verify() {
  Type element = getSequentialContainerElement(getContainer().getType());
  if (!element)
    return emitOpError("container must be a dynamic array or queue");
  if (element != getValue().getType())
    return emitOpError("value type must match the container element");
  return success();
}

LogicalResult verifyAssocKey(Operation *op, AssocArrayType array,
                                    Type key) {
  if (array.getWildcardIndex())
    return op->emitOpError("wildcard associative arrays are not executable");
  if (array.getKeyType() != key)
    return op->emitOpError("key type must match the associative array key");
  return success();
}

LogicalResult SimAssocCreateOp::verify() {
  AssocArrayType array = getResult().getType();
  if (array.getWildcardIndex())
    return emitOpError("wildcard associative arrays are not executable");
  if (getTypeId() == 0 || getElementKind() < 1 || getElementKind() > 8)
    return emitOpError("element descriptor is outside the runtime ABI");
  if ((getElementFlags() & ~3u) != 0 || getValueSize() == 0 ||
      getAlignment() == 0 || !llvm::isPowerOf2_64(getAlignment()) ||
      getValueSize() % getAlignment() != 0)
    return emitOpError("element descriptor has an invalid layout");
  if (getTraceOffsets().size() != getTraceKinds().size())
    return emitOpError("trace offset and kind inventories must match");
  int64_t previousOffset = -1;
  for (auto [offset, kind] :
       llvm::zip_equal(getTraceOffsets(), getTraceKinds())) {
    if (offset < 0 || static_cast<uint64_t>(offset) > getValueSize() ||
        sizeof(void *) > getValueSize() - static_cast<uint64_t>(offset))
      return emitOpError("trace slot is outside the element value plane");
    if (static_cast<uint64_t>(offset) % alignof(void *) != 0)
      return emitOpError("trace slot is not pointer aligned");
    if (!isValidManagedTraceKind(kind))
      return emitOpError("trace slot kind is outside the runtime ABI");
    if (offset <= previousOffset)
      return emitOpError("trace slot offsets must be strictly increasing");
    previousOffset = offset;
  }
  Type element = array.getElementType();
  uint32_t expectedKind = 0;
  uint64_t expectedSize = 0;
  uint64_t expectedWidth = 0;
  bool fourState = false;
  SmallVector<int64_t, 2> expectedTraceOffsets;
  SmallVector<int32_t, 2> expectedTraceKinds;
  if (auto integer = dyn_cast<IntegerType>(element)) {
    expectedKind = 1;
    expectedSize = (integer.getWidth() + 7) / 8;
    expectedWidth = integer.getWidth();
  } else if (auto logic = dyn_cast<LogicType>(element)) {
    expectedKind = 2;
    expectedSize = (logic.getWidth() + 7) / 8;
    expectedWidth = logic.getWidth();
    fourState = true;
  } else if (auto real = dyn_cast<FloatType>(element)) {
    expectedKind = 3;
    expectedSize = real.getWidth() / 8;
    expectedWidth = real.getWidth();
  } else if (isa<ClassHandleType>(element)) {
    expectedKind = 4;
    expectedSize = sizeof(void *);
  } else if (isa<StringType>(element)) {
    expectedKind = 5;
    expectedSize = sizeof(void *);
  } else if (isa<DynamicArrayType, QueueType, MailboxType, SemaphoreType,
                 AssocArrayType>(element)) {
    expectedKind = 6;
    expectedSize = sizeof(void *);
  } else if (isa<EventType>(element)) {
    expectedKind = 8;
    expectedSize = sizeof(uint64_t);
  } else if (Type scalar = getPackedScalarType(element)) {
    std::optional<unsigned> width = getPackedWidth(element);
    if (!width || *width == 0)
      return emitOpError("packed element has no canonical width");
    fourState = isa<LogicType>(scalar);
    expectedKind = fourState ? 2 : 1;
    expectedSize = (*width + 7) / 8;
    expectedWidth = *width;
  } else if (isAggregateType(element)) {
    std::optional<uint64_t> width = getProvenanceSpan(element);
    if (!width || *width == 0)
      return emitOpError("aggregate element has no canonical layout");
    element.walk([&](LogicType) { fourState = true; });
    expectedKind = 7;
    expectedSize = (*width + 7) / 8;
    expectedWidth = expectedSize * 8;
    if (failed(collectExpectedManagedTrace(
            element, expectedTraceOffsets, expectedTraceKinds)))
      return emitOpError("aggregate element has no canonical trace layout");
  }
  if (expectedKind != 0 &&
      (getElementKind() != expectedKind || getValueSize() != expectedSize ||
       getBitWidth() != expectedWidth ||
       ((getElementFlags() & 1u) != 0) != fourState))
    return emitOpError(
        "element metadata does not match the associative element type");
  if (getElementKind() != 7 && !getTraceOffsets().empty())
    return emitOpError("only aggregate elements carry explicit trace slots");
  if (getTraceOffsets() != ArrayRef<int64_t>(expectedTraceOffsets) ||
      getTraceKinds() != ArrayRef<int32_t>(expectedTraceKinds))
    return emitOpError(
        "trace inventory does not match the associative element type");
  Type key = array.getKeyType();
  if (isa<StringType>(key)) {
    if (getKeyKind() != 3 || getKeyWidth() != 0)
      return emitOpError("string key metadata is inconsistent");
  } else if (isa<ClassHandleType>(key)) {
    if (getKeyKind() != 4 || getKeyWidth() != 0)
      return emitOpError("class key metadata is inconsistent");
  } else if (isa<ProcessType>(key)) {
    if (getKeyKind() != 5 || getKeyWidth() != 0)
      return emitOpError("process key metadata is inconsistent");
  } else {
    std::optional<unsigned> width = getPackedWidth(key);
    if (!width || *width == 0 ||
        (getKeyKind() != 1 && getKeyKind() != 2) || getKeyWidth() != *width)
      return emitOpError("integral key metadata is inconsistent");
  }
  return success();
}

LogicalResult SimAssocReadOp::verify() {
  AssocArrayType array = getArray().getType();
  if (failed(verifyAssocKey(getOperation(), array, getKey().getType())))
    return failure();
  if (getResult().getType() != array.getElementType())
    return emitOpError("result type must match the associative element");
  return success();
}

LogicalResult SimAssocWriteOp::verify() {
  AssocArrayType array = getArray().getType();
  if (failed(verifyAssocKey(getOperation(), array, getKey().getType())))
    return failure();
  if (getValue().getType() != array.getElementType())
    return emitOpError("value type must match the associative element");
  return success();
}

LogicalResult SimAssocExistsOp::verify() {
  return verifyAssocKey(getOperation(), getArray().getType(),
                        getKey().getType());
}

LogicalResult SimAssocDeleteOp::verify() {
  return verifyAssocKey(getOperation(), getArray().getType(),
                        getKey().getType());
}

LogicalResult SimAssocSetDefaultOp::verify() {
  if (getValue().getType() != getArray().getType().getElementType())
    return emitOpError("default type must match the associative element");
  return success();
}

LogicalResult SimAssocTraverseOp::verify() {
  AssocArrayType array = getArray().getType();
  if (failed(verifyAssocKey(getOperation(), array, getKey().getType())))
    return failure();
  if (getResultKey().getType() != array.getKeyType())
    return emitOpError("result key type must match the associative key");
  int32_t direction = static_cast<int32_t>(getDirection());
  if (direction != -1 && direction != 1)
    return emitOpError("direction must be -1 or 1");
  return success();
}

LogicalResult SimStringFromPackedOp::verify() {
  Type scalar = getPackedScalarType(getInput().getType());
  if (!scalar || !getPackedWidth(scalar))
    return emitOpError("input must be a fixed packed value");
  return success();
}

LogicalResult SimStringConcatOp::verify() {
  if (getInputs().size() > std::numeric_limits<uint32_t>::max())
    return emitOpError("input count exceeds the managed string ABI");
  return success();
}

static LogicalResult verifyStringRadix(Operation *operation, uint32_t radix) {
  if (radix != 2 && radix != 8 && radix != 10 && radix != 16)
    return operation->emitOpError("radix must be 2, 8, 10, or 16");
  return success();
}

LogicalResult SimStringParseIntegerOp::verify() {
  return verifyStringRadix(getOperation(), getRadix());
}

LogicalResult SimStringFormatIntegerOp::verify() {
  return verifyStringRadix(getOperation(), getRadix());
}


} // namespace obelisk::sim
