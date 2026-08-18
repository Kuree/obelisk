//===- ProcessNativeState.cpp - Native static state planes ---------------===//
//
// The static state a native AOT plan owns: locating static roots, decoding and
// range-checking packed handles, importing/exporting and reconciling the
// plan's value/unknown bit planes, and the force/release override entry points
// that write those planes from outside a generated kernel.  Split out of
// Process.cpp.
//
//===----------------------------------------------------------------------===//

#include "ProcessAllocation.h"
#include "ProcessContext.h"
#include "ProcessObservers.h"
#include "ProcessPacking.h"
#include "ProcessShared.h"
#include "ProcessSignals.h"
#include "ProcessValidation.h"
#include "RuntimeInternal.h"
#include "SignalSemantics.h"
#include "obelisk/Runtime/StableHandle.h"
#include "obelisk/Runtime/StableHash.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <tuple>

using namespace obelisk::process;
using namespace obelisk::runtime;

const NativeStaticState *
findNativeStaticState(const obelisk_rt_context *context, uint32_t id) {
  if (id < context->nativeScheduleStaticStateIndex.size() &&
      context->nativeScheduleStaticStateIndex[id].bitWidth != 0)
    return &context->nativeScheduleStaticStateIndex[id];
  auto found = context->nativeStaticStates.find(id);
  return found == context->nativeStaticStates.end() ? nullptr : &found->second;
}

static obelisk_rt_status resolveCheckedNativePackedRangeUnlocked(
    obelisk_rt_context *context, uint64_t handle, uint64_t globalBitCount,
    uint64_t &rootOffset, uint64_t &rootWidth, int64_t &coordinate) {
  rootOffset = 0;
  rootWidth = globalBitCount;
  uint32_t staticID = 0;
  if (decodeNativeStatic(handle, staticID, coordinate)) {
    const NativeStaticState *state = findNativeStaticState(context, staticID);
    if (!state || state->bitOffset > globalBitCount ||
        state->bitWidth > globalBitCount - state->bitOffset)
      return OBELISK_RT_INVALID_HANDLE;
    rootOffset = state->bitOffset;
    rootWidth = state->bitWidth;
    return OBELISK_RT_OK;
  }
  return decodeNativeGlobal(handle, coordinate) ? OBELISK_RT_OK
                                                : OBELISK_RT_INVALID_HANDLE;
}

void releaseOwnedNativeStates(obelisk_rt_context *context,
                              obelisk_rt_process_instance_v1 *instance) {
  if (!context || !instance)
    return;
  ContextMutexLock lock(context);
  for (auto state = context->nativeAutomaticStates.begin();
       state != context->nativeAutomaticStates.end();) {
    if (state->second.owner != instance) {
      ++state;
      continue;
    }
    state->second.owner = nullptr;
    if (state->second.referenceCount <= 1) {
      obelisk_rt_erase_automatic_bookkeeping_unlocked(context, state->first);
      state = context->nativeAutomaticStates.erase(state);
    } else {
      --state->second.referenceCount;
      ++state;
    }
  }
  instance->ownership_context = nullptr;
}

bool byteBit(const uint8_t *bytes, uint64_t bit) {
  return (bytes[bit / 8] & static_cast<uint8_t>(1u << (bit % 8))) != 0;
}

void setByteBit(uint8_t *bytes, uint64_t bit, bool value) {
  uint8_t mask = static_cast<uint8_t>(1u << (bit % 8));
  if (value)
    bytes[bit / 8] |= mask;
  else
    bytes[bit / 8] &= static_cast<uint8_t>(~mask);
}

bool validNativeStatePlanesUnlocked(const obelisk_rt_context *context,
                                    const uint8_t *value,
                                    const uint8_t *unknown, uint64_t bitCount) {
  return context && value && unknown && context->execution &&
         context->execution->state_bit_count == bitCount &&
         context->stateValue.size() == (bitCount + 63) / 64 &&
         context->stateUnknown.size() == context->stateValue.size();
}

