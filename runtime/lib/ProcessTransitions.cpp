//===- ProcessTransitions.cpp - Signal occurrence publication ------------===//
//
// Publication of signal occurrences and value transitions: subscription
// matching and wakeup, the indexed and static AOT fanout fast paths, and the
// scheduler entry points generated code calls to report signal, static, real,
// and event transitions.  Split out of Process.cpp.
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

void wakeMonitorProcessUnlocked(obelisk_rt_context *context,
                                uint64_t logicalToken) {
  if (!logicalToken)
    return;
  if ((logicalToken & kNativeLogicalProcessTag) != 0) {
    uint64_t token = logicalToken & ~kNativeLogicalProcessTag;
    if (ScheduledProcess *process = findScheduledProcess(context, token);
        process && process->instance) {
      obelisk_rt_unregister_signal_wait_unlocked(
          context, process->signalSubscriptions, process->token, false);
      process->suspendKind = OBELISK_RT_SUSPEND_NONE;
      process->signalTriggered = false;
      process->urgent = false;
      process->queuedRegion = OBELISK_RT_REGION_POSTPONED;
      context->nativePollCandidates.insert(token);
      if (++context->schedulerSelectionGeneration == 0)
        context->schedulerSelectionGeneration = 1;
    }
    return;
  }
  for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
    if (task.id != logicalToken || task.terminated)
      continue;
    obelisk_rt_unregister_signal_wait_unlocked(
        context, task.signalSubscriptions, task.id, true);
    task.suspendKind = OBELISK_RT_SUSPEND_NONE;
    task.signalTriggered = false;
    task.urgent = false;
    task.queuedRegion = OBELISK_RT_REGION_POSTPONED;
    context->designPollCandidates.insert(logicalToken);
    if (++context->schedulerSelectionGeneration == 0)
      context->schedulerSelectionGeneration = 1;
    return;
  }
}

template <typename Matches>
static bool publishSignalOccurrenceUnlocked(obelisk_rt_context *context,
                                            uint64_t stableID,
                                            uint64_t bitWidth,
                                            Matches &&matches,
                                            uint64_t *outSequence = nullptr) {
  if (context->nextSchedulerSequence == 0) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint64_t sequence = context->nextSchedulerSequence++;
  if (outSequence)
    *outSequence = sequence;
  if (context->signalDiagnosticsEnabled)
    ++context->signalDiagnostics.publications;
  // A direct native wait is polled by the generated schedule only while that
  // schedule owns execution.  During a transient fine-scheduler handoff the
  // same subscription must seed the runtime candidate set instead; otherwise
  // a force/assign that survives the current slot can strand every subsequent
  // edge-triggered actor while the fine scheduler advances time.
  bool fullyStaticAOT = !context->nativeScheduleStopAtCleanBoundary &&
                        context->nativeSchedulePlan &&
                        !context->nativeScheduleDeoptimized &&
                        (context->nativeSchedulePlan->flags &
                         OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC) != 0;

  uint32_t kind = 0;
  uint32_t objectID = 0;
  int64_t firstPage = 0;
  int64_t lastPage = 0;
  if (!signalSubscriptionBucketRange(stableID, bitWidth, kind, objectID,
                                     firstPage, lastPage)) {
    context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
    return false;
  }
  auto visitBucket = [&](int64_t page) {
    SignalSubscriptionBucketKey key{kind, objectID, page};
    auto bucket = context->signalSubscriptionBuckets.find(key);
    if (bucket != context->signalSubscriptionBuckets.end()) {
      for (const SignalSubscriptionBucketEntry &entry : bucket->second) {
        SignalSubscription *subscription = entry.subscription;
        if (!subscription || subscription->lastExaminedSequence == sequence)
          continue;
        subscription->lastExaminedSequence = sequence;
        if (context->signalDiagnosticsEnabled)
          ++context->signalDiagnostics.subscribersExamined;
        bool overlaps = false;
        if (fullyStaticAOT &&
            subscription->target == SignalSubscription::NativeDirectWait) {
          int64_t publishedOffset = static_cast<int32_t>(stableID);
          int64_t subscribedOffset =
              static_cast<int32_t>(subscription->stableID);
          overlaps =
              static_cast<__int128>(publishedOffset) + bitWidth >
                  subscribedOffset &&
              static_cast<__int128>(subscribedOffset) + subscription->bitWidth >
                  publishedOffset;
        } else {
          overlaps = rangesOverlap(subscription->stableID,
                                   subscription->bitWidth, stableID, bitWidth);
        }
        if (!overlaps)
          continue;
        if (subscription->suppressActiveSelf &&
            subscription->waiterToken != 0) {
          uint64_t logicalToken =
              subscription->target == SignalSubscription::NativeDirectWait
                  ? kNativeLogicalProcessTag | subscription->waiterToken
                  : subscription->waiterToken;
          if (context->activeLogicalProcessToken == logicalToken)
            continue;
        }
        if (!matches(*subscription))
          continue;
        if (subscription->target == SignalSubscription::NativeDirectWait ||
            subscription->target == SignalSubscription::DesignDirectWait) {
          if (!subscription->latch || subscription->latch->triggered)
            continue;
          subscription->latch->triggered = true;
          bool staticallyPolledNative =
              fullyStaticAOT &&
              subscription->target == SignalSubscription::NativeDirectWait;
          if (staticallyPolledNative) {
            ScheduledProcess *scheduled =
                findScheduledProcess(context, subscription->waiterToken);
            if (!scheduled || scheduled->aotActorSlot == UINT32_MAX ||
                !markNativeAOTActorReadyUnlocked(context,
                                                 scheduled->aotActorSlot)) {
              context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
              return false;
            }
          }
          if (!staticallyPolledNative && subscription->waiterToken != 0) {
            auto &candidates =
                subscription->target == SignalSubscription::NativeDirectWait
                    ? context->nativePollCandidates
                    : context->designPollCandidates;
            try {
              candidates.insert(subscription->waiterToken);
            } catch (const std::bad_alloc &) {
              context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
              return false;
            }
          }
          if (!staticallyPolledNative &&
              ++context->schedulerSelectionGeneration == 0)
            context->schedulerSelectionGeneration = 1;
          continue;
        }
        if (!subscription->latch || subscription->latch->affected)
          continue;
        try {
          auto &pending =
              subscription->target == SignalSubscription::NativeComputedWait
                  ? context->pendingNativeComputedWaiters
                  : context->pendingDesignComputedWaiters;
          pending.push_back(subscription->waiterToken);
          subscription->latch->affected = true;
        } catch (const std::bad_alloc &) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
          return false;
        }
      }
    }
    return true;
  };
  __int128 pageCount = static_cast<__int128>(lastPage) - firstPage + 1;
  if (pageCount <= kMaximumIndexedSignalPages) {
    if (!visitBucket(kWideSignalSubscriptionPage))
      return false;
    for (int64_t page = firstPage;; ++page) {
      if (!visitBucket(page))
        return false;
      if (page == lastPage)
        break;
    }
  } else {
    std::vector<int64_t> pages;
    try {
      for (const auto &[key, bucket] : context->signalSubscriptionBuckets) {
        (void)bucket;
        if (key.kind == kind && key.id == objectID)
          pages.push_back(key.page);
      }
    } catch (const std::bad_alloc &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
      return false;
    }
    for (int64_t page : pages)
      if (!visitBucket(page))
        return false;
  }
  return true;
}

