//===- ProcessState.cpp - Native automatic state ownership --------------===//

#include "ProcessContext.h"
#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHandle.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace {

bool decodeAutomatic(uint64_t handle, uint32_t &id, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_AUTOMATIC)
    return false;
  id = decoded.id;
  offset = decoded.offset;
  return true;
}

bool validNonAutomaticHandle(uint64_t handle) {
  obelisk_rt_stable_handle_v1 decoded;
  return obelisk_rt_stable_handle_decode(handle, &decoded) &&
         decoded.kind != OBELISK_RT_STABLE_HANDLE_AUTOMATIC;
}

bool managedRootWordBelongsTo(obelisk_rt_context *context,
                              obelisk_rt_managed_word_v1 word) {
  if (word == 0)
    return true;
  if ((word & UINT64_C(3)) == UINT64_C(1))
    return obelisk_rt_validate_string(context, word) == OBELISK_RT_OK;
  if ((word & UINT64_C(3)) != 0)
    return false;
  return obelisk_rt_managed_object_belongs_to(
      context,
      reinterpret_cast<obelisk_rt_object_v1 *>(static_cast<uintptr_t>(word)));
}

obelisk_rt_status publishNativeAutomaticState(obelisk_rt_context *context,
                                              NativeAutomaticState state,
                                              uint64_t *outHandle) {
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    state.owner = context->activeNativeProcess;
    if (!state.owner)
      state.designOwner = context->activeDesignTaskID;
    uint32_t id = context->nextNativeAutomaticID;
    if (id == 0 || id > OBELISK_RT_STABLE_HANDLE_MAX_AUTOMATIC_ID) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    if (!context->nativeAutomaticStates.emplace(id, std::move(state)).second)
      return OBELISK_RT_INVALID_LIFECYCLE;
    ++context->nextNativeAutomaticID;
    *outHandle = obelisk_rt_stable_handle_encode(
        OBELISK_RT_STABLE_HANDLE_AUTOMATIC, id, 0);
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

} // namespace

extern "C" uint64_t obelisk_rt_v1_native_state_static_handle(uint32_t id) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, id,
                                         0);
}

extern "C" uint64_t obelisk_rt_v1_native_handle_offset(uint64_t handle,
                                                       int64_t offset) {
  return obelisk_rt_stable_handle_offset(handle, offset);
}

obelisk_rt_status
obelisk_rt_native_state_alloc_managed(obelisk_rt_context *context,
                                      obelisk_rt_object_v1 *value,
                                      uint64_t *outHandle) {
  if (!outHandle)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outHandle = UINT64_MAX;
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!obelisk_rt_managed_object_belongs_to(context, value))
    return OBELISK_RT_INVALID_HANDLE;
  NativeAutomaticState state;
  state.bitWidth = 64;
  state.managedValue = value;
  state.managedRootRegistered = true;
  return publishNativeAutomaticState(context, std::move(state), outHandle);
}

