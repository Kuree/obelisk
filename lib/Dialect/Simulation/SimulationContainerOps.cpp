//===- SimulationContainerOps.cpp - Container, string, and managed op verifiers
//===//
//
// Verifiers for managed handles, dynamic arrays, queues, associative arrays,
// and string operations, plus the aggregate field helpers they share.
//
//===----------------------------------------------------------------------===//

#include "SimulationVerifiers.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/InliningUtils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <array>
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

// The bits one element occupies inside the aggregate that holds it. IEEE
// 1800-2017 7.3.2 gives a tagged union a tag alongside its member value, and
// the native layout stores that tag above the member payload, so a nested
// tagged union brings both into its parent. getProvenanceSpan reports only the
// payload -- that is what locates the tag within the union itself -- so laying
// a member out by it alone would let the parent place its next member, or its
// own tag, over the nested tag.
static std::optional<uint64_t> getElementStorageSpan(Type type) {
  std::optional<uint64_t> span = getProvenanceSpan(type);
  auto unionType = dyn_cast<UnpackedUnionType>(type);
  if (!span || !unionType || !unionType.getIsTagged())
    return span;
  uint64_t tagBits = llvm::Log2_64_Ceil(
      static_cast<uint64_t>(getAggregateNumElements(type)) + 1);
  if (tagBits > std::numeric_limits<uint64_t>::max() - *span)
    return std::nullopt;
  return *span + tagBits;
}

static StorageLayoutAttr getStorageLayout(Type type) {
  if (auto array = dyn_cast<UnpackedArrayType>(type))
    return dyn_cast_or_null<StorageLayoutAttr>(array.getLayout());
  if (auto record = dyn_cast<UnpackedStructType>(type))
    return dyn_cast_or_null<StorageLayoutAttr>(record.getLayout());
  return {};
}