static bool signalTransitionBatchMatches(const SignalSubscription &subscription,
                                  uint64_t stableID, uint64_t bitWidth,
                                  const uint8_t *changed,
                                  const uint8_t *posedge,
                                  const uint8_t *negedge,
                                  uint64_t edgeBitOffset) {
  int64_t publishedOffset = 0;
  int64_t subscribedOffset = 0;
  if ((stableID & OBELISK_RT_STABLE_HANDLE_TAG_MASK) ==
          OBELISK_RT_STABLE_HANDLE_STATIC_TAG &&
      (subscription.stableID & OBELISK_RT_STABLE_HANDLE_TAG_MASK) ==
          OBELISK_RT_STABLE_HANDLE_STATIC_TAG) {
    if ((stableID >> 32) != (subscription.stableID >> 32))
      return false;
    publishedOffset = static_cast<int32_t>(stableID);
    subscribedOffset = static_cast<int32_t>(subscription.stableID);
  } else {
    obelisk_rt_stable_handle_v1 published;
    obelisk_rt_stable_handle_v1 subscribed;
    if (!obelisk_rt_stable_handle_decode(stableID, &published) ||
        !obelisk_rt_stable_handle_decode(subscription.stableID, &subscribed) ||
        published.kind != subscribed.kind ||
        (published.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL &&
         published.id != subscribed.id))
      return false;
    publishedOffset = published.offset;
    subscribedOffset = subscribed.offset;
  }
  __int128 overlapBegin = std::max<__int128>(publishedOffset, subscribedOffset);
  __int128 overlapEnd =
      std::min(static_cast<__int128>(publishedOffset) + bitWidth,
               static_cast<__int128>(subscribedOffset) + subscription.bitWidth);
  bool direct = subscription.target == SignalSubscription::NativeDirectWait ||
                subscription.target == SignalSubscription::DesignDirectWait;
  for (__int128 coordinate = overlapBegin; coordinate < overlapEnd;
       ++coordinate) {
    uint64_t index =
        edgeBitOffset + static_cast<uint64_t>(coordinate - publishedOffset);
    uint32_t observed = byteBit(changed, index) ? OBELISK_RT_SIGNAL_CHANGE : 0;
    if (byteBit(posedge, index))
      observed |= OBELISK_RT_SIGNAL_POSEDGE;
    if (byteBit(negedge, index))
      observed |= OBELISK_RT_SIGNAL_NEGEDGE;
    if (observed != 0 &&
        (!direct || signalEdgeMatches(subscription.edge, observed)))
      return true;
  }
  return false;
}

static bool staticAOTFanoutRangeHasConsumer(const obelisk_rt_context *context,
                                     uint32_t staticID, uint64_t staticOffset,
                                     uint64_t bitWidth) {
  if (context->nativeScheduleFanoutEntryCount == 0)
    return false;
  const obelisk_rt_static_fanout_entry *begin =
      context->nativeScheduleFanoutEntries;
  const obelisk_rt_static_fanout_entry *end =
      begin + context->nativeScheduleFanoutEntryCount;
  auto first = end;
  auto last = end;
  if (staticID < context->nativeScheduleFanoutRanges.size()) {
    auto [firstIndex, lastIndex] =
        context->nativeScheduleFanoutRanges[staticID];
    if (firstIndex <= lastIndex &&
        lastIndex <= context->nativeScheduleFanoutEntryCount) {
      first = begin + firstIndex;
      last = begin + lastIndex;
    }
  } else {
    first = std::lower_bound(
        begin, end, staticID,
        [](const obelisk_rt_static_fanout_entry &entry, uint32_t id) {
          return entry.static_state < id;
        });
    last = first;
    while (last != end && last->static_state == staticID)
      ++last;
  }
  return std::any_of(
      first, last, [&](const obelisk_rt_static_fanout_entry &entry) {
        return static_cast<__int128>(staticOffset) <
                   static_cast<__int128>(entry.low_bit) + entry.bit_width &&
               static_cast<__int128>(entry.low_bit) <
                   static_cast<__int128>(staticOffset) + bitWidth;
      });
}

