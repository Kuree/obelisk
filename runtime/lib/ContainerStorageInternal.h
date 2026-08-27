//===- ContainerStorageInternal.h - Shared storage ABI -------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_CONTAINERSTORAGEINTERNAL_H
#define OBELISK_RUNTIME_LIB_CONTAINERSTORAGEINTERNAL_H

#include "obelisk/Runtime/Runtime.h"

namespace obelisk::runtime_detail {

struct BufferHeader {
  const void *descriptor;
  uint64_t reserved;
};

// Physical associative-slot prefix shared with the cold bit-stream exporter.
// Keeping it beside the container header prevents the ordinary and feature
// runtimes from silently disagreeing about the value-plane offset.
struct AssocSlot {
  uint64_t hash;
  uint64_t distance;
  uint64_t integral;
  union {
    obelisk_rt_string_v1 string;
    obelisk_rt_object_v1 *object;
  };
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
  obelisk_rt_object_v1 *referenceBuffer;
  uint64_t referenceCount;
  uint64_t referenceCapacity;
};

static_assert(sizeof(BufferHeader) == 16);
static_assert(sizeof(AssocSlot) == 32);
static_assert(sizeof(ContainerHeader) == (sizeof(void *) == 8 ? 128 : 112));

inline uint64_t elementStride(const obelisk_rt_element_type_v1 *element) {
  return element->value_size *
         ((element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) ? 2 : 1);
}

inline uint64_t assocValueOffset(const obelisk_rt_element_type_v1 *element) {
  return (sizeof(AssocSlot) + element->alignment - 1) &
         ~(element->alignment - 1);
}

inline uint64_t assocSlotStride(const obelisk_rt_element_type_v1 *element) {
  uint64_t alignment = alignof(AssocSlot) > element->alignment
                           ? alignof(AssocSlot)
                           : element->alignment;
  uint64_t size = assocValueOffset(element) + elementStride(element);
  return (size + alignment - 1) & ~(alignment - 1);
}

/// Materialize and publish the cached index-sorted slot order for an
/// associative array. The first ordering pass may allocate the cache.
obelisk_rt_status ensureAssocOrdered(obelisk_rt_gc_lane_v1 *lane,
                                     obelisk_rt_object_v1 *array);

} // namespace obelisk::runtime_detail

#endif // OBELISK_RUNTIME_LIB_CONTAINERSTORAGEINTERNAL_H
