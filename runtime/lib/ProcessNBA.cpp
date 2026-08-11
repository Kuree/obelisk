//===- ProcessNBA.cpp - Non-blocking assignment staging and commit -------===//
//
// Staging and commit of non-blocking assignments: the generic scheduled-NBA
// queues, the packed/wide static NBA fast paths, the generated 256-bit
// accumulators (including their AVX2 kernels), and the inline barrier commit
// used by native AOT plans.  Split out of Process.cpp.
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

static obelisk_rt_status
schedulerNBA(obelisk_rt_context *context, uint8_t *valuePlane,
             uint8_t *unknownPlane, uint64_t planeBitCount, uint64_t bitOffset,
             uint64_t bitWidth, uint64_t delay, const uint8_t *value,
             const uint8_t *unknown, bool stringValue,
             uint64_t staticSite = UINT64_MAX) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto fail = [&](obelisk_rt_status status) {
    obelisk_rt_v1_scheduler_fail(context, status);
    return status;
  };
  if (!valuePlane || bitWidth == 0 || (bitWidth + 7) < bitWidth ||
      (stringValue && (bitWidth != 64 || unknownPlane)))
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
  obelisk_rt_string_v1 queuedString = 0;
  if (stringValue) {
    std::memcpy(&queuedString, value, sizeof(queuedString));
    obelisk_rt_status status =
        obelisk_rt_validate_string(context, queuedString);
    if (status != OBELISK_RT_OK)
      return fail(status);
  }
  try {
    ScheduledNBA update;
    update.valuePlane = valuePlane;
    update.unknownPlane = unknownPlane;
    update.planeBitCount = planeBitCount;
    update.bitOffset = bitOffset;
    update.bitWidth = bitWidth;
    update.stringValue = stringValue;
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
    if (staticSite != UINT64_MAX && boundedStatic && !stringValue &&
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
          for (uint64_t byte = 0; byte != byteCount; ++byte) {
            packedValue |= uint64_t{value[byte]} << (byte * 8);
            if (unknownPlane)
              packedUnknown |= uint64_t{unknown[byte]} << (byte * 8);
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
            setByteBit(accValue, destination, byteBit(value, source));
            setByteBit(accUnknown, destination,
                       unknownPlane && byteBit(unknown, source));
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
      for (uint64_t byte = 0; byte != byteCount; ++byte)
        update.inlineValue |= uint64_t{value[byte]} << (byte * 8);
      if (unknownPlane)
        for (uint64_t byte = 0; byte != byteCount; ++byte)
          update.inlineUnknown |= uint64_t{unknown[byte]} << (byte * 8);
      ++context->signalDiagnostics.aotNBAStages;
    } else {
      update.value.assign(value, value + static_cast<size_t>(byteCount));
      if (unknownPlane)
        update.unknown.assign(unknown,
                              unknown + static_cast<size_t>(byteCount));
    }
    update.sequence = context->nextSchedulerSequence++;
    update.dueTime = delay > UINT64_MAX - context->schedulerTime
                         ? UINT64_MAX
                         : context->schedulerTime + delay;
    context->scheduledNBAs.push_back(std::move(update));
    if (automatic)
      ++context->nativeAutomaticStates.find(automaticID)->second.referenceCount;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
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
    accumulator.unknownPlane =
        root.bit_width <= OBELISK_RT_SCALAR_NBA_MAX_BITS
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
      (!trustedStaticFanout && nativeStaticRootDirty(context, root.static_state)) ||
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
    return plane.empty()
               ? uint64_t{0}
               : loadPackedBytes(
                     reinterpret_cast<const uint8_t *>(plane.data()),
                     stateOffset, root.bit_width);
  };
  writeMask &= ~(loadOverrideMask(context->forceMask) |
                 loadOverrideMask(context->assignMask));
  uint64_t oldValue = loadPackedBytes(plan->state_value, stateOffset,
                                      root.bit_width);
  uint64_t oldUnknown = loadPackedBytes(plan->state_unknown, stateOffset,
                                        root.bit_width);
  uint64_t newValue =
      (oldValue & ~writeMask) | (generated->value[0] & writeMask);
  uint64_t newUnknown =
      (oldUnknown & ~writeMask) | (generated->unknown[0] & writeMask);
  storePackedBytes(plan->state_value, stateOffset, root.bit_width,
                   newValue);
  storePackedBytes(plan->state_unknown, stateOffset, root.bit_width,
                   newUnknown);
  bool rootChanged =
      ((oldValue ^ newValue) | (oldUnknown ^ newUnknown)) != 0;
  changed |= rootChanged;
  context->signalDiagnostics.aotNBAStages += stageCount;
  generated->write_mask[0] = 0;
  generated->valid = 0;
  ++context->signalDiagnostics.aotNBACommits;
  handled = true;
  if (rootChanged)
    obelisk_rt_v1_scheduler_static_transition(
        context, root.static_state, 0, root.bit_width, oldValue, oldUnknown,
        newValue, newUnknown);
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
  if (!indexed && tryCommitGeneratedNBA256BatchUnlocked(
                      context, rootCount, barrierRegion, changed))
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
    bool generatedPending = rootPlan.generated_accumulator &&
                            hasGeneratedNBAStages(
                                *rootPlan.generated_accumulator);
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
      if (obelisk_rt_status status = commitRoot(root);
          status != OBELISK_RT_OK)
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
        bool generatedPending = rootPlan.generated_accumulator &&
                                hasGeneratedNBAStages(
                                    *rootPlan.generated_accumulator);
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

extern "C" uint32_t obelisk_rt_v1_static_nba_direct_commit_guard(
    obelisk_rt_context *context) {
  if (!context || activeNativeAOTContext != context ||
      lockedNativeAOTContext != context || !context->nativeSchedulePlan)
    return 0;
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  return (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP) != 0 &&
         canUseStaticAOTFanout(context) &&
         !context->nativeScheduleExternalWritePending &&
         !context->nativeScheduleDirtyRootsPresent &&
         nativeStaticSpecializationEnvironmentClean(context) &&
         (!plan->specialization_fast || *plan->specialization_fast != 0);
}

extern "C" void obelisk_rt_v1_static_nba_account_generated_commits(
    obelisk_rt_context *context, uint32_t count) {
  if (!context || count == 0)
    return;
  context->signalDiagnostics.aotNBAStages += count;
  context->signalDiagnostics.aotNBACommits += count;
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
    if (!update.inlinePacked || update.stringValue || update.managedValue ||
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

