//===- ProcessNBA.cpp - Non-blocking assignment staging and commit -------===//
//
// Staging and commit of non-blocking assignments: the generic scheduled-NBA
// queues, the packed/wide static NBA fast paths, the generated 256-bit
// accumulators (including their AVX2 kernels), and the inline barrier commit
// used by native AOT plans.  Split out of Process.cpp.
//
//===----------------------------------------------------------------------===//

#include "DesignBytecodeNets.h"
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

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

using namespace obelisk::process;
using namespace obelisk::runtime;

std::optional<uint64_t> countGeneratedNBAStages(
    const obelisk_rt_generated_nba_accumulator_256 &generated) {
  uint64_t writtenLanes = 0;
  for (uint64_t mask : generated.write_mask)
    for (unsigned shift : {0u, 32u}) {
      uint32_t lane = static_cast<uint32_t>(mask >> shift);
      if (lane == 0)
        continue;
      if (lane != UINT32_MAX)
        return std::nullopt;
      ++writtenLanes;
    }
  if (writtenLanes == 0)
    return std::nullopt;
  return writtenLanes;
}

bool hasGeneratedNBAStages(
    const obelisk_rt_generated_nba_accumulator_256 &generated) {
  return generated.valid != 0;
}

static bool validInertialStatePlanesUnlocked(const obelisk_rt_context *context,
                                             const uint8_t *valuePlane,
                                             const uint8_t * /*unknownPlane*/,
                                             uint64_t planeBitCount) {
  // Generated coroutine, eval, and AOT schedules can each own a distinct
  // long-lived native plane even while the runtime plan points at another
  // tier's plane. As with the native load/store ABI, validate the shared
  // layout rather than pointer identity; the caller-provided plane is the
  // destination that must be reconciled when this delayed write commits.
  if (!context || !valuePlane)
    return false;
  if (context->execution &&
      planeBitCount == context->execution->state_bit_count &&
      context->stateValue.size() == (planeBitCount + 63) / 64 &&
      context->stateUnknown.size() == context->stateValue.size())
    return true;
  // Native planes may include allocation padding beyond the semantic design
  // width passed by the generated operation.
  if (context->nativeSchedulePlan &&
      planeBitCount <= context->nativeSchedulePlan->state_bit_count)
    return true;
  if (context->nativeStateBitCount != 0 &&
      planeBitCount <= context->nativeStateBitCount)
    return true;
  // Coroutine-only native execution has neither a bytecode image nor an AOT
  // plan installed. Its generated global is nevertheless a stable full state
  // plane, and the registered static roots below provide the range checks.
  return planeBitCount != 0;
}

static bool validInertialStrengthPairUnlocked(
    obelisk_rt_context *context, const NativeStaticState &lowState,
    int64_t lowOffset, const NativeStaticState &highState, int64_t highOffset,
    uint64_t width) {
  if (!context || !context->designBytecodeImage.data || lowOffset < 0 ||
      highOffset < 0)
    return false;
  const auto &image = context->designBytecodeImage;
  uint64_t lowAbsolute = lowState.bitOffset + static_cast<uint64_t>(lowOffset);
  uint64_t highAbsolute =
      highState.bitOffset + static_cast<uint64_t>(highOffset);
  return obelisk::designbytecode::isComplementaryDriverPair(
      image, context, lowAbsolute, highAbsolute, width);
}

void markStaticNBAAccumulatorPending(obelisk_rt_context *context,
                                     uint32_t rootIndex,
                                     StaticNBAAccumulator &accumulator) {
  accumulator.valid = true;
  context->staticNBAAccumulatorsPending = true;
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  if (plan && plan->nba_dirty_roots && rootIndex < plan->nba_root_count) {
    uint32_t word = rootIndex / 64;
    if (word < plan->nba_dirty_word_count) {
      plan->nba_dirty_roots[word] |= uint64_t{1} << (rootIndex % 64);
      uint32_t summaryWord = word / 64;
      if (plan->nba_dirty_summary &&
          summaryWord < plan->nba_dirty_summary_word_count)
        plan->nba_dirty_summary[summaryWord] |= uint64_t{1} << (word % 64);
    }
  }
}

void refreshStaticNBAAccumulatorsPending(obelisk_rt_context *context) {
  context->staticNBAAccumulatorsPending =
      std::any_of(context->staticNBAAccumulators.begin(),
                  context->staticNBAAccumulators.end(),
                  [](const StaticNBAAccumulator &accumulator) {
                    return accumulator.valid;
                  });
}