obelisk_rt_status obelisk_rt_native_state_alloc_with_root_offsets(
    obelisk_rt_context *context, uint64_t bitWidth, const uint8_t *value,
    const uint8_t *unknown, std::vector<uint64_t> bitOffsets,
    uint64_t *outHandle) {
  if (!outHandle)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outHandle = UINT64_MAX;
  if (!context || !value || bitWidth == 0 || bitWidth > INT32_MAX ||
      bitWidth > UINT64_MAX - 7)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (byteCount > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    for (size_t index = 0; index != bitOffsets.size(); ++index) {
      uint64_t bitOffset = bitOffsets[index];
      if ((bitOffset & 63) != 0 || bitOffset > bitWidth ||
          64 > bitWidth - bitOffset)
        return OBELISK_RT_INVALID_ARGUMENT;
      bitOffsets[index] = bitOffset / 8;
    }
    std::sort(bitOffsets.begin(), bitOffsets.end());
    if (std::adjacent_find(bitOffsets.begin(), bitOffsets.end()) !=
        bitOffsets.end())
      return OBELISK_RT_INVALID_ARGUMENT;

    NativeAutomaticState state;
    state.bitWidth = bitWidth;
    for (uint64_t byteOffset : bitOffsets) {
      obelisk_rt_managed_word_v1 managed = 0;
      std::memcpy(&managed, value + byteOffset, sizeof(managed));
      if (!managedRootWordBelongsTo(context, managed))
        return OBELISK_RT_INVALID_HANDLE;
    }
    state.managedRootByteOffsets = std::move(bitOffsets);
    state.value.assign(value, value + static_cast<size_t>(byteCount));
    if (unknown)
      state.unknown.assign(unknown, unknown + static_cast<size_t>(byteCount));
    return publishNativeAutomaticState(context, std::move(state), outHandle);
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_alloc(obelisk_rt_context *context, uint64_t bitWidth,
                                 const uint8_t *value, const uint8_t *unknown,
                                 uint64_t *outHandle) {
  return obelisk_rt_native_state_alloc_with_root_offsets(
      context, bitWidth, value, unknown, {}, outHandle);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_retain(obelisk_rt_context *context,
                                  uint64_t handle) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (handle == UINT64_MAX)
    return OBELISK_RT_OK;
  uint32_t id = 0;
  int64_t offset = 0;
  if (!decodeAutomatic(handle, id, offset))
    return validNonAutomaticHandle(handle) ? OBELISK_RT_OK
                                           : OBELISK_RT_INVALID_HANDLE;
  try {
    ContextMutexLock lock(context);
    auto found = context->nativeAutomaticStates.find(id);
    if (found == context->nativeAutomaticStates.end())
      return OBELISK_RT_INVALID_HANDLE;
    if (found->second.referenceCount == UINT64_MAX) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    ++found->second.referenceCount;
    return OBELISK_RT_OK;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_register_managed_roots(
    obelisk_rt_context *context, uint64_t handle, const uint64_t *bitOffsets,
    uint64_t count) {
  if (!context || !bitOffsets || count == 0 ||
      count > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  uint32_t id = 0;
  int64_t handleOffset = 0;
  if (!decodeAutomatic(handle, id, handleOffset) || handleOffset != 0)
    return OBELISK_RT_INVALID_HANDLE;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    auto found = context->nativeAutomaticStates.find(id);
    if (found == context->nativeAutomaticStates.end())
      return OBELISK_RT_INVALID_HANDLE;
    NativeAutomaticState &state = found->second;
    if (state.managedRootRegistered || !state.managedRootByteOffsets.empty())
      return OBELISK_RT_INVALID_LIFECYCLE;
    std::vector<uint64_t> byteOffsets;
    byteOffsets.reserve(static_cast<size_t>(count));
    for (uint64_t index = 0; index != count; ++index) {
      uint64_t bitOffset = bitOffsets[index];
      if ((bitOffset & 63) != 0 || bitOffset > state.bitWidth ||
          64 > state.bitWidth - bitOffset)
        return OBELISK_RT_INVALID_ARGUMENT;
      uint64_t byteOffset = bitOffset / 8;
      if (byteOffset > state.value.size() ||
          sizeof(obelisk_rt_managed_word_v1) > state.value.size() - byteOffset)
        return OBELISK_RT_INVALID_ARGUMENT;
      obelisk_rt_managed_word_v1 word = 0;
      std::memcpy(&word, state.value.data() + byteOffset, sizeof(word));
      if (!managedRootWordBelongsTo(context, word))
        return OBELISK_RT_INVALID_HANDLE;
      byteOffsets.push_back(byteOffset);
    }
    std::sort(byteOffsets.begin(), byteOffsets.end());
    if (std::adjacent_find(byteOffsets.begin(), byteOffsets.end()) !=
        byteOffsets.end())
      return OBELISK_RT_INVALID_ARGUMENT;
    state.managedRootByteOffsets = std::move(byteOffsets);
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_alloc_with_roots(
    obelisk_rt_context *context, uint64_t bitWidth, const uint8_t *value,
    const uint8_t *unknown, const uint64_t *bitOffsets, uint64_t count,
    uint64_t *outHandle) {
  if (!outHandle)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outHandle = UINT64_MAX;
  if (!bitOffsets || count == 0 || count > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::vector<uint64_t> offsets(bitOffsets, bitOffsets + count);
    return obelisk_rt_native_state_alloc_with_root_offsets(
        context, bitWidth, value, unknown, std::move(offsets), outHandle);
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_release(obelisk_rt_context *context, uint64_t handle,
                                   uint32_t ownerReference) {
  if (!context || ownerReference > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (handle == UINT64_MAX)
    return OBELISK_RT_OK;
  uint32_t id = 0;
  int64_t offset = 0;
  if (!decodeAutomatic(handle, id, offset))
    return validNonAutomaticHandle(handle) ? OBELISK_RT_OK
                                           : OBELISK_RT_INVALID_HANDLE;
  try {
    ContextMutexLock lock(context);
    auto found = context->nativeAutomaticStates.find(id);
    if (found == context->nativeAutomaticStates.end() ||
        found->second.referenceCount == 0)
      return OBELISK_RT_INVALID_HANDLE;
    if (ownerReference) {
      bool nativeOwner = found->second.owner &&
                         found->second.owner == context->activeNativeProcess;
      bool designOwner =
          found->second.designOwner != 0 &&
          found->second.designOwner == context->activeDesignTaskID;
      if (!nativeOwner && !designOwner)
        return OBELISK_RT_INVALID_LIFECYCLE;
      found->second.owner = nullptr;
      found->second.designOwner = 0;
    }
    if (--found->second.referenceCount == 0) {
      obelisk_rt_erase_automatic_bookkeeping_unlocked(context, id);
      context->nativeAutomaticStates.erase(found);
    }
    return OBELISK_RT_OK;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}