template <bool UseClockIngress>
static bool publishStaticAOTSignalTransitionUnlockedImpl(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t *outSequence, bool indexedExternalDeposit) {
  if (outSequence)
    *outSequence = 0;
  if (!(indexedExternalDeposit ? canUseIndexedExternalAOTFanout(context)
                               : canUseStaticAOTFanout(context)))
    return false;
  uint32_t staticID = 0;
  int64_t staticOffset = 0;
  if (!decodeNativeStatic(stableID, staticID, staticOffset) || staticOffset < 0)
    return false;
  uint64_t publishedLow = static_cast<uint64_t>(staticOffset);
  // Exact static fanout proves that no observer, conditional waiter, design
  // task, or unlisted direct wait can consume this transition.  Keep the
  // canonical state update but do not manufacture a scheduler publication.
  if (!staticAOTFanoutRangeHasConsumer(context, staticID, publishedLow,
                                       bitWidth))
    return true;
  // Metadata-only clock plans predate executable generated fragments.  Their
  // coordinator is nevertheless the owner of external-deposit ingress, so
  // retain that routing while using the ownership marker for hybrid plans.
  // This scan is confined to the asynchronous publication path and therefore
  // does not add work to the generated periodic loop.
  bool metadataOnlyClockCoordinator = false;
  if constexpr (UseClockIngress) {
    const obelisk_rt_native_schedule_plan *plan =
        context->nativeSchedulePlan;
    metadataOnlyClockCoordinator =
        plan->merged_fragment_count != 0 &&
        std::none_of(plan->merged_fragments,
                     plan->merged_fragments + plan->merged_fragment_count,
                     [](const obelisk_rt_native_merged_fragment &fragment) {
                       return fragment.execute != nullptr;
                     });
  }
  bool published = false;
  const obelisk_rt_static_fanout_entry *begin =
      context->nativeScheduleFanoutEntries;
  const obelisk_rt_static_fanout_entry *end =
      begin + context->nativeScheduleFanoutEntryCount;
  auto first = end;
  auto last = end;
  if (staticID < context->nativeScheduleFanoutRanges.size()) {
    auto [firstIndex, lastIndex] =
        context->nativeScheduleFanoutRanges[staticID];
    if (firstIndex <= lastIndex &&
        lastIndex <= context->nativeScheduleFanoutEntryCount) {
      first = begin + firstIndex;
      last = begin + lastIndex;
    }
  } else {
    first = std::lower_bound(
        begin, end, staticID,
        [](const obelisk_rt_static_fanout_entry &entry, uint32_t id) {
          return entry.static_state < id;
        });
    last = first;
    while (last != end && last->static_state == staticID)
      ++last;
  }
  for (auto entry = first; entry != last; ++entry) {
    ++context->signalDiagnostics.aotFanoutEntries;
    uint32_t slot = entry->actor_slot;
    if (slot >= context->nativeScheduleActors.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return true;
    }
    obelisk_rt_process_instance_v1 *actor = context->nativeScheduleActors[slot];
    if (!actor)
      continue;
    size_t index = context->nativeScheduleActorIndices[slot];
    if (index >= context->scheduledProcesses.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return true;
    }
    ScheduledProcess &scheduled = context->scheduledProcesses[index];
    bool activeSelf =
        context->activeLogicalProcessToken ==
        (kNativeLogicalProcessTag | scheduled.token);
    if ((actor->continuation != entry->continuation && !activeSelf) ||
        scheduled.instance != actor || !scheduled.started ||
        (scheduled.signalTriggered && !activeSelf) ||
        (scheduled.suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
         scheduled.suspendKind != OBELISK_RT_SUSPEND_EDGE))
      continue;
    uint64_t overlapLow = std::max(publishedLow, entry->low_bit);
    uint64_t overlapHigh =
        std::min(publishedLow + bitWidth, entry->low_bit + entry->bit_width);
    bool matched = false;
    for (uint64_t coordinate = overlapLow; coordinate < overlapHigh;
         ++coordinate) {
      uint64_t bit = coordinate - publishedLow;
      if (entry->edge == OBELISK_RT_WAIT_EDGE_CHANGE)
        matched = byteBit(changed, bit);
      else if (entry->edge == OBELISK_RT_WAIT_EDGE_POSEDGE)
        matched = byteBit(posedge, bit);
      else if (entry->edge == OBELISK_RT_WAIT_EDGE_NEGEDGE)
        matched = byteBit(negedge, bit);
      else
        matched = byteBit(posedge, bit) || byteBit(negedge, bit);
      if (matched)
        break;
    }
    if (!matched)
      continue;
    if (!published) {
      if (context->nextSchedulerSequence == 0) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return true;
      }
      uint64_t sequence = context->nextSchedulerSequence++;
      if (outSequence)
        *outSequence = sequence;
      if (context->signalDiagnosticsEnabled)
        ++context->signalDiagnostics.publications;
      published = true;
    }
    if constexpr (UseClockIngress) {
      if (entry->reserved == OBELISK_RT_FANOUT_DIRECT ||
          metadataOnlyClockCoordinator) {
        if (entry->kernel >=
                context->nativeSchedulePlan->clock_kernel_count ||
            entry->merged_bit / 64 >=
                context->nativeSchedulePlan
                    ->clock_kernels[entry->kernel]
                    .ingress_word_count) {
          context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
          return true;
        }
        obelisk_rt_native_clock_kernel kernel =
            context->nativeSchedulePlan->clock_kernels[entry->kernel];
        kernel.ingress_mask[entry->merged_bit / 64] |=
            uint64_t{1} << (entry->merged_bit % 64);
        context->nativeScheduleClockIngressPending = true;
        continue;
      }
    }
    scheduled.signalTriggered = true;
    uint32_t node = entry->compute_node;
    if (node >= context->nativeScheduleNodes.size() ||
        node / 64 >= context->nativeScheduleReadyNodes.size() ||
        context->nativeScheduleNodes[node].actor_slot != slot ||
        context->nativeScheduleNodes[node].continuation !=
            entry->continuation) {
      context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
      return true;
    }
    context->nativeScheduleReadyNodes[node / 64] |=
        uint64_t{1} << (node % 64);
    context->nativeScheduleMinimumActivatedNode =
        std::min(context->nativeScheduleMinimumActivatedNode, node);
  }
  return true;
}

bool publishStaticAOTSignalTransitionUnlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t *outSequence, bool indexedExternalDeposit) {
  bool useClockIngress =
      context && context->nativeSchedulePlan &&
      context->nativeSchedulePlan->clock_kernel_count != 0;
  return useClockIngress
             ? publishStaticAOTSignalTransitionUnlockedImpl<true>(
                   context, stableID, bitWidth, changed, posedge, negedge,
                   outSequence, indexedExternalDeposit)
             : publishStaticAOTSignalTransitionUnlockedImpl<false>(
                   context, stableID, bitWidth, changed, posedge, negedge,
                   outSequence, indexedExternalDeposit);
}

static bool publishSignalTransitionBatchImpl(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t edgeBitOffset, uint64_t *outSequence) {
  if (!context || bitWidth == 0 || !changed || !posedge || !negedge)
    return context != nullptr;
  bool anyChanged = false;
  for (uint64_t bit = 0; bit != bitWidth; ++bit)
    anyChanged |= byteBit(changed, edgeBitOffset + bit);
  if (!anyChanged)
    return true;
  return publishSignalOccurrenceUnlocked(
      context, stableID, bitWidth,
      [&](const SignalSubscription &subscription) {
        return signalTransitionBatchMatches(subscription, stableID, bitWidth,
                                            changed, posedge, negedge,
                                            edgeBitOffset);
      },
      outSequence);
}


bool obelisk_rt_publish_signal_transition_batch_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t edgeBitOffset, uint64_t *outSequence) {
  if (edgeBitOffset == 0 && publishStaticAOTSignalTransitionUnlocked(
                                context, stableID, bitWidth, changed, posedge,
                                negedge, outSequence, false)) {
    return context->schedulerStatus == OBELISK_RT_OK;
  }
  return publishSignalTransitionBatchImpl(context, stableID, bitWidth, changed,
                                          posedge, negedge, edgeBitOffset,
                                          outSequence);
}

bool obelisk_rt_publish_signal_occurrence_unlocked(obelisk_rt_context *context,
                                                   uint64_t stableID,
                                                   uint64_t bitWidth,
                                                   uint32_t edges,
                                                   uint64_t *outSequence) {
  if (!context || bitWidth == 0 || edges == 0)
    return context != nullptr;
  return publishSignalOccurrenceUnlocked(
      context, stableID, bitWidth,
      [&](const SignalSubscription &subscription) {
        bool direct =
            subscription.target == SignalSubscription::NativeDirectWait ||
            subscription.target == SignalSubscription::DesignDirectWait;
        return !direct || signalEdgeMatches(subscription.edge, edges);
      },
      outSequence);
}

extern "C" void obelisk_rt_v1_scheduler_signal(obelisk_rt_context *context,
                                               uint64_t bitOffset,
                                               uint64_t bitWidth,
                                               uint32_t edges) {
  if (!context || bitOffset == UINT64_MAX || bitWidth == 0 || edges == 0 ||
      (edges & ~(OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE |
                 OBELISK_RT_SIGNAL_NEGEDGE)) != 0)
    return;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    uint32_t objectID = 0;
    int64_t offset = 0;
    uint64_t objectWidth = 0;
    if (decodeNativeAutomatic(bitOffset, objectID, offset)) {
      auto found = context->nativeAutomaticStates.find(objectID);
      if (found == context->nativeAutomaticStates.end())
        return;
      objectWidth = found->second.bitWidth;
    } else if (decodeNativeStatic(bitOffset, objectID, offset)) {
      auto found = context->nativeStaticStates.find(objectID);
      if (found == context->nativeStaticStates.end())
        return;
      objectWidth = found->second.bitWidth;
    } else if (!decodeNativeGlobal(bitOffset, offset)) {
      return;
    }
    if (objectWidth != 0 && (offset >= static_cast<__int128>(objectWidth) ||
                             static_cast<__int128>(offset) + bitWidth <= 0))
      return;
    if (!obelisk_rt_publish_signal_occurrence_unlocked(context, bitOffset,
                                                       bitWidth, edges))
      return;
    if (!obelisk_rt_latch_conditional_signal_range_unlocked(context, bitOffset,
                                                            bitWidth, edges))
      return;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  } catch (const std::bad_alloc &) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
}