static obelisk_rt_status schedulerNBA(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown,
    bool stringValue, uint64_t staticSite = UINT64_MAX, bool driver = false,
    uint64_t clockingOutput = UINT64_MAX, uint64_t sourceBitOffset = 0) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto fail = [&](obelisk_rt_status status) {
    obelisk_rt_v1_scheduler_fail(context, status);
    return status;
  };
  if (!valuePlane || bitWidth == 0 || (bitWidth + 7) < bitWidth ||
      sourceBitOffset > UINT64_MAX - bitWidth ||
      (stringValue && (bitWidth != 64 || unknownPlane || sourceBitOffset != 0)))
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  if (bitOffset == UINT64_MAX)
    return OBELISK_RT_OK;
  ContextTransaction transaction(context);
  uint32_t automaticID = 0;
  uint32_t staticID = 0;
  int64_t offset = 0;
  bool automatic = decodeNativeAutomatic(bitOffset, automaticID, offset);
  bool boundedStatic =
      !automatic && decodeNativeStatic(bitOffset, staticID, offset);
  if (!automatic && !boundedStatic && !decodeNativeGlobal(bitOffset, offset))
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  if (!automatic && !boundedStatic &&
      (offset >= static_cast<__int128>(planeBitCount) ||
       static_cast<__int128>(offset) + bitWidth <= 0))
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (!value || (unknownPlane && !unknown) ||
      byteCount > std::numeric_limits<size_t>::max())
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  auto sourceBit = [&](const uint8_t *plane, uint64_t bit) {
    return byteBit(plane, sourceBitOffset + bit);
  };
  obelisk_rt_string_v1 queuedString = 0;
  if (stringValue) {
    std::memcpy(&queuedString, value, sizeof(queuedString));
    obelisk_rt_status status =
        obelisk_rt_validate_string(context, queuedString);
    if (status != OBELISK_RT_OK)
      return fail(status);
  }
  OBELISK_RT_TRY {
    ScheduledNBA update;
    update.valuePlane = valuePlane;
    update.unknownPlane = unknownPlane;
    update.planeBitCount = planeBitCount;
    update.bitOffset = bitOffset;
    update.bitWidth = bitWidth;
    update.clockingOutput = clockingOutput;
    update.stringValue = stringValue;
    update.driver = driver;
    update.rootedString = queuedString;
    ContextMutexLock lock(context);
    const NativeStaticState *staticState = nullptr;
    if (automatic) {
      auto found = context->nativeAutomaticStates.find(automaticID);
      if (found == context->nativeAutomaticStates.end() ||
          offset >= static_cast<__int128>(found->second.bitWidth) ||
          static_cast<__int128>(offset) + bitWidth <= 0) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
      if (found->second.referenceCount == UINT64_MAX) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return OBELISK_RT_OUT_OF_RESOURCES;
      }
      update.retainedAutomaticID = automaticID;
    } else if (boundedStatic) {
      staticState = findNativeStaticState(context, staticID);
      if (!staticState || staticState->bitOffset > planeBitCount ||
          staticState->bitWidth > planeBitCount - staticState->bitOffset ||
          offset >= static_cast<__int128>(staticState->bitWidth) ||
          static_cast<__int128>(offset) + bitWidth <= 0) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
    }
    update.execRegion = obelisk_rt_commit_region(
        context->activeHomeRegion == UINT32_MAX
            ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
            : context->activeHomeRegion);
    if (update.execRegion == UINT32_MAX) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return OBELISK_RT_INVALID_LIFECYCLE;
    }
    if (context->nextSchedulerSequence == 0) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    if (!driver && staticSite != UINT64_MAX && boundedStatic && !stringValue &&
        delay == 0 && context->nativeSchedulePlan &&
        !context->nativeScheduleDeoptimized &&
        context->nativeScheduleNBASiteCount != 0) {
      uint32_t staticRoot = UINT32_MAX;
      if (staticSite < context->nativeScheduleNBASiteIndex.size()) {
        staticRoot = context->nativeScheduleNBASiteIndex[staticSite];
      } else {
        const obelisk_rt_static_nba_site *begin =
            context->nativeScheduleNBASites;
        const obelisk_rt_static_nba_site *end =
            begin + context->nativeScheduleNBASiteCount;
        const obelisk_rt_static_nba_site *found = std::lower_bound(
            begin, end, staticSite,
            [](const auto &entry, uint64_t id) { return entry.site < id; });
        if (found != end && found->site == staticSite)
          staticRoot = found->root;
      }
      if (staticRoot == UINT32_MAX ||
          staticRoot >= context->staticNBAAccumulators.size() ||
          staticRoot >= context->nativeScheduleNBARootCount)
        return fail(OBELISK_RT_INVALID_DESIGN);
      const obelisk_rt_static_nba_root &root =
          context->nativeScheduleNBARoots[staticRoot];
      if (root.static_state != staticID ||
          root.bit_width != staticState->bitWidth)
        return fail(OBELISK_RT_LAYOUT_MISMATCH);
      if (root.generated_accumulator) {
        // A source-ordered generic site may follow generated direct stages for
        // the same root. Materialize first so the generic write remains the
        // last write at the barrier.
        if (obelisk_rt_status status =
                materializeGeneratedNBAAccumulatorUnlocked(context, staticRoot,
                                                           update.execRegion);
            status != OBELISK_RT_OK)
          return fail(status);
      }
      if (staticRoot >= context->staticNBASlowRoots.size())
        return fail(OBELISK_RT_INVALID_DESIGN);
      bool rootDirty =
          context->nativeScheduleTransientDirtyRoots.find(root.static_state) !=
              context->nativeScheduleTransientDirtyRoots.end() ||
          context->nativeSchedulePersistentDirtyRoots.find(root.static_state) !=
              context->nativeSchedulePersistentDirtyRoots.end();
      if (!rootDirty && context->staticNBASlowRoots[staticRoot] == 0) {
        // Both v1 storage classes are immediate and root-bounded. FixedSlot
        // proves site uniqueness to the compiler; after value capture it can
        // share the root accumulator's ordered last-write merge.
        StaticNBAAccumulator &accumulator =
            context->staticNBAAccumulators[staticRoot];
        if (accumulator.valid &&
            (accumulator.valuePlane != valuePlane ||
             accumulator.unknownPlane != unknownPlane ||
             accumulator.planeBitCount != planeBitCount ||
             accumulator.execRegion != update.execRegion)) {
          context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
          return OBELISK_RT_INVALID_LIFECYCLE;
        }
        accumulator.valuePlane = valuePlane;
        accumulator.unknownPlane = unknownPlane;
        accumulator.planeBitCount = planeBitCount;
        accumulator.execRegion = update.execRegion;
        markStaticNBAAccumulatorPending(context, staticRoot, accumulator);
        bool packedStage =
            bitWidth <= 64 && offset >= 0 &&
            static_cast<uint64_t>(offset) <= root.bit_width &&
            bitWidth <= root.bit_width - static_cast<uint64_t>(offset);
        if (packedStage) {
          uint64_t packedValue = 0;
          uint64_t packedUnknown = 0;
          for (uint64_t bit = 0; bit != bitWidth; ++bit) {
            packedValue |= uint64_t{sourceBit(value, bit)} << bit;
            if (unknownPlane)
              packedUnknown |= uint64_t{sourceBit(unknown, bit)} << bit;
          }
          uint64_t sourceMask = packedWidthMask(bitWidth);
          packedValue &= sourceMask;
          packedUnknown &= sourceMask;
          uint64_t destination = static_cast<uint64_t>(offset);
          size_t word = static_cast<size_t>(destination / 64);
          unsigned shift = static_cast<unsigned>(destination % 64);
          uint64_t lowMask = sourceMask << shift;
          accumulator.value[word] =
              (accumulator.value[word] & ~lowMask) | (packedValue << shift);
          accumulator.unknown[word] =
              (accumulator.unknown[word] & ~lowMask) | (packedUnknown << shift);
          accumulator.writeMask[word] |= lowMask;
          if (shift != 0 && bitWidth > 64 - shift) {
            uint64_t highMask = sourceMask >> (64 - shift);
            accumulator.value[word + 1] =
                (accumulator.value[word + 1] & ~highMask) |
                (packedValue >> (64 - shift));
            accumulator.unknown[word + 1] =
                (accumulator.unknown[word + 1] & ~highMask) |
                (packedUnknown >> (64 - shift));
            accumulator.writeMask[word + 1] |= highMask;
          }
        } else {
          __int128 firstWide =
              std::max<__int128>(0, -static_cast<__int128>(offset));
          __int128 lastWide = std::min<__int128>(
              bitWidth, static_cast<__int128>(root.bit_width) - offset);
          if (firstWide >= lastWide) {
            context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
            return OBELISK_RT_INVALID_HANDLE;
          }
          uint64_t first = static_cast<uint64_t>(firstWide);
          uint64_t last = static_cast<uint64_t>(lastWide);
          auto *accValue =
              reinterpret_cast<uint8_t *>(accumulator.value.data());
          auto *accUnknown =
              reinterpret_cast<uint8_t *>(accumulator.unknown.data());
          auto *accMask =
              reinterpret_cast<uint8_t *>(accumulator.writeMask.data());
          for (uint64_t source = first; source < last; ++source) {
            uint64_t destination =
                static_cast<uint64_t>(static_cast<__int128>(offset) + source);
            setByteBit(accValue, destination, sourceBit(value, source));
            setByteBit(accUnknown, destination,
                       unknownPlane && sourceBit(unknown, source));
            setByteBit(accMask, destination, true);
          }
        }
        accumulator.sequence = context->nextSchedulerSequence++;
        ++context->signalDiagnostics.aotNBAStages;
        return OBELISK_RT_OK;
      }
    }
    if (boundedStatic && !stringValue && delay == 0 &&
        context->nativeSchedulePlan) {
      for (uint32_t root = 0; root != context->nativeScheduleNBARootCount;
           ++root)
        if (context->nativeScheduleNBARoots[root].static_state == staticID) {
          if (root >= context->staticNBASlowRoots.size())
            return fail(OBELISK_RT_INVALID_DESIGN);
          if (obelisk_rt_status status =
                  materializeGeneratedNBAAccumulatorUnlocked(context, root,
                                                             update.execRegion);
              status != OBELISK_RT_OK)
            return fail(status);
          context->staticNBASlowRoots[root] = 1;
          context->staticNBASlowRootsPresent = true;
          invalidateNativeStaticSpecializationFastUnlocked(context);
          break;
        }
    }
    update.inlinePacked =
        !automatic && boundedStatic && !stringValue && delay == 0 &&
        bitWidth <= 64 && offset >= 0 && staticState &&
        static_cast<uint64_t>(offset) <= staticState->bitWidth &&
        bitWidth <= staticState->bitWidth - static_cast<uint64_t>(offset) &&
        context->nativeSchedulePlan && !context->nativeScheduleDeoptimized &&
        (context->nativeSchedulePlan->flags &
         OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC) != 0;
    if (update.inlinePacked) {
      for (uint64_t bit = 0; bit != bitWidth; ++bit)
        update.inlineValue |= uint64_t{sourceBit(value, bit)} << bit;
      if (unknownPlane)
        for (uint64_t bit = 0; bit != bitWidth; ++bit)
          update.inlineUnknown |= uint64_t{sourceBit(unknown, bit)} << bit;
      ++context->signalDiagnostics.aotNBAStages;
    } else {
      update.value.assign(static_cast<size_t>(byteCount), 0);
      if (unknownPlane)
        update.unknown.assign(static_cast<size_t>(byteCount), 0);
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        setByteBit(update.value.data(), bit, sourceBit(value, bit));
        if (unknownPlane)
          setByteBit(update.unknown.data(), bit, sourceBit(unknown, bit));
      }
    }
    update.sequence = context->nextSchedulerSequence++;
    update.dueTime = delay > UINT64_MAX - context->schedulerTime
                         ? UINT64_MAX
                         : context->schedulerTime + delay;
    context->scheduledNBAs.push_back(std::move(update));
    if (automatic)
      ++context->nativeAutomaticStates.find(automaticID)->second.referenceCount;
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

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_nba(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown) {
  return schedulerNBA(context, valuePlane, unknownPlane, planeBitCount,
                      bitOffset, bitWidth, delay, value, unknown, false);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_packed_slice_nba(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t baseHandle, uint64_t baseBitWidth,
    int64_t lowBit, uint32_t lowBitValid, uint64_t sourceBitWidth,
    uint64_t delay, uint64_t staticSite, uint64_t clockingOutput,
    const uint8_t *value, const uint8_t *unknown) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!lowBitValid || baseHandle == UINT64_MAX)
    return OBELISK_RT_OK;
  if (baseBitWidth == 0 || sourceBitWidth == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  __int128 low = lowBit;
  __int128 first = std::max<__int128>(0, -low);
  __int128 last = std::min<__int128>(sourceBitWidth,
                                     static_cast<__int128>(baseBitWidth) - low);
  if (first >= last)
    return OBELISK_RT_OK;
  __int128 destinationOffset = low + first;
  if (destinationOffset < 0 || destinationOffset > INT64_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t selectedWidth = static_cast<uint64_t>(last - first);
  uint64_t sourceOffset = static_cast<uint64_t>(first);
  if (sourceOffset > sourceBitWidth ||
      selectedWidth > sourceBitWidth - sourceOffset)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t destination = obelisk_rt_stable_handle_offset(
      baseHandle, static_cast<int64_t>(destinationOffset));
  if (destination == UINT64_MAX)
    return OBELISK_RT_INVALID_HANDLE;
  return schedulerNBA(context, valuePlane, unknownPlane, planeBitCount,
                      destination, selectedWidth, delay, value, unknown, false,
                      staticSite, false, clockingOutput, sourceOffset);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_driver_nba(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown) {
  return schedulerNBA(context, valuePlane, unknownPlane, planeBitCount,
                      bitOffset, bitWidth, delay, value, unknown, false,
                      UINT64_MAX, true);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_inertial_driver(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t codeUnit, uint32_t component, uint32_t flags, uint64_t riseDelay,
    uint64_t fallDelay, uint64_t turnoffDelay, const uint8_t *value,
    const uint8_t *unknown) {
  if (!context || !context->execution || !valuePlane || bitWidth == 0 ||
      codeUnit == UINT64_MAX ||
      (flags & ~(OBELISK_RT_INERTIAL_DRIVER_VECTOR_DELAY |
                 OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION |
                 OBELISK_RT_INERTIAL_DRIVER_PUBLISH_RAW |
                 OBELISK_RT_INERTIAL_DRIVER_REAL32 |
                 OBELISK_RT_INERTIAL_DRIVER_REAL64)) != 0 ||
      ((flags & OBELISK_RT_INERTIAL_DRIVER_REAL32) != 0 && bitWidth != 32) ||
      ((flags & OBELISK_RT_INERTIAL_DRIVER_REAL64) != 0 && bitWidth != 64) ||
      (flags & (OBELISK_RT_INERTIAL_DRIVER_REAL32 |
                OBELISK_RT_INERTIAL_DRIVER_REAL64)) ==
          (OBELISK_RT_INERTIAL_DRIVER_REAL32 |
           OBELISK_RT_INERTIAL_DRIVER_REAL64) ||
      ((flags & (OBELISK_RT_INERTIAL_DRIVER_REAL32 |
                 OBELISK_RT_INERTIAL_DRIVER_REAL64)) != 0 &&
       ((flags & OBELISK_RT_INERTIAL_DRIVER_VECTOR_DELAY) == 0 ||
        riseDelay != fallDelay || riseDelay != turnoffDelay)))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    ContextTransaction transaction(context);
    ContextMutexLock lock(context);
    if (!validInertialStatePlanesUnlocked(context, valuePlane, unknownPlane,
                                          planeBitCount))
      return OBELISK_RT_INVALID_HANDLE;
    InertialDriverSite site{codeUnit, component};
    auto cancelPending = [&] {
      context->scheduledNBAs.erase(
          std::remove_if(context->scheduledNBAs.begin(),
                         context->scheduledNBAs.end(),
                         [&](const ScheduledNBA &update) {
                           return update.inertialSite == site;
                         }),
          context->scheduledNBAs.end());
      context->inertialDriverPending.erase(site);
    };
    auto preserveInitialProjection = [&] {
      context->scheduledNBAs.erase(
          std::remove_if(context->scheduledNBAs.begin(),
                         context->scheduledNBAs.end(),
                         [&](const ScheduledNBA &update) {
                           return update.inertialSite == site &&
                                  !update.inertialDriverInitialProjection;
                         }),
          context->scheduledNBAs.end());
      context->inertialDriverPending.erase(site);
    };
    // Dynamic driver selections use the invalid handle as a no-drive value.
    // Re-evaluation still rejects an older pulse from this assignment site.
    if (bitOffset == UINT64_MAX) {
      cancelPending();
      return OBELISK_RT_OK;
    }

    uint32_t staticID = 0;
    int64_t offset = 0;
    uint64_t rootHandle = bitOffset;
    bool boundedStatic = decodeNativeStatic(bitOffset, staticID, offset);
    if (!boundedStatic && !decodeNativeGlobal(bitOffset, offset))
      return OBELISK_RT_INVALID_HANDLE;
    const NativeStaticState *state =
        boundedStatic ? findNativeStaticState(context, staticID) : nullptr;
    if (boundedStatic && !state)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t availableWidth = boundedStatic ? state->bitWidth : planeBitCount;
    __int128 firstWide = std::max<__int128>(0, -static_cast<__int128>(offset));
    __int128 lastWide = std::min<__int128>(
        bitWidth, static_cast<__int128>(availableWidth) - offset);
    if (firstWide >= lastWide) {
      cancelPending();
      return OBELISK_RT_OK;
    }
    uint64_t sourceFirst = static_cast<uint64_t>(firstWide);
    uint64_t selectedWidth = static_cast<uint64_t>(lastWide - firstWide);
    bool realValue = (flags & (OBELISK_RT_INERTIAL_DRIVER_REAL32 |
                               OBELISK_RT_INERTIAL_DRIVER_REAL64)) != 0;
    // A real is one atomic user-defined-net value (6.6.7), never a clipped
    // packed slice. Continuous assignments to a UDNT also admit one delay
    // only (10.3.3), which the validation above preserves in this ABI.
    if (realValue && (sourceFirst != 0 || selectedWidth != bitWidth))
      return OBELISK_RT_INVALID_HANDLE;
    __int128 selectedOffsetWide = static_cast<__int128>(offset) + firstWide;
    if (selectedOffsetWide < 0 || selectedOffsetWide > INT64_MAX)
      return OBELISK_RT_INVALID_HANDLE;
    int64_t selectedOffset = static_cast<int64_t>(selectedOffsetWide);
    uint64_t selectedHandle =
        nativeHandleOffset(rootHandle, static_cast<int64_t>(sourceFirst));
    if (selectedHandle == UINT64_MAX)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t byteCount = (bitWidth - 1) / 8 + 1;
    if (!value || (unknownPlane && !unknown) ||
        byteCount > std::numeric_limits<size_t>::max())
      return OBELISK_RT_INVALID_ARGUMENT;

    uint64_t selectedBytes = (selectedWidth - 1) / 8 + 1;
    std::vector<uint8_t> targetValue(static_cast<size_t>(selectedBytes), 0);
    // Keep pending-target identity independent of whether a two-state caller
    // omitted its physically all-zero unknown plane.
    std::vector<uint8_t> targetUnknown(static_cast<size_t>(selectedBytes), 0);
    for (uint64_t bit = 0; bit != selectedWidth; ++bit) {
      if (byteBit(value, sourceFirst + bit))
        setByteBit(targetValue.data(), bit, true);
      if (unknownPlane && byteBit(unknown, sourceFirst + bit))
        setByteBit(targetUnknown.data(), bit, true);
    }
    bitOffset = selectedHandle;
    bitWidth = selectedWidth;
    offset = selectedOffset;
    auto realBitsAreNaN = [&](const std::vector<uint8_t> &bytes) {
      if (!realValue)
        return false;
      if ((flags & OBELISK_RT_INERTIAL_DRIVER_REAL32) != 0) {
        float value = 0.0f;
        std::memcpy(&value, bytes.data(), sizeof(value));
        return std::isnan(value);
      }
      double value = 0.0;
      std::memcpy(&value, bytes.data(), sizeof(value));
      return std::isnan(value);
    };
    if (auto pending = context->inertialDriverPending.find(site);
        pending != context->inertialDriverPending.end() &&
        pending->second.destination == bitOffset &&
        pending->second.width == bitWidth &&
        pending->second.value == targetValue &&
        pending->second.unknown == targetUnknown &&
        !realBitsAreNaN(targetValue))
      return OBELISK_RT_OK;

    auto sourceBit = [](const std::vector<uint8_t> &plane, uint64_t bit) {
      return bit / 8 < plane.size() && byteBit(plane.data(), bit);
    };
    auto currentBit = [&](bool unknownBit, uint64_t bit) {
      uint64_t absolute = (boundedStatic ? state->bitOffset : 0) +
                          static_cast<uint64_t>(offset) + bit;
      const std::vector<uint64_t> &plane =
          unknownBit ? context->stateUnknown : context->stateValue;
      if (absolute / 64 < plane.size())
        return ((plane[absolute / 64] >> (absolute % 64)) & 1) != 0;
      const uint8_t *nativePlane = unknownBit ? unknownPlane : valuePlane;
      return nativePlane && absolute < planeBitCount &&
             byteBit(nativePlane, absolute);
    };
    auto delayFor = [&](bool newValue, bool newUnknown) {
      if (!newUnknown)
        return newValue ? riseDelay : fallDelay;
      return newValue ? turnoffDelay
                      : std::min({riseDelay, fallDelay, turnoffDelay});
    };
    auto enqueue = [&](uint64_t first, uint64_t width, uint64_t delay,
                       bool initialProjection) {
      if (context->nextSchedulerSequence == 0 ||
          context->nextSchedulerSequence == UINT64_MAX)
        return false;
      ScheduledNBA update;
      update.valuePlane = valuePlane;
      update.unknownPlane = unknownPlane;
      update.planeBitCount = planeBitCount;
      update.bitOffset =
          boundedStatic
              ? obelisk::designbytecode::encodeStaticHandle(
                    staticID,
                    static_cast<int64_t>(static_cast<uint64_t>(offset) + first))
              : nativeHandleOffset(rootHandle, static_cast<int64_t>(first));
      update.bitWidth = width;
      update.driver = true;
      update.deferDriverResolution =
          (flags & OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION) != 0;
      update.publishDriverTransition =
          (flags & OBELISK_RT_INERTIAL_DRIVER_PUBLISH_RAW) != 0;
      update.realWidth = (flags & OBELISK_RT_INERTIAL_DRIVER_REAL32) != 0   ? 32
                         : (flags & OBELISK_RT_INERTIAL_DRIVER_REAL64) != 0 ? 64
                                                                            : 0;
      update.execRegion = OBELISK_RT_REGION_ACTIVE;
      update.sequence = context->nextSchedulerSequence++;
      update.dueTime = delay > UINT64_MAX - context->schedulerTime
                           ? UINT64_MAX
                           : context->schedulerTime + delay;
      update.inertialSite = site;
      update.inertialDriverVector =
          (flags & OBELISK_RT_INERTIAL_DRIVER_VECTOR_DELAY) != 0;
      update.inertialDriverInitialProjection = initialProjection;
      uint64_t bytes = (width - 1) / 8 + 1;
      update.value.assign(static_cast<size_t>(bytes), 0);
      if (unknownPlane)
        update.unknown.assign(static_cast<size_t>(bytes), 0);
      for (uint64_t bit = 0; bit != width; ++bit) {
        if (sourceBit(targetValue, first + bit))
          setByteBit(update.value.data(), bit, true);
        if (unknownPlane && sourceBit(targetUnknown, first + bit))
          setByteBit(update.unknown.data(), bit, true);
      }
      context->scheduledNBAs.push_back(std::move(update));
      return true;
    };

    uint64_t scheduled = 0;
    if ((flags & OBELISK_RT_INERTIAL_DRIVER_VECTOR_DELAY) != 0) {
      bool changed = false;
      bool oldHighZ = !realValue;
      bool newZero = true;
      bool newHighZ = true;
      if (realValue) {
        uint64_t oldBits = 0;
        uint64_t newBits = 0;
        for (uint64_t bit = 0; bit != bitWidth; ++bit) {
          oldBits |= static_cast<uint64_t>(currentBit(false, bit)) << bit;
          newBits |= static_cast<uint64_t>(sourceBit(targetValue, bit)) << bit;
        }
        if ((flags & OBELISK_RT_INERTIAL_DRIVER_REAL32) != 0) {
          uint32_t oldNarrow = static_cast<uint32_t>(oldBits);
          uint32_t newNarrow = static_cast<uint32_t>(newBits);
          float oldReal = 0.0f;
          float newReal = 0.0f;
          std::memcpy(&oldReal, &oldNarrow, sizeof(oldReal));
          std::memcpy(&newReal, &newNarrow, sizeof(newReal));
          changed = oldReal != newReal || std::isnan(newReal);
        } else {
          double oldReal = 0.0;
          double newReal = 0.0;
          std::memcpy(&oldReal, &oldBits, sizeof(oldReal));
          std::memcpy(&newReal, &newBits, sizeof(newReal));
          changed = oldReal != newReal || std::isnan(newReal);
        }
      } else {
        for (uint64_t bit = 0; bit != bitWidth; ++bit) {
          bool oldValue = currentBit(false, bit);
          bool oldUnknown = currentBit(true, bit);
          bool newValue = sourceBit(targetValue, bit);
          bool newUnknown = sourceBit(targetUnknown, bit);
          changed |= oldValue != newValue || oldUnknown != newUnknown;
          oldHighZ &= oldUnknown && oldValue;
          newZero &= !newUnknown && !newValue;
          newHighZ &= newUnknown && newValue;
        }
      }
      if (changed) {
        // The first projected value of a continuous assignment starts from
        // the net's implicit high-impedance state and must be allowed to
        // establish that initial value. Later input pulses reject an older
        // pending projection from the same assignment site.
        bool hasInitialProjection = std::any_of(
            context->scheduledNBAs.begin(), context->scheduledNBAs.end(),
            [&](const ScheduledNBA &update) {
              return update.inertialSite == site &&
                     update.inertialDriverInitialProjection;
            });
        if (oldHighZ)
          preserveInitialProjection();
        else
          cancelPending();
        if (context->nextSchedulerSequence == 0 ||
            context->nextSchedulerSequence == UINT64_MAX)
          return OBELISK_RT_OUT_OF_RESOURCES;
        uint64_t delay = realValue               ? riseDelay
                         : newHighZ              ? turnoffDelay
                         : newZero               ? fallDelay
                                                 : riseDelay;
        if (!enqueue(0, bitWidth, delay, oldHighZ && !hasInitialProjection))
          return OBELISK_RT_OUT_OF_RESOURCES;
        scheduled = 1;
      } else
        cancelPending();
    } else {
      cancelPending();
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        bool newValue = sourceBit(targetValue, bit);
        bool newUnknown = sourceBit(targetUnknown, bit);
        scheduled += currentBit(false, bit) != newValue ||
                     currentBit(true, bit) != newUnknown;
      }
      if (scheduled != 0 && (context->nextSchedulerSequence == 0 ||
                             static_cast<__uint128_t>(scheduled) >
                                 static_cast<__uint128_t>(UINT64_MAX) -
                                     context->nextSchedulerSequence))
        return OBELISK_RT_OUT_OF_RESOURCES;
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        bool newValue = sourceBit(targetValue, bit);
        bool newUnknown = sourceBit(targetUnknown, bit);
        if (currentBit(false, bit) == newValue &&
            currentBit(true, bit) == newUnknown)
          continue;
        if (!enqueue(bit, 1, delayFor(newValue, newUnknown), false))
          return OBELISK_RT_OUT_OF_RESOURCES;
      }
    }
    if (scheduled != 0)
      context->inertialDriverPending.emplace(
          site,
          InertialDriverPending{bitOffset, bitWidth, std::move(targetValue),
                                std::move(targetUnknown), scheduled});
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

template <bool Storage>
static obelisk_rt_status schedulerInertialPath(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t codeUnit, uint32_t component, uint32_t group, uint32_t groupCount,
    uint32_t flags, uint64_t riseDelay, uint64_t fallDelay,
    uint64_t turnoffDelay, uint64_t pulseReject, uint64_t pulseError,
    const uint8_t *value, const uint8_t *unknown, const uint8_t *writeMask,
    const uint8_t *activeMask, const uint8_t *riseMask, const uint8_t *fallMask,
    const uint8_t *turnoffMask, const uint8_t *pulseTransitionMasks,
    bool nonblocking) {
  if (!context || !context->execution || !valuePlane || bitWidth == 0 ||
      codeUnit == UINT64_MAX || groupCount == 0 || group >= groupCount ||
      (flags & ~(OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION |
                 OBELISK_RT_INERTIAL_DRIVER_PUBLISH_RAW |
                 OBELISK_RT_INERTIAL_PATH_ON_DETECT |
                 OBELISK_RT_INERTIAL_PATH_SHOW_CANCELLED |
                 OBELISK_RT_INERTIAL_PATH_EXACT_TRANSITIONS)) != 0 ||
      !value || (unknownPlane && !unknown) || !activeMask || !riseMask ||
      !fallMask || !turnoffMask ||
      (((flags & OBELISK_RT_INERTIAL_PATH_EXACT_TRANSITIONS) != 0) !=
       (pulseTransitionMasks != nullptr)) ||
      (((pulseReject != UINT64_MAX || pulseError != UINT64_MAX ||
         (flags & (OBELISK_RT_INERTIAL_PATH_ON_DETECT |
                   OBELISK_RT_INERTIAL_PATH_SHOW_CANCELLED)) != 0)) &&
       pulseTransitionMasks == nullptr) ||
      (Storage && !writeMask))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    ContextTransaction transaction(context);
    ContextMutexLock lock(context);
    if (!validInertialStatePlanesUnlocked(context, valuePlane, unknownPlane,
                                          planeBitCount))
      return OBELISK_RT_INVALID_HANDLE;

    uint32_t staticID = 0;
    int64_t offset = 0;
    uint64_t rootHandle = bitOffset;
    bool boundedStatic = decodeNativeStatic(bitOffset, staticID, offset);
    if (!boundedStatic && !decodeNativeGlobal(bitOffset, offset))
      return OBELISK_RT_INVALID_HANDLE;
    const NativeStaticState *nativeState =
        boundedStatic ? findNativeStaticState(context, staticID) : nullptr;
    if (boundedStatic && !nativeState)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t availableWidth =
        boundedStatic ? nativeState->bitWidth : planeBitCount;
    if (offset < 0 || static_cast<uint64_t>(offset) > availableWidth ||
        bitWidth > availableWidth - static_cast<uint64_t>(offset))
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t selectedHandle =
        boundedStatic
            ? obelisk::designbytecode::encodeStaticHandle(staticID, offset)
            : rootHandle;
    if (selectedHandle == UINT64_MAX)
      return OBELISK_RT_INVALID_HANDLE;

    InertialDriverSite site{codeUnit, component, Storage};
    InertialPathPending &pending = context->inertialPathPending[site];
    bool pulseControlled = pulseTransitionMasks != nullptr;
    size_t bytes = static_cast<size_t>((bitWidth - 1) / 8 + 1);
    auto cancelScheduled = [&](uint64_t bit) {
      size_t index = static_cast<size_t>(bit);
      if (index >= pending.scheduledSequence.size() ||
          pending.scheduledSequence[index] == 0)
        return;
      uint64_t sequence = pending.scheduledSequence[index];
      context->scheduledInertialPathNBAs.erase(
          {pending.scheduledDueTime[index], sequence});
      if (pending.pulseControlled) {
        auto &live = pending.liveSequences[index];
        live.erase(std::remove(live.begin(), live.end(), sequence), live.end());
      }
      pending.scheduledDueTime[index] = 0;
      pending.scheduledSequence[index] = 0;
    };
    auto resetState = [&] {
      for (uint64_t bit = 0; bit != pending.width; ++bit)
        cancelScheduled(bit);
      pending = InertialPathPending{};
      pending.destination = selectedHandle;
      pending.width = bitWidth;
      pending.pulseControlled = pulseControlled;
      pending.generation.assign(static_cast<size_t>(bitWidth), 1);
      pending.targetValue.assign(bytes, 0);
      pending.targetUnknown.assign(bytes, 0);
      pending.valid.assign(static_cast<size_t>(bitWidth), 0);
      pending.delayed.assign(static_cast<size_t>(bitWidth), 0);
      pending.needsSchedule.assign(static_cast<size_t>(bitWidth), 0);
      pending.candidateDelay.assign(static_cast<size_t>(bitWidth), UINT64_MAX);
      if (pulseControlled) {
        pending.candidatePulseReject.assign(static_cast<size_t>(bitWidth),
                                            UINT64_MAX);
        pending.candidatePulseError.assign(static_cast<size_t>(bitWidth),
                                           UINT64_MAX);
        pending.candidatePulseFlags.assign(static_cast<size_t>(bitWidth), 0);
        pending.candidateFromSymbol.assign(static_cast<size_t>(bitWidth), 0);
      }
      pending.scheduledDueTime.assign(static_cast<size_t>(bitWidth), 0);
      pending.scheduledSequence.assign(static_cast<size_t>(bitWidth), 0);
      if (pulseControlled)
        pending.liveSequences.resize(static_cast<size_t>(bitWidth));
    };
    if (group == 0) {
      if (pending.destination != selectedHandle || pending.width != bitWidth ||
          pending.pulseControlled != pulseControlled)
        resetState();
      pending.nextGroup = 0;
      pending.groupCount = groupCount;
    } else if (pending.destination != selectedHandle ||
               pending.width != bitWidth || pending.groupCount != groupCount ||
               pending.nextGroup != group ||
               pending.pulseControlled != pulseControlled)
      return OBELISK_RT_INVALID_ARGUMENT;

    auto currentBit = [&](bool unknownBit, uint64_t bit) {
      uint64_t absolute = (boundedStatic ? nativeState->bitOffset : 0) +
                          static_cast<uint64_t>(offset) + bit;
      const std::vector<uint64_t> &plane =
          unknownBit ? context->stateUnknown : context->stateValue;
      if (absolute / 64 < plane.size())
        return ((plane[absolute / 64] >> (absolute % 64)) & 1) != 0;
      const uint8_t *nativePlane = unknownBit ? unknownPlane : valuePlane;
      return nativePlane && absolute < planeBitCount &&
             byteBit(nativePlane, absolute);
    };
    auto commitImmediateStorage = [&](uint64_t bit, bool nextValue,
                                      bool nextUnknown) {
      uint64_t handle =
          nativeHandleOffset(selectedHandle, static_cast<int64_t>(bit));
      if (handle == UINT64_MAX)
        return OBELISK_RT_INVALID_HANDLE;
      uint8_t oldValue = currentBit(false, bit) ? 1 : 0;
      uint8_t oldUnknown = currentBit(true, bit) ? 1 : 0;
      uint8_t storedValue = nextValue ? 1 : 0;
      uint8_t storedUnknown = nextUnknown ? 1 : 0;
      uint8_t changed = 0;
      obelisk_rt_status status = obelisk_rt_v1_native_state_store_plane(
          context, valuePlane, planeBitCount, handle, 1, 0, &storedValue,
          &changed);
      if (status != OBELISK_RT_OK)
        return status;
      if (unknownPlane) {
        status = obelisk_rt_v1_native_state_store_plane(
            context, unknownPlane, planeBitCount, handle, 1, 1, &storedUnknown,
            &changed);
        if (status != OBELISK_RT_OK)
          return status;
      }
      uint8_t finalValue = currentBit(false, bit) ? 1 : 0;
      uint8_t finalUnknown = currentBit(true, bit) ? 1 : 0;
      if (oldValue != finalValue || oldUnknown != finalUnknown)
        obelisk_rt_v1_scheduler_signal_transition(context, handle, 1, &oldValue,
                                                  &oldUnknown, &finalValue,
                                                  &finalUnknown);
      return context->schedulerStatus;
    };
    auto enqueueAt = [&](uint64_t bit, uint64_t dueTime, bool targetValue,
                         bool targetUnknown) {
      if constexpr (Storage)
        if (!nonblocking && dueTime == context->schedulerTime)
          return commitImmediateStorage(bit, targetValue, targetUnknown) ==
                 OBELISK_RT_OK;
      if (context->nextSchedulerSequence == 0 ||
          context->nextSchedulerSequence == UINT64_MAX)
        return false;
      ScheduledNBA update;
      update.valuePlane = valuePlane;
      update.unknownPlane = unknownPlane;
      update.planeBitCount = planeBitCount;
      update.bitOffset =
          nativeHandleOffset(selectedHandle, static_cast<int64_t>(bit));
      if (update.bitOffset == UINT64_MAX)
        return false;
      update.bitWidth = 1;
      update.driver = !Storage;
      update.deferDriverResolution =
          (flags & OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION) != 0;
      update.publishDriverTransition =
          (flags & OBELISK_RT_INERTIAL_DRIVER_PUBLISH_RAW) != 0;
      update.execRegion = Storage && nonblocking ? OBELISK_RT_REGION_NBA
                                                 : OBELISK_RT_REGION_ACTIVE;
      update.sequence = context->nextSchedulerSequence++;
      update.dueTime = dueTime;
      update.inertialSite = site;
      update.inertialPathDriver = true;
      update.inertialPathBit = bit;
      update.inertialPathGeneration =
          pending.generation[static_cast<size_t>(bit)];
      update.inlinePacked = true;
      update.inlineValue = targetValue ? 1 : 0;
      update.inlineUnknown = targetUnknown ? 1 : 0;
      auto key = std::make_pair(update.dueTime, update.sequence);
      if (!context->scheduledInertialPathNBAs.emplace(key, std::move(update))
               .second)
        return false;
      pending.scheduledDueTime[static_cast<size_t>(bit)] = key.first;
      pending.scheduledSequence[static_cast<size_t>(bit)] = key.second;
      if (pending.pulseControlled)
        pending.liveSequences[static_cast<size_t>(bit)].push_back(key.second);
      return true;
    };
    auto enqueue = [&](uint64_t bit, uint64_t delay) {
      uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                             ? UINT64_MAX
                             : context->schedulerTime + delay;
      return enqueueAt(bit, dueTime, byteBit(value, bit),
                       unknownPlane && byteBit(unknown, bit));
    };

    if (group == 0) {
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        if (writeMask && !byteBit(writeMask, bit))
          continue;
        bool targetValue = byteBit(value, bit);
        bool targetUnknown = unknownPlane && byteBit(unknown, bit);
        bool active = byteBit(activeMask, bit);
        bool sameTarget =
            pending.valid[static_cast<size_t>(bit)] &&
            byteBit(pending.targetValue.data(), bit) == targetValue &&
            byteBit(pending.targetUnknown.data(), bit) == targetUnknown;
        // An unrelated path activation still presents the whole driver value.
        // Preserve a delayed bit whose target did not change instead of
        // treating its absence from this activation's mask as an immediate
        // update. Conversely, an applicable delayed path supersedes a pending
        // zero-delay outside-path update at the same scheduler time.
        bool pendingSame =
            sameTarget &&
            (Storage ? active && pending.delayed[static_cast<size_t>(bit)]
                     : pending.delayed[static_cast<size_t>(bit)] || !active);
        pending.needsSchedule[static_cast<size_t>(bit)] = 0;
        pending.candidateDelay[static_cast<size_t>(bit)] = UINT64_MAX;
        if (pending.pulseControlled) {
          pending.candidatePulseReject[static_cast<size_t>(bit)] = UINT64_MAX;
          pending.candidatePulseError[static_cast<size_t>(bit)] = UINT64_MAX;
          pending.candidatePulseFlags[static_cast<size_t>(bit)] = 0;
        }
        if (pendingSame)
          continue;
        if (pending.pulseControlled) {
          bool previousValue =
              pending.scheduledSequence[static_cast<size_t>(bit)] != 0
                  ? byteBit(pending.targetValue.data(), bit)
                  : currentBit(false, bit);
          bool previousUnknown =
              pending.scheduledSequence[static_cast<size_t>(bit)] != 0
                  ? byteBit(pending.targetUnknown.data(), bit)
                  : currentBit(true, bit);
          pending.candidateFromSymbol[static_cast<size_t>(bit)] =
              previousUnknown ? (previousValue ? 3 : 2)
                              : (previousValue ? 1 : 0);
        }
        setByteBit(pending.targetValue.data(), bit, targetValue);
        setByteBit(pending.targetUnknown.data(), bit, targetUnknown);
        bool changed = currentBit(false, bit) != targetValue ||
                       currentBit(true, bit) != targetUnknown ||
                       pending.scheduledSequence[static_cast<size_t>(bit)] != 0;
        pending.valid[static_cast<size_t>(bit)] = 1;
        pending.delayed[static_cast<size_t>(bit)] = active ? 1 : 0;
        if (active) {
          pending.needsSchedule[static_cast<size_t>(bit)] = changed ? 1 : 0;
        } else {
          cancelScheduled(bit);
          if (changed && !enqueue(bit, 0))
            return OBELISK_RT_OUT_OF_RESOURCES;
          if (Storage && !nonblocking) {
            pending.valid[static_cast<size_t>(bit)] = 0;
            pending.delayed[static_cast<size_t>(bit)] = 0;
          }
        }
      }
    }

    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      if (!pending.needsSchedule[static_cast<size_t>(bit)])
        continue;
      bool targetValue = byteBit(value, bit);
      bool targetUnknown = unknownPlane && byteBit(unknown, bit);
      uint64_t candidate = UINT64_MAX;
      if ((flags & OBELISK_RT_INERTIAL_PATH_EXACT_TRANSITIONS) != 0) {
        // IEEE 1800-2017 30.5.1 assigns distinct delays (and therefore 30.7
        // pulse limits) to all twelve four-state transitions.  While a
        // leading edge is pending, its target is the old symbol; consulting
        // the published destination here would collapse a real trailing edge
        // into a non-transition and select the wrong pulse policy.
        uint8_t from = pending.candidateFromSymbol[static_cast<size_t>(bit)];
        uint8_t to =
            targetUnknown ? (targetValue ? 3 : 2) : (targetValue ? 1 : 0);
        constexpr uint8_t noTransition = UINT8_MAX;
        constexpr uint8_t transition[4][4] = {{noTransition, 0, 6, 2},
                                              {1, noTransition, 8, 4},
                                              {9, 7, noTransition, 10},
                                              {5, 3, 11, noTransition}};
        uint8_t index = transition[from][to];
        if (index != noTransition &&
            byteBit(pulseTransitionMasks,
                    static_cast<uint64_t>(index) * bitWidth + bit))
          candidate = riseDelay;
      } else if (!targetUnknown && targetValue && byteBit(riseMask, bit))
        candidate = riseDelay;
      else if (!targetUnknown && !targetValue && byteBit(fallMask, bit))
        candidate = fallDelay;
      else if (targetUnknown && targetValue && byteBit(turnoffMask, bit))
        candidate = turnoffDelay;
      else if (targetUnknown) {
        if (byteBit(riseMask, bit))
          candidate = std::min(candidate, riseDelay);
        if (byteBit(fallMask, bit))
          candidate = std::min(candidate, fallDelay);
        if (byteBit(turnoffMask, bit))
          candidate = std::min(candidate, turnoffDelay);
      }
      size_t index = static_cast<size_t>(bit);
      if (candidate < pending.candidateDelay[index]) {
        pending.candidateDelay[index] = candidate;
        if (pending.pulseControlled) {
          pending.candidatePulseReject[index] =
              pulseReject == UINT64_MAX ? candidate : pulseReject;
          pending.candidatePulseError[index] =
              pulseError == UINT64_MAX ? candidate : pulseError;
          pending.candidatePulseFlags[index] = static_cast<uint8_t>(
              flags & (OBELISK_RT_INERTIAL_PATH_ON_DETECT |
                       OBELISK_RT_INERTIAL_PATH_SHOW_CANCELLED));
        }
      }
    }
    pending.nextGroup = group + 1;
    if (group + 1 == groupCount) {
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        size_t index = static_cast<size_t>(bit);
        if (!pending.needsSchedule[index])
          continue;
        uint64_t delay = pending.candidateDelay[index];
        if (!pending.pulseControlled) {
          if (delay == UINT64_MAX)
            return OBELISK_RT_INVALID_ARGUMENT;
          cancelScheduled(bit);
          if (!enqueue(bit, delay))
            return OBELISK_RT_OUT_OF_RESOURCES;
          if (Storage && !nonblocking && delay == 0) {
            pending.valid[index] = 0;
            pending.delayed[index] = 0;
          }
          pending.needsSchedule[index] = 0;
          continue;
        }
        uint64_t reject = pending.candidatePulseReject[index];
        uint64_t error = pending.candidatePulseError[index];
        uint8_t pulseFlags = pending.candidatePulseFlags[index];
        if (delay == UINT64_MAX || reject == UINT64_MAX ||
            error == UINT64_MAX || error < reject)
          return OBELISK_RT_INVALID_ARGUMENT;
        uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                               ? UINT64_MAX
                               : context->schedulerTime + delay;
        bool targetValue = byteBit(value, bit);
        bool targetUnknown = unknownPlane && byteBit(unknown, bit);
        uint64_t leadingSequence = pending.scheduledSequence[index];
        uint64_t leadingDue = pending.scheduledDueTime[index];
        auto scheduleFinalIfNeeded = [&](uint64_t finalDue) {
          bool differs = currentBit(false, bit) != targetValue ||
                         currentBit(true, bit) != targetUnknown;
          return !differs ||
                 enqueueAt(bit, finalDue, targetValue, targetUnknown);
        };
        if (leadingSequence == 0) {
          if (!enqueueAt(bit, dueTime, targetValue, targetUnknown))
            return OBELISK_RT_OUT_OF_RESOURCES;
        } else {
          bool negative = dueTime < leadingDue;
          uint64_t width =
              negative ? leadingDue - dueTime : dueTime - leadingDue;
          bool showCancelled =
              (pulseFlags & OBELISK_RT_INERTIAL_PATH_SHOW_CANCELLED) != 0;
          bool onDetect =
              (pulseFlags & OBELISK_RT_INERTIAL_PATH_ON_DETECT) != 0;

          // IEEE 1800-2017 30.7: the trailing edge's limits classify the
          // pulse. Width >= error passes; reject <= width < error produces X;
          // width < reject disappears. 30.7.4.1 moves only the X-leading
          // event to detection time, while 30.7.4.2 applies the same X policy
          // to a negative pulse only when showcancelled is enabled.
          bool passPulse = !negative && width >= error;
          bool xPulse = (!negative && width >= reject && width < error) ||
                        (negative && showCancelled);
          if (passPulse) {
            if (!enqueueAt(bit, dueTime, targetValue, targetUnknown))
              return OBELISK_RT_OUT_OF_RESOURCES;
          } else if (xPulse) {
            cancelScheduled(bit);
            uint64_t xDue = onDetect ? context->schedulerTime
                                     : std::min(leadingDue, dueTime);
            uint64_t finalDue =
                negative ? std::max(leadingDue, dueTime) : dueTime;
            if (!enqueueAt(bit, xDue, false, true) ||
                !enqueueAt(bit, finalDue, targetValue, targetUnknown))
              return OBELISK_RT_OUT_OF_RESOURCES;
          } else {
            cancelScheduled(bit);
            if (!scheduleFinalIfNeeded(dueTime))
              return OBELISK_RT_OUT_OF_RESOURCES;
          }
        }
        if (Storage && !nonblocking && delay == 0) {
          pending.valid[index] = 0;
          pending.delayed[index] = 0;
        }
        pending.needsSchedule[index] = 0;
      }
      pending.nextGroup = 0;
    }
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

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_inertial_path_driver_pulse(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t codeUnit, uint32_t component, uint32_t group, uint32_t groupCount,
    uint32_t flags, uint64_t riseDelay, uint64_t fallDelay,
    uint64_t turnoffDelay, uint64_t pulseReject, uint64_t pulseError,
    const uint8_t *value, const uint8_t *unknown, const uint8_t *activeMask,
    const uint8_t *riseMask, const uint8_t *fallMask,
    const uint8_t *turnoffMask, const uint8_t *pulseTransitionMasks) {
  return schedulerInertialPath<false>(
      context, valuePlane, unknownPlane, planeBitCount, bitOffset, bitWidth,
      codeUnit, component, group, groupCount, flags, riseDelay, fallDelay,
      turnoffDelay, pulseReject, pulseError, value, unknown, nullptr,
      activeMask, riseMask, fallMask, turnoffMask, pulseTransitionMasks, false);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_inertial_path_driver(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t codeUnit, uint32_t component, uint32_t group, uint32_t groupCount,
    uint32_t flags, uint64_t riseDelay, uint64_t fallDelay,
    uint64_t turnoffDelay, const uint8_t *value, const uint8_t *unknown,
    const uint8_t *activeMask, const uint8_t *riseMask, const uint8_t *fallMask,
    const uint8_t *turnoffMask) {
  return obelisk_rt_v1_scheduler_inertial_path_driver_pulse(
      context, valuePlane, unknownPlane, planeBitCount, bitOffset, bitWidth,
      codeUnit, component, group, groupCount, flags, riseDelay, fallDelay,
      turnoffDelay, UINT64_MAX, UINT64_MAX, value, unknown, activeMask,
      riseMask, fallMask, turnoffMask, nullptr);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_inertial_path_storage_pulse(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t siteID, uint32_t component, uint32_t group, uint32_t groupCount,
    uint32_t nonblocking, uint32_t pulseFlags, uint64_t riseDelay,
    uint64_t fallDelay, uint64_t turnoffDelay, uint64_t pulseReject,
    uint64_t pulseError, const uint8_t *value, const uint8_t *unknown,
    const uint8_t *writeMask, const uint8_t *activeMask,
    const uint8_t *riseMask, const uint8_t *fallMask,
    const uint8_t *turnoffMask, const uint8_t *pulseTransitionMasks) {
  if (nonblocking > 1 ||
      (pulseFlags & ~(OBELISK_RT_INERTIAL_PATH_ON_DETECT |
                      OBELISK_RT_INERTIAL_PATH_SHOW_CANCELLED |
                      OBELISK_RT_INERTIAL_PATH_EXACT_TRANSITIONS)) != 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  return schedulerInertialPath<true>(
      context, valuePlane, unknownPlane, planeBitCount, bitOffset, bitWidth,
      siteID, component, group, groupCount, pulseFlags, riseDelay, fallDelay,
      turnoffDelay, pulseReject, pulseError, value, unknown, writeMask,
      activeMask, riseMask, fallMask, turnoffMask, pulseTransitionMasks,
      nonblocking != 0);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_inertial_path_storage(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t siteID, uint32_t component, uint32_t group, uint32_t groupCount,
    uint32_t nonblocking, uint64_t riseDelay, uint64_t fallDelay,
    uint64_t turnoffDelay, const uint8_t *value, const uint8_t *unknown,
    const uint8_t *writeMask, const uint8_t *activeMask,
    const uint8_t *riseMask, const uint8_t *fallMask,
    const uint8_t *turnoffMask) {
  return obelisk_rt_v1_scheduler_inertial_path_storage_pulse(
      context, valuePlane, unknownPlane, planeBitCount, bitOffset, bitWidth,
      siteID, component, group, groupCount, nonblocking, 0, riseDelay,
      fallDelay, turnoffDelay, UINT64_MAX, UINT64_MAX, value, unknown,
      writeMask, activeMask, riseMask, fallMask, turnoffMask, nullptr);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_inertial_driver_strength_pair(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t lowBitOffset, uint64_t highBitOffset,
    uint64_t bitWidth, uint64_t codeUnit, uint32_t component,
    uint64_t riseDelay, uint64_t fallDelay, uint64_t turnoffDelay,
    const uint8_t *lowValue, const uint8_t *lowUnknown,
    const uint8_t *highValue, const uint8_t *highUnknown,
    const uint8_t *transitionValue, const uint8_t *transitionUnknown) {
  if (!context || !context->execution || !valuePlane || !unknownPlane ||
      bitWidth == 0 || codeUnit == UINT64_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    ContextTransaction transaction(context);
    ContextMutexLock lock(context);
    if (!validInertialStatePlanesUnlocked(context, valuePlane, unknownPlane,
                                          planeBitCount))
      return OBELISK_RT_INVALID_HANDLE;
    InertialDriverSite site{codeUnit, component};
    auto cancelPending = [&] {
      context->scheduledNBAs.erase(
          std::remove_if(context->scheduledNBAs.begin(),
                         context->scheduledNBAs.end(),
                         [&](const ScheduledNBA &update) {
                           return update.inertialSite == site;
                         }),
          context->scheduledNBAs.end());
      context->inertialDriverPending.erase(site);
    };
    if (lowBitOffset == UINT64_MAX || highBitOffset == UINT64_MAX) {
      cancelPending();
      return lowBitOffset == highBitOffset ? OBELISK_RT_OK
                                           : OBELISK_RT_INVALID_HANDLE;
    }
    uint64_t byteCount = (bitWidth - 1) / 8 + 1;
    if (!lowValue || !lowUnknown || !highValue || !highUnknown ||
        !transitionValue || !transitionUnknown ||
        byteCount > std::numeric_limits<size_t>::max())
      return OBELISK_RT_INVALID_ARGUMENT;

    struct Selection {
      uint32_t staticID = 0;
      int64_t offset = 0;
      uint64_t first = 0;
      uint64_t width = 0;
      uint64_t handle = UINT64_MAX;
      const NativeStaticState *state = nullptr;
    };
    auto select = [&](uint64_t handle) -> std::optional<Selection> {
      Selection selected;
      if (!decodeNativeStatic(handle, selected.staticID, selected.offset))
        return std::nullopt;
      selected.state = findNativeStaticState(context, selected.staticID);
      if (!selected.state)
        return std::nullopt;
      __int128 firstWide =
          std::max<__int128>(0, -static_cast<__int128>(selected.offset));
      __int128 lastWide = std::min<__int128>(
          bitWidth,
          static_cast<__int128>(selected.state->bitWidth) - selected.offset);
      if (firstWide >= lastWide) {
        selected.first = bitWidth;
        return selected;
      }
      selected.first = static_cast<uint64_t>(firstWide);
      selected.width = static_cast<uint64_t>(lastWide - firstWide);
      __int128 offsetWide = static_cast<__int128>(selected.offset) + firstWide;
      if (offsetWide < 0 || offsetWide > INT64_MAX)
        return std::nullopt;
      selected.offset = static_cast<int64_t>(offsetWide);
      selected.handle = obelisk::designbytecode::encodeStaticHandle(
          selected.staticID, selected.offset);
      if (selected.handle == UINT64_MAX)
        return std::nullopt;
      return selected;
    };
    std::optional<Selection> low = select(lowBitOffset);
    std::optional<Selection> high = select(highBitOffset);
    if (!low || !high)
      return OBELISK_RT_INVALID_HANDLE;
    if (low->width == 0 || high->width == 0) {
      cancelPending();
      return low->width == high->width && low->first == high->first
                 ? OBELISK_RT_OK
                 : OBELISK_RT_INVALID_HANDLE;
    }
    if (low->first != high->first || low->width != high->width)
      return OBELISK_RT_INVALID_HANDLE;
    if (static_cast<__int128>(low->offset) + low->width - 1 > INT64_MAX ||
        static_cast<__int128>(high->offset) + high->width - 1 > INT64_MAX)
      return OBELISK_RT_INVALID_HANDLE;
    if (!validInertialStrengthPairUnlocked(context, *low->state, low->offset,
                                           *high->state, high->offset,
                                           low->width))
      return OBELISK_RT_INVALID_HANDLE;

    uint64_t selectedBytes = (low->width - 1) / 8 + 1;
    auto copySelected = [&](const uint8_t *source) {
      std::vector<uint8_t> result(static_cast<size_t>(selectedBytes), 0);
      for (uint64_t bit = 0; bit != low->width; ++bit)
        if (byteBit(source, low->first + bit))
          setByteBit(result.data(), bit, true);
      return result;
    };
    std::vector<uint8_t> targetLowValue = copySelected(lowValue);
    std::vector<uint8_t> targetLowUnknown = copySelected(lowUnknown);
    std::vector<uint8_t> targetHighValue = copySelected(highValue);
    std::vector<uint8_t> targetHighUnknown = copySelected(highUnknown);
    std::vector<uint8_t> targetTransitionValue = copySelected(transitionValue);
    std::vector<uint8_t> targetTransitionUnknown =
        copySelected(transitionUnknown);
    if (auto pending = context->inertialDriverPending.find(site);
        pending != context->inertialDriverPending.end() &&
        pending->second.destination == low->handle &&
        pending->second.secondDestination == high->handle &&
        pending->second.width == low->width &&
        pending->second.value == targetLowValue &&
        pending->second.unknown == targetLowUnknown &&
        pending->second.secondValue == targetHighValue &&
        pending->second.secondUnknown == targetHighUnknown)
      return OBELISK_RT_OK;

    cancelPending();
    auto currentBit = [&](const Selection &selection, bool unknown,
                          uint64_t bit) {
      uint64_t absolute = selection.state->bitOffset +
                          static_cast<uint64_t>(selection.offset) + bit;
      const std::vector<uint64_t> &plane =
          unknown ? context->stateUnknown : context->stateValue;
      return absolute / 64 < plane.size() &&
             ((plane[absolute / 64] >> (absolute % 64)) & 1) != 0;
    };
    auto targetBit = [](const std::vector<uint8_t> &plane, uint64_t bit) {
      return byteBit(plane.data(), bit);
    };
    uint64_t changedBits = 0;
    for (uint64_t bit = 0; bit != low->width; ++bit) {
      bool lowChanged =
          currentBit(*low, false, bit) != targetBit(targetLowValue, bit) ||
          currentBit(*low, true, bit) != targetBit(targetLowUnknown, bit);
      bool highChanged =
          currentBit(*high, false, bit) != targetBit(targetHighValue, bit) ||
          currentBit(*high, true, bit) != targetBit(targetHighUnknown, bit);
      changedBits += lowChanged || highChanged;
    }
    if (changedBits == 0)
      return OBELISK_RT_OK;
    __uint128_t updateCount = static_cast<__uint128_t>(changedBits) * 2;
    if (context->nextSchedulerSequence == 0 ||
        updateCount > static_cast<__uint128_t>(UINT64_MAX) -
                          context->nextSchedulerSequence)
      return OBELISK_RT_OUT_OF_RESOURCES;

    auto delayFor = [&](uint64_t bit) {
      bool value = targetBit(targetTransitionValue, bit);
      bool unknown = targetBit(targetTransitionUnknown, bit);
      if (!unknown)
        return value ? riseDelay : fallDelay;
      return value ? turnoffDelay
                   : std::min({riseDelay, fallDelay, turnoffDelay});
    };
    auto enqueue = [&](const Selection &selection, uint64_t bit, bool value,
                       bool unknown, uint64_t delay, bool final) {
      ScheduledNBA update;
      update.valuePlane = valuePlane;
      update.unknownPlane = unknownPlane;
      update.planeBitCount = planeBitCount;
      update.bitOffset = obelisk::designbytecode::encodeStaticHandle(
          selection.staticID, selection.offset + static_cast<int64_t>(bit));
      update.bitWidth = 1;
      update.driver = true;
      update.deferDriverResolution = !final;
      update.forceDriverResolution = final;
      update.execRegion = OBELISK_RT_REGION_ACTIVE;
      update.sequence = context->nextSchedulerSequence++;
      update.dueTime = delay > UINT64_MAX - context->schedulerTime
                           ? UINT64_MAX
                           : context->schedulerTime + delay;
      update.inertialSite = site;
      update.inlinePacked = true;
      update.inlineValue = value;
      update.inlineUnknown = unknown;
      context->scheduledNBAs.push_back(std::move(update));
    };
    for (uint64_t bit = 0; bit != low->width; ++bit) {
      bool lowChanged =
          currentBit(*low, false, bit) != targetBit(targetLowValue, bit) ||
          currentBit(*low, true, bit) != targetBit(targetLowUnknown, bit);
      bool highChanged =
          currentBit(*high, false, bit) != targetBit(targetHighValue, bit) ||
          currentBit(*high, true, bit) != targetBit(targetHighUnknown, bit);
      if (!lowChanged && !highChanged)
        continue;
      uint64_t delay = delayFor(bit);
      enqueue(*low, bit, targetBit(targetLowValue, bit),
              targetBit(targetLowUnknown, bit), delay, false);
      enqueue(*high, bit, targetBit(targetHighValue, bit),
              targetBit(targetHighUnknown, bit), delay, true);
    }
    InertialDriverPending pending;
    pending.destination = low->handle;
    pending.width = low->width;
    pending.value = std::move(targetLowValue);
    pending.unknown = std::move(targetLowUnknown);
    pending.remaining = static_cast<uint64_t>(updateCount);
    pending.secondDestination = high->handle;
    pending.secondValue = std::move(targetHighValue);
    pending.secondUnknown = std::move(targetHighUnknown);
    context->inertialDriverPending.emplace(site, std::move(pending));
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

template <bool PulseControlled>
static obelisk_rt_status schedulerInertialPathStrengthPair(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t lowBitOffset, uint64_t highBitOffset,
    uint64_t bitWidth, uint64_t codeUnit, uint32_t component, uint32_t group,
    uint32_t groupCount, uint32_t pulseFlags, uint64_t riseDelay,
    uint64_t fallDelay, uint64_t turnoffDelay, uint64_t pulseReject,
    uint64_t pulseError, const uint8_t *lowValue, const uint8_t *lowUnknown,
    const uint8_t *highValue, const uint8_t *highUnknown,
    const uint8_t *transitionValue, const uint8_t *transitionUnknown,
    const uint8_t *activeMask, const uint8_t *riseMask, const uint8_t *fallMask,
    const uint8_t *turnoffMask, const uint8_t *pulseTransitionMasks) {
  if (!context || !context->execution || !valuePlane || !unknownPlane ||
      bitWidth == 0 || codeUnit == UINT64_MAX || groupCount == 0 ||
      group >= groupCount || !lowValue || !lowUnknown || !highValue ||
      !highUnknown || !transitionValue || !transitionUnknown || !activeMask ||
      !riseMask || !fallMask || !turnoffMask)
    return OBELISK_RT_INVALID_ARGUMENT;
  if constexpr (PulseControlled) {
    if ((pulseFlags & ~(OBELISK_RT_INERTIAL_PATH_ON_DETECT |
                        OBELISK_RT_INERTIAL_PATH_SHOW_CANCELLED |
                        OBELISK_RT_INERTIAL_PATH_EXACT_TRANSITIONS)) != 0 ||
        (pulseFlags & OBELISK_RT_INERTIAL_PATH_EXACT_TRANSITIONS) == 0 ||
        !pulseTransitionMasks)
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  OBELISK_RT_TRY {
    ContextTransaction transaction(context);
    ContextMutexLock lock(context);
    if (!validInertialStatePlanesUnlocked(context, valuePlane, unknownPlane,
                                          planeBitCount))
      return OBELISK_RT_INVALID_HANDLE;

    struct Selection {
      uint32_t staticID = 0;
      int64_t offset = 0;
      uint64_t first = 0;
      uint64_t width = 0;
      uint64_t handle = UINT64_MAX;
      const NativeStaticState *state = nullptr;
    };
    auto select = [&](uint64_t handle) -> std::optional<Selection> {
      Selection selected;
      if (!decodeNativeStatic(handle, selected.staticID, selected.offset))
        return std::nullopt;
      selected.state = findNativeStaticState(context, selected.staticID);
      if (!selected.state)
        return std::nullopt;
      __int128 first =
          std::max<__int128>(0, -static_cast<__int128>(selected.offset));
      __int128 last = std::min<__int128>(
          bitWidth,
          static_cast<__int128>(selected.state->bitWidth) - selected.offset);
      if (first >= last) {
        selected.first = bitWidth;
        return selected;
      }
      selected.first = static_cast<uint64_t>(first);
      selected.width = static_cast<uint64_t>(last - first);
      __int128 offset = static_cast<__int128>(selected.offset) + first;
      if (offset < 0 || offset > INT64_MAX)
        return std::nullopt;
      selected.offset = static_cast<int64_t>(offset);
      selected.handle = obelisk::designbytecode::encodeStaticHandle(
          selected.staticID, selected.offset);
      if (selected.handle == UINT64_MAX)
        return std::nullopt;
      return selected;
    };
    std::optional<Selection> low = select(lowBitOffset);
    std::optional<Selection> high = select(highBitOffset);
    if (!low || !high || low->first != high->first ||
        low->width != high->width || low->width == 0 ||
        !validInertialStrengthPairUnlocked(context, *low->state, low->offset,
                                           *high->state, high->offset,
                                           low->width))
      return OBELISK_RT_INVALID_HANDLE;
    if (static_cast<__int128>(low->offset) + low->width - 1 > INT64_MAX ||
        static_cast<__int128>(high->offset) + high->width - 1 > INT64_MAX)
      return OBELISK_RT_INVALID_HANDLE;

    InertialDriverSite site{codeUnit, component};
    InertialStrengthPathPending &pending =
        context->inertialStrengthPathPending[site];
    constexpr bool pulseControlled = PulseControlled;
    auto cancelScheduled = [&](uint64_t bit, bool invalidateGeneration) {
      size_t index = static_cast<size_t>(bit);
      if (index >= pending.highSequence.size() ||
          pending.highSequence[index] == 0)
        return;
      context->scheduledInertialPathNBAs.erase(
          {pending.scheduledDueTime[index], pending.lowSequence[index]});
      context->scheduledInertialPathNBAs.erase(
          {pending.scheduledDueTime[index], pending.highSequence[index]});
      if constexpr (PulseControlled) {
        auto &live = pending.liveSequences[index];
        live.erase(
            std::remove(live.begin(), live.end(), pending.lowSequence[index]),
            live.end());
        live.erase(
            std::remove(live.begin(), live.end(), pending.highSequence[index]),
            live.end());
      }
      pending.scheduledDueTime[index] = 0;
      pending.lowSequence[index] = 0;
      pending.highSequence[index] = 0;
      // Pulse mode can retain older passed pairs in the keyed calendar. IEEE
      // 1800-2017 30.7 cancellation removes only the current leading pair;
      // generation invalidation is reserved for a nonpulse replacement or a
      // complete site reset so those earlier live pairs remain observable.
      if (invalidateGeneration && ++pending.generation[index] == 0)
        pending.generation[index] = 1;
    };
    size_t bytes = static_cast<size_t>((low->width - 1) / 8 + 1);
    auto resetState = [&] {
      // A site reset can cross the compact/pulse ABI boundary. Purge every
      // old strength-pair event by site before resetting generations: using
      // the new mode's latest-event cancellation would either index absent
      // pulse state or leave older Clause 30.7 passed pairs able to alias the
      // new generation. This scan is confined to rare identity/mode resets.
      if (pending.width != 0) {
        for (auto iterator = context->scheduledInertialPathNBAs.begin();
             iterator != context->scheduledInertialPathNBAs.end();) {
          const ScheduledNBA &update = iterator->second;
          if (update.inertialPathStrengthPair && update.inertialSite == site)
            iterator = context->scheduledInertialPathNBAs.erase(iterator);
          else
            ++iterator;
        }
      }
      pending = InertialStrengthPathPending{};
      pending.lowDestination = low->handle;
      pending.highDestination = high->handle;
      pending.width = low->width;
      pending.pulseControlled = pulseControlled;
      pending.lowValue.assign(bytes, 0);
      pending.lowUnknown.assign(bytes, 0);
      pending.highValue.assign(bytes, 0);
      pending.highUnknown.assign(bytes, 0);
      if constexpr (PulseControlled) {
        pending.transitionValue.assign(bytes, 0);
        pending.transitionUnknown.assign(bytes, 0);
      }
      pending.valid.assign(static_cast<size_t>(low->width), 0);
      pending.needsSchedule.assign(static_cast<size_t>(low->width), 0);
      pending.candidateDelay.assign(static_cast<size_t>(low->width),
                                    UINT64_MAX);
      if constexpr (PulseControlled) {
        pending.candidatePulseReject.assign(static_cast<size_t>(low->width),
                                            UINT64_MAX);
        pending.candidatePulseError.assign(static_cast<size_t>(low->width),
                                           UINT64_MAX);
        pending.candidatePulseFlags.assign(static_cast<size_t>(low->width), 0);
        pending.candidateFromSymbol.assign(static_cast<size_t>(low->width), 0);
      }
      pending.generation.assign(static_cast<size_t>(low->width), 1);
      pending.scheduledDueTime.assign(static_cast<size_t>(low->width), 0);
      pending.lowSequence.assign(static_cast<size_t>(low->width), 0);
      pending.highSequence.assign(static_cast<size_t>(low->width), 0);
      if constexpr (PulseControlled)
        pending.liveSequences.resize(static_cast<size_t>(low->width));
    };
    if (group == 0) {
      if (pending.lowDestination != low->handle ||
          pending.highDestination != high->handle ||
          pending.width != low->width ||
          pending.pulseControlled != pulseControlled)
        resetState();
      pending.nextGroup = 0;
      pending.groupCount = groupCount;
    } else if (pending.lowDestination != low->handle ||
               pending.highDestination != high->handle ||
               pending.width != low->width ||
               pending.groupCount != groupCount || pending.nextGroup != group ||
               pending.pulseControlled != pulseControlled)
      return OBELISK_RT_INVALID_ARGUMENT;

    auto sourceBit = [&](const uint8_t *plane, uint64_t bit) {
      return byteBit(plane, low->first + bit);
    };
    auto currentBit = [&](const Selection &selection, bool unknown,
                          uint64_t bit) {
      uint64_t absolute = selection.state->bitOffset +
                          static_cast<uint64_t>(selection.offset) + bit;
      const std::vector<uint64_t> &plane =
          unknown ? context->stateUnknown : context->stateValue;
      return absolute / 64 < plane.size() &&
             ((plane[absolute / 64] >> (absolute % 64)) & 1) != 0;
    };
    auto currentSymbol = [&](uint64_t bit) -> uint8_t {
      bool lowValueBit = currentBit(*low, false, bit);
      bool lowUnknownBit = currentBit(*low, true, bit);
      bool highValueBit = currentBit(*high, false, bit);
      bool highUnknownBit = currentBit(*high, true, bit);
      bool lowZero = !lowUnknownBit && !lowValueBit;
      bool lowZ = lowUnknownBit && lowValueBit;
      bool highOne = !highUnknownBit && highValueBit;
      bool highZ = highUnknownBit && highValueBit;
      if (lowZero && highZ)
        return 0;
      if (lowZ && highOne)
        return 1;
      if (lowZ && highZ)
        return 3;
      return 2;
    };
    auto enqueuePairAt = [&](uint64_t bit, uint64_t due, bool lowValueBit,
                             bool lowUnknownBit, bool highValueBit,
                             bool highUnknownBit) {
      if (context->nextSchedulerSequence == 0 ||
          context->nextSchedulerSequence > UINT64_MAX - 2)
        return false;
      size_t index = static_cast<size_t>(bit);
      auto enqueue = [&](const Selection &selection, bool value, bool unknown,
                         bool final) -> std::optional<uint64_t> {
        ScheduledNBA update;
        update.valuePlane = valuePlane;
        update.unknownPlane = unknownPlane;
        update.planeBitCount = planeBitCount;
        update.bitOffset = obelisk::designbytecode::encodeStaticHandle(
            selection.staticID, selection.offset + static_cast<int64_t>(bit));
        if (update.bitOffset == UINT64_MAX)
          return std::nullopt;
        update.bitWidth = 1;
        update.driver = true;
        update.deferDriverResolution = !final;
        update.forceDriverResolution = final;
        update.execRegion = OBELISK_RT_REGION_ACTIVE;
        update.sequence = context->nextSchedulerSequence++;
        update.dueTime = due;
        update.inertialSite = site;
        update.inertialPathDriver = true;
        update.inertialPathStrengthPair = true;
        update.inertialPathStrengthFinal = final;
        update.inertialPathBit = bit;
        update.inertialPathGeneration = pending.generation[index];
        update.inlinePacked = true;
        update.inlineValue = value ? 1 : 0;
        update.inlineUnknown = unknown ? 1 : 0;
        uint64_t sequence = update.sequence;
        if (!context->scheduledInertialPathNBAs
                 .emplace(std::make_pair(due, sequence), std::move(update))
                 .second)
          return std::nullopt;
        return sequence;
      };
      std::optional<uint64_t> lowSequence =
          enqueue(*low, lowValueBit, lowUnknownBit, false);
      if (!lowSequence)
        return false;
      std::optional<uint64_t> highSequence =
          enqueue(*high, highValueBit, highUnknownBit, true);
      if (!highSequence) {
        context->scheduledInertialPathNBAs.erase({due, *lowSequence});
        return false;
      }
      pending.scheduledDueTime[index] = due;
      pending.lowSequence[index] = *lowSequence;
      pending.highSequence[index] = *highSequence;
      if constexpr (PulseControlled) {
        pending.liveSequences[index].push_back(*lowSequence);
        pending.liveSequences[index].push_back(*highSequence);
      }
      return true;
    };
    auto enqueuePair = [&](uint64_t bit, uint64_t delay) {
      uint64_t due = delay > UINT64_MAX - context->schedulerTime
                         ? UINT64_MAX
                         : context->schedulerTime + delay;
      return enqueuePairAt(bit, due, byteBit(pending.lowValue.data(), bit),
                           byteBit(pending.lowUnknown.data(), bit),
                           byteBit(pending.highValue.data(), bit),
                           byteBit(pending.highUnknown.data(), bit));
    };

    if (group == 0) {
      for (uint64_t bit = 0; bit != low->width; ++bit) {
        size_t index = static_cast<size_t>(bit);
        bool nextLowValue = sourceBit(lowValue, bit);
        bool nextLowUnknown = sourceBit(lowUnknown, bit);
        bool nextHighValue = sourceBit(highValue, bit);
        bool nextHighUnknown = sourceBit(highUnknown, bit);
        bool sameTarget =
            pending.valid[index] &&
            byteBit(pending.lowValue.data(), bit) == nextLowValue &&
            byteBit(pending.lowUnknown.data(), bit) == nextLowUnknown &&
            byteBit(pending.highValue.data(), bit) == nextHighValue &&
            byteBit(pending.highUnknown.data(), bit) == nextHighUnknown;
        bool active = sourceBit(activeMask, bit);
        pending.needsSchedule[index] = 0;
        pending.candidateDelay[index] = UINT64_MAX;
        if constexpr (PulseControlled) {
          pending.candidatePulseReject[index] = UINT64_MAX;
          pending.candidatePulseError[index] = UINT64_MAX;
          pending.candidatePulseFlags[index] = 0;
        }
        if (sameTarget)
          continue;
        if constexpr (PulseControlled)
          pending.candidateFromSymbol[index] =
              pending.highSequence[index] != 0
                  ? (byteBit(pending.transitionUnknown.data(), bit)
                         ? (byteBit(pending.transitionValue.data(), bit) ? 3
                                                                         : 2)
                         : (byteBit(pending.transitionValue.data(), bit) ? 1
                                                                         : 0))
                  : currentSymbol(bit);
        bool changed = currentBit(*low, false, bit) != nextLowValue ||
                       currentBit(*low, true, bit) != nextLowUnknown ||
                       currentBit(*high, false, bit) != nextHighValue ||
                       currentBit(*high, true, bit) != nextHighUnknown;
        bool pulseEdge = PulseControlled && pending.highSequence[index] != 0;
        if constexpr (!PulseControlled)
          cancelScheduled(bit, true);
        setByteBit(pending.lowValue.data(), bit, nextLowValue);
        setByteBit(pending.lowUnknown.data(), bit, nextLowUnknown);
        setByteBit(pending.highValue.data(), bit, nextHighValue);
        setByteBit(pending.highUnknown.data(), bit, nextHighUnknown);
        if constexpr (PulseControlled) {
          setByteBit(pending.transitionValue.data(), bit,
                     sourceBit(transitionValue, bit));
          setByteBit(pending.transitionUnknown.data(), bit,
                     sourceBit(transitionUnknown, bit));
        }
        pending.valid[index] = changed || pulseEdge ? 1 : 0;
        if (!changed && !pulseEdge)
          continue;
        if (active)
          pending.needsSchedule[index] = 1;
        else {
          cancelScheduled(bit, !PulseControlled);
          if (changed && !enqueuePair(bit, 0))
            return OBELISK_RT_OUT_OF_RESOURCES;
        }
      }
    }

    for (uint64_t bit = 0; bit != low->width; ++bit) {
      size_t index = static_cast<size_t>(bit);
      if (!pending.needsSchedule[index])
        continue;
      bool targetValue = sourceBit(transitionValue, bit);
      bool targetUnknown = sourceBit(transitionUnknown, bit);
      uint64_t candidate = UINT64_MAX;
      if constexpr (PulseControlled) {
        uint8_t from = pending.candidateFromSymbol[index];
        uint8_t to =
            targetUnknown ? (targetValue ? 3 : 2) : (targetValue ? 1 : 0);
        constexpr uint8_t noTransition = UINT8_MAX;
        constexpr uint8_t transition[4][4] = {{noTransition, 0, 6, 2},
                                              {1, noTransition, 8, 4},
                                              {9, 7, noTransition, 10},
                                              {5, 3, 11, noTransition}};
        uint8_t transitionIndex = transition[from][to];
        // IEEE 1800-2017 30.5.1 keeps twelve independent transition-class
        // masks. A clipped native view therefore preserves the original
        // bitWidth class stride and applies its selected low->first offset.
        if (transitionIndex != noTransition &&
            byteBit(pulseTransitionMasks,
                    static_cast<uint64_t>(transitionIndex) * bitWidth +
                        low->first + bit))
          candidate = riseDelay;
      } else if (!targetUnknown && targetValue && sourceBit(riseMask, bit))
        candidate = riseDelay;
      else if (!targetUnknown && !targetValue && sourceBit(fallMask, bit))
        candidate = fallDelay;
      else if (targetUnknown && targetValue && sourceBit(turnoffMask, bit))
        candidate = turnoffDelay;
      else if (targetUnknown) {
        if (sourceBit(riseMask, bit))
          candidate = std::min(candidate, riseDelay);
        if (sourceBit(fallMask, bit))
          candidate = std::min(candidate, fallDelay);
        if (sourceBit(turnoffMask, bit))
          candidate = std::min(candidate, turnoffDelay);
      }
      if (candidate < pending.candidateDelay[index]) {
        pending.candidateDelay[index] = candidate;
        if constexpr (PulseControlled) {
          pending.candidatePulseReject[index] =
              pulseReject == UINT64_MAX ? candidate : pulseReject;
          pending.candidatePulseError[index] =
              pulseError == UINT64_MAX ? candidate : pulseError;
          pending.candidatePulseFlags[index] = static_cast<uint8_t>(
              pulseFlags & (OBELISK_RT_INERTIAL_PATH_ON_DETECT |
                            OBELISK_RT_INERTIAL_PATH_SHOW_CANCELLED));
        }
      }
    }
    pending.nextGroup = group + 1;
    if (group + 1 == groupCount) {
      __uint128_t updateCount = 0;
      for (uint8_t needed : pending.needsSchedule)
        updateCount += needed ? (PulseControlled ? 4 : 2) : 0;
      if (context->nextSchedulerSequence == 0 ||
          updateCount > static_cast<__uint128_t>(UINT64_MAX) -
                            context->nextSchedulerSequence)
        return OBELISK_RT_OUT_OF_RESOURCES;
      for (uint64_t bit = 0; bit != low->width; ++bit) {
        size_t index = static_cast<size_t>(bit);
        if (!pending.needsSchedule[index])
          continue;
        uint64_t delay = pending.candidateDelay[index];
        // IEEE 1800-2017 28.12.2 and 30.5.1 permit an L/H strength-range
        // change without a new 0/1/X/Z transition. Under 30.7 it is not a new
        // pulse edge: replace an already pending pair at its original due time
        // or publish the strength-only change immediately when none is pending.
        if (delay == UINT64_MAX && PulseControlled) {
          bool differs = currentBit(*low, false, bit) !=
                             byteBit(pending.lowValue.data(), bit) ||
                         currentBit(*low, true, bit) !=
                             byteBit(pending.lowUnknown.data(), bit) ||
                         currentBit(*high, false, bit) !=
                             byteBit(pending.highValue.data(), bit) ||
                         currentBit(*high, true, bit) !=
                             byteBit(pending.highUnknown.data(), bit);
          bool hadPending = pending.highSequence[index] != 0;
          uint64_t replacementDue = hadPending ? pending.scheduledDueTime[index]
                                               : context->schedulerTime;
          cancelScheduled(bit, false);
          if (differs &&
              !enqueuePairAt(bit, replacementDue,
                             byteBit(pending.lowValue.data(), bit),
                             byteBit(pending.lowUnknown.data(), bit),
                             byteBit(pending.highValue.data(), bit),
                             byteBit(pending.highUnknown.data(), bit)))
            return OBELISK_RT_OUT_OF_RESOURCES;
          pending.needsSchedule[index] = 0;
          continue;
        }
        if (delay == UINT64_MAX)
          delay = 0;
        if constexpr (PulseControlled) {
          uint64_t reject = pending.candidatePulseReject[index];
          uint64_t error = pending.candidatePulseError[index];
          uint8_t flags = pending.candidatePulseFlags[index];
          if (reject == UINT64_MAX || error == UINT64_MAX || error < reject)
            return OBELISK_RT_INVALID_ARGUMENT;
          uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                                 ? UINT64_MAX
                                 : context->schedulerTime + delay;
          uint64_t leadingSequence = pending.highSequence[index];
          uint64_t leadingDue = pending.scheduledDueTime[index];
          auto enqueueTargetAt = [&](uint64_t due) {
            return enqueuePairAt(bit, due,
                                 byteBit(pending.lowValue.data(), bit),
                                 byteBit(pending.lowUnknown.data(), bit),
                                 byteBit(pending.highValue.data(), bit),
                                 byteBit(pending.highUnknown.data(), bit));
          };
          auto scheduleFinalIfNeeded = [&](uint64_t due) {
            bool differs = currentBit(*low, false, bit) !=
                               byteBit(pending.lowValue.data(), bit) ||
                           currentBit(*low, true, bit) !=
                               byteBit(pending.lowUnknown.data(), bit) ||
                           currentBit(*high, false, bit) !=
                               byteBit(pending.highValue.data(), bit) ||
                           currentBit(*high, true, bit) !=
                               byteBit(pending.highUnknown.data(), bit);
            return !differs || enqueueTargetAt(due);
          };
          if (leadingSequence == 0) {
            if (!enqueueTargetAt(dueTime))
              return OBELISK_RT_OUT_OF_RESOURCES;
          } else {
            bool negative = dueTime < leadingDue;
            uint64_t pulseWidth =
                negative ? leadingDue - dueTime : dueTime - leadingDue;
            bool showCancelled =
                (flags & OBELISK_RT_INERTIAL_PATH_SHOW_CANCELLED) != 0;
            bool onDetect = (flags & OBELISK_RT_INERTIAL_PATH_ON_DETECT) != 0;
            // IEEE 1800-2017 30.7 applies the trailing transition's pulse
            // limits to the one logical strength-pair destination. Clauses
            // 30.7.4.1 and 30.7.4.2 place an inserted X at detection/event
            // time and define negative-pulse display; the X is an atomic L/H
            // pair so resolution never observes a half-strength contribution.
            bool passPulse = !negative && pulseWidth >= error;
            bool xPulse =
                (!negative && pulseWidth >= reject && pulseWidth < error) ||
                (negative && showCancelled);
            if (passPulse) {
              if (!enqueueTargetAt(dueTime))
                return OBELISK_RT_OUT_OF_RESOURCES;
            } else if (xPulse) {
              cancelScheduled(bit, false);
              uint64_t xDue = onDetect ? context->schedulerTime
                                       : std::min(leadingDue, dueTime);
              uint64_t finalDue =
                  negative ? std::max(leadingDue, dueTime) : dueTime;
              if (!enqueuePairAt(bit, xDue, false, true, false, true) ||
                  !enqueueTargetAt(finalDue))
                return OBELISK_RT_OUT_OF_RESOURCES;
            } else {
              cancelScheduled(bit, false);
              if (!scheduleFinalIfNeeded(dueTime))
                return OBELISK_RT_OUT_OF_RESOURCES;
            }
          }
          pending.needsSchedule[index] = 0;
          continue;
        }
        // IEEE 1800-2017 28.12.2 strength ranges are one logical primitive
        // output, while 30.5.1 delays its destination transition. Queue the low
        // bank first with deferred resolution and force resolution only after
        // the matching high-bank event matures at the same time.
        if (!enqueuePair(bit, delay))
          return OBELISK_RT_OUT_OF_RESOURCES;
        pending.needsSchedule[index] = 0;
      }
      pending.nextGroup = 0;
    }
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

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_inertial_path_strength_pair_pulse(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t lowBitOffset, uint64_t highBitOffset,
    uint64_t bitWidth, uint64_t codeUnit, uint32_t component, uint32_t group,
    uint32_t groupCount, uint32_t pulseFlags, uint64_t riseDelay,
    uint64_t fallDelay, uint64_t turnoffDelay, uint64_t pulseReject,
    uint64_t pulseError, const uint8_t *lowValue, const uint8_t *lowUnknown,
    const uint8_t *highValue, const uint8_t *highUnknown,
    const uint8_t *transitionValue, const uint8_t *transitionUnknown,
    const uint8_t *activeMask, const uint8_t *riseMask, const uint8_t *fallMask,
    const uint8_t *turnoffMask, const uint8_t *pulseTransitionMasks) {
  return schedulerInertialPathStrengthPair<true>(
      context, valuePlane, unknownPlane, planeBitCount, lowBitOffset,
      highBitOffset, bitWidth, codeUnit, component, group, groupCount,
      pulseFlags, riseDelay, fallDelay, turnoffDelay, pulseReject, pulseError,
      lowValue, lowUnknown, highValue, highUnknown, transitionValue,
      transitionUnknown, activeMask, riseMask, fallMask, turnoffMask,
      pulseTransitionMasks);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_inertial_path_strength_pair(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t lowBitOffset, uint64_t highBitOffset,
    uint64_t bitWidth, uint64_t codeUnit, uint32_t component, uint32_t group,
    uint32_t groupCount, uint64_t riseDelay, uint64_t fallDelay,
    uint64_t turnoffDelay, const uint8_t *lowValue, const uint8_t *lowUnknown,
    const uint8_t *highValue, const uint8_t *highUnknown,
    const uint8_t *transitionValue, const uint8_t *transitionUnknown,
    const uint8_t *activeMask, const uint8_t *riseMask, const uint8_t *fallMask,
    const uint8_t *turnoffMask) {
  return schedulerInertialPathStrengthPair<false>(
      context, valuePlane, unknownPlane, planeBitCount, lowBitOffset,
      highBitOffset, bitWidth, codeUnit, component, group, groupCount, 0,
      riseDelay, fallDelay, turnoffDelay, UINT64_MAX, UINT64_MAX, lowValue,
      lowUnknown, highValue, highUnknown, transitionValue, transitionUnknown,
      activeMask, riseMask, fallMask, turnoffMask, nullptr);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_clocking_nba(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown,
    uint64_t clockingOutput) {
  if (clockingOutput == UINT64_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  return schedulerNBA(context, valuePlane, unknownPlane, planeBitCount,
                      bitOffset, bitWidth, delay, value, unknown, false,
                      UINT64_MAX, false, clockingOutput);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_clocking_driver_nba(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown,
    uint64_t clockingOutput) {
  if (clockingOutput == UINT64_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  return schedulerNBA(context, valuePlane, unknownPlane, planeBitCount,
                      bitOffset, bitWidth, delay, value, unknown, false,
                      UINT64_MAX, true, clockingOutput);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_static_nba(
    obelisk_rt_context *context, uint64_t site, uint8_t *valuePlane,
    uint8_t *unknownPlane, uint64_t planeBitCount, uint64_t bitOffset,
    uint64_t bitWidth, const uint8_t *value, const uint8_t *unknown) {
  if (site == UINT64_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  return schedulerNBA(context, valuePlane, unknownPlane, planeBitCount,
                      bitOffset, bitWidth, 0, value, unknown, false, site);
}

static obelisk_rt_status stageStaticNBAPacked(
    obelisk_rt_context *context, uint32_t rootIndex, uint8_t *valuePlane,
    uint8_t *unknownPlane, uint64_t planeBitCount, uint64_t rootOffset,
    uint64_t bitWidth, uint64_t value, uint64_t unknown, bool guardedClaim) {
  if (!context || activeNativeAOTContext != context || !valuePlane ||
      bitWidth == 0 || bitWidth > 64 ||
      rootIndex >= context->staticNBAAccumulators.size() ||
      rootIndex >= context->nativeScheduleNBARootCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (guardedClaim ? !context->nativeSchedulePlan ||
                         (context->nativeSchedulePlan->flags &
                          OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA) == 0 ||
                         context->nativeScheduleDeoptimized
                   : !isStaticControlAOT(context) ||
                         context->nativeScheduleDeoptimized ||
                         context->nativeScheduleExternalWritePending)
    return OBELISK_RT_TIER_UNAVAILABLE;
  if (context->schedulerStatus != OBELISK_RT_OK)
    return context->schedulerStatus;
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  const NativeStaticState *staticState =
      findNativeStaticState(context, root.static_state);
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  if (!staticState || staticState->bitWidth != root.bit_width ||
      staticState->bitOffset > planeBitCount ||
      root.bit_width > planeBitCount - staticState->bitOffset || !plan ||
      valuePlane != plan->state_value ||
      (unknownPlane && unknownPlane != plan->state_unknown) ||
      planeBitCount != plan->state_bit_count || rootOffset > root.bit_width ||
      bitWidth > root.bit_width - rootOffset) {
    context->schedulerStatus = OBELISK_RT_LAYOUT_MISMATCH;
    return context->schedulerStatus;
  }
  if (guardedClaim) {
    if (rootIndex >= context->staticNBASlowRoots.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
      return context->schedulerStatus;
    }
    if (nativeStaticRootDirty(context, root.static_state)) {
      context->staticNBASlowRoots[rootIndex] = 1;
      context->staticNBASlowRootsPresent = true;
    }
    if (context->staticNBASlowRoots[rootIndex] != 0) {
      uint32_t execRegion = obelisk_rt_commit_region(
          context->activeHomeRegion == UINT32_MAX
              ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
              : context->activeHomeRegion);
      if (execRegion == UINT32_MAX) {
        context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
        return context->schedulerStatus;
      }
      if (obelisk_rt_status status = materializeGeneratedNBAAccumulatorUnlocked(
              context, rootIndex, execRegion);
          status != OBELISK_RT_OK) {
        context->schedulerStatus = status;
        return status;
      }
      invalidateNativeStaticSpecializationFastUnlocked(context);
      uint64_t rootHandle = obelisk_rt_stable_handle_encode(
          OBELISK_RT_STABLE_HANDLE_STATIC, root.static_state, 0);
      uint64_t destination =
          nativeHandleOffset(rootHandle, static_cast<int64_t>(rootOffset));
      return schedulerNBA(
          context, valuePlane, unknownPlane, planeBitCount, destination,
          bitWidth, 0, reinterpret_cast<const uint8_t *>(&value),
          unknownPlane ? reinterpret_cast<const uint8_t *>(&unknown) : nullptr,
          false);
    }
  }
  uint32_t execRegion = obelisk_rt_commit_region(
      context->activeHomeRegion == UINT32_MAX
          ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
          : context->activeHomeRegion);
  if (execRegion == UINT32_MAX || context->nextSchedulerSequence == 0) {
    context->schedulerStatus = execRegion == UINT32_MAX
                                   ? OBELISK_RT_INVALID_LIFECYCLE
                                   : OBELISK_RT_OUT_OF_RESOURCES;
    return context->schedulerStatus;
  }
  if (guardedClaim) {
    obelisk_rt_status status = materializeGeneratedNBAAccumulatorUnlocked(
        context, rootIndex, execRegion);
    if (status != OBELISK_RT_OK) {
      context->schedulerStatus = status;
      return status;
    }
  }

  StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[rootIndex];
  if (accumulator.valid && (accumulator.valuePlane != valuePlane ||
                            accumulator.unknownPlane != unknownPlane ||
                            accumulator.planeBitCount != planeBitCount ||
                            accumulator.execRegion != execRegion)) {
    context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
    return context->schedulerStatus;
  }
  accumulator.valuePlane = valuePlane;
  accumulator.unknownPlane = unknownPlane;
  accumulator.planeBitCount = planeBitCount;
  accumulator.execRegion = execRegion;
  markStaticNBAAccumulatorPending(context, rootIndex, accumulator);

  uint64_t sourceMask = packedWidthMask(bitWidth);
  value &= sourceMask;
  unknown = unknownPlane ? unknown & sourceMask : 0;
  size_t word = static_cast<size_t>(rootOffset / 64);
  unsigned shift = static_cast<unsigned>(rootOffset % 64);
  uint64_t lowMask = sourceMask << shift;
  accumulator.value[word] =
      (accumulator.value[word] & ~lowMask) | (value << shift);
  accumulator.unknown[word] =
      (accumulator.unknown[word] & ~lowMask) | (unknown << shift);
  accumulator.writeMask[word] |= lowMask;
  if (shift != 0 && bitWidth > 64 - shift) {
    uint64_t highMask = sourceMask >> (64 - shift);
    accumulator.value[word + 1] =
        (accumulator.value[word + 1] & ~highMask) | (value >> (64 - shift));
    accumulator.unknown[word + 1] =
        (accumulator.unknown[word + 1] & ~highMask) | (unknown >> (64 - shift));
    accumulator.writeMask[word + 1] |= highMask;
  }
  accumulator.sequence = context->nextSchedulerSequence++;
  ++context->signalDiagnostics.aotNBAStages;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_static_nba_packed(
    obelisk_rt_context *context, uint32_t rootIndex, uint8_t *valuePlane,
    uint8_t *unknownPlane, uint64_t planeBitCount, uint64_t rootOffset,
    uint64_t bitWidth, uint64_t value, uint64_t unknown) {
  return stageStaticNBAPacked(context, rootIndex, valuePlane, unknownPlane,
                              planeBitCount, rootOffset, bitWidth, value,
                              unknown, false);
}

obelisk_rt_status materializeGeneratedNBAAccumulatorUnlocked(
    obelisk_rt_context *context, uint32_t rootIndex, uint32_t execRegion) {
  if (rootIndex >= context->nativeScheduleNBARootCount ||
      rootIndex >= context->staticNBAAccumulators.size())
    return OBELISK_RT_INVALID_DESIGN;
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  obelisk_rt_generated_nba_accumulator_256 *generated =
      root.generated_accumulator;
  if (!generated || !hasGeneratedNBAStages(*generated))
    return OBELISK_RT_OK;
  std::optional<uint64_t> stageCount =
      root.bit_width <= OBELISK_RT_SCALAR_NBA_MAX_BITS
          ? std::optional<uint64_t>{1}
          : countGeneratedNBAStages(*generated);
  if (!stageCount || generated->exec_region != execRegion ||
      root.bit_width > OBELISK_RT_GENERATED_NBA_MAX_BITS)
    return OBELISK_RT_INVALID_DESIGN;
  StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[rootIndex];
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  if (!plan)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (!accumulator.valid) {
    if (context->nextSchedulerSequence == 0)
      return OBELISK_RT_OUT_OF_RESOURCES;
    accumulator.valuePlane = plan->state_value;
    accumulator.unknownPlane = root.bit_width <= OBELISK_RT_SCALAR_NBA_MAX_BITS
                                   ? plan->state_unknown
                                   : nullptr;
    accumulator.planeBitCount = plan->state_bit_count;
    accumulator.execRegion = execRegion;
    accumulator.sequence = context->nextSchedulerSequence++;
    markStaticNBAAccumulatorPending(context, rootIndex, accumulator);
  } else if (accumulator.execRegion != execRegion ||
             accumulator.valuePlane != plan->state_value ||
             (root.bit_width <= OBELISK_RT_SCALAR_NBA_MAX_BITS &&
              accumulator.unknownPlane != plan->state_unknown) ||
             accumulator.planeBitCount != plan->state_bit_count) {
    return OBELISK_RT_INVALID_LIFECYCLE;
  }
  size_t words = static_cast<size_t>((root.bit_width + 63) / 64);
  for (size_t word = 0; word != words; ++word) {
    uint64_t mask = generated->write_mask[word];
    accumulator.value[word] =
        (accumulator.value[word] & ~mask) | (generated->value[word] & mask);
    accumulator.unknown[word] =
        (accumulator.unknown[word] & ~mask) | (generated->unknown[word] & mask);
    accumulator.writeMask[word] |= mask;
    generated->write_mask[word] = 0;
  }
  generated->valid = 0;
  context->signalDiagnostics.aotNBAStages += *stageCount;
  return OBELISK_RT_OK;
}

extern "C" void obelisk_rt_v1_static_nba_stage_wide(
    obelisk_rt_context *context, uint32_t rootIndex, uint64_t rootOffset,
    uint64_t bitWidth, uint64_t value, uint64_t unknown, uint32_t hasUnknown) {
  if (!context)
    return;
  if (activeNativeAOTContext != context || hasUnknown > 1 || bitWidth == 0 ||
      bitWidth > 64 || rootIndex >= context->nativeScheduleNBARootCount ||
      rootIndex >= context->staticNBAAccumulators.size() ||
      !context->nativeSchedulePlan || context->nativeScheduleDeoptimized) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return;
  }
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  if (rootOffset > root.bit_width || bitWidth > root.bit_width - rootOffset) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_LAYOUT_MISMATCH);
    return;
  }
  uint32_t execRegion = obelisk_rt_commit_region(
      context->activeHomeRegion == UINT32_MAX
          ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
          : context->activeHomeRegion);
  if (execRegion == UINT32_MAX) {
    context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
    return;
  }
  if (obelisk_rt_status status = materializeGeneratedNBAAccumulatorUnlocked(
          context, rootIndex, execRegion);
      status != OBELISK_RT_OK) {
    context->schedulerStatus = status;
    return;
  }
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[rootIndex];
  if (!accumulator.valid) {
    if (context->nextSchedulerSequence == 0) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return;
    }
    accumulator.valuePlane = plan->state_value;
    accumulator.unknownPlane = hasUnknown ? plan->state_unknown : nullptr;
    accumulator.planeBitCount = plan->state_bit_count;
    accumulator.execRegion = execRegion;
    accumulator.sequence = context->nextSchedulerSequence++;
    markStaticNBAAccumulatorPending(context, rootIndex, accumulator);
  }
  uint64_t sourceMask = packedWidthMask(bitWidth);
  value &= sourceMask;
  unknown = hasUnknown ? unknown & sourceMask : 0;
  size_t word = static_cast<size_t>(rootOffset / 64);
  unsigned shift = static_cast<unsigned>(rootOffset % 64);
  uint64_t lowMask = sourceMask << shift;
  accumulator.value[word] =
      (accumulator.value[word] & ~lowMask) | (value << shift);
  accumulator.unknown[word] =
      (accumulator.unknown[word] & ~lowMask) | (unknown << shift);
  accumulator.writeMask[word] |= lowMask;
  if (shift != 0 && bitWidth > 64 - shift) {
    uint64_t highMask = sourceMask >> (64 - shift);
    accumulator.value[word + 1] =
        (accumulator.value[word + 1] & ~highMask) | (value >> (64 - shift));
    accumulator.unknown[word + 1] =
        (accumulator.unknown[word + 1] & ~highMask) | (unknown >> (64 - shift));
    accumulator.writeMask[word + 1] |= highMask;
  }
  ++context->signalDiagnostics.aotNBAStages;
}

extern "C" obelisk_rt_status obelisk_rt_v1_static_nba_claim(
    obelisk_rt_context *context, uint32_t rootIndex, uint8_t *valuePlane,
    uint8_t *unknownPlane, uint64_t planeBitCount, uint64_t rootOffset,
    uint64_t bitWidth, uint64_t value, uint64_t unknown) {
  return stageStaticNBAPacked(context, rootIndex, valuePlane, unknownPlane,
                              planeBitCount, rootOffset, bitWidth, value,
                              unknown, true);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_string_nba(
    obelisk_rt_context *context, uint8_t *valuePlane, uint64_t planeBitCount,
    uint64_t bitOffset, uint64_t delay, obelisk_rt_string_v1 value) {
  return schedulerNBA(context, valuePlane, nullptr, planeBitCount, bitOffset,
                      64, delay, reinterpret_cast<const uint8_t *>(&value),
                      nullptr, true);
}

#if defined(__x86_64__) || defined(_M_X64)
__attribute__((target("avx2"))) bool countGeneratedNBA256StagesAVX2(
    const obelisk_rt_generated_nba_accumulator_256 &generated,
    uint8_t &stageCount) {
  __m256i mask = _mm256_loadu_si256(
      reinterpret_cast<const __m256i *>(generated.write_mask));
  __m256i zero = _mm256_setzero_si256();
  __m256i full = _mm256_set1_epi32(-1);
  __m256i isZero = _mm256_cmpeq_epi32(mask, zero);
  __m256i isFull = _mm256_cmpeq_epi32(mask, full);
  unsigned validLanes = static_cast<unsigned>(
      _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_or_si256(isZero, isFull))));
  if (validLanes != UINT8_MAX)
    return false;
  unsigned zeroLanes =
      static_cast<unsigned>(_mm256_movemask_ps(_mm256_castsi256_ps(isZero)));
  stageCount = static_cast<uint8_t>(8 - __builtin_popcount(zeroLanes));
  return stageCount != 0;
}

__attribute__((target("avx2"))) bool commitGeneratedNBA256ByteAlignedAVX2(
    const obelisk_rt_generated_nba_accumulator_256 &generated,
    uint8_t *workingValue, uint8_t *workingUnknown, uint64_t byteOffset) {
  auto *value = reinterpret_cast<__m256i *>(workingValue + byteOffset);
  auto *unknown = reinterpret_cast<__m256i *>(workingUnknown + byteOffset);
  __m256i oldValue = _mm256_loadu_si256(value);
  __m256i oldUnknown = _mm256_loadu_si256(unknown);
  __m256i writeMask = _mm256_loadu_si256(
      reinterpret_cast<const __m256i *>(generated.write_mask));
  __m256i stagedValue =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(generated.value));
  __m256i stagedUnknown =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(generated.unknown));
  __m256i newValue = _mm256_or_si256(_mm256_andnot_si256(writeMask, oldValue),
                                     _mm256_and_si256(writeMask, stagedValue));
  __m256i newUnknown =
      _mm256_or_si256(_mm256_andnot_si256(writeMask, oldUnknown),
                      _mm256_and_si256(writeMask, stagedUnknown));
  __m256i changed = _mm256_or_si256(_mm256_xor_si256(oldValue, newValue),
                                    _mm256_xor_si256(oldUnknown, newUnknown));
  _mm256_storeu_si256(value, newValue);
  _mm256_storeu_si256(unknown, newUnknown);
  return !_mm256_testz_si256(changed, changed);
}

__attribute__((target("avx2"))) bool commitGeneratedNBA256ShiftedAVX2(
    const obelisk_rt_generated_nba_accumulator_256 &generated,
    uint8_t *workingValue, uint8_t *workingUnknown, uint64_t planeBit) {
  size_t byteOffset = static_cast<size_t>(planeBit / 64) * sizeof(uint64_t);
  uint64_t shift = planeBit % 64;
  __m256i shiftCount = _mm256_set1_epi64x(static_cast<int64_t>(shift));
  __m256i inverseCount = _mm256_set1_epi64x(static_cast<int64_t>(64 - shift));
  __m256i valueLow = _mm256_loadu_si256(
      reinterpret_cast<const __m256i *>(workingValue + byteOffset));
  __m256i valueHigh = _mm256_loadu_si256(
      reinterpret_cast<const __m256i *>(workingValue + byteOffset + 8));
  __m256i oldValue =
      _mm256_or_si256(_mm256_srlv_epi64(valueLow, shiftCount),
                      _mm256_sllv_epi64(valueHigh, inverseCount));
  __m256i unknownLow = _mm256_loadu_si256(
      reinterpret_cast<const __m256i *>(workingUnknown + byteOffset));
  __m256i unknownHigh = _mm256_loadu_si256(
      reinterpret_cast<const __m256i *>(workingUnknown + byteOffset + 8));
  __m256i oldUnknown =
      _mm256_or_si256(_mm256_srlv_epi64(unknownLow, shiftCount),
                      _mm256_sllv_epi64(unknownHigh, inverseCount));
  __m256i writeMask = _mm256_loadu_si256(
      reinterpret_cast<const __m256i *>(generated.write_mask));
  __m256i stagedValue =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(generated.value));
  __m256i stagedUnknown =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(generated.unknown));
  __m256i newValue = _mm256_or_si256(_mm256_andnot_si256(writeMask, oldValue),
                                     _mm256_and_si256(writeMask, stagedValue));
  __m256i newUnknown =
      _mm256_or_si256(_mm256_andnot_si256(writeMask, oldUnknown),
                      _mm256_and_si256(writeMask, stagedUnknown));
  __m256i changed = _mm256_or_si256(_mm256_xor_si256(oldValue, newValue),
                                    _mm256_xor_si256(oldUnknown, newUnknown));

  alignas(32) uint64_t valueWords[4];
  alignas(32) uint64_t unknownWords[4];
  _mm256_store_si256(reinterpret_cast<__m256i *>(valueWords), newValue);
  _mm256_store_si256(reinterpret_cast<__m256i *>(unknownWords), newUnknown);
  auto merge = [&](uint8_t *plane, const uint64_t *root) {
    auto loadWord = [&](unsigned word) {
      uint64_t value;
      std::memcpy(&value, plane + byteOffset + word * sizeof(uint64_t),
                  sizeof(value));
      return value;
    };
    auto storeWord = [&](unsigned word, uint64_t value) {
      std::memcpy(plane + byteOffset + word * sizeof(uint64_t), &value,
                  sizeof(value));
    };
    uint64_t lowMask = (uint64_t{1} << shift) - 1;
    storeWord(0, (loadWord(0) & lowMask) | (root[0] << shift));
    for (unsigned word = 1; word != 4; ++word)
      storeWord(word, (root[word - 1] >> (64 - shift)) | (root[word] << shift));
    storeWord(4, (loadWord(4) & ~lowMask) | (root[3] >> (64 - shift)));
  };
  merge(workingValue, valueWords);
  merge(workingUnknown, unknownWords);
  return !_mm256_testz_si256(changed, changed);
}

__attribute__((target("avx2"))) bool commitStaticNBA256AVX2(
    uint64_t *stagedValueWords, uint64_t *stagedUnknownWords,
    uint64_t *writeMaskWords, uint64_t *changedWords, uint64_t *posedgeWords,
    uint64_t *negedgeWords, uint64_t planeBit, uint64_t planeBitCount,
    uint8_t *workingValue, uint8_t *workingUnknown, uint64_t *canonicalValue,
    uint64_t *canonicalUnknown, bool synchronizeCanonical,
    bool trackTransitions, bool updateStagedValues) {
  size_t wordOffset = static_cast<size_t>(planeBit / 64);
  unsigned shift = static_cast<unsigned>(planeBit % 64);
  size_t byteOffset = wordOffset * sizeof(uint64_t);
  size_t planeBytes = static_cast<size_t>((planeBitCount + 7) / 8);
  size_t segmentBytes =
      std::min((shift == 0 ? size_t{4} : size_t{5}) * sizeof(uint64_t),
               planeBytes - byteOffset);
  alignas(32) uint64_t workingValueWords[5] = {};
  alignas(32) uint64_t workingUnknownWords[5] = {};
  std::memcpy(workingValueWords, workingValue + byteOffset, segmentBytes);
  std::memcpy(workingUnknownWords, workingUnknown + byteOffset, segmentBytes);
  alignas(32) uint64_t oldValueWords[4];
  alignas(32) uint64_t oldUnknownWords[4];
  auto extractRoot = [&](const uint64_t *segment, uint64_t *root) {
    if (shift == 0) {
      std::memcpy(root, segment, sizeof(oldValueWords));
      return;
    }
    for (unsigned word = 0; word != 4; ++word)
      root[word] =
          (segment[word] >> shift) | (segment[word + 1] << (64 - shift));
  };
  extractRoot(workingValueWords, oldValueWords);
  extractRoot(workingUnknownWords, oldUnknownWords);
  __m256i writeMask =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(writeMaskWords));
  __m256i oldValue =
      _mm256_load_si256(reinterpret_cast<const __m256i *>(oldValueWords));
  __m256i oldUnknown =
      _mm256_load_si256(reinterpret_cast<const __m256i *>(oldUnknownWords));
  __m256i stagedValue =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(stagedValueWords));
  __m256i stagedUnknown =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(stagedUnknownWords));
  __m256i newValue = _mm256_or_si256(_mm256_andnot_si256(writeMask, oldValue),
                                     _mm256_and_si256(writeMask, stagedValue));
  __m256i newUnknown =
      _mm256_or_si256(_mm256_andnot_si256(writeMask, oldUnknown),
                      _mm256_and_si256(writeMask, stagedUnknown));
  __m256i changed = _mm256_or_si256(_mm256_xor_si256(oldValue, newValue),
                                    _mm256_xor_si256(oldUnknown, newUnknown));
  if (updateStagedValues) {
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(stagedValueWords),
                        newValue);
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(stagedUnknownWords),
                        newUnknown);
  }
  if (trackTransitions) {
    __m256i oldZero = _mm256_andnot_si256(_mm256_or_si256(oldUnknown, oldValue),
                                          _mm256_set1_epi64x(-1));
    __m256i oldOne = _mm256_andnot_si256(oldUnknown, oldValue);
    __m256i newZero = _mm256_andnot_si256(_mm256_or_si256(newUnknown, newValue),
                                          _mm256_set1_epi64x(-1));
    __m256i newOne = _mm256_andnot_si256(newUnknown, newValue);
    __m256i posedge = _mm256_or_si256(_mm256_andnot_si256(newZero, oldZero),
                                      _mm256_and_si256(oldUnknown, newOne));
    __m256i negedge = _mm256_or_si256(_mm256_andnot_si256(newOne, oldOne),
                                      _mm256_and_si256(oldUnknown, newZero));
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(changedWords), changed);
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(posedgeWords), posedge);
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(negedgeWords), negedge);
  }
  alignas(32) uint64_t newValueWords[4];
  alignas(32) uint64_t newUnknownWords[4];
  _mm256_store_si256(reinterpret_cast<__m256i *>(newValueWords), newValue);
  _mm256_store_si256(reinterpret_cast<__m256i *>(newUnknownWords), newUnknown);
  auto mergeRoot = [&](uint64_t *segment, const uint64_t *rootWords) {
    if (shift == 0) {
      std::memcpy(segment, rootWords, 4 * sizeof(*rootWords));
    } else {
      uint64_t lowMask = (uint64_t{1} << shift) - 1;
      segment[0] = (segment[0] & lowMask) | (rootWords[0] << shift);
      for (unsigned word = 1; word != 4; ++word)
        segment[word] =
            (rootWords[word - 1] >> (64 - shift)) | (rootWords[word] << shift);
      segment[4] = (segment[4] & ~lowMask) | (rootWords[3] >> (64 - shift));
    }
  };
  mergeRoot(workingValueWords, newValueWords);
  mergeRoot(workingUnknownWords, newUnknownWords);
  std::memcpy(workingValue + byteOffset, workingValueWords, segmentBytes);
  std::memcpy(workingUnknown + byteOffset, workingUnknownWords, segmentBytes);
  if (synchronizeCanonical) {
    mergeRoot(canonicalValue + wordOffset, newValueWords);
    mergeRoot(canonicalUnknown + wordOffset, newUnknownWords);
  }
  return !_mm256_testz_si256(changed, changed);
}
#endif

obelisk_rt_status tryCommitGeneratedNBA256Unlocked(obelisk_rt_context *context,
                                                   uint32_t rootIndex,
                                                   uint32_t barrierRegion,
                                                   bool &changed,
                                                   bool &handled) {
  handled = false;
#if defined(__x86_64__) || defined(_M_X64)
  if (!context->nativeScheduleAVX2 ||
      rootIndex >= context->nativeScheduleNBARootCount ||
      rootIndex >= context->staticNBAAccumulators.size() ||
      rootIndex >= context->staticNBASlowRoots.size())
    return OBELISK_RT_OK;
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[rootIndex];
  obelisk_rt_generated_nba_accumulator_256 *generated =
      root.generated_accumulator;
  if (!generated || !hasGeneratedNBAStages(*generated) || accumulator.valid ||
      context->staticNBASlowRoots[rootIndex] != 0 || root.bit_width != 256 ||
      nativeStaticRootDirty(context, root.static_state) ||
      generated->exec_region != barrierRegion ||
      staticNBARootNeedsTransitions(context, rootIndex))
    return OBELISK_RT_OK;
  std::optional<uint64_t> stageCount = countGeneratedNBAStages(*generated);
  if (!stageCount)
    return OBELISK_RT_OK;
  const NativeStaticState *staticState =
      findNativeStaticState(context, root.static_state);
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  bool synchronizeCanonical =
      activeNativeAOTContext != context || !canUseStaticAOTFanout(context);
  bool canonical = plan && context->execution &&
                   context->execution->state_bit_count == plan->state_bit_count;
  if (!staticState || staticState->bitWidth != root.bit_width || !canonical ||
      staticState->bitOffset > plan->state_bit_count ||
      root.bit_width > plan->state_bit_count - staticState->bitOffset ||
      (synchronizeCanonical &&
       (context->stateValue.size() != (plan->state_bit_count + 63) / 64 ||
        context->stateUnknown.size() != context->stateValue.size())))
    return OBELISK_RT_OK;
  auto loadOverrideMask = [&](const std::vector<uint64_t> &plane,
                              uint64_t offset, uint64_t width) {
    return plane.empty() ? uint64_t{0}
                         : loadPackedBytes(
                               reinterpret_cast<const uint8_t *>(plane.data()),
                               offset, width);
  };
  if (!context->forceMask.empty() || !context->assignMask.empty())
    for (uint64_t local = 0; local < root.bit_width; local += 64) {
      uint64_t planeBit = staticState->bitOffset + local;
      if ((loadOverrideMask(context->forceMask, planeBit, 64) |
           loadOverrideMask(context->assignMask, planeBit, 64)) != 0)
        return OBELISK_RT_OK;
    }
  if (context->nextSchedulerSequence == 0)
    return OBELISK_RT_OUT_OF_RESOURCES;
  ++context->nextSchedulerSequence;
  bool rootChanged = commitStaticNBA256AVX2(
      generated->value, generated->unknown, generated->write_mask, nullptr,
      nullptr, nullptr, staticState->bitOffset, plan->state_bit_count,
      plan->state_value, plan->state_unknown, context->stateValue.data(),
      context->stateUnknown.data(), synchronizeCanonical, false, false);
  changed |= rootChanged;
  context->signalDiagnostics.aotNBAStages += *stageCount;
  std::fill(std::begin(generated->write_mask), std::end(generated->write_mask),
            uint64_t{0});
  generated->valid = 0;
  ++context->signalDiagnostics.aotNBACommits;
  handled = true;
#else
  (void)context;
  (void)rootIndex;
  (void)barrierRegion;
  (void)changed;
#endif
  return OBELISK_RT_OK;
}

obelisk_rt_status tryCommitGeneratedNBAScalarUnlocked(
    obelisk_rt_context *context, uint32_t rootIndex, uint32_t barrierRegion,
    bool &changed, bool &handled, bool trustedStaticFanout = false) {
  handled = false;
  if (rootIndex >= context->nativeScheduleNBARootCount ||
      rootIndex >= context->staticNBAAccumulators.size() ||
      rootIndex >= context->staticNBASlowRoots.size())
    return OBELISK_RT_OK;
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[rootIndex];
  obelisk_rt_generated_nba_accumulator_256 *generated =
      root.generated_accumulator;
  if (!generated || !hasGeneratedNBAStages(*generated) || accumulator.valid ||
      context->staticNBASlowRoots[rootIndex] != 0 || root.bit_width > 64 ||
      (!trustedStaticFanout &&
       nativeStaticRootDirty(context, root.static_state)) ||
      generated->exec_region != barrierRegion ||
      (!trustedStaticFanout &&
       (activeNativeAOTContext != context || !canUseStaticAOTFanout(context))))
    return OBELISK_RT_OK;
  // A scalar record represents one final root update regardless of its bit
  // width. Unlike the 256-bit lane form, its mask need not consist of full
  // 32-bit lanes.
  constexpr uint64_t stageCount = 1;
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  uint64_t stateOffset =
      rootIndex < context->nativeScheduleGeneratedNBAOffsets.size()
          ? context->nativeScheduleGeneratedNBAOffsets[rootIndex]
          : UINT64_MAX;
  if (!plan || stateOffset == UINT64_MAX ||
      stateOffset > plan->state_bit_count ||
      root.bit_width > plan->state_bit_count - stateOffset)
    return OBELISK_RT_OK;
  uint64_t widthMask = packedWidthMask(root.bit_width);
  uint64_t writeMask = generated->write_mask[0] & widthMask;
  auto loadOverrideMask = [&](const std::vector<uint64_t> &plane) {
    return plane.empty() ? uint64_t{0}
                         : loadPackedBytes(
                               reinterpret_cast<const uint8_t *>(plane.data()),
                               stateOffset, root.bit_width);
  };
  writeMask &= ~(loadOverrideMask(context->forceMask) |
                 loadOverrideMask(context->assignMask));
  uint64_t oldValue =
      loadPackedBytes(plan->state_value, stateOffset, root.bit_width);
  uint64_t oldUnknown =
      loadPackedBytes(plan->state_unknown, stateOffset, root.bit_width);
  uint64_t newValue =
      (oldValue & ~writeMask) | (generated->value[0] & writeMask);
  uint64_t newUnknown =
      (oldUnknown & ~writeMask) | (generated->unknown[0] & writeMask);
  storePackedBytes(plan->state_value, stateOffset, root.bit_width, newValue);
  storePackedBytes(plan->state_unknown, stateOffset, root.bit_width,
                   newUnknown);
  bool rootChanged = ((oldValue ^ newValue) | (oldUnknown ^ newUnknown)) != 0;
  changed |= rootChanged;
  context->signalDiagnostics.aotNBAStages += stageCount;
  generated->write_mask[0] = 0;
  generated->valid = 0;
  ++context->signalDiagnostics.aotNBACommits;
  handled = true;
  if (rootChanged)
    obelisk_rt_v1_scheduler_static_transition(context, root.static_state, 0,
                                              root.bit_width, oldValue,
                                              oldUnknown, newValue, newUnknown);
  return context->schedulerStatus;
}

obelisk_rt_status commitStaticNBARootUnlocked(obelisk_rt_context *context,
                                              uint32_t rootIndex,
                                              uint32_t barrierRegion,
                                              bool &changed,
                                              bool allowGeneratedFast = true) {
  if (!context->nativeSchedulePlan)
    return OBELISK_RT_OK;
  if (rootIndex >= context->staticNBAAccumulators.size() ||
      rootIndex >= context->nativeScheduleNBARootCount)
    return OBELISK_RT_INVALID_DESIGN;
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[rootIndex];
  obelisk_rt_generated_nba_accumulator_256 *generated =
      root.generated_accumulator;
  if ((!generated || !hasGeneratedNBAStages(*generated)) &&
      (!accumulator.valid || accumulator.execRegion != barrierRegion))
    return OBELISK_RT_OK;
  if (allowGeneratedFast) {
    bool generatedHandled = false;
    if (obelisk_rt_status status = tryCommitGeneratedNBAScalarUnlocked(
            context, rootIndex, barrierRegion, changed, generatedHandled);
        status != OBELISK_RT_OK || generatedHandled)
      return status;
    if (obelisk_rt_status status = tryCommitGeneratedNBA256Unlocked(
            context, rootIndex, barrierRegion, changed, generatedHandled);
        status != OBELISK_RT_OK || generatedHandled)
      return status;
  }
  bool trackTransitions = staticNBARootNeedsTransitions(context, rootIndex);
  if (obelisk_rt_status status = materializeGeneratedNBAAccumulatorUnlocked(
          context, rootIndex, barrierRegion);
      status != OBELISK_RT_OK)
    return status;
  if (!accumulator.valid || accumulator.execRegion != barrierRegion)
    return OBELISK_RT_OK;
  const NativeStaticState *staticState =
      findNativeStaticState(context, root.static_state);
  if (!staticState || staticState->bitWidth != root.bit_width ||
      staticState->bitOffset > accumulator.planeBitCount ||
      root.bit_width > accumulator.planeBitCount - staticState->bitOffset)
    return OBELISK_RT_LAYOUT_MISMATCH;
  bool canonical = context->execution && context->execution->state_bit_count ==
                                             accumulator.planeBitCount;
  if (canonical &&
      (context->stateValue.size() != (accumulator.planeBitCount + 63) / 64 ||
       context->stateUnknown.size() != context->stateValue.size()))
    return OBELISK_RT_INVALID_DESIGN;
  auto *workingValue = accumulator.valuePlane;
  auto *workingUnknown = accumulator.unknownPlane;
  if (!workingValue)
    return OBELISK_RT_INVALID_DESIGN;
  auto loadMask = [&](const std::vector<uint64_t> &plane, uint64_t offset,
                      uint64_t width) {
    return !canonical || plane.empty()
               ? uint64_t{0}
               : loadPackedBytes(
                     reinterpret_cast<const uint8_t *>(plane.data()), offset,
                     width);
  };
  bool rootChanged = false;
#if defined(__x86_64__) || defined(_M_X64)
  bool rootHasOverride = false;
  for (uint64_t local = 0; local < root.bit_width && !rootHasOverride;
       local += 64) {
    uint64_t width = std::min<uint64_t>(64, root.bit_width - local);
    uint64_t planeBit = staticState->bitOffset + local;
    rootHasOverride = (loadMask(context->forceMask, planeBit, width) |
                       loadMask(context->assignMask, planeBit, width)) != 0;
  }
  bool usedAVX2 =
      root.bit_width == 256 && context->nativeScheduleAVX2 && canonical &&
      !nativeStaticRootDirty(context, root.static_state) && !rootHasOverride &&
      accumulator.value.size() == 4 && accumulator.unknown.size() == 4 &&
      accumulator.writeMask.size() == 4 && accumulator.changed.size() == 4 &&
      accumulator.posedge.size() == 4 && accumulator.negedge.size() == 4;
  if (usedAVX2) {
    rootChanged = commitStaticNBA256AVX2(
        accumulator.value.data(), accumulator.unknown.data(),
        accumulator.writeMask.data(), accumulator.changed.data(),
        accumulator.posedge.data(), accumulator.negedge.data(),
        staticState->bitOffset, accumulator.planeBitCount, workingValue,
        workingUnknown ? workingUnknown
                       : context->nativeSchedulePlan->state_unknown,
        context->stateValue.data(), context->stateUnknown.data(), true,
        trackTransitions, true);
    auto *canonicalValue =
        reinterpret_cast<uint8_t *>(context->stateValue.data());
    auto *canonicalUnknown =
        reinterpret_cast<uint8_t *>(context->stateUnknown.data());
    for (uint64_t local = 0; local != root.bit_width; local += 64) {
      uint64_t planeBit = staticState->bitOffset + local;
      uint64_t newValue = loadPackedBytes(canonicalValue, planeBit, 64);
      uint64_t newUnknown = loadPackedBytes(canonicalUnknown, planeBit, 64);
      if (!storeNativeScheduleStateUnlocked(context, planeBit, 64, newValue,
                                            newUnknown))
        return OBELISK_RT_LAYOUT_MISMATCH;
    }
  } else
#endif
    for (uint64_t local = 0; local < root.bit_width; local += 64) {
      size_t word = static_cast<size_t>(local / 64);
      uint64_t width = std::min<uint64_t>(64, root.bit_width - local);
      uint64_t widthMask = packedWidthMask(width);
      uint64_t planeBit = staticState->bitOffset + local;
      uint64_t writeMask = accumulator.writeMask[word] & widthMask;
      writeMask &= ~(loadMask(context->forceMask, planeBit, width) |
                     loadMask(context->assignMask, planeBit, width));
      uint64_t oldValue = loadPackedBytes(workingValue, planeBit, width);
      uint64_t oldUnknown =
          workingUnknown ? loadPackedBytes(workingUnknown, planeBit, width)
                         : uint64_t{0};
      uint64_t newValue =
          (oldValue & ~writeMask) | (accumulator.value[word] & writeMask);
      uint64_t newUnknown =
          (oldUnknown & ~writeMask) | (accumulator.unknown[word] & writeMask);
      uint64_t changedBits =
          ((oldValue ^ newValue) | (oldUnknown ^ newUnknown)) & widthMask;
      if (trackTransitions) {
        uint64_t oldZero = ~oldUnknown & ~oldValue & widthMask;
        uint64_t oldOne = ~oldUnknown & oldValue & widthMask;
        uint64_t newZero = ~newUnknown & ~newValue & widthMask;
        uint64_t newOne = ~newUnknown & newValue & widthMask;
        accumulator.changed[word] = changedBits;
        accumulator.posedge[word] =
            ((oldZero & ~newZero) | (oldUnknown & newOne)) & widthMask;
        accumulator.negedge[word] =
            ((oldOne & ~newOne) | (oldUnknown & newZero)) & widthMask;
      }
      accumulator.value[word] = newValue;
      accumulator.unknown[word] = newUnknown;
      rootChanged |= changedBits != 0;
      if (accumulator.valuePlane)
        storePackedBytes(accumulator.valuePlane, planeBit, width, newValue);
      if (accumulator.unknownPlane)
        storePackedBytes(accumulator.unknownPlane, planeBit, width, newUnknown);
      const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
      if (plan && plan->state_bit_count == accumulator.planeBitCount) {
        if (plan->state_value != accumulator.valuePlane)
          storePackedBytes(plan->state_value, planeBit, width, newValue);
        if (plan->state_unknown &&
            plan->state_unknown != accumulator.unknownPlane)
          storePackedBytes(plan->state_unknown, planeBit, width, newUnknown);
      }
      if (canonical) {
        storePackedBytes(
            reinterpret_cast<uint8_t *>(context->stateValue.data()), planeBit,
            width, newValue);
        storePackedBytes(
            reinterpret_cast<uint8_t *>(context->stateUnknown.data()), planeBit,
            width, newUnknown);
      }
    }
  if (rootChanged) {
    if (!trackTransitions) {
      changed = true;
    } else {
      uint64_t rootHandle = obelisk_rt_stable_handle_encode(
          OBELISK_RT_STABLE_HANDLE_STATIC, root.static_state, 0);
      uint64_t sequence = 0;
      if (rootHandle == UINT64_MAX ||
          !obelisk_rt_publish_signal_transition_batch_unlocked(
              context, rootHandle, root.bit_width,
              reinterpret_cast<uint8_t *>(accumulator.changed.data()),
              reinterpret_cast<uint8_t *>(accumulator.posedge.data()),
              reinterpret_cast<uint8_t *>(accumulator.negedge.data()), 0,
              &sequence))
        return context->schedulerStatus;
      obelisk_rt_invalidate_signal_snapshots_unlocked(context, rootHandle,
                                                      root.bit_width);
      if (obelisk_rt_has_conditional_signal_waiters(context)) {
        for (uint64_t bit = 0; bit != root.bit_width; ++bit) {
          uint64_t mask = uint64_t{1} << (bit % 64);
          if ((accumulator.changed[bit / 64] & mask) == 0)
            continue;
          uint64_t eventHandle =
              nativeHandleOffset(rootHandle, static_cast<int64_t>(bit));
          context->signalValueSnapshots[eventHandle] = {
              sequence, (accumulator.value[bit / 64] & mask) != 0,
              (accumulator.unknown[bit / 64] & mask) != 0};
          uint32_t edges = OBELISK_RT_SIGNAL_CHANGE;
          if ((accumulator.posedge[bit / 64] & mask) != 0)
            edges |= OBELISK_RT_SIGNAL_POSEDGE;
          if ((accumulator.negedge[bit / 64] & mask) != 0)
            edges |= OBELISK_RT_SIGNAL_NEGEDGE;
          if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
                  context, eventHandle, edges))
            return context->schedulerStatus;
        }
      }
      if (!obelisk_rt_notify_observer_signal_unlocked(context, rootHandle,
                                                      root.bit_width))
        return context->schedulerStatus;
      changed = true;
    }
  }
  std::fill(accumulator.writeMask.begin(), accumulator.writeMask.end(),
            uint64_t{0});
  accumulator.valid = false;
  accumulator.sequence = 0;
  ++context->signalDiagnostics.aotNBACommits;
  return OBELISK_RT_OK;
}

