//===- ProcessState.cpp - Native automatic state ownership --------------===//

#include "ProcessContext.h"
#include "ProcessPacking.h"
#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHandle.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <tuple>
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

// The caller holds the context lock. Automatic state may outlive its owner
// because executable references retain it, so frame-handle lifetime is checked
// separately by VPI before reaching this storage helper.
obelisk_rt_status
resolveVPIAutomaticSlice(obelisk_rt_context *context, uint64_t reference,
                         uint64_t bitOffset, uint64_t bitWidth,
                         const NativeAutomaticState *&outState,
                         uint64_t &outOffset) {
  uint32_t id = 0;
  int64_t base = 0;
  if (!decodeAutomatic(reference, id, base))
    return OBELISK_RT_INVALID_HANDLE;
  auto found = context->nativeAutomaticStates.find(id);
  if (found == context->nativeAutomaticStates.end() ||
      found->second.referenceCount == 0)
    return OBELISK_RT_INVALID_HANDLE;
  const auto &state = found->second;
  if (base < 0 || bitOffset > UINT64_MAX - static_cast<uint64_t>(base))
    return OBELISK_RT_INVALID_ARGUMENT;
  const uint64_t offset = static_cast<uint64_t>(base) + bitOffset;
  if (offset > state.bitWidth || bitWidth > state.bitWidth - offset)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (state.managedRootRegistered) {
    if (state.bitWidth != 64 || offset != 0 || bitWidth != 64)
      return OBELISK_RT_INVALID_ARGUMENT;
  } else {
    if (state.bitWidth > UINT64_MAX - 7)
      return OBELISK_RT_INVALID_FRAME;
    const uint64_t bytes = (state.bitWidth + 7) / 8;
    if (state.value.size() < bytes ||
        (!state.unknown.empty() && state.unknown.size() < bytes))
      return OBELISK_RT_INVALID_FRAME;
  }
  outState = &state;
  outOffset = offset;
  return OBELISK_RT_OK;
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
      context, obelisk_rt_object_from_managed_word(word));
}

obelisk_rt_status publishNativeAutomaticState(obelisk_rt_context *context,
                                              NativeAutomaticState state,
                                              uint64_t *outHandle) {
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
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
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

} // namespace

obelisk_rt_status
obelisk_rt_vpi_automatic_validate(obelisk_rt_context *context,
                                  uint64_t reference,
                                  uint64_t bitWidth) noexcept {
  if (!context || !bitWidth || bitWidth > UINT64_MAX - 7)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    const NativeAutomaticState *state = nullptr;
    uint64_t offset = 0;
    return resolveVPIAutomaticSlice(context, reference, 0, bitWidth, state,
                                    offset);
  }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