bool importNativeStatePlanesUnlocked(obelisk_rt_context *context,
                                     const uint8_t *value,
                                     const uint8_t *unknown,
                                     uint64_t bitCount) {
  if (!validNativeStatePlanesUnlocked(context, value, unknown, bitCount))
    return false;
  std::fill(context->stateValue.begin(), context->stateValue.end(), 0);
  std::fill(context->stateUnknown.begin(), context->stateUnknown.end(), 0);
#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) ||  \
    defined(_WIN32)
  size_t byteCount = static_cast<size_t>((bitCount + 7) / 8);
  if (byteCount != 0) {
    std::memcpy(context->stateValue.data(), value, byteCount);
    std::memcpy(context->stateUnknown.data(), unknown, byteCount);
  }
#else
  for (uint64_t bit = 0; bit != bitCount; ++bit) {
    uint64_t mask = uint64_t{1} << (bit % 64);
    if (byteBit(value, bit))
      context->stateValue[bit / 64] |= mask;
    if (byteBit(unknown, bit))
      context->stateUnknown[bit / 64] |= mask;
  }
#endif
  if (bitCount % 64 != 0 && !context->stateValue.empty()) {
    uint64_t mask = (uint64_t{1} << (bitCount % 64)) - 1;
    context->stateValue.back() &= mask;
    context->stateUnknown.back() &= mask;
  }
  return true;
}

bool exportNativeStatePlanesUnlocked(const obelisk_rt_context *context,
                                     uint8_t *value, uint8_t *unknown,
                                     uint64_t bitCount) {
  if (!validNativeStatePlanesUnlocked(context, value, unknown, bitCount))
    return false;
  size_t byteCount = static_cast<size_t>((bitCount + 7) / 8);
#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) ||  \
    defined(_WIN32)
  if (byteCount != 0) {
    std::memcpy(value, context->stateValue.data(), byteCount);
    std::memcpy(unknown, context->stateUnknown.data(), byteCount);
    if (bitCount % 8 != 0) {
      uint8_t mask = static_cast<uint8_t>((1u << (bitCount % 8)) - 1);
      value[byteCount - 1] &= mask;
      unknown[byteCount - 1] &= mask;
    }
  }
#else
  std::fill_n(value, byteCount, uint8_t{0});
  std::fill_n(unknown, byteCount, uint8_t{0});
  for (uint64_t bit = 0; bit != bitCount; ++bit) {
    uint64_t mask = uint64_t{1} << (bit % 64);
    setByteBit(value, bit, (context->stateValue[bit / 64] & mask) != 0);
    setByteBit(unknown, bit, (context->stateUnknown[bit / 64] & mask) != 0);
  }
#endif
  return true;
}

bool reconcileNativeRootToPlanesUnlocked(
    const obelisk_rt_context *context,
    const obelisk_rt_native_schedule_plan *plan, uint32_t id) {
  if (!context || !plan ||
      !validNativeStatePlanesUnlocked(context, plan->state_value,
                                      plan->state_unknown,
                                      plan->state_bit_count))
    return false;
  const NativeStaticState *state = findNativeStaticState(context, id);
  if (!state || state->bitOffset > plan->state_bit_count ||
      state->bitWidth > plan->state_bit_count - state->bitOffset)
    return false;
  for (uint64_t local = 0; local != state->bitWidth; ++local) {
    uint64_t absolute = state->bitOffset + local;
    uint64_t mask = uint64_t{1} << (absolute % 64);
    setByteBit(plan->state_value, absolute,
               (context->stateValue[absolute / 64] & mask) != 0);
    setByteBit(plan->state_unknown, absolute,
               (context->stateUnknown[absolute / 64] & mask) != 0);
  }
  return true;
}