#if defined(__x86_64__) || defined(_M_X64)
bool tryCommitGeneratedNBA256BatchUnlocked(obelisk_rt_context *context,
                                           uint32_t rootCount,
                                           uint32_t barrierRegion,
                                           bool &changed) {
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  if (!plan || activeNativeAOTContext != context ||
      !context->nativeScheduleAVX2 || !canUseStaticAOTFanout(context) ||
      context->nativeScheduleDirtyRootsPresent ||
      !context->nativeScheduleGeneratedBatchEligible ||
      rootCount != context->nativeScheduleNBARootCount ||
      rootCount != context->staticNBAAccumulators.size() ||
      rootCount != context->staticNBASlowRoots.size() ||
      rootCount != context->nativeScheduleGeneratedNBAStageCounts.size() ||
      rootCount != context->nativeScheduleGeneratedNBAOffsets.size() ||
      context->nextSchedulerSequence == 0 ||
      context->nextSchedulerSequence > UINT64_MAX - rootCount)
    return false;

  // Validate dynamic slot state before mutating any root. Root layout,
  // transition fanout, and generated-accumulator eligibility were proven once
  // when the revision-coupled plan was installed.
  for (uint32_t index = 0; index != rootCount; ++index) {
    const obelisk_rt_static_nba_root &root =
        context->nativeScheduleNBARoots[index];
    const StaticNBAAccumulator &accumulator =
        context->staticNBAAccumulators[index];
    const obelisk_rt_generated_nba_accumulator_256 *generated =
        root.generated_accumulator;
    uint8_t stageCount = 0;
    if (!generated || !hasGeneratedNBAStages(*generated) ||
        !countGeneratedNBA256StagesAVX2(*generated, stageCount) ||
        accumulator.valid || context->staticNBASlowRoots[index] != 0 ||
        generated->exec_region != barrierRegion)
      return false;
    context->nativeScheduleGeneratedNBAStageCounts[index] = stageCount;
  }

  uint64_t totalStages = 0;
  for (uint32_t index = 0; index != rootCount; ++index) {
    const obelisk_rt_static_nba_root &root =
        context->nativeScheduleNBARoots[index];
    obelisk_rt_generated_nba_accumulator_256 &generated =
        *root.generated_accumulator;
    totalStages += context->nativeScheduleGeneratedNBAStageCounts[index];
    uint64_t offset = context->nativeScheduleGeneratedNBAOffsets[index];
    if ((offset & 7) == 0)
      changed |= commitGeneratedNBA256ByteAlignedAVX2(
          generated, plan->state_value, plan->state_unknown, offset / 8);
    else if ((plan->state_bit_count + 7) / 8 -
                 static_cast<size_t>(offset / 64) * sizeof(uint64_t) >=
             5 * sizeof(uint64_t))
      changed |= commitGeneratedNBA256ShiftedAVX2(generated, plan->state_value,
                                                  plan->state_unknown, offset);
    else
      changed |= commitStaticNBA256AVX2(
          generated.value, generated.unknown, generated.write_mask, nullptr,
          nullptr, nullptr, offset, plan->state_bit_count, plan->state_value,
          plan->state_unknown, nullptr, nullptr, false, false, false);
    generated.write_mask[0] = 0;
    generated.write_mask[1] = 0;
    generated.write_mask[2] = 0;
    generated.write_mask[3] = 0;
    generated.valid = 0;
  }
  context->nextSchedulerSequence += rootCount;
  context->signalDiagnostics.aotNBAStages += totalStages;
  context->signalDiagnostics.aotNBACommits += rootCount;
  return true;
}
#endif

