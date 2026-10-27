//===- ContainerStorageInternal.h - Shared storage ABI -------*- C++ -*-===//

#ifndef TALOS_RUNTIME_LIB_CONTAINERSTORAGEINTERNAL_H
#define TALOS_RUNTIME_LIB_CONTAINERSTORAGEINTERNAL_H

#include "talos/Runtime/Runtime.h"

namespace talos::runtime_detail {

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
    talos_rt_string_v1 string;
    talos_rt_object_v1 *object;
  };
};

struct ContainerHeader {
  const void *descriptor;
  talos_rt_container_kind_v1 kind;
  // Mailboxes share queue storage but preserve a distinct object identity.
  uint32_t isMailbox;
  const talos_rt_element_type_v1 *element;
  talos_rt_object_v1 *buffer;
  talos_rt_object_v1 *ordered;
  talos_rt_object_v1 *defaultValue;
  uint64_t size;
  uint64_t capacity;
  uint64_t head;
  uint64_t bound;
  uint64_t epoch;
  talos_rt_assoc_key_kind_v1 keyKind;
  uint32_t hasDefault;
  uint64_t keyWidth;
  talos_rt_object_v1 *referenceBuffer;
  uint64_t referenceCount;
  uint64_t referenceCapacity;
  // Open-addressed index into referenceBuffer. The registry owns weak handles;
  // the index stores only hashes and ordinals, so it needs no element tracer.
  talos_rt_object_v1 *referenceIndex;
  uint64_t referenceIndexCapacity;
  // An TALOS_RT_ASSOC_KEY_ELEMENT array's interned key element type and
  // equality plan words; null for every other container.
  const talos_rt_element_type_v1 *keyElement;
  const uint64_t *keyPlan;
  // Exact immutable source descriptor for a boxed value. Zero means the box
  // predates VPI metadata; it never means a type inferred from element storage.
  uint64_t boxTypeID;
};

static_assert(sizeof(BufferHeader) == 16);
static_assert(sizeof(AssocSlot) == 32);
static_assert(sizeof(ContainerHeader) == (sizeof(void *) == 8 ? 168 : 144));

inline uint64_t elementStride(const talos_rt_element_type_v1 *element) {
  return element->value_size *
         ((element->flags & TALOS_RT_ELEMENT_FOUR_STATE) ? 2 : 1);
}

inline uint64_t assocValueOffset(const talos_rt_element_type_v1 *element) {
  return (sizeof(AssocSlot) + element->alignment - 1) &
         ~(element->alignment - 1);
}

inline uint64_t assocSlotStride(const talos_rt_element_type_v1 *element) {
  uint64_t alignment = alignof(AssocSlot) > element->alignment
                           ? alignof(AssocSlot)
                           : element->alignment;
  uint64_t size = assocValueOffset(element) + elementStride(element);
  return (size + alignment - 1) & ~(alignment - 1);
}

/// Materialize and publish the cached index-sorted slot order for an
/// associative array. The first ordering pass may allocate the cache.
talos_rt_status ensureAssocOrdered(talos_rt_gc_lane_v1 *lane,
                                     talos_rt_object_v1 *array);
talos_rt_status
ensureAssocOrderedWithoutSafepoint(talos_rt_gc_lane_v1 *lane,
                                   talos_rt_object_v1 *array);

} // namespace talos::runtime_detail

#endif // TALOS_RUNTIME_LIB_CONTAINERSTORAGEINTERNAL_H
