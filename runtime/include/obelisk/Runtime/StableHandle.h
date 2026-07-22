//===- StableHandle.h - Pointer-free native state handle ABI ----*- C -*-===//

#ifndef OBELISK_RUNTIME_STABLEHANDLE_H
#define OBELISK_RUNTIME_STABLEHANDLE_H

#include <limits.h>
#include <stdint.h>

#define OBELISK_RT_STABLE_HANDLE_AUTOMATIC_TAG (UINT64_C(1) << 63)
#define OBELISK_RT_STABLE_HANDLE_STATIC_TAG (UINT64_C(1) << 62)
#define OBELISK_RT_STABLE_HANDLE_TAG_MASK                                  \
  (OBELISK_RT_STABLE_HANDLE_AUTOMATIC_TAG |                                \
   OBELISK_RT_STABLE_HANDLE_STATIC_TAG)
#define OBELISK_RT_STABLE_HANDLE_MAX_AUTOMATIC_ID UINT32_C(0x7ffffffe)
#define OBELISK_RT_STABLE_HANDLE_MAX_STATIC_ID UINT32_C(0x3fffffff)

typedef uint32_t obelisk_rt_stable_handle_kind_v1;
enum {
  OBELISK_RT_STABLE_HANDLE_INVALID = 0,
  OBELISK_RT_STABLE_HANDLE_GLOBAL = 1,
  OBELISK_RT_STABLE_HANDLE_STATIC = 2,
  OBELISK_RT_STABLE_HANDLE_AUTOMATIC = 3
};

typedef struct obelisk_rt_stable_handle_v1 {
  obelisk_rt_stable_handle_kind_v1 kind;
  uint32_t id;
  int64_t offset;
} obelisk_rt_stable_handle_v1;

static inline int obelisk_rt_stable_handle_decode(
    uint64_t handle, obelisk_rt_stable_handle_v1 *decoded) {
  if (!decoded)
    return 0;
  decoded->kind = OBELISK_RT_STABLE_HANDLE_INVALID;
  decoded->id = 0;
  decoded->offset = 0;
  if (handle == UINT64_MAX)
    return 0;
  if ((handle & OBELISK_RT_STABLE_HANDLE_AUTOMATIC_TAG) != 0) {
    uint32_t id = (uint32_t)((handle >> 32) & UINT32_C(0x7fffffff));
    if (id == 0 || id > OBELISK_RT_STABLE_HANDLE_MAX_AUTOMATIC_ID)
      return 0;
    decoded->kind = OBELISK_RT_STABLE_HANDLE_AUTOMATIC;
    decoded->id = id;
    decoded->offset = (int32_t)handle;
    return 1;
  }
  if ((handle & OBELISK_RT_STABLE_HANDLE_TAG_MASK) ==
      OBELISK_RT_STABLE_HANDLE_STATIC_TAG) {
    uint32_t id = (uint32_t)((handle >> 32) & UINT32_C(0x3fffffff));
    if (id == 0 || id > OBELISK_RT_STABLE_HANDLE_MAX_STATIC_ID)
      return 0;
    decoded->kind = OBELISK_RT_STABLE_HANDLE_STATIC;
    decoded->id = id;
    decoded->offset = (int32_t)handle;
    return 1;
  }
  if ((handle & OBELISK_RT_STABLE_HANDLE_TAG_MASK) != 0)
    return 0;
  decoded->kind = OBELISK_RT_STABLE_HANDLE_GLOBAL;
  decoded->offset = (int64_t)handle;
  return 1;
}

static inline uint64_t obelisk_rt_stable_handle_encode(
    obelisk_rt_stable_handle_kind_v1 kind, uint32_t id, int64_t offset) {
  if (kind == OBELISK_RT_STABLE_HANDLE_GLOBAL)
    return offset >= 0 && (uint64_t)offset <
                              OBELISK_RT_STABLE_HANDLE_STATIC_TAG
               ? (uint64_t)offset
               : UINT64_MAX;
  if (offset < INT32_MIN || offset > INT32_MAX)
    return UINT64_MAX;
  if (kind == OBELISK_RT_STABLE_HANDLE_STATIC) {
    if (id == 0 || id > OBELISK_RT_STABLE_HANDLE_MAX_STATIC_ID)
      return UINT64_MAX;
    return OBELISK_RT_STABLE_HANDLE_STATIC_TAG | ((uint64_t)id << 32) |
           (uint32_t)(int32_t)offset;
  }
  if (kind == OBELISK_RT_STABLE_HANDLE_AUTOMATIC) {
    if (id == 0 || id > OBELISK_RT_STABLE_HANDLE_MAX_AUTOMATIC_ID)
      return UINT64_MAX;
    return OBELISK_RT_STABLE_HANDLE_AUTOMATIC_TAG | ((uint64_t)id << 32) |
           (uint32_t)(int32_t)offset;
  }
  return UINT64_MAX;
}

static inline uint64_t
obelisk_rt_stable_handle_offset(uint64_t handle, int64_t amount) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      (amount > 0 && decoded.offset > INT64_MAX - amount) ||
      (amount < 0 && decoded.offset < INT64_MIN - amount))
    return UINT64_MAX;
  return obelisk_rt_stable_handle_encode(decoded.kind, decoded.id,
                                         decoded.offset + amount);
}

static inline int obelisk_rt_stable_handle_same_object(uint64_t left,
                                                        uint64_t right) {
  obelisk_rt_stable_handle_v1 lhs;
  obelisk_rt_stable_handle_v1 rhs;
  return obelisk_rt_stable_handle_decode(left, &lhs) &&
         obelisk_rt_stable_handle_decode(right, &rhs) &&
         lhs.kind == rhs.kind &&
         (lhs.kind == OBELISK_RT_STABLE_HANDLE_GLOBAL || lhs.id == rhs.id);
}

#endif // OBELISK_RUNTIME_STABLEHANDLE_H
