//===- ContainerBitstream.cpp - Feature-local bit-stream export -----------===//

#include "ContainerStorageInternal.h"
#include "RuntimeInternal.h"

#include <array>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using obelisk::runtime_detail::AssocSlot;
using obelisk::runtime_detail::assocSlotStride;
using obelisk::runtime_detail::assocValueOffset;
using obelisk::runtime_detail::BufferHeader;
using obelisk::runtime_detail::ContainerHeader;
using obelisk::runtime_detail::ensureAssocOrdered;
using obelisk::runtime_detail::ensureAssocOrderedWithoutSafepoint;

constexpr obelisk_rt_status assocOrderRequired = -1;

OBELISK_RT_FEATURE_HELPER void
copyBits(uint8_t *destination, uint64_t destinationOffset,
         const uint8_t *source, const uint8_t *sourceUnknown,
         uint64_t sourceOffset, uint64_t width, bool outputFourState,
         uint8_t *outputUnknown) {
  if (((destinationOffset | sourceOffset | width) & 7) == 0) {
    uint8_t *output = destination + destinationOffset / 8;
    source += sourceOffset / 8;
    if (sourceUnknown)
      sourceUnknown += sourceOffset / 8;
    size_t bytes = static_cast<size_t>(width / 8);
    if (!sourceUnknown || outputFourState)
      std::memcpy(output, source, bytes);
    else
      for (size_t byte = 0; byte != bytes; ++byte)
        output[byte] =
            static_cast<uint8_t>(source[byte] & ~sourceUnknown[byte]);
    if (outputFourState && sourceUnknown)
      std::memcpy(outputUnknown + destinationOffset / 8, sourceUnknown, bytes);
    return;
  }
  for (uint64_t bit = 0; bit != width; ++bit) {
    uint64_t sourceBit = sourceOffset + bit;
    uint8_t sourceMask = static_cast<uint8_t>(1u << (sourceBit & 7));
    uint64_t destinationBit = destinationOffset + bit;
    uint8_t destinationMask = static_cast<uint8_t>(1u << (destinationBit & 7));
    uint8_t &output = destination[destinationBit / 8];
    bool unknown = sourceUnknown && (sourceUnknown[sourceBit / 8] & sourceMask);
    if ((source[sourceBit / 8] & sourceMask) && (!unknown || outputFourState))
      output |= destinationMask;
    else
      output &= static_cast<uint8_t>(~destinationMask);
    if (outputFourState && unknown)
      outputUnknown[destinationBit / 8] |= destinationMask;
  }
}

OBELISK_RT_FEATURE_HELPER bool checkedRange(const void *pointer, uint64_t size,
                                            uintptr_t &begin, uintptr_t &end) {
  if (!pointer || size > UINTPTR_MAX)
    return false;
  begin = reinterpret_cast<uintptr_t>(pointer);
  if (size > UINTPTR_MAX - begin)
    return false;
  end = begin + static_cast<uintptr_t>(size);
  return true;
}

OBELISK_RT_FEATURE_HELPER bool overlaps(uintptr_t leftBegin, uintptr_t leftEnd,
                                        uintptr_t rightBegin,
                                        uintptr_t rightEnd) {
  return leftBegin < rightEnd && rightBegin < leftEnd;
}

#ifndef OBELISK_RT_RECURSIVE_BITSTREAM_ONLY

struct ExportRequest {
  void *value;
  void *unknown;
  uint64_t planeSize;
  uint64_t bitWidth;
  uint32_t fourState;
  uint64_t elementWidth;
  uint64_t count;
  uint64_t elementPlaneSize;
  uint32_t elementFourState;
};

struct BufferRequest {
  ContainerHeader *header;
  ExportRequest *request;
};

OBELISK_RT_FEATURE_HELPER obelisk_rt_status packBuffer(void *opaque,
                                                       uint8_t *buffer,
                                                       uint64_t extent) {
  auto *environment = static_cast<BufferRequest *>(opaque);
  ContainerHeader &header = *environment->header;
  ExportRequest &request = *environment->request;
  if (extent < sizeof(BufferHeader) ||
      reinterpret_cast<BufferHeader *>(buffer)->reserved != 0)
    return OBELISK_RT_INVALID_HANDLE;
  if (request.elementFourState && request.elementPlaneSize > UINT64_MAX / 2)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t stride =
      request.elementPlaneSize * (request.elementFourState ? 2 : 1);
  if (stride == 0 || header.capacity > (extent - sizeof(BufferHeader)) / stride)
    return OBELISK_RT_INVALID_HANDLE;
  uint8_t *data = buffer + sizeof(BufferHeader);
  for (uint64_t ordinal = 0; ordinal != request.count; ++ordinal) {
    uint64_t physical = header.kind == OBELISK_RT_CONTAINER_QUEUE
                            ? (header.head + ordinal) & (header.capacity - 1)
                            : ordinal;
    const uint8_t *source = data + physical * stride;
    const uint8_t *sourceUnknown =
        request.elementFourState ? source + request.elementPlaneSize : nullptr;
    uint64_t destination = (request.count - ordinal - 1) * request.elementWidth;
    copyBits(static_cast<uint8_t *>(request.value), destination, source,
             sourceUnknown, 0, request.elementWidth, request.fourState != 0,
             static_cast<uint8_t *>(request.unknown));
  }
  return OBELISK_RT_OK;
}

struct AssocBufferRequest {
  ContainerHeader *header;
  ExportRequest *request;
  const uint64_t *indices;
};

OBELISK_RT_FEATURE_HELPER obelisk_rt_status packAssocBuffer(void *opaque,
                                                            uint8_t *buffer,
                                                            uint64_t extent) {
  auto *environment = static_cast<AssocBufferRequest *>(opaque);
  ContainerHeader &header = *environment->header;
  ExportRequest &request = *environment->request;
  if (extent < sizeof(BufferHeader) ||
      reinterpret_cast<BufferHeader *>(buffer)->reserved != 0 ||
      (request.elementFourState && request.elementPlaneSize > UINT64_MAX / 2))
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t stride = assocSlotStride(header.element);
  uint64_t valueOffset = assocValueOffset(header.element);
  if (stride == 0 || header.capacity > (extent - sizeof(BufferHeader)) / stride)
    return OBELISK_RT_INVALID_HANDLE;
  const uint8_t *data = buffer + sizeof(BufferHeader);
  for (uint64_t ordinal = 0; ordinal != request.count; ++ordinal) {
    uint64_t physical = environment->indices[ordinal];
    if (physical >= header.capacity)
      return OBELISK_RT_INVALID_HANDLE;
    const uint8_t *slotBytes = data + physical * stride;
    auto *slot = reinterpret_cast<const AssocSlot *>(slotBytes);
    if (slot->hash == 0)
      return OBELISK_RT_INVALID_HANDLE;
    const uint8_t *source = slotBytes + valueOffset;
    const uint8_t *sourceUnknown =
        request.elementFourState ? source + request.elementPlaneSize : nullptr;
    uint64_t destination = (request.count - ordinal - 1) * request.elementWidth;
    copyBits(static_cast<uint8_t *>(request.value), destination, source,
             sourceUnknown, 0, request.elementWidth, request.fourState != 0,
             static_cast<uint8_t *>(request.unknown));
  }
  return OBELISK_RT_OK;
}

struct AssocOrderRequest {
  ContainerHeader *header;
  ExportRequest *request;
};

OBELISK_RT_FEATURE_HELPER obelisk_rt_status packAssocOrder(void *opaque,
                                                           uint8_t *buffer,
                                                           uint64_t extent) {
  auto *environment = static_cast<AssocOrderRequest *>(opaque);
  if (extent < sizeof(BufferHeader) ||
      reinterpret_cast<BufferHeader *>(buffer)->reserved != 0 ||
      environment->request->count >
          (extent - sizeof(BufferHeader)) / sizeof(uint64_t))
    return OBELISK_RT_INVALID_HANDLE;
  auto *indices =
      reinterpret_cast<const uint64_t *>(buffer + sizeof(BufferHeader));
  AssocBufferRequest request{environment->header, environment->request,
                             indices};
  return obelisk_rt_managed_object_access(environment->header->buffer,
                                          OBELISK_RT_MANAGED_BUFFER,
                                          packAssocBuffer, &request);
}

