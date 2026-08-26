//===- ContainerStorageInternal.h - Shared storage ABI -------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_CONTAINERSTORAGEINTERNAL_H
#define OBELISK_RUNTIME_LIB_CONTAINERSTORAGEINTERNAL_H

#include "obelisk/Runtime/Runtime.h"

namespace obelisk::runtime_detail {

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
  obelisk_rt_object_v1 *referenceBuffer;
  uint64_t referenceCount;
  uint64_t referenceCapacity;
};

static_assert(sizeof(BufferHeader) == 16);
static_assert(sizeof(ContainerHeader) == (sizeof(void *) == 8 ? 128 : 112));

} // namespace obelisk::runtime_detail

#endif // OBELISK_RUNTIME_LIB_CONTAINERSTORAGEINTERNAL_H