obelisk_rt_status
obelisk_rt_vpi_automatic_read(obelisk_rt_context *context, uint64_t reference,
                              uint64_t bitOffset, uint64_t bitWidth,
                              uint8_t *outValue, uint8_t *outUnknown) noexcept {
  if (!context || !outValue || !outUnknown || !bitWidth ||
      bitWidth > UINT64_MAX - 7 || (bitWidth + 7) / 8 > SIZE_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  const size_t bytes = static_cast<size_t>((bitWidth + 7) / 8);
  std::memset(outValue, 0, bytes);
  std::memset(outUnknown, 0, bytes);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    const NativeAutomaticState *state = nullptr;
    uint64_t offset = 0;
    auto status = resolveVPIAutomaticSlice(context, reference, bitOffset,
                                           bitWidth, state, offset);
    if (status != OBELISK_RT_OK)
      return status;
    if (state->managedRootRegistered) {
      const auto word =
          obelisk_rt_managed_word_from_object(state->managedValue);
      std::memcpy(outValue, &word, sizeof(word));
      return OBELISK_RT_OK;
    }
    for (uint64_t copied = 0; copied < bitWidth; copied += 8) {
      const uint64_t width = std::min<uint64_t>(8, bitWidth - copied);
      outValue[copied / 8] = static_cast<uint8_t>(
          loadPackedBytes(state->value.data(), offset + copied, width));
      if (!state->unknown.empty())
        outUnknown[copied / 8] = static_cast<uint8_t>(
            loadPackedBytes(state->unknown.data(), offset + copied, width));
    }
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

uint64_t
obelisk_rt_canonical_state_handle_unlocked(const obelisk_rt_context *context,
                                           uint64_t bitOffset,
                                           uint64_t bitWidth) noexcept {
  if (!context || bitWidth == 0 || bitOffset > UINT64_MAX - bitWidth)
    return UINT64_MAX;
  uint32_t selectedID = UINT32_MAX;
  uint64_t selectedOffset = 0;
  if (!context->nativeStaticStateRangesValid) {
    OBELISK_RT_TRY {
      std::vector<NativeStaticStateRange> ranges;
      ranges.reserve(context->nativeStaticStates.size());
      for (const auto &[id, state] : context->nativeStaticStates)
        ranges.push_back(
            {state.bitOffset, state.bitOffset + state.bitWidth, 0, id});
      std::sort(ranges.begin(), ranges.end(),
                [](const NativeStaticStateRange &left,
                   const NativeStaticStateRange &right) {
                  return std::tie(left.bitOffset, left.id) <
                         std::tie(right.bitOffset, right.id);
                });
      uint64_t prefixEnd = 0;
      for (NativeStaticStateRange &range : ranges) {
        prefixEnd = std::max(prefixEnd, range.bitEnd);
        range.prefixEnd = prefixEnd;
      }
      context->nativeStaticStateRanges = std::move(ranges);
      context->nativeStaticStateRangesValid = true;
    }
    OBELISK_RT_CATCH_ALL {
      // This lookup is noexcept and has always had an allocation-free linear
      // implementation. Preserve that fallback if building the cache fails.
    }
  }
  if (context->nativeStaticStateRangesValid) {
    uint64_t bitEnd = bitOffset + bitWidth;
    const auto &ranges = context->nativeStaticStateRanges;
    auto upper = std::upper_bound(
        ranges.begin(), ranges.end(), bitOffset,
        [](uint64_t offset, const NativeStaticStateRange &range) {
          return offset < range.bitOffset;
        });
    size_t index = static_cast<size_t>(upper - ranges.begin());
    while (index != 0) {
      const NativeStaticStateRange &range = ranges[--index];
      if (range.bitEnd >= bitEnd && range.id < selectedID) {
        selectedID = range.id;
        selectedOffset = bitOffset - range.bitOffset;
      }
      if (index == 0 || ranges[index - 1].prefixEnd < bitEnd)
        break;
    }
  } else {
    for (const auto &[id, state] : context->nativeStaticStates) {
      if (state.bitOffset > bitOffset ||
          bitOffset - state.bitOffset > state.bitWidth ||
          bitWidth > state.bitWidth - (bitOffset - state.bitOffset) ||
          id >= selectedID)
        continue;
      selectedID = id;
      selectedOffset = bitOffset - state.bitOffset;
    }
  }
  if (selectedID != UINT32_MAX) {
    if (selectedOffset > static_cast<uint64_t>(INT64_MAX))
      return UINT64_MAX;
    return obelisk_rt_stable_handle_encode(
        OBELISK_RT_STABLE_HANDLE_STATIC, selectedID,
        static_cast<int64_t>(selectedOffset));
  }
  if (bitOffset > static_cast<uint64_t>(INT64_MAX))
    return UINT64_MAX;
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_GLOBAL, 0,
                                         static_cast<int64_t>(bitOffset));
}

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
  OBELISK_RT_TRY {
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
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
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
  OBELISK_RT_TRY {
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
  }
  OBELISK_RT_CATCH_ALL {
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
  OBELISK_RT_TRY {
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
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
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

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_alloc_with_typed_roots(
    obelisk_rt_context *context, uint64_t bitWidth, const uint8_t *value,
    const uint8_t *unknown, const obelisk_rt_managed_root_slot_v1 *slots,
    uint64_t count, uint64_t *outHandle) {
  if (!outHandle)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outHandle = UINT64_MAX;
  if (!context || !value || !slots || count == 0 || bitWidth == 0 ||
      bitWidth > INT32_MAX || count > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t byteCount = (bitWidth + 7) / 8;
  OBELISK_RT_TRY {
    NativeAutomaticState state;
    state.bitWidth = bitWidth;
    state.value.assign(value, value + static_cast<size_t>(byteCount));
    if (unknown)
      state.unknown.assign(unknown, unknown + static_cast<size_t>(byteCount));
    uint64_t previousBitOffset = 0;
    bool havePrevious = false;
    for (uint64_t index = 0; index != count; ++index) {
      const obelisk_rt_managed_root_slot_v1 &slot = slots[index];
      if ((slot.bit_offset & 63) != 0 || slot.bit_offset > bitWidth ||
          64 > bitWidth - slot.bit_offset || slot.kind_mask == 0 ||
          (slot.kind_mask & ~OBELISK_RT_MANAGED_ROOT_KIND_ALL) != 0 ||
          (slot.flags & ~OBELISK_RT_MANAGED_ROOT_SLOT_CANDIDATE) != 0 ||
          (havePrevious && slot.bit_offset <= previousBitOffset))
        return OBELISK_RT_INVALID_ARGUMENT;
      previousBitOffset = slot.bit_offset;
      havePrevious = true;
      uint64_t byteOffset = slot.bit_offset / 8;
      if ((slot.flags & OBELISK_RT_MANAGED_ROOT_SLOT_CANDIDATE) != 0) {
        state.candidateRootByteOffsets.push_back({byteOffset, slot.kind_mask});
      } else {
        obelisk_rt_managed_word_v1 word = 0;
        std::memcpy(&word, value + byteOffset, sizeof(word));
        if (obelisk_rt_v1_gc_candidate_root(context, word, slot.kind_mask) !=
            word)
          return OBELISK_RT_INVALID_HANDLE;
        state.managedRootByteOffsets.push_back(byteOffset);
      }
    }
    std::sort(state.managedRootByteOffsets.begin(),
              state.managedRootByteOffsets.end());
    std::sort(state.candidateRootByteOffsets.begin(),
              state.candidateRootByteOffsets.end(),
              [](const auto &lhs, const auto &rhs) {
                return lhs.byteOffset < rhs.byteOffset;
              });
    return publishNativeAutomaticState(context, std::move(state), outHandle);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
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
  OBELISK_RT_TRY {
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
  }
  OBELISK_RT_CATCH_ALL {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_argument_ref_load(
    obelisk_rt_context *context, const uint8_t *stateValue,
    const uint8_t *stateUnknown, uint64_t stateBitCount,
    obelisk_rt_object_v1 *owner, uint64_t payload, uint32_t managed,
    uint64_t bitWidth, uint64_t planeSize, uint32_t fourState,
    uint32_t valueKind, void *outValue, void *outUnknown) {
  if (!context || !outValue || bitWidth == 0 || planeSize == 0 ||
      planeSize > std::numeric_limits<size_t>::max() || managed > 2 ||
      fourState > 1 || valueKind > OBELISK_RT_ARGUMENT_VALUE_STRING ||
      (fourState && !outUnknown) ||
      (valueKind != OBELISK_RT_ARGUMENT_VALUE_BITS && fourState) ||
      (!managed && (!stateValue || (fourState && !stateUnknown))))
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memset(outValue, 0, static_cast<size_t>(planeSize));
  if (outUnknown)
    std::memset(outUnknown, 0, static_cast<size_t>(planeSize));
  if (managed == 2) {
    obelisk_rt_status shape = obelisk_rt_reference_path_shape(
        owner, planeSize, bitWidth, fourState,
        valueKind != OBELISK_RT_ARGUMENT_VALUE_BITS);
    if (shape != OBELISK_RT_OK)
      return shape;
    uint32_t present = 0;
    obelisk_rt_status status = obelisk_rt_v1_reference_path_load(
        owner, outValue, outUnknown, &present);
    if (status != OBELISK_RT_OK ||
        valueKind != OBELISK_RT_ARGUMENT_VALUE_STRING)
      return status;
    obelisk_rt_string_v1 string = 0;
    std::memcpy(&string, outValue, sizeof(string));
    return obelisk_rt_validate_string(context, string);
  }
  if (managed == 1) {
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_CLASS) {
      if (bitWidth != sizeof(obelisk_rt_managed_word_v1) * 8 ||
          planeSize != sizeof(obelisk_rt_managed_word_v1))
        return OBELISK_RT_INVALID_ARGUMENT;
      obelisk_rt_status status =
          obelisk_rt_v1_object_read(owner, payload, outValue, planeSize);
      if (status != OBELISK_RT_OK)
        return status;
      obelisk_rt_managed_word_v1 word = 0;
      std::memcpy(&word, outValue, sizeof(word));
      obelisk_rt_object_v1 *object = obelisk_rt_object_from_managed_word(word);
      return word == obelisk_rt_managed_word_from_object(object)
                 ? OBELISK_RT_OK
                 : OBELISK_RT_INVALID_HANDLE;
    }
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) {
      if (bitWidth != sizeof(obelisk_rt_string_v1) * 8 ||
          planeSize != sizeof(obelisk_rt_string_v1))
        return OBELISK_RT_INVALID_ARGUMENT;
      obelisk_rt_status status =
          obelisk_rt_v1_object_read(owner, payload, outValue, planeSize);
      if (status != OBELISK_RT_OK)
        return status;
      obelisk_rt_string_v1 string = 0;
      std::memcpy(&string, outValue, sizeof(string));
      return obelisk_rt_validate_string(context, string);
    }
    if (fourState && payload > UINT64_MAX - planeSize)
      return OBELISK_RT_INVALID_ARGUMENT;
    if (fourState)
      return obelisk_rt_v1_object_read_planes(owner, payload, outValue,
                                              outUnknown, planeSize);
    return obelisk_rt_v1_object_read(owner, payload, outValue, planeSize);
  }
  ContextTransaction transaction(context);
  obelisk_rt_status status = obelisk_rt_v1_native_state_load_plane(
      context, stateValue, stateBitCount, payload, bitWidth, 0, 0,
      static_cast<uint8_t *>(outValue));
  if (status != OBELISK_RT_OK)
    return status;
  if (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) {
    if (bitWidth != sizeof(obelisk_rt_string_v1) * 8 ||
        planeSize != sizeof(obelisk_rt_string_v1))
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_string_v1 string = 0;
    std::memcpy(&string, outValue, sizeof(string));
    return obelisk_rt_validate_string(context, string);
  }
  if (!fourState)
    return OBELISK_RT_OK;
  return obelisk_rt_v1_native_state_load_plane(
      context, stateUnknown, stateBitCount, payload, bitWidth, 1, 1,
      static_cast<uint8_t *>(outUnknown));
}

extern "C" obelisk_rt_status obelisk_rt_v1_argument_ref_store(
    obelisk_rt_context *context, uint8_t *stateValue, uint8_t *stateUnknown,
    uint64_t stateBitCount, obelisk_rt_object_v1 *owner, uint64_t payload,
    uint32_t managed, uint64_t bitWidth, uint64_t planeSize, uint32_t fourState,
    uint32_t valueKind, const void *value, const void *unknown) {
  if (!context || !value || bitWidth == 0 || planeSize == 0 ||
      planeSize > std::numeric_limits<size_t>::max() || managed > 2 ||
      fourState > 1 || valueKind > OBELISK_RT_ARGUMENT_VALUE_STRING ||
      (fourState && !unknown) ||
      (valueKind != OBELISK_RT_ARGUMENT_VALUE_BITS && fourState) ||
      (!managed && (!stateValue || (fourState && !stateUnknown))))
    return OBELISK_RT_INVALID_ARGUMENT;
  {
    ContextMutexLock lock(context);
    if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return OBELISK_RT_INVALID_LIFECYCLE;
    }
  }
  if (managed == 2) {
    obelisk_rt_status shape = obelisk_rt_reference_path_shape(
        owner, planeSize, bitWidth, fourState,
        valueKind != OBELISK_RT_ARGUMENT_VALUE_BITS);
    if (shape != OBELISK_RT_OK)
      return shape;
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) {
      obelisk_rt_string_v1 string = 0;
      std::memcpy(&string, value, sizeof(string));
      obelisk_rt_status status = obelisk_rt_validate_string(context, string);
      if (status != OBELISK_RT_OK)
        return status;
    }
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    return obelisk_rt_v1_reference_path_store(lane, owner, value, unknown);
  }
  if (managed == 1) {
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_CLASS) {
      if (bitWidth != sizeof(obelisk_rt_managed_word_v1) * 8 ||
          planeSize != sizeof(obelisk_rt_managed_word_v1))
        return OBELISK_RT_INVALID_ARGUMENT;
      obelisk_rt_managed_word_v1 word = 0;
      std::memcpy(&word, value, sizeof(word));
      obelisk_rt_object_v1 *stored = obelisk_rt_object_from_managed_word(word);
      if (word != obelisk_rt_managed_word_from_object(stored))
        return OBELISK_RT_INVALID_HANDLE;
      return obelisk_rt_v1_object_write(owner, payload, value, planeSize);
    }
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) {
      if (bitWidth != sizeof(obelisk_rt_string_v1) * 8 ||
          planeSize != sizeof(obelisk_rt_string_v1))
        return OBELISK_RT_INVALID_ARGUMENT;
      obelisk_rt_string_v1 string = 0;
      std::memcpy(&string, value, sizeof(string));
      obelisk_rt_status status = obelisk_rt_validate_string(context, string);
      if (status != OBELISK_RT_OK)
        return status;
      return obelisk_rt_v1_object_write(owner, payload, value, planeSize);
    }
    if (fourState && payload > UINT64_MAX - planeSize)
      return OBELISK_RT_INVALID_ARGUMENT;
    if (fourState)
      return obelisk_rt_v1_object_write_planes(owner, payload, value, unknown,
                                               planeSize);
    return obelisk_rt_v1_object_write(owner, payload, value, planeSize);
  }
  // Keep one reusable transition snapshot per host thread. This avoids a heap
  // allocation for each language `ref` write while preserving exact signal
  // change and edge notification for ordinary simulation storage.
  struct TransitionScratch {
    std::vector<uint8_t> value;
    std::vector<uint8_t> unknown;
  };
  thread_local TransitionScratch scratch;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    scratch.value.resize(static_cast<size_t>(planeSize));
    if (fourState)
      scratch.unknown.resize(static_cast<size_t>(planeSize));
    ContextMutexLock lock(context);
    obelisk_rt_status status = obelisk_rt_v1_argument_ref_load(
        context, stateValue, stateUnknown, stateBitCount, nullptr, payload, 0,
        bitWidth, planeSize, fourState, valueKind, scratch.value.data(),
        fourState ? scratch.unknown.data() : nullptr);
    if (status != OBELISK_RT_OK)
      return status;
    bool equalStringContents = false;
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) {
      obelisk_rt_string_v1 previous = 0;
      obelisk_rt_string_v1 next = 0;
      std::memcpy(&previous, scratch.value.data(), sizeof(previous));
      std::memcpy(&next, value, sizeof(next));
      status = obelisk_rt_validate_string(context, next);
      if (status != OBELISK_RT_OK)
        return status;
      equalStringContents = obelisk_rt_v1_string_compare(previous, next) == 0;
    }
    uint8_t changed = 0;
    status = obelisk_rt_v1_native_state_store_plane(
        context, stateValue, stateBitCount, payload, bitWidth, 0,
        static_cast<const uint8_t *>(value), &changed);
    if (status != OBELISK_RT_OK)
      return status;
    if (fourState) {
      status = obelisk_rt_v1_native_state_store_plane(
          context, stateUnknown, stateBitCount, payload, bitWidth, 1,
          static_cast<const uint8_t *>(unknown), &changed);
      if (status != OBELISK_RT_OK)
        return status;
    }
    if (!equalStringContents)
      obelisk_rt_v1_scheduler_signal_transition(
          context, payload, bitWidth, scratch.value.data(),
          fourState ? scratch.unknown.data() : nullptr,
          static_cast<const uint8_t *>(value),
          fourState ? static_cast<const uint8_t *>(unknown) : nullptr);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}