std::optional<uint64_t> getProvenanceSpan(Type type) {
  if (auto reference = dyn_cast<RefType>(type))
    return getProvenanceSpan(reference.getElementType());
  if (auto net = dyn_cast<NetType>(type))
    return getProvenanceSpan(net.getElementType());
  if (auto driver = dyn_cast<DriverType>(type))
    return getProvenanceSpan(driver.getElementType());
  if (auto layout = getStorageLayout(type))
    return layout.getSize() * 8;
  // Events and covergroups store runtime identities. Aggregate geometry must
  // reserve a whole handle for each element, including covergroup arrays
  // whose elements are independently constructed (IEEE 1800-2023 19.3).
  if (isa<EventType, CovergroupHandleType>(type))
    return uint64_t{simulationHandleBitWidth};
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
    std::optional<uint64_t> element = getElementStorageSpan(elementType);
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
      std::optional<uint64_t> child = getElementStorageSpan(childType);
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
      std::optional<uint64_t> child = getElementStorageSpan(childType);
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
  if (auto layout = getStorageLayout(type))
    return layout.getAlignment() * 8;
  if (type.isF32())
    return uint64_t{32};
  if (type.isF64())
    return uint64_t{64};
  if (isManagedHandleType(type) || isa<CovergroupHandleType>(type))
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
  std::optional<uint64_t> span = getElementStorageSpan(element);
  if (!element || !span)
    return std::nullopt;
  if (auto layout = getStorageLayout(type)) {
    uint64_t offset = isa<UnpackedArrayType>(type) ? index * layout.getStride()
                                                   : layout.getOffsets()[index];
    return std::pair<uint64_t, uint64_t>{offset * 8, *span};
  }
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
      std::optional<uint64_t> previousSpan =
          getElementStorageSpan(previousType);
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

std::optional<uint64_t> getArrayElementStorageStride(Type type) {
  if (!isa<PackedArrayType, UnpackedArrayType>(type))
    return std::nullopt;
  uint64_t count = getAggregateNumElements(type);
  auto span = getProvenanceSpan(type);
  if (!count || !span || *span % count)
    return std::nullopt;
  return *span / count;
}

static std::optional<SmallVector<uint64_t>>
getFixedBitStreamPlanImpl(Type type, bool importing, bool provenanceLayout) {
  if (!isa<UnpackedArrayType, UnpackedStructType>(type))
    return std::nullopt;

  constexpr unsigned maximumDepth =
      OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAX_DEPTH;
  constexpr uint64_t maximumRecords =
      OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAX_RECORDS;
  SmallVector<uint64_t> records;
  std::function<std::optional<uint64_t>(Type, uint64_t, unsigned)> append =
      [&](Type current, uint64_t offset,
          unsigned depth) -> std::optional<uint64_t> {
    if (depth > maximumDepth ||
        records.size() / OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_RECORD_WORDS >=
            maximumRecords)
      return std::nullopt;
    if (Type scalar = getPackedScalarType(current)) {
      std::optional<unsigned> width = getPackedWidth(scalar);
      if (!width || *width == 0)
        return std::nullopt;
      uint64_t opcode = importing && isa<LogicType>(scalar)
                            ? OBELISK_RT_AGGREGATE_BITSTREAM_COPY_LOGIC
                            : OBELISK_RT_AGGREGATE_BITSTREAM_COPY;
      records.append({opcode, offset, *width, 0, 0, *width});
      return *width;
    }
    // Managed DPI leaves have stable 64-bit provenance storage even though
    // they are not language-level packed values. Aggregate import/export uses
    // this plan only as an internal transport; the owning aggregate remains a
    // precise GC root for the duration of the transfer.
    if (provenanceLayout && !isAggregateType(current)) {
      std::optional<uint64_t> width = getProvenanceSpan(current);
      if (!width || *width == 0)
        return std::nullopt;
      records.append({uint64_t{OBELISK_RT_AGGREGATE_BITSTREAM_COPY}, offset,
                      *width, 0, 0, *width});
      return *width;
    }
    if (auto array = dyn_cast<UnpackedArrayType>(current)) {
      uint64_t count = getAggregateNumElements(array);
      Type element = count ? getAggregateElementType(array, 0) : Type{};
      if (!count || !element)
        return std::nullopt;
      auto first = getAggregateProvenanceSubelement(array, 0);
      auto stride = getArrayElementStorageStride(array);
      if (!first || !stride)
        return std::nullopt;
      size_t record = records.size();
      records.append({uint64_t{OBELISK_RT_AGGREGATE_BITSTREAM_REPEAT},
                      offset + first->first, count, *stride, first->second, 0});
      size_t bodyStart = records.size();
      std::optional<uint64_t> childWidth = append(element, 0, depth + 1);
      if (!childWidth)
        return std::nullopt;
      uint64_t bodyRecords = (records.size() - bodyStart) /
                             OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_RECORD_WORDS;
      if (bodyRecords == 0 || bodyRecords > UINT32_MAX ||
          *childWidth > std::numeric_limits<uint64_t>::max() / count)
        return std::nullopt;
      records[record] |= bodyRecords << 32;
      records[record + 5] = *childWidth;
      return *childWidth * count;
    }
    auto structure = dyn_cast<UnpackedStructType>(current);
    if (!structure || getAggregateNumElements(structure) == 0)
      return std::nullopt;
    uint64_t total = 0;
    for (unsigned ordinal = 0, end = getAggregateNumElements(structure);
         ordinal != end; ++ordinal) {
      auto provenance = getAggregateProvenanceSubelement(structure, ordinal);
      if (!provenance)
        return std::nullopt;
      uint64_t childOffset = provenance->first;
      if (childOffset > UINT64_MAX - offset)
        return std::nullopt;
      std::optional<uint64_t> child =
          append(getAggregateElementType(structure, ordinal),
                 offset + childOffset, depth + 1);
      if (!child || *child > std::numeric_limits<uint64_t>::max() - total)
        return std::nullopt;
      total += *child;
    }
    return total;
  };
  std::optional<uint64_t> outputWidth = append(type, 0, 0);
  if (!outputWidth)
    return std::nullopt;
  std::optional<uint64_t> sourceSpan = getProvenanceSpan(type);
  if (!sourceSpan || records.empty() ||
      records.size() % OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_RECORD_WORDS != 0)
    return std::nullopt;
  uint64_t recordCount =
      records.size() / OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_RECORD_WORDS;
  SmallVector<uint64_t> plan{
      uint64_t{OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAGIC} |
          (uint64_t{OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_VERSION} << 32),
      recordCount, *sourceSpan, *outputWidth};
  llvm::append_range(plan, records);
  return plan;
}

std::optional<SmallVector<uint64_t>> getFixedBitStreamPlan(Type type) {
  return getFixedBitStreamPlanImpl(type, false, false);
}

std::optional<SmallVector<uint64_t>> getFixedBitStreamImportPlan(Type type) {
  return getFixedBitStreamPlanImpl(type, true, false);
}

std::optional<SmallVector<uint64_t>> getFixedArrayPatternPlan(Type type) {
  SmallVector<std::pair<uint64_t, uint64_t>, 4> dimensions;
  Type leaf = type;
  while (auto array = dyn_cast<UnpackedArrayType>(leaf)) {
    uint64_t count = getAggregateNumElements(array);
    if (count == 0)
      return std::nullopt;
    Type child = array.getElementType();
    auto stride = getArrayElementStorageStride(array);
    if (!stride)
      return std::nullopt;
    uint64_t strideBits = *stride;
    if ((strideBits & 7) != 0)
      return std::nullopt;
    dimensions.emplace_back(count, strideBits / 8);
    leaf = child;
  }
  if (dimensions.empty())
    return SmallVector<uint64_t>{};

  uint32_t kind = 0;
  uint32_t flags = 0;
  uint64_t valueSize = 0;
  uint64_t bitWidth = 0;
  if (auto integer = dyn_cast<IntegerType>(leaf)) {
    kind = OBELISK_RT_ELEMENT_BITS;
    flags = integer.isSigned() ? OBELISK_RT_ELEMENT_SIGNED : 0;
    valueSize = (integer.getWidth() + 7) / 8;
    bitWidth = integer.getWidth();
  } else if (auto logic = dyn_cast<LogicType>(leaf)) {
    kind = OBELISK_RT_ELEMENT_LOGIC;
    flags = OBELISK_RT_ELEMENT_FOUR_STATE;
    valueSize = (logic.getWidth() + 7) / 8;
    bitWidth = logic.getWidth();
  } else if (Type scalar = getPackedScalarType(leaf)) {
    std::optional<unsigned> width = getPackedWidth(leaf);
    if (!width || *width == 0)
      return std::nullopt;
    bool fourState = isa<LogicType>(scalar);
    kind = fourState ? OBELISK_RT_ELEMENT_LOGIC : OBELISK_RT_ELEMENT_BITS;
    flags = fourState ? OBELISK_RT_ELEMENT_FOUR_STATE : 0;
    valueSize = (*width + 7) / 8;
    bitWidth = *width;
  } else {
    // Other aggregate leaves need richer per-leaf metadata. Keep their
    // established representation until such a complete plan is available.
    return SmallVector<uint64_t>{};
  }

  SmallVector<uint64_t> plan{OBELISK_RT_CONTAINER_PATTERN_PLAN_VERSION,
                             dimensions.size(),
                             kind,
                             flags,
                             valueSize,
                             bitWidth};
  for (auto [count, stride] : dimensions) {
    plan.push_back(count);
    plan.push_back(stride);
  }
  return plan;
}

std::optional<SmallVector<uint64_t>> getDPIAggregateBitStreamPlan(Type type) {
  return getFixedBitStreamPlanImpl(type, false, true);
}

std::optional<SmallVector<uint64_t>>
getDPIAggregateBitStreamImportPlan(Type type) {
  return getFixedBitStreamPlanImpl(type, true, true);
}

uint64_t getClassBitStreamGroupID(ClassHandleType type, bool allowHiddenRoot) {
  std::string spelling;
  llvm::raw_string_ostream stream(spelling);
  type.getClassName().print(stream);
  stream << (allowHiddenRoot ? "#this" : "#public");
  uint64_t id = obelisk_stable_hash(spelling.data(), spelling.size());
  return id == 0 ? 1 : id;
}

std::optional<SmallVector<uint64_t>>
getRecursiveBitStreamPlan(Type type, ClassBitStreamGroupResolver objectGroup,
                          bool requireDynamic) {
  constexpr uint64_t maximumRecords =
      OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_MAX_RECORDS;
  constexpr uint64_t recordWords =
      OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_RECORD_WORDS;
  SmallVector<uint64_t> records;
  bool hasDynamic = false;
  bool hasObject = false;

  struct Layout {
    uint64_t span = 0;
    uint64_t alignment = 0;
    uint64_t stride = 0;
    std::optional<unsigned> packedWidth;
    SmallVector<uint64_t, 4> childOffsets;
  };
  DenseMap<Type, Layout> layouts;
  struct LayoutFrame {
    Type type;
    bool exit;
  };
  SmallVector<LayoutFrame, 16> layoutStack{{type, false}};
  uint64_t visitedTypes = 0;
  auto checkedAlign = [](uint64_t value,
                         uint64_t alignment) -> std::optional<uint64_t> {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
      return std::nullopt;
    return (value + alignment - 1) & ~(alignment - 1);
  };
  while (!layoutStack.empty()) {
    LayoutFrame frame = layoutStack.pop_back_val();
    if (layouts.count(frame.type))
      continue;
    if (!frame.exit) {
      if (++visitedTypes > maximumRecords)
        return std::nullopt;
      if (auto integer = dyn_cast<IntegerType>(frame.type)) {
        if (!integer.isSignless() || integer.getWidth() == 0)
          return std::nullopt;
        layouts[frame.type] = {
            integer.getWidth(), 1, 0, integer.getWidth(), {}};
        continue;
      }
      if (auto logic = dyn_cast<LogicType>(frame.type)) {
        if (logic.getWidth() == 0)
          return std::nullopt;
        layouts[frame.type] = {logic.getWidth(), 1, 0, logic.getWidth(), {}};
        continue;
      }
      if (isa<StringType, DynamicArrayType, QueueType, AssocArrayType,
              ClassHandleType>(frame.type)) {
        layouts[frame.type] = {
            managedHandleBitWidth, managedHandleBitWidth, 0, std::nullopt, {}};
        Type element;
        if (auto array = dyn_cast<DynamicArrayType>(frame.type))
          element = array.getElementType();
        else if (auto queue = dyn_cast<QueueType>(frame.type))
          element = queue.getElementType();
        else if (auto associative = dyn_cast<AssocArrayType>(frame.type))
          element = associative.getElementType();
        if (element) {
          layouts.erase(frame.type);
          layoutStack.push_back({frame.type, true});
          layoutStack.push_back({element, false});
        }
        continue;
      }
      unsigned count = getAggregateNumElements(frame.type);
      if (count == 0 || !isa<PackedArrayType, PackedStructType, PackedUnionType,
                             UnpackedArrayType, UnpackedStructType>(frame.type))
        return std::nullopt;
      layoutStack.push_back({frame.type, true});
      if (auto array = dyn_cast<PackedArrayType>(frame.type)) {
        layoutStack.push_back({array.getElementType(), false});
      } else if (auto array = dyn_cast<UnpackedArrayType>(frame.type)) {
        layoutStack.push_back({array.getElementType(), false});
      } else {
        for (unsigned ordinal = count; ordinal != 0; --ordinal)
          layoutStack.push_back(
              {getAggregateElementType(frame.type, ordinal - 1), false});
      }
      continue;
    }

    if (auto array = dyn_cast<DynamicArrayType>(frame.type)) {
      if (!layouts.count(array.getElementType()))
        return std::nullopt;
      layouts[frame.type] = {
          managedHandleBitWidth, managedHandleBitWidth, 0, std::nullopt, {}};
      continue;
    }
    if (auto queue = dyn_cast<QueueType>(frame.type)) {
      if (!layouts.count(queue.getElementType()))
        return std::nullopt;
      layouts[frame.type] = {
          managedHandleBitWidth, managedHandleBitWidth, 0, std::nullopt, {}};
      continue;
    }
    if (auto associative = dyn_cast<AssocArrayType>(frame.type)) {
      if (!layouts.count(associative.getElementType()))
        return std::nullopt;
      layouts[frame.type] = {
          managedHandleBitWidth, managedHandleBitWidth, 0, std::nullopt, {}};
      continue;
    }
    if (auto array = dyn_cast<PackedArrayType>(frame.type)) {
      auto found = layouts.find(array.getElementType());
      uint64_t count = getAggregateNumElements(array);
      if (found == layouts.end() || !found->second.packedWidth ||
          *found->second.packedWidth >
              std::numeric_limits<unsigned>::max() / count)
        return std::nullopt;
      unsigned width = *found->second.packedWidth * count;
      layouts[frame.type] = {width, 1, 0, width, {}};
      continue;
    }
    if (isa<PackedStructType, PackedUnionType>(frame.type)) {
      uint64_t width = 0;
      ArrayAttr fields = getAggregateFields(frame.type);
      for (Attribute attribute : fields) {
        auto field = dyn_cast<FieldAttr>(attribute);
        auto found = field ? layouts.find(field.getType()) : layouts.end();
        if (!field || found == layouts.end() || !found->second.packedWidth ||
            field.getPackedOffset() > std::numeric_limits<unsigned>::max() -
                                          *found->second.packedWidth)
          return std::nullopt;
        width = std::max<uint64_t>(width, field.getPackedOffset() +
                                              *found->second.packedWidth);
      }
      if (auto unionType = dyn_cast<PackedUnionType>(frame.type)) {
        if (unionType.getTagBits() >
            std::numeric_limits<unsigned>::max() - width)
          return std::nullopt;
        width += unionType.getTagBits();
      }
      if (width == 0 || width > std::numeric_limits<unsigned>::max())
        return std::nullopt;
      layouts[frame.type] = {width, 1, 0, static_cast<unsigned>(width), {}};
      continue;
    }
    if (auto array = dyn_cast<UnpackedArrayType>(frame.type)) {
      auto found = layouts.find(array.getElementType());
      uint64_t count = getAggregateNumElements(array);
      if (found == layouts.end())
        return std::nullopt;
      std::optional<uint64_t> stride =
          checkedAlign(found->second.span, found->second.alignment);
      if (!stride || *stride == 0 || *stride > UINT64_MAX / count)
        return std::nullopt;
      layouts[frame.type] = {
          *stride * count, found->second.alignment, *stride, std::nullopt, {0}};
      continue;
    }
    auto structure = cast<UnpackedStructType>(frame.type);
    Layout layout;
    layout.alignment = 1;
    unsigned count = getAggregateNumElements(structure);
    layout.childOffsets.reserve(count);
    for (unsigned ordinal = 0; ordinal != count; ++ordinal) {
      auto found = layouts.find(getAggregateElementType(structure, ordinal));
      if (found == layouts.end())
        return std::nullopt;
      std::optional<uint64_t> offset =
          checkedAlign(layout.span, found->second.alignment);
      if (!offset || found->second.span > UINT64_MAX - *offset)
        return std::nullopt;
      layout.childOffsets.push_back(*offset);
      layout.span = *offset + found->second.span;
      layout.alignment = std::max(layout.alignment, found->second.alignment);
    }
    std::optional<uint64_t> span = checkedAlign(layout.span, layout.alignment);
    if (!span)
      return std::nullopt;
    layout.span = *span;
    layouts[frame.type] = std::move(layout);
  }

  auto appendRecord = [&](ArrayRef<uint64_t> words) -> std::optional<size_t> {
    if (words.size() != recordWords ||
        records.size() / recordWords >= maximumRecords)
      return std::nullopt;
    size_t index = records.size();
    llvm::append_range(records, words);
    return index;
  };

  enum class EmitKind : uint8_t { Value, Patch, Structure };
  struct EmitFrame {
    EmitKind kind;
    Type type;
    uint64_t offset = 0;
    size_t record = 0;
    size_t bodyStart = 0;
    unsigned ordinal = 0;
    bool directRoot = false;
  };
  SmallVector<EmitFrame, 16> emitStack{
      {EmitKind::Value, type, 0, 0, 0, 0, true}};
  bool valid = true;
  while (valid && !emitStack.empty()) {
    EmitFrame frame = emitStack.pop_back_val();
    auto foundLayout = layouts.find(frame.type);
    if (foundLayout == layouts.end())
      return std::nullopt;
    const Layout &layout = foundLayout->second;
    if (frame.kind == EmitKind::Patch) {
      uint64_t bodyRecords = (records.size() - frame.bodyStart) / recordWords;
      if (bodyRecords == 0 || bodyRecords > UINT32_MAX)
        return std::nullopt;
      records[frame.record] |= bodyRecords << 32;
      continue;
    }
    if (frame.kind == EmitKind::Structure) {
      unsigned count = getAggregateNumElements(frame.type);
      if (frame.ordinal == count)
        continue;
      emitStack.push_back({EmitKind::Structure, frame.type, frame.offset, 0, 0,
                           frame.ordinal + 1, false});
      uint64_t childOffset = layout.childOffsets[frame.ordinal];
      if (childOffset > UINT64_MAX - frame.offset)
        return std::nullopt;
      emitStack.push_back({EmitKind::Value,
                           getAggregateElementType(frame.type, frame.ordinal),
                           frame.offset + childOffset, 0, 0, 0, false});
      continue;
    }
    if (layout.packedWidth) {
      valid = appendRecord({OBELISK_RT_RECURSIVE_BITSTREAM_COPY, frame.offset,
                            *layout.packedWidth, 0, 0, *layout.packedWidth})
                  .has_value();
      continue;
    }
    if (isa<StringType>(frame.type)) {
      hasDynamic = true;
      valid = appendRecord({OBELISK_RT_RECURSIVE_BITSTREAM_STRING, frame.offset,
                            0, 0, managedHandleBitWidth, 0})
                  .has_value();
      continue;
    }
    if (auto handle = dyn_cast<ClassHandleType>(frame.type)) {
      std::optional<uint64_t> group = objectGroup(handle, frame.directRoot);
      if (!group || *group == 0)
        return std::nullopt;
      valid = appendRecord({OBELISK_RT_RECURSIVE_BITSTREAM_OBJECT, frame.offset,
                            *group, 0, managedHandleBitWidth, 0})
                  .has_value();
      hasDynamic = true;
      hasObject = true;
      continue;
    }
    Type element;
    uint64_t containerKind = 0;
    if (auto array = dyn_cast<DynamicArrayType>(frame.type)) {
      element = array.getElementType();
      containerKind = OBELISK_RT_CONTAINER_DYNAMIC_ARRAY;
    } else if (auto queue = dyn_cast<QueueType>(frame.type)) {
      element = queue.getElementType();
      containerKind = OBELISK_RT_CONTAINER_QUEUE;
    } else if (auto associative = dyn_cast<AssocArrayType>(frame.type)) {
      element = associative.getElementType();
      containerKind = OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY;
    }
    if (element) {
      uint64_t childSpan = layouts.find(element)->second.span;
      if (isAggregateType(element)) {
        if (childSpan > UINT64_MAX - 7)
          return std::nullopt;
        childSpan = (childSpan + 7) & ~uint64_t{7};
      }
      std::optional<size_t> record =
          childSpan
              ? appendRecord({OBELISK_RT_RECURSIVE_BITSTREAM_CONTAINER,
                              frame.offset, containerKind, 0, childSpan, 0})
              : std::nullopt;
      if (!record)
        return std::nullopt;
      size_t bodyStart = records.size();
      emitStack.push_back({EmitKind::Patch, frame.type, 0, *record, bodyStart});
      emitStack.push_back({EmitKind::Value, element, 0, 0, 0, 0, false});
      hasDynamic = true;
      continue;
    }
    if (auto array = dyn_cast<UnpackedArrayType>(frame.type)) {
      Type child = array.getElementType();
      const Layout &childLayout = layouts.find(child)->second;
      std::optional<size_t> record = appendRecord(
          {OBELISK_RT_RECURSIVE_BITSTREAM_REPEAT, frame.offset,
           getAggregateNumElements(array), layout.stride, childLayout.span, 0});
      if (!record)
        return std::nullopt;
      size_t bodyStart = records.size();
      emitStack.push_back({EmitKind::Patch, frame.type, 0, *record, bodyStart});
      emitStack.push_back({EmitKind::Value, child, 0, 0, 0, 0, false});
      continue;
    }
    if (!isa<UnpackedStructType>(frame.type))
      return std::nullopt;
    emitStack.push_back(
        {EmitKind::Structure, frame.type, frame.offset, 0, 0, 0, false});
  }

  auto root = layouts.find(type);
  if (!valid || root == layouts.end() || root->second.span == 0 ||
      (requireDynamic && !hasDynamic) || records.empty() ||
      records.size() % recordWords != 0)
    return std::nullopt;
  uint64_t recordCount = records.size() / recordWords;
  SmallVector<uint64_t> plan{
      uint64_t{OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_MAGIC} |
          (uint64_t{hasObject
                        ? OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_OBJECT_VERSION
                        : OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_VERSION}
           << 32),
      recordCount, root->second.span, 0};
  llvm::append_range(plan, records);
  return plan;
}

std::optional<SmallVector<uint64_t>> getRecursiveBitStreamPlan(Type type) {
  return getRecursiveBitStreamPlan(
      type, [](ClassHandleType, bool) -> std::optional<uint64_t> {
        return std::nullopt;
      });
}

bool getManagedHandleSlots(Type type,
                           llvm::SmallVectorImpl<ManagedHandleSlot> &slots) {
  auto leafKind = [](Type leaf) -> std::optional<uint32_t> {
    if (isa<ClassHandleType>(leaf))
      return static_cast<uint32_t>(ManagedHandleKind::Class);
    if (isa<StringType>(leaf))
      return static_cast<uint32_t>(ManagedHandleKind::String);
    if (isa<MailboxType, SemaphoreType>(leaf))
      return static_cast<uint32_t>(ManagedHandleKind::Class) |
             static_cast<uint32_t>(ManagedHandleKind::Container);
    if (isa<DynamicArrayType, QueueType, BoxType, AssocArrayType>(leaf))
      return static_cast<uint32_t>(ManagedHandleKind::Container);
    if (isa<ReferencePathType>(leaf))
      return static_cast<uint32_t>(ManagedHandleKind::ReferencePath);
    return std::nullopt;
  };

  std::function<bool(Type, uint64_t, bool)> collect =
      [&](Type nestedType, uint64_t baseOffset, bool conditional) {
        if (std::optional<uint32_t> kind = leafKind(nestedType)) {
          slots.push_back(
              {baseOffset, *kind, conditional,
               isa<MailboxType>(nestedType)     ? ManagedBuiltinKind::Mailbox
               : isa<SemaphoreType>(nestedType) ? ManagedBuiltinKind::Semaphore
                                                : ManagedBuiltinKind::None});
          return true;
        }
        if (!isAggregateType(nestedType))
          return true;
        bool overlapping = isa<PackedUnionType>(nestedType);
        if (auto unpacked = dyn_cast<UnpackedUnionType>(nestedType))
          overlapping = !unpacked.getIsTagged();
        unsigned count = getAggregateNumElements(nestedType);
        bool prefixLayout =
            !getStorageLayout(nestedType) &&
            (isa<UnpackedStructType>(nestedType) ||
             (isa<UnpackedUnionType>(nestedType) && !overlapping));
        uint64_t nextOffset = 0;
        auto collectElement = [&](unsigned index) {
          Type element = getAggregateElementType(nestedType, index);
          std::optional<std::pair<uint64_t, uint64_t>> child;
          if (prefixLayout) {
            auto span = getElementStorageSpan(element);
            auto alignment = getProvenanceAlignment(element);
            if (!span || !alignment || !*alignment ||
                nextOffset > UINT64_MAX - (*alignment - 1))
              return false;
            uint64_t offset = llvm::alignTo(nextOffset, *alignment);
            if (*span > UINT64_MAX - offset)
              return false;
            nextOffset = offset + *span;
            child = std::pair<uint64_t, uint64_t>{offset, *span};
          } else {
            child = getAggregateProvenanceSubelement(nestedType, index);
          }
          if (!child ||
              child->first > std::numeric_limits<uint64_t>::max() - baseOffset)
            return false;
          return collect(element, baseOffset + child->first,
                         conditional || overlapping);
        };
        unsigned first = 0;
        if (count && isa<PackedArrayType, UnpackedArrayType>(nestedType)) {
          size_t before = slots.size();
          if (!collectElement(0))
            return false;
          // Every array element has the same type. If the first has no
          // managed roots, neither do the remaining elements: do not walk
          // millions of RAM words to rediscover an empty trace layout.
          // Check the other endpoint as well so the monotonic element offsets
          // (including nested offsets) retain their overflow validation.
          if (slots.size() == before)
            return count == 1 || collectElement(count - 1);
          first = 1;
        }
        for (unsigned index = first; index < count; ++index)
          if (!collectElement(index))
            return false;
        return true;
      };
  size_t originalSize = slots.size();
  if (!collect(type, 0, false)) {
    slots.resize(originalSize);
    return false;
  }
  // An overlapping union has no active-arm discriminator, so every managed
  // slot is a candidate root. For a four-state arm the candidate word is its
  // value plane; the unknown plane is deliberately irrelevant. Runtime
  // candidate tracing accepts only canonical inline strings or live managed
  // objects of one of the declared kinds, so arbitrary value/X/Z plane pairs
  // cannot be dereferenced as handles. A coincidental live-object identity is
  // retained conservatively, which is the same safe policy used for an
  // ordinary two-state arm whose bits happen to equal a live identity.
  auto ordered =
      llvm::MutableArrayRef<ManagedHandleSlot>(slots).drop_front(originalSize);
  if (!llvm::is_sorted(ordered, [](const auto &a, const auto &b) {
        return a.bitOffset < b.bitOffset;
      })) {
    // Fixed-width offsets need eight stable byte passes. Sorting only offsets
    // is enough: the fold below combines every kind and condition at that slot.
    llvm::SmallVector<ManagedHandleSlot> scratch(ordered.size());
    auto input = ordered;
    llvm::MutableArrayRef<ManagedHandleSlot> output(scratch);
    for (unsigned shift = 0; shift != 64; shift += 8) {
      std::array<size_t, 256> counts{};
      for (const auto &slot : input)
        ++counts[(slot.bitOffset >> shift) & 255];
      size_t offset = 0;
      for (size_t &count : counts) {
        size_t size = count;
        count = offset;
        offset += size;
      }
      for (const auto &slot : input)
        output[counts[(slot.bitOffset >> shift) & 255]++] = slot;
      std::swap(input, output);
    }
  }
  size_t output = originalSize;
  for (size_t index = originalSize; index != slots.size(); ++index) {
    ManagedHandleSlot slot = slots[index];
    if (output != originalSize &&
        slots[output - 1].bitOffset == slot.bitOffset) {
      slots[output - 1].kindMask |= slot.kindMask;
      slots[output - 1].conditional |= slot.conditional;
      if (slots[output - 1].builtinKind != slot.builtinKind)
        slots[output - 1].builtinKind = ManagedBuiltinKind::None;
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
  if (slot.builtinKind != ManagedBuiltinKind::None) {
    if (slot.kindMask != (static_cast<uint32_t>(ManagedHandleKind::Class) |
                          static_cast<uint32_t>(ManagedHandleKind::Container)))
      return std::nullopt;
    return slot.builtinKind == ManagedBuiltinKind::Mailbox ? 5 : 6;
  }
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
    return kind >= 1 && kind <= 6;
  uint32_t mask = kind & ~managedHandleCandidateFlag;
  constexpr uint32_t allKinds =
      static_cast<uint32_t>(ManagedHandleKind::Class) |
      static_cast<uint32_t>(ManagedHandleKind::String) |
      static_cast<uint32_t>(ManagedHandleKind::Container) |
      static_cast<uint32_t>(ManagedHandleKind::ReferencePath);
  return mask != 0 && (mask & ~allKinds) == 0;
}

static LogicalResult verifyManagedTraceInventory(Operation *operation,
                                                 ElementKind elementKind,
                                                 uint64_t valueSize,
                                                 ArrayRef<int64_t> traceOffsets,
                                                 ArrayRef<int32_t> traceKinds) {
  if (traceOffsets.size() != traceKinds.size())
    return operation->emitOpError(
        "trace offset and kind inventories must match");
  bool builtinHandle = elementKind == ElementKind::ContainerHandle &&
                       traceOffsets.size() == 1 && traceOffsets[0] == 0 &&
                       (traceKinds[0] == 5 || traceKinds[0] == 6);
  if (elementKind != ElementKind::Aggregate && !traceOffsets.empty() &&
      !builtinHandle)
    return operation->emitOpError(
        "explicit trace slots require an aggregate or a typed builtin handle");
  int64_t previousOffset = -1;
  for (auto [offset, kind] : llvm::zip_equal(traceOffsets, traceKinds)) {
    if (offset < 0 || static_cast<uint64_t>(offset) > valueSize ||
        managedHandleByteWidth > valueSize - static_cast<uint64_t>(offset))
      return operation->emitOpError(
          "trace slot is outside the element value plane");
    if (static_cast<uint64_t>(offset) % managedHandleByteAlignment != 0)
      return operation->emitOpError("trace slot is not managed-word aligned");
    if (!isValidManagedTraceKind(kind))
      return operation->emitOpError(
          "trace slot kind is outside the runtime ABI");
    if (offset <= previousOffset)
      return operation->emitOpError(
          "trace slot offsets must be strictly increasing");
    previousOffset = offset;
  }
  return success();
}

static LogicalResult
collectExpectedManagedTrace(Type type, SmallVectorImpl<int64_t> &traceOffsets,
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
             MailboxType, BoxType, SemaphoreType, AssocArrayType,
             ReferencePathType>(type);
}

bool isSimulationHandleType(Type type) {
  return isa<RefType, NetType, DriverType, EventType, ProcessType, ControlType,
             ObserverType, ManagedWatchType, CovergroupHandleType,
             VirtualInterfaceType, ChandleType>(type);
}

std::optional<ElementDescriptor> getElementDescriptor(Type type) {
  ElementDescriptor result{getStableStorageTypeID(type),
                           ElementKind::Bits,
                           ElementFlags::None,
                           0,
                           1,
                           0,
                           {},
                           {}};
  if (auto integer = dyn_cast<IntegerType>(type)) {
    result.valueSize = (integer.getWidth() + 7) / 8;
    result.bitWidth = integer.getWidth();
    return result;
  }
  if (auto logic = dyn_cast<LogicType>(type)) {
    result.kind = ElementKind::Logic;
    result.flags = ElementFlags::FourState;
    result.valueSize = (logic.getWidth() + 7) / 8;
    result.bitWidth = logic.getWidth();
    return result;
  }
  if (auto real = dyn_cast<FloatType>(type)) {
    result.kind = ElementKind::Real;
    result.valueSize = real.getWidth() / 8;
    result.bitWidth = real.getWidth();
    return result;
  }
  if (isa<ClassHandleType>(type)) {
    result.kind = ElementKind::ClassHandle;
    result.valueSize = managedHandleByteWidth;
    return result;
  }
  if (isa<StringType>(type)) {
    result.kind = ElementKind::String;
    result.valueSize = managedHandleByteWidth;
    return result;
  }
  if (isa<EventType>(type)) {
    result.kind = ElementKind::Event;
    result.valueSize = sizeof(uint64_t);
    return result;
  }
  // These handles are context-owned identities or opaque foreign pointers,
  // rather than managed-heap roots. In particular, covergroup instances stay
  // in the coverage model when a container drops its last handle (19.3, 19.8).
  if (isa<ProcessType, CovergroupHandleType, VirtualInterfaceType, ChandleType>(
          type)) {
    result.valueSize = simulationHandleBitWidth / 8;
    result.bitWidth = simulationHandleBitWidth;
    return result;
  }
  if (isa<DynamicArrayType, QueueType, MailboxType, BoxType, SemaphoreType,
          AssocArrayType>(type)) {
    result.kind = ElementKind::ContainerHandle;
    result.valueSize = managedHandleByteWidth;
    if (isa<MailboxType, SemaphoreType>(type) &&
        failed(collectExpectedManagedTrace(type, result.traceOffsets,
                                           result.traceKinds)))
      return std::nullopt;
    return result;
  }
  if (Type scalar = getPackedScalarType(type)) {
    std::optional<unsigned> width = getPackedWidth(type);
    if (!width || *width == 0)
      return std::nullopt;
    bool fourState = isa<LogicType>(scalar);
    result.kind = fourState ? ElementKind::Logic : ElementKind::Bits;
    result.flags = fourState ? ElementFlags::FourState : ElementFlags::None;
    result.valueSize = (*width + 7) / 8;
    result.bitWidth = *width;
    return result;
  }
  if (!isAggregateType(type))
    return std::nullopt;
  std::optional<uint64_t> width = getElementStorageSpan(type);
  if (!width || *width == 0 ||
      failed(collectExpectedManagedTrace(type, result.traceOffsets,
                                         result.traceKinds)))
    return std::nullopt;
  bool fourState = false;
  type.walk([&](LogicType) { fourState = true; });
  result.kind = ElementKind::Aggregate;
  result.flags = fourState ? ElementFlags::FourState : ElementFlags::None;
  result.valueSize = (*width + 7) / 8;
  result.bitWidth = result.valueSize * 8;
  return result;
}

std::optional<AssocKeyLayout> getAssocKeyLayout(AssocArrayType type) {
  Type key = type.getKeyType();
  if (type.getWildcardIndex())
    return isa<BoxType>(key)
               ? std::optional<AssocKeyLayout>({AssocKeyKind::Wildcard, 0})
               : std::nullopt;
  if (isa<StringType>(key))
    return AssocKeyLayout{AssocKeyKind::String, 0};
  if (isa<ClassHandleType>(key))
    return AssocKeyLayout{AssocKeyKind::Class, 0};
  if (isa<ProcessType>(key))
    return AssocKeyLayout{AssocKeyKind::Process, 0};
  if (std::optional<unsigned> width = getPackedWidth(key)) {
    if (*width == 0)
      return std::nullopt;
    return AssocKeyLayout{type.getSignedKey() ? AssocKeyKind::Signed
                                              : AssocKeyKind::Unsigned,
                          *width};
  }
  // IEEE 1800-2023 7.8.5: any other index type with an equality operator
  // indexes by value; a key travels as its element planes.
  std::optional<ElementDescriptor> descriptor = getElementDescriptor(key);
  if (!descriptor || !getAssocKeyPlan(key))
    return std::nullopt;
  return AssocKeyLayout{AssocKeyKind::Element, descriptor->valueSize * 8};
}

std::optional<SmallVector<uint64_t>> getAssocKeyPlan(Type type) {
  SmallVector<uint64_t> records;
  // Append one record; its body, when it has one, follows it.
  auto record = [&](uint32_t opcode, uint64_t offset, uint64_t operand0 = 0,
                    uint64_t operand1 = 0) {
    records.append({opcode, offset, operand0, operand1});
    return records.size() / OBELISK_RT_ASSOC_KEY_PLAN_RECORD_WORDS - 1;
  };
  auto closeBody = [&](uint64_t index) {
    uint64_t body =
        records.size() / OBELISK_RT_ASSOC_KEY_PLAN_RECORD_WORDS - index - 1;
    records[index * OBELISK_RT_ASSOC_KEY_PLAN_RECORD_WORDS] |= body << 32;
  };
  std::function<bool(Type, uint64_t, unsigned)> append =
      [&](Type current, uint64_t offset, unsigned depth) -> bool {
    if (depth > OBELISK_RT_ASSOC_KEY_PLAN_MAX_DEPTH)
      return false;
    if (Type scalar = getPackedScalarType(current)) {
      std::optional<unsigned> width = getPackedWidth(current);
      if (!width || *width == 0)
        return false;
      record(OBELISK_RT_ASSOC_KEY_PLAN_BITS, offset, *width,
             isa<LogicType>(scalar) ? 1 : 0);
      return true;
    }
    if (isa<StringType>(current)) {
      record(OBELISK_RT_ASSOC_KEY_PLAN_STRING, offset);
      return true;
    }
    // Class handles and the built-in mailbox and semaphore classes compare by
    // identity.
    if (isa<ClassHandleType, MailboxType, SemaphoreType>(current)) {
      record(OBELISK_RT_ASSOC_KEY_PLAN_OBJECT, offset);
      return true;
    }
    if (isa<EventType, ProcessType, CovergroupHandleType, VirtualInterfaceType,
            ChandleType>(current)) {
      record(OBELISK_RT_ASSOC_KEY_PLAN_BITS, offset, simulationHandleBitWidth);
      return true;
    }
    if (isa<DynamicArrayType, QueueType, AssocArrayType>(current)) {
      bool associative = isa<AssocArrayType>(current);
      Type element = getContainerElement(current);
      std::optional<ElementDescriptor> descriptor =
          element ? getElementDescriptor(element) : std::nullopt;
      if (!descriptor)
        return false;
      uint64_t index =
          record(associative ? OBELISK_RT_ASSOC_KEY_PLAN_ASSOCIATIVE
                             : OBELISK_RT_ASSOC_KEY_PLAN_SEQUENCE,
                 offset, descriptor->valueSize);
      if (!append(element, 0, depth + 1))
        return false;
      closeBody(index);
      return true;
    }
    if (auto array = dyn_cast<UnpackedArrayType>(current)) {
      uint64_t count = getAggregateNumElements(array);
      Type element = count ? getAggregateElementType(array, 0) : Type{};
      auto first = getAggregateProvenanceSubelement(array, 0);
      std::optional<uint64_t> stride = getArrayElementStorageStride(array);
      if (!element || !first || !stride || *stride == 0)
        return false;
      uint64_t index = record(OBELISK_RT_ASSOC_KEY_PLAN_REPEAT,
                              offset + first->first, count, *stride);
      if (!append(element, 0, depth + 1))
        return false;
      closeBody(index);
      return true;
    }
    if (!isa<UnpackedStructType, UnpackedUnionType>(current))
      return false;
    auto unionType = dyn_cast<UnpackedUnionType>(current);
    bool tagged = unionType && unionType.getIsTagged();
    // A tagged union compares its tag and then its active member only
    // (IEEE 1800-2023 7.3.2); its tag above the payload is one plus the
    // member ordinal.
    std::optional<uint64_t> payload =
        tagged ? getProvenanceSpan(current) : std::nullopt;
    if (tagged && !payload)
      return false;
    unsigned count = getAggregateNumElements(current);
    uint64_t taggedIndex =
        tagged ? record(OBELISK_RT_ASSOC_KEY_PLAN_TAGGED, offset, *payload,
                        llvm::Log2_64_Ceil(uint64_t{count} + 1))
               : 0;
    // A structure compares every member. So does an untagged union, whose
    // members all overlap the same storage, as its equality operator does.
    for (unsigned ordinal = 0; ordinal != count; ++ordinal) {
      Type member = getAggregateElementType(current, ordinal);
      auto subelement = getAggregateProvenanceSubelement(current, ordinal);
      if (!member || !subelement)
        return false;
      if (!tagged) {
        if (!append(member, offset + subelement->first, depth + 1))
          return false;
        continue;
      }
      uint64_t alternative = record(OBELISK_RT_ASSOC_KEY_PLAN_ALTERNATIVE,
                                    subelement->first, ordinal + 1);
      // A void member has no value to compare.
      if (subelement->second != 0 && !append(member, 0, depth + 1))
        return false;
      closeBody(alternative);
    }
    if (tagged)
      closeBody(taggedIndex);
    return true;
  };
  if (!append(type, 0, 0) || records.empty())
    return std::nullopt;
  uint64_t count = records.size() / OBELISK_RT_ASSOC_KEY_PLAN_RECORD_WORDS;
  if (count > OBELISK_RT_ASSOC_KEY_PLAN_MAX_RECORDS)
    return std::nullopt;
  SmallVector<uint64_t> plan{
      uint64_t{OBELISK_RT_ASSOC_KEY_PLAN_MAGIC} |
          (uint64_t{OBELISK_RT_ASSOC_KEY_PLAN_VERSION} << 32),
      count};
  llvm::append_range(plan, records);
  return plan;
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

LogicalResult SimRandomCycleSampleOp::verify() {
  if (getMutableMask().getType() != getStart().getType() ||
      getAssignment().getType() != getStart().getType())
    return emitOpError("start, mutable mask, and assignment types must match");
  return success();
}

static LogicalResult verifyRealRank(Operation *op, Type real, Type rank) {
  auto floating = dyn_cast<FloatType>(real);
  auto integer = dyn_cast<IntegerType>(rank);
  if (!floating || (!floating.isF32() && !floating.isF64()))
    return op->emitOpError("requires IEEE binary32 or binary64");
  unsigned width = floating.isF32() ? OBELISK_RT_REAL32_RANK_BITS
                                    : OBELISK_RT_REAL64_RANK_BITS;
  if (!integer || !integer.isSignless() || integer.getWidth() != width)
    return op->emitOpError()
           << "requires a " << width << "-bit signed numeric coordinate";
  return success();
}

LogicalResult SimRandomRealToRankOp::verify() {
  return verifyRealRank(getOperation(), getValue().getType(),
                        getRank().getType());
}

LogicalResult SimRandomRealFromRankOp::verify() {
  return verifyRealRank(getOperation(), getValue().getType(),
                        getRank().getType());
}

LogicalResult SimContainerCreateLikeOp::verify() {
  Type type = getResult().getType();
  if (!getSequentialContainerElement(type))
    return emitOpError("result must be a dynamic array or queue");
  if (getPreferred().getType() != type || getFallback().getType() != type)
    return emitOpError("source and result container types must match");
  return success();
}

/// Check an op's element descriptor attributes against its element type, which
/// diagnostics call the `owner` element type. A type without a canonical
/// element layout carries no trace inventory and is otherwise accepted unless
/// `requireLayout`.
static LogicalResult verifyElementMetadata(
    Operation *operation, Type element, ElementKind kind, ElementFlags flags,
    uint64_t valueSize, uint64_t bitWidth, ArrayRef<int64_t> traceOffsets,
    ArrayRef<int32_t> traceKinds, StringRef owner, bool requireLayout) {
  std::optional<ElementDescriptor> expected = getElementDescriptor(element);
  if (!expected) {
    if (getPackedScalarType(element))
      return operation->emitOpError("packed element has no canonical width");
    if (isAggregateType(element)) {
      std::optional<uint64_t> width = getProvenanceSpan(element);
      return operation->emitOpError(
          width && *width ? "aggregate element has no canonical trace layout"
                          : "aggregate element has no canonical layout");
    }
    if (requireLayout)
      return operation->emitOpError("element metadata does not match the ")
             << owner << " element type";
  } else if (kind != expected->kind || valueSize != expected->valueSize ||
             bitWidth != expected->bitWidth ||
             bitEnumContainsAny(flags, ElementFlags::FourState) !=
                 bitEnumContainsAny(expected->flags, ElementFlags::FourState)) {
    return operation->emitOpError("element metadata does not match the ")
           << owner << " element type";
  }
  ArrayRef<int64_t> expectedOffsets =
      expected ? ArrayRef<int64_t>(expected->traceOffsets)
               : ArrayRef<int64_t>{};
  ArrayRef<int32_t> expectedKinds =
      expected ? ArrayRef<int32_t>(expected->traceKinds) : ArrayRef<int32_t>{};
  if (traceOffsets != expectedOffsets || traceKinds != expectedKinds)
    return operation->emitOpError("trace inventory does not match the ")
           << owner << " element type";
  return success();
}

LogicalResult SimContainerCreateOp::verify() {
  Type type = getResult().getType();
  Type element = getSequentialContainerElement(type);
  if (!element)
    return emitOpError("result must be a dynamic array or queue");
  if (getTypeId() == 0)
    return emitOpError("element type ID must be nonzero");
  if (getValueSize() == 0 || getAlignment() == 0 ||
      !llvm::isPowerOf2_64(getAlignment()) ||
      getValueSize() % getAlignment() != 0)
    return emitOpError("element size and alignment are invalid");
  ArrayRef<int64_t> traceOffsets = getTraceOffsets();
  ArrayRef<int32_t> traceKinds = getTraceKinds();
  if (failed(verifyManagedTraceInventory(getOperation(), getElementKind(),
                                         getValueSize(), traceOffsets,
                                         traceKinds)))
    return failure();
  if (isa<DynamicArrayType>(type) &&
      getContainerKind() != ContainerKind::DynamicArray)
    return emitOpError("dynamic-array result requires dynamic-array metadata");
  if (isa<QueueType>(type) && getContainerKind() != ContainerKind::Queue)
    return emitOpError("queue result requires queue metadata");
  if (failed(verifyElementMetadata(
          getOperation(), element, getElementKind(), getElementFlags(),
          getValueSize(), getBitWidth(), traceOffsets, traceKinds,
          "result container", /*requireLayout=*/false)))
    return failure();
  return success();
}

LogicalResult SimContainerCloneOp::verify() {
  if (!getContainerElement(getInput().getType()) ||
      getInput().getType() != getResult().getType())
    return emitOpError("input and result must be the same container type");
  return success();
}

static LogicalResult verifyFixedContainerTransfer(Operation *operation,
                                                  Type containerType,
                                                  UnpackedArrayType array,
                                                  uint64_t elementSpan) {
  Type element = getContainerElement(containerType);
  std::optional<uint64_t> expected = getArrayElementStorageStride(array);
  if (!element || element != array.getElementType())
    return operation->emitOpError(
        "container and fixed array element types must match");
  if (!expected || *expected == 0 || elementSpan != *expected)
    return operation->emitOpError(
        "element span does not match the fixed array layout");
  return success();
}

LogicalResult SimContainerImportFixedOp::verify() {
  return verifyFixedContainerTransfer(getOperation(), getContainer().getType(),
                                      getInput().getType(), getElementSpan());
}

LogicalResult SimContainerExportFixedOp::verify() {
  return verifyFixedContainerTransfer(getOperation(), getContainer().getType(),
                                      getResult().getType(), getElementSpan());
}

LogicalResult SimContainerExportBitstreamOp::verify() {
  Type element = getContainerElement(getContainer().getType());
  Type elementScalar = getPackedScalarType(element);
  Type resultScalar = getResult().getType();
  auto associative = dyn_cast<AssocArrayType>(getContainer().getType());
  std::optional<unsigned> elementWidth =
      elementScalar ? getPackedWidth(elementScalar) : std::nullopt;
  if ((!isa<DynamicArrayType, QueueType>(getContainer().getType()) &&
       !associative) ||
      !elementScalar || !elementWidth || *elementWidth == 0)
    return emitOpError(
        "input must be a sequential container or typed associative array of "
        "fixed packed elements");
  if (!isa<IntegerType, LogicType>(resultScalar))
    return emitOpError(
        "result must be a nonempty fixed bit-stream containing a whole "
        "number of input elements");
  std::optional<unsigned> resultWidth = getPackedWidth(resultScalar);
  if (!resultWidth || *resultWidth == 0 || *resultWidth % *elementWidth != 0)
    return emitOpError(
        "result must be a nonempty fixed bit-stream containing a whole "
        "number of input elements");
  return success();
}

LogicalResult SimRecursiveExportBitstreamOp::verify() {
  if (!isa<IntegerType, LogicType>(getResult().getType()) ||
      getPackedWidth(getResult().getType()).value_or(0) == 0)
    return emitOpError("result must be a nonempty fixed packed scalar");
  std::optional<SmallVector<uint64_t>> expected = getRecursiveBitStreamPlan(
      getInput().getType(), [&](ClassHandleType handle, bool directRoot) {
        return std::optional<uint64_t>(getClassBitStreamGroupID(
            handle, directRoot && getClassAllowHiddenRoot()));
      });
  if (!expected || expected->size() != getPlan().size())
    return emitOpError(
        "input must be a recursively dynamically sized bit-stream source");
  for (auto [actual, wanted] : llvm::zip_equal(getPlan(), *expected))
    if (static_cast<uint64_t>(actual) != wanted)
      return emitOpError("recursive bit-stream plan does not match the input ")
             << "type";
  uint64_t identity = (*expected)[0];
  bool objectPlan = static_cast<uint32_t>(identity >> 32) ==
                    OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_OBJECT_VERSION;
  if (objectPlan) {
    ModuleOp module = (*this)->getParentOfType<ModuleOp>();
    if (!module || !sim::has<sim::metadata::ClassBitstreamSource>(module))
      return emitOpError(
          "object plan requires the class bit-stream source feature marker");
  }
  if (auto site = getClassSiteIdAttr()) {
    if (!objectPlan || site.getValue().isZero() || site.getValue().isNegative())
      return emitOpError(
          "class site ID must be nonzero and occur only on an object plan");
  }
  if (getClassAllowHiddenRoot() &&
      (!objectPlan || !isa<ClassHandleType>(getInput().getType())))
    return emitOpError(
        "hidden class root access requires a direct object-plan source");
  return success();
}

void SimRecursiveExportBitstreamOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  effects.emplace_back(MemoryEffects::Read::get(), HeapResource::get());
  if (getObserve())
    effects.emplace_back(MemoryEffects::Write::get(), SchedulerResource::get());
}

LogicalResult SimContainerSwapOp::verify() {
  if (!isa<DynamicArrayType, QueueType>(getContainer().getType()))
    return emitOpError("operand must be a dynamic array or queue");
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
  if (getResult().getType().getSpecialization() &&
      getTypeId() != getResult().getType().getMessageTypeID())
    return emitOpError(
        "message descriptor ID must match the mailbox specialization");
  if (getValueSize() == 0 || getAlignment() == 0 ||
      !llvm::isPowerOf2_64(getAlignment()) ||
      getValueSize() % getAlignment() != 0)
    return emitOpError("element size and alignment are invalid");
  ArrayRef<int64_t> traceOffsets = getTraceOffsets();
  ArrayRef<int32_t> traceKinds = getTraceKinds();
  if (failed(verifyManagedTraceInventory(getOperation(), getElementKind(),
                                         getValueSize(), traceOffsets,
                                         traceKinds)))
    return failure();
  if (failed(verifyElementMetadata(getOperation(), element, getElementKind(),
                                   getElementFlags(), getValueSize(),
                                   getBitWidth(), traceOffsets, traceKinds,
                                   "mailbox", /*requireLayout=*/true)))
    return failure();
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

LogicalResult verifyAssocKey(Operation *op, AssocArrayType array, Type key) {
  if (array.getKeyType() != key)
    return op->emitOpError("key type must match the associative array key");
  return success();
}

LogicalResult SimAssocCreateOp::verify() {
  AssocArrayType array = getResult().getType();
  if (getTypeId() == 0)
    return emitOpError("element descriptor is outside the runtime ABI");
  if (getValueSize() == 0 || getAlignment() == 0 ||
      !llvm::isPowerOf2_64(getAlignment()) ||
      getValueSize() % getAlignment() != 0)
    return emitOpError("element descriptor has an invalid layout");
  if (failed(verifyManagedTraceInventory(getOperation(), getElementKind(),
                                         getValueSize(), getTraceOffsets(),
                                         getTraceKinds())))
    return failure();
  Type element = array.getElementType();
  if (failed(verifyElementMetadata(
          getOperation(), element, getElementKind(), getElementFlags(),
          getValueSize(), getBitWidth(), getTraceOffsets(), getTraceKinds(),
          "associative", /*requireLayout=*/false)))
    return failure();
  std::optional<AssocKeyLayout> key = getAssocKeyLayout(array);
  if (!key || key->kind != getKeyKind() || key->width != getKeyWidth())
    return emitOpError("key metadata does not match the associative key type");
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
  return success();
}

LogicalResult SimStringFromPackedOp::verify() {
  Type scalar = getPackedScalarType(getInput().getType());
  if (!scalar || !getPackedWidth(scalar))
    return emitOpError("input must be a fixed packed value");
  return success();
}

LogicalResult SimStringToPackedExactOp::verify() {
  auto type = dyn_cast<IntegerType>(getResult().getType());
  if (!type || type.getWidth() == 0 || (type.getWidth() % 8) != 0)
    return emitOpError(
        "packed result width must be a nonzero multiple of eight");
  return success();
}

LogicalResult SimStringConcatOp::verify() {
  if (getInputs().size() > std::numeric_limits<uint32_t>::max())
    return emitOpError("input count exceeds the managed string ABI");
  return success();
}

static LogicalResult verifyStringRadix(Operation *operation, Radix radix) {
  if (!symbolizeRadix(static_cast<uint32_t>(radix)))
    return operation->emitOpError("radix must be 2, 8, 10, or 16");
  return success();
}

LogicalResult SimStringParseIntegerOp::verify() {
  return verifyStringRadix(getOperation(), getRadix());
}

LogicalResult SimStringParseLogicOp::verify() {
  return verifyStringRadix(getOperation(), getRadix());
}

LogicalResult SimPlusargParseLogicOp::verify() {
  return verifyStringRadix(getOperation(), getRadix());
}

LogicalResult SimStringFormatIntegerOp::verify() {
  return verifyStringRadix(getOperation(), getRadix());
}

} // namespace obelisk::sim
