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

const obelisk_rt_execution_extension_v1 *
executionExtension(const obelisk_rt_execution_descriptor_v1 &execution) {
  return reinterpret_cast<const obelisk_rt_execution_extension_v1 *>(
      static_cast<uintptr_t>(execution.reserved));
}

void copyPackedRange(uint8_t *destination, const uint8_t *source,
                     uint64_t sourceBytes, uint64_t sourceBit,
                     uint64_t bitWidth) {
  size_t bytes = static_cast<size_t>((bitWidth + 7) / 8);
  unsigned shift = static_cast<unsigned>(sourceBit % 8);
  uint64_t sourceByte = sourceBit / 8;
  for (size_t index = 0; index != bytes; ++index) {
    uint16_t value = source[sourceByte + index];
    if (shift != 0 && sourceByte + index + 1 < sourceBytes)
      value |= static_cast<uint16_t>(source[sourceByte + index + 1]) << 8;
    destination[index] = static_cast<uint8_t>(value >> shift);
  }
  if (bitWidth % 8 != 0)
    destination[bytes - 1] &= static_cast<uint8_t>((1u << (bitWidth % 8)) - 1);
}

bool resolveCanonicalRange(const obelisk_rt_context *context, uint64_t handle,
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

bool resolveSnapshotRange(const obelisk_rt_context *context,
                          uint64_t canonicalStart, uint64_t width,
                          uint64_t &snapshotStart) {
  const obelisk_rt_execution_descriptor_v1 *execution = context->execution;
  const obelisk_rt_execution_extension_v1 *extension =
      execution ? executionExtension(*execution) : nullptr;
  uint64_t low = 0, high = extension ? extension->sampled_range_count : 0;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    if (extension->sampled_ranges[middle].source_bit_offset <= canonicalStart)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == 0)
    return false;
  const obelisk_rt_sampled_range_v1 &range = extension->sampled_ranges[low - 1];
  uint64_t relative = canonicalStart - range.source_bit_offset;
  if (relative > range.bit_width || width > range.bit_width - relative)
    return false;
  snapshotStart = range.snapshot_byte_offset * 8 + relative;
  return true;
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
    const obelisk_rt_execution_descriptor_v1 &execution = *context->execution;
    const obelisk_rt_execution_extension_v1 &extension =
        *executionExtension(execution);
    const obelisk_rt_sampled_range_v1 &last =
        extension.sampled_ranges[extension.sampled_range_count - 1];
    uint64_t snapshotBytes =
        last.snapshot_byte_offset + (last.bit_width + 7) / 8;
    size_t words = static_cast<size_t>((snapshotBytes + 7) / 8);
    context->preponedValue.resize(words);
    context->preponedUnknown.resize(words);

    const uint8_t *value = nullptr;
    const uint8_t *unknown = nullptr;
    if (context->nativeSchedulePlan &&
        context->nativeSchedulePlan->state_bit_count ==
            execution.state_bit_count) {
      value = context->nativeSchedulePlan->state_value;
      unknown = context->nativeSchedulePlan->state_unknown;
    }
    if (!value || !unknown) {
      size_t stateWords =
          static_cast<size_t>((execution.state_bit_count + 63) / 64);
      if (context->stateValue.size() != stateWords ||
          context->stateUnknown.size() != stateWords)
        return OBELISK_RT_INVALID_DESIGN;
      value = reinterpret_cast<const uint8_t *>(context->stateValue.data());
      unknown = reinterpret_cast<const uint8_t *>(context->stateUnknown.data());
    }
    uint8_t *sampledValue =
        reinterpret_cast<uint8_t *>(context->preponedValue.data());
    uint8_t *sampledUnknown =
        reinterpret_cast<uint8_t *>(context->preponedUnknown.data());
    uint64_t stateBytes = (execution.state_bit_count + 7) / 8;
    for (uint64_t index = 0; index != extension.sampled_range_count; ++index) {
      const obelisk_rt_sampled_range_v1 &range =
          extension.sampled_ranges[index];
      copyPackedRange(sampledValue + range.snapshot_byte_offset, value,
                      stateBytes, range.source_bit_offset, range.bit_width);
      copyPackedRange(sampledUnknown + range.snapshot_byte_offset, unknown,
                      stateBytes, range.source_bit_offset, range.bit_width);
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
    uint64_t canonicalStart = 0, snapshotStart = 0;
    if (!resolveCanonicalRange(context, stableID, bitWidth, canonicalStart) ||
        !resolveSnapshotRange(context, canonicalStart, bitWidth,
                              snapshotStart) ||
        context->preponedValue.empty() || context->preponedUnknown.empty())
      return OBELISK_RT_INVALID_HANDLE;
    std::memset(outValue, 0, bytes);
    std::memset(outUnknown, 0, bytes);
    const uint8_t *values =
        reinterpret_cast<const uint8_t *>(context->preponedValue.data());
    const uint8_t *unknowns =
        reinterpret_cast<const uint8_t *>(context->preponedUnknown.data());
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      setBit(outValue, bit, getBit(values, snapshotStart + bit));
      setBit(outUnknown, bit, getBit(unknowns, snapshotStart + bit));
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