bool reconcileNativeDirtyRootsToPlanesUnlocked(
    const obelisk_rt_context *context,
    const obelisk_rt_native_schedule_plan *plan) {
  if (!context || !plan)
    return false;
  for (uint32_t id : context->nativeScheduleTransientDirtyRoots)
    if (!reconcileNativeRootToPlanesUnlocked(context, plan, id))
      return false;
  for (uint32_t id : context->nativeSchedulePersistentDirtyRoots)
    if (!reconcileNativeRootToPlanesUnlocked(context, plan, id))
      return false;
  return true;
}

bool nativeMaskIntersectsRange(const std::vector<uint64_t> &mask,
                               uint64_t bitOffset, uint64_t bitWidth) {
  if (bitWidth == 0 || bitOffset > UINT64_MAX - bitWidth)
    return false;
  uint64_t end = bitOffset + bitWidth;
  uint64_t firstLimb = bitOffset / 64;
  uint64_t lastLimb = (end - 1) / 64;
  if (firstLimb >= mask.size())
    return false;
  lastLimb = std::min<uint64_t>(lastLimb, mask.size() - 1);
  for (uint64_t limb = firstLimb; limb <= lastLimb; ++limb) {
    uint64_t selected = UINT64_MAX;
    if (limb == firstLimb)
      selected &= UINT64_MAX << (bitOffset % 64);
    if (limb == (end - 1) / 64 && end % 64 != 0)
      selected &= (uint64_t{1} << (end % 64)) - 1;
    if ((mask[limb] & selected) != 0)
      return true;
  }
  return false;
}

bool storeNativeScheduleStateUnlocked(obelisk_rt_context *context,
                                      uint64_t bitOffset, uint64_t bitWidth,
                                      uint64_t value, uint64_t unknown) {
  const obelisk_rt_native_schedule_plan *plan =
      context ? context->nativeSchedulePlan : nullptr;
  if (!plan || plan->state_bit_count == 0)
    return true;
  if (!plan->state_value || !plan->state_unknown || bitWidth == 0 ||
      bitWidth > 64 || bitOffset > plan->state_bit_count ||
      bitWidth > plan->state_bit_count - bitOffset)
    return false;
  storePackedBytes(plan->state_value, bitOffset, bitWidth, value);
  storePackedBytes(plan->state_unknown, bitOffset, bitWidth, unknown);
  return true;
}

namespace {

bool staticOverrideRange(obelisk_rt_context *context, uint64_t handle,
                         uint64_t width, uint64_t &absolute) {
  uint32_t id = 0;
  int64_t offset = 0;
  if (!decodeNativeStatic(handle, id, offset) || offset < 0)
    return false;
  auto found = context->nativeStaticStates.find(id);
  if (found == context->nativeStaticStates.end() ||
      static_cast<uint64_t>(offset) > found->second.bitWidth ||
      width > found->second.bitWidth - static_cast<uint64_t>(offset))
    return false;
  absolute = found->second.bitOffset + static_cast<uint64_t>(offset);
  return context->execution &&
         absolute <= context->execution->state_bit_count &&
         width <= context->execution->state_bit_count - absolute &&
         context->stateValue.size() ==
             (context->execution->state_bit_count + 63) / 64 &&
         context->stateUnknown.size() == context->stateValue.size();
}

} // namespace