obelisk_rt_status commitStaticNBARootRangeUnlocked(obelisk_rt_context *context,
                                                   uint32_t rootCount,
                                                   uint32_t barrierRegion,
                                                   bool &changed) {
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  bool indexed = plan && plan->nba_dirty_roots && plan->nba_dirty_summary &&
                 rootCount <= plan->nba_root_count;
  bool trustedStaticFanout =
      activeNativeAOTContext == context && canUseStaticAOTFanout(context);
#if defined(__x86_64__) || defined(_M_X64)
  if (!indexed && tryCommitGeneratedNBA256BatchUnlocked(context, rootCount,
                                                        barrierRegion, changed))
    return OBELISK_RT_OK;
#endif
  auto commitRoot = [&](uint32_t root) -> obelisk_rt_status {
    // A generated callback may already have consumed the root while leaving
    // its dirty bit for this canonical index owner to clear. Avoid descending
    // through all three commit variants merely to rediscover that neither
    // accumulator has pending state.
    if (root >= context->nativeScheduleNBARootCount ||
        root >= context->staticNBAAccumulators.size())
      return OBELISK_RT_INVALID_DESIGN;
    const obelisk_rt_static_nba_root &rootPlan =
        context->nativeScheduleNBARoots[root];
    bool generatedPending =
        rootPlan.generated_accumulator &&
        hasGeneratedNBAStages(*rootPlan.generated_accumulator);
    bool accumulatorPending = context->staticNBAAccumulators[root].valid;
    if (!generatedPending && !accumulatorPending)
      return OBELISK_RT_OK;
    bool generatedHandled = false;
    if (obelisk_rt_status status = tryCommitGeneratedNBAScalarUnlocked(
            context, root, barrierRegion, changed, generatedHandled,
            trustedStaticFanout);
        status != OBELISK_RT_OK)
      return status;
    if (generatedHandled)
      return OBELISK_RT_OK;
    if (obelisk_rt_status status = tryCommitGeneratedNBA256Unlocked(
            context, root, barrierRegion, changed, generatedHandled);
        status != OBELISK_RT_OK)
      return status;
    if (generatedHandled)
      return OBELISK_RT_OK;
    return commitStaticNBARootUnlocked(context, root, barrierRegion, changed,
                                       false);
  };
  if (!indexed) {
    for (uint32_t root = 0; root != rootCount; ++root)
      if (obelisk_rt_status status = commitRoot(root); status != OBELISK_RT_OK)
        return status;
    return OBELISK_RT_OK;
  }

  // Traverse the compiler-owned bitmap like a tiny ordered radix index. The
  // summary level skips empty 64-root leaf pages; roots within a page retain
  // compute-graph order. A root stays indexed when it targets a later event
  // region and is removed only after all of its pending forms are consumed.
  for (uint32_t summaryIndex = 0;
       summaryIndex != plan->nba_dirty_summary_word_count; ++summaryIndex) {
    uint64_t summary = plan->nba_dirty_summary[summaryIndex];
    while (summary != 0) {
      uint32_t summaryBit = static_cast<uint32_t>(__builtin_ctzll(summary));
      uint32_t leafIndex = summaryIndex * 64 + summaryBit;
      if (leafIndex >= plan->nba_dirty_word_count)
        break;
      uint64_t roots = plan->nba_dirty_roots[leafIndex];
      while (roots != 0) {
        uint32_t rootBit = static_cast<uint32_t>(__builtin_ctzll(roots));
        uint32_t root = leafIndex * 64 + rootBit;
        if (root >= rootCount)
          break;
        if (obelisk_rt_status status = commitRoot(root);
            status != OBELISK_RT_OK)
          return status;
        const obelisk_rt_static_nba_root &rootPlan =
            context->nativeScheduleNBARoots[root];
        bool generatedPending =
            rootPlan.generated_accumulator &&
            hasGeneratedNBAStages(*rootPlan.generated_accumulator);
        bool accumulatorPending =
            root < context->staticNBAAccumulators.size() &&
            context->staticNBAAccumulators[root].valid;
        uint64_t rootMask = uint64_t{1} << rootBit;
        if (!generatedPending && !accumulatorPending)
          plan->nba_dirty_roots[leafIndex] &= ~rootMask;
        roots &= roots - 1;
      }
      uint64_t leafMask = uint64_t{1} << summaryBit;
      if (plan->nba_dirty_roots[leafIndex] == 0)
        plan->nba_dirty_summary[summaryIndex] &= ~leafMask;
      summary &= summary - 1;
    }
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status
commitStaticNBAAccumulatorsUnlocked(obelisk_rt_context *context,
                                    uint32_t barrierRegion, bool &changed) {
  if (!context->nativeSchedulePlan)
    return OBELISK_RT_OK;
  obelisk_rt_status status = OBELISK_RT_OK;
  if (context->nativeSchedulePlan->nba_commit) {
    uint32_t callbackChanged = changed ? 1u : 0u;
    status = context->nativeSchedulePlan->nba_commit(
        context->nativeSchedulePlan->mutable_state, context, barrierRegion,
        &callbackChanged);
    changed = callbackChanged != 0;
  } else {
    status = commitStaticNBARootRangeUnlocked(
        context, static_cast<uint32_t>(context->staticNBAAccumulators.size()),
        barrierRegion, changed);
  }
  if (status == OBELISK_RT_OK)
    refreshStaticNBAAccumulatorsPending(context);
  return status;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_static_nba_commit_root(obelisk_rt_context *context,
                                     uint32_t rootIndex, uint32_t barrierRegion,
                                     uint32_t *outChanged) {
  if (!context || !outChanged || !context->nativeSchedulePlan)
    return OBELISK_RT_INVALID_ARGUMENT;
  bool changed = *outChanged != 0;
  obelisk_rt_status status =
      commitStaticNBARootUnlocked(context, rootIndex, barrierRegion, changed);
  *outChanged = changed ? 1u : 0u;
  return status;
}

extern "C" obelisk_rt_status obelisk_rt_v1_static_nba_commit_roots(
    obelisk_rt_context *context, uint32_t rootCount, uint32_t barrierRegion,
    uint32_t *outChanged) {
  if (!context || !outChanged ||
      rootCount > context->staticNBAAccumulators.size() ||
      rootCount > context->nativeScheduleNBARootCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  bool changed = *outChanged != 0;
  obelisk_rt_status status = commitStaticNBARootRangeUnlocked(
      context, rootCount, barrierRegion, changed);
  *outChanged = changed ? 1u : 0u;
  return status;
}

extern "C" uint32_t
obelisk_rt_v1_static_nba_direct_commit_guard(obelisk_rt_context *context) {
  if (!context || activeNativeAOTContext != context ||
      lockedNativeAOTContext != context || !context->nativeSchedulePlan)
    return 0;
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  return (plan->flags & (OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
                         OBELISK_RT_NATIVE_SCHEDULE_STATIC_EVAL_ISLAND)) != 0 &&
         canUseStaticAOTFanout(context) &&
         !context->nativeScheduleExternalWritePending &&
         !context->nativeScheduleDirtyRootsPresent &&
         nativeStaticSpecializationEnvironmentClean(context) &&
         (!plan->specialization_fast || *plan->specialization_fast != 0);
}

extern "C" void
obelisk_rt_v1_static_nba_account_generated_commits(obelisk_rt_context *context,
                                                   uint32_t count) {
  if (!context || count == 0)
    return;
  context->signalDiagnostics.aotNBAStages += count;
  context->signalDiagnostics.aotNBACommits += count;
}

obelisk_rt_status
resolveClockingDriveConflictsUnlocked(obelisk_rt_context *context,
                                      uint32_t barrierRegion) {
  struct RootPosition {
    uint32_t kind;
    uint32_t id;
    int64_t offset;
  };
  auto locate = [](const ScheduledNBA &update, RootPosition &position) -> bool {
    if (decodeNativeAutomatic(update.bitOffset, position.id, position.offset)) {
      position.kind = 2;
      return true;
    }
    if (decodeNativeStatic(update.bitOffset, position.id, position.offset)) {
      position.kind = 1;
      return true;
    }
    if (decodeNativeGlobal(update.bitOffset, position.offset)) {
      position.kind = 0;
      position.id = 0;
      return true;
    }
    return false;
  };
  auto fourState = [](const ScheduledNBA &update) {
    return update.unknownPlane || !update.unknown.empty();
  };
  auto readBit = [](const ScheduledNBA &update, uint64_t bit,
                    bool unknown) -> bool {
    if (update.inlinePacked)
      return (((unknown ? update.inlineUnknown : update.inlineValue) >> bit) &
              uint64_t{1}) != 0;
    const std::vector<uint8_t> &plane = unknown ? update.unknown : update.value;
    return bit / 8 < plane.size() && byteBit(plane.data(), bit);
  };
  auto writeConflict = [&](ScheduledNBA &update, uint64_t bit) {
    bool unknown = fourState(update);
    if (update.inlinePacked) {
      uint64_t mask = uint64_t{1} << bit;
      update.inlineValue &= ~mask;
      if (unknown)
        update.inlineUnknown |= mask;
      return;
    }
    setByteBit(update.value.data(), bit, false);
    if (unknown)
      setByteBit(update.unknown.data(), bit, true);
  };

  bool conflict = false;
  for (size_t lhsIndex = 0; lhsIndex != context->scheduledNBAs.size();
       ++lhsIndex) {
    ScheduledNBA &lhs = context->scheduledNBAs[lhsIndex];
    if (lhs.clockingOutput == UINT64_MAX ||
        lhs.dueTime > context->schedulerTime || lhs.execRegion != barrierRegion)
      continue;
    RootPosition lhsPosition{};
    if (!locate(lhs, lhsPosition))
      return OBELISK_RT_INVALID_HANDLE;
    for (size_t rhsIndex = lhsIndex + 1;
         rhsIndex != context->scheduledNBAs.size(); ++rhsIndex) {
      ScheduledNBA &rhs = context->scheduledNBAs[rhsIndex];
      if (rhs.clockingOutput != lhs.clockingOutput ||
          rhs.dueTime != lhs.dueTime || rhs.execRegion != barrierRegion)
        continue;
      RootPosition rhsPosition{};
      if (!locate(rhs, rhsPosition))
        return OBELISK_RT_INVALID_HANDLE;
      if (lhsPosition.kind != rhsPosition.kind ||
          lhsPosition.id != rhsPosition.id)
        continue;
      __int128 begin =
          std::max<__int128>(lhsPosition.offset, rhsPosition.offset);
      __int128 end = std::min<__int128>(
          static_cast<__int128>(lhsPosition.offset) + lhs.bitWidth,
          static_cast<__int128>(rhsPosition.offset) + rhs.bitWidth);
      for (__int128 rootBit = begin; rootBit < end; ++rootBit) {
        uint64_t lhsBit = static_cast<uint64_t>(rootBit - lhsPosition.offset);
        uint64_t rhsBit = static_cast<uint64_t>(rootBit - rhsPosition.offset);
        bool lhsValue = readBit(lhs, lhsBit, false);
        bool rhsValue = readBit(rhs, rhsBit, false);
        bool lhsUnknown = readBit(lhs, lhsBit, true);
        bool rhsUnknown = readBit(rhs, rhsBit, true);
        if (lhsValue == rhsValue && lhsUnknown == rhsUnknown)
          continue;
        conflict = true;
        writeConflict(lhs, lhsBit);
        writeConflict(rhs, rhsBit);
      }
    }
  }
  if (conflict) {
    std::fprintf(stderr,
                 "error: conflicting synchronous clocking drives at time "
                 "%llu\n",
                 static_cast<unsigned long long>(context->schedulerTime));
    context->schedulerFinishStatus = OBELISK_RT_FATAL;
  }
  return OBELISK_RT_OK;
}

bool canCommitInlineNativeNBABarrierUnlocked(obelisk_rt_context *context,
                                             uint32_t barrierRegion) {
  if (!context->execution ||
      context->stateValue.size() !=
          (context->execution->state_bit_count + 63) / 64 ||
      context->stateUnknown.size() != context->stateValue.size())
    return false;
  for (const ScheduledNBA &update : context->scheduledNBAs) {
    if (update.dueTime > context->schedulerTime ||
        update.execRegion != barrierRegion)
      continue;
    uint32_t staticID = 0;
    int64_t offset = 0;
    if (update.clockingOutput != UINT64_MAX || update.driver ||
        !update.inlinePacked || update.stringValue || update.managedValue ||
        update.retainedAutomaticID != 0 || update.bitWidth == 0 ||
        update.bitWidth > 64 ||
        update.planeBitCount != context->execution->state_bit_count ||
        !decodeNativeStatic(update.bitOffset, staticID, offset) || offset < 0)
      return false;
    const NativeStaticState *state = findNativeStaticState(context, staticID);
    if (!state || state->bitOffset > update.planeBitCount ||
        state->bitWidth > update.planeBitCount - state->bitOffset ||
        static_cast<uint64_t>(offset) > state->bitWidth ||
        update.bitWidth > state->bitWidth - static_cast<uint64_t>(offset))
      return false;
  }
  return true;
}

obelisk_rt_status
commitInlineNativeNBABarrierUnlocked(obelisk_rt_context *context,
                                     uint32_t barrierRegion, bool &changed) {
  size_t retained = 0;
  for (size_t index = 0; index != context->scheduledNBAs.size(); ++index) {
    ScheduledNBA &update = context->scheduledNBAs[index];
    bool due = update.dueTime <= context->schedulerTime &&
               update.execRegion == barrierRegion;
    if (!due) {
      if (retained != index)
        context->scheduledNBAs[retained] = std::move(update);
      ++retained;
      continue;
    }

    uint32_t staticID = 0;
    int64_t offset = 0;
    if (!decodeNativeStatic(update.bitOffset, staticID, offset) || offset < 0)
      return OBELISK_RT_INVALID_HANDLE;
    const NativeStaticState *state = findNativeStaticState(context, staticID);
    if (!state)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t planeBit = state->bitOffset + static_cast<uint64_t>(offset);
    uint64_t widthMask = packedWidthMask(update.bitWidth);
    auto loadMask = [&](const std::vector<uint64_t> &mask) {
      if (mask.empty())
        return uint64_t{0};
      return loadPackedBytes(reinterpret_cast<const uint8_t *>(mask.data()),
                             planeBit, update.bitWidth);
    };
    uint64_t writableMask = widthMask & ~(loadMask(context->forceMask) |
                                          loadMask(context->assignMask));
    auto *canonicalValue =
        reinterpret_cast<uint8_t *>(context->stateValue.data());
    auto *canonicalUnknown =
        reinterpret_cast<uint8_t *>(context->stateUnknown.data());
    uint64_t oldValue =
        loadPackedBytes(canonicalValue, planeBit, update.bitWidth);
    uint64_t oldUnknown =
        loadPackedBytes(canonicalUnknown, planeBit, update.bitWidth);
    uint64_t newValue =
        (oldValue & ~writableMask) | (update.inlineValue & writableMask);
    uint64_t newUnknown =
        (oldUnknown & ~writableMask) | (update.inlineUnknown & writableMask);
    uint64_t changedBits =
        ((oldValue ^ newValue) | (oldUnknown ^ newUnknown)) & widthMask;

    if (update.valuePlane)
      storePackedBytes(update.valuePlane, planeBit, update.bitWidth, newValue);
    if (update.unknownPlane)
      storePackedBytes(update.unknownPlane, planeBit, update.bitWidth,
                       newUnknown);
    if (!storeNativeScheduleStateUnlocked(context, planeBit, update.bitWidth,
                                          newValue, newUnknown))
      return OBELISK_RT_LAYOUT_MISMATCH;
    storePackedBytes(canonicalValue, planeBit, update.bitWidth, newValue);
    storePackedBytes(canonicalUnknown, planeBit, update.bitWidth, newUnknown);

    if (changedBits == 0)
      continue;
    changed = true;
    uint64_t oldZero = ~oldUnknown & ~oldValue & widthMask;
    uint64_t oldOne = ~oldUnknown & oldValue & widthMask;
    uint64_t newZero = ~newUnknown & ~newValue & widthMask;
    uint64_t newOne = ~newUnknown & newValue & widthMask;
    uint64_t posedge = (oldZero & ~newZero) | (oldUnknown & newOne);
    uint64_t negedge = (oldOne & ~newOne) | (oldUnknown & newZero);
    PackedSignalTransitionBuffer transitions(update.bitWidth);
    uint64_t byteCount = (update.bitWidth + 7) / 8;
    std::memcpy(transitions.changed(), &changedBits, byteCount);
    std::memcpy(transitions.posedge(), &posedge, byteCount);
    std::memcpy(transitions.negedge(), &negedge, byteCount);
    uint64_t sequence = 0;
    if (!obelisk_rt_publish_signal_transition_batch_unlocked(
            context, update.bitOffset, update.bitWidth, transitions.changed(),
            transitions.posedge(), transitions.negedge(), 0, &sequence))
      return context->schedulerStatus;
    obelisk_rt_invalidate_signal_snapshots_unlocked(context, update.bitOffset,
                                                    update.bitWidth);
    if (obelisk_rt_has_conditional_signal_waiters(context)) {
      for (uint64_t bit = 0; bit != update.bitWidth; ++bit) {
        if (!byteBit(transitions.changed(), bit) ||
            bit > static_cast<uint64_t>(INT64_MAX))
          continue;
        uint64_t eventHandle =
            nativeHandleOffset(update.bitOffset, static_cast<int64_t>(bit));
        if (eventHandle == UINT64_MAX)
          continue;
        context->signalValueSnapshots[eventHandle] = {
            sequence, ((newValue >> bit) & uint64_t{1}) != 0,
            ((newUnknown >> bit) & uint64_t{1}) != 0};
      }
      for (uint64_t bit = 0; bit != update.bitWidth; ++bit) {
        if (!byteBit(transitions.changed(), bit) ||
            bit > static_cast<uint64_t>(INT64_MAX))
          continue;
        uint64_t eventHandle =
            nativeHandleOffset(update.bitOffset, static_cast<int64_t>(bit));
        if (eventHandle == UINT64_MAX)
          continue;
        uint32_t edges = OBELISK_RT_SIGNAL_CHANGE;
        if (byteBit(transitions.posedge(), bit))
          edges |= OBELISK_RT_SIGNAL_POSEDGE;
        if (byteBit(transitions.negedge(), bit))
          edges |= OBELISK_RT_SIGNAL_NEGEDGE;
        if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
                context, eventHandle, edges))
          return context->schedulerStatus;
      }
    }
    if (!obelisk_rt_notify_observer_signal_unlocked(context, update.bitOffset,
                                                    update.bitWidth))
      return context->schedulerStatus;
  }
  context->scheduledNBAs.resize(retained);
  return OBELISK_RT_OK;
}
