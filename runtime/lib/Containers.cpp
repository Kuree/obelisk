//===- Containers.cpp - Managed strings and container storage -------------===//

#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHash.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>

static int64_t signedAssocValue(uint64_t value, uint64_t width);

namespace {

const uint64_t stringDescriptorToken = UINT64_C(0x535452494e475631);
const uint64_t containerDescriptorToken = UINT64_C(0x434f4e5441494e31);
const uint64_t bufferDescriptorToken = UINT64_C(0x4255464645525631);
const uint64_t referencePathDescriptorToken = UINT64_C(0x5245465041544831);

struct StringHeader {
  const void *descriptor;
  std::atomic<uint64_t> hash;
};

static_assert(sizeof(StringHeader) == 16);

struct BufferHeader {
  const void *descriptor;
  uint64_t reserved;
};

struct ContainerHeader {
  const void *descriptor;
  obelisk_rt_container_kind_v1 kind;
  uint32_t reserved;
  const obelisk_rt_element_type_v1 *element;
  obelisk_rt_object_v1 *buffer;
  obelisk_rt_object_v1 *ordered;
  obelisk_rt_object_v1 *defaultValue;
  uint64_t size;
  uint64_t capacity;
  uint64_t head;
  uint64_t bound;
  uint64_t epoch;
  obelisk_rt_assoc_key_kind_v1 keyKind;
  uint32_t hasDefault;
  uint64_t keyWidth;
};

static_assert(sizeof(BufferHeader) == 16);
static_assert(sizeof(ContainerHeader) == 104);

struct AssocSlot {
  uint64_t hash;
  uint64_t distance;
  uint64_t integral;
  obelisk_rt_string_v1 string;
};

enum class ReferenceSelector : uint32_t { Index = 1, Associative = 2 };

struct ReferencePathHeader {
  const void *descriptor;
  obelisk_rt_object_v1 *owner;
  const obelisk_rt_element_type_v1 *element;
  ReferenceSelector selector;
  uint32_t ownerManaged;
  int64_t index;
  obelisk_rt_assoc_key_v1 key;
  obelisk_rt_object_v1 *watchOwner;
  uint64_t ownerPayload;
};

static_assert(sizeof(ReferencePathHeader) == 96);

constexpr uint64_t emptyAssocHash = 0;
constexpr uint64_t stringTagMask = 3;
constexpr uint64_t stringInlineTag = 1;

obelisk_rt_object_v1 *heapStringObject(obelisk_rt_string_v1 string);
bool stringBelongsTo(obelisk_rt_context *context,
                     obelisk_rt_string_v1 string);

uint64_t elementStride(const obelisk_rt_element_type_v1 *element);

uint64_t assocSlotStride(const obelisk_rt_element_type_v1 *element) {
  uint64_t valueOffset =
      (sizeof(AssocSlot) + element->alignment - 1) & ~(element->alignment - 1);
  uint64_t alignment =
      std::max<uint64_t>(alignof(AssocSlot), element->alignment);
  uint64_t size = valueOffset + elementStride(element);
  return (size + alignment - 1) & ~(alignment - 1);
}

uint64_t assocValueOffset(const obelisk_rt_element_type_v1 *element) {
  return (sizeof(AssocSlot) + element->alignment - 1) &
         ~(element->alignment - 1);
}

uint64_t elementStride(const obelisk_rt_element_type_v1 *element) {
  return element->value_size *
         ((element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) ? 2 : 1);
}

void initializeElementDefault(const obelisk_rt_element_type_v1 *element,
                              void *value, void *unknown = nullptr) {
  std::memset(value, element->kind == OBELISK_RT_ELEMENT_EVENT ? 0xff : 0,
              static_cast<size_t>(element->value_size));
  if ((element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) && unknown)
    std::memset(unknown, 0, static_cast<size_t>(element->value_size));
}

bool multiplyFits(uint64_t left, uint64_t right, uint64_t &result) {
  if (left != 0 && right > UINT64_MAX / left)
    return false;
  result = left * right;
  return true;
}

uint64_t nextPowerOfTwo(uint64_t value) {
  if (value <= 1)
    return 1;
  if (value > (UINT64_C(1) << 63))
    return 0;
  --value;
  for (unsigned shift = 1; shift != 64; shift <<= 1)
    value |= value >> shift;
  return value + 1;
}

uint64_t queueElementLimit(uint64_t declaredBound) {
  return declaredBound == UINT64_MAX ? UINT64_MAX : declaredBound + 1;
}

bool queueIsFull(const ContainerHeader &header) {
  return header.kind == OBELISK_RT_CONTAINER_QUEUE &&
         header.size >= queueElementLimit(header.bound);
}

void walkTraceSlots(uint8_t *base, const obelisk_rt_trace_layout_v1 *layout,
                    const std::function<void(
                        obelisk_rt_managed_word_v1 *,
                        obelisk_rt_managed_slot_kind_v1)> &visit) {
  if (!layout)
    return;
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const auto &entry = layout->entries[index];
    for (uint64_t item = 0; item != entry.count; ++item) {
      uint8_t *address = base + entry.offset + item * entry.stride;
      if (entry.kind == OBELISK_RT_TRACE_EMBEDDED)
        walkTraceSlots(address, entry.child_layout, visit);
      else
        visit(reinterpret_cast<obelisk_rt_managed_word_v1 *>(address),
              entry.slot_kind);
    }
  }
}

void enumerateTraceSlots(obelisk_rt_context *context, uint8_t *base,
                         const obelisk_rt_trace_layout_v1 *layout,
                         ManagedRootVisit visit, void *environment) noexcept {
  if (!layout)
    return;
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const auto &entry = layout->entries[index];
    for (uint64_t item = 0; item != entry.count; ++item) {
      uint8_t *address = base + entry.offset + item * entry.stride;
      if (entry.kind == OBELISK_RT_TRACE_EMBEDDED)
        enumerateTraceSlots(context, address, entry.child_layout, visit,
                            environment);
      else if ((entry.slot_kind & OBELISK_RT_MANAGED_SLOT_CANDIDATE) != 0) {
        obelisk_rt_managed_word_v1 word = 0;
        std::memcpy(&word, address, sizeof(word));
        word = obelisk_rt_v1_gc_candidate_root(
            context, word,
            entry.slot_kind & ~OBELISK_RT_MANAGED_SLOT_CANDIDATE);
        if (word == 0 || (word & stringTagMask) != 0)
          continue;
        auto *object = reinterpret_cast<obelisk_rt_object_v1 *>(
            static_cast<uintptr_t>(word));
        visit(environment, &object);
      } else if (entry.slot_kind == OBELISK_RT_MANAGED_SLOT_STRING) {
        obelisk_rt_managed_word_v1 word = 0;
        std::memcpy(&word, address, sizeof(word));
        if (word == 0 || (word & stringTagMask) == stringInlineTag)
          continue;
        if ((word & stringTagMask) != 0)
          continue;
        obelisk_rt_object_v1 *object = heapStringObject(word);
        visit(environment, &object);
      } else {
        visit(environment, reinterpret_cast<obelisk_rt_object_v1 **>(address));
      }
    }
  }
}

struct ValueRootEnvironment {
  obelisk_rt_context *context;
  uint8_t *data;
  uint64_t count;
  uint64_t stride;
  const obelisk_rt_element_type_v1 *element;
};

class ScopedManagedRoot {
public:
  ScopedManagedRoot(obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 **slot)
      : lane(lane), status(obelisk_rt_v1_gc_root_push(lane, &root, slot)) {}
  ScopedManagedRoot(const ScopedManagedRoot &) = delete;
  ScopedManagedRoot &operator=(const ScopedManagedRoot &) = delete;
  ~ScopedManagedRoot() {
    if (status == OBELISK_RT_OK)
      (void)obelisk_rt_v1_gc_root_pop(lane, &root);
  }
  obelisk_rt_status getStatus() const { return status; }

private:
  obelisk_rt_gc_lane_v1 *lane;
  obelisk_rt_gc_root_v1 root{};
  obelisk_rt_status status;
};

class ScopedManagedWordRoot {
public:
  ScopedManagedWordRoot(obelisk_rt_gc_lane_v1 *lane,
                        obelisk_rt_managed_word_v1 *slot)
      : lane(lane),
        status(obelisk_rt_v1_gc_managed_root_push(lane, &root, slot)) {}
  ScopedManagedWordRoot(const ScopedManagedWordRoot &) = delete;
  ScopedManagedWordRoot &operator=(const ScopedManagedWordRoot &) = delete;
  ~ScopedManagedWordRoot() {
    if (status == OBELISK_RT_OK)
      (void)obelisk_rt_v1_gc_managed_root_pop(lane, &root);
  }
  obelisk_rt_status getStatus() const { return status; }

private:
  obelisk_rt_gc_lane_v1 *lane;
  obelisk_rt_gc_managed_root_v1 root{};
  obelisk_rt_status status;
};

void enumerateValueRoots(void *opaque, ManagedRootVisit visit,
                         void *environment) {
  auto *values = static_cast<ValueRootEnvironment *>(opaque);
  for (uint64_t index = 0; index != values->count; ++index)
    enumerateTraceSlots(values->context,
                        values->data + index * values->stride,
                        values->element->trace, visit, environment);
}

class ScopedValueRoots {
public:
  ScopedValueRoots(obelisk_rt_gc_lane_v1 *lane,
                   ValueRootEnvironment *environment)
      : lane(lane),
        status(obelisk_rt_managed_roots_push(
            lane, &provider, enumerateValueRoots, environment)) {}
  ScopedValueRoots(const ScopedValueRoots &) = delete;
  ScopedValueRoots &operator=(const ScopedValueRoots &) = delete;
  ~ScopedValueRoots() {
    if (status == OBELISK_RT_OK)
      (void)obelisk_rt_managed_roots_pop(lane, &provider);
  }
  obelisk_rt_status getStatus() const { return status; }

private:
  obelisk_rt_gc_lane_v1 *lane;
  ManagedRootProvider provider{};
  obelisk_rt_status status;
};

obelisk_rt_status
validateManagedSlots(obelisk_rt_context *context,
                     const obelisk_rt_element_type_v1 *element,
                     const uint8_t *value) {
  obelisk_rt_status status = OBELISK_RT_OK;
  walkTraceSlots(const_cast<uint8_t *>(value), element->trace,
                 [&](obelisk_rt_managed_word_v1 *slot,
                     obelisk_rt_managed_slot_kind_v1 kind) {
                   if (status != OBELISK_RT_OK || !slot || *slot == 0)
                     return;
                   if ((kind & OBELISK_RT_MANAGED_SLOT_CANDIDATE) != 0)
                     return;
                   if (kind == OBELISK_RT_MANAGED_SLOT_STRING) {
                     if (!stringBelongsTo(context, *slot))
                       status = OBELISK_RT_INVALID_HANDLE;
                     return;
                   }
                   if ((*slot & stringTagMask) != 0) {
                     status = OBELISK_RT_INVALID_HANDLE;
                     return;
                   }
                   auto *object = heapStringObject(*slot);
                   obelisk_rt_managed_kind_v1 expected;
                   switch (kind) {
                   case OBELISK_RT_MANAGED_SLOT_CLASS:
                     expected = OBELISK_RT_MANAGED_CLASS;
                     break;
                   case OBELISK_RT_MANAGED_SLOT_CONTAINER:
                     expected = OBELISK_RT_MANAGED_CONTAINER;
                     break;
                   case OBELISK_RT_MANAGED_SLOT_REFERENCE_PATH:
                     expected = OBELISK_RT_MANAGED_REFERENCE_PATH;
                     break;
                   default:
                     status = OBELISK_RT_INVALID_HANDLE;
                     return;
                   }
                   if (!obelisk_rt_managed_object_belongs_to(context, object) ||
                       obelisk_rt_managed_object_kind(object) != expected)
                     status = OBELISK_RT_INVALID_HANDLE;
                 });
  return status;
}

struct HeaderSnapshot {
  ContainerHeader header{};
};

obelisk_rt_status snapshotHeader(obelisk_rt_object_v1 *container,
                                 ContainerHeader &header) {
  HeaderSnapshot snapshot;
  obelisk_rt_status status = obelisk_rt_managed_object_access(
      container, OBELISK_RT_MANAGED_CONTAINER,
      [](void *environment, uint8_t *object,
         uint64_t extent) -> obelisk_rt_status {
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *snapshot = static_cast<HeaderSnapshot *>(environment);
        std::memcpy(&snapshot->header, object, sizeof(ContainerHeader));
        bool sequential =
            snapshot->header.kind == OBELISK_RT_CONTAINER_DYNAMIC_ARRAY ||
            snapshot->header.kind == OBELISK_RT_CONTAINER_QUEUE;
        bool associative =
            snapshot->header.kind == OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY;
        bool validAssocKey =
            snapshot->header.keyKind == OBELISK_RT_ASSOC_KEY_STRING
                ? snapshot->header.keyWidth == 0
                : ((snapshot->header.keyKind == OBELISK_RT_ASSOC_KEY_UNSIGNED ||
                    snapshot->header.keyKind == OBELISK_RT_ASSOC_KEY_SIGNED) &&
                   snapshot->header.keyWidth >= 1 &&
                   snapshot->header.keyWidth <= 64);
        bool invalidAssoc =
            associative &&
            (snapshot->header.hasDefault > 1 || !validAssocKey ||
             (snapshot->header.capacity != 0 &&
              (snapshot->header.capacity < 8 ||
               (snapshot->header.capacity &
                (snapshot->header.capacity - 1)) != 0)));
        if (snapshot->header.descriptor != &containerDescriptorToken ||
            !snapshot->header.element || (!sequential && !associative) ||
            snapshot->header.size > snapshot->header.capacity ||
            (snapshot->header.capacity == 0) !=
                (snapshot->header.buffer == nullptr) ||
            (snapshot->header.kind == OBELISK_RT_CONTAINER_QUEUE &&
             snapshot->header.capacity != 0 &&
             ((snapshot->header.capacity & (snapshot->header.capacity - 1)) !=
                  0 ||
              snapshot->header.head >= snapshot->header.capacity)) ||
            invalidAssoc)
          return OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_OK;
      },
      &snapshot);
  if (status == OBELISK_RT_OK)
    header = snapshot.header;
  return status;
}