bool publishNativeSignalTransitionUnlocked(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    const uint8_t *newValue, const uint8_t *newUnknown) {
  uint64_t sequence = 0;
  if (!obelisk_rt_publish_signal_transition_batch_unlocked(
          context, bitOffset, bitWidth, changed, posedge, negedge, 0,
          &sequence))
    return false;
  // Generated native code may have committed later source stores to its
  // private plane before publishing this transition. Advance the canonical
  // plane one publication at a time so observer evaluators see source-order
  // state: prior publications plus this transition, never future stores.
  uint32_t publishedStaticID = 0;
  int64_t publishedOffset = 0;
  bool publishedStatic =
      decodeNativeStatic(bitOffset, publishedStaticID, publishedOffset);
  bool publishedGlobal =
      !publishedStatic && decodeNativeGlobal(bitOffset, publishedOffset);
  const NativeStaticState *publishedState =
      publishedStatic ? findNativeStaticState(context, publishedStaticID)
                      : nullptr;
  for (uint64_t bit = 0; bit != bitWidth; ++bit) {
    if (!byteBit(changed, bit) || bit > static_cast<uint64_t>(INT64_MAX))
      continue;
    int64_t local = 0;
    if (!addHandleOffset(publishedOffset, bit, local) || local < 0)
      continue;
    uint64_t absolute = static_cast<uint64_t>(local);
    if (publishedStatic) {
      if (!publishedState || absolute >= publishedState->bitWidth)
        continue;
      absolute += publishedState->bitOffset;
    } else if (!publishedGlobal) {
      continue;
    }
    if (absolute >= context->stateValue.size() * uint64_t{64} ||
        absolute >= context->stateUnknown.size() * uint64_t{64})
      continue;
    uint64_t mask = uint64_t{1} << (absolute % 64);
    uint64_t &valueLimb = context->stateValue[absolute / 64];
    uint64_t &unknownLimb = context->stateUnknown[absolute / 64];
    valueLimb = byteBit(newValue, bit) ? valueLimb | mask : valueLimb & ~mask;
    unknownLimb = newUnknown && byteBit(newUnknown, bit)
                      ? unknownLimb | mask
                      : unknownLimb & ~mask;
  }
  obelisk_rt_invalidate_signal_snapshots_unlocked(context, bitOffset, bitWidth);
  if (obelisk_rt_has_conditional_signal_waiters(context)) {
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      if (!byteBit(changed, bit) || bit > static_cast<uint64_t>(INT64_MAX))
        continue;
      uint64_t eventHandle =
          nativeHandleOffset(bitOffset, static_cast<int64_t>(bit));
      if (eventHandle == UINT64_MAX)
        continue;
      context->signalValueSnapshots[eventHandle] = {
          sequence, byteBit(newValue, bit),
          newUnknown && byteBit(newUnknown, bit)};
    }
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      if (!byteBit(changed, bit) || bit > static_cast<uint64_t>(INT64_MAX))
        continue;
      uint64_t eventHandle =
          nativeHandleOffset(bitOffset, static_cast<int64_t>(bit));
      if (eventHandle == UINT64_MAX)
        continue;
      uint32_t edges = OBELISK_RT_SIGNAL_CHANGE;
      if (byteBit(posedge, bit))
        edges |= OBELISK_RT_SIGNAL_POSEDGE;
      if (byteBit(negedge, bit))
        edges |= OBELISK_RT_SIGNAL_NEGEDGE;
      if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
              context, eventHandle, edges))
        return false;
    }
  }
  bool previousCanonicalPlane = context->observerForcesCanonicalPlane;
  context->observerForcesCanonicalPlane = true;
  bool observerStatus =
      obelisk_rt_notify_observer_signal_unlocked(context, bitOffset, bitWidth);
  context->observerForcesCanonicalPlane = previousCanonicalPlane;
  if (!observerStatus)
    return false;
  if (++context->schedulerEpoch == 0)
    context->schedulerEpoch = 1;
  return true;
}

extern "C" void obelisk_rt_v1_scheduler_signal_transition(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth,
    const uint8_t *oldValue, const uint8_t *oldUnknown, const uint8_t *newValue,
    const uint8_t *newUnknown) {
  if (!context || bitOffset == UINT64_MAX || bitWidth == 0 ||
      bitWidth > UINT64_MAX - 7 || !oldValue || !newValue)
    return;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    bool packedRange = bitWidth <= 64;
    uint32_t staticID = 0;
    int64_t staticOffset = 0;
    if (packedRange && decodeNativeStatic(bitOffset, staticID, staticOffset)) {
      const NativeStaticState *state = findNativeStaticState(context, staticID);
      packedRange =
          state && staticOffset >= 0 &&
          static_cast<uint64_t>(staticOffset) <= state->bitWidth &&
          bitWidth <= state->bitWidth - static_cast<uint64_t>(staticOffset);
    } else {
      packedRange = false;
    }
    if (packedRange) {
      uint64_t widthMask = packedWidthMask(bitWidth);
      uint64_t oldValueBits =
          loadPackedBytes(oldValue, 0, bitWidth) & widthMask;
      uint64_t oldUnknownBits =
          oldUnknown ? loadPackedBytes(oldUnknown, 0, bitWidth) & widthMask : 0;
      uint64_t newValueBits =
          loadPackedBytes(newValue, 0, bitWidth) & widthMask;
      uint64_t newUnknownBits =
          newUnknown ? loadPackedBytes(newUnknown, 0, bitWidth) & widthMask : 0;
      uint64_t changedBits =
          (oldValueBits ^ newValueBits) | (oldUnknownBits ^ newUnknownBits);
      if (changedBits == 0)
        return;
      uint64_t oldZero = ~oldUnknownBits & ~oldValueBits & widthMask;
      uint64_t oldOne = ~oldUnknownBits & oldValueBits & widthMask;
      uint64_t newZero = ~newUnknownBits & ~newValueBits & widthMask;
      uint64_t newOne = ~newUnknownBits & newValueBits & widthMask;
      uint64_t posedgeBits =
          ((oldZero & ~newZero) | (oldUnknownBits & newOne)) & widthMask;
      uint64_t negedgeBits =
          ((oldOne & ~newOne) | (oldUnknownBits & newZero)) & widthMask;
      (void)publishNativeSignalTransitionUnlocked(
          context, bitOffset, bitWidth,
          reinterpret_cast<const uint8_t *>(&changedBits),
          reinterpret_cast<const uint8_t *>(&posedgeBits),
          reinterpret_cast<const uint8_t *>(&negedgeBits), newValue,
          newUnknown);
      return;
    }
    PackedSignalTransitionBuffer transitions(bitWidth);
    bool changed = false;
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      bool oldValueBit = byteBit(oldValue, bit);
      bool oldUnknownBit = oldUnknown && byteBit(oldUnknown, bit);
      bool newValueBit = byteBit(newValue, bit);
      bool newUnknownBit = newUnknown && byteBit(newUnknown, bit);
      uint32_t edges = transitionEdges(oldValueBit, oldUnknownBit, newValueBit,
                                       newUnknownBit);
      if (edges == 0)
        continue;
      if (bit > static_cast<uint64_t>(INT64_MAX))
        continue;
      uint64_t eventHandle =
          nativeHandleOffset(bitOffset, static_cast<int64_t>(bit));
      uint32_t automaticID = 0;
      uint32_t staticID = 0;
      int64_t eventOffset = 0;
      bool automatic =
          decodeNativeAutomatic(eventHandle, automaticID, eventOffset);
      bool boundedStatic =
          !automatic && decodeNativeStatic(eventHandle, staticID, eventOffset);
      bool inRange = eventOffset >= 0;
      if (automatic) {
        auto found = context->nativeAutomaticStates.find(automaticID);
        inRange &= found != context->nativeAutomaticStates.end() &&
                   static_cast<uint64_t>(eventOffset) < found->second.bitWidth;
      } else if (boundedStatic) {
        auto found = context->nativeStaticStates.find(staticID);
        inRange &= found != context->nativeStaticStates.end() &&
                   static_cast<uint64_t>(eventOffset) < found->second.bitWidth;
      } else {
        inRange &= decodeNativeGlobal(eventHandle, eventOffset);
      }
      if (!inRange)
        continue;
      transitions.record(bit, edges);
      changed = true;
    }
    if (changed)
      (void)publishNativeSignalTransitionUnlocked(
          context, bitOffset, bitWidth, transitions.changed(),
          transitions.posedge(), transitions.negedge(), newValue, newUnknown);
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
  }
}