OBELISK_RT_FEATURE_HELPER obelisk_rt_status packContainer(void *opaque,
                                                          uint8_t *object,
                                                          uint64_t extent) {
  if (extent != sizeof(ContainerHeader))
    return OBELISK_RT_INVALID_HANDLE;
  auto *header = reinterpret_cast<ContainerHeader *>(object);
  auto *request = static_cast<ExportRequest *>(opaque);
  bool sequential = header->kind == OBELISK_RT_CONTAINER_DYNAMIC_ARRAY ||
                    header->kind == OBELISK_RT_CONTAINER_QUEUE;
  bool associative = header->kind == OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY;
  bool fourState = header->element && (header->element->flags &
                                       OBELISK_RT_ELEMENT_FOUR_STATE) != 0;
  if ((!sequential && !associative) || !header->element ||
      (header->element->kind != OBELISK_RT_ELEMENT_BITS &&
       header->element->kind != OBELISK_RT_ELEMENT_LOGIC) ||
      (header->element->kind == OBELISK_RT_ELEMENT_LOGIC) != fourState ||
      (header->element->flags &
       ~(OBELISK_RT_ELEMENT_FOUR_STATE | OBELISK_RT_ELEMENT_SIGNED)) != 0 ||
      header->element->bit_width != request->elementWidth ||
      header->element->value_size != request->elementPlaneSize ||
      fourState != (request->elementFourState != 0) ||
      (sequential ? header->size < request->count
                  : header->size != request->count) ||
      header->size > header->capacity ||
      !header->buffer ||
      (header->kind == OBELISK_RT_CONTAINER_QUEUE &&
       (header->capacity == 0 ||
        (header->capacity & (header->capacity - 1)) != 0 ||
        header->head >= header->capacity)) ||
      (associative && (header->capacity == 0 ||
                       (header->capacity & (header->capacity - 1)) != 0)))
    return OBELISK_RT_ARGUMENT_MISMATCH;
  if (associative) {
    if (!header->ordered)
      return assocOrderRequired;
    AssocOrderRequest orderRequest{header, request};
    return obelisk_rt_managed_object_access(header->ordered,
                                            OBELISK_RT_MANAGED_BUFFER,
                                            packAssocOrder, &orderRequest);
  }
  BufferRequest bufferRequest{header, request};
  return obelisk_rt_managed_object_access(
      header->buffer, OBELISK_RT_MANAGED_BUFFER, packBuffer, &bufferRequest);
}

} // namespace

extern "C" OBELISK_RT_FEATURE_TEXT obelisk_rt_status
obelisk_rt_v1_container_export_bitstream(obelisk_rt_object_v1 *container,
                                         void *outValue, void *outUnknown,
                                         uint64_t planeSize, uint64_t bitWidth,
                                         uint32_t fourState,
                                         uint64_t elementWidth, uint64_t count,
                                         uint64_t elementPlaneSize,
                                         uint32_t elementFourState) {
  if (!container || !outValue || planeSize == 0 || bitWidth == 0 ||
      planeSize < bitWidth / 8 + ((bitWidth & 7) != 0) || fourState > 1 ||
      (fourState && !outUnknown) || elementWidth == 0 ||
      bitWidth % elementWidth != 0 || count != bitWidth / elementWidth ||
      elementPlaneSize == 0 || elementFourState > 1 || planeSize > SIZE_MAX ||
      elementPlaneSize > SIZE_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memset(outValue, 0, static_cast<size_t>(planeSize));
  if (fourState)
    std::memset(outUnknown, 0, static_cast<size_t>(planeSize));
  ExportRequest request{outValue, outUnknown,       planeSize,
                        bitWidth, fourState,        elementWidth,
                        count,    elementPlaneSize, elementFourState};
  obelisk_rt_status status = obelisk_rt_managed_object_access(
      container, OBELISK_RT_MANAGED_CONTAINER, packContainer, &request);
  if (status != assocOrderRequired)
    return status;
  obelisk_rt_context *context = obelisk_rt_managed_object_context(container);
  obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
  if (!lane)
    return OBELISK_RT_INVALID_LIFECYCLE;
  status = ensureAssocOrderedWithoutSafepoint(lane, container);
  if (status != OBELISK_RT_OK)
    return status;
  status = obelisk_rt_managed_object_access(
      container, OBELISK_RT_MANAGED_CONTAINER, packContainer, &request);
  return status == assocOrderRequired ? OBELISK_RT_INVALID_HANDLE : status;
}

#endif

#ifdef OBELISK_RT_RECURSIVE_BITSTREAM_ONLY

} // namespace