template <typename Access>
obelisk_rt_status accessBuffer(obelisk_rt_object_v1 *buffer, Access &&access) {
  if (!buffer)
    return OBELISK_RT_INVALID_HANDLE;
  struct Environment {
    Access *access;
  } environment{&access};
  return obelisk_rt_managed_object_access(
      buffer, OBELISK_RT_MANAGED_BUFFER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        if (extent < sizeof(BufferHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<BufferHeader *>(object);
        if (header->descriptor != &bufferDescriptorToken ||
            header->reserved != 0)
          return OBELISK_RT_INVALID_HANDLE;
        auto *environment = static_cast<Environment *>(opaque);
        return (*environment->access)(object + sizeof(BufferHeader),
                                      extent - sizeof(BufferHeader));
      },
      &environment);
}

obelisk_rt_status allocateBuffer(obelisk_rt_gc_lane_v1 *lane, uint64_t capacity,
                                 uint64_t stride,
                                 obelisk_rt_object_v1 **outBuffer) {
  if (!outBuffer)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outBuffer = nullptr;
  if (capacity == 0)
    return OBELISK_RT_OK;
  uint64_t bytes = 0;
  if (!multiplyFits(capacity, stride, bytes) ||
      bytes > UINT64_MAX - sizeof(BufferHeader))
    return OBELISK_RT_OUT_OF_RESOURCES;
  return obelisk_rt_managed_allocate(lane, OBELISK_RT_MANAGED_BUFFER,
                                     sizeof(BufferHeader) + bytes, 16,
                                     &bufferDescriptorToken, outBuffer);
}

obelisk_rt_status initializeContainer(obelisk_rt_gc_lane_v1 *lane,
                                      obelisk_rt_container_kind_v1 kind,
                                      const obelisk_rt_element_type_v1 *element,
                                      uint64_t size, uint64_t bound,
                                      obelisk_rt_object_v1 **outContainer) {
  if (!outContainer ||
      obelisk_rt_v1_element_type_validate(element) != OBELISK_RT_OK)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outContainer = nullptr;
  if (kind == OBELISK_RT_CONTAINER_QUEUE)
    size = std::min(size, queueElementLimit(bound));
  uint64_t capacity =
      size == 0
          ? 0
          : (kind == OBELISK_RT_CONTAINER_QUEUE ? nextPowerOfTwo(size) : size);
  if (size != 0 && capacity == 0)
    return OBELISK_RT_OUT_OF_RESOURCES;

  obelisk_rt_object_v1 *container = nullptr;
  obelisk_rt_status status = obelisk_rt_managed_allocate(
      lane, OBELISK_RT_MANAGED_CONTAINER, sizeof(ContainerHeader),
      alignof(ContainerHeader), &containerDescriptorToken, &container);
  if (status != OBELISK_RT_OK)
    return status;
  obelisk_rt_gc_root_v1 root{};
  status = obelisk_rt_v1_gc_root_push(lane, &root, &container);
  if (status != OBELISK_RT_OK)
    return status;
  obelisk_rt_context *context = obelisk_rt_managed_object_context(container);
  status = obelisk_rt_v1_element_type_register(context, element);
  if (status == OBELISK_RT_OK) {
    element = obelisk_rt_managed_element_type_lookup(context, element->type_id);
    if (!element)
      status = OBELISK_RT_INVALID_DESIGN;
  }
  obelisk_rt_object_v1 *buffer = nullptr;
  if (status == OBELISK_RT_OK)
    status = allocateBuffer(lane, capacity, elementStride(element), &buffer);
  if (status == OBELISK_RT_OK && size != 0 &&
      element->kind == OBELISK_RT_ELEMENT_EVENT)
    status = accessBuffer(buffer, [&](uint8_t *data, uint64_t extent) {
      uint64_t bytes = size * elementStride(element);
      if (bytes > extent)
        return OBELISK_RT_INVALID_HANDLE;
      std::memset(data, 0xff, static_cast<size_t>(bytes));
      return OBELISK_RT_OK;
    });
  if (status == OBELISK_RT_OK) {
    struct Initialize {
      obelisk_rt_container_kind_v1 kind;
      const obelisk_rt_element_type_v1 *element;
      obelisk_rt_object_v1 *buffer;
      uint64_t size;
      uint64_t capacity;
      uint64_t bound;
    } initialize{kind, element, buffer, size, capacity, bound};
    status = obelisk_rt_managed_object_access(
        container, OBELISK_RT_MANAGED_CONTAINER,
        [](void *environment, uint8_t *object,
           uint64_t extent) -> obelisk_rt_status {
          if (extent != sizeof(ContainerHeader))
            return OBELISK_RT_INVALID_HANDLE;
          auto *initialize = static_cast<Initialize *>(environment);
          auto *header = reinterpret_cast<ContainerHeader *>(object);
          header->descriptor = &containerDescriptorToken;
          header->kind = initialize->kind;
          header->element = initialize->element;
          header->buffer = initialize->buffer;
          header->size = initialize->size;
          header->capacity = initialize->capacity;
          header->bound = initialize->bound;
          header->epoch = 1;
          return OBELISK_RT_OK;
        },
        &initialize);
  }
  obelisk_rt_status popStatus = obelisk_rt_v1_gc_root_pop(lane, &root);
  if (status == OBELISK_RT_OK) {
    *outContainer = container;
    return popStatus;
  }
  return status;
}

obelisk_rt_status initializeAssoc(obelisk_rt_gc_lane_v1 *lane,
                                  const obelisk_rt_element_type_v1 *element,
                                  obelisk_rt_assoc_key_kind_v1 keyKind,
                                  uint64_t keyWidth,
                                  obelisk_rt_object_v1 **outContainer) {
  if (!lane || !outContainer ||
      obelisk_rt_v1_element_type_validate(element) != OBELISK_RT_OK)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (keyKind == OBELISK_RT_ASSOC_KEY_STRING) {
    if (keyWidth != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
  } else if ((keyKind != OBELISK_RT_ASSOC_KEY_UNSIGNED &&
              keyKind != OBELISK_RT_ASSOC_KEY_SIGNED) ||
             keyWidth == 0 || keyWidth > 64) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  *outContainer = nullptr;
  obelisk_rt_object_v1 *container = nullptr;
  obelisk_rt_status status = obelisk_rt_managed_allocate(
      lane, OBELISK_RT_MANAGED_CONTAINER, sizeof(ContainerHeader),
      alignof(ContainerHeader), &containerDescriptorToken, &container);
  if (status != OBELISK_RT_OK)
    return status;
  obelisk_rt_gc_root_v1 root{};
  status = obelisk_rt_v1_gc_root_push(lane, &root, &container);
  if (status != OBELISK_RT_OK)
    return status;
  obelisk_rt_context *context = obelisk_rt_managed_object_context(container);
  status = obelisk_rt_v1_element_type_register(context, element);
  if (status == OBELISK_RT_OK) {
    element = obelisk_rt_managed_element_type_lookup(context, element->type_id);
    if (!element)
      status = OBELISK_RT_INVALID_DESIGN;
  }
  struct Initialize {
    const obelisk_rt_element_type_v1 *element;
    obelisk_rt_assoc_key_kind_v1 keyKind;
    uint64_t keyWidth;
  } initialize{element, keyKind, keyWidth};
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_managed_object_access(
        container, OBELISK_RT_MANAGED_CONTAINER,
        [](void *environment, uint8_t *object,
           uint64_t extent) -> obelisk_rt_status {
          if (extent != sizeof(ContainerHeader))
            return OBELISK_RT_INVALID_HANDLE;
          auto *initialize = static_cast<Initialize *>(environment);
          auto *header = reinterpret_cast<ContainerHeader *>(object);
          header->descriptor = &containerDescriptorToken;
          header->kind = OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY;
          header->element = initialize->element;
          header->epoch = 1;
          header->keyKind = initialize->keyKind;
          header->keyWidth = initialize->keyWidth;
          return OBELISK_RT_OK;
        },
        &initialize);
  obelisk_rt_status popStatus = obelisk_rt_v1_gc_root_pop(lane, &root);
  if (status == OBELISK_RT_OK) {
    *outContainer = container;
    return popStatus;
  }
  return status;
}

uint64_t physicalIndex(const ContainerHeader &header, uint64_t logical) {
  return header.kind == OBELISK_RT_CONTAINER_QUEUE
             ? (header.head + logical) & (header.capacity - 1)
             : logical;
}

obelisk_rt_status ensureCapacity(obelisk_rt_gc_lane_v1 *lane,
                                 obelisk_rt_object_v1 *container,
                                 uint64_t required) {
  if (!lane || !container)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (obelisk_rt_managed_object_context(container) !=
      obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  obelisk_rt_gc_root_v1 ownerRoot{};
  obelisk_rt_status status =
      obelisk_rt_v1_gc_root_push(lane, &ownerRoot, &container);
  if (status != OBELISK_RT_OK)
    return status;
  while (status == OBELISK_RT_OK) {
    ContainerHeader snapshot;
    status = snapshotHeader(container, snapshot);
    if (status != OBELISK_RT_OK || snapshot.capacity >= required)
      break;
    uint64_t capacity =
        snapshot.kind == OBELISK_RT_CONTAINER_QUEUE
            ? nextPowerOfTwo(std::max<uint64_t>(required, 4))
            : std::max(required,
                       snapshot.capacity + snapshot.capacity / 2 + UINT64_C(1));
    if (capacity == 0) {
      status = OBELISK_RT_OUT_OF_RESOURCES;
      break;
    }
    obelisk_rt_object_v1 *replacement = nullptr;
    status = allocateBuffer(lane, capacity, elementStride(snapshot.element),
                            &replacement);
    if (status != OBELISK_RT_OK)
      break;
    struct Publish {
      ContainerHeader snapshot;
      obelisk_rt_object_v1 *replacement;
      uint64_t capacity;
      bool retry = false;
    } publish{snapshot, replacement, capacity};
    status = obelisk_rt_managed_object_access(
        container, OBELISK_RT_MANAGED_CONTAINER,
        [](void *environment, uint8_t *object,
           uint64_t extent) -> obelisk_rt_status {
          auto *publish = static_cast<Publish *>(environment);
          if (extent != sizeof(ContainerHeader))
            return OBELISK_RT_INVALID_HANDLE;
          auto *current = reinterpret_cast<ContainerHeader *>(object);
          if (current->epoch != publish->snapshot.epoch ||
              current->buffer != publish->snapshot.buffer ||
              current->capacity != publish->snapshot.capacity) {
            publish->retry = true;
            return OBELISK_RT_OK;
          }
          uint64_t stride = elementStride(current->element);
          obelisk_rt_status copyStatus = OBELISK_RT_OK;
          if (current->size != 0) {
            copyStatus = accessBuffer(
                current->buffer, [&](uint8_t *source, uint64_t sourceSize) {
                  return accessBuffer(
                      publish->replacement,
                      [&](uint8_t *destination, uint64_t destinationSize) {
                        if (sourceSize < current->capacity * stride ||
                            destinationSize < publish->capacity * stride)
                          return OBELISK_RT_INVALID_HANDLE;
                        for (uint64_t index = 0; index != current->size;
                             ++index)
                          std::memcpy(destination + index * stride,
                                      source + physicalIndex(*current, index) *
                                                   stride,
                                      static_cast<size_t>(stride));
                        return OBELISK_RT_OK;
                      });
                });
          }
          if (copyStatus != OBELISK_RT_OK)
            return copyStatus;
          current->buffer = publish->replacement;
          current->capacity = publish->capacity;
          current->head = 0;
          ++current->epoch;
          return OBELISK_RT_OK;
        },
        &publish);
    if (status == OBELISK_RT_OK && !publish.retry)
      break;
  }
  obelisk_rt_status popStatus = obelisk_rt_v1_gc_root_pop(lane, &ownerRoot);
  return status == OBELISK_RT_OK ? popStatus : status;
}

uint64_t hashBytes(const char *bytes, uint64_t size) {
  // Preserve the container table's established seed; only serialized hashes
  // use the canonical stable-hash offset basis.
  constexpr uint64_t stringHashSeed = UINT64_C(1469598103934665603);
  uint64_t hash = stringHashSeed;
  for (uint64_t index = 0; index != size; ++index)
    hash = obelisk_stable_hash_append_byte(
        hash, static_cast<unsigned char>(bytes[index]));
  return hash;
}

struct StringView {
  const char *bytes = "";
  uint64_t size = 0;
  uint64_t hash = 0;
  char inlineBytes[8] = {};
};

obelisk_rt_object_v1 *heapStringObject(obelisk_rt_string_v1 string) {
  return reinterpret_cast<obelisk_rt_object_v1 *>(
      static_cast<uintptr_t>(string));
}

bool isValidInlineString(obelisk_rt_string_v1 string) {
  if ((string & stringTagMask) != stringInlineTag)
    return false;
  uint8_t control = static_cast<uint8_t>(string);
  uint64_t length = (string >> 2) & 7;
  if ((control & UINT8_C(0xe0)) != 0 || length == 0)
    return false;
  uint64_t usedBits = 8 + length * 8;
  return usedBits == 64 || (string >> usedBits) == 0;
}

bool stringBelongsTo(obelisk_rt_context *context,
                     obelisk_rt_string_v1 string) {
  if (string == 0)
    return true;
  if ((string & stringTagMask) == stringInlineTag)
    return isValidInlineString(string);
  return (string & stringTagMask) == 0 &&
         obelisk_rt_managed_object_belongs_to(context,
                                              heapStringObject(string)) &&
         obelisk_rt_managed_object_kind(heapStringObject(string)) ==
             OBELISK_RT_MANAGED_STRING;
}

obelisk_rt_string_v1 encodeInlineString(const char *bytes, uint64_t size) {
  obelisk_rt_string_v1 string = (size << 2) | stringInlineTag;
  for (uint64_t index = 0; index != size; ++index)
    string |= static_cast<uint64_t>(
                  static_cast<unsigned char>(bytes[index]))
              << (8 + index * 8);
  return string;
}

obelisk_rt_status readString(obelisk_rt_string_v1 string, StringView &view) {
  view = {};
  if (string == 0)
    return OBELISK_RT_OK;
  uint64_t tag = string & stringTagMask;
  if (tag == stringInlineTag) {
    if (!isValidInlineString(string))
      return OBELISK_RT_INVALID_HANDLE;
    view.size = (string >> 2) & 7;
    for (uint64_t index = 0; index != view.size; ++index)
      view.inlineBytes[index] =
          static_cast<char>(string >> (8 + index * 8));
    view.bytes = view.inlineBytes;
    view.hash = hashBytes(view.bytes, view.size);
    return OBELISK_RT_OK;
  }
  if (tag != 0)
    return OBELISK_RT_INVALID_HANDLE;
  obelisk_rt_object_v1 *object = heapStringObject(string);
  if (!object) {
    view = {};
    return OBELISK_RT_OK;
  }
  struct Read {
    StringView *view;
  } read{&view};
  return obelisk_rt_managed_object_access(
      object, OBELISK_RT_MANAGED_STRING,
      [](void *environment, uint8_t *object,
         uint64_t extent) -> obelisk_rt_status {
        auto *read = static_cast<Read *>(environment);
        if (extent < sizeof(StringHeader) + 1)
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<StringHeader *>(object);
        if (header->descriptor != &stringDescriptorToken)
          return OBELISK_RT_INVALID_HANDLE;
        uint64_t length = extent - sizeof(StringHeader) - 1;
        const char *bytes =
            reinterpret_cast<const char *>(object + sizeof(StringHeader));
        if (bytes[length] != '\0')
          return OBELISK_RT_INVALID_HANDLE;
        read->view->bytes = bytes;
        read->view->size = length;
        uint64_t hash = header->hash.load(std::memory_order_relaxed);
        if (hash == 0) {
          hash = hashBytes(bytes, length);
          header->hash.store(hash, std::memory_order_relaxed);
        }
        read->view->hash = hash;
        return OBELISK_RT_OK;
      },
      &read);
}

obelisk_rt_status createString(obelisk_rt_gc_lane_v1 *lane, const char *bytes,
                               uint64_t size,
                               obelisk_rt_string_v1 *outString) {
  if (!outString || (!bytes && size != 0))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outString = 0;
  if (size == 0)
    return OBELISK_RT_OK;
  if (size <= 7) {
    *outString = encodeInlineString(bytes, size);
    return OBELISK_RT_OK;
  }
  if (!lane || size > UINT64_MAX - sizeof(StringHeader) - 1)
    return OBELISK_RT_OUT_OF_RESOURCES;

  obelisk_rt_object_v1 *result = nullptr;
  obelisk_rt_status status = obelisk_rt_managed_allocate(
      lane, OBELISK_RT_MANAGED_STRING, sizeof(StringHeader) + size + 1,
      alignof(StringHeader), &stringDescriptorToken, &result);
  if (status != OBELISK_RT_OK)
    return status;
  struct Initialize {
    const char *bytes;
    uint64_t size;
  } initialize{bytes, size};
  status = obelisk_rt_managed_object_access(
      result, OBELISK_RT_MANAGED_STRING,
      [](void *environment, uint8_t *object,
         uint64_t extent) -> obelisk_rt_status {
        auto *initialize = static_cast<Initialize *>(environment);
        if (extent != sizeof(StringHeader) + initialize->size + 1)
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<StringHeader *>(object);
        header->descriptor = &stringDescriptorToken;
        header->hash.store(0, std::memory_order_relaxed);
        char *destination =
            reinterpret_cast<char *>(object + sizeof(StringHeader));
        std::memcpy(destination, initialize->bytes,
                    static_cast<size_t>(initialize->size));
        destination[initialize->size] = '\0';
        return OBELISK_RT_OK;
      },
      &initialize);
  if (status == OBELISK_RT_OK)
    *outString = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(result));
  return status;
}

int32_t compareViews(const StringView &left, const StringView &right,
                     bool insensitive) {
  uint64_t common = std::min(left.size, right.size);
  if (!insensitive && common != 0) {
    int compared =
        std::memcmp(left.bytes, right.bytes, static_cast<size_t>(common));
    if (compared != 0)
      return compared < 0 ? -1 : 1;
  }
  if (!insensitive)
    return left.size == right.size ? 0 : (left.size < right.size ? -1 : 1);
  for (uint64_t index = 0; index != common; ++index) {
    unsigned char a = static_cast<unsigned char>(left.bytes[index]);
    unsigned char b = static_cast<unsigned char>(right.bytes[index]);
    if (insensitive) {
      if (a >= 'A' && a <= 'Z')
        a = static_cast<unsigned char>(a + ('a' - 'A'));
      if (b >= 'A' && b <= 'Z')
        b = static_cast<unsigned char>(b + ('a' - 'A'));
    }
    if (a != b)
      return a < b ? -1 : 1;
  }
  if (left.size == right.size)
    return 0;
  return left.size < right.size ? -1 : 1;
}

} // namespace

obelisk_rt_status
obelisk_rt_validate_string(obelisk_rt_context *context,
                           obelisk_rt_string_v1 string) noexcept {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  return stringBelongsTo(context, string) ? OBELISK_RT_OK
                                          : OBELISK_RT_INVALID_HANDLE;
}

void obelisk_rt_managed_trace_runtime_object(obelisk_rt_managed_kind_v1 kind,
                                             uint8_t *object, uint64_t extent,
                                             ManagedTraceVisit visit,
                                             void *environment) noexcept {
  // Keeping this dispatch centralized prevents runtime values from being
  // interpreted as class descriptors.
  switch (kind) {
  case OBELISK_RT_MANAGED_CONTAINER: {
    if (extent != sizeof(ContainerHeader))
      return;
    auto *header = reinterpret_cast<ContainerHeader *>(object);
    obelisk_rt_context *context = obelisk_rt_managed_object_context(
        reinterpret_cast<obelisk_rt_object_v1 *>(object));
    if (header->descriptor != &containerDescriptorToken || !header->element ||
        header->size > header->capacity)
      return;
    visit(environment, header->buffer);
    visit(environment, header->ordered);
    visit(environment, header->defaultValue);
    std::pair<ManagedTraceVisit, void *> adapter{visit, environment};
    if (header->hasDefault && header->defaultValue &&
        obelisk_rt_managed_object_kind(header->defaultValue) ==
            OBELISK_RT_MANAGED_BUFFER) {
      uint64_t defaultExtent =
          obelisk_rt_managed_object_extent(header->defaultValue);
      auto *defaultBuffer =
          reinterpret_cast<uint8_t *>(header->defaultValue);
      if (defaultExtent >= sizeof(BufferHeader) + elementStride(header->element) &&
          reinterpret_cast<BufferHeader *>(defaultBuffer)->descriptor ==
              &bufferDescriptorToken)
        enumerateTraceSlots(
            context, defaultBuffer + sizeof(BufferHeader),
            header->element->trace,
            [](void *opaque, obelisk_rt_object_v1 **root) {
              auto *pair =
                  static_cast<std::pair<ManagedTraceVisit, void *> *>(opaque);
              pair->first(pair->second, root ? *root : nullptr);
            },
            &adapter);
    }
    if (!header->buffer || header->size == 0 ||
        obelisk_rt_managed_object_kind(header->buffer) !=
            OBELISK_RT_MANAGED_BUFFER)
      return;
    uint64_t bufferExtent = obelisk_rt_managed_object_extent(header->buffer);
    if (bufferExtent < sizeof(BufferHeader))
      return;
    auto *buffer = reinterpret_cast<uint8_t *>(header->buffer);
    auto *bufferHeader = reinterpret_cast<BufferHeader *>(buffer);
    bool associative = header->kind == OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY;
    uint64_t stride = associative ? assocSlotStride(header->element)
                                  : elementStride(header->element);
    uint64_t required = 0;
    if (bufferHeader->descriptor != &bufferDescriptorToken ||
        !multiplyFits(header->capacity, stride, required) ||
        required > bufferExtent - sizeof(BufferHeader))
      return;
    uint8_t *data = buffer + sizeof(BufferHeader);
    if (associative) {
      uint64_t valueOffset = assocValueOffset(header->element);
      for (uint64_t index = 0; index != header->capacity; ++index) {
        auto *slot = reinterpret_cast<AssocSlot *>(data + index * stride);
        if (slot->hash == emptyAssocHash)
          continue;
        if (header->keyKind == OBELISK_RT_ASSOC_KEY_STRING &&
            slot->string != 0 &&
            (slot->string & stringTagMask) == 0)
          visit(environment, heapStringObject(slot->string));
        enumerateTraceSlots(
            context, reinterpret_cast<uint8_t *>(slot) + valueOffset,
            header->element->trace,
            [](void *opaque, obelisk_rt_object_v1 **root) {
              auto *pair =
                  static_cast<std::pair<ManagedTraceVisit, void *> *>(opaque);
              pair->first(pair->second, root ? *root : nullptr);
            },
            &adapter);
      }
    } else {
      for (uint64_t index = 0; index != header->size; ++index)
        enumerateTraceSlots(
            context, data + physicalIndex(*header, index) * stride,
            header->element->trace,
            [](void *opaque, obelisk_rt_object_v1 **slot) {
              auto *pair =
                  static_cast<std::pair<ManagedTraceVisit, void *> *>(opaque);
              pair->first(pair->second, slot ? *slot : nullptr);
            },
            &adapter);
    }
    return;
  }
  case OBELISK_RT_MANAGED_STRING:
  case OBELISK_RT_MANAGED_BUFFER:
  case OBELISK_RT_MANAGED_KEY_BLOB:
    return;
  case OBELISK_RT_MANAGED_REFERENCE_PATH: {
    if (extent != sizeof(ReferencePathHeader))
      return;
    auto *path = reinterpret_cast<ReferencePathHeader *>(object);
    if (path->descriptor != &referencePathDescriptorToken || !path->owner ||
        !path->element)
      return;
    visit(environment, path->owner);
    visit(environment, path->watchOwner);
    if (path->selector == ReferenceSelector::Associative &&
        path->key.kind == OBELISK_RT_ASSOC_KEY_STRING &&
        path->key.string != 0 &&
        (path->key.string & stringTagMask) == 0)
      visit(environment, heapStringObject(path->key.string));
    return;
  }
  default:
    return;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_string_create(obelisk_rt_gc_lane_v1 *lane, const char *bytes,
                            uint64_t size, obelisk_rt_string_v1 *outString) {
  return createString(lane, bytes, size, outString);
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_concat(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_string_v1 left,
    obelisk_rt_string_v1 right, obelisk_rt_string_v1 *outString) {
  const obelisk_rt_string_span_v1 spans[] = {{left}, {right}};
  return obelisk_rt_v1_string_concat_many(lane, spans, 2, outString);
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_concat_many(
    obelisk_rt_gc_lane_v1 *lane, const obelisk_rt_string_span_v1 *spans,
    uint64_t spanCount, obelisk_rt_string_v1 *outString) {
  static_assert(sizeof(obelisk_rt_string_span_v1) ==
                sizeof(obelisk_rt_managed_word_v1));
  if (!outString || (!spans && spanCount != 0))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outString = 0;
  uint64_t total = 0;
  uint64_t nonempty = 0;
  obelisk_rt_string_v1 only = 0;
  for (uint64_t index = 0; index != spanCount; ++index) {
    StringView view;
    obelisk_rt_status status = readString(spans[index].string, view);
    if (status != OBELISK_RT_OK)
      return status;
    if (view.size > UINT64_MAX - total)
      return OBELISK_RT_OUT_OF_RESOURCES;
    total += view.size;
    if (view.size != 0) {
      ++nonempty;
      only = spans[index].string;
    }
  }
  if (nonempty == 0)
    return OBELISK_RT_OK;
  if (nonempty == 1) {
    *outString = only;
    return OBELISK_RT_OK;
  }
  if (total <= 7) {
    char bytes[7];
    uint64_t offset = 0;
    for (uint64_t index = 0; index != spanCount; ++index) {
      StringView view;
      obelisk_rt_status status = readString(spans[index].string, view);
      if (status != OBELISK_RT_OK)
        return status;
      std::memcpy(bytes + offset, view.bytes, static_cast<size_t>(view.size));
      offset += view.size;
    }
    *outString = encodeInlineString(bytes, total);
    return OBELISK_RT_OK;
  }
  if (!lane || total > UINT64_MAX - sizeof(StringHeader) - 1)
    return OBELISK_RT_OUT_OF_RESOURCES;

  auto *rootWords = const_cast<obelisk_rt_managed_word_v1 *>(
      reinterpret_cast<const obelisk_rt_managed_word_v1 *>(spans));
  obelisk_rt_gc_managed_root_range_v1 rootRange{};
  obelisk_rt_status status = obelisk_rt_v1_gc_managed_root_range_push(
      lane, &rootRange, rootWords, spanCount);
  if (status != OBELISK_RT_OK)
    return status;
  obelisk_rt_object_v1 *result = nullptr;
  status = obelisk_rt_managed_allocate(
      lane, OBELISK_RT_MANAGED_STRING, sizeof(StringHeader) + total + 1,
      alignof(StringHeader), &stringDescriptorToken, &result);
  if (status == OBELISK_RT_OK) {
    struct Initialize {
      const obelisk_rt_string_span_v1 *spans;
      uint64_t count;
      uint64_t total;
    } initialize{spans, spanCount, total};
    status = obelisk_rt_managed_object_access(
        result, OBELISK_RT_MANAGED_STRING,
        [](void *environment, uint8_t *object,
           uint64_t extent) -> obelisk_rt_status {
          auto *initialize = static_cast<Initialize *>(environment);
          if (extent != sizeof(StringHeader) + initialize->total + 1)
            return OBELISK_RT_INVALID_HANDLE;
          auto *header = reinterpret_cast<StringHeader *>(object);
          header->descriptor = &stringDescriptorToken;
          header->hash.store(0, std::memory_order_relaxed);
          char *destination =
              reinterpret_cast<char *>(object + sizeof(StringHeader));
          uint64_t offset = 0;
          for (uint64_t index = 0; index != initialize->count; ++index) {
            StringView view;
            obelisk_rt_status readStatus =
                readString(initialize->spans[index].string, view);
            if (readStatus != OBELISK_RT_OK)
              return readStatus;
            std::memcpy(destination + offset, view.bytes,
                        static_cast<size_t>(view.size));
            offset += view.size;
          }
          destination[initialize->total] = '\0';
          return OBELISK_RT_OK;
        },
        &initialize);
    if (status == OBELISK_RT_OK)
      *outString =
          static_cast<uint64_t>(reinterpret_cast<uintptr_t>(result));
  }
  obelisk_rt_status popStatus =
      obelisk_rt_v1_gc_managed_root_range_pop(lane, &rootRange);
  return status == OBELISK_RT_OK ? popStatus : status;
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_repeat(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_string_v1 string, uint64_t count,
    obelisk_rt_string_v1 *outString) {
  if (!outString)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outString = 0;
  StringView view;
  obelisk_rt_status status = readString(string, view);
  if (status != OBELISK_RT_OK)
    return status;
  if (count == 0 || view.size == 0)
    return OBELISK_RT_OK;
  if (count == 1) {
    *outString = string;
    return OBELISK_RT_OK;
  }
  if (view.size > UINT64_MAX / count)
    return OBELISK_RT_OUT_OF_RESOURCES;
  uint64_t total = view.size * count;
  if (total > std::string{}.max_size())
    return OBELISK_RT_OUT_OF_RESOURCES;
  try {
    std::string bytes;
    bytes.reserve(static_cast<size_t>(total));
    for (uint64_t index = 0; index != count; ++index)
      bytes.append(view.bytes, static_cast<size_t>(view.size));
    return createString(lane, bytes.data(), total, outString);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_from_packed(
    obelisk_rt_gc_lane_v1 *lane, const void *value, const void *unknown,
    uint64_t bitWidth, obelisk_rt_string_v1 *outString) {
  if (!outString || bitWidth == 0 || !value)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outString = 0;
  if (bitWidth > UINT64_MAX - 7)
    return OBELISK_RT_OUT_OF_RESOURCES;
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (byteCount > std::string{}.max_size())
    return OBELISK_RT_OUT_OF_RESOURCES;
  const auto *valueBytes = static_cast<const uint8_t *>(value);
  const auto *unknownBytes = static_cast<const uint8_t *>(unknown);
  try {
    std::string bytes;
    bytes.reserve(static_cast<size_t>(byteCount));
    // Packed planes are little-endian. SystemVerilog strings enumerate the
    // most-significant packed byte first and omit zero-valued bytes.
    for (uint64_t index = 0; index != byteCount; ++index) {
      uint64_t source = byteCount - index - 1;
      uint8_t byte = valueBytes[source];
      if (unknownBytes)
        byte &= static_cast<uint8_t>(~unknownBytes[source]);
      if (index == 0 && (bitWidth & 7) != 0)
        byte &= static_cast<uint8_t>((UINT32_C(1) << (bitWidth & 7)) - 1);
      if (byte != 0)
        bytes.push_back(static_cast<char>(byte));
    }
    return createString(lane, bytes.data(), bytes.size(), outString);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_to_packed(
    obelisk_rt_string_v1 string, void *value, void *unknown,
    uint64_t bitWidth) {
  if (!value || bitWidth == 0 || bitWidth > UINT64_MAX - 7)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t byteCount = (bitWidth + 7) / 8;
  std::memset(value, 0, static_cast<size_t>(byteCount));
  if (unknown)
    std::memset(unknown, 0, static_cast<size_t>(byteCount));
  StringView view;
  obelisk_rt_status status = readString(string, view);
  if (status != OBELISK_RT_OK)
    return status;
  uint64_t copied = std::min(view.size, byteCount);
  auto *output = static_cast<uint8_t *>(value);
  // Assignment to a narrower packed destination retains the rightmost bytes.
  for (uint64_t index = 0; index != copied; ++index)
    output[index] =
        static_cast<uint8_t>(view.bytes[view.size - index - 1]);
  if ((bitWidth & 7) != 0)
    output[byteCount - 1] &=
        static_cast<uint8_t>((UINT32_C(1) << (bitWidth & 7)) - 1);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_view(
    obelisk_rt_string_v1 string, char scratch[8], const char **outBytes,
    uint64_t *outSize) {
  if (!scratch || !outBytes || !outSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outBytes = "";
  *outSize = 0;
  StringView view;
  obelisk_rt_status status = readString(string, view);
  if (status == OBELISK_RT_OK) {
    *outSize = view.size;
    if (string != 0 && (string & stringTagMask) == stringInlineTag) {
      std::memcpy(scratch, view.bytes, static_cast<size_t>(view.size));
      scratch[view.size] = '\0';
      *outBytes = scratch;
    } else {
      *outBytes = view.bytes;
    }
  }
  return status;
}

extern "C" uint64_t
obelisk_rt_v1_string_length(obelisk_rt_string_v1 string) {
  StringView view;
  return readString(string, view) == OBELISK_RT_OK ? view.size : 0;
}

extern "C" uint64_t obelisk_rt_v1_string_hash(obelisk_rt_string_v1 string) {
  StringView view;
  return readString(string, view) == OBELISK_RT_OK ? view.hash
                                                   : hashBytes("", 0);
}

extern "C" uint32_t obelisk_rt_v1_string_getc(obelisk_rt_string_v1 string,
                                              int64_t index) {
  StringView view;
  if (index < 0 || readString(string, view) != OBELISK_RT_OK ||
      static_cast<uint64_t>(index) >= view.size)
    return 0;
  return static_cast<unsigned char>(view.bytes[index]);
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_putc(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_string_v1 string, int64_t index,
    uint32_t character, obelisk_rt_string_v1 *outString) {
  if (!outString)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outString = 0;
  StringView view;
  obelisk_rt_status status = readString(string, view);
  if (status != OBELISK_RT_OK)
    return status;
  if ((character & 0xff) == 0 || index < 0 ||
      static_cast<uint64_t>(index) >= view.size) {
    *outString = string;
    return OBELISK_RT_OK;
  }
  if (static_cast<unsigned char>(view.bytes[index]) ==
      static_cast<unsigned char>(character)) {
    *outString = string;
    return OBELISK_RT_OK;
  }
  try {
    std::string bytes(view.bytes, static_cast<size_t>(view.size));
    bytes[static_cast<size_t>(index)] = static_cast<char>(character);
    return createString(lane, bytes.data(), view.size, outString);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_string_substr(obelisk_rt_gc_lane_v1 *lane,
                            obelisk_rt_string_v1 string, int64_t left,
                            int64_t right, obelisk_rt_string_v1 *outString) {
  if (!outString)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outString = 0;
  StringView view;
  obelisk_rt_status status = readString(string, view);
  if (status != OBELISK_RT_OK)
    return status;
  if (left < 0 || right < left || static_cast<uint64_t>(right) >= view.size)
    return OBELISK_RT_OK;
  if (left == 0 && static_cast<uint64_t>(right) + 1 == view.size) {
    *outString = string;
    return OBELISK_RT_OK;
  }
  try {
    uint64_t size = static_cast<uint64_t>(right - left) + 1;
    std::string bytes(view.bytes + left, static_cast<size_t>(size));
    return createString(lane, bytes.data(), size, outString);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
}

extern "C" int32_t obelisk_rt_v1_string_compare(obelisk_rt_string_v1 left,
                                                obelisk_rt_string_v1 right) {
  if (left == right)
    return 0;
  StringView leftView;
  StringView rightView;
  if (readString(left, leftView) != OBELISK_RT_OK ||
      readString(right, rightView) != OBELISK_RT_OK)
    return 0;
  return compareViews(leftView, rightView, false);
}

extern "C" int32_t
obelisk_rt_v1_string_compare_insensitive(obelisk_rt_string_v1 left,
                                         obelisk_rt_string_v1 right) {
  StringView leftView;
  StringView rightView;
  if (readString(left, leftView) != OBELISK_RT_OK ||
      readString(right, rightView) != OBELISK_RT_OK)
    return 0;
  return compareViews(leftView, rightView, true);
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_case_convert(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_string_v1 string, uint32_t toUpper,
    obelisk_rt_string_v1 *outString) {
  if (!outString || toUpper > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outString = 0;
  StringView view;
  obelisk_rt_status status = readString(string, view);
  if (status != OBELISK_RT_OK || view.size == 0)
    return status;
  try {
    std::string bytes(view.bytes, static_cast<size_t>(view.size));
    bool changed = false;
    for (char &character : bytes) {
      unsigned char value = static_cast<unsigned char>(character);
      unsigned char converted = value;
      if (toUpper && value >= 'a' && value <= 'z')
        converted = static_cast<unsigned char>(value - ('a' - 'A'));
      if (!toUpper && value >= 'A' && value <= 'Z')
        converted = static_cast<unsigned char>(value + ('a' - 'A'));
      changed |= converted != value;
      character = static_cast<char>(converted);
    }
    if (!changed) {
      *outString = string;
      return OBELISK_RT_OK;
    }
    return createString(lane, bytes.data(), view.size, outString);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
}

namespace {

// Scanning skips NUL along with whitespace: converting a packed variable to a
// string left-pads it with NUL bytes, and those are padding rather than input.
bool scanSpace(char character) {
  return character == ' ' || character == '\t' || character == '\n' ||
         character == '\r' || character == '\f' || character == '\v' ||
         character == '\0';
}

bool scanDigit(char character, uint32_t radix) {
  unsigned char value = static_cast<unsigned char>(character);
  if (character == '_')
    return true;
  uint32_t digit = value >= '0' && value <= '9'   ? value - '0'
                   : value >= 'a' && value <= 'f' ? value - 'a' + 10
                   : value >= 'A' && value <= 'F' ? value - 'A' + 10
                                                  : 0xffffffffu;
  return digit < radix;
}

// The span the conversion letter claims, starting at `index`, or an empty
// span when nothing matched. Every conversion but %c first skips whitespace.
uint64_t scanFieldExtent(const StringView &view, uint64_t &index,
                         uint32_t specifier) {
  char letter = static_cast<char>(
      std::tolower(static_cast<unsigned char>(specifier)));
  if (letter == 'c')
    return index < view.size ? 1 : 0;
  while (index < view.size && scanSpace(view.bytes[index]))
    ++index;
  uint64_t start = index;
  if (letter == 's') {
    while (index < view.size && !scanSpace(view.bytes[index]))
      ++index;
    return index - start;
  }
  if (index < view.size &&
      (view.bytes[index] == '+' || view.bytes[index] == '-'))
    ++index;
  if (letter == 'e' || letter == 'f' || letter == 'g') {
    while (index < view.size && (scanDigit(view.bytes[index], 10) ||
                                 view.bytes[index] == '.'))
      ++index;
    if (index < view.size &&
        (view.bytes[index] == 'e' || view.bytes[index] == 'E')) {
      uint64_t exponent = index + 1;
      if (exponent < view.size &&
          (view.bytes[exponent] == '+' || view.bytes[exponent] == '-'))
        ++exponent;
      if (exponent < view.size && scanDigit(view.bytes[exponent], 10)) {
        index = exponent;
        while (index < view.size && scanDigit(view.bytes[index], 10))
          ++index;
      }
    }
  } else {
    uint32_t radix = letter == 'b'   ? 2
                     : letter == 'o' ? 8
                     : letter == 'd' ? 10
                                     : 16;
    while (index < view.size && scanDigit(view.bytes[index], radix))
      ++index;
  }
  // A lone sign is not a field.
  return index == start || (index == start + 1 &&
                            (view.bytes[start] == '+' ||
                             view.bytes[start] == '-'))
             ? (index = start, 0)
             : index - start;
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_string_scan_field(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_string_v1 input, uint32_t cursor,
    const char *prefix, uint64_t prefixSize, uint32_t specifier,
    obelisk_rt_string_v1 *outField, uint32_t *outCursor, uint32_t *outOk) {
  if (!outField || !outCursor || !outOk || (!prefix && prefixSize != 0))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outField = 0;
  *outCursor = cursor;
  *outOk = 0;
  StringView view;
  obelisk_rt_status status = readString(input, view);
  if (status != OBELISK_RT_OK)
    return status;
  if (cursor > view.size)
    return OBELISK_RT_OK;
  uint64_t index = cursor;
  for (uint64_t position = 0; position != prefixSize; ++position) {
    if (scanSpace(prefix[position])) {
      while (index < view.size && scanSpace(view.bytes[index]))
        ++index;
      continue;
    }
    if (index >= view.size || view.bytes[index] != prefix[position])
      return OBELISK_RT_OK;
    ++index;
  }
  uint64_t extent = scanFieldExtent(view, index, specifier);
  if (extent == 0)
    return OBELISK_RT_OK;
  status = createString(lane, view.bytes + index - extent, extent, outField);
  if (status != OBELISK_RT_OK)
    return status;
  *outCursor = static_cast<uint32_t>(index);
  *outOk = 1;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_parse_integer(
    obelisk_rt_string_v1 string, uint32_t radix, uint64_t *outValue) {
  if (!outValue || (radix != 2 && radix != 8 && radix != 10 && radix != 16))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outValue = 0;
  StringView view;
  obelisk_rt_status status = readString(string, view);
  if (status != OBELISK_RT_OK)
    return status;
  uint64_t index = 0;
  while (index < view.size &&
         (view.bytes[index] == ' ' || view.bytes[index] == '\t' ||
          view.bytes[index] == '\n' || view.bytes[index] == '\r' ||
          view.bytes[index] == '\f' || view.bytes[index] == '\v'))
    ++index;
  bool negative = false;
  if (index < view.size &&
      (view.bytes[index] == '+' || view.bytes[index] == '-')) {
    negative = view.bytes[index] == '-';
    ++index;
  }
  uint64_t value = 0;
  for (; index < view.size; ++index) {
    unsigned char character = static_cast<unsigned char>(view.bytes[index]);
    if (character == '_')
      continue;
    uint32_t digit = character >= '0' && character <= '9'
                         ? character - '0'
                     : character >= 'a' && character <= 'f'
                         ? character - 'a' + 10
                     : character >= 'A' && character <= 'F'
                         ? character - 'A' + 10
                         : UINT32_MAX;
    if (digit >= radix)
      break;
    value = value * radix + digit;
  }
  *outValue = negative ? uint64_t{0} - value : value;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_string_parse_real(obelisk_rt_string_v1 string,
                                double *outValue) {
  if (!outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outValue = 0.0;
  StringView view;
  obelisk_rt_status status = readString(string, view);
  if (status != OBELISK_RT_OK)
    return status;
  try {
    std::string spelling;
    spelling.reserve(static_cast<size_t>(view.size));
    for (uint64_t index = 0; index != view.size; ++index)
      if (view.bytes[index] != '_')
        spelling.push_back(view.bytes[index]);
    const char *begin = spelling.data();
    const char *end = begin + spelling.size();
    while (begin != end &&
           (*begin == ' ' || *begin == '\t' || *begin == '\n' ||
            *begin == '\r' || *begin == '\f' || *begin == '\v'))
      ++begin;
    double value = 0.0;
    auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
    if (parsed.ec == std::errc{})
      *outValue = value;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_format_integer(
    obelisk_rt_gc_lane_v1 *lane, uint64_t value, uint32_t radix,
    uint32_t isSigned, obelisk_rt_string_v1 *outString) {
  if (!outString || isSigned > 1 ||
      (radix != 2 && radix != 8 && radix != 10 && radix != 16))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outString = 0;
  char buffer[66];
  std::to_chars_result formatted;
  if (isSigned && radix == 10)
    formatted = std::to_chars(std::begin(buffer), std::end(buffer),
                              static_cast<int64_t>(value), radix);
  else
    formatted =
        std::to_chars(std::begin(buffer), std::end(buffer), value, radix);
  if (formatted.ec != std::errc{})
    return OBELISK_RT_OUT_OF_RESOURCES;
  return createString(lane, buffer,
                      static_cast<uint64_t>(formatted.ptr - buffer), outString);
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_format_real(
    obelisk_rt_gc_lane_v1 *lane, double value,
    obelisk_rt_string_v1 *outString) {
  if (!outString)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outString = 0;
  char buffer[128];
  auto formatted =
      std::to_chars(std::begin(buffer), std::end(buffer), value,
                    std::chars_format::general);
  if (formatted.ec != std::errc{})
    return OBELISK_RT_OUT_OF_RESOURCES;
  return createString(lane, buffer,
                      static_cast<uint64_t>(formatted.ptr - buffer), outString);
}

namespace {

obelisk_rt_status cloneContainerImpl(obelisk_rt_gc_lane_v1 *lane,
                                     obelisk_rt_object_v1 *container,
                                     obelisk_rt_object_v1 **outContainer);

obelisk_rt_status prepareElementValue(obelisk_rt_gc_lane_v1 *lane,
                                      obelisk_rt_context *context,
                                      const obelisk_rt_element_type_v1 *element,
                                      const void *value, const void *unknown,
                                      std::vector<uint8_t> &storage) {
  if (!lane || !element || !value ||
      ((element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) && !unknown))
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t stride = elementStride(element);
  if (stride > std::numeric_limits<size_t>::max())
    return OBELISK_RT_OUT_OF_RESOURCES;
  try {
    storage.assign(static_cast<size_t>(stride), 0);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  std::memcpy(storage.data(), value, static_cast<size_t>(element->value_size));
  if (element->flags & OBELISK_RT_ELEMENT_FOUR_STATE)
    std::memcpy(storage.data() + element->value_size, unknown,
                static_cast<size_t>(element->value_size));
  obelisk_rt_status status =
      validateManagedSlots(context, element, storage.data());
  if (status != OBELISK_RT_OK || !element->trace)
    return status;

  ValueRootEnvironment roots{context, storage.data(), 1, stride, element};
  ManagedRootProvider provider{};
  status = obelisk_rt_managed_roots_push(lane, &provider, enumerateValueRoots,
                                         &roots);
  if (status != OBELISK_RT_OK)
    return status;
  walkTraceSlots(storage.data(), element->trace,
                 [&](obelisk_rt_managed_word_v1 *word,
                     obelisk_rt_managed_slot_kind_v1 kind) {
                   bool exactContainer =
                       kind == OBELISK_RT_MANAGED_SLOT_CONTAINER;
                   bool candidateContainer =
                       (kind & OBELISK_RT_MANAGED_SLOT_CANDIDATE) != 0 &&
                       (kind & OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER) != 0;
                   if (status != OBELISK_RT_OK || !word || *word == 0 ||
                       (!exactContainer && !candidateContainer))
                     return;
                   if (candidateContainer) {
                     obelisk_rt_managed_word_v1 candidate =
                         obelisk_rt_v1_gc_candidate_root(
                         context, *word,
                         OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER);
                     if (candidate == 0)
                       return;
                   }
                   auto *slot =
                       reinterpret_cast<obelisk_rt_object_v1 **>(word);
                   if (obelisk_rt_managed_object_kind(*slot) !=
                           OBELISK_RT_MANAGED_CONTAINER)
                     return;
                   obelisk_rt_object_v1 *clone = nullptr;
                   status = cloneContainerImpl(lane, *slot, &clone);
                   if (status == OBELISK_RT_OK)
                     *slot = clone;
                 });
  obelisk_rt_status popStatus = obelisk_rt_managed_roots_pop(lane, &provider);
  return status == OBELISK_RT_OK ? popStatus : status;
}

struct NormalizedAssocKey {
  uint64_t hash = 0;
  uint64_t integral = 0;
  obelisk_rt_string_v1 string = 0;
  bool ignored = false;
};

uint64_t mixAssocHash(uint64_t value) {
  value ^= value >> 30;
  value *= UINT64_C(0xbf58476d1ce4e5b9);
  value ^= value >> 27;
  value *= UINT64_C(0x94d049bb133111eb);
  value ^= value >> 31;
  return value == emptyAssocHash ? UINT64_C(1) : value;
}

obelisk_rt_status normalizeAssocKey(obelisk_rt_context *context,
                                    const ContainerHeader &header,
                                    const obelisk_rt_assoc_key_v1 *key,
                                    NormalizedAssocKey &normalized) {
  if (!key || key->reserved != 0 || key->kind != header.keyKind ||
      key->width != header.keyWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (key->kind == OBELISK_RT_ASSOC_KEY_STRING) {
    if (key->value != 0 || key->unknown != 0 ||
        !stringBelongsTo(context, key->string))
      return OBELISK_RT_INVALID_HANDLE;
    StringView view;
    obelisk_rt_status status = readString(key->string, view);
    if (status != OBELISK_RT_OK)
      return status;
    normalized.hash = mixAssocHash(view.hash);
    normalized.string = key->string;
    return OBELISK_RT_OK;
  }
  uint64_t mask = header.keyWidth == 64
                      ? UINT64_MAX
                      : ((UINT64_C(1) << header.keyWidth) - 1);
  if ((key->unknown & mask) != 0) {
    normalized.ignored = true;
    return OBELISK_RT_OK;
  }
  normalized.integral = key->value & mask;
  normalized.hash = mixAssocHash(
      normalized.integral ^ (uint64_t(header.keyKind) << 56) ^ header.keyWidth);
  return OBELISK_RT_OK;
}

bool assocKeysEqual(const ContainerHeader &header, const AssocSlot &slot,
                    const NormalizedAssocKey &key) {
  if (header.keyKind != OBELISK_RT_ASSOC_KEY_STRING)
    return slot.integral == key.integral;
  StringView left;
  StringView right;
  return readString(slot.string, left) == OBELISK_RT_OK &&
         readString(key.string, right) == OBELISK_RT_OK &&
         left.size == right.size && left.hash == right.hash &&
         std::memcmp(left.bytes, right.bytes, static_cast<size_t>(left.size)) ==
             0;
}

std::optional<uint64_t> findAssocSlot(const ContainerHeader &header,
                                      uint8_t *data, uint64_t dataSize,
                                      const NormalizedAssocKey &key) {
  uint64_t stride = assocSlotStride(header.element);
  if (header.capacity == 0 || dataSize < header.capacity * stride)
    return std::nullopt;
  uint64_t index = key.hash & (header.capacity - 1);
  uint64_t distance = 0;
  while (distance < header.capacity) {
    auto *slot = reinterpret_cast<AssocSlot *>(data + index * stride);
    if (slot->hash == emptyAssocHash || slot->distance < distance)
      return std::nullopt;
    if (slot->hash == key.hash && assocKeysEqual(header, *slot, key))
      return index;
    index = (index + 1) & (header.capacity - 1);
    ++distance;
  }
  return std::nullopt;
}

obelisk_rt_status insertAssocCandidate(const ContainerHeader &header,
                                       uint8_t *data, uint64_t dataSize,
                                       std::vector<uint8_t> &candidate) {
  uint64_t stride = assocSlotStride(header.element);
  if (candidate.size() != stride || dataSize < header.capacity * stride)
    return OBELISK_RT_INVALID_HANDLE;
  auto *incoming = reinterpret_cast<AssocSlot *>(candidate.data());
  uint64_t index = incoming->hash & (header.capacity - 1);
  incoming->distance = 0;
  for (uint64_t probe = 0; probe != header.capacity; ++probe) {
    uint8_t *address = data + index * stride;
    auto *slot = reinterpret_cast<AssocSlot *>(address);
    if (slot->hash == emptyAssocHash) {
      std::memcpy(address, candidate.data(), static_cast<size_t>(stride));
      return OBELISK_RT_OK;
    }
    if (slot->distance < incoming->distance) {
      for (uint64_t byte = 0; byte != stride; ++byte)
        std::swap(address[byte], candidate[byte]);
      incoming = reinterpret_cast<AssocSlot *>(candidate.data());
    }
    ++incoming->distance;
    index = (index + 1) & (header.capacity - 1);
  }
  return OBELISK_RT_OUT_OF_RESOURCES;
}

obelisk_rt_status ensureAssocCapacity(obelisk_rt_gc_lane_v1 *lane,
                                      obelisk_rt_object_v1 *array,
                                      uint64_t required) {
  ScopedManagedRoot ownerRoot(lane, &array);
  if (ownerRoot.getStatus() != OBELISK_RT_OK)
    return ownerRoot.getStatus();
  while (true) {
    ContainerHeader snapshot;
    obelisk_rt_status status = snapshotHeader(array, snapshot);
    if (status != OBELISK_RT_OK ||
        snapshot.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
      return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
    if (snapshot.capacity != 0 && required <= snapshot.capacity * 3 / 4)
      return OBELISK_RT_OK;
    uint64_t capacity =
        snapshot.capacity == 0 ? 8 : nextPowerOfTwo(snapshot.capacity + 1);
    while (capacity != 0 && required > capacity * 3 / 4)
      capacity = nextPowerOfTwo(capacity + 1);
    if (capacity == 0)
      return OBELISK_RT_OUT_OF_RESOURCES;
    uint64_t stride = assocSlotStride(snapshot.element);
    obelisk_rt_object_v1 *replacement = nullptr;
    status = allocateBuffer(lane, capacity, stride, &replacement);
    if (status != OBELISK_RT_OK)
      return status;
    ScopedManagedRoot replacementRoot(lane, &replacement);
    if (replacementRoot.getStatus() != OBELISK_RT_OK)
      return replacementRoot.getStatus();
    std::vector<uint8_t> candidate;
    try {
      candidate.resize(static_cast<size_t>(stride));
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    struct Publish {
      ContainerHeader snapshot;
      obelisk_rt_object_v1 *replacement;
      uint64_t capacity;
      std::vector<uint8_t> *candidate;
      bool retry = false;
    } publish{snapshot, replacement, capacity, &candidate};
    status = obelisk_rt_managed_object_access(
        array, OBELISK_RT_MANAGED_CONTAINER,
        [](void *opaque, uint8_t *object,
           uint64_t extent) -> obelisk_rt_status {
          auto *publish = static_cast<Publish *>(opaque);
          if (extent != sizeof(ContainerHeader))
            return OBELISK_RT_INVALID_HANDLE;
          auto *header = reinterpret_cast<ContainerHeader *>(object);
          if (header->epoch != publish->snapshot.epoch ||
              header->buffer != publish->snapshot.buffer ||
              header->capacity != publish->snapshot.capacity) {
            publish->retry = true;
            return OBELISK_RT_OK;
          }
          uint64_t stride = assocSlotStride(header->element);
          ContainerHeader replacementHeader = *header;
          replacementHeader.capacity = publish->capacity;
          obelisk_rt_status rehash =
              accessBuffer(publish->replacement, [&](uint8_t *destination,
                                                     uint64_t destinationSize) {
                if (!header->buffer)
                  return OBELISK_RT_OK;
                return accessBuffer(header->buffer, [&](uint8_t *source,
                                                        uint64_t sourceSize) {
                  if (sourceSize < header->capacity * stride)
                    return OBELISK_RT_INVALID_HANDLE;
                  for (uint64_t index = 0; index != header->capacity; ++index) {
                    uint8_t *address = source + index * stride;
                    auto *slot = reinterpret_cast<AssocSlot *>(address);
                    if (slot->hash == emptyAssocHash)
                      continue;
                    std::memcpy(publish->candidate->data(), address,
                                static_cast<size_t>(stride));
                    obelisk_rt_status inserted = insertAssocCandidate(
                        replacementHeader, destination, destinationSize,
                        *publish->candidate);
                    if (inserted != OBELISK_RT_OK)
                      return inserted;
                  }
                  return OBELISK_RT_OK;
                });
              });
          if (rehash != OBELISK_RT_OK)
            return rehash;
          header->buffer = publish->replacement;
          header->capacity = publish->capacity;
          header->ordered = nullptr;
          ++header->epoch;
          return OBELISK_RT_OK;
        },
        &publish);
    if (status != OBELISK_RT_OK)
      return status;
    if (!publish.retry)
      return OBELISK_RT_OK;
  }
}

obelisk_rt_status snapshotValues(obelisk_rt_object_v1 *container,
                                 ContainerHeader &snapshot,
                                 std::vector<uint8_t> &values) {
  while (true) {
    obelisk_rt_status status = snapshotHeader(container, snapshot);
    if (status != OBELISK_RT_OK)
      return status;
    uint64_t bytes = 0;
    if (!multiplyFits(snapshot.size, elementStride(snapshot.element), bytes) ||
        bytes > std::numeric_limits<size_t>::max())
      return OBELISK_RT_OUT_OF_RESOURCES;
    try {
      values.assign(static_cast<size_t>(bytes), 0);
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    struct Copy {
      ContainerHeader snapshot;
      std::vector<uint8_t> *values;
      bool retry = false;
    } copy{snapshot, &values};
    status = obelisk_rt_managed_object_access(
        container, OBELISK_RT_MANAGED_CONTAINER,
        [](void *opaque, uint8_t *object,
           uint64_t extent) -> obelisk_rt_status {
          auto *copy = static_cast<Copy *>(opaque);
          if (extent != sizeof(ContainerHeader))
            return OBELISK_RT_INVALID_HANDLE;
          auto *current = reinterpret_cast<ContainerHeader *>(object);
          if (current->epoch != copy->snapshot.epoch ||
              current->size != copy->snapshot.size ||
              current->buffer != copy->snapshot.buffer) {
            copy->retry = true;
            return OBELISK_RT_OK;
          }
          if (current->size == 0)
            return OBELISK_RT_OK;
          uint64_t stride = elementStride(current->element);
          return accessBuffer(
              current->buffer, [&](uint8_t *source, uint64_t sourceSize) {
                if (sourceSize < current->capacity * stride)
                  return OBELISK_RT_INVALID_HANDLE;
                for (uint64_t index = 0; index != current->size; ++index)
                  std::memcpy(copy->values->data() + index * stride,
                              source + physicalIndex(*current, index) * stride,
                              static_cast<size_t>(stride));
                return OBELISK_RT_OK;
              });
        },
        &copy);
    if (status != OBELISK_RT_OK)
      return status;
    if (!copy.retry)
      return OBELISK_RT_OK;
  }
}

struct AssocCloneRoots {
  obelisk_rt_context *context;
  uint8_t *entries;
  uint64_t count;
  uint64_t stride;
  uint64_t valueOffset;
  obelisk_rt_assoc_key_kind_v1 keyKind;
  const obelisk_rt_element_type_v1 *element;
};

void enumerateAssocCloneRoots(void *opaque, ManagedRootVisit visit,
                              void *environment) {
  auto *roots = static_cast<AssocCloneRoots *>(opaque);
  for (uint64_t index = 0; index != roots->count; ++index) {
    auto *slot =
        reinterpret_cast<AssocSlot *>(roots->entries + index * roots->stride);
    if (roots->keyKind == OBELISK_RT_ASSOC_KEY_STRING)
      visit(environment,
            reinterpret_cast<obelisk_rt_object_v1 **>(&slot->string));
    enumerateTraceSlots(roots->context,
                        reinterpret_cast<uint8_t *>(slot) + roots->valueOffset,
                        roots->element->trace, visit, environment);
  }
}

obelisk_rt_status cloneAssocContainer(obelisk_rt_gc_lane_v1 *lane,
                                      obelisk_rt_object_v1 *container,
                                      const ContainerHeader &initial,
                                      obelisk_rt_object_v1 **outContainer) {
  uint64_t stride = assocSlotStride(initial.element);
  uint64_t valueOffset = assocValueOffset(initial.element);
  std::vector<uint8_t> entries;
  ContainerHeader snapshot;
  while (true) {
    obelisk_rt_status status = snapshotHeader(container, snapshot);
    if (status != OBELISK_RT_OK)
      return status;
    uint64_t bytes = 0;
    if (!multiplyFits(snapshot.size, stride, bytes) ||
        bytes > std::numeric_limits<size_t>::max())
      return OBELISK_RT_OUT_OF_RESOURCES;
    try {
      entries.assign(static_cast<size_t>(bytes), 0);
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    struct Copy {
      ContainerHeader snapshot;
      std::vector<uint8_t> *entries;
      uint64_t stride;
      bool retry = false;
    } copy{snapshot, &entries, stride};
    status = obelisk_rt_managed_object_access(
        container, OBELISK_RT_MANAGED_CONTAINER,
        [](void *opaque, uint8_t *object,
           uint64_t extent) -> obelisk_rt_status {
          auto *copy = static_cast<Copy *>(opaque);
          if (extent != sizeof(ContainerHeader))
            return OBELISK_RT_INVALID_HANDLE;
          auto *header = reinterpret_cast<ContainerHeader *>(object);
          if (header->epoch != copy->snapshot.epoch ||
              header->buffer != copy->snapshot.buffer ||
              header->size != copy->snapshot.size) {
            copy->retry = true;
            return OBELISK_RT_OK;
          }
          if (!header->buffer)
            return OBELISK_RT_OK;
          return accessBuffer(header->buffer, [&](uint8_t *data,
                                                  uint64_t size) {
            if (size < header->capacity * copy->stride)
              return OBELISK_RT_INVALID_HANDLE;
            uint64_t output = 0;
            for (uint64_t index = 0; index != header->capacity; ++index) {
              auto *slot =
                  reinterpret_cast<AssocSlot *>(data + index * copy->stride);
              if (slot->hash == emptyAssocHash)
                continue;
              std::memcpy(copy->entries->data() + output * copy->stride, slot,
                          static_cast<size_t>(copy->stride));
              ++output;
            }
            return output == header->size ? OBELISK_RT_OK
                                          : OBELISK_RT_INVALID_HANDLE;
          });
        },
        &copy);
    if (status != OBELISK_RT_OK)
      return status;
    if (!copy.retry)
      break;
  }

  AssocCloneRoots roots{obelisk_rt_managed_lane_context(lane), entries.data(),
                        snapshot.size, stride, valueOffset, snapshot.keyKind,
                        snapshot.element};
  ManagedRootProvider provider{};
  obelisk_rt_status status = obelisk_rt_managed_roots_push(
      lane, &provider, enumerateAssocCloneRoots, &roots);
  if (status != OBELISK_RT_OK)
    return status;
  obelisk_rt_object_v1 *result = nullptr;
  status = initializeAssoc(lane, snapshot.element, snapshot.keyKind,
                           snapshot.keyWidth, &result);
  ScopedManagedRoot resultRoot(lane, &result);
  if (status == OBELISK_RT_OK && resultRoot.getStatus() != OBELISK_RT_OK)
    status = resultRoot.getStatus();
  std::vector<uint8_t> defaultBytes;
  if (status == OBELISK_RT_OK && snapshot.hasDefault) {
    try {
      defaultBytes.resize(static_cast<size_t>(elementStride(snapshot.element)));
    } catch (const std::bad_alloc &) {
      status = OBELISK_RT_OUT_OF_MEMORY;
    }
    if (status == OBELISK_RT_OK)
      status = accessBuffer(
          snapshot.defaultValue, [&](uint8_t *data, uint64_t size) {
            if (size < defaultBytes.size())
              return OBELISK_RT_INVALID_HANDLE;
            std::memcpy(defaultBytes.data(), data, defaultBytes.size());
            return OBELISK_RT_OK;
          });
    if (status == OBELISK_RT_OK) {
      const void *unknown =
          snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE
              ? defaultBytes.data() + snapshot.element->value_size
              : nullptr;
      status = obelisk_rt_v1_assoc_set_default(
          lane, result, defaultBytes.data(), unknown);
    }
  }
  for (uint64_t index = 0; status == OBELISK_RT_OK && index != snapshot.size;
       ++index) {
    auto *slot = reinterpret_cast<AssocSlot *>(entries.data() + index * stride);
    obelisk_rt_assoc_key_v1 key{};
    key.kind = snapshot.keyKind;
    key.width = snapshot.keyWidth;
    key.value = slot->integral;
    key.string = slot->string;
    const uint8_t *value = reinterpret_cast<uint8_t *>(slot) + valueOffset;
    const void *unknown =
        snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE
            ? value + snapshot.element->value_size
            : nullptr;
    status = obelisk_rt_v1_assoc_write(lane, result, &key, value, unknown);
  }
  obelisk_rt_status providerPop = obelisk_rt_managed_roots_pop(lane, &provider);
  if (status == OBELISK_RT_OK)
    status = providerPop;
  if (status == OBELISK_RT_OK)
    *outContainer = result;
  return status;
}

obelisk_rt_status cloneContainerImpl(obelisk_rt_gc_lane_v1 *lane,
                                     obelisk_rt_object_v1 *container,
                                     obelisk_rt_object_v1 **outContainer) {
  if (!lane || !outContainer)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outContainer = nullptr;
  if (!container)
    return OBELISK_RT_OK;
  if (obelisk_rt_managed_object_context(container) !=
      obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  obelisk_rt_gc_root_v1 sourceRoot{};
  obelisk_rt_status status =
      obelisk_rt_v1_gc_root_push(lane, &sourceRoot, &container);
  if (status != OBELISK_RT_OK)
    return status;
  ContainerHeader snapshot{};
  status = snapshotHeader(container, snapshot);
  if (status == OBELISK_RT_OK &&
      snapshot.kind == OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY) {
    status = cloneAssocContainer(lane, container, snapshot, outContainer);
    obelisk_rt_status sourcePop = obelisk_rt_v1_gc_root_pop(lane, &sourceRoot);
    return status == OBELISK_RT_OK ? sourcePop : status;
  }
  std::vector<uint8_t> values;
  if (status == OBELISK_RT_OK)
    status = snapshotValues(container, snapshot, values);
  uint64_t stride =
      status == OBELISK_RT_OK ? elementStride(snapshot.element) : 0;
  ValueRootEnvironment roots{obelisk_rt_managed_lane_context(lane),
                             values.data(), snapshot.size, stride,
                             snapshot.element};
  ManagedRootProvider provider{};
  bool providerPushed = false;
  if (status == OBELISK_RT_OK && snapshot.element->trace) {
    status = obelisk_rt_managed_roots_push(lane, &provider, enumerateValueRoots,
                                           &roots);
    providerPushed = status == OBELISK_RT_OK;
  }
  if (status == OBELISK_RT_OK && snapshot.element->trace) {
    for (uint64_t index = 0; index != snapshot.size && status == OBELISK_RT_OK;
         ++index)
      walkTraceSlots(values.data() + index * stride, snapshot.element->trace,
                     [&](obelisk_rt_managed_word_v1 *word,
                         obelisk_rt_managed_slot_kind_v1 kind) {
                       bool exactContainer =
                           kind == OBELISK_RT_MANAGED_SLOT_CONTAINER;
                       bool candidateContainer =
                           (kind & OBELISK_RT_MANAGED_SLOT_CANDIDATE) != 0 &&
                           (kind & OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER) != 0;
                       if (status != OBELISK_RT_OK || !word || *word == 0 ||
                           (!exactContainer && !candidateContainer))
                         return;
                       if (candidateContainer) {
                         obelisk_rt_managed_word_v1 candidate =
                             obelisk_rt_v1_gc_candidate_root(
                             obelisk_rt_managed_lane_context(lane), *word,
                             OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER);
                         if (candidate == 0)
                           return;
                       }
                       auto *slot =
                           reinterpret_cast<obelisk_rt_object_v1 **>(word);
                       if (obelisk_rt_managed_object_kind(*slot) !=
                               OBELISK_RT_MANAGED_CONTAINER)
                         return;
                       obelisk_rt_object_v1 *nested = nullptr;
                       status = cloneContainerImpl(lane, *slot, &nested);
                       if (status == OBELISK_RT_OK)
                         *slot = nested;
                     });
  }
  obelisk_rt_object_v1 *result = nullptr;
  if (status == OBELISK_RT_OK)
    status = initializeContainer(lane, snapshot.kind, snapshot.element,
                                 snapshot.size, snapshot.bound, &result);
  if (status == OBELISK_RT_OK && snapshot.size != 0) {
    ContainerHeader destination;
    status = snapshotHeader(result, destination);
    if (status == OBELISK_RT_OK)
      status =
          accessBuffer(destination.buffer, [&](uint8_t *data, uint64_t size) {
            if (size < values.size())
              return OBELISK_RT_INVALID_HANDLE;
            std::memcpy(data, values.data(), values.size());
            return OBELISK_RT_OK;
          });
  }
  if (providerPushed) {
    obelisk_rt_status popStatus = obelisk_rt_managed_roots_pop(lane, &provider);
    if (status == OBELISK_RT_OK)
      status = popStatus;
  }
  obelisk_rt_status sourcePop = obelisk_rt_v1_gc_root_pop(lane, &sourceRoot);
  if (status == OBELISK_RT_OK) {
    *outContainer = result;
    return sourcePop;
  }
  return status;
}

} // namespace

obelisk_rt_status obelisk_rt_container_pattern(obelisk_rt_object_v1 *container,
                                               std::string &output,
                                               unsigned depth) {
  if (!container) {
    output += "'{}";
    return OBELISK_RT_OK;
  }
  if (depth >= 32) {
    output += "<recursive-container>";
    return OBELISK_RT_OK;
  }
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(container, snapshot);
  if (status != OBELISK_RT_OK)
    return status;
  if (!snapshot.element ||
      snapshot.element->value_size > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_DESIGN;

  size_t valueSize = static_cast<size_t>(snapshot.element->value_size);
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
  try {
    value.resize(valueSize);
    if (snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE)
      unknown.resize(valueSize);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }

  auto bit = [](const std::vector<uint8_t> &bytes, uint64_t index) {
    return index / 8 < bytes.size() &&
           ((bytes[static_cast<size_t>(index / 8)] >> (index % 8)) & 1) != 0;
  };
  auto appendString = [&](obelisk_rt_string_v1 string) {
    char inlineBytes[8];
    const char *bytes = nullptr;
    uint64_t size = 0;
    obelisk_rt_status stringStatus =
        obelisk_rt_v1_string_view(string, inlineBytes, &bytes, &size);
    if (stringStatus != OBELISK_RT_OK ||
        size > std::numeric_limits<size_t>::max())
      return stringStatus == OBELISK_RT_OK ? OBELISK_RT_OUT_OF_RESOURCES
                                           : stringStatus;
    output.push_back('"');
    for (size_t index = 0; index != static_cast<size_t>(size); ++index) {
      unsigned char character = static_cast<unsigned char>(bytes[index]);
      switch (character) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (character >= 0x20 && character < 0x7f)
          output.push_back(static_cast<char>(character));
        else {
          static constexpr char digits[] = "0123456789abcdef";
          output += "\\x";
          output.push_back(digits[character >> 4]);
          output.push_back(digits[character & 15]);
        }
      }
    }
    output.push_back('"');
    return OBELISK_RT_OK;
  };

  bool associative =
      snapshot.kind == OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY;
  std::vector<uint64_t> associativeOrder;
  if (associative && snapshot.size != 0) {
    try {
      associativeOrder.reserve(static_cast<size_t>(snapshot.size));
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    uint64_t stride = assocSlotStride(snapshot.element);
    status = accessBuffer(snapshot.buffer, [&](uint8_t *data, uint64_t size) {
      if (size < snapshot.capacity * stride)
        return OBELISK_RT_INVALID_HANDLE;
      for (uint64_t index = 0; index != snapshot.capacity; ++index) {
        auto *slot = reinterpret_cast<AssocSlot *>(data + index * stride);
        if (slot->hash != emptyAssocHash)
          associativeOrder.push_back(index);
      }
      if (associativeOrder.size() != snapshot.size)
        return OBELISK_RT_INVALID_HANDLE;
      std::sort(associativeOrder.begin(), associativeOrder.end(),
                [&](uint64_t leftIndex, uint64_t rightIndex) {
                  auto *left = reinterpret_cast<AssocSlot *>(
                      data + leftIndex * stride);
                  auto *right = reinterpret_cast<AssocSlot *>(
                      data + rightIndex * stride);
                  if (snapshot.keyKind == OBELISK_RT_ASSOC_KEY_STRING) {
                    StringView leftView;
                    StringView rightView;
                    if (readString(left->string, leftView) != OBELISK_RT_OK ||
                        readString(right->string, rightView) != OBELISK_RT_OK)
                      return leftIndex < rightIndex;
                    return compareViews(leftView, rightView, false) < 0;
                  }
                  if (snapshot.keyKind == OBELISK_RT_ASSOC_KEY_SIGNED)
                    return signedAssocValue(left->integral,
                                            snapshot.keyWidth) <
                           signedAssocValue(right->integral,
                                            snapshot.keyWidth);
                  return left->integral < right->integral;
                });
      return OBELISK_RT_OK;
    });
    if (status != OBELISK_RT_OK)
      return status;
  }

  output += "'{";
  for (uint64_t index = 0; index != snapshot.size; ++index) {
    if (index)
      output += ", ";
    if (associative) {
      uint64_t stride = assocSlotStride(snapshot.element);
      status = accessBuffer(snapshot.buffer, [&](uint8_t *data, uint64_t size) {
        uint64_t slotIndex = associativeOrder[static_cast<size_t>(index)];
        if (slotIndex >= snapshot.capacity ||
            size < snapshot.capacity * stride)
          return OBELISK_RT_INVALID_HANDLE;
        auto *slot =
            reinterpret_cast<AssocSlot *>(data + slotIndex * stride);
        if (slot->hash == emptyAssocHash)
          return OBELISK_RT_INVALID_HANDLE;
        if (snapshot.keyKind == OBELISK_RT_ASSOC_KEY_STRING) {
          obelisk_rt_status keyStatus = appendString(slot->string);
          if (keyStatus != OBELISK_RT_OK)
            return keyStatus;
        } else if (snapshot.keyKind == OBELISK_RT_ASSOC_KEY_SIGNED) {
          output += std::to_string(
              signedAssocValue(slot->integral, snapshot.keyWidth));
        } else {
          output += std::to_string(slot->integral);
        }
        output.push_back(':');
        uint8_t *stored = data + slotIndex * stride +
                          assocValueOffset(snapshot.element);
        std::memcpy(value.data(), stored, valueSize);
        if (!unknown.empty())
          std::memcpy(unknown.data(), stored + valueSize, valueSize);
        return OBELISK_RT_OK;
      });
    } else {
      status = obelisk_rt_v1_container_read(
          container, static_cast<int64_t>(index), value.data(),
          unknown.empty() ? nullptr : unknown.data());
    }
    if (status != OBELISK_RT_OK)
      return status;
    switch (snapshot.element->kind) {
    case OBELISK_RT_ELEMENT_BITS:
    case OBELISK_RT_ELEMENT_LOGIC:
      output += std::to_string(snapshot.element->bit_width);
      output.push_back('\'');
      if (snapshot.element->flags & OBELISK_RT_ELEMENT_SIGNED)
        output.push_back('s');
      output.push_back('b');
      for (uint64_t position = snapshot.element->bit_width; position != 0;
           --position) {
        uint64_t selected = position - 1;
        if (!unknown.empty() && bit(unknown, selected))
          output.push_back(bit(value, selected) ? 'z' : 'x');
        else
          output.push_back(bit(value, selected) ? '1' : '0');
      }
      break;
    case OBELISK_RT_ELEMENT_REAL: {
      double real = 0.0;
      if (valueSize == sizeof(double))
        std::memcpy(&real, value.data(), sizeof(real));
      else if (valueSize == sizeof(float)) {
        float shortReal = 0.0f;
        std::memcpy(&shortReal, value.data(), sizeof(shortReal));
        real = shortReal;
      } else {
        return OBELISK_RT_INVALID_DESIGN;
      }
      char buffer[64];
      int length = std::snprintf(buffer, sizeof(buffer), "%g", real);
      if (length <= 0 || static_cast<size_t>(length) >= sizeof(buffer))
        return OBELISK_RT_OUT_OF_RESOURCES;
      output.append(buffer, static_cast<size_t>(length));
      break;
    }
    case OBELISK_RT_ELEMENT_STRING: {
      obelisk_rt_string_v1 string = 0;
      if (valueSize != sizeof(string))
        return OBELISK_RT_INVALID_DESIGN;
      std::memcpy(&string, value.data(), sizeof(string));
      status = appendString(string);
      if (status != OBELISK_RT_OK)
        return status;
      break;
    }
    case OBELISK_RT_ELEMENT_CLASS_HANDLE: {
      obelisk_rt_object_v1 *object = nullptr;
      if (valueSize != sizeof(object))
        return OBELISK_RT_INVALID_DESIGN;
      std::memcpy(&object, value.data(), sizeof(object));
      output += object ? "<object>" : "null";
      break;
    }
    case OBELISK_RT_ELEMENT_CONTAINER_HANDLE: {
      obelisk_rt_object_v1 *nested = nullptr;
      if (valueSize != sizeof(nested))
        return OBELISK_RT_INVALID_DESIGN;
      std::memcpy(&nested, value.data(), sizeof(nested));
      status = obelisk_rt_container_pattern(nested, output, depth + 1);
      if (status != OBELISK_RT_OK)
        return status;
      break;
    }
    case OBELISK_RT_ELEMENT_EVENT: {
      uint64_t event = UINT64_MAX;
      if (valueSize != sizeof(event))
        return OBELISK_RT_INVALID_DESIGN;
      std::memcpy(&event, value.data(), sizeof(event));
      if (event == UINT64_MAX)
        output += "null";
      else {
        output += "<event:";
        output += std::to_string(event);
        output.push_back('>');
      }
      break;
    }
    case OBELISK_RT_ELEMENT_AGGREGATE:
      output += "'h";
      for (size_t position = value.size(); position != 0; --position) {
        static constexpr char digits[] = "0123456789abcdef";
        uint8_t byte = value[position - 1];
        output.push_back(digits[byte >> 4]);
        output.push_back(digits[byte & 15]);
      }
      break;
    default:
      return OBELISK_RT_INVALID_DESIGN;
    }
  }
  output.push_back('}');
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_dynamic_array_create(
    obelisk_rt_gc_lane_v1 *lane, const obelisk_rt_element_type_v1 *elementType,
    uint64_t size, obelisk_rt_object_v1 **outArray) {
  return initializeContainer(lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY,
                             elementType, size, 0, outArray);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_queue_create(obelisk_rt_gc_lane_v1 *lane,
                           const obelisk_rt_element_type_v1 *elementType,
                           uint64_t bound, obelisk_rt_object_v1 **outQueue) {
  return initializeContainer(lane, OBELISK_RT_CONTAINER_QUEUE, elementType, 0,
                             bound, outQueue);
}

extern "C" obelisk_rt_status obelisk_rt_v1_container_create_like(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *preferred,
    obelisk_rt_object_v1 *fallback, uint64_t size,
    obelisk_rt_object_v1 **outContainer) {
  if (!lane || !outContainer)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outContainer = nullptr;
  obelisk_rt_context *context = obelisk_rt_managed_lane_context(lane);
  if ((preferred &&
       obelisk_rt_managed_object_context(preferred) != context) ||
      (fallback && obelisk_rt_managed_object_context(fallback) != context))
    return OBELISK_RT_INVALID_HANDLE;

  ContainerHeader selected{};
  bool haveSelected = false;
  for (obelisk_rt_object_v1 *source : {preferred, fallback}) {
    if (!source)
      continue;
    ContainerHeader current{};
    obelisk_rt_status status = snapshotHeader(source, current);
    if (status != OBELISK_RT_OK)
      return status;
    if (!haveSelected) {
      selected = current;
      haveSelected = true;
      continue;
    }
    if (current.kind != selected.kind ||
        current.element->type_id != selected.element->type_id ||
        current.bound != selected.bound)
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  if (!haveSelected)
    return OBELISK_RT_OK;
  return initializeContainer(lane, selected.kind, selected.element, size,
                             selected.bound, outContainer);
}

extern "C" obelisk_rt_status obelisk_rt_v1_container_create_typed(
    obelisk_rt_gc_lane_v1 *lane, uint32_t containerKind, uint64_t typeID,
    uint32_t elementKind, uint32_t elementFlags, uint64_t valueSize,
    uint64_t alignment, uint64_t bitWidth,
    const obelisk_rt_element_trace_slot_v1 *traceSlots, uint64_t traceSlotCount,
    uint64_t size, uint64_t bound, obelisk_rt_object_v1 **outContainer) {
  if (!lane || !outContainer || typeID == 0 ||
      (traceSlotCount != 0 && !traceSlots) ||
      traceSlotCount >
          std::numeric_limits<size_t>::max() - (elementKind != 0 ? 1 : 0))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outContainer = nullptr;
  if (containerKind != OBELISK_RT_CONTAINER_DYNAMIC_ARRAY &&
      containerKind != OBELISK_RT_CONTAINER_QUEUE)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (containerKind == OBELISK_RT_CONTAINER_DYNAMIC_ARRAY &&
      size > uint64_t{INT64_MAX})
    return OBELISK_RT_INVALID_ARGUMENT;

  std::unique_ptr<OwnedElementTypeDescriptor> owned;
  try {
    owned = std::make_unique<OwnedElementTypeDescriptor>();
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  owned->descriptor = {
      OBELISK_RT_VERSION,
      static_cast<obelisk_rt_element_kind_v1>(elementKind),
      typeID,
      elementFlags,
      0,
      valueSize,
      alignment,
      bitWidth,
      nullptr,
  };
  uint32_t slotKind = 0;
  if (elementKind == OBELISK_RT_ELEMENT_CLASS_HANDLE)
    slotKind = OBELISK_RT_MANAGED_SLOT_CLASS;
  else if (elementKind == OBELISK_RT_ELEMENT_STRING)
    slotKind = OBELISK_RT_MANAGED_SLOT_STRING;
  else if (elementKind == OBELISK_RT_ELEMENT_CONTAINER_HANDLE)
    slotKind = OBELISK_RT_MANAGED_SLOT_CONTAINER;
  if (slotKind && traceSlotCount != 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (elementKind != OBELISK_RT_ELEMENT_AGGREGATE && traceSlotCount != 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    owned->entries.reserve(
        static_cast<size_t>(traceSlotCount + (slotKind != 0)));
    if (slotKind)
      owned->entries.push_back(
          {0, 0, 1, OBELISK_RT_TRACE_STRONG, slotKind, nullptr});
    for (uint64_t index = 0; index != traceSlotCount; ++index) {
      obelisk_rt_element_trace_slot_v1 slot{};
      std::memcpy(&slot, traceSlots + index, sizeof(slot));
      // The descriptor validator below is the authority for exact and
      // candidate slot kinds. Keep this local check limited to the flattened
      // trace representation and ordering so the two validators cannot drift.
      if (slot.reserved != 0 || slot.offset > valueSize ||
          sizeof(obelisk_rt_managed_word_v1) > valueSize - slot.offset ||
          slot.offset % alignof(obelisk_rt_managed_word_v1) != 0 ||
          (!owned->entries.empty() &&
           slot.offset <= owned->entries.back().offset))
        return OBELISK_RT_INVALID_ARGUMENT;
      owned->entries.push_back(
          {slot.offset, 0, 1, OBELISK_RT_TRACE_STRONG, slot.kind, nullptr});
    }
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  if (!owned->entries.empty()) {
    owned->trace = {
        OBELISK_RT_VERSION,   0, valueSize, alignment, owned->entries.data(),
        owned->entries.size()};
    owned->descriptor.trace = &owned->trace;
  }
  if (obelisk_rt_v1_element_type_validate(&owned->descriptor) != OBELISK_RT_OK)
    return OBELISK_RT_INVALID_ARGUMENT;

  obelisk_rt_context *context = obelisk_rt_managed_lane_context(lane);
  const obelisk_rt_element_type_v1 *element = nullptr;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto found = context->managedElementTypes.find(typeID);
    if (found != context->managedElementTypes.end()) {
      obelisk_rt_status status =
          obelisk_rt_v1_element_type_register(context, &owned->descriptor);
      if (status != OBELISK_RT_OK)
        return status;
      element = found->second;
    } else {
      auto [stored, inserted] = context->managedOwnedElementTypes.try_emplace(
          typeID, std::move(owned));
      if (!inserted)
        return OBELISK_RT_INVALID_DESIGN;
      const obelisk_rt_element_type_v1 *descriptor =
          &stored->second->descriptor;
      obelisk_rt_status status =
          obelisk_rt_v1_element_type_register(context, descriptor);
      if (status != OBELISK_RT_OK) {
        context->managedOwnedElementTypes.erase(stored);
        return status;
      }
      element = descriptor;
    }
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  return initializeContainer(
      lane, static_cast<obelisk_rt_container_kind_v1>(containerKind), element,
      containerKind == OBELISK_RT_CONTAINER_QUEUE ? 0 : size, bound,
      outContainer);
}

extern "C" obelisk_rt_status obelisk_rt_v1_assoc_create_typed(
    obelisk_rt_gc_lane_v1 *lane, uint64_t typeID, uint32_t elementKind,
    uint32_t elementFlags, uint64_t valueSize, uint64_t alignment,
    uint64_t bitWidth,
    const obelisk_rt_element_trace_slot_v1 *traceSlots, uint64_t traceSlotCount,
    obelisk_rt_assoc_key_kind_v1 keyKind, uint64_t keyWidth,
    obelisk_rt_object_v1 **outArray) {
  if (!outArray)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outArray = nullptr;
  obelisk_rt_object_v1 *registration = nullptr;
  obelisk_rt_status status = obelisk_rt_v1_container_create_typed(
      lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, typeID, elementKind,
      elementFlags, valueSize, alignment, bitWidth, traceSlots, traceSlotCount,
      0, 0, &registration);
  if (status != OBELISK_RT_OK)
    return status;
  ContainerHeader registered;
  status = snapshotHeader(registration, registered);
  if (status != OBELISK_RT_OK)
    return status;
  return initializeAssoc(lane, registered.element, keyKind, keyWidth, outArray);
}

extern "C" uint64_t
obelisk_rt_v1_container_size(obelisk_rt_object_v1 *container) {
  if (!container)
    return 0;
  ContainerHeader snapshot;
  return snapshotHeader(container, snapshot) == OBELISK_RT_OK ? snapshot.size
                                                              : 0;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_container_read(obelisk_rt_object_v1 *container, int64_t index,
                             void *outValue, void *outUnknown) {
  if (!container || !outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(container, snapshot);
  if (status != OBELISK_RT_OK)
    return status;
  if (snapshot.kind == OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return OBELISK_RT_INVALID_ARGUMENT;
  initializeElementDefault(snapshot.element, outValue, outUnknown);
  if (snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) {
    if (!outUnknown)
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  if (index < 0 || static_cast<uint64_t>(index) >= snapshot.size)
    return OBELISK_RT_OK;
  struct Read {
    int64_t index;
    void *value;
    void *unknown;
  } read{index, outValue, outUnknown};
  return obelisk_rt_managed_object_access(
      container, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        auto *read = static_cast<Read *>(opaque);
        if (read->index < 0 ||
            static_cast<uint64_t>(read->index) >= header->size)
          return OBELISK_RT_OK;
        uint64_t stride = elementStride(header->element);
        return accessBuffer(header->buffer, [&](uint8_t *data, uint64_t size) {
          uint64_t offset =
              physicalIndex(*header, static_cast<uint64_t>(read->index)) *
              stride;
          if (offset > size || stride > size - offset)
            return OBELISK_RT_INVALID_HANDLE;
          std::memcpy(read->value, data + offset,
                      static_cast<size_t>(header->element->value_size));
          if (header->element->flags & OBELISK_RT_ELEMENT_FOUR_STATE)
            std::memcpy(read->unknown,
                        data + offset + header->element->value_size,
                        static_cast<size_t>(header->element->value_size));
          return OBELISK_RT_OK;
        });
      },
      &read);
}

extern "C" obelisk_rt_status obelisk_rt_v1_container_read_checked(
    obelisk_rt_object_v1 *container, int64_t index, void *outValue,
    uint64_t valueSize, void *outUnknown, uint64_t unknownSize) {
  if (!container || !outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(container, snapshot);
  if (status != OBELISK_RT_OK)
    return status;
  bool fourState =
      (snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0;
  if (valueSize < snapshot.element->value_size ||
      (fourState && unknownSize < snapshot.element->value_size) ||
      (!fourState && unknownSize != 0) ||
      fourState != (outUnknown != nullptr))
    return OBELISK_RT_INVALID_ARGUMENT;
  return obelisk_rt_v1_container_read(container, index, outValue, outUnknown);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_dynamic_array_resize(obelisk_rt_gc_lane_v1 *lane,
                                   obelisk_rt_object_v1 *array,
                                   uint64_t newSize) {
  if (!lane || !array)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (obelisk_rt_managed_object_context(array) !=
      obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(array, snapshot);
  if (status != OBELISK_RT_OK ||
      snapshot.kind != OBELISK_RT_CONTAINER_DYNAMIC_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  if (newSize > snapshot.capacity) {
    status = ensureCapacity(lane, array, newSize);
    if (status != OBELISK_RT_OK)
      return status;
  }
  struct Resize {
    uint64_t size;
  } resize{newSize};
  return obelisk_rt_managed_object_access(
      array, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        auto *resize = static_cast<Resize *>(opaque);
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        if (header->kind != OBELISK_RT_CONTAINER_DYNAMIC_ARRAY ||
            resize->size > header->capacity)
          return OBELISK_RT_INVALID_ARGUMENT;
        uint64_t oldSize = header->size;
        if (resize->size > oldSize) {
          uint64_t stride = elementStride(header->element);
          obelisk_rt_status status =
              accessBuffer(header->buffer, [&](uint8_t *data, uint64_t size) {
                uint64_t offset = oldSize * stride;
                uint64_t bytes = (resize->size - oldSize) * stride;
                if (offset > size || bytes > size - offset)
                  return OBELISK_RT_INVALID_HANDLE;
                if (header->element->kind == OBELISK_RT_ELEMENT_EVENT)
                  std::memset(data + offset, 0xff, static_cast<size_t>(bytes));
                else
                  std::memset(data + offset, 0, static_cast<size_t>(bytes));
                return OBELISK_RT_OK;
              });
          if (status != OBELISK_RT_OK)
            return status;
        }
        header->size = resize->size;
        ++header->epoch;
        return OBELISK_RT_OK;
      },
      &resize);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_container_write(obelisk_rt_gc_lane_v1 *lane,
                              obelisk_rt_object_v1 *container, int64_t index,
                              const void *value, const void *unknown) {
  if (!lane || !container)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (obelisk_rt_managed_object_context(container) !=
      obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  ScopedManagedRoot ownerRoot(lane, &container);
  if (ownerRoot.getStatus() != OBELISK_RT_OK)
    return ownerRoot.getStatus();
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(container, snapshot);
  if (status != OBELISK_RT_OK)
    return status;
  if (snapshot.kind == OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (index < 0 || static_cast<uint64_t>(index) > snapshot.size)
    return OBELISK_RT_OK;
  bool append = snapshot.kind == OBELISK_RT_CONTAINER_QUEUE &&
                static_cast<uint64_t>(index) == snapshot.size;
  if (!append && static_cast<uint64_t>(index) >= snapshot.size)
    return OBELISK_RT_OK;
  if (append) {
    if (queueIsFull(snapshot))
      return OBELISK_RT_OK;
    status = ensureCapacity(lane, container, snapshot.size + 1);
    if (status != OBELISK_RT_OK)
      return status;
  }
  std::vector<uint8_t> prepared;
  status =
      prepareElementValue(lane, obelisk_rt_managed_object_context(container),
                          snapshot.element, value, unknown, prepared);
  if (status != OBELISK_RT_OK)
    return status;
  struct Write {
    int64_t index;
    const std::vector<uint8_t> *value;
    bool append;
  } write{index, &prepared, append};
  return obelisk_rt_managed_object_access(
      container, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        auto *write = static_cast<Write *>(opaque);
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        if (write->index < 0)
          return OBELISK_RT_OK;
        uint64_t index = static_cast<uint64_t>(write->index);
        if (write->append) {
          if (index != header->size || header->size >= header->capacity ||
              queueIsFull(*header))
            return OBELISK_RT_INVALID_LIFECYCLE;
          ++header->size;
        } else if (index >= header->size) {
          return OBELISK_RT_OK;
        }
        uint64_t stride = elementStride(header->element);
        obelisk_rt_status status =
            accessBuffer(header->buffer, [&](uint8_t *data, uint64_t size) {
              uint64_t offset = physicalIndex(*header, index) * stride;
              if (offset > size || stride > size - offset)
                return OBELISK_RT_INVALID_HANDLE;
              std::memcpy(data + offset, write->value->data(),
                          static_cast<size_t>(stride));
              return OBELISK_RT_OK;
            });
        if (status == OBELISK_RT_OK)
          ++header->epoch;
        return status;
      },
      &write);
}

extern "C" obelisk_rt_status obelisk_rt_v1_container_write_checked(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *container,
    int64_t index, const void *value, uint64_t valueSize,
    const void *unknown, uint64_t unknownSize) {
  if (!lane || !container || !value)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(container, snapshot);
  if (status != OBELISK_RT_OK)
    return status;
  bool fourState =
      (snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0;
  if (valueSize < snapshot.element->value_size ||
      (fourState && unknownSize < snapshot.element->value_size) ||
      (!fourState && unknownSize != 0) ||
      fourState != (unknown != nullptr))
    return OBELISK_RT_INVALID_ARGUMENT;
  return obelisk_rt_v1_container_write(lane, container, index, value, unknown);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_container_clone(obelisk_rt_gc_lane_v1 *lane,
                              obelisk_rt_object_v1 *container,
                              obelisk_rt_object_v1 **outContainer) {
  return cloneContainerImpl(lane, container, outContainer);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_container_delete(obelisk_rt_object_v1 *container) {
  if (!container)
    return OBELISK_RT_OK;
  return obelisk_rt_managed_object_access(
      container, OBELISK_RT_MANAGED_CONTAINER,
      [](void *, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        header->buffer = nullptr;
        header->ordered = nullptr;
        header->defaultValue = nullptr;
        header->hasDefault = 0;
        header->size = 0;
        header->capacity = 0;
        header->head = 0;
        ++header->epoch;
        return OBELISK_RT_OK;
      },
      nullptr);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_queue_push(obelisk_rt_gc_lane_v1 *lane,
                         obelisk_rt_object_v1 *queue, uint32_t front,
                         const void *value, const void *unknown) {
  if (!lane || !queue || front > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (obelisk_rt_managed_object_context(queue) !=
      obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  ScopedManagedRoot ownerRoot(lane, &queue);
  if (ownerRoot.getStatus() != OBELISK_RT_OK)
    return ownerRoot.getStatus();
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(queue, snapshot);
  if (status != OBELISK_RT_OK || snapshot.kind != OBELISK_RT_CONTAINER_QUEUE)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  if (queueIsFull(snapshot))
    return OBELISK_RT_OK;
  status = ensureCapacity(lane, queue, snapshot.size + 1);
  if (status != OBELISK_RT_OK)
    return status;
  std::vector<uint8_t> prepared;
  status = prepareElementValue(lane, obelisk_rt_managed_object_context(queue),
                               snapshot.element, value, unknown, prepared);
  if (status != OBELISK_RT_OK)
    return status;
  struct Push {
    uint32_t front;
    const std::vector<uint8_t> *value;
  } push{front, &prepared};
  return obelisk_rt_managed_object_access(
      queue, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        auto *push = static_cast<Push *>(opaque);
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        if (header->kind != OBELISK_RT_CONTAINER_QUEUE ||
            header->size >= header->capacity || queueIsFull(*header))
          return OBELISK_RT_INVALID_LIFECYCLE;
        if (push->front)
          header->head = (header->head - 1) & (header->capacity - 1);
        uint64_t physical =
            push->front ? header->head : physicalIndex(*header, header->size);
        uint64_t stride = elementStride(header->element);
        obelisk_rt_status status =
            accessBuffer(header->buffer, [&](uint8_t *data, uint64_t size) {
              uint64_t offset = physical * stride;
              if (offset > size || stride > size - offset)
                return OBELISK_RT_INVALID_HANDLE;
              std::memcpy(data + offset, push->value->data(),
                          static_cast<size_t>(stride));
              return OBELISK_RT_OK;
            });
        if (status == OBELISK_RT_OK) {
          ++header->size;
          ++header->epoch;
        }
        return status;
      },
      &push);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_queue_pop(obelisk_rt_object_v1 *queue, uint32_t front,
                        void *outValue, void *outUnknown,
                        uint32_t *outPresent) {
  if (front > 1 || !outValue || !outPresent)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outPresent = 0;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(queue, snapshot);
  if (status != OBELISK_RT_OK || snapshot.kind != OBELISK_RT_CONTAINER_QUEUE)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  if (snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) {
    if (!outUnknown)
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  initializeElementDefault(snapshot.element, outValue, outUnknown);
  struct Pop {
    uint32_t front;
    void *value;
    void *unknown;
    uint32_t *present;
  } pop{front, outValue, outUnknown, outPresent};
  return obelisk_rt_managed_object_access(
      queue, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        auto *pop = static_cast<Pop *>(opaque);
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        if (header->kind != OBELISK_RT_CONTAINER_QUEUE)
          return OBELISK_RT_INVALID_ARGUMENT;
        if (header->size == 0)
          return OBELISK_RT_OK;
        uint64_t logical = pop->front ? 0 : header->size - 1;
        uint64_t stride = elementStride(header->element);
        obelisk_rt_status status =
            accessBuffer(header->buffer, [&](uint8_t *data, uint64_t size) {
              uint64_t offset = physicalIndex(*header, logical) * stride;
              if (offset > size || stride > size - offset)
                return OBELISK_RT_INVALID_HANDLE;
              std::memcpy(pop->value, data + offset,
                          static_cast<size_t>(header->element->value_size));
              if (header->element->flags & OBELISK_RT_ELEMENT_FOUR_STATE)
                std::memcpy(pop->unknown,
                            data + offset + header->element->value_size,
                            static_cast<size_t>(header->element->value_size));
              std::memset(data + offset, 0, static_cast<size_t>(stride));
              return OBELISK_RT_OK;
            });
        if (status == OBELISK_RT_OK) {
          if (pop->front)
            header->head = (header->head + 1) & (header->capacity - 1);
          --header->size;
          ++header->epoch;
          *pop->present = 1;
        }
        return status;
      },
      &pop);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_queue_insert(obelisk_rt_gc_lane_v1 *lane,
                           obelisk_rt_object_v1 *queue, int64_t index,
                           const void *value, const void *unknown) {
  if (!lane || !queue)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (obelisk_rt_managed_object_context(queue) !=
      obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  if (index < 0)
    return OBELISK_RT_OK;
  ScopedManagedRoot ownerRoot(lane, &queue);
  if (ownerRoot.getStatus() != OBELISK_RT_OK)
    return ownerRoot.getStatus();
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(queue, snapshot);
  if (status != OBELISK_RT_OK || snapshot.kind != OBELISK_RT_CONTAINER_QUEUE)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  if (static_cast<uint64_t>(index) > snapshot.size || queueIsFull(snapshot))
    return OBELISK_RT_OK;
  status = ensureCapacity(lane, queue, snapshot.size + 1);
  if (status != OBELISK_RT_OK)
    return status;
  std::vector<uint8_t> prepared;
  status = prepareElementValue(lane, obelisk_rt_managed_object_context(queue),
                               snapshot.element, value, unknown, prepared);
  if (status != OBELISK_RT_OK)
    return status;
  struct Insert {
    uint64_t index;
    const std::vector<uint8_t> *value;
  } insert{static_cast<uint64_t>(index), &prepared};
  return obelisk_rt_managed_object_access(
      queue, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        auto *insert = static_cast<Insert *>(opaque);
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        if (header->kind != OBELISK_RT_CONTAINER_QUEUE ||
            insert->index > header->size || header->size >= header->capacity)
          return OBELISK_RT_INVALID_LIFECYCLE;
        uint64_t stride = elementStride(header->element);
        obelisk_rt_status status =
            accessBuffer(header->buffer, [&](uint8_t *data, uint64_t size) {
              if (size < header->capacity * stride)
                return OBELISK_RT_INVALID_HANDLE;
              for (uint64_t logical = header->size; logical > insert->index;
                   --logical)
                std::memcpy(data + physicalIndex(*header, logical) * stride,
                            data + physicalIndex(*header, logical - 1) * stride,
                            static_cast<size_t>(stride));
              std::memcpy(data + physicalIndex(*header, insert->index) * stride,
                          insert->value->data(), static_cast<size_t>(stride));
              return OBELISK_RT_OK;
            });
        if (status == OBELISK_RT_OK) {
          ++header->size;
          ++header->epoch;
        }
        return status;
      },
      &insert);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_queue_delete_index(obelisk_rt_object_v1 *queue, int64_t index) {
  if (index < 0)
    return OBELISK_RT_OK;
  struct Delete {
    uint64_t index;
  } remove{static_cast<uint64_t>(index)};
  return obelisk_rt_managed_object_access(
      queue, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        auto *remove = static_cast<Delete *>(opaque);
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        if (header->kind != OBELISK_RT_CONTAINER_QUEUE)
          return OBELISK_RT_INVALID_ARGUMENT;
        if (remove->index >= header->size)
          return OBELISK_RT_OK;
        uint64_t stride = elementStride(header->element);
        obelisk_rt_status status =
            accessBuffer(header->buffer, [&](uint8_t *data, uint64_t size) {
              if (size < header->capacity * stride)
                return OBELISK_RT_INVALID_HANDLE;
              for (uint64_t logical = remove->index; logical + 1 < header->size;
                   ++logical)
                std::memcpy(data + physicalIndex(*header, logical) * stride,
                            data + physicalIndex(*header, logical + 1) * stride,
                            static_cast<size_t>(stride));
              std::memset(data +
                              physicalIndex(*header, header->size - 1) * stride,
                          0, static_cast<size_t>(stride));
              return OBELISK_RT_OK;
            });
        if (status == OBELISK_RT_OK) {
          --header->size;
          ++header->epoch;
        }
        return status;
      },
      &remove);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_assoc_create(obelisk_rt_gc_lane_v1 *lane,
                           const obelisk_rt_element_type_v1 *elementType,
                           obelisk_rt_assoc_key_kind_v1 keyKind,
                           uint64_t keyWidth, obelisk_rt_object_v1 **outArray) {
  return initializeAssoc(lane, elementType, keyKind, keyWidth, outArray);
}

static void warnIgnoredAssocKey() {
  std::fputs("warning: associative array operation ignored because the key "
             "contains X or Z bits\n",
             stderr);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_assoc_exists(obelisk_rt_object_v1 *array,
                           const obelisk_rt_assoc_key_v1 *key,
                           uint32_t *outExists) {
  if (!array || !outExists)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outExists = 0;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(array, snapshot);
  if (status != OBELISK_RT_OK ||
      snapshot.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  NormalizedAssocKey normalized;
  status = normalizeAssocKey(obelisk_rt_managed_object_context(array), snapshot,
                             key, normalized);
  if (status != OBELISK_RT_OK)
    return status;
  if (normalized.ignored) {
    warnIgnoredAssocKey();
    return OBELISK_RT_OK;
  }
  struct Exists {
    NormalizedAssocKey key;
    uint32_t *result;
  } exists{normalized, outExists};
  return obelisk_rt_managed_object_access(
      array, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        auto *exists = static_cast<Exists *>(opaque);
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        if (header->kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY ||
            !header->buffer)
          return header->kind == OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY
                     ? OBELISK_RT_OK
                     : OBELISK_RT_INVALID_ARGUMENT;
        return accessBuffer(header->buffer, [&](uint8_t *data, uint64_t size) {
          *exists->result =
              findAssocSlot(*header, data, size, exists->key).has_value();
          return OBELISK_RT_OK;
        });
      },
      &exists);
}

extern "C" obelisk_rt_status obelisk_rt_v1_assoc_key_info(
    obelisk_rt_object_v1 *array, obelisk_rt_assoc_key_kind_v1 *outKind,
    uint64_t *outWidth) {
  if (!array || !outKind || !outWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(array, snapshot);
  if (status != OBELISK_RT_OK ||
      snapshot.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  *outKind = snapshot.keyKind;
  *outWidth = snapshot.keyWidth;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_assoc_read(obelisk_rt_object_v1 *array,
                         const obelisk_rt_assoc_key_v1 *key, void *outValue,
                         void *outUnknown, uint32_t *outPresent) {
  if (!array || !outValue || !outPresent)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outPresent = 0;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(array, snapshot);
  if (status != OBELISK_RT_OK ||
      snapshot.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  if (snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) {
    if (!outUnknown)
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  initializeElementDefault(snapshot.element, outValue, outUnknown);
  NormalizedAssocKey normalized;
  status = normalizeAssocKey(obelisk_rt_managed_object_context(array), snapshot,
                             key, normalized);
  if (status != OBELISK_RT_OK)
    return status;
  if (normalized.ignored) {
    warnIgnoredAssocKey();
    return OBELISK_RT_OK;
  }
  struct Read {
    NormalizedAssocKey key;
    void *value;
    void *unknown;
    uint32_t *present;
  } read{normalized, outValue, outUnknown, outPresent};
  return obelisk_rt_managed_object_access(
      array, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        auto *read = static_cast<Read *>(opaque);
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        if (header->kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
          return OBELISK_RT_INVALID_ARGUMENT;
        auto readDefault = [&]() -> obelisk_rt_status {
          if (!header->hasDefault)
            return OBELISK_RT_OK;
          if (!header->defaultValue)
            return OBELISK_RT_INVALID_HANDLE;
          return accessBuffer(
              header->defaultValue, [&](uint8_t *data, uint64_t size) {
                uint64_t stride = elementStride(header->element);
                if (size < stride)
                  return OBELISK_RT_INVALID_HANDLE;
                std::memcpy(read->value, data,
                            static_cast<size_t>(header->element->value_size));
                if (header->element->flags & OBELISK_RT_ELEMENT_FOUR_STATE)
                  std::memcpy(
                      read->unknown, data + header->element->value_size,
                      static_cast<size_t>(header->element->value_size));
                return OBELISK_RT_OK;
              });
        };
        if (!header->buffer)
          return readDefault();
        return accessBuffer(header->buffer, [&](uint8_t *data, uint64_t size) {
          std::optional<uint64_t> found =
              findAssocSlot(*header, data, size, read->key);
          if (!found)
            return readDefault();
          uint64_t offset = *found * assocSlotStride(header->element) +
                            assocValueOffset(header->element);
          uint64_t stride = elementStride(header->element);
          if (offset > size || stride > size - offset)
            return OBELISK_RT_INVALID_HANDLE;
          std::memcpy(read->value, data + offset,
                      static_cast<size_t>(header->element->value_size));
          if (header->element->flags & OBELISK_RT_ELEMENT_FOUR_STATE)
            std::memcpy(read->unknown,
                        data + offset + header->element->value_size,
                        static_cast<size_t>(header->element->value_size));
          *read->present = 1;
          return OBELISK_RT_OK;
        });
      },
      &read);
}

extern "C" obelisk_rt_status obelisk_rt_v1_assoc_read_checked(
    obelisk_rt_object_v1 *array, const obelisk_rt_assoc_key_v1 *key,
    void *outValue, uint64_t valueSize, void *outUnknown,
    uint64_t unknownSize, uint32_t *outPresent) {
  if (!array || !outValue || !outPresent)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(array, snapshot);
  if (status != OBELISK_RT_OK ||
      snapshot.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  bool fourState =
      (snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0;
  if (valueSize < snapshot.element->value_size ||
      (fourState && unknownSize < snapshot.element->value_size) ||
      (!fourState && unknownSize != 0) ||
      fourState != (outUnknown != nullptr))
    return OBELISK_RT_INVALID_ARGUMENT;
  return obelisk_rt_v1_assoc_read(array, key, outValue, outUnknown,
                                  outPresent);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_assoc_write(obelisk_rt_gc_lane_v1 *lane,
                          obelisk_rt_object_v1 *array,
                          const obelisk_rt_assoc_key_v1 *key, const void *value,
                          const void *unknown) {
  if (!lane || !array || !key)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (obelisk_rt_managed_object_context(array) !=
      obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  ScopedManagedRoot ownerRoot(lane, &array);
  if (ownerRoot.getStatus() != OBELISK_RT_OK)
    return ownerRoot.getStatus();
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(array, snapshot);
  if (status != OBELISK_RT_OK ||
      snapshot.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  NormalizedAssocKey normalized;
  status = normalizeAssocKey(obelisk_rt_managed_object_context(array), snapshot,
                             key, normalized);
  if (status != OBELISK_RT_OK)
    return status;
  if (normalized.ignored) {
    warnIgnoredAssocKey();
    return OBELISK_RT_OK;
  }
  obelisk_rt_string_v1 keyRootValue = normalized.string;
  ScopedManagedWordRoot keyRoot(lane, &keyRootValue);
  if (keyRoot.getStatus() != OBELISK_RT_OK)
    return keyRoot.getStatus();
  normalized.string = keyRootValue;
  status = ensureAssocCapacity(lane, array, snapshot.size + 1);
  if (status != OBELISK_RT_OK)
    return status;
  std::vector<uint8_t> prepared;
  status = prepareElementValue(lane, obelisk_rt_managed_object_context(array),
                               snapshot.element, value, unknown, prepared);
  if (status != OBELISK_RT_OK)
    return status;
  uint64_t slotStride = assocSlotStride(snapshot.element);
  std::vector<uint8_t> candidate;
  try {
    candidate.assign(static_cast<size_t>(slotStride), 0);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  auto *candidateSlot = reinterpret_cast<AssocSlot *>(candidate.data());
  candidateSlot->hash = normalized.hash;
  candidateSlot->integral = normalized.integral;
  candidateSlot->string = normalized.string;
  std::memcpy(candidate.data() + assocValueOffset(snapshot.element),
              prepared.data(), prepared.size());
  struct Write {
    NormalizedAssocKey key;
    std::vector<uint8_t> *candidate;
  } write{normalized, &candidate};
  return obelisk_rt_managed_object_access(
      array, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        auto *write = static_cast<Write *>(opaque);
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        if (header->kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY ||
            !header->buffer)
          return OBELISK_RT_INVALID_LIFECYCLE;
        obelisk_rt_status status =
            accessBuffer(header->buffer, [&](uint8_t *data, uint64_t size) {
              std::optional<uint64_t> found =
                  findAssocSlot(*header, data, size, write->key);
              uint64_t stride = assocSlotStride(header->element);
              if (found) {
                uint64_t valueOffset =
                    *found * stride + assocValueOffset(header->element);
                uint64_t valueSize = elementStride(header->element);
                if (valueOffset > size || valueSize > size - valueOffset)
                  return OBELISK_RT_INVALID_HANDLE;
                std::memcpy(data + valueOffset,
                            write->candidate->data() +
                                assocValueOffset(header->element),
                            static_cast<size_t>(valueSize));
                return OBELISK_RT_OK;
              }
              if (header->size >= header->capacity * 3 / 4)
                return OBELISK_RT_INVALID_LIFECYCLE;
              obelisk_rt_status inserted =
                  insertAssocCandidate(*header, data, size, *write->candidate);
              if (inserted == OBELISK_RT_OK)
                ++header->size;
              return inserted;
            });
        if (status == OBELISK_RT_OK) {
          header->ordered = nullptr;
          ++header->epoch;
        }
        return status;
      },
      &write);
}

extern "C" obelisk_rt_status obelisk_rt_v1_assoc_write_checked(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *array,
    const obelisk_rt_assoc_key_v1 *key, const void *value,
    uint64_t valueSize, const void *unknown, uint64_t unknownSize) {
  if (!lane || !array || !value)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(array, snapshot);
  if (status != OBELISK_RT_OK ||
      snapshot.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  bool fourState =
      (snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0;
  if (valueSize < snapshot.element->value_size ||
      (fourState && unknownSize < snapshot.element->value_size) ||
      (!fourState && unknownSize != 0) ||
      fourState != (unknown != nullptr))
    return OBELISK_RT_INVALID_ARGUMENT;
  return obelisk_rt_v1_assoc_write(lane, array, key, value, unknown);
}

extern "C" obelisk_rt_status obelisk_rt_v1_assoc_set_default(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *array,
    const void *value, const void *unknown) {
  if (!lane || !array || !value)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (obelisk_rt_managed_object_context(array) !=
      obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  ScopedManagedRoot ownerRoot(lane, &array);
  if (ownerRoot.getStatus() != OBELISK_RT_OK)
    return ownerRoot.getStatus();
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(array, snapshot);
  if (status != OBELISK_RT_OK ||
      snapshot.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  std::vector<uint8_t> prepared;
  status = prepareElementValue(lane, obelisk_rt_managed_object_context(array),
                               snapshot.element, value, unknown, prepared);
  if (status != OBELISK_RT_OK)
    return status;
  ValueRootEnvironment preparedRoots{
      obelisk_rt_managed_lane_context(lane), prepared.data(), 1,
      elementStride(snapshot.element), snapshot.element};
  ScopedValueRoots preparedRoot(lane, &preparedRoots);
  if (preparedRoot.getStatus() != OBELISK_RT_OK)
    return preparedRoot.getStatus();
  obelisk_rt_object_v1 *storage = nullptr;
  status = allocateBuffer(lane, 1, elementStride(snapshot.element), &storage);
  if (status != OBELISK_RT_OK)
    return status;
  ScopedManagedRoot storageRoot(lane, &storage);
  if (storageRoot.getStatus() != OBELISK_RT_OK)
    return storageRoot.getStatus();
  status = accessBuffer(storage, [&](uint8_t *data, uint64_t size) {
    if (size < prepared.size())
      return OBELISK_RT_INVALID_HANDLE;
    std::memcpy(data, prepared.data(), prepared.size());
    return OBELISK_RT_OK;
  });
  if (status != OBELISK_RT_OK)
    return status;
  struct Publish {
    obelisk_rt_object_v1 *storage;
  } publish{storage};
  return obelisk_rt_managed_object_access(
      array, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) {
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        if (header->kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
          return OBELISK_RT_INVALID_ARGUMENT;
        header->defaultValue = static_cast<Publish *>(opaque)->storage;
        header->hasDefault = 1;
        ++header->epoch;
        return OBELISK_RT_OK;
      },
      &publish);
}

extern "C" obelisk_rt_status obelisk_rt_v1_assoc_set_default_checked(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *array,
    const void *value, uint64_t valueSize, const void *unknown,
    uint64_t unknownSize) {
  if (!lane || !array || !value)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(array, snapshot);
  if (status != OBELISK_RT_OK ||
      snapshot.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  bool fourState =
      (snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0;
  if (valueSize < snapshot.element->value_size ||
      (fourState && unknownSize < snapshot.element->value_size) ||
      (!fourState && unknownSize != 0) ||
      fourState != (unknown != nullptr))
    return OBELISK_RT_INVALID_ARGUMENT;
  return obelisk_rt_v1_assoc_set_default(lane, array, value, unknown);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_assoc_delete(obelisk_rt_object_v1 *array,
                           const obelisk_rt_assoc_key_v1 *key) {
  if (!array || !key)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContainerHeader snapshot;
  obelisk_rt_status status = snapshotHeader(array, snapshot);
  if (status != OBELISK_RT_OK ||
      snapshot.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  NormalizedAssocKey normalized;
  status = normalizeAssocKey(obelisk_rt_managed_object_context(array), snapshot,
                             key, normalized);
  if (status != OBELISK_RT_OK)
    return status;
  if (normalized.ignored) {
    warnIgnoredAssocKey();
    return OBELISK_RT_OK;
  }
  struct Delete {
    NormalizedAssocKey key;
  } remove{normalized};
  return obelisk_rt_managed_object_access(
      array, OBELISK_RT_MANAGED_CONTAINER,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        auto *remove = static_cast<Delete *>(opaque);
        if (extent != sizeof(ContainerHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *header = reinterpret_cast<ContainerHeader *>(object);
        if (header->kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
          return OBELISK_RT_INVALID_ARGUMENT;
        if (!header->buffer)
          return OBELISK_RT_OK;
        bool deleted = false;
        obelisk_rt_status status =
            accessBuffer(header->buffer, [&](uint8_t *data, uint64_t size) {
              std::optional<uint64_t> found =
                  findAssocSlot(*header, data, size, remove->key);
              if (!found)
                return OBELISK_RT_OK;
              uint64_t stride = assocSlotStride(header->element);
              uint64_t current = *found;
              uint64_t next = (current + 1) & (header->capacity - 1);
              while (true) {
                auto *nextSlot =
                    reinterpret_cast<AssocSlot *>(data + next * stride);
                if (nextSlot->hash == emptyAssocHash || nextSlot->distance == 0)
                  break;
                std::memcpy(data + current * stride, data + next * stride,
                            static_cast<size_t>(stride));
                auto *moved =
                    reinterpret_cast<AssocSlot *>(data + current * stride);
                --moved->distance;
                current = next;
                next = (next + 1) & (header->capacity - 1);
              }
              std::memset(data + current * stride, 0,
                          static_cast<size_t>(stride));
              deleted = true;
              return OBELISK_RT_OK;
            });
        if (status == OBELISK_RT_OK && deleted) {
          --header->size;
          header->ordered = nullptr;
          ++header->epoch;
        }
        return status;
      },
      &remove);
}

static int64_t signedAssocValue(uint64_t value, uint64_t width) {
  if (width == 64)
    return static_cast<int64_t>(value);
  uint64_t mask = (UINT64_C(1) << width) - 1;
  uint64_t sign = UINT64_C(1) << (width - 1);
  value &= mask;
  return static_cast<int64_t>((value & sign) ? (value | ~mask) : value);
}

static obelisk_rt_status compareAssocSlotWithKey(const ContainerHeader &header,
                                                 const AssocSlot &slot,
                                                 const NormalizedAssocKey &key,
                                                 int &comparison) {
  if (header.keyKind == OBELISK_RT_ASSOC_KEY_STRING) {
    StringView slotView;
    StringView keyView;
    obelisk_rt_status status = readString(slot.string, slotView);
    if (status != OBELISK_RT_OK)
      return status;
    status = readString(key.string, keyView);
    if (status != OBELISK_RT_OK)
      return status;
    comparison = compareViews(slotView, keyView, false);
    return OBELISK_RT_OK;
  }
  if (header.keyKind == OBELISK_RT_ASSOC_KEY_SIGNED) {
    int64_t left = signedAssocValue(slot.integral, header.keyWidth);
    int64_t right = signedAssocValue(key.integral, header.keyWidth);
    comparison = left < right ? -1 : left > right ? 1 : 0;
    return OBELISK_RT_OK;
  }
  comparison = slot.integral < key.integral   ? -1
               : slot.integral > key.integral ? 1
                                              : 0;
  return OBELISK_RT_OK;
}

static obelisk_rt_status ensureAssocOrdered(obelisk_rt_gc_lane_v1 *lane,
                                            obelisk_rt_object_v1 *array) {
  if (!lane || !array)
    return OBELISK_RT_INVALID_ARGUMENT;
  ScopedManagedRoot ownerRoot(lane, &array);
  if (ownerRoot.getStatus() != OBELISK_RT_OK)
    return ownerRoot.getStatus();
  while (true) {
    ContainerHeader snapshot;
    obelisk_rt_status status = snapshotHeader(array, snapshot);
    if (status != OBELISK_RT_OK ||
        snapshot.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
      return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
    if (snapshot.ordered || snapshot.size == 0)
      return OBELISK_RT_OK;
    std::vector<uint64_t> indices;
    try {
      indices.resize(static_cast<size_t>(snapshot.size));
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    struct Order {
      ContainerHeader snapshot;
      std::vector<uint64_t> *indices;
      bool retry = false;
    } order{snapshot, &indices};
    status = obelisk_rt_managed_object_access(
        array, OBELISK_RT_MANAGED_CONTAINER,
        [](void *opaque, uint8_t *object,
           uint64_t extent) -> obelisk_rt_status {
          auto *order = static_cast<Order *>(opaque);
          if (extent != sizeof(ContainerHeader))
            return OBELISK_RT_INVALID_HANDLE;
          auto *header = reinterpret_cast<ContainerHeader *>(object);
          if (header->epoch != order->snapshot.epoch ||
              header->buffer != order->snapshot.buffer ||
              header->size != order->snapshot.size) {
            order->retry = true;
            return OBELISK_RT_OK;
          }
          uint64_t stride = assocSlotStride(header->element);
          return accessBuffer(header->buffer, [&](uint8_t *data,
                                                  uint64_t size) {
            if (size < header->capacity * stride)
              return OBELISK_RT_INVALID_HANDLE;
            uint64_t count = 0;
            for (uint64_t index = 0; index != header->capacity; ++index) {
              auto *slot = reinterpret_cast<AssocSlot *>(data + index * stride);
              if (slot->hash != emptyAssocHash)
                (*order->indices)[count++] = index;
            }
            if (count != header->size)
              return OBELISK_RT_INVALID_HANDLE;
            std::sort(
                order->indices->begin(), order->indices->end(),
                [&](uint64_t leftIndex, uint64_t rightIndex) {
                  auto *left =
                      reinterpret_cast<AssocSlot *>(data + leftIndex * stride);
                  auto *right =
                      reinterpret_cast<AssocSlot *>(data + rightIndex * stride);
                  if (header->keyKind == OBELISK_RT_ASSOC_KEY_STRING) {
                    StringView leftView;
                    StringView rightView;
                    if (readString(left->string, leftView) != OBELISK_RT_OK ||
                        readString(right->string, rightView) != OBELISK_RT_OK)
                      return leftIndex < rightIndex;
                    return compareViews(leftView, rightView, false) < 0;
                  }
                  if (header->keyKind == OBELISK_RT_ASSOC_KEY_SIGNED)
                    return signedAssocValue(left->integral, header->keyWidth) <
                           signedAssocValue(right->integral, header->keyWidth);
                  return left->integral < right->integral;
                });
            return OBELISK_RT_OK;
          });
        },
        &order);
    if (status != OBELISK_RT_OK)
      return status;
    if (order.retry)
      continue;
    obelisk_rt_object_v1 *ordered = nullptr;
    status = allocateBuffer(lane, snapshot.size, sizeof(uint64_t), &ordered);
    if (status != OBELISK_RT_OK)
      return status;
    ScopedManagedRoot orderedRoot(lane, &ordered);
    if (orderedRoot.getStatus() != OBELISK_RT_OK)
      return orderedRoot.getStatus();
    status = accessBuffer(ordered, [&](uint8_t *data, uint64_t size) {
      uint64_t bytes = snapshot.size * sizeof(uint64_t);
      if (size < bytes)
        return OBELISK_RT_INVALID_HANDLE;
      std::memcpy(data, indices.data(), static_cast<size_t>(bytes));
      return OBELISK_RT_OK;
    });
    if (status != OBELISK_RT_OK)
      return status;
    struct Publish {
      ContainerHeader snapshot;
      obelisk_rt_object_v1 *ordered;
      bool retry = false;
    } publish{snapshot, ordered};
    status = obelisk_rt_managed_object_access(
        array, OBELISK_RT_MANAGED_CONTAINER,
        [](void *opaque, uint8_t *object,
           uint64_t extent) -> obelisk_rt_status {
          auto *publish = static_cast<Publish *>(opaque);
          if (extent != sizeof(ContainerHeader))
            return OBELISK_RT_INVALID_HANDLE;
          auto *header = reinterpret_cast<ContainerHeader *>(object);
          if (header->epoch != publish->snapshot.epoch ||
              header->buffer != publish->snapshot.buffer ||
              header->size != publish->snapshot.size) {
            publish->retry = true;
            return OBELISK_RT_OK;
          }
          if (!header->ordered)
            header->ordered = publish->ordered;
          return OBELISK_RT_OK;
        },
        &publish);
    if (status != OBELISK_RT_OK)
      return status;
    if (!publish.retry)
      return OBELISK_RT_OK;
  }
}

static obelisk_rt_status assocTraverse(obelisk_rt_gc_lane_v1 *lane,
                                       obelisk_rt_object_v1 *array,
                                       obelisk_rt_assoc_key_v1 *inoutKey,
                                       uint32_t *outSuccess, int direction,
                                       bool endpoint) {
  if (!lane || !array || !inoutKey || !outSuccess ||
      (direction != -1 && direction != 1))
    return OBELISK_RT_INVALID_ARGUMENT;
  if (obelisk_rt_managed_object_context(array) !=
      obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  *outSuccess = 0;
  ContainerHeader preflightHeader;
  obelisk_rt_status status = snapshotHeader(array, preflightHeader);
  if (status != OBELISK_RT_OK ||
      preflightHeader.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  NormalizedAssocKey preflightKey;
  if (!endpoint) {
    status = normalizeAssocKey(obelisk_rt_managed_object_context(array),
                               preflightHeader, inoutKey, preflightKey);
    if (status != OBELISK_RT_OK)
      return status;
    if (preflightKey.ignored) {
      warnIgnoredAssocKey();
      return OBELISK_RT_OK;
    }
  }
  ScopedManagedRoot ownerRoot(lane, &array);
  if (ownerRoot.getStatus() != OBELISK_RT_OK)
    return ownerRoot.getStatus();
  obelisk_rt_string_v1 keyRootValue =
      endpoint ? 0 : preflightKey.string;
  ScopedManagedWordRoot keyRoot(lane, &keyRootValue);
  if (keyRoot.getStatus() != OBELISK_RT_OK)
    return keyRoot.getStatus();
  status = ensureAssocOrdered(lane, array);
  struct Traverse {
    obelisk_rt_assoc_key_v1 *key;
    uint32_t *success;
    int direction;
    bool endpoint;
  } traverse{inoutKey, outSuccess, direction, endpoint};
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_managed_object_access(
        array, OBELISK_RT_MANAGED_CONTAINER,
        [](void *opaque, uint8_t *object,
           uint64_t extent) -> obelisk_rt_status {
          auto *traverse = static_cast<Traverse *>(opaque);
          if (extent != sizeof(ContainerHeader))
            return OBELISK_RT_INVALID_HANDLE;
          auto *header = reinterpret_cast<ContainerHeader *>(object);
          if (header->kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
            return OBELISK_RT_INVALID_ARGUMENT;
          if (header->size == 0 || !header->ordered || !header->buffer)
            return OBELISK_RT_OK;
          NormalizedAssocKey current;
          if (!traverse->endpoint) {
            obelisk_rt_status normalized = normalizeAssocKey(
                obelisk_rt_managed_object_context(
                    reinterpret_cast<obelisk_rt_object_v1 *>(object)),
                *header, traverse->key, current);
            if (normalized != OBELISK_RT_OK)
              return normalized;
            if (current.ignored) {
              warnIgnoredAssocKey();
              return OBELISK_RT_OK;
            }
          }
          uint64_t stride = assocSlotStride(header->element);
          return accessBuffer(header->ordered, [&](uint8_t *orderData,
                                                   uint64_t orderSize) {
            if (orderSize < header->size * sizeof(uint64_t))
              return OBELISK_RT_INVALID_HANDLE;
            auto *indices = reinterpret_cast<uint64_t *>(orderData);
            int64_t ordinal =
                traverse->endpoint
                    ? (traverse->direction > 0
                           ? 0
                           : static_cast<int64_t>(header->size - 1))
                    : -1;
            return accessBuffer(header->buffer, [&](uint8_t *data,
                                                    uint64_t size) {
              if (size < header->capacity * stride)
                return OBELISK_RT_INVALID_HANDLE;
              if (!traverse->endpoint) {
                if (traverse->direction > 0) {
                  for (uint64_t index = 0; index != header->size; ++index) {
                    auto *slot = reinterpret_cast<AssocSlot *>(
                        data + indices[index] * stride);
                    int comparison = 0;
                    obelisk_rt_status compared = compareAssocSlotWithKey(
                        *header, *slot, current, comparison);
                    if (compared != OBELISK_RT_OK)
                      return compared;
                    if (comparison > 0) {
                      ordinal = static_cast<int64_t>(index);
                      break;
                    }
                  }
                } else {
                  for (uint64_t index = header->size; index != 0; --index) {
                    auto *slot = reinterpret_cast<AssocSlot *>(
                        data + indices[index - 1] * stride);
                    int comparison = 0;
                    obelisk_rt_status compared = compareAssocSlotWithKey(
                        *header, *slot, current, comparison);
                    if (compared != OBELISK_RT_OK)
                      return compared;
                    if (comparison < 0) {
                      ordinal = static_cast<int64_t>(index - 1);
                      break;
                    }
                  }
                }
              }
              if (ordinal < 0 || static_cast<uint64_t>(ordinal) >= header->size)
                return OBELISK_RT_OK;
              uint64_t slotIndex = indices[ordinal];
              if (slotIndex >= header->capacity)
                return OBELISK_RT_INVALID_HANDLE;
              auto *slot =
                  reinterpret_cast<AssocSlot *>(data + slotIndex * stride);
              if (slot->hash == emptyAssocHash)
                return OBELISK_RT_INVALID_HANDLE;
              *traverse->key = {};
              traverse->key->kind = header->keyKind;
              traverse->key->width = header->keyWidth;
              traverse->key->value = slot->integral;
              traverse->key->string = slot->string;
              *traverse->success = 1;
              return OBELISK_RT_OK;
            });
          });
        },
        &traverse);
  return status;
}

extern "C" obelisk_rt_status obelisk_rt_v1_assoc_first(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *array,
    obelisk_rt_assoc_key_v1 *inoutKey, uint32_t *outSuccess) {
  return assocTraverse(lane, array, inoutKey, outSuccess, 1, true);
}

extern "C" obelisk_rt_status obelisk_rt_v1_assoc_last(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *array,
    obelisk_rt_assoc_key_v1 *inoutKey, uint32_t *outSuccess) {
  return assocTraverse(lane, array, inoutKey, outSuccess, -1, true);
}

extern "C" obelisk_rt_status obelisk_rt_v1_assoc_next(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *array,
    obelisk_rt_assoc_key_v1 *inoutKey, uint32_t *outSuccess) {
  return assocTraverse(lane, array, inoutKey, outSuccess, 1, false);
}

extern "C" obelisk_rt_status obelisk_rt_v1_assoc_prev(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *array,
    obelisk_rt_assoc_key_v1 *inoutKey, uint32_t *outSuccess) {
  return assocTraverse(lane, array, inoutKey, outSuccess, -1, false);
}

static obelisk_rt_status snapshotReferencePath(obelisk_rt_object_v1 *path,
                                               ReferencePathHeader &snapshot) {
  struct Snapshot {
    ReferencePathHeader *output;
  } environment{&snapshot};
  return obelisk_rt_managed_object_access(
      path, OBELISK_RT_MANAGED_REFERENCE_PATH,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        if (extent != sizeof(ReferencePathHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *path = reinterpret_cast<ReferencePathHeader *>(object);
        if (path->descriptor != &referencePathDescriptorToken || !path->owner ||
            !path->element || path->ownerManaged > 2 ||
            ((path->ownerManaged == 0) != (path->watchOwner == nullptr)) ||
            (path->selector != ReferenceSelector::Index &&
             path->selector != ReferenceSelector::Associative))
          return OBELISK_RT_INVALID_HANDLE;
        *static_cast<Snapshot *>(opaque)->output = *path;
        return OBELISK_RT_OK;
      },
      &environment);
}

obelisk_rt_status obelisk_rt_reference_path_shape(obelisk_rt_object_v1 *path,
                                                  uint64_t valueSize,
                                                  uint64_t bitWidth,
                                                  uint32_t fourState,
                                                  uint32_t managedValue) {
  ReferencePathHeader snapshot;
  obelisk_rt_status status = snapshotReferencePath(path, snapshot);
  if (status != OBELISK_RT_OK)
    return status;
  bool pathFourState =
      (snapshot.element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0;
  bool pathManaged =
      snapshot.element->kind == OBELISK_RT_ELEMENT_CLASS_HANDLE ||
      snapshot.element->kind == OBELISK_RT_ELEMENT_STRING ||
      snapshot.element->kind == OBELISK_RT_ELEMENT_CONTAINER_HANDLE;
  bool widthMatches = false;
  switch (snapshot.element->kind) {
  case OBELISK_RT_ELEMENT_BITS:
  case OBELISK_RT_ELEMENT_LOGIC:
    widthMatches = snapshot.element->bit_width == bitWidth;
    break;
  case OBELISK_RT_ELEMENT_REAL:
    widthMatches = bitWidth == 64;
    break;
  case OBELISK_RT_ELEMENT_EVENT:
    widthMatches = bitWidth == 64;
    break;
  case OBELISK_RT_ELEMENT_CLASS_HANDLE:
  case OBELISK_RT_ELEMENT_STRING:
  case OBELISK_RT_ELEMENT_CONTAINER_HANDLE:
    widthMatches = bitWidth == sizeof(void *) * 8;
    break;
  case OBELISK_RT_ELEMENT_AGGREGATE:
    widthMatches = bitWidth == snapshot.element->value_size * 8;
    break;
  default:
    return OBELISK_RT_INVALID_DESIGN;
  }
  return snapshot.element->value_size == valueSize && widthMatches &&
                 pathFourState == (fourState != 0) &&
                 pathManaged == (managedValue != 0)
             ? OBELISK_RT_OK
             : OBELISK_RT_ARGUMENT_MISMATCH;
}

extern "C" obelisk_rt_status obelisk_rt_v1_reference_path_index_create(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *container, int64_t index,
    obelisk_rt_object_v1 *watchOwner, uint64_t ownerPayload,
    uint32_t ownerManaged,
    obelisk_rt_object_v1 **outPath) {
  if (!lane || !container || !outPath || ownerManaged > 2)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (obelisk_rt_managed_object_context(container) !=
      obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  if ((ownerManaged == 0 && watchOwner) ||
      (ownerManaged != 0 &&
       (!watchOwner ||
        obelisk_rt_managed_object_context(watchOwner) !=
            obelisk_rt_managed_lane_context(lane))))
    return OBELISK_RT_INVALID_HANDLE;
  *outPath = nullptr;
  ContainerHeader owner;
  obelisk_rt_status status = snapshotHeader(container, owner);
  if (status != OBELISK_RT_OK ||
      owner.kind == OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  ScopedManagedRoot ownerRoot(lane, &container);
  if (ownerRoot.getStatus() != OBELISK_RT_OK)
    return ownerRoot.getStatus();
  ScopedManagedRoot watchRoot(lane, &watchOwner);
  if (watchRoot.getStatus() != OBELISK_RT_OK)
    return watchRoot.getStatus();
  obelisk_rt_object_v1 *path = nullptr;
  status = obelisk_rt_managed_allocate(
      lane, OBELISK_RT_MANAGED_REFERENCE_PATH, sizeof(ReferencePathHeader),
      alignof(ReferencePathHeader), &referencePathDescriptorToken, &path);
  if (status != OBELISK_RT_OK)
    return status;
  struct Initialize {
    obelisk_rt_object_v1 *owner;
    const obelisk_rt_element_type_v1 *element;
    int64_t index;
    obelisk_rt_object_v1 *watchOwner;
    uint64_t ownerPayload;
    uint32_t ownerManaged;
  } initialize{container, owner.element, index, watchOwner, ownerPayload,
               ownerManaged};
  status = obelisk_rt_managed_object_access(
      path, OBELISK_RT_MANAGED_REFERENCE_PATH,
      [](void *opaque, uint8_t *object, uint64_t extent) -> obelisk_rt_status {
        if (extent != sizeof(ReferencePathHeader))
          return OBELISK_RT_INVALID_HANDLE;
        auto *initialize = static_cast<Initialize *>(opaque);
        auto *path = reinterpret_cast<ReferencePathHeader *>(object);
        path->descriptor = &referencePathDescriptorToken;
        path->owner = initialize->owner;
        path->element = initialize->element;
        path->selector = ReferenceSelector::Index;
        path->ownerManaged = initialize->ownerManaged;
        path->index = initialize->index;
        path->watchOwner = initialize->watchOwner;
        path->ownerPayload = initialize->ownerPayload;
        return OBELISK_RT_OK;
      },
      &initialize);
  if (status == OBELISK_RT_OK)
    *outPath = path;
  return status;
}

extern "C" obelisk_rt_status obelisk_rt_v1_reference_path_assoc_create(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *array,
    const obelisk_rt_assoc_key_v1 *key, obelisk_rt_object_v1 *watchOwner,
    uint64_t ownerPayload, uint32_t ownerManaged,
    obelisk_rt_object_v1 **outPath) {
  if (!lane || !array || !key || !outPath || ownerManaged > 2)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (obelisk_rt_managed_object_context(array) !=
      obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  if ((ownerManaged == 0 && watchOwner) ||
      (ownerManaged != 0 &&
       (!watchOwner ||
        obelisk_rt_managed_object_context(watchOwner) !=
            obelisk_rt_managed_lane_context(lane))))
    return OBELISK_RT_INVALID_HANDLE;
  *outPath = nullptr;
  ContainerHeader owner;
  obelisk_rt_status status = snapshotHeader(array, owner);
  if (status != OBELISK_RT_OK ||
      owner.kind != OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY)
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  NormalizedAssocKey normalized;
  status = normalizeAssocKey(obelisk_rt_managed_object_context(array), owner,
                             key, normalized);
  if (status != OBELISK_RT_OK)
    return status;
  ScopedManagedRoot ownerRoot(lane, &array);
  if (ownerRoot.getStatus() != OBELISK_RT_OK)
    return ownerRoot.getStatus();
  ScopedManagedRoot watchRoot(lane, &watchOwner);
  if (watchRoot.getStatus() != OBELISK_RT_OK)
    return watchRoot.getStatus();
  obelisk_rt_string_v1 keyRootValue = normalized.string;
  ScopedManagedWordRoot keyRoot(lane, &keyRootValue);
  if (keyRoot.getStatus() != OBELISK_RT_OK)
    return keyRoot.getStatus();
  obelisk_rt_object_v1 *path = nullptr;
  status = obelisk_rt_managed_allocate(
      lane, OBELISK_RT_MANAGED_REFERENCE_PATH, sizeof(ReferencePathHeader),
      alignof(ReferencePathHeader), &referencePathDescriptorToken, &path);
  if (status == OBELISK_RT_OK) {
    struct Initialize {
      obelisk_rt_object_v1 *owner;
      const obelisk_rt_element_type_v1 *element;
      obelisk_rt_assoc_key_v1 key;
      obelisk_rt_object_v1 *watchOwner;
      uint64_t ownerPayload;
      uint32_t ownerManaged;
    } initialize{array, owner.element, *key, watchOwner, ownerPayload,
                 ownerManaged};
    initialize.key.value = normalized.integral;
    initialize.key.string = keyRootValue;
    status = obelisk_rt_managed_object_access(
        path, OBELISK_RT_MANAGED_REFERENCE_PATH,
        [](void *opaque, uint8_t *object,
           uint64_t extent) -> obelisk_rt_status {
          if (extent != sizeof(ReferencePathHeader))
            return OBELISK_RT_INVALID_HANDLE;
          auto *initialize = static_cast<Initialize *>(opaque);
          auto *path = reinterpret_cast<ReferencePathHeader *>(object);
          path->descriptor = &referencePathDescriptorToken;
          path->owner = initialize->owner;
          path->element = initialize->element;
          path->selector = ReferenceSelector::Associative;
          path->key = initialize->key;
          path->watchOwner = initialize->watchOwner;
          path->ownerPayload = initialize->ownerPayload;
          path->ownerManaged = initialize->ownerManaged;
          return OBELISK_RT_OK;
        },
        &initialize);
  }
  if (status == OBELISK_RT_OK) {
    *outPath = path;
  }
  return status;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_reference_path_load(obelisk_rt_object_v1 *path, void *outValue,
                                  void *outUnknown, uint32_t *outPresent) {
  if (!outValue || !outPresent)
    return OBELISK_RT_INVALID_ARGUMENT;
  ReferencePathHeader snapshot;
  obelisk_rt_status status = snapshotReferencePath(path, snapshot);
  if (status != OBELISK_RT_OK)
    return status;
  if (snapshot.selector == ReferenceSelector::Associative)
    return obelisk_rt_v1_assoc_read(snapshot.owner, &snapshot.key, outValue,
                                    outUnknown, outPresent);
  ContainerHeader owner;
  status = snapshotHeader(snapshot.owner, owner);
  if (status != OBELISK_RT_OK)
    return status;
  *outPresent =
      snapshot.index >= 0 && static_cast<uint64_t>(snapshot.index) < owner.size;
  return obelisk_rt_v1_container_read(snapshot.owner, snapshot.index, outValue,
                                      outUnknown);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_reference_path_store(obelisk_rt_gc_lane_v1 *lane,
                                   obelisk_rt_object_v1 *path,
                                   const void *value, const void *unknown) {
  if (!lane || !value)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!path || obelisk_rt_managed_object_context(path) !=
                   obelisk_rt_managed_lane_context(lane))
    return OBELISK_RT_INVALID_HANDLE;
  ScopedManagedRoot pathRoot(lane, &path);
  if (pathRoot.getStatus() != OBELISK_RT_OK)
    return pathRoot.getStatus();
  ReferencePathHeader snapshot;
  obelisk_rt_status status = snapshotReferencePath(path, snapshot);
  if (status != OBELISK_RT_OK)
    return status;
  if (snapshot.selector == ReferenceSelector::Associative) {
    status = obelisk_rt_v1_assoc_write(lane, snapshot.owner, &snapshot.key,
                                       value, unknown);
    if (status == OBELISK_RT_OK && snapshot.ownerManaged == 0)
      obelisk_rt_v1_scheduler_signal(
          obelisk_rt_managed_lane_context(lane), snapshot.ownerPayload,
          sizeof(obelisk_rt_managed_word_v1) * 8, OBELISK_RT_SIGNAL_CHANGE);
    return status;
  }
  ContainerHeader owner;
  status = snapshotHeader(snapshot.owner, owner);
  if (status != OBELISK_RT_OK)
    return status;
  bool present =
      snapshot.index >= 0 && static_cast<uint64_t>(snapshot.index) < owner.size;
  status = obelisk_rt_v1_container_write(lane, snapshot.owner, snapshot.index,
                                         value, unknown);
  if (status == OBELISK_RT_OK && present && snapshot.ownerManaged == 0)
    obelisk_rt_v1_scheduler_signal(
        obelisk_rt_managed_lane_context(lane), snapshot.ownerPayload,
        sizeof(obelisk_rt_managed_word_v1) * 8, OBELISK_RT_SIGNAL_CHANGE);
  return status;
}