extern "C" void obelisk_rt_v1_scheduler_static_transition(
    obelisk_rt_context *context, uint32_t staticState, uint64_t lowBit,
    uint64_t bitWidth, uint64_t oldValue, uint64_t oldUnknown,
    uint64_t newValue, uint64_t newUnknown) {
  if (!context || staticState == 0 || bitWidth == 0 || bitWidth > 64)
    return;
  uint64_t widthMask = packedWidthMask(bitWidth);
  oldValue &= widthMask;
  oldUnknown &= widthMask;
  newValue &= widthMask;
  newUnknown &= widthMask;
  uint64_t changed = (oldValue ^ newValue) | (oldUnknown ^ newUnknown);
  if (changed == 0)
    return;

  // The generated leaf is valid only while the installed exact-fanout plan is
  // the active clean AOT kernel. A mid-slot handover remains correct by
  // entering the ordinary transition path with the same scalar planes.
  if (activeNativeAOTContext != context || !canUseStaticAOTFanout(context)) {
    uint64_t handle = obelisk_rt_stable_handle_encode(
        OBELISK_RT_STABLE_HANDLE_STATIC, staticState,
        static_cast<int64_t>(lowBit));
    obelisk_rt_v1_scheduler_signal_transition(
        context, handle, bitWidth, reinterpret_cast<const uint8_t *>(&oldValue),
        reinterpret_cast<const uint8_t *>(&oldUnknown),
        reinterpret_cast<const uint8_t *>(&newValue),
        reinterpret_cast<const uint8_t *>(&newUnknown));
    return;
  }
  const NativeStaticState *state = findNativeStaticState(context, staticState);
  if (!state || lowBit > state->bitWidth ||
      bitWidth > state->bitWidth - lowBit) {
    context->schedulerStatus = OBELISK_RT_LAYOUT_MISMATCH;
    return;
  }
  uint8_t edgeKinds = 0;
  if (staticState < context->nativeScheduleStaticStateFanoutEdges.size()) {
    edgeKinds = context->nativeScheduleStaticStateFanoutEdges[staticState];
    if (edgeKinds == 0) {
      if (++context->schedulerEpoch == 0)
        context->schedulerEpoch = 1;
      return;
    }
  }
  uint8_t posedgeKinds =
      (uint8_t{1} << OBELISK_RT_WAIT_EDGE_POSEDGE) |
      (uint8_t{1} << OBELISK_RT_WAIT_EDGE_BOTH);
  uint8_t negedgeKinds =
      (uint8_t{1} << OBELISK_RT_WAIT_EDGE_NEGEDGE) |
      (uint8_t{1} << OBELISK_RT_WAIT_EDGE_BOTH);
  uint64_t posedge = 0;
  uint64_t negedge = 0;
  if ((edgeKinds & (posedgeKinds | negedgeKinds)) != 0) {
    uint64_t oldZero = ~oldUnknown & ~oldValue & widthMask;
    uint64_t oldOne = ~oldUnknown & oldValue & widthMask;
    uint64_t newZero = ~newUnknown & ~newValue & widthMask;
    uint64_t newOne = ~newUnknown & newValue & widthMask;
    if ((edgeKinds & posedgeKinds) != 0)
      posedge =
          ((oldZero & ~newZero) | (oldUnknown & newOne)) & widthMask;
    if ((edgeKinds & negedgeKinds) != 0)
      negedge =
          ((oldOne & ~newOne) | (oldUnknown & newZero)) & widthMask;
  }
  uint64_t observedEdges = 0;
  if ((edgeKinds & (uint8_t{1} << OBELISK_RT_WAIT_EDGE_CHANGE)) != 0)
    observedEdges |= changed;
  if ((edgeKinds & posedgeKinds) != 0)
    observedEdges |= posedge;
  if ((edgeKinds & negedgeKinds) != 0)
    observedEdges |= negedge;
  if (observedEdges == 0) {
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return;
  }

  const obelisk_rt_static_fanout_entry *begin =
      context->nativeScheduleFanoutEntries;
  const obelisk_rt_static_fanout_entry *end =
      begin + context->nativeScheduleFanoutEntryCount;
  auto first = end;
  auto last = end;
  if (staticState < context->nativeScheduleFanoutRanges.size()) {
    auto [firstIndex, lastIndex] =
        context->nativeScheduleFanoutRanges[staticState];
    if (firstIndex <= lastIndex &&
        lastIndex <= context->nativeScheduleFanoutEntryCount) {
      first = begin + firstIndex;
      last = begin + lastIndex;
    }
  } else {
    first = std::lower_bound(
        begin, end, staticState,
        [](const obelisk_rt_static_fanout_entry &entry, uint32_t id) {
          return entry.static_state < id;
        });
    last = first;
    while (last != end && last->static_state == staticState)
      ++last;
  }
  bool published = false;
  uint64_t publishedEnd = lowBit + bitWidth;
  for (auto entry = first; entry != last; ++entry) {
    ++context->signalDiagnostics.aotFanoutEntries;
    uint64_t overlapLow = std::max(lowBit, entry->low_bit);
    uint64_t overlapHigh =
        std::min(publishedEnd, entry->low_bit + entry->bit_width);
    if (overlapLow >= overlapHigh)
      continue;
    uint64_t localLow = overlapLow - lowBit;
    uint64_t localWidth = overlapHigh - overlapLow;
    uint64_t overlapMask = packedWidthMask(localWidth)
                           << static_cast<unsigned>(localLow);
    uint64_t observed = changed;
    switch (entry->edge) {
    case OBELISK_RT_WAIT_EDGE_POSEDGE:
      observed = posedge;
      break;
    case OBELISK_RT_WAIT_EDGE_NEGEDGE:
      observed = negedge;
      break;
    case OBELISK_RT_WAIT_EDGE_BOTH:
      observed = posedge | negedge;
      break;
    default:
      break;
    }
    if ((observed & overlapMask) == 0)
      continue;

    uint32_t slot = entry->actor_slot;
    if (slot >= context->nativeScheduleActors.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    obelisk_rt_process_instance_v1 *actor = context->nativeScheduleActors[slot];
    if (!actor)
      continue;
    size_t index = context->nativeScheduleActorIndices[slot];
    if (index >= context->scheduledProcesses.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    ScheduledProcess &scheduled = context->scheduledProcesses[index];
    bool activeSelf =
        context->activeLogicalProcessToken ==
        (kNativeLogicalProcessTag | scheduled.token);
    if ((actor->continuation != entry->continuation && !activeSelf) ||
        scheduled.instance != actor || !scheduled.started ||
        (scheduled.signalTriggered && !activeSelf) ||
        (scheduled.suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
         scheduled.suspendKind != OBELISK_RT_SUSPEND_EDGE))
      continue;
    if (!published) {
      if (context->nextSchedulerSequence == 0) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return;
      }
      ++context->nextSchedulerSequence;
      if (context->signalDiagnosticsEnabled)
        ++context->signalDiagnostics.publications;
      published = true;
    }
    scheduled.signalTriggered = true;
    uint32_t node = entry->compute_node;
    if (node >= context->nativeScheduleNodes.size() ||
        node / 64 >= context->nativeScheduleReadyNodes.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
      return;
    }
    context->nativeScheduleReadyNodes[node / 64] |= uint64_t{1} << (node % 64);
    context->nativeScheduleMinimumActivatedNode =
        std::min(context->nativeScheduleMinimumActivatedNode, node);
  }
  if (++context->schedulerEpoch == 0)
    context->schedulerEpoch = 1;
}

extern "C" void obelisk_rt_v1_scheduler_activate_static_nodes(
    obelisk_rt_context *context, const uint64_t *nodeWords,
    uint32_t wordCount) {
  if (!context || !nodeWords)
    return;
  if (!context->nativeSchedulePlan ||
      wordCount != context->nativeScheduleReadyNodes.size()) {
    context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
    return;
  }
  bool hasNodes = false;
  for (uint32_t word = 0; word != wordCount; ++word)
    hasNodes |= nodeWords[word] != 0;
  if (!hasNodes)
    return;
  if ((activeNativeAOTContext != context &&
       !canUseIndexedExternalAOTFanout(context)) ||
      (!canUseStaticAOTFanout(context) &&
       !canUseIndexedExternalAOTFanout(context)) ||
      (context->nativeSchedulePlan->flags &
       OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP) == 0 ||
      context->nativeScheduleReadyNodes.empty()) {
    context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
    return;
  }

  bool published = false;
  for (uint32_t word = 0; word != wordCount; ++word) {
    uint64_t nodes = nodeWords[word];
    while (nodes != 0) {
      uint32_t bit = static_cast<uint32_t>(__builtin_ctzll(nodes));
      uint32_t node = word * 64 + bit;
      nodes &= nodes - 1;
      if (node >= context->nativeScheduleNodes.size()) {
        context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
        return;
      }
      const obelisk_rt_native_schedule_node &entry =
          context->nativeScheduleNodes[node];
      if (entry.actor_slot >= context->nativeScheduleActors.size()) {
        context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
        return;
      }
      obelisk_rt_process_instance_v1 *actor =
          context->nativeScheduleActors[entry.actor_slot];
      if (!actor)
        continue;
      size_t index = context->nativeScheduleActorIndices[entry.actor_slot];
      if (index >= context->scheduledProcesses.size()) {
        context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
        return;
      }
      ScheduledProcess &scheduled = context->scheduledProcesses[index];
      bool activeSelf =
          context->activeLogicalProcessToken ==
          (kNativeLogicalProcessTag | scheduled.token);
      if ((actor->continuation != entry.continuation && !activeSelf) ||
          scheduled.instance != actor || !scheduled.started ||
          (scheduled.signalTriggered && !activeSelf) ||
          (scheduled.suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
           scheduled.suspendKind != OBELISK_RT_SUSPEND_EDGE))
        continue;
      scheduled.signalTriggered = true;
      context->nativeScheduleReadyNodes[word] |= uint64_t{1} << bit;
      context->nativeScheduleMinimumActivatedNode =
          std::min(context->nativeScheduleMinimumActivatedNode, node);
      ++context->signalDiagnostics.aotFanoutEntries;
      published = true;
    }
  }
  if (!published)
    return;
  if (context->nextSchedulerSequence == 0) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return;
  }
  ++context->nextSchedulerSequence;
  if (context->signalDiagnosticsEnabled)
    ++context->signalDiagnostics.publications;
  if (++context->schedulerEpoch == 0)
    context->schedulerEpoch = 1;
}