namespace {

struct RecursiveRecord {
  uint32_t opcode;
  uint32_t bodyRecords;
  uint64_t sourceOffset;
  uint64_t extent;
  uint64_t stride;
  uint64_t sourceSpan;
  uint64_t outputWidth;
};

OBELISK_RT_RECURSIVE_BITSTREAM_HELPER uint64_t
readRecursive64(const uint8_t *bytes) {
  uint64_t value = 0;
  for (unsigned index = 0; index != 8; ++index)
    value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
  return value;
}

OBELISK_RT_RECURSIVE_BITSTREAM_HELPER RecursiveRecord
readRecursiveRecord(const uint8_t *records, uint64_t index) {
  const uint8_t *record = records + index * 48;
  uint64_t operation = readRecursive64(record);
  return {
      static_cast<uint32_t>(operation), static_cast<uint32_t>(operation >> 32),
      readRecursive64(record + 8),      readRecursive64(record + 16),
      readRecursive64(record + 24),     readRecursive64(record + 32),
      readRecursive64(record + 40)};
}

struct RecursiveValidationFrame {
  uint64_t index;
  uint64_t end;
  uint64_t sourceSpan;
  uint64_t sourceEnd;
};

enum class RecursiveValidation { Invalid, Valid, NeedsDepth };

OBELISK_RT_RECURSIVE_BITSTREAM_HELPER RecursiveValidation validateRecursiveBody(
    const uint8_t *records, uint64_t recordCount, uint64_t sourceSpan,
    RecursiveValidationFrame *stack, uint64_t capacity) {
  uint32_t depth = 0;
  stack[0] = {0, recordCount, sourceSpan, 0};
  while (true) {
    RecursiveValidationFrame &frame = stack[depth];
    if (frame.index == frame.end) {
      if (depth == 0)
        return RecursiveValidation::Valid;
      --depth;
      continue;
    }
    if (frame.index > frame.end)
      return RecursiveValidation::Invalid;
    RecursiveRecord record = readRecursiveRecord(records, frame.index++);
    if (record.sourceOffset < frame.sourceEnd)
      return RecursiveValidation::Invalid;
    if (record.opcode == OBELISK_RT_RECURSIVE_BITSTREAM_COPY) {
      if (record.bodyRecords != 0 || record.extent == 0 || record.stride != 0 ||
          record.sourceSpan != 0 || record.outputWidth != record.extent ||
          record.sourceOffset > frame.sourceSpan ||
          record.extent > frame.sourceSpan - record.sourceOffset)
        return RecursiveValidation::Invalid;
      frame.sourceEnd = record.sourceOffset + record.extent;
      continue;
    }
    if (record.opcode == OBELISK_RT_RECURSIVE_BITSTREAM_STRING) {
      if (record.bodyRecords != 0 || record.extent != 0 || record.stride != 0 ||
          record.sourceSpan != sizeof(obelisk_rt_managed_word_v1) * 8 ||
          record.outputWidth != 0 || (record.sourceOffset & 7) != 0 ||
          record.sourceOffset > frame.sourceSpan ||
          sizeof(obelisk_rt_managed_word_v1) * 8 >
              frame.sourceSpan - record.sourceOffset)
        return RecursiveValidation::Invalid;
      frame.sourceEnd =
          record.sourceOffset + sizeof(obelisk_rt_managed_word_v1) * 8;
      continue;
    }
    if (record.opcode == OBELISK_RT_RECURSIVE_BITSTREAM_OBJECT)
      return RecursiveValidation::Invalid;
    if (record.opcode != OBELISK_RT_RECURSIVE_BITSTREAM_REPEAT &&
        record.opcode != OBELISK_RT_RECURSIVE_BITSTREAM_CONTAINER)
      return RecursiveValidation::Invalid;
    if (record.bodyRecords == 0 ||
        record.bodyRecords > frame.end - frame.index ||
        record.sourceSpan == 0 || record.outputWidth != 0)
      return RecursiveValidation::Invalid;
    uint64_t bodyBegin = frame.index;
    uint64_t bodyEnd = bodyBegin + record.bodyRecords;
    if (record.opcode == OBELISK_RT_RECURSIVE_BITSTREAM_REPEAT) {
      if (record.extent == 0 || record.stride < record.sourceSpan ||
          record.sourceOffset > frame.sourceSpan ||
          record.extent - 1 >
              (UINT64_MAX - record.sourceOffset) / record.stride)
        return RecursiveValidation::Invalid;
      uint64_t last = record.sourceOffset + (record.extent - 1) * record.stride;
      if (last > frame.sourceSpan ||
          record.sourceSpan > frame.sourceSpan - last)
        return RecursiveValidation::Invalid;
      frame.sourceEnd = last + record.sourceSpan;
    } else {
      if ((record.extent != OBELISK_RT_CONTAINER_DYNAMIC_ARRAY &&
           record.extent != OBELISK_RT_CONTAINER_QUEUE &&
           record.extent != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY) ||
          record.stride != 0 || (record.sourceOffset & 7) != 0 ||
          record.sourceOffset > frame.sourceSpan ||
          sizeof(obelisk_rt_managed_word_v1) * 8 >
              frame.sourceSpan - record.sourceOffset)
        return RecursiveValidation::Invalid;
      frame.sourceEnd =
          record.sourceOffset + sizeof(obelisk_rt_managed_word_v1) * 8;
    }
    if (depth + 1 >= capacity)
      return RecursiveValidation::NeedsDepth;
    frame.index = bodyEnd;
    stack[++depth] = {bodyBegin, bodyEnd, record.sourceSpan, 0};
  }
}

struct RecursiveView {
  const uint8_t *value;
  const uint8_t *unknown;
  uint64_t planeSize;
  uint64_t span;
  uint64_t base;
};

struct RecursiveObjectSet {
  static constexpr size_t inlineCapacity = 8;
  std::array<obelisk_rt_object_v1 *, inlineCapacity> inlineObjects{};
  size_t inlineSize = 0;
  std::vector<obelisk_rt_object_v1 *> overflow;
  std::unordered_set<obelisk_rt_object_v1 *> overflowMembership;

  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER ~RecursiveObjectSet() = default;

  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER obelisk_rt_status
  insert(obelisk_rt_object_v1 *object) {
    for (size_t index = 0; index != inlineSize; ++index)
      if (inlineObjects[index] == object)
        return OBELISK_RT_OK;
    if (overflowMembership.find(object) != overflowMembership.end())
      return OBELISK_RT_OK;
    if (inlineSize != inlineCapacity) {
      inlineObjects[inlineSize++] = object;
      return OBELISK_RT_OK;
    }
    OBELISK_RT_TRY {
      auto [_, fresh] = overflowMembership.insert(object);
      if (!fresh)
        return OBELISK_RT_OK;
      OBELISK_RT_TRY { overflow.push_back(object); }
      OBELISK_RT_CATCH_ALL {
        overflowMembership.erase(object);
        OBELISK_RT_RETHROW;
      }
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    return OBELISK_RT_OK;
  }

  size_t size() const { return inlineSize + overflow.size(); }
  bool empty() const { return size() == 0; }

  template <typename Function>
  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER obelisk_rt_status
  visit(Function &&function) const {
    for (size_t index = 0; index != inlineSize; ++index) {
      obelisk_rt_status status = function(inlineObjects[index]);
      if (status != OBELISK_RT_OK)
        return status;
    }
    for (obelisk_rt_object_v1 *object : overflow) {
      obelisk_rt_status status = function(object);
      if (status != OBELISK_RT_OK)
        return status;
    }
    return OBELISK_RT_OK;
  }
};

struct RecursiveExport {
  const uint8_t *records;
  uint64_t recordCount;
  uint8_t *value;
  uint8_t *unknown;
  uint64_t cursor;
  bool fourState;
  bool overflow = false;
  bool collectAssociative = false;
  bool collectWatchContainers = false;
  bool writeOutput = false;
  bool needsOrdering = false;
  obelisk_rt_context *context = nullptr;
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  RecursiveObjectSet associative;
  RecursiveObjectSet watchContainers;
};

OBELISK_RT_RECURSIVE_BITSTREAM_HELPER bool
readManagedWord(const RecursiveView &view, uint64_t bitOffset,
                obelisk_rt_managed_word_v1 &word) {
  if (bitOffset > view.span || view.base > UINT64_MAX - bitOffset)
    return false;
  uint64_t absolute = view.base + bitOffset;
  if ((absolute & 7) != 0 || sizeof(word) * 8 > view.span - bitOffset ||
      absolute / 8 > view.planeSize ||
      sizeof(word) > view.planeSize - absolute / 8)
    return false;
  if (view.unknown) {
    obelisk_rt_managed_word_v1 unknown = 0;
    std::memcpy(&unknown, view.unknown + absolute / 8, sizeof(unknown));
    if (unknown != 0)
      return false;
  }
  std::memcpy(&word, view.value + absolute / 8, sizeof(word));
  return true;
}

OBELISK_RT_RECURSIVE_BITSTREAM_HELPER std::optional<uint64_t>
elementSpan(const obelisk_rt_element_type_v1 *element) {
  if (obelisk_rt_v1_element_type_validate(element) != OBELISK_RT_OK)
    return std::nullopt;
  bool fourState = (element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0;
  switch (element->kind) {
  case OBELISK_RT_ELEMENT_BITS:
    if (fourState)
      return std::nullopt;
    return element->bit_width;
  case OBELISK_RT_ELEMENT_LOGIC:
    if (!fourState)
      return std::nullopt;
    return element->bit_width;
  case OBELISK_RT_ELEMENT_AGGREGATE:
    return element->value_size <= UINT64_MAX / 8
               ? std::optional<uint64_t>(element->value_size * 8)
               : std::nullopt;
  case OBELISK_RT_ELEMENT_STRING:
  case OBELISK_RT_ELEMENT_CONTAINER_HANDLE:
    return sizeof(obelisk_rt_managed_word_v1) * 8;
  default:
    return std::nullopt;
  }
}

struct RecursiveContainerLease {
  // Declaration order deliberately makes destruction release buffer, order,
  // then header, matching the nested acquisition order.
  ManagedObjectLease header;
  ManagedObjectLease order;
  ManagedObjectLease buffer;
  obelisk_rt_object_v1 *object = nullptr;
  RecursiveRecord record{};
  uint8_t *data = nullptr;
  const uint64_t *indices = nullptr;
  uint64_t count = 0;
  uint64_t capacity = 0;
  uint64_t head = 0;
  uint64_t stride = 0;
  uint64_t valueOffset = 0;
  uint64_t planeSize = 0;
  bool fourState = false;
  bool physicalAssociative = false;

  RecursiveContainerLease() = default;
  RecursiveContainerLease(const RecursiveContainerLease &) = delete;
  RecursiveContainerLease &operator=(const RecursiveContainerLease &) = delete;
  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER
  RecursiveContainerLease(RecursiveContainerLease &&) noexcept = default;
  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER RecursiveContainerLease &
  operator=(RecursiveContainerLease &&) noexcept = default;
  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER ~RecursiveContainerLease() = default;
};

OBELISK_RT_RECURSIVE_BITSTREAM_HELPER obelisk_rt_status
acquireRecursiveContainer(RecursiveExport &request,
                          obelisk_rt_object_v1 *container,
                          const RecursiveRecord &record,
                          RecursiveContainerLease &lease) {
  lease.record = record;
  if (!container)
    return OBELISK_RT_OK;
  lease.object = container;
  if (!request.context || !request.lane)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (request.collectWatchContainers)
    if (obelisk_rt_status status = request.watchContainers.insert(container);
        status != OBELISK_RT_OK)
      return status;
  if (request.collectAssociative &&
      record.extent == OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    if (obelisk_rt_status status = request.associative.insert(container);
        status != OBELISK_RT_OK)
      return status;

  obelisk_rt_status status = obelisk_rt_managed_object_acquire(
      request.lane, container, OBELISK_RT_MANAGED_CONTAINER, &lease.header);
  if (status != OBELISK_RT_OK)
    return status;
  if (lease.header.extent != sizeof(ContainerHeader))
    return OBELISK_RT_INVALID_HANDLE;
  auto *header = reinterpret_cast<ContainerHeader *>(lease.header.object);
  std::optional<uint64_t> span = elementSpan(header->element);
  bool sequential = header->kind == OBELISK_RT_CONTAINER_DYNAMIC_ARRAY ||
                    header->kind == OBELISK_RT_CONTAINER_QUEUE;
  bool associative = header->kind == OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY;
  if ((!sequential && !associative) || header->kind != record.extent || !span ||
      *span != record.sourceSpan || header->element->value_size == 0 ||
      header->size > header->capacity ||
      (header->size != 0 && !header->buffer) ||
      (header->kind == OBELISK_RT_CONTAINER_QUEUE && header->capacity != 0 &&
       ((header->capacity & (header->capacity - 1)) != 0 ||
        header->head >= header->capacity)) ||
      (associative && header->capacity != 0 &&
       (header->capacity & (header->capacity - 1)) != 0))
    return OBELISK_RT_ARGUMENT_MISMATCH;
  lease.count = header->size;
  lease.capacity = header->capacity;
  lease.head = header->head;
  lease.planeSize = header->element->value_size;
  lease.fourState =
      (header->element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0;
  lease.physicalAssociative =
      associative && request.collectAssociative && !header->ordered;
  if (lease.physicalAssociative)
    request.needsOrdering = true;
  if (header->size == 0)
    return OBELISK_RT_OK;

  if (associative && !lease.physicalAssociative) {
    if (!header->ordered)
      return OBELISK_RT_INVALID_HANDLE;
    status = obelisk_rt_managed_object_acquire(
        request.lane, header->ordered, OBELISK_RT_MANAGED_BUFFER, &lease.order);
    if (status != OBELISK_RT_OK)
      return status;
    if (lease.order.extent < sizeof(BufferHeader) ||
        reinterpret_cast<BufferHeader *>(lease.order.object)->reserved != 0 ||
        header->size >
            (lease.order.extent - sizeof(BufferHeader)) / sizeof(uint64_t))
      return OBELISK_RT_INVALID_HANDLE;
    lease.indices = reinterpret_cast<const uint64_t *>(lease.order.object +
                                                       sizeof(BufferHeader));
  }
  status = obelisk_rt_managed_object_acquire(
      request.lane, header->buffer, OBELISK_RT_MANAGED_BUFFER, &lease.buffer);
  if (status != OBELISK_RT_OK)
    return status;
  if (lease.buffer.extent < sizeof(BufferHeader) ||
      reinterpret_cast<BufferHeader *>(lease.buffer.object)->reserved != 0)
    return OBELISK_RT_INVALID_HANDLE;
  lease.stride = associative
                     ? assocSlotStride(header->element)
                     : obelisk::runtime_detail::elementStride(header->element);
  lease.valueOffset = associative ? assocValueOffset(header->element) : 0;
  if (lease.stride == 0 ||
      header->capacity >
          (lease.buffer.extent - sizeof(BufferHeader)) / lease.stride)
    return OBELISK_RT_INVALID_HANDLE;
  lease.data = lease.buffer.object + sizeof(BufferHeader);
  return OBELISK_RT_OK;
}

struct RecursiveWalkFrame {
  enum Kind : uint8_t { Body, Repeat, Container } kind = Body;
  uint64_t index = 0;
  uint64_t end = 0;
  RecursiveView view{};
  RecursiveRecord record{};
  uint64_t ordinal = 0;
  RecursiveContainerLease container{};
  uint64_t visited = 0;

  RecursiveWalkFrame() = default;
  RecursiveWalkFrame(const RecursiveWalkFrame &) = delete;
  RecursiveWalkFrame &operator=(const RecursiveWalkFrame &) = delete;
  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER
  RecursiveWalkFrame(RecursiveWalkFrame &&) noexcept = default;
  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER RecursiveWalkFrame &
  operator=(RecursiveWalkFrame &&) noexcept = default;
  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER ~RecursiveWalkFrame() = default;
};

struct RecursiveFrameStack {
  static constexpr size_t inlineCapacity = 16;
  struct ActiveContainers {
    struct Entry {
      obelisk_rt_object_v1 *object = nullptr;
      bool tombstone = false;
    };
    std::array<obelisk_rt_object_v1 *, inlineCapacity> inlineObjects{};
    size_t inlineSize = 0;
    size_t overflowSize = 0;
    std::vector<Entry> overflow;

    OBELISK_RT_RECURSIVE_BITSTREAM_HELPER static size_t
    hash(obelisk_rt_object_v1 *object) {
      uintptr_t bits = reinterpret_cast<uintptr_t>(object) >> 3;
      bits ^= bits >> 17;
      bits *= static_cast<uintptr_t>(UINT64_C(0x9e3779b97f4a7c15));
      return static_cast<size_t>(bits ^ (bits >> 29));
    }

    OBELISK_RT_RECURSIVE_BITSTREAM_HELPER obelisk_rt_status grow() {
      size_t newSize = overflow.empty() ? 32 : overflow.size() * 2;
      if (newSize < overflow.size())
        return OBELISK_RT_OUT_OF_RESOURCES;
      std::vector<Entry> replacement;
      OBELISK_RT_TRY { replacement.resize(newSize); }
      OBELISK_RT_CATCH(const std::bad_alloc &) {
        return OBELISK_RT_OUT_OF_MEMORY;
      }
      for (const Entry &entry : overflow) {
        if (!entry.object)
          continue;
        size_t index = hash(entry.object) & (newSize - 1);
        while (replacement[index].object)
          index = (index + 1) & (newSize - 1);
        replacement[index].object = entry.object;
      }
      overflow.swap(replacement);
      return OBELISK_RT_OK;
    }

    OBELISK_RT_RECURSIVE_BITSTREAM_HELPER obelisk_rt_status
    insert(obelisk_rt_object_v1 *object) {
      if (!object)
        return OBELISK_RT_OK;
      for (size_t index = 0; index != inlineSize; ++index)
        if (inlineObjects[index] == object)
          return OBELISK_RT_INVALID_HANDLE;
      if (inlineSize != inlineCapacity && overflow.empty()) {
        inlineObjects[inlineSize++] = object;
        return OBELISK_RT_OK;
      }
      if (overflow.empty() || (overflowSize + 1) * 2 > overflow.size()) {
        obelisk_rt_status status = grow();
        if (status != OBELISK_RT_OK)
          return status;
      }
      size_t index = hash(object) & (overflow.size() - 1);
      size_t available = overflow.size();
      for (size_t probes = 0; probes != overflow.size(); ++probes) {
        Entry &entry = overflow[index];
        if (entry.object == object)
          return OBELISK_RT_INVALID_HANDLE;
        if (!entry.object) {
          if (available == overflow.size())
            available = index;
          if (!entry.tombstone)
            break;
        }
        index = (index + 1) & (overflow.size() - 1);
      }
      if (available == overflow.size())
        return OBELISK_RT_OUT_OF_RESOURCES;
      overflow[available].object = object;
      overflow[available].tombstone = false;
      ++overflowSize;
      return OBELISK_RT_OK;
    }

    OBELISK_RT_RECURSIVE_BITSTREAM_HELPER void
    erase(obelisk_rt_object_v1 *object) {
      if (!object)
        return;
      if (!overflow.empty()) {
        size_t index = hash(object) & (overflow.size() - 1);
        for (size_t probes = 0; probes != overflow.size(); ++probes) {
          Entry &entry = overflow[index];
          if (entry.object == object) {
            entry.object = nullptr;
            entry.tombstone = true;
            --overflowSize;
            return;
          }
          if (!entry.object && !entry.tombstone)
            break;
          index = (index + 1) & (overflow.size() - 1);
        }
      }
      for (size_t index = inlineSize; index != 0; --index)
        if (inlineObjects[index - 1] == object) {
          std::move(inlineObjects.begin() + index,
                    inlineObjects.begin() + inlineSize,
                    inlineObjects.begin() + index - 1);
          --inlineSize;
          return;
        }
    }
  } activeContainers;
  std::array<RecursiveWalkFrame, inlineCapacity> inlineFrames;
  size_t inlineSize = 0;
  std::vector<RecursiveWalkFrame> overflow;

  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER obelisk_rt_status
  push(RecursiveWalkFrame frame) {
    obelisk_rt_object_v1 *container =
        frame.kind == RecursiveWalkFrame::Container ? frame.container.object
                                                    : nullptr;
    obelisk_rt_status status = activeContainers.insert(container);
    if (status != OBELISK_RT_OK)
      return status;
    if (inlineSize != inlineCapacity && overflow.empty()) {
      inlineFrames[inlineSize++] = std::move(frame);
      return OBELISK_RT_OK;
    }
    OBELISK_RT_TRY { overflow.push_back(std::move(frame)); }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      activeContainers.erase(container);
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    return OBELISK_RT_OK;
  }
  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER RecursiveWalkFrame &top() {
    return overflow.empty() ? inlineFrames[inlineSize - 1] : overflow.back();
  }
  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER void pop() {
    obelisk_rt_object_v1 *container =
        top().kind == RecursiveWalkFrame::Container ? top().container.object
                                                    : nullptr;
    if (!overflow.empty()) {
      overflow.pop_back();
    } else {
      inlineFrames[inlineSize - 1].~RecursiveWalkFrame();
      new (&inlineFrames[inlineSize - 1]) RecursiveWalkFrame();
      --inlineSize;
    }
    activeContainers.erase(container);
  }
  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER bool empty() const {
    return inlineSize == 0 && overflow.empty();
  }
  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER ~RecursiveFrameStack() {
    while (!empty())
      pop();
  }
};

OBELISK_RT_RECURSIVE_BITSTREAM_HELPER obelisk_rt_status
walkRecursiveBody(RecursiveExport &request, uint64_t begin, uint64_t end,
                  const RecursiveView &root, RecursiveFrameStack &stack) {
  if (!stack.empty())
    return OBELISK_RT_INVALID_LIFECYCLE;
  RecursiveWalkFrame initial;
  initial.kind = RecursiveWalkFrame::Body;
  initial.index = begin;
  initial.end = end;
  initial.view = root;
  obelisk_rt_status status = stack.push(std::move(initial));
  if (status != OBELISK_RT_OK)
    return status;
  while (!stack.empty()) {
    RecursiveWalkFrame &frame = stack.top();
    if (frame.kind == RecursiveWalkFrame::Repeat) {
      if (frame.ordinal == frame.record.extent) {
        stack.pop();
        continue;
      }
      uint64_t base =
          frame.record.sourceOffset + frame.ordinal++ * frame.record.stride;
      RecursiveView child{frame.view.value, frame.view.unknown,
                          frame.view.planeSize, frame.record.sourceSpan,
                          frame.view.base + base};
      RecursiveWalkFrame body;
      body.kind = RecursiveWalkFrame::Body;
      body.index = frame.index;
      body.end = frame.end;
      body.view = child;
      status = stack.push(std::move(body));
      if (status != OBELISK_RT_OK)
        return status;
      continue;
    }
    if (frame.kind == RecursiveWalkFrame::Container) {
      RecursiveContainerLease &container = frame.container;
      uint64_t physical = frame.ordinal;
      if (container.physicalAssociative) {
        while (physical != container.capacity &&
               reinterpret_cast<const AssocSlot *>(container.data +
                                                   physical * container.stride)
                       ->hash == 0)
          ++physical;
        frame.ordinal = physical;
      }
      if (frame.ordinal == (container.physicalAssociative ? container.capacity
                                                          : container.count)) {
        if (frame.visited != container.count)
          return OBELISK_RT_INVALID_HANDLE;
        stack.pop();
        continue;
      }
      uint64_t ordinal = frame.ordinal++;
      if (container.record.extent == OBELISK_RT_CONTAINER_QUEUE)
        physical = (container.head + ordinal) & (container.capacity - 1);
      else if (container.record.extent ==
                   OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY &&
               !container.physicalAssociative) {
        physical = container.indices[ordinal];
        if (physical >= container.capacity ||
            reinterpret_cast<const AssocSlot *>(container.data +
                                                physical * container.stride)
                    ->hash == 0)
          return OBELISK_RT_INVALID_HANDLE;
      } else {
        physical = ordinal;
      }
      ++frame.visited;
      uint8_t *element =
          container.data + physical * container.stride + container.valueOffset;
      RecursiveView child{element,
                          container.fourState ? element + container.planeSize
                                              : nullptr,
                          container.planeSize, container.record.sourceSpan, 0};
      RecursiveWalkFrame body;
      body.kind = RecursiveWalkFrame::Body;
      body.index = frame.index;
      body.end = frame.end;
      body.view = child;
      status = stack.push(std::move(body));
      if (status != OBELISK_RT_OK)
        return status;
      continue;
    }
    if (frame.index == frame.end) {
      stack.pop();
      continue;
    }
    RecursiveRecord record =
        readRecursiveRecord(request.records, frame.index++);
    if (record.opcode == OBELISK_RT_RECURSIVE_BITSTREAM_COPY) {
      if (record.extent > request.cursor) {
        request.overflow = true;
        return OBELISK_RT_OK;
      }
      request.cursor -= record.extent;
      if (request.writeOutput)
        copyBits(request.value, request.cursor, frame.view.value,
                 frame.view.unknown, frame.view.base + record.sourceOffset,
                 record.extent, request.fourState, request.unknown);
      continue;
    }
    if (record.opcode == OBELISK_RT_RECURSIVE_BITSTREAM_STRING) {
      obelisk_rt_managed_word_v1 string = 0;
      if (!readManagedWord(frame.view, record.sourceOffset, string))
        return OBELISK_RT_INVALID_HANDLE;
      char scratch[8]{};
      const char *bytes = nullptr;
      uint64_t size = 0;
      obelisk_rt_object_v1 *stringObject =
          obelisk_rt_object_from_managed_word(string);
      if (string != 0 && stringObject) {
        ManagedObjectLease stringLease;
        status = obelisk_rt_managed_object_acquire(request.lane, stringObject,
                                                   OBELISK_RT_MANAGED_STRING,
                                                   &stringLease);
        if (status != OBELISK_RT_OK)
          return status;
      } else {
        status = obelisk_rt_validate_string(request.context, string);
        if (status != OBELISK_RT_OK)
          return status;
      }
      status = obelisk_rt_v1_string_view(string, scratch, &bytes, &size);
      if (status != OBELISK_RT_OK)
        return status;
      if (size > request.cursor / 8) {
        request.overflow = true;
        return OBELISK_RT_OK;
      }
      if (!request.writeOutput) {
        request.cursor -= size * 8;
        continue;
      }
      for (uint64_t ordinal = 0; ordinal != size; ++ordinal) {
        request.cursor -= 8;
        copyBits(request.value, request.cursor,
                 reinterpret_cast<const uint8_t *>(bytes + ordinal), nullptr, 0,
                 8, request.fourState, request.unknown);
      }
      continue;
    }
    uint64_t bodyBegin = frame.index;
    uint64_t bodyEnd = bodyBegin + record.bodyRecords;
    frame.index = bodyEnd;
    if (record.opcode == OBELISK_RT_RECURSIVE_BITSTREAM_REPEAT) {
      RecursiveWalkFrame repeat;
      repeat.kind = RecursiveWalkFrame::Repeat;
      repeat.index = bodyBegin;
      repeat.end = bodyEnd;
      repeat.view = frame.view;
      repeat.record = record;
      status = stack.push(std::move(repeat));
      if (status != OBELISK_RT_OK)
        return status;
      continue;
    }
    if (record.opcode != OBELISK_RT_RECURSIVE_BITSTREAM_CONTAINER)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_managed_word_v1 word = 0;
    if (!readManagedWord(frame.view, record.sourceOffset, word))
      return OBELISK_RT_INVALID_HANDLE;
    obelisk_rt_object_v1 *container = obelisk_rt_object_from_managed_word(word);
    if (word != obelisk_rt_managed_word_from_object(container))
      return OBELISK_RT_INVALID_HANDLE;
    RecursiveWalkFrame elements;
    elements.kind = RecursiveWalkFrame::Container;
    elements.index = bodyBegin;
    elements.end = bodyEnd;
    elements.container.object = container;
    status = stack.push(std::move(elements));
    if (status != OBELISK_RT_OK)
      return status;
    status = acquireRecursiveContainer(request, container, record,
                                       stack.top().container);
    if (status != OBELISK_RT_OK)
      stack.pop();
    if (status != OBELISK_RT_OK)
      return status;
  }
  return OBELISK_RT_OK;
}

struct RecursiveWatchGroupStateImpl final : RecursiveWatchGroupState {
  std::unordered_map<uint64_t, std::vector<uint64_t>> byToken;
  uint64_t next = 1;

  OBELISK_RT_RECURSIVE_BITSTREAM_HELPER ~RecursiveWatchGroupStateImpl() =
      default;

  static OBELISK_RT_RECURSIVE_BITSTREAM_HELPER void
  destroyState(RecursiveWatchGroupState *state) noexcept {
    delete static_cast<RecursiveWatchGroupStateImpl *>(state);
  }

  RecursiveWatchGroupStateImpl() { destroy = destroyState; }
};

OBELISK_RT_RECURSIVE_BITSTREAM_HELPER obelisk_rt_status
createRecursiveWatchGroup(obelisk_rt_context *context,
                          const RecursiveObjectSet &containers,
                          uint64_t &outToken) {
  outToken = 0;
  if (containers.empty())
    return OBELISK_RT_OK;
  std::vector<uint64_t> members;
  OBELISK_RT_TRY {
    members.reserve(containers.size());
    obelisk_rt_status status = containers.visit(
        [&](obelisk_rt_object_v1 *container)
            OBELISK_RT_RECURSIVE_BITSTREAM_HELPER {
              uint64_t token = obelisk_rt_v1_managed_watch(
                  container, OBELISK_RT_MANAGED_WATCH_CONTAINER_SIZE, 0);
              if (token == 0)
                return OBELISK_RT_INVALID_HANDLE;
              members.push_back(token);
              return OBELISK_RT_OK;
            });
    if (status != OBELISK_RT_OK)
      return status;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto *state = static_cast<RecursiveWatchGroupStateImpl *>(
        context->recursiveWatchGroups);
    if (!state) {
      state = new RecursiveWatchGroupStateImpl();
      context->recursiveWatchGroups = state;
    }
    if (state->next == 0 || state->next >= kRecursiveWatchGroupBit)
      return OBELISK_RT_OUT_OF_RESOURCES;
    uint64_t token = kRecursiveWatchGroupBit | state->next++;
    auto [_, fresh] = state->byToken.emplace(token, std::move(members));
    if (!fresh)
      return OBELISK_RT_INVALID_LIFECYCLE;
    outToken = token;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
}

OBELISK_RT_RECURSIVE_BITSTREAM_HELPER void
discardRecursiveWatchGroup(obelisk_rt_context *context, uint64_t token) {
  if (!context || token == 0)
    return;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto *state = static_cast<RecursiveWatchGroupStateImpl *>(
      context->recursiveWatchGroups);
  if (state)
    state->byToken.erase(token);
}

} // namespace

OBELISK_RT_RECURSIVE_BITSTREAM_TEXT bool
obelisk_rt_expand_recursive_watch_group(obelisk_rt_context *context,
                                        uint64_t token,
                                        RecursiveWatchGroupVisit visit,
                                        void *environment) {
  if (!context || (token & kRecursiveWatchGroupBit) == 0 || !visit)
    return false;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto *state = static_cast<RecursiveWatchGroupStateImpl *>(
      context->recursiveWatchGroups);
  if (!state)
    return false;
  auto found = state->byToken.find(token);
  if (found == state->byToken.end())
    return false;
  std::vector<uint64_t> members = std::move(found->second);
  state->byToken.erase(found);
  for (uint64_t member : members)
    if (!visit(environment, member))
      return false;
  return true;
}

extern "C" OBELISK_RT_RECURSIVE_BITSTREAM_TEXT void
obelisk_rt_v1_recursive_bitstream_link_anchor() {}

extern "C" OBELISK_RT_RECURSIVE_BITSTREAM_TEXT obelisk_rt_status
obelisk_rt_v1_recursive_export_bitstream(
    obelisk_rt_context *context, const void *inputValue,
    const void *inputUnknown, uint64_t inputPlaneSize, uint64_t inputBitWidth,
    uint32_t inputFourState, void *outValue, void *outUnknown,
    uint64_t outputPlaneSize, uint64_t outputBitWidth, uint32_t outputFourState,
    const void *plan, uint64_t planSize, uint32_t observe, uint32_t *outMatched,
    uint64_t *outWatch) {
  constexpr uint64_t headerSize =
      OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_HEADER_WORDS * 8;
  constexpr uint64_t recordSize =
      OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_RECORD_WORDS * 8;
  if (!context || !inputValue || !outValue || !plan || !outMatched ||
      !outWatch || inputFourState > 1 || outputFourState > 1 || observe > 1 ||
      (inputFourState && !inputUnknown) || (outputFourState && !outUnknown) ||
      inputBitWidth == 0 || outputBitWidth == 0 || inputPlaneSize > SIZE_MAX ||
      outputPlaneSize > SIZE_MAX || planSize > SIZE_MAX ||
      inputPlaneSize < inputBitWidth / 8 + ((inputBitWidth & 7) != 0) ||
      outputPlaneSize < outputBitWidth / 8 + ((outputBitWidth & 7) != 0) ||
      planSize < headerSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  uintptr_t inputValueBegin, inputValueEnd, inputUnknownBegin = 0,
                                            inputUnknownEnd = 0;
  uintptr_t outputValueBegin, outputValueEnd, outputUnknownBegin = 0,
                                              outputUnknownEnd = 0;
  uintptr_t planBegin, planEnd, matchedBegin, matchedEnd, watchBegin, watchEnd;
  if (!checkedRange(inputValue, inputPlaneSize, inputValueBegin,
                    inputValueEnd) ||
      (inputFourState && !checkedRange(inputUnknown, inputPlaneSize,
                                       inputUnknownBegin, inputUnknownEnd)) ||
      !checkedRange(outValue, outputPlaneSize, outputValueBegin,
                    outputValueEnd) ||
      (outputFourState &&
       !checkedRange(outUnknown, outputPlaneSize, outputUnknownBegin,
                     outputUnknownEnd)) ||
      !checkedRange(plan, planSize, planBegin, planEnd) ||
      !checkedRange(outMatched, sizeof(*outMatched), matchedBegin,
                    matchedEnd) ||
      !checkedRange(outWatch, sizeof(*outWatch), watchBegin, watchEnd))
    return OBELISK_RT_INVALID_ARGUMENT;
  auto outputOverlapsInput =
      [&](uintptr_t begin, uintptr_t end)
          OBELISK_RT_RECURSIVE_BITSTREAM_HELPER {
            return overlaps(begin, end, inputValueBegin, inputValueEnd) ||
                   (inputFourState &&
                    overlaps(begin, end, inputUnknownBegin, inputUnknownEnd)) ||
                   overlaps(begin, end, planBegin, planEnd);
          };
  if (outputOverlapsInput(outputValueBegin, outputValueEnd) ||
      outputOverlapsInput(matchedBegin, matchedEnd) ||
      outputOverlapsInput(watchBegin, watchEnd) ||
      overlaps(outputValueBegin, outputValueEnd, matchedBegin, matchedEnd) ||
      overlaps(outputValueBegin, outputValueEnd, watchBegin, watchEnd) ||
      overlaps(matchedBegin, matchedEnd, watchBegin, watchEnd) ||
      (outputFourState &&
       (outputOverlapsInput(outputUnknownBegin, outputUnknownEnd) ||
        overlaps(outputValueBegin, outputValueEnd, outputUnknownBegin,
                 outputUnknownEnd) ||
        overlaps(outputUnknownBegin, outputUnknownEnd, matchedBegin,
                 matchedEnd) ||
        overlaps(outputUnknownBegin, outputUnknownEnd, watchBegin, watchEnd))))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outMatched = 0;
  *outWatch = 0;
  const auto *bytes = static_cast<const uint8_t *>(plan);
  uint64_t identity = readRecursive64(bytes);
  uint64_t recordCount = readRecursive64(bytes + 8);
  uint64_t rootSpan = readRecursive64(bytes + 16);
  uint64_t reserved = readRecursive64(bytes + 24);
  if (static_cast<uint32_t>(identity) !=
          OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_MAGIC ||
      static_cast<uint32_t>(identity >> 32) !=
          OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_VERSION ||
      recordCount == 0 ||
      recordCount > OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_MAX_RECORDS ||
      recordCount > (UINT64_MAX - headerSize) / recordSize ||
      planSize != headerSize + recordCount * recordSize || reserved != 0 ||
      rootSpan != inputBitWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  const uint8_t *records = bytes + headerSize;
  constexpr size_t inlineValidationDepth = 16;
  std::array<RecursiveValidationFrame, inlineValidationDepth>
      inlineValidationFrames;
  RecursiveValidationFrame *validationFrames = inlineValidationFrames.data();
  RecursiveValidation validation =
      validateRecursiveBody(records, recordCount, rootSpan, validationFrames,
                            inlineValidationFrames.size());
  std::unique_ptr<RecursiveValidationFrame[]> deepValidationFrames;
  if (validation == RecursiveValidation::NeedsDepth) {
    if (recordCount == UINT64_MAX ||
        recordCount + 1 > SIZE_MAX / sizeof(RecursiveValidationFrame))
      return OBELISK_RT_OUT_OF_RESOURCES;
    deepValidationFrames.reset(new (std::nothrow)
                                   RecursiveValidationFrame[recordCount + 1]);
    if (!deepValidationFrames)
      return OBELISK_RT_OUT_OF_MEMORY;
    validationFrames = deepValidationFrames.get();
    validation = validateRecursiveBody(records, recordCount, rootSpan,
                                       validationFrames, recordCount + 1);
  }
  if (validation != RecursiveValidation::Valid)
    return OBELISK_RT_INVALID_ARGUMENT;
  deepValidationFrames.reset();
  ContextTransaction transaction(context);
  obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
  if (!lane)
    return OBELISK_RT_INVALID_LIFECYCLE;
  RecursiveExport request{records,
                          recordCount,
                          static_cast<uint8_t *>(outValue),
                          static_cast<uint8_t *>(outUnknown),
                          outputBitWidth,
                          outputFourState != 0};
  request.context = context;
  request.lane = lane;
  RecursiveView root{static_cast<const uint8_t *>(inputValue),
                     inputFourState ? static_cast<const uint8_t *>(inputUnknown)
                                    : nullptr,
                     inputPlaneSize, rootSpan, 0};
  request.collectAssociative = true;
  request.collectWatchContainers = observe != 0;
  RecursiveFrameStack walkStack;
  obelisk_rt_status status =
      walkRecursiveBody(request, 0, recordCount, root, walkStack);
  if (status != OBELISK_RT_OK)
    return status;
  if (request.overflow || request.cursor != 0)
    return OBELISK_RT_OK;
  auto validateAndPack =
      [&](bool prevalidated)
          OBELISK_RT_RECURSIVE_BITSTREAM_HELPER -> obelisk_rt_status {
    request.collectAssociative = false;
    request.collectWatchContainers = false;
    request.writeOutput = false;
    request.cursor = outputBitWidth;
    request.overflow = false;
    obelisk_rt_status result = OBELISK_RT_OK;
    if (!prevalidated) {
      result = walkRecursiveBody(request, 0, recordCount, root, walkStack);
      if (result != OBELISK_RT_OK || request.overflow || request.cursor != 0)
        return result;
    }
    uint64_t preparedWatch = 0;
    if (observe) {
      result = createRecursiveWatchGroup(context, request.watchContainers,
                                         preparedWatch);
      if (result != OBELISK_RT_OK)
        return result;
    }
    std::memset(outValue, 0, static_cast<size_t>(outputPlaneSize));
    if (outputFourState)
      std::memset(outUnknown, 0, static_cast<size_t>(outputPlaneSize));
    request.writeOutput = true;
    request.cursor = outputBitWidth;
    request.overflow = false;
    result = walkRecursiveBody(request, 0, recordCount, root, walkStack);
    if (result == OBELISK_RT_OK && !request.overflow && request.cursor == 0) {
      *outMatched = 1;
      *outWatch = preparedWatch;
    } else {
      discardRecursiveWatchGroup(context, preparedWatch);
    }
    return result;
  };

  if (request.needsOrdering) {
    status = request.associative.visit(
        [&](obelisk_rt_object_v1 *array) OBELISK_RT_RECURSIVE_BITSTREAM_HELPER {
          return ensureAssocOrderedWithoutSafepoint(lane, array);
        });
    if (status != OBELISK_RT_OK)
      return status;
  }
  status = validateAndPack(!request.needsOrdering);
  return status;
}

#endif

#ifndef OBELISK_RT_RECURSIVE_BITSTREAM_ONLY

namespace {

OBELISK_RT_FEATURE_HELPER uint64_t readPlan64(const uint8_t *bytes) {
  uint64_t value = 0;
  for (unsigned index = 0; index != 8; ++index)
    value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
  return value;
}

struct PlanRecord {
  uint32_t opcode;
  uint32_t bodyRecords;
  uint64_t sourceOffset;
  uint64_t extent;
  uint64_t stride;
  uint64_t sourceSpan;
  uint64_t outputWidth;
};

OBELISK_RT_FEATURE_HELPER PlanRecord readRecord(const uint8_t *records,
                                                uint64_t index) {
  const uint8_t *record = records + index * 48;
  uint64_t operation = readPlan64(record);
  return {
      static_cast<uint32_t>(operation), static_cast<uint32_t>(operation >> 32),
      readPlan64(record + 8),           readPlan64(record + 16),
      readPlan64(record + 24),          readPlan64(record + 32),
      readPlan64(record + 40)};
}

struct PlanFrame {
  uint64_t index;
  uint64_t end;
  union {
    struct {
      uint64_t sourceSpan;
      uint64_t output;
      uint64_t expectedOutput;
      uint64_t sourceCursor;
    } validation;
    struct {
      uint64_t sourceBase;
      uint64_t repeat;
      uint64_t repeatCount;
      uint64_t repeatBase;
      uint64_t repeatStride;
    } execution;
  } state;
};

enum class PlanValidation { Invalid, Valid, NeedsDepth };

OBELISK_RT_FEATURE_HELPER PlanValidation validatePlan(
    const uint8_t *records, uint64_t recordCount, uint64_t sourceSpan,
    uint64_t outputWidth, bool importing, PlanFrame *stack, uint64_t capacity) {
  uint64_t depth = 0;
  stack[0].index = 0;
  stack[0].end = recordCount;
  stack[0].state.validation = {sourceSpan, 0, outputWidth, 0};
  while (true) {
    PlanFrame &frame = stack[depth];
    auto &validation = frame.state.validation;
    if (frame.index == frame.end) {
      if (validation.output != validation.expectedOutput ||
          validation.sourceCursor > validation.sourceSpan)
        return PlanValidation::Invalid;
      if (depth == 0)
        return PlanValidation::Valid;
      --depth;
      continue;
    }
    if (frame.index > frame.end)
      return PlanValidation::Invalid;
    uint64_t index = frame.index++;
    PlanRecord record = readRecord(records, index);
    if (record.opcode == OBELISK_RT_AGGREGATE_BITSTREAM_COPY ||
        (importing &&
         record.opcode == OBELISK_RT_AGGREGATE_BITSTREAM_COPY_LOGIC)) {
      if (record.bodyRecords != 0 || record.extent == 0 || record.stride != 0 ||
          record.sourceSpan != 0 || record.outputWidth != record.extent ||
          record.sourceOffset < validation.sourceCursor ||
          record.sourceOffset > validation.sourceSpan ||
          record.extent > validation.sourceSpan - record.sourceOffset ||
          record.extent > UINT64_MAX - validation.output)
        return PlanValidation::Invalid;
      validation.output += record.extent;
      validation.sourceCursor = record.sourceOffset + record.extent;
      continue;
    }
    if (record.opcode != OBELISK_RT_AGGREGATE_BITSTREAM_REPEAT ||
        record.bodyRecords == 0 || record.extent == 0 ||
        record.sourceSpan == 0 || record.outputWidth == 0 ||
        record.bodyRecords > frame.end - frame.index ||
        record.sourceOffset < validation.sourceCursor ||
        record.stride < record.sourceSpan)
      return PlanValidation::Invalid;
    uint64_t bodyEnd = frame.index + record.bodyRecords;
    uint64_t last = record.extent - 1;
    if ((record.stride && last > UINT64_MAX / record.stride) ||
        record.sourceOffset > validation.sourceSpan)
      return PlanValidation::Invalid;
    uint64_t lastOffset = last * record.stride;
    if (lastOffset > validation.sourceSpan - record.sourceOffset ||
        record.sourceSpan >
            validation.sourceSpan - record.sourceOffset - lastOffset ||
        record.outputWidth > UINT64_MAX / record.extent ||
        record.outputWidth * record.extent > UINT64_MAX - validation.output)
      return PlanValidation::Invalid;
    validation.output += record.outputWidth * record.extent;
    validation.sourceCursor =
        record.sourceOffset + lastOffset + record.sourceSpan;
    uint64_t bodyStart = frame.index;
    frame.index = bodyEnd;
    if (depth == OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAX_DEPTH)
      return PlanValidation::Invalid;
    if (depth + 1 >= capacity)
      return PlanValidation::NeedsDepth;
    ++depth;
    stack[depth].index = bodyStart;
    stack[depth].end = bodyEnd;
    stack[depth].state.validation = {record.sourceSpan, 0, record.outputWidth,
                                     0};
  }
}

OBELISK_RT_FEATURE_HELPER void
executePlan(const uint8_t *records, uint64_t recordCount,
            const uint8_t *inputValue, const uint8_t *inputUnknown,
            uint8_t *outputValue, uint8_t *outputUnknown, uint64_t outputWidth,
            bool outputFourState, PlanFrame *stack) {
  uint64_t depth = 0;
  uint64_t cursor = outputWidth;
  stack[0].index = 0;
  stack[0].end = recordCount;
  stack[0].state.execution = {0, 0, 1, 0, 0};
  while (true) {
    PlanFrame &frame = stack[depth];
    auto &execution = frame.state.execution;
    if (frame.index == frame.end) {
      ++execution.repeat;
      if (execution.repeat < execution.repeatCount) {
        frame.index = execution.repeatBase;
        execution.sourceBase += execution.repeatStride;
        continue;
      }
      if (depth == 0)
        return;
      --depth;
      continue;
    }
    PlanRecord record = readRecord(records, frame.index++);
    if (record.opcode == OBELISK_RT_AGGREGATE_BITSTREAM_COPY) {
      cursor -= record.extent;
      copyBits(outputValue, cursor, inputValue, inputUnknown,
               execution.sourceBase + record.sourceOffset, record.extent,
               outputFourState, outputUnknown);
      continue;
    }
    uint64_t bodyStart = frame.index;
    frame.index += record.bodyRecords;
    ++depth;
    stack[depth].index = bodyStart;
    stack[depth].end = bodyStart + record.bodyRecords;
    stack[depth].state.execution = {execution.sourceBase + record.sourceOffset,
                                    0, record.extent, bodyStart, record.stride};
  }
}

OBELISK_RT_FEATURE_HELPER void
executeImportPlan(const uint8_t *records, uint64_t recordCount,
                  const uint8_t *inputValue, const uint8_t *inputUnknown,
                  uint8_t *outputValue, uint8_t *outputUnknown,
                  uint64_t inputWidth, bool outputFourState, PlanFrame *stack) {
  uint64_t depth = 0;
  uint64_t cursor = inputWidth;
  stack[0].index = 0;
  stack[0].end = recordCount;
  stack[0].state.execution = {0, 0, 1, 0, 0};
  while (true) {
    PlanFrame &frame = stack[depth];
    auto &execution = frame.state.execution;
    if (frame.index == frame.end) {
      ++execution.repeat;
      if (execution.repeat < execution.repeatCount) {
        frame.index = execution.repeatBase;
        execution.sourceBase += execution.repeatStride;
        continue;
      }
      if (depth == 0)
        return;
      --depth;
      continue;
    }
    PlanRecord record = readRecord(records, frame.index++);
    if (record.opcode == OBELISK_RT_AGGREGATE_BITSTREAM_COPY ||
        record.opcode == OBELISK_RT_AGGREGATE_BITSTREAM_COPY_LOGIC) {
      cursor -= record.extent;
      copyBits(outputValue, execution.sourceBase + record.sourceOffset,
               inputValue, inputUnknown, cursor, record.extent,
               outputFourState &&
                   record.opcode == OBELISK_RT_AGGREGATE_BITSTREAM_COPY_LOGIC,
               outputUnknown);
      continue;
    }
    uint64_t bodyStart = frame.index;
    frame.index += record.bodyRecords;
    ++depth;
    stack[depth].index = bodyStart;
    stack[depth].end = bodyStart + record.bodyRecords;
    stack[depth].state.execution = {execution.sourceBase + record.sourceOffset,
                                    0, record.extent, bodyStart, record.stride};
  }
}

} // namespace

extern "C" OBELISK_RT_FEATURE_TEXT obelisk_rt_status
obelisk_rt_v1_aggregate_export_bitstream(
    const void *inputValue, const void *inputUnknown, uint64_t inputPlaneSize,
    uint64_t inputBitWidth, uint32_t inputFourState, void *outValue,
    void *outUnknown, uint64_t outputPlaneSize, uint64_t outputBitWidth,
    uint32_t outputFourState, const void *plan, uint64_t planSize) {
  constexpr uint64_t headerSize =
      OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_HEADER_WORDS * 8;
  constexpr uint64_t recordSize =
      OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_RECORD_WORDS * 8;
  if (!inputValue || !outValue || !plan || inputFourState > 1 ||
      outputFourState > 1 || (inputFourState && !inputUnknown) ||
      (outputFourState && !outUnknown) || inputBitWidth == 0 ||
      outputBitWidth == 0 || inputPlaneSize > SIZE_MAX ||
      outputPlaneSize > SIZE_MAX || planSize > SIZE_MAX ||
      inputPlaneSize < inputBitWidth / 8 + ((inputBitWidth & 7) != 0) ||
      outputPlaneSize < outputBitWidth / 8 + ((outputBitWidth & 7) != 0) ||
      planSize < headerSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  uintptr_t inputValueBegin, inputValueEnd, inputUnknownBegin = 0,
                                            inputUnknownEnd = 0;
  uintptr_t outputValueBegin, outputValueEnd, outputUnknownBegin = 0,
                                              outputUnknownEnd = 0;
  uintptr_t planBegin, planEnd;
  if (!checkedRange(inputValue, inputPlaneSize, inputValueBegin,
                    inputValueEnd) ||
      (inputFourState && !checkedRange(inputUnknown, inputPlaneSize,
                                       inputUnknownBegin, inputUnknownEnd)) ||
      !checkedRange(outValue, outputPlaneSize, outputValueBegin,
                    outputValueEnd) ||
      (outputFourState &&
       !checkedRange(outUnknown, outputPlaneSize, outputUnknownBegin,
                     outputUnknownEnd)) ||
      !checkedRange(plan, planSize, planBegin, planEnd))
    return OBELISK_RT_INVALID_ARGUMENT;
  auto outputOverlapsInput = [&](uintptr_t begin, uintptr_t end) {
    return overlaps(begin, end, inputValueBegin, inputValueEnd) ||
           (inputFourState &&
            overlaps(begin, end, inputUnknownBegin, inputUnknownEnd)) ||
           overlaps(begin, end, planBegin, planEnd);
  };
  if (outputOverlapsInput(outputValueBegin, outputValueEnd) ||
      (outputFourState &&
       (outputOverlapsInput(outputUnknownBegin, outputUnknownEnd) ||
        overlaps(outputValueBegin, outputValueEnd, outputUnknownBegin,
                 outputUnknownEnd))))
    return OBELISK_RT_INVALID_ARGUMENT;
  const auto *bytes = static_cast<const uint8_t *>(plan);
  uint64_t identity = readPlan64(bytes);
  uint64_t recordCount = readPlan64(bytes + 8);
  uint64_t sourceSpan = readPlan64(bytes + 16);
  uint64_t streamWidth = readPlan64(bytes + 24);
  if (static_cast<uint32_t>(identity) !=
          OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAGIC ||
      static_cast<uint32_t>(identity >> 32) !=
          OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_VERSION ||
      recordCount == 0 ||
      recordCount > OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAX_RECORDS ||
      recordCount > (UINT64_MAX - headerSize) / recordSize ||
      planSize != headerSize + recordCount * recordSize ||
      sourceSpan != inputBitWidth || streamWidth != outputBitWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  const uint8_t *records = bytes + headerSize;
  constexpr size_t inlineDepth = 16;
  std::array<PlanFrame, inlineDepth> inlineFrames;
  std::unique_ptr<PlanFrame[]> deepFrames;
  PlanFrame *frames = inlineFrames.data();
  uint64_t frameCapacity = inlineFrames.size();
  PlanValidation validation =
      validatePlan(records, recordCount, sourceSpan, streamWidth, false, frames,
                   frameCapacity);
  if (validation == PlanValidation::NeedsDepth) {
    frameCapacity = OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAX_DEPTH + 1;
    deepFrames.reset(new (std::nothrow) PlanFrame[frameCapacity]);
    if (!deepFrames)
      return OBELISK_RT_OUT_OF_MEMORY;
    frames = deepFrames.get();
    validation = validatePlan(records, recordCount, sourceSpan, streamWidth,
                              false, frames, frameCapacity);
  }
  if (validation != PlanValidation::Valid)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memset(outValue, 0, static_cast<size_t>(outputPlaneSize));
  if (outputFourState)
    std::memset(outUnknown, 0, static_cast<size_t>(outputPlaneSize));
  executePlan(records, recordCount, static_cast<const uint8_t *>(inputValue),
              inputFourState ? static_cast<const uint8_t *>(inputUnknown)
                             : nullptr,
              static_cast<uint8_t *>(outValue),
              outputFourState ? static_cast<uint8_t *>(outUnknown) : nullptr,
              outputBitWidth, outputFourState != 0, frames);
  return OBELISK_RT_OK;
}

extern "C" OBELISK_RT_FEATURE_TEXT obelisk_rt_status
obelisk_rt_v1_aggregate_import_bitstream(
    const void *inputValue, const void *inputUnknown, uint64_t inputPlaneSize,
    uint64_t inputBitWidth, uint32_t inputFourState, void *outValue,
    void *outUnknown, uint64_t outputPlaneSize, uint64_t outputBitWidth,
    uint32_t outputFourState, const void *plan, uint64_t planSize) {
  constexpr uint64_t headerSize =
      OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_HEADER_WORDS * 8;
  constexpr uint64_t recordSize =
      OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_RECORD_WORDS * 8;
  if (!inputValue || !outValue || !plan || inputFourState > 1 ||
      outputFourState > 1 || (inputFourState && !inputUnknown) ||
      (outputFourState && !outUnknown) || inputBitWidth == 0 ||
      outputBitWidth == 0 || inputPlaneSize > SIZE_MAX ||
      outputPlaneSize > SIZE_MAX || planSize > SIZE_MAX ||
      inputPlaneSize < inputBitWidth / 8 + ((inputBitWidth & 7) != 0) ||
      outputPlaneSize < outputBitWidth / 8 + ((outputBitWidth & 7) != 0) ||
      planSize < headerSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  uintptr_t inputValueBegin, inputValueEnd, inputUnknownBegin = 0,
                                            inputUnknownEnd = 0;
  uintptr_t outputValueBegin, outputValueEnd, outputUnknownBegin = 0,
                                              outputUnknownEnd = 0;
  uintptr_t planBegin, planEnd;
  if (!checkedRange(inputValue, inputPlaneSize, inputValueBegin,
                    inputValueEnd) ||
      (inputFourState && !checkedRange(inputUnknown, inputPlaneSize,
                                       inputUnknownBegin, inputUnknownEnd)) ||
      !checkedRange(outValue, outputPlaneSize, outputValueBegin,
                    outputValueEnd) ||
      (outputFourState &&
       !checkedRange(outUnknown, outputPlaneSize, outputUnknownBegin,
                     outputUnknownEnd)) ||
      !checkedRange(plan, planSize, planBegin, planEnd))
    return OBELISK_RT_INVALID_ARGUMENT;
  auto outputOverlapsInput = [&](uintptr_t begin, uintptr_t end) {
    return overlaps(begin, end, inputValueBegin, inputValueEnd) ||
           (inputFourState &&
            overlaps(begin, end, inputUnknownBegin, inputUnknownEnd)) ||
           overlaps(begin, end, planBegin, planEnd);
  };
  if (outputOverlapsInput(outputValueBegin, outputValueEnd) ||
      (outputFourState &&
       (outputOverlapsInput(outputUnknownBegin, outputUnknownEnd) ||
        overlaps(outputValueBegin, outputValueEnd, outputUnknownBegin,
                 outputUnknownEnd))))
    return OBELISK_RT_INVALID_ARGUMENT;
  const auto *bytes = static_cast<const uint8_t *>(plan);
  uint64_t identity = readPlan64(bytes);
  uint64_t recordCount = readPlan64(bytes + 8);
  uint64_t targetSpan = readPlan64(bytes + 16);
  uint64_t streamWidth = readPlan64(bytes + 24);
  if (static_cast<uint32_t>(identity) !=
          OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAGIC ||
      static_cast<uint32_t>(identity >> 32) !=
          OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_VERSION ||
      recordCount == 0 ||
      recordCount > OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAX_RECORDS ||
      recordCount > (UINT64_MAX - headerSize) / recordSize ||
      planSize != headerSize + recordCount * recordSize ||
      targetSpan != outputBitWidth || streamWidth != inputBitWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  const uint8_t *records = bytes + headerSize;
  constexpr size_t inlineDepth = 16;
  std::array<PlanFrame, inlineDepth> inlineFrames;
  std::unique_ptr<PlanFrame[]> deepFrames;
  PlanFrame *frames = inlineFrames.data();
  uint64_t frameCapacity = inlineFrames.size();
  PlanValidation validation =
      validatePlan(records, recordCount, targetSpan, streamWidth, true, frames,
                   frameCapacity);
  if (validation == PlanValidation::NeedsDepth) {
    frameCapacity = OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAX_DEPTH + 1;
    deepFrames.reset(new (std::nothrow) PlanFrame[frameCapacity]);
    if (!deepFrames)
      return OBELISK_RT_OUT_OF_MEMORY;
    frames = deepFrames.get();
    validation = validatePlan(records, recordCount, targetSpan, streamWidth,
                              true, frames, frameCapacity);
  }
  if (validation != PlanValidation::Valid)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memset(outValue, 0, static_cast<size_t>(outputPlaneSize));
  if (outputFourState)
    std::memset(outUnknown, 0, static_cast<size_t>(outputPlaneSize));
  executeImportPlan(
      records, recordCount, static_cast<const uint8_t *>(inputValue),
      inputFourState ? static_cast<const uint8_t *>(inputUnknown) : nullptr,
      static_cast<uint8_t *>(outValue),
      outputFourState ? static_cast<uint8_t *>(outUnknown) : nullptr,
      inputBitWidth, outputFourState != 0, frames);
  return OBELISK_RT_OK;
}

#endif
