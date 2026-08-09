//===- Sampled.cpp - Preponed sampled-value history ---------------------===//

#include "RuntimeInternal.h"

#include "obelisk/Runtime/StableHandle.h"

#include <cstring>
#include <limits>

namespace {

bool getBit(const uint8_t *bytes, uint64_t bit) {
  return (bytes[bit / 8] & static_cast<uint8_t>(1u << (bit % 8))) != 0;
}

void setBit(uint8_t *bytes, uint64_t bit, bool value) {
  uint8_t mask = static_cast<uint8_t>(1u << (bit % 8));
  if (value)
    bytes[bit / 8] |= mask;
  else
    bytes[bit / 8] &= static_cast<uint8_t>(~mask);
}

bool checkedBytes(uint64_t bits, size_t &bytes) {
  if (bits == 0 || bits > UINT64_MAX - 7)
    return false;
  uint64_t count = (bits + 7) / 8;
  if (count > std::numeric_limits<size_t>::max())
    return false;
  bytes = static_cast<size_t>(count);
  return true;
}

bool resolveSnapshotRange(const obelisk_rt_context *context, uint64_t handle,
                          uint64_t width, uint64_t &start) {
  obelisk_rt_stable_handle_v1 decoded{};
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) || decoded.offset < 0)
    return false;
  uint64_t offset = static_cast<uint64_t>(decoded.offset);
  if (decoded.kind == OBELISK_RT_STABLE_HANDLE_GLOBAL) {
    start = offset;
  } else if (decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC) {
    const NativeStaticState *state = nullptr;
    if (decoded.id < context->nativeScheduleStaticStateIndex.size() &&
        context->nativeScheduleStaticStateIndex[decoded.id].bitWidth != 0)
      state = &context->nativeScheduleStaticStateIndex[decoded.id];
    else if (auto found = context->nativeStaticStates.find(decoded.id);
             found != context->nativeStaticStates.end())
      state = &found->second;
    if (!state || offset > state->bitWidth || width > state->bitWidth - offset)
      return false;
    start = state->bitOffset + offset;
  } else {
    return false;
  }
  uint64_t total = context->execution ? context->execution->state_bit_count : 0;
  return start <= total && width <= total - start;
}

} // namespace

obelisk_rt_status
obelisk_rt_capture_preponed_unlocked(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!context->execution ||
      (context->execution->flags &
       OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT) == 0)
    return OBELISK_RT_OK;
  try {
    uint64_t bits = context->execution ? context->execution->state_bit_count : 0;
    size_t words = static_cast<size_t>((bits + 63) / 64);
    context->preponedValue.resize(words);
    context->preponedUnknown.resize(words);
    if (words == 0)
      return OBELISK_RT_OK;

    const uint8_t *value = nullptr;
    const uint8_t *unknown = nullptr;
    if (context->nativeSchedulePlan &&
        context->nativeSchedulePlan->state_bit_count == bits) {
      value = context->nativeSchedulePlan->state_value;
      unknown = context->nativeSchedulePlan->state_unknown;
    }
    size_t bytes = static_cast<size_t>((bits + 7) / 8);
    if (value && unknown) {
      std::memset(context->preponedValue.data(), 0, words * sizeof(uint64_t));
      std::memset(context->preponedUnknown.data(), 0,
                  words * sizeof(uint64_t));
      std::memcpy(context->preponedValue.data(), value, bytes);
      std::memcpy(context->preponedUnknown.data(), unknown, bytes);
    } else {
      if (context->stateValue.size() != words ||
          context->stateUnknown.size() != words)
        return OBELISK_RT_INVALID_DESIGN;
      std::copy(context->stateValue.begin(), context->stateValue.end(),
                context->preponedValue.begin());
      std::copy(context->stateUnknown.begin(), context->stateUnknown.end(),
                context->preponedUnknown.begin());
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_sampled_read(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    uint8_t *outValue, uint8_t *outUnknown) {
  size_t bytes = 0;
  if (!context || !outValue || !outUnknown || !checkedBytes(bitWidth, bytes))
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    uint64_t start = 0;
    if (!resolveSnapshotRange(context, stableID, bitWidth, start) ||
        context->preponedValue.empty() || context->preponedUnknown.empty())
      return OBELISK_RT_INVALID_HANDLE;
    std::memset(outValue, 0, bytes);
    std::memset(outUnknown, 0, bytes);
    const uint8_t *values =
        reinterpret_cast<const uint8_t *>(context->preponedValue.data());
    const uint8_t *unknowns =
        reinterpret_cast<const uint8_t *>(context->preponedUnknown.data());
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      setBit(outValue, bit, getBit(values, start + bit));
      setBit(outUnknown, bit, getBit(unknowns, start + bit));
    }
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_sampled_history(
    obelisk_rt_context *context, uint64_t siteID, uint64_t bitWidth,
    uint64_t depth, uint32_t fourState, uint32_t gate,
    const uint8_t *currentValue, const uint8_t *currentUnknown,
    uint8_t *outValue, uint8_t *outUnknown) {
  size_t bytes = 0;
  if (!context || siteID == 0 || depth == 0 || fourState > 1 || gate > 1 ||
      !currentValue || (fourState && !currentUnknown) || !outValue ||
      !outUnknown || !checkedBytes(bitWidth, bytes) ||
      depth > std::numeric_limits<size_t>::max() / bytes)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->activeLogicalProcessToken == 0)
      return OBELISK_RT_INVALID_LIFECYCLE;
    SampledHistoryState &history =
        context->sampledHistories[context->activeLogicalProcessToken][siteID];
    if (history.bitWidth == 0) {
      history.bitWidth = bitWidth;
      history.depth = depth;
      history.value.assign(static_cast<size_t>(depth) * bytes, 0);
      history.unknown.assign(static_cast<size_t>(depth) * bytes,
                             fourState ? UINT8_MAX : 0);
      if (bitWidth % 8 != 0 && fourState)
        for (uint64_t index = 0; index != depth; ++index)
          history.unknown[index * bytes + bytes - 1] &=
              static_cast<uint8_t>((1u << (bitWidth % 8)) - 1);
    } else if (history.bitWidth != bitWidth || history.depth != depth) {
      return OBELISK_RT_LAYOUT_MISMATCH;
    }

    uint64_t selected = history.count < depth ? history.count : history.next;
    std::memcpy(outValue, history.value.data() + selected * bytes, bytes);
    std::memcpy(outUnknown, history.unknown.data() + selected * bytes, bytes);
    if (gate) {
      std::memcpy(history.value.data() + history.next * bytes, currentValue,
                  bytes);
      if (fourState)
        std::memcpy(history.unknown.data() + history.next * bytes,
                    currentUnknown, bytes);
      else
        std::memset(history.unknown.data() + history.next * bytes, 0, bytes);
      history.next = (history.next + 1) % depth;
      if (history.count < depth)
        ++history.count;
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}