extern "C" void obelisk_rt_v1_scheduler_real_transition(
    obelisk_rt_context *context, uint64_t bitOffset, uint32_t bitWidth,
    const void *oldValue, const void *newValue) {
  if (!context || bitOffset == UINT64_MAX ||
      (bitWidth != 32 && bitWidth != 64) || !oldValue || !newValue)
    return;
  bool changed = false;
  if (bitWidth == 32) {
    float oldReal = 0.0f;
    float newReal = 0.0f;
    std::memcpy(&oldReal, oldValue, sizeof(oldReal));
    std::memcpy(&newReal, newValue, sizeof(newReal));
    changed = oldReal != newReal || std::isnan(newReal);
  } else {
    double oldReal = 0.0;
    double newReal = 0.0;
    std::memcpy(&oldReal, oldValue, sizeof(oldReal));
    std::memcpy(&newReal, newValue, sizeof(newReal));
    changed = oldReal != newReal || std::isnan(newReal);
  }
  if (!changed)
    return;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    if (!obelisk_rt_publish_signal_occurrence_unlocked(
            context, bitOffset, bitWidth, OBELISK_RT_SIGNAL_CHANGE))
      return;
    obelisk_rt_invalidate_signal_snapshots_unlocked(context, bitOffset,
                                                    bitWidth);
    if (!obelisk_rt_latch_conditional_signal_range_unlocked(
            context, bitOffset, bitWidth, OBELISK_RT_SIGNAL_CHANGE))
      return;
    if (!obelisk_rt_notify_observer_signal_unlocked(context, bitOffset,
                                                    bitWidth))
      return;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
  }
}