bool obelisk_rt_publish_native_signal_transition_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    const uint8_t *newValue, const uint8_t *newUnknown,
    bool indexedExternalDeposit) {
  uint64_t sequence = 0;
  if (indexedExternalDeposit &&
      publishStaticAOTSignalTransitionUnlocked(
          context, stableID, bitWidth, changed, posedge, negedge, &sequence,
          true)) {
    obelisk_rt_invalidate_signal_snapshots_unlocked(context, stableID,
                                                    bitWidth);
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return context->schedulerStatus == OBELISK_RT_OK;
  }
  return publishNativeSignalTransitionUnlocked(context, stableID, bitWidth,
                                               changed, posedge, negedge,
                                               newValue, newUnknown);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_override(obelisk_rt_context *context, uint8_t *globalValue,
                              uint8_t *globalUnknown, uint64_t globalBitCount,
                              uint64_t handle, uint64_t bitWidth,
                              uint32_t descriptorKind, uint32_t assign,
                              const uint8_t *value, const uint8_t *unknown) {
  if (!context || !globalValue || !globalUnknown || !value || bitWidth == 0 ||
      bitWidth > UINT64_MAX - 7 || assign > 1 ||
      (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE &&
       descriptorKind != OBELISK_RT_DESCRIPTOR_NET) ||
      (assign && descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE) ||
      !context->execution ||
      context->execution->state_bit_count != globalBitCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    uint64_t absolute = 0;
    uint64_t byteCount = (bitWidth + 7) / 8;
    if (byteCount > std::numeric_limits<size_t>::max())
      return OBELISK_RT_INVALID_ARGUMENT;
    if (descriptorKind == OBELISK_RT_DESCRIPTOR_NET) {
      {
        ContextMutexLock lock(context);
        if (!staticOverrideRange(context, handle, bitWidth, absolute))
          return OBELISK_RT_INVALID_HANDLE;
      }
      return obelisk_rt_force_design_nets(context, absolute, bitWidth, value,
                                          unknown);
    }
    std::vector<uint8_t> oldValue(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> oldUnknown(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> publishedValue(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> publishedUnknown(static_cast<size_t>(byteCount), 0);
    {
      ContextMutexLock lock(context);
      if (!staticOverrideRange(context, handle, bitWidth, absolute))
        return OBELISK_RT_INVALID_HANDLE;
      size_t limbs = context->stateValue.size();
      if (assign) {
        if (context->assignMask.empty()) {
          context->assignMask.assign(limbs, 0);
          context->assignValue.assign(limbs, 0);
          context->assignUnknown.assign(limbs, 0);
        }
      } else if (context->forceMask.empty()) {
        context->forceMask.assign(limbs, 0);
      }
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        uint64_t destination = absolute + bit;
        uint64_t mask = uint64_t{1} << (destination % 64);
        uint64_t limb = destination / 64;
        bool oldV = (context->stateValue[limb] & mask) != 0;
        bool oldU = (context->stateUnknown[limb] & mask) != 0;
        bool nextV = byteBit(value, bit);
        bool nextU = unknown && byteBit(unknown, bit);
        setByteBit(oldValue.data(), bit, oldV);
        setByteBit(oldUnknown.data(), bit, oldU);
        if (assign) {
          context->assignMask[limb] |= mask;
          context->assignValue[limb] = nextV
                                           ? context->assignValue[limb] | mask
                                           : context->assignValue[limb] & ~mask;
          context->assignUnknown[limb] =
              nextU ? context->assignUnknown[limb] | mask
                    : context->assignUnknown[limb] & ~mask;
          if (limb < context->forceMask.size() &&
              (context->forceMask[limb] & mask) != 0) {
            nextV = oldV;
            nextU = oldU;
          }
        } else {
          context->forceMask[limb] |= mask;
        }
        context->stateValue[limb] = nextV ? context->stateValue[limb] | mask
                                          : context->stateValue[limb] & ~mask;
        context->stateUnknown[limb] = nextU
                                          ? context->stateUnknown[limb] | mask
                                          : context->stateUnknown[limb] & ~mask;
        setByteBit(globalValue, destination, nextV);
        setByteBit(globalUnknown, destination, nextU);
        setByteBit(publishedValue.data(), bit, nextV);
        setByteBit(publishedUnknown.data(), bit, nextU);
      }
      obelisk_rt_aot_external_write_range_unlocked(context, absolute, bitWidth,
                                                   true);
    }
    publishOverrideEstablishmentTransition(
        context, handle, bitWidth, oldValue.data(), oldUnknown.data(),
        publishedValue.data(), publishedUnknown.data());
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_release_override(
    obelisk_rt_context *context, uint8_t *globalValue, uint8_t *globalUnknown,
    uint64_t globalBitCount, uint64_t handle, uint64_t bitWidth,
    uint32_t descriptorKind, uint32_t assign) {
  if (!context || !globalValue || !globalUnknown || bitWidth == 0 ||
      bitWidth > UINT64_MAX - 7 || assign > 1 ||
      (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE &&
       descriptorKind != OBELISK_RT_DESCRIPTOR_NET) ||
      (assign && descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE) ||
      !context->execution ||
      context->execution->state_bit_count != globalBitCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    uint64_t absolute = 0;
    uint64_t byteCount = (bitWidth + 7) / 8;
    if (byteCount > std::numeric_limits<size_t>::max())
      return OBELISK_RT_INVALID_ARGUMENT;
    if (descriptorKind == OBELISK_RT_DESCRIPTOR_NET) {
      {
        ContextMutexLock lock(context);
        if (!staticOverrideRange(context, handle, bitWidth, absolute))
          return OBELISK_RT_INVALID_HANDLE;
      }
      return obelisk_rt_release_design_nets(context, absolute, bitWidth);
    }
    std::vector<uint8_t> oldValue(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> oldUnknown(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> publishedValue(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> publishedUnknown(static_cast<size_t>(byteCount), 0);
    bool changed = false;
    {
      ContextMutexLock lock(context);
      if (!staticOverrideRange(context, handle, bitWidth, absolute))
        return OBELISK_RT_INVALID_HANDLE;
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        uint64_t destination = absolute + bit;
        uint64_t mask = uint64_t{1} << (destination % 64);
        uint64_t limb = destination / 64;
        bool oldV = (context->stateValue[limb] & mask) != 0;
        bool oldU = (context->stateUnknown[limb] & mask) != 0;
        setByteBit(oldValue.data(), bit, oldV);
        setByteBit(oldUnknown.data(), bit, oldU);
        bool nextV = oldV, nextU = oldU;
        if (assign) {
          if (limb < context->assignMask.size())
            context->assignMask[limb] &= ~mask;
        } else {
          if (limb < context->forceMask.size())
            context->forceMask[limb] &= ~mask;
        }
        bool forced = limb < context->forceMask.size() &&
                      (context->forceMask[limb] & mask) != 0;
        bool assigned = limb < context->assignMask.size() &&
                        (context->assignMask[limb] & mask) != 0;
        bool retained = limb < context->continuousMask.size() &&
                        (context->continuousMask[limb] & mask) != 0;
        if (!forced && (assigned || retained)) {
          if (assigned) {
            nextV = (context->assignValue[limb] & mask) != 0;
            nextU = (context->assignUnknown[limb] & mask) != 0;
          } else {
            nextV = (context->continuousValue[limb] & mask) != 0;
            nextU = (context->continuousUnknown[limb] & mask) != 0;
          }
          context->stateValue[limb] = nextV
                                          ? context->stateValue[limb] | mask
                                          : context->stateValue[limb] & ~mask;
          context->stateUnknown[limb] =
              nextU ? context->stateUnknown[limb] | mask
                    : context->stateUnknown[limb] & ~mask;
          setByteBit(globalValue, destination, nextV);
          setByteBit(globalUnknown, destination, nextU);
          changed |= oldV != nextV || oldU != nextU;
        }
        setByteBit(publishedValue.data(), bit, nextV);
        setByteBit(publishedUnknown.data(), bit, nextU);
      }
      obelisk_rt_aot_release_range_unlocked(context, absolute, bitWidth);
    }
    if (changed)
      obelisk_rt_v1_scheduler_signal_transition(
          context, handle, bitWidth, oldValue.data(), oldUnknown.data(),
          publishedValue.data(), publishedUnknown.data());
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_load_plane(
    obelisk_rt_context *context, const uint8_t *globalPlane,
    uint64_t globalBitCount, uint64_t handle, uint64_t bitWidth,
    uint32_t unknownPlane, uint32_t fallback, uint8_t *outValue) {
  if (!context || !globalPlane || !outValue || bitWidth == 0 ||
      bitWidth > UINT64_MAX - 7 || unknownPlane > 1 || fallback > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (byteCount > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memset(outValue, fallback ? UINT8_MAX : 0,
              static_cast<size_t>(byteCount));
  auto maskPadding = [&] {
    if (uint32_t remainder = static_cast<uint32_t>(bitWidth % 8))
      outValue[byteCount - 1] &= static_cast<uint8_t>((1u << remainder) - 1);
  };
  if (handle == UINT64_MAX) {
    maskPadding();
    return OBELISK_RT_OK;
  }
  try {
    ContextMutexLock lock(context);
    uint32_t id = 0;
    int64_t offset = 0;
    if (decodeNativeAutomatic(handle, id, offset)) {
      auto found = context->nativeAutomaticStates.find(id);
      if (found == context->nativeAutomaticStates.end()) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
      const NativeAutomaticState &state = found->second;
      if (state.managedRootRegistered) {
        if (unknownPlane != 0 || offset != 0 || bitWidth != 64)
          return OBELISK_RT_INVALID_HANDLE;
        std::memcpy(outValue, &state.managedValue, sizeof(state.managedValue));
        return OBELISK_RT_OK;
      }
      const std::vector<uint8_t> &plane =
          unknownPlane ? state.unknown : state.value;
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        int64_t source = 0;
        if (addHandleOffset(offset, bit, source) && source >= 0 &&
            static_cast<uint64_t>(source) < state.bitWidth && !plane.empty())
          setByteBit(outValue, bit,
                     byteBit(plane.data(), static_cast<uint64_t>(source)));
      }
      maskPadding();
      return OBELISK_RT_OK;
    }
    uint64_t rootOffset = 0;
    uint64_t rootWidth = 0;
    int64_t globalOffset = 0;
    obelisk_rt_status rangeStatus = resolveCheckedNativePackedRangeUnlocked(
        context, handle, globalBitCount, rootOffset, rootWidth, globalOffset);
    if (rangeStatus != OBELISK_RT_OK) {
      context->schedulerStatus = rangeStatus;
      return rangeStatus;
    }
    bool canonical = context->execution &&
                     context->execution->state_bit_count == globalBitCount;
    const std::vector<uint64_t> *canonicalPlane = nullptr;
    if (canonical) {
      canonicalPlane =
          unknownPlane ? &context->stateUnknown : &context->stateValue;
      if (canonicalPlane->size() != (globalBitCount + 63) / 64)
        return OBELISK_RT_INVALID_DESIGN;
    }
    if (canonical && bitWidth <= 64 && globalOffset >= 0 &&
        static_cast<uint64_t>(globalOffset) <= rootWidth &&
        bitWidth <= rootWidth - static_cast<uint64_t>(globalOffset)) {
      uint64_t source = rootOffset + static_cast<uint64_t>(globalOffset);
      bool readGlobal = !context->observerForcesCanonicalPlane &&
                        isStaticControlAOT(context);
      uint64_t globalValue = loadPackedBytes(globalPlane, source, bitWidth);
      uint64_t canonicalValue =
          loadPackedBits(*canonicalPlane, source, bitWidth);
      uint64_t overrideMask = 0;
      if (!context->forceMask.empty())
        overrideMask |=
            loadPackedBits(context->forceMask, source, bitWidth);
      if (!context->assignMask.empty())
        overrideMask |=
            loadPackedBits(context->assignMask, source, bitWidth);
      uint64_t value =
          readGlobal ? globalValue
                     : (canonicalValue & ~overrideMask) |
                           (globalValue & overrideMask);
      for (uint64_t byte = 0; byte != byteCount; ++byte)
        outValue[byte] = static_cast<uint8_t>(value >> (byte * 8));
      maskPadding();
      return OBELISK_RT_OK;
    }
    bool readGlobalPlane =
        !canonical ||
        (!context->observerForcesCanonicalPlane &&
         isStaticControlAOT(context));
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      int64_t coordinate = 0;
      if (addHandleOffset(globalOffset, bit, coordinate) && coordinate >= 0 &&
          static_cast<uint64_t>(coordinate) < rootWidth) {
        uint64_t source = rootOffset + static_cast<uint64_t>(coordinate);
        uint64_t sourceMask = uint64_t{1} << (source % 64);
        bool overridden =
            (source / 64 < context->forceMask.size() &&
             (context->forceMask[source / 64] & sourceMask) != 0) ||
            (source / 64 < context->assignMask.size() &&
             (context->assignMask[source / 64] & sourceMask) != 0);
        setByteBit(
            outValue, bit,
            readGlobalPlane || overridden
                ? byteBit(globalPlane, source)
                : (((*canonicalPlane)[source / 64] >> (source % 64)) & 1) != 0);
      }
    }
    maskPadding();
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

static obelisk_rt_status nativeStateStorePlane(
    obelisk_rt_context *context, uint8_t *globalPlane, uint64_t globalBitCount,
    uint64_t handle, uint64_t bitWidth, uint32_t unknownPlane,
    const uint8_t *value, uint8_t *outChanged, bool continuous) {
  if (!context || !globalPlane || !value || !outChanged || bitWidth == 0 ||
      bitWidth > UINT64_MAX - 7 || unknownPlane > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (byteCount > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  *outChanged = 0;
  if (handle == UINT64_MAX)
    return OBELISK_RT_OK;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return context->schedulerStatus;
    }
    uint32_t id = 0;
    int64_t offset = 0;
    if (decodeNativeAutomatic(handle, id, offset)) {
      auto found = context->nativeAutomaticStates.find(id);
      if (found == context->nativeAutomaticStates.end()) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
      NativeAutomaticState &state = found->second;
      if (state.managedRootRegistered) {
        if (unknownPlane != 0 || offset != 0 || bitWidth != 64)
          return OBELISK_RT_INVALID_HANDLE;
        obelisk_rt_object_v1 *managed = nullptr;
        std::memcpy(&managed, value, sizeof(managed));
        if (!obelisk_rt_managed_object_belongs_to(context, managed))
          return OBELISK_RT_INVALID_HANDLE;
        *outChanged = managed != state.managedValue;
        state.managedValue = managed;
        return OBELISK_RT_OK;
      }
      std::vector<uint8_t> &plane = unknownPlane ? state.unknown : state.value;
      if (plane.empty())
        return OBELISK_RT_INVALID_HANDLE;
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        int64_t coordinate = 0;
        if (!addHandleOffset(offset, bit, coordinate) || coordinate < 0 ||
            static_cast<uint64_t>(coordinate) >= state.bitWidth)
          continue;
        uint64_t destination = static_cast<uint64_t>(coordinate);
        bool old = byteBit(plane.data(), destination);
        bool next = byteBit(value, bit);
        *outChanged |= old != next;
        setByteBit(plane.data(), destination, next);
      }
      return OBELISK_RT_OK;
    }
    uint64_t rootOffset = 0;
    uint64_t rootWidth = 0;
    int64_t globalOffset = 0;
    obelisk_rt_status rangeStatus = resolveCheckedNativePackedRangeUnlocked(
        context, handle, globalBitCount, rootOffset, rootWidth, globalOffset);
    if (rangeStatus != OBELISK_RT_OK) {
      context->schedulerStatus = rangeStatus;
      return rangeStatus;
    }
    bool canonical = context->execution &&
                     context->execution->state_bit_count == globalBitCount;
    std::vector<uint64_t> *canonicalPlane = nullptr;
    if (canonical) {
      canonicalPlane =
          unknownPlane ? &context->stateUnknown : &context->stateValue;
      if (canonicalPlane->size() != (globalBitCount + 63) / 64)
        return OBELISK_RT_INVALID_DESIGN;
      if (continuous && context->continuousMask.empty()) {
        context->continuousMask.assign(canonicalPlane->size(), 0);
        context->continuousValue.assign(canonicalPlane->size(), 0);
        context->continuousUnknown.assign(canonicalPlane->size(), 0);
      }
    }
    if (canonical && bitWidth <= 64 && globalOffset >= 0 &&
        static_cast<uint64_t>(globalOffset) <= rootWidth &&
        bitWidth <= rootWidth - static_cast<uint64_t>(globalOffset)) {
      uint64_t destination = rootOffset + static_cast<uint64_t>(globalOffset);
      bool masked =
          (!context->forceMask.empty() &&
           loadPackedBits(context->forceMask, destination, bitWidth) != 0) ||
          (!context->assignMask.empty() &&
           loadPackedBits(context->assignMask, destination, bitWidth) != 0);
      uint64_t next = 0;
      for (uint64_t byte = 0; byte != byteCount; ++byte)
        next |= uint64_t{value[byte]} << (byte * 8);
      next &= packedWidthMask(bitWidth);
      if (continuous) {
        std::vector<uint64_t> &retained =
            unknownPlane ? context->continuousUnknown
                         : context->continuousValue;
        storePackedBits(retained, destination, bitWidth, next);
        storePackedBits(context->continuousMask, destination, bitWidth,
                        packedWidthMask(bitWidth));
      }
      if (!masked) {
        uint64_t old =
            isStaticControlAOT(context)
                ? loadPackedBytes(globalPlane, destination, bitWidth)
                : loadPackedBits(*canonicalPlane, destination, bitWidth);
        *outChanged = old != next;
        storePackedBytes(globalPlane, destination, bitWidth, next);
        storePackedBits(*canonicalPlane, destination, bitWidth, next);
        return OBELISK_RT_OK;
      }
    }
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      int64_t coordinate = 0;
      if (!addHandleOffset(globalOffset, bit, coordinate) || coordinate < 0 ||
          static_cast<uint64_t>(coordinate) >= rootWidth)
        continue;
      uint64_t destination = rootOffset + static_cast<uint64_t>(coordinate);
      uint64_t destinationMask = uint64_t{1} << (destination % 64);
      bool forced =
          destination / 64 < context->forceMask.size() &&
          (context->forceMask[destination / 64] & destinationMask) != 0;
      bool assigned =
          destination / 64 < context->assignMask.size() &&
          (context->assignMask[destination / 64] & destinationMask) != 0;
      bool next = byteBit(value, bit);
      if (canonical && continuous) {
        std::vector<uint64_t> &retained =
            unknownPlane ? context->continuousUnknown
                         : context->continuousValue;
        uint64_t &retainedLimb = retained[destination / 64];
        retainedLimb = next ? retainedLimb | destinationMask
                            : retainedLimb & ~destinationMask;
        context->continuousMask[destination / 64] |= destinationMask;
      }
      if (canonical && (forced || assigned))
        continue;
      bool old =
          canonical
              ? (((*canonicalPlane)[destination / 64] >> (destination % 64)) &
                 1) != 0
              : byteBit(globalPlane, destination);
      *outChanged |= old != next;
      setByteBit(globalPlane, destination, next);
      if (canonical) {
        uint64_t mask = uint64_t{1} << (destination % 64);
        uint64_t &limb = (*canonicalPlane)[destination / 64];
        limb = next ? limb | mask : limb & ~mask;
      }
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_store_plane(
    obelisk_rt_context *context, uint8_t *globalPlane, uint64_t globalBitCount,
    uint64_t handle, uint64_t bitWidth, uint32_t unknownPlane,
    const uint8_t *value, uint8_t *outChanged) {
  return nativeStateStorePlane(context, globalPlane, globalBitCount, handle,
                               bitWidth, unknownPlane, value, outChanged,
                               false);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_store_continuous_plane(
    obelisk_rt_context *context, uint8_t *globalPlane, uint64_t globalBitCount,
    uint64_t handle, uint64_t bitWidth, uint32_t unknownPlane,
    const uint8_t *value, uint8_t *outChanged) {
  return nativeStateStorePlane(context, globalPlane, globalBitCount, handle,
                               bitWidth, unknownPlane, value, outChanged,
                               true);
}