extern "C" void obelisk_rt_v1_scheduler_event(obelisk_rt_context *context,
                                              uint64_t stableID,
                                              uint32_t nonblocking) {
  obelisk_rt_v1_scheduler_event_after(context, stableID, nonblocking, 0);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_event_create(
    obelisk_rt_context *context, uint64_t *outStableID) {
  if (!context || !outStableID)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outStableID = UINT64_MAX;
  try {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return context->schedulerStatus;
    if (context->nextDynamicEventID == 0 ||
        context->nextDynamicEventID >=
            OBELISK_RT_STABLE_HANDLE_DYNAMIC_EVENT_TAG)
      return OBELISK_RT_OUT_OF_RESOURCES;
    *outStableID = OBELISK_RT_STABLE_HANDLE_DYNAMIC_EVENT_TAG |
                   context->nextDynamicEventID++;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" void obelisk_rt_v1_scheduler_event_after(obelisk_rt_context *context,
                                                    uint64_t stableID,
                                                    uint32_t nonblocking,
                                                    uint64_t delay) {
  if (!context || nonblocking > 1 || (!nonblocking && delay != 0)) {
    if (context)
      obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return;
  }
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    uint32_t retainedAutomaticID = 0;
    int64_t automaticOffset = 0;
    if (decodeNativeAutomatic(stableID, retainedAutomaticID, automaticOffset)) {
      auto found = context->nativeAutomaticStates.find(retainedAutomaticID);
      if (found == context->nativeAutomaticStates.end()) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return;
      }
      if (nonblocking && found->second.referenceCount == UINT64_MAX) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return;
      }
    }
    if (nonblocking) {
      if (context->nextSchedulerSequence == 0) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return;
      }
      uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                             ? UINT64_MAX
                             : context->schedulerTime + delay;
      uint32_t execRegion = obelisk_rt_commit_region(
          context->activeHomeRegion == UINT32_MAX
              ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
              : context->activeHomeRegion);
      if (execRegion == UINT32_MAX) {
        context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
        return;
      }
      context->scheduledDesignEvents.push_back({context->nextSchedulerSequence,
                                                dueTime, execRegion, stableID,
                                                retainedAutomaticID});
      if (retainedAutomaticID != 0)
        ++context->nativeAutomaticStates.find(retainedAutomaticID)
              ->second.referenceCount;
      ++context->nextSchedulerSequence;
      return;
    }
    EventState &event = context->events[stableID];
    if (++event.generation == 0)
      event.generation = 1;
    event.lastTriggeredTime = context->schedulerTime;
    if (!obelisk_rt_notify_observer_event_unlocked(context, stableID))
      return;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  } catch (const std::bad_alloc &) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" uint32_t
obelisk_rt_v1_scheduler_event_triggered(obelisk_rt_context *context,
                                        uint64_t stableID) {
  if (!context)
    return 0;
  try {
    ContextMutexLock lock(context);
    auto found = context->events.find(stableID);
    return found != context->events.end() && found->second.generation != 0 &&
           found->second.lastTriggeredTime == context->schedulerTime;
  } catch (...) {
    return 0;
  }
}
