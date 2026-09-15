//===- ProcessSignals.cpp - Signal subscription indexing ----------------===//

#include "ProcessSignals.h"
#include "ProcessContext.h"
#include "ProcessObservers.h"
#include "ProcessShared.h"
#include "ProcessValidation.h"
#include "RuntimeInternal.h"
#include "SignalSemantics.h"
#include "obelisk/Runtime/StableHandle.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

using namespace obelisk::process;
using namespace obelisk::runtime;

#if defined(__clang__) || defined(__GNUC__)
__attribute__((weak))
#endif
bool obelisk_rt_expand_recursive_watch_group(
    obelisk_rt_context *context, uint64_t token,
    RecursiveWatchGroupVisit visit, void *environment);
#if defined(__clang__) || defined(__GNUC__)
__attribute__((weak))
#endif
bool obelisk_rt_expand_class_watch_group(obelisk_rt_context *context,
                                         uint64_t token,
                                         RecursiveWatchGroupVisit visit,
                                         void *environment);

namespace {

bool appendSignalSubscriptionUnlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    uint32_t edge, SignalSubscription::Target target, uint64_t waiterToken,
    bool suppressActiveSelf, SignalWaitLatch *latch,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions) {
  uint32_t kind = 0;
  uint32_t objectID = 0;
  int64_t firstPage = 0;
  int64_t lastPage = 0;
  if (!signalSubscriptionBucketRange(stableID, bitWidth, kind, objectID,
                                     firstPage, lastPage)) {
    context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
    return false;
  }
  __int128 pageCount = static_cast<__int128>(lastPage) - firstPage + 1;
  if (pageCount <= 0) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  bool wide = pageCount > kMaximumIndexedSignalPages;

  auto subscription = std::make_unique<SignalSubscription>();
  subscription->stableID = stableID;
  subscription->bitWidth = bitWidth;
  subscription->edge = edge;
  subscription->target = target;
  subscription->waiterToken = waiterToken;
  subscription->suppressActiveSelf = suppressActiveSelf;
  subscription->latch = latch;
  subscription->bucketSlots.reserve(wide ? 1 : static_cast<size_t>(pageCount));
  subscriptions.push_back(std::move(subscription));
  SignalSubscription &stored = *subscriptions.back();
  if (target == SignalSubscription::NativeDirectWait &&
      context->nativeSchedulePlan) {
    ScheduledProcess *process = findScheduledProcess(context, waiterToken);
    stored.outsideStaticFanout =
        process && process->suspendedAOTActorSlot != UINT32_MAX;
  }
  if (target == SignalSubscription::NativeComputedWait ||
      stored.outsideStaticFanout)
    ++context->nativeDynamicSignalSubscriptions;
  if (stored.outsideStaticFanout && context->nativeSchedulePlan &&
      context->nativeSchedulePlan->specialization_fast)
    *context->nativeSchedulePlan->specialization_fast = 0;
  if (context->signalDiagnosticsEnabled) {
    ++context->signalDiagnostics.subscriptionsCurrent;
    context->signalDiagnostics.subscriptionsHighWater =
        std::max(context->signalDiagnostics.subscriptionsHighWater,
                 context->signalDiagnostics.subscriptionsCurrent);
  }
  int64_t indexedFirst = wide ? kWideSignalSubscriptionPage : firstPage;
  int64_t indexedLast = wide ? kWideSignalSubscriptionPage : lastPage;
  for (int64_t page = indexedFirst;; ++page) {
    SignalSubscriptionBucketKey key{kind, objectID, page};
    bool bucketEntryAppended = false;
    size_t slotIndex = stored.bucketSlots.size();
    OBELISK_RT_TRY {
      auto &bucket = context->signalSubscriptionBuckets[key];
      bucket.push_back({&stored, slotIndex});
      bucketEntryAppended = true;
      stored.bucketSlots.push_back({key, bucket.size() - 1});
    }
    OBELISK_RT_CATCH_ALL {
      auto found = context->signalSubscriptionBuckets.find(key);
      if (found != context->signalSubscriptionBuckets.end()) {
        if (bucketEntryAppended && !found->second.empty() &&
            found->second.back().subscription == &stored &&
            found->second.back().slotIndex == slotIndex)
          found->second.pop_back();
        if (found->second.empty())
          context->signalSubscriptionBuckets.erase(found);
      }
      OBELISK_RT_RETHROW;
    }
    if (page == indexedLast)
      break;
  }
  return true;
}

bool appendClockOccurrenceSubscriptionUnlocked(
    obelisk_rt_context *context, uint64_t logicalToken, uint64_t stableID,
    uint64_t bitWidth, uint32_t edge, uint64_t waiterToken, bool native,
    uint8_t occurrenceBit) {
  ClockOccurrenceFeatureState &feature = *context->clockOccurrences;
  uint32_t kind = 0;
  uint32_t objectID = 0;
  int64_t firstPage = 0;
  int64_t lastPage = 0;
  if (!signalSubscriptionBucketRange(stableID, bitWidth, kind, objectID,
                                     firstPage, lastPage)) {
    context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
    return false;
  }
  __int128 pageCount = static_cast<__int128>(lastPage) - firstPage + 1;
  if (pageCount <= 0) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  bool wide = pageCount > kMaximumIndexedSignalPages;

  auto subscription = std::make_unique<ClockOccurrenceSubscription>();
  subscription->stableID = stableID;
  subscription->bitWidth = bitWidth;
  subscription->edge = edge;
  subscription->waiterToken = waiterToken;
  subscription->occurrenceBit = occurrenceBit;
  subscription->native = native;
  bool customEdge = (edge & ~OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) ==
                        OBELISK_RT_WAIT_EDGE_TRANSITION_MASK &&
                    (edge & OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) != 0;
  if (customEdge) {
    if (bitWidth > UINT64_MAX - 7 ||
        (bitWidth + 7) / 8 > std::numeric_limits<size_t>::max()) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return false;
    }
    size_t bytes = static_cast<size_t>((bitWidth + 7) / 8);
    subscription->previousValue.assign(bytes, 0);
    subscription->previousUnknown.assign(bytes, 0);
    subscription->previousInitialized.assign(bytes, 0);
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      bool value = false;
      bool unknown = false;
      if (!obelisk_rt_read_signal_bit_unlocked(context, stableID, bit, value,
                                               unknown))
        // Native timing coordinators can register before the root initializer
        // has imported its generated plane. Defer only that feature-local
        // snapshot to the first real publication, before its state commit.
        continue;
      setByteBit(subscription->previousValue.data(), bit, value);
      setByteBit(subscription->previousUnknown.data(), bit, unknown);
      setByteBit(subscription->previousInitialized.data(), bit, true);
    }
  }
  subscription->bucketSlots.reserve(wide ? 1 : static_cast<size_t>(pageCount));
  auto [ownedEntry, freshOwner] =
      feature.subscriptions.try_emplace(logicalToken);
  OBELISK_RT_TRY { ownedEntry->second.push_back(std::move(subscription)); }
  OBELISK_RT_CATCH_ALL {
    if (freshOwner && ownedEntry->second.empty())
      feature.subscriptions.erase(ownedEntry);
    OBELISK_RT_RETHROW;
  }
  auto &owned = ownedEntry->second;
  ClockOccurrenceSubscription &stored = *owned.back();
  // A custom native descriptor is not present in the frozen AOT fanout.
  // Reuse its existing dynamic-subscription guard so ordinary publications
  // gain no feature lookup or branch; unregister and failed registration
  // roll this pay-for-play marker back with the subscription.
  if (customEdge && native)
    ++context->nativeDynamicSignalSubscriptions;
  if (context->signalDiagnosticsEnabled) {
    ++context->signalDiagnostics.subscriptionsCurrent;
    context->signalDiagnostics.subscriptionsHighWater =
        std::max(context->signalDiagnostics.subscriptionsHighWater,
                 context->signalDiagnostics.subscriptionsCurrent);
  }
  auto rollback = [&] {
    for (const SignalSubscriptionBucketSlot &slot : stored.bucketSlots) {
      auto bucket = feature.subscriptionBuckets.find(slot.key);
      if (bucket == feature.subscriptionBuckets.end() ||
          slot.bucketIndex >= bucket->second.size())
        continue;
      ClockOccurrenceBucketEntry &entry = bucket->second[slot.bucketIndex];
      if (entry.subscription != &stored)
        continue;
      ClockOccurrenceBucketEntry moved = bucket->second.back();
      entry = moved;
      bucket->second.pop_back();
      if (moved.subscription && moved.subscription != &stored &&
          moved.slotIndex < moved.subscription->bucketSlots.size())
        moved.subscription->bucketSlots[moved.slotIndex].bucketIndex =
            slot.bucketIndex;
      if (bucket->second.empty())
        feature.subscriptionBuckets.erase(bucket);
    }
    if (context->signalDiagnosticsEnabled &&
        context->signalDiagnostics.subscriptionsCurrent != 0)
      --context->signalDiagnostics.subscriptionsCurrent;
    if (customEdge && native && context->nativeDynamicSignalSubscriptions != 0)
      --context->nativeDynamicSignalSubscriptions;
    owned.pop_back();
    if (owned.empty())
      feature.subscriptions.erase(logicalToken);
  };
  int64_t indexedFirst = wide ? kWideSignalSubscriptionPage : firstPage;
  int64_t indexedLast = wide ? kWideSignalSubscriptionPage : lastPage;
  for (int64_t page = indexedFirst;; ++page) {
    SignalSubscriptionBucketKey key{kind, objectID, page};
    bool bucketEntryAppended = false;
    size_t slotIndex = stored.bucketSlots.size();
    OBELISK_RT_TRY {
      auto &bucket = feature.subscriptionBuckets[key];
      bucket.push_back({&stored, slotIndex});
      bucketEntryAppended = true;
      stored.bucketSlots.push_back({key, bucket.size() - 1});
    }
    OBELISK_RT_CATCH_ALL {
      auto found = feature.subscriptionBuckets.find(key);
      if (found != feature.subscriptionBuckets.end()) {
        if (bucketEntryAppended && !found->second.empty() &&
            found->second.back().subscription == &stored &&
            found->second.back().slotIndex == slotIndex)
          found->second.pop_back();
        if (found->second.empty())
          feature.subscriptionBuckets.erase(found);
      }
      rollback();
      OBELISK_RT_RETHROW;
    }
    if (page == indexedLast)
      break;
  }
  return true;
}

bool validClockingOutputEdge(uint32_t edge) {
  return edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
         edge == OBELISK_RT_WAIT_EDGE_POSEDGE ||
         edge == OBELISK_RT_WAIT_EDGE_NEGEDGE;
}

void eraseClockOccurrenceSubscriptionsUnlocked(obelisk_rt_context *context,
                                               uint64_t logicalToken) {
  if (!context->clockOccurrences)
    return;
  ClockOccurrenceFeatureState &feature = *context->clockOccurrences;
  auto owned = feature.subscriptions.find(logicalToken);
  if (owned == feature.subscriptions.end())
    return;
  for (const auto &pointer : owned->second) {
    if (!pointer)
      continue;
    ClockOccurrenceSubscription &subscription = *pointer;
    bool customEdge =
        (subscription.edge & ~OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) ==
            OBELISK_RT_WAIT_EDGE_TRANSITION_MASK &&
        (subscription.edge & OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) != 0;
    for (const SignalSubscriptionBucketSlot &slot : subscription.bucketSlots) {
      auto bucket = feature.subscriptionBuckets.find(slot.key);
      if (bucket == feature.subscriptionBuckets.end())
        continue;
      if (slot.bucketIndex < bucket->second.size()) {
        ClockOccurrenceBucketEntry &entry = bucket->second[slot.bucketIndex];
        if (entry.subscription == &subscription) {
          ClockOccurrenceBucketEntry moved = bucket->second.back();
          entry = moved;
          bucket->second.pop_back();
          if (moved.subscription && moved.subscription != &subscription &&
              moved.slotIndex < moved.subscription->bucketSlots.size())
            moved.subscription->bucketSlots[moved.slotIndex].bucketIndex =
                slot.bucketIndex;
        }
      }
      if (bucket->second.empty())
        feature.subscriptionBuckets.erase(bucket);
    }
    if (context->signalDiagnosticsEnabled &&
        context->signalDiagnostics.subscriptionsCurrent != 0)
      --context->signalDiagnostics.subscriptionsCurrent;
    if (customEdge && subscription.native &&
        context->nativeDynamicSignalSubscriptions != 0)
      --context->nativeDynamicSignalSubscriptions;
  }
  feature.subscriptions.erase(owned);
}

void eraseReplaceableEventsForOwnerUnlocked(obelisk_rt_context *context,
                                            uint64_t logicalToken) {
  if (!context || !context->clockOccurrences ||
      !context->clockOccurrences->replaceableEvents)
    return;
  ClockOccurrenceFeatureState &clock = *context->clockOccurrences;
  ReplaceableEventFeatureState &feature = *clock.replaceableEvents;
  auto owned = feature.ownedTimers.find(logicalToken);
  if (owned == feature.ownedTimers.end())
    return;
  for (uint64_t stableID : owned->second) {
    auto pending = feature.pending.find(stableID);
    if (pending == feature.pending.end() ||
        pending->second.ownerToken != logicalToken) {
      context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
      continue;
    }
    if (pending->second.scheduled)
      feature.calendar.erase(pending->second.event);
    feature.pending.erase(pending);
  }
  feature.ownedTimers.erase(owned);
  if (feature.pending.empty() && feature.calendar.empty() &&
      feature.ownedTimers.empty())
    clock.replaceableEvents.reset();
}

bool appendManagedSubscriptionUnlocked(
    obelisk_rt_context *context, uint64_t token,
    SignalSubscription::Target target, uint64_t waiterToken,
    bool suppressActiveSelf, SignalWaitLatch *latch,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions) {
  if (target != SignalSubscription::NativeManagedWait &&
      target != SignalSubscription::DesignManagedWait) {
    context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
    return false;
  }
  // A null managed object has no mutation identity. Its owning storage or
  // outer managed-field dependency remains in the same wait and will rebuild
  // this token after the object becomes non-null.
  if (token == 0)
    return true;
  if ((token & (kRecursiveWatchGroupBit | kClassWatchGroupBit)) != 0) {
    auto expand = (token & kRecursiveWatchGroupBit) != 0
                      ? obelisk_rt_expand_recursive_watch_group
                      : obelisk_rt_expand_class_watch_group;
    if (!expand) {
      context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
      return false;
    }
    struct Environment {
      obelisk_rt_context *context;
      SignalSubscription::Target target;
      uint64_t waiterToken;
      bool suppressActiveSelf;
      SignalWaitLatch *latch;
      std::vector<std::unique_ptr<SignalSubscription>> *subscriptions;
    } environment{context, target,        waiterToken, suppressActiveSelf,
                  latch,   &subscriptions};
    auto append = [](void *opaque, uint64_t member) {
      auto &environment = *static_cast<Environment *>(opaque);
      return appendManagedSubscriptionUnlocked(
          environment.context, member, environment.target,
          environment.waiterToken, environment.suppressActiveSelf,
          environment.latch, *environment.subscriptions);
    };
    if (!expand(context, token, append, &environment)) {
      if (context->schedulerStatus == OBELISK_RT_OK)
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
      return false;
    }
    return true;
  }
  auto subscription = std::make_unique<SignalSubscription>();
  subscription->stableID = token;
  subscription->bitWidth = OBELISK_RT_WAIT_WIDTH_MANAGED;
  subscription->edge = OBELISK_RT_WAIT_EDGE_CHANGE;
  subscription->target = target;
  subscription->waiterToken = waiterToken;
  subscription->suppressActiveSelf = suppressActiveSelf;
  subscription->latch = latch;
  subscriptions.push_back(std::move(subscription));
  SignalSubscription *stored = subscriptions.back().get();
  OBELISK_RT_TRY { context->managedWatchWaiters[token].insert(stored); }
  OBELISK_RT_CATCH_ALL {
    subscriptions.pop_back();
    OBELISK_RT_RETHROW;
  }
  if (context->nativeSchedulePlan &&
      context->nativeSchedulePlan->specialization_fast)
    *context->nativeSchedulePlan->specialization_fast = 0;
  context->nativeScheduleGuardedFanoutActive = false;
  if (context->signalDiagnosticsEnabled) {
    ++context->signalDiagnostics.subscriptionsCurrent;
    context->signalDiagnostics.subscriptionsHighWater =
        std::max(context->signalDiagnostics.subscriptionsHighWater,
                 context->signalDiagnostics.subscriptionsCurrent);
  }
  return true;
}

} // namespace

extern "C" obelisk_rt_status
obelisk_rt_v1_clocking_output_track(obelisk_rt_context *context,
                                    uint64_t stableID, uint64_t bitWidth,
                                    uint32_t edge) {
  if (!context || stableID == UINT64_MAX || bitWidth == 0 ||
      !validClockingOutputEdge(edge))
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return context->schedulerStatus;
    if (!context->clockOccurrences)
      context->clockOccurrences =
          std::make_unique<ClockOccurrenceFeatureState>();
    ClockOccurrenceFeatureState &feature = *context->clockOccurrences;
    ClockingOutputOccurrenceKey key{stableID, bitWidth, edge};
    if (feature.clockingOutputs.find(key) != feature.clockingOutputs.end())
      return OBELISK_RT_OK;
    auto [inserted, fresh] = feature.clockingOutputs.try_emplace(key, nullptr);
    if (!fresh)
      return OBELISK_RT_OK;
    OBELISK_RT_TRY {
      if (!appendClockOccurrenceSubscriptionUnlocked(
              context, /*logicalToken=*/0, stableID, bitWidth, edge,
              /*waiterToken=*/0, /*native=*/false, /*occurrenceBit=*/0)) {
        feature.clockingOutputs.erase(inserted);
        return context->schedulerStatus == OBELISK_RT_OK
                   ? OBELISK_RT_INVALID_ARGUMENT
                   : context->schedulerStatus;
      }
      inserted->second = feature.subscriptions[0].back().get();
    }
    OBELISK_RT_CATCH_ALL {
      feature.clockingOutputs.erase(key);
      OBELISK_RT_RETHROW;
    }
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" uint32_t
obelisk_rt_v1_clocking_output_current(obelisk_rt_context *context,
                                      uint64_t stableID, uint64_t bitWidth,
                                      uint32_t edge) {
  if (!context || stableID == UINT64_MAX || bitWidth == 0 ||
      !validClockingOutputEdge(edge))
    return 0;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (!context->clockOccurrences)
      return 0;
    ClockingOutputOccurrenceKey key{stableID, bitWidth, edge};
    auto found = context->clockOccurrences->clockingOutputs.find(key);
    return found != context->clockOccurrences->clockingOutputs.end() &&
           found->second && found->second->clockingOutputSeen &&
           found->second->lastClockingOutputTime == context->schedulerTime;
  }
  OBELISK_RT_CATCH_ALL { return 0; }
}

bool signalSubscriptionBucketRange(uint64_t stableID, uint64_t bitWidth,
                                   uint32_t &kind, uint32_t &id,
                                   int64_t &firstPage, int64_t &lastPage) {
  if (bitWidth == 0)
    return false;
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(stableID, &decoded))
    return false;
  auto page = [](__int128 bit) {
    __int128 result = bit / kSignalSubscriptionPageBits;
    if (bit < 0 && bit % kSignalSubscriptionPageBits != 0)
      --result;
    return result;
  };
  __int128 first = page(decoded.offset);
  __int128 last = page(static_cast<__int128>(decoded.offset) + bitWidth - 1);
  if (first < INT64_MIN || first > INT64_MAX || last < INT64_MIN ||
      last > INT64_MAX)
    return false;
  kind = decoded.kind;
  id = decoded.id;
  firstPage = static_cast<int64_t>(first);
  lastPage = static_cast<int64_t>(last);
  return true;
}

namespace {

bool isStaticCoverageSignal(obelisk_rt_context *context, uint64_t stableID,
                            uint64_t width) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!context || width == 0 ||
      !obelisk_rt_stable_handle_decode(stableID, &decoded) ||
      decoded.offset < 0 ||
      (decoded.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL &&
       decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC &&
       decoded.kind != OBELISK_RT_STABLE_HANDLE_AUTOMATIC))
    return false;
  uint64_t offset = static_cast<uint64_t>(decoded.offset);
  if (decoded.kind == OBELISK_RT_STABLE_HANDLE_AUTOMATIC) {
    auto state = context->nativeAutomaticStates.find(decoded.id);
    return state != context->nativeAutomaticStates.end() &&
           offset <= state->second.bitWidth &&
           width <= state->second.bitWidth - offset;
  }
  if (decoded.kind == OBELISK_RT_STABLE_HANDLE_GLOBAL)
    return context->execution &&
           offset <= context->execution->state_bit_count &&
           width <= context->execution->state_bit_count - offset;
  auto state = context->nativeStaticStates.find(decoded.id);
  return state != context->nativeStaticStates.end() &&
         offset <= state->second.bitWidth &&
         width <= state->second.bitWidth - offset;
}

bool isValidArgumentRefCapture(obelisk_rt_context *context,
                               const obelisk_rt_observer_capture_abi_v1 &abi,
                               const obelisk_rt_computed_capture_v1 &capture) {
  if (!context || abi.kind != OBELISK_RT_OBSERVER_CAPTURE_ARGUMENT_REF ||
      abi.width == 0 || capture.payload1 > 2 || capture.payload2 != 0 ||
      (capture.payload1 == 0 && capture.stable_id != 0) ||
      (capture.payload1 != 0 && capture.stable_id == 0))
    return false;
  if (capture.payload1 == 0)
    return isStaticCoverageSignal(context, capture.payload0, abi.width);
  obelisk_rt_object_v1 *owner =
      obelisk_rt_object_from_managed_word(capture.stable_id);
  if (!owner ||
      obelisk_rt_managed_word_from_object(owner) != capture.stable_id ||
      obelisk_rt_managed_object_context(owner) != context)
    return false;
  obelisk_rt_managed_kind_v1 expected = capture.payload1 == 1
                                            ? OBELISK_RT_MANAGED_CLASS
                                            : OBELISK_RT_MANAGED_REFERENCE_PATH;
  if (obelisk_rt_managed_object_kind(owner) != expected)
    return false;
  if (expected != OBELISK_RT_MANAGED_CLASS)
    return true;
  const obelisk_rt_class_descriptor_v1 *descriptor =
      obelisk_rt_managed_object_class_descriptor(owner);
  uint64_t byteWidth = (uint64_t{abi.width} + 7) / 8;
  return descriptor && capture.payload0 >= sizeof(void *) &&
         capture.payload0 <= descriptor->instance_size &&
         byteWidth <= descriptor->instance_size - capture.payload0;
}

bool isValidManagedCapture(obelisk_rt_context *context,
                           const obelisk_rt_observer_capture_abi_v1 &abi,
                           const obelisk_rt_computed_capture_v1 &capture) {
  if (!context || abi.kind != OBELISK_RT_OBSERVER_CAPTURE_MANAGED ||
      abi.width != 64 || capture.payload0 != 0 || capture.payload1 != 0 ||
      capture.payload2 != 0)
    return false;
  obelisk_rt_object_v1 *object =
      obelisk_rt_object_from_managed_word(capture.stable_id);
  return object &&
         obelisk_rt_managed_word_from_object(object) == capture.stable_id &&
         obelisk_rt_managed_object_context(object) == context;
}

bool resolveArgumentRefDependenciesUnlocked(
    obelisk_rt_context *context,
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_computed_wait_record_v1 *plan,
    uint32_t observerLimit = UINT32_MAX) {
  if (!context || !plan)
    return false;
  if (plan->dependency_count == 0)
    return true;
  auto *rawDependencies = reinterpret_cast<obelisk_rt_computed_dependency_v1 *>(
      reinterpret_cast<uint8_t *>(plan) + plan->dependencies_offset);
  if (observerLimit == UINT32_MAX) {
    bool hasArgumentRef = false;
    for (uint32_t index = 0; index != plan->dependency_count; ++index)
      hasArgumentRef |= rawDependencies[index].kind ==
                        OBELISK_RT_OBSERVER_DEPENDENCY_ARGUMENT_REF;
    if (!hasArgumentRef)
      return true;
  }
  auto *observers = computedWaitSpan<obelisk_rt_computed_observer_v1>(
      plan, plan->observers_offset, plan->observer_count);
  auto *captures = computedWaitSpan<obelisk_rt_computed_capture_v1>(
      plan, plan->captures_offset, plan->capture_count);
  auto *dependencies = computedWaitSpan<obelisk_rt_computed_dependency_v1>(
      plan, plan->dependencies_offset, plan->dependency_count);
  if (!observers || !captures || !dependencies)
    return false;
  observerLimit = std::min(observerLimit, plan->observer_count);
  bool hasArgumentRef = false;
  for (uint32_t observerIndex = 0; observerIndex != observerLimit;
       ++observerIndex) {
    const obelisk_rt_computed_observer_v1 &observer = observers[observerIndex];
    for (uint32_t dependencyIndex = 0;
         dependencyIndex != observer.dependency_count; ++dependencyIndex)
      hasArgumentRef |=
          dependencies[observer.dependency_begin + dependencyIndex].kind ==
          OBELISK_RT_OBSERVER_DEPENDENCY_ARGUMENT_REF;
  }
  if (!hasArgumentRef)
    return true;
  if (!execution)
    return false;
  for (uint32_t observerIndex = 0; observerIndex != observerLimit;
       ++observerIndex) {
    const obelisk_rt_computed_observer_v1 &observer = observers[observerIndex];
    const obelisk_rt_observer_descriptor_v1 *descriptor =
        findObserverDescriptor(execution, observer.code_unit_id);
    if (!descriptor)
      return false;
    for (uint32_t dependencyIndex = 0;
         dependencyIndex != observer.dependency_count; ++dependencyIndex) {
      obelisk_rt_computed_dependency_v1 &dependency =
          dependencies[observer.dependency_begin + dependencyIndex];
      if (dependency.kind != OBELISK_RT_OBSERVER_DEPENDENCY_ARGUMENT_REF)
        continue;
      if (dependency.stable_id < observer.capture_begin ||
          dependency.stable_id >=
              uint64_t{observer.capture_begin} + observer.capture_count)
        return false;
      uint32_t localCapture =
          static_cast<uint32_t>(dependency.stable_id - observer.capture_begin);
      if (descriptor->capture_abi[localCapture].kind !=
              OBELISK_RT_OBSERVER_CAPTURE_ARGUMENT_REF ||
          descriptor->capture_abi[localCapture].width != dependency.width)
        return false;
      const obelisk_rt_computed_capture_v1 &capture =
          captures[dependency.stable_id];
      if (capture.payload1 == 0) {
        if (capture.stable_id != 0 ||
            !isStaticCoverageSignal(context, capture.payload0,
                                    dependency.width))
          return false;
        dependency.stable_id = capture.payload0;
        dependency.kind = OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL;
        continue;
      }
      if (capture.payload1 != 1 || capture.payload2 != 0)
        return false;
      obelisk_rt_object_v1 *owner =
          obelisk_rt_object_from_managed_word(capture.stable_id);
      if (!owner ||
          obelisk_rt_managed_word_from_object(owner) != capture.stable_id ||
          obelisk_rt_managed_object_context(owner) != context ||
          obelisk_rt_managed_object_kind(owner) != OBELISK_RT_MANAGED_CLASS)
        return false;
      uint64_t token = obelisk_rt_v1_managed_watch(
          owner, OBELISK_RT_MANAGED_WATCH_FIELD, capture.payload0);
      if (token == 0)
        return false;
      dependency.stable_id = token;
      dependency.kind = OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED;
      dependency.width = 1;
    }
  }
  return true;
}

void eraseCovergroupClockEventRegistrationUnlocked(obelisk_rt_context *context,
                                                   uint64_t logicalToken) {
  if (!context || !context->covergroupClockEvents || logicalToken == 0)
    return;
  CovergroupClockEventFeatureState &feature = *context->covergroupClockEvents;
  auto owned = feature.registrations.find(logicalToken);
  if (owned == feature.registrations.end())
    return;
  if (owned->second && owned->second->strobePending &&
      feature.pendingStrobeCount != 0)
    --feature.pendingStrobeCount;
  for (const auto &pointer : owned->second->clauses) {
    if (!pointer)
      continue;
    CovergroupClockEventClause &clause = *pointer;
    for (const SignalSubscriptionBucketSlot &slot : clause.bucketSlots) {
      auto bucket = feature.subscriptionBuckets.find(slot.key);
      if (bucket == feature.subscriptionBuckets.end() ||
          slot.bucketIndex >= bucket->second.size())
        continue;
      CovergroupClockEventBucketEntry &entry = bucket->second[slot.bucketIndex];
      if (entry.clause != &clause)
        continue;
      CovergroupClockEventBucketEntry moved = bucket->second.back();
      size_t movedFrom = bucket->second.size() - 1;
      entry = moved;
      bucket->second.pop_back();
      if (slot.bucketIndex != movedFrom && moved.clause &&
          moved.slotIndex < moved.clause->bucketSlots.size())
        moved.clause->bucketSlots[moved.slotIndex].bucketIndex =
            slot.bucketIndex;
      if (bucket->second.empty())
        feature.subscriptionBuckets.erase(bucket);
    }
    if (!clause.bucketSlots.empty() && context->signalDiagnosticsEnabled &&
        context->signalDiagnostics.subscriptionsCurrent != 0)
      --context->signalDiagnostics.subscriptionsCurrent;
  }
  feature.registrations.erase(owned);
  for (auto bucket = feature.subscriptionBuckets.begin();
       bucket != feature.subscriptionBuckets.end();) {
    if (bucket->second.empty())
      bucket = feature.subscriptionBuckets.erase(bucket);
    else
      ++bucket;
  }
  if (feature.registrations.empty() && feature.subscriptionBuckets.empty())
    context->covergroupClockEvents.reset();
}

} // namespace

void obelisk_rt_unregister_covergroup_clock_events_unlocked(
    obelisk_rt_context *context, uint64_t logicalToken) {
  eraseCovergroupClockEventRegistrationUnlocked(context, logicalToken);
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_clock_event_register(
    obelisk_rt_context *context,
    const obelisk_rt_computed_wait_record_v1 *eventPlan, uint64_t eventPlanSize,
    uint32_t expectedStrobe, uint64_t observerCodeUnit,
    const obelisk_rt_computed_capture_v1 *captures, uint32_t captureCount) {
  if (!context || !eventPlan ||
      eventPlanSize < sizeof(obelisk_rt_computed_wait_record_v1) ||
      expectedStrobe > 1 || observerCodeUnit == 0 ||
      (captureCount != 0 && !captures))
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  uint64_t insertedLogicalToken = 0;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return context->schedulerStatus;
    uint64_t logicalToken = context->activeLogicalProcessToken;
    if (logicalToken == 0)
      return OBELISK_RT_INVALID_LIFECYCLE;
    bool native = (logicalToken & kNativeLogicalProcessTag) != 0;
    const obelisk_rt_execution_descriptor_v1 *execution = nullptr;
    if (native) {
      uint64_t token = logicalToken & ~kNativeLogicalProcessTag;
      ScheduledProcess *process = findScheduledProcess(context, token);
      if (!process || !process->instance ||
          process->instance != context->activeNativeProcess ||
          !process->instance->descriptor)
        return OBELISK_RT_INVALID_LIFECYCLE;
      execution = process->instance->descriptor->execution;
    } else {
      if (!context->activeDesignTask ||
          context->activeDesignTaskID != logicalToken ||
          !context->designTaskExecuting)
        return OBELISK_RT_INVALID_LIFECYCLE;
      execution = context->execution;
    }
    const obelisk_rt_observer_descriptor_v1 *descriptor =
        findObserverDescriptor(execution, observerCodeUnit);
    if (!descriptor || descriptor->capture_count != captureCount ||
        descriptor->result_width != 1 || descriptor->flags != 0 ||
        captureCount == 0 ||
        descriptor->capture_abi[0].kind !=
            OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP)
      return OBELISK_RT_INVALID_DESIGN;
    if (!obelisk_rt_validate_computed_wait_record(execution, eventPlan,
                                                  eventPlanSize) ||
        eventPlan->clause_count > 64 || eventPlan->total_size != eventPlanSize)
      return OBELISK_RT_INVALID_DESIGN;

    uint64_t covergroupHandle = 0;
    for (uint32_t index = 0; index != captureCount; ++index) {
      const obelisk_rt_observer_capture_abi_v1 &abi =
          descriptor->capture_abi[index];
      const obelisk_rt_computed_capture_v1 &capture = captures[index];
      if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_ARGUMENT_REF) {
        if (!isValidArgumentRefCapture(context, abi, capture))
          return OBELISK_RT_INVALID_DESIGN;
        continue;
      }
      if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_MANAGED) {
        if (!isValidManagedCapture(context, abi, capture))
          return OBELISK_RT_INVALID_DESIGN;
        continue;
      }
      if (capture.payload0 != 0 || capture.payload1 != 0 ||
          capture.payload2 != 0)
        return OBELISK_RT_INVALID_DESIGN;
      if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP) {
        if (index != 0 || covergroupHandle != 0 || capture.stable_id == 0 ||
            abi.width != 64)
          return OBELISK_RT_INVALID_DESIGN;
        covergroupHandle = capture.stable_id;
        continue;
      }
      if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_EVENT &&
          obelisk_rt_stable_handle_is_dynamic_event(capture.stable_id))
        return OBELISK_RT_INVALID_DESIGN;
      obelisk_rt_stable_handle_v1 decoded;
      if (!obelisk_rt_stable_handle_decode(capture.stable_id, &decoded))
        return OBELISK_RT_INVALID_DESIGN;
    }
    if (covergroupHandle == 0)
      return OBELISK_RT_INVALID_DESIGN;
    bool strobe = false;
    obelisk_rt_status status = obelisk_rt_covergroup_strobe_unlocked(
        context, covergroupHandle, strobe);
    if (status != OBELISK_RT_OK)
      return status;
    if (strobe != (expectedStrobe != 0))
      return OBELISK_RT_INVALID_DESIGN;

    const auto *eventObservers =
        computedWaitSpan<obelisk_rt_computed_observer_v1>(
            eventPlan, eventPlan->observers_offset, eventPlan->observer_count);
    const auto *eventCaptures =
        computedWaitSpan<obelisk_rt_computed_capture_v1>(
            eventPlan, eventPlan->captures_offset, eventPlan->capture_count);
    const auto *eventDependencies =
        computedWaitSpan<obelisk_rt_computed_dependency_v1>(
            eventPlan, eventPlan->dependencies_offset,
            eventPlan->dependency_count);
    if (!eventObservers || !eventCaptures || !eventDependencies)
      return OBELISK_RT_INVALID_DESIGN;
    for (uint32_t observerIndex = 0; observerIndex != eventPlan->observer_count;
         ++observerIndex) {
      const auto &observer = eventObservers[observerIndex];
      const auto *eventDescriptor =
          findObserverDescriptor(execution, observer.code_unit_id);
      if (!eventDescriptor)
        return OBELISK_RT_INVALID_DESIGN;
      for (uint32_t captureIndex = 0; captureIndex != observer.capture_count;
           ++captureIndex) {
        const auto &abi = eventDescriptor->capture_abi[captureIndex];
        const auto &capture =
            eventCaptures[observer.capture_begin + captureIndex];
        if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_ARGUMENT_REF) {
          if (!isValidArgumentRefCapture(context, abi, capture))
            return OBELISK_RT_INVALID_DESIGN;
          continue;
        }
        if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_MANAGED) {
          if (!isValidManagedCapture(context, abi, capture))
            return OBELISK_RT_INVALID_DESIGN;
          continue;
        }
        if (capture.payload0 != 0 || capture.payload1 != 0 ||
            capture.payload2 != 0)
          return OBELISK_RT_INVALID_DESIGN;
        if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP ||
            (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_EVENT &&
             obelisk_rt_stable_handle_is_dynamic_event(capture.stable_id)))
          return OBELISK_RT_INVALID_DESIGN;
        obelisk_rt_stable_handle_v1 decoded;
        if (!obelisk_rt_stable_handle_decode(capture.stable_id, &decoded))
          return OBELISK_RT_INVALID_DESIGN;
      }
      if (observerIndex < eventPlan->clause_count)
        for (uint32_t dependencyIndex = 0;
             dependencyIndex != observer.dependency_count; ++dependencyIndex) {
          const auto &dependency =
              eventDependencies[observer.dependency_begin + dependencyIndex];
          if (dependency.kind == OBELISK_RT_OBSERVER_DEPENDENCY_ARGUMENT_REF)
            continue;
          if (dependency.kind == OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED) {
            if (dependency.stable_id == 0 || dependency.width != 1)
              return OBELISK_RT_INVALID_DESIGN;
            continue;
          }
          if (dependency.kind != OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL ||
              !isStaticCoverageSignal(context, dependency.stable_id,
                                      dependency.width))
            return OBELISK_RT_INVALID_DESIGN;
        }
    }
    if (!context->covergroupClockEvents)
      context->covergroupClockEvents =
          std::make_unique<CovergroupClockEventFeatureState>();
    CovergroupClockEventFeatureState &feature = *context->covergroupClockEvents;
    if (feature.registrations.count(logicalToken) != 0)
      return OBELISK_RT_INVALID_LIFECYCLE;

    auto registration = std::make_unique<CovergroupClockEventRegistration>();
    registration->ownerLogicalToken = logicalToken;
    registration->observerCodeUnitID = observerCodeUnit;
    registration->covergroupHandle = covergroupHandle;
    registration->native = native;
    registration->strobe = strobe;
    registration->eventPlan.resize((eventPlanSize + 7) / 8);
    std::memcpy(registration->eventPlan.data(), eventPlan, eventPlanSize);
    registration->captures.assign(captures, captures + captureCount);
    registration->clauses.reserve(eventPlan->clause_count);
    auto *resolvedPlan = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
        registration->eventPlan.data());
    if (!resolveArgumentRefDependenciesUnlocked(
            context, execution, resolvedPlan, resolvedPlan->clause_count))
      return OBELISK_RT_INVALID_DESIGN;
    CovergroupClockEventRegistration *stored = registration.get();
    feature.registrations.emplace(logicalToken, std::move(registration));
    insertedLogicalToken = logicalToken;
    auto rollback = [&] {
      eraseCovergroupClockEventRegistrationUnlocked(context, logicalToken);
    };
    auto *storedPlan = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
        stored->eventPlan.data());
    auto *storedObservers = computedWaitSpan<obelisk_rt_computed_observer_v1>(
        storedPlan, storedPlan->observers_offset, storedPlan->observer_count);
    auto *storedDependencies =
        computedWaitSpan<obelisk_rt_computed_dependency_v1>(
            storedPlan, storedPlan->dependencies_offset,
            storedPlan->dependency_count);
    for (uint32_t index = 0; index != storedPlan->clause_count; ++index) {
      auto clause = std::make_unique<CovergroupClockEventClause>();
      clause->registration = stored;
      clause->clauseIndex = index;
      const auto &primary = storedObservers[index];
      clause->bucketSlots.reserve(primary.dependency_count);
      CovergroupClockEventClause *storedClause = clause.get();
      stored->clauses.push_back(std::move(clause));
      for (uint32_t dependencyIndex = 0;
           dependencyIndex != primary.dependency_count; ++dependencyIndex) {
        const auto &dependency =
            storedDependencies[primary.dependency_begin + dependencyIndex];
        if (dependency.kind == OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED)
          continue;
        if (dependency.kind != OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL) {
          rollback();
          return OBELISK_RT_INVALID_DESIGN;
        }
        uint32_t kind = 0;
        uint32_t id = 0;
        int64_t firstPage = 0;
        int64_t lastPage = 0;
        if (!signalSubscriptionBucketRange(dependency.stable_id,
                                           dependency.width, kind, id,
                                           firstPage, lastPage)) {
          rollback();
          return OBELISK_RT_INVALID_DESIGN;
        }
        __int128 pageCount = static_cast<__int128>(lastPage) - firstPage + 1;
        if (pageCount <= 0) {
          rollback();
          return OBELISK_RT_OUT_OF_RESOURCES;
        }
        bool wide = pageCount > kMaximumIndexedSignalPages;
        int64_t indexedFirst = wide ? kWideSignalSubscriptionPage : firstPage;
        int64_t indexedLast = wide ? kWideSignalSubscriptionPage : lastPage;
        for (int64_t page = indexedFirst;; ++page) {
          SignalSubscriptionBucketKey key{kind, id, page};
          bool alreadyIndexed =
              std::any_of(storedClause->bucketSlots.begin(),
                          storedClause->bucketSlots.end(),
                          [&](const auto &slot) { return slot.key == key; });
          if (!alreadyIndexed) {
            auto &bucket = feature.subscriptionBuckets[key];
            size_t slot = storedClause->bucketSlots.size();
            // Record the reverse edge first. If growing the forward bucket
            // then fails, registration rollback sees the provisional
            // out-of-range index and safely discards this clause without
            // leaving a dangling bucket pointer.
            storedClause->bucketSlots.push_back(
                {key, std::numeric_limits<size_t>::max()});
            bucket.push_back({storedClause, slot});
            storedClause->bucketSlots.back().bucketIndex = bucket.size() - 1;
          }
          if (page == indexedLast)
            break;
        }
      }
      if (!storedClause->bucketSlots.empty() &&
          context->signalDiagnosticsEnabled) {
        ++context->signalDiagnostics.subscriptionsCurrent;
        context->signalDiagnostics.subscriptionsHighWater =
            std::max(context->signalDiagnostics.subscriptionsHighWater,
                     context->signalDiagnostics.subscriptionsCurrent);
      }
    }
    insertedLogicalToken = 0;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    if (insertedLogicalToken != 0) {
      ContextMutexLock lock(context);
      eraseCovergroupClockEventRegistrationUnlocked(context,
                                                    insertedLogicalToken);
    }
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH(const std::length_error &) {
    if (insertedLogicalToken != 0) {
      ContextMutexLock lock(context);
      eraseCovergroupClockEventRegistrationUnlocked(context,
                                                    insertedLogicalToken);
    }
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
  OBELISK_RT_CATCH_ALL {
    if (insertedLogicalToken != 0) {
      ContextMutexLock lock(context);
      eraseCovergroupClockEventRegistrationUnlocked(context,
                                                    insertedLogicalToken);
    }
    return OBELISK_RT_INVALID_DESIGN;
  }
}

void obelisk_rt_unregister_signal_wait_unlocked(
    obelisk_rt_context *context,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    uint64_t waiterToken, bool designWaiter) {
  if (!context) {
    subscriptions.clear();
    return;
  }
  if (waiterToken != 0) {
    if (designWaiter)
      context->designConditionalSignalWaiters.erase(waiterToken);
    else
      context->nativeConditionalSignalWaiters.erase(waiterToken);
    bool *computedRegistered = nullptr;
    if (designWaiter) {
      // Generic computed waits may depend only on events or managed objects.
      // Such a wait has no signal subscription, and a dequeued design task is
      // owner-addressable only through the active-task slot until finalization
      // reindexes it.
      if (context->activeDesignTaskID == waiterToken &&
          context->activeDesignTask)
        computedRegistered =
            &context->activeDesignTask->computedObserverWaitRegistered;
      else {
        auto indexed = context->scheduledDesignTaskIndices.find(waiterToken);
        if (indexed != context->scheduledDesignTaskIndices.end() &&
            indexed->second < context->scheduledDesignTasks.size())
          computedRegistered = &context->scheduledDesignTasks[indexed->second]
                                    .computedObserverWaitRegistered;
      }
    } else if (ScheduledProcess *process =
                   findScheduledProcess(context, waiterToken)) {
      computedRegistered = &process->computedObserverWaitRegistered;
    }
    if (computedRegistered && *computedRegistered) {
      *computedRegistered = false;
      if (context->activeComputedObserverWaiterCount != 0)
        --context->activeComputedObserverWaiterCount;
    }
    uint64_t logicalToken =
        designWaiter ? waiterToken : kNativeLogicalProcessTag | waiterToken;
    bool terminated =
        designWaiter
            ? context->terminatedDesignTasks.count(waiterToken) != 0 ||
                  context->killedDesignTasks.count(waiterToken) != 0
            : context->terminatedNativeProcesses.count(waiterToken) != 0 ||
                  context->killedNativeProcesses.count(waiterToken) != 0;
    if (terminated)
      eraseCovergroupClockEventRegistrationUnlocked(context, logicalToken);
    if (context->noChangeChecks) {
      auto &checks = context->noChangeChecks->checks;
      for (auto current = checks.begin(); current != checks.end();) {
        if (current->first.logicalToken == logicalToken)
          current = checks.erase(current);
        else
          ++current;
      }
      if (checks.empty())
        context->noChangeChecks.reset();
    }
    if (context->clockOccurrences) {
      ClockOccurrenceFeatureState &feature = *context->clockOccurrences;
      if (auto occurrence = feature.waits.find(logicalToken);
          occurrence != feature.waits.end()) {
        if (occurrence->second.conditionMask != 0 &&
            feature.conditionalWaitCount != 0)
          --feature.conditionalWaitCount;
        feature.waits.erase(occurrence);
        // Compiler-private Clause 31 timers have the same lifetime as their
        // coordinator's clock-occurrence wait. Remove only this token's live
        // calendar nodes even when other timing coordinators remain
        // registered. Ordinary signal-wait unregister never enters this cold
        // owner index.
        eraseReplaceableEventsForOwnerUnlocked(context, logicalToken);
      }
      eraseClockOccurrenceSubscriptionsUnlocked(context, logicalToken);
      if (feature.waits.empty() && feature.subscriptions.empty() &&
          feature.subscriptionBuckets.empty())
        context->clockOccurrences.reset();
    }
  }
  for (const std::unique_ptr<SignalSubscription> &owned : subscriptions) {
    if (!owned)
      continue;
    SignalSubscription &subscription = *owned;
    if (subscription.target == SignalSubscription::NativeManagedWait ||
        subscription.target == SignalSubscription::DesignManagedWait) {
      auto waiters = context->managedWatchWaiters.find(subscription.stableID);
      if (waiters != context->managedWatchWaiters.end()) {
        waiters->second.erase(&subscription);
        if (waiters->second.empty())
          context->managedWatchWaiters.erase(waiters);
      }
    }
    if ((subscription.target == SignalSubscription::NativeComputedWait ||
         subscription.outsideStaticFanout) &&
        context->nativeDynamicSignalSubscriptions != 0)
      --context->nativeDynamicSignalSubscriptions;
    for (const SignalSubscriptionBucketSlot &slot : subscription.bucketSlots) {
      auto bucket = context->signalSubscriptionBuckets.find(slot.key);
      if (bucket == context->signalSubscriptionBuckets.end())
        continue;
      if (slot.bucketIndex < bucket->second.size()) {
        SignalSubscriptionBucketEntry &entry = bucket->second[slot.bucketIndex];
        if (entry.subscription == &subscription) {
          SignalSubscriptionBucketEntry moved = bucket->second.back();
          entry = moved;
          bucket->second.pop_back();
          if (moved.subscription && moved.subscription != &subscription &&
              moved.slotIndex < moved.subscription->bucketSlots.size())
            moved.subscription->bucketSlots[moved.slotIndex].bucketIndex =
                slot.bucketIndex;
        }
      }
      if (bucket->second.empty())
        context->signalSubscriptionBuckets.erase(bucket);
    }
    if (context->signalDiagnosticsEnabled &&
        context->signalDiagnostics.subscriptionsCurrent != 0)
      --context->signalDiagnostics.subscriptionsCurrent;
  }
  subscriptions.clear();
}

bool obelisk_rt_register_signal_wait_unlocked(
    obelisk_rt_context *context, const obelisk_rt_wait_record_v1 *wait,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    std::unique_ptr<SignalWaitLatch> &latch, uint64_t waiterToken,
    bool designWaiter) {
  if (!context || !wait)
    return false;
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                             waiterToken, designWaiter);
  if (latch) {
    latch->triggered = false;
    latch->affected = false;
  }
  if (wait->kind != OBELISK_RT_SUSPEND_CHANGE &&
      wait->kind != OBELISK_RT_SUSPEND_EDGE)
    return true;
  uint32_t behaviorFlags =
      wait->flags & ~(OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF |
                      OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL |
                      OBELISK_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS);
  if (behaviorFlags == OBELISK_RT_WAIT_CLOCK_OCCURRENCE) {
    if (waiterToken == 0 || wait->payload == 0 || wait->count == 0 ||
        wait->count > 128)
      return false;
    uint32_t conditionCount =
        static_cast<uint32_t>(__builtin_popcountll(wait->auxiliary));
    if (conditionCount >= wait->count)
      return false;
    uint32_t primaryCount = wait->count - conditionCount;
    if (primaryCount == 0 || primaryCount > 64 ||
        (primaryCount != 64 && (wait->auxiliary >> primaryCount) != 0))
      return false;
    uint64_t logicalToken =
        designWaiter ? waiterToken : kNativeLogicalProcessTag | waiterToken;
    const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
    OBELISK_RT_TRY {
      if (!context->clockOccurrences)
        context->clockOccurrences =
            std::make_unique<ClockOccurrenceFeatureState>();
      ClockOccurrenceFeatureState &feature = *context->clockOccurrences;
      ClockOccurrenceWaitState state;
      state.occurrenceSite = wait->payload;
      state.conditionMask = wait->auxiliary;
      state.conditions.resize(primaryCount);
      uint32_t conditionEntry = primaryCount;
      const auto *captures =
          reinterpret_cast<const obelisk_rt_computed_capture_v1 *>(entries +
                                                                   wait->count);
      uint64_t captureCursor = 0;
      for (uint32_t index = 0; index != primaryCount; ++index) {
        if ((wait->auxiliary & (uint64_t{1} << index)) == 0)
          continue;
        const obelisk_rt_wait_entry_v1 &condition = entries[conditionEntry++];
        ClockOccurrenceCondition &stored = state.conditions[index];
        bool observer =
            condition.edge >= OBELISK_RT_WAIT_CONDITION_OBSERVER &&
            condition.edge <= OBELISK_RT_WAIT_CONDITION_OBSERVER_LAST;
        stored.predicate =
            condition.edge == OBELISK_RT_WAIT_EDGE_NONE
                ? static_cast<uint32_t>(OBELISK_RT_WAIT_CONDITION_KNOWN_ONE)
            : observer
                ? OBELISK_RT_WAIT_CONDITION_PREDICATE +
                      (condition.edge - OBELISK_RT_WAIT_CONDITION_OBSERVER)
                : condition.edge;
        if (observer) {
          if (condition.reserved > UINT64_MAX - captureCursor)
            return false;
          stored.observerCodeUnitID = condition.stable_id;
          stored.observerCaptures.assign(captures + captureCursor,
                                         captures + captureCursor +
                                             condition.reserved);
          captureCursor += condition.reserved;
        } else {
          stored.stableID = condition.stable_id;
          stored.width = condition.reserved;
        }
      }
      feature.waits.insert_or_assign(logicalToken, std::move(state));
      if (conditionCount != 0)
        ++feature.conditionalWaitCount;
      for (uint32_t index = 0; index != primaryCount; ++index)
        if (!appendClockOccurrenceSubscriptionUnlocked(
                context, logicalToken, entries[index].stable_id,
                entries[index].reserved, entries[index].edge, waiterToken,
                !designWaiter, static_cast<uint8_t>(index))) {
          obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                                     waiterToken, designWaiter);
          return false;
        }
      // Cohort subscriptions live in the feature-local index above, but a
      // null sentinel keeps the existing wait-exit ownership guard exact.
      // Ordinary actors retain their original vector layout and hot cleanup
      // branches; feature actors alone pay for this one pointer.
      subscriptions.emplace_back(nullptr);
      return true;
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    }
    OBELISK_RT_CATCH_ALL {
      context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
    }
    obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                               waiterToken, designWaiter);
    return false;
  }
  if (behaviorFlags != OBELISK_RT_WAIT_FLAGS_NONE) {
    if ((behaviorFlags == OBELISK_RT_WAIT_LEVEL_TRUE ||
         behaviorFlags == OBELISK_RT_WAIT_EDGE_IFF) &&
        waiterToken != 0) {
      OBELISK_RT_TRY {
        if (designWaiter)
          context->designConditionalSignalWaiters.insert(waiterToken);
        else
          context->nativeConditionalSignalWaiters.insert(waiterToken);
      }
      OBELISK_RT_CATCH(const std::bad_alloc &) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
        return false;
      }
    }
    return true;
  }
  bool suppressActiveSelf =
      (wait->flags & OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF) != 0;
  OBELISK_RT_TRY {
    if (!latch)
      latch = std::make_unique<SignalWaitLatch>();
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return false;
  }
  const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
  OBELISK_RT_TRY {
    subscriptions.reserve(wait->count);
    for (uint32_t index = 0; index != wait->count; ++index) {
      if (entries[index].reserved == OBELISK_RT_WAIT_WIDTH_MANAGED) {
        if (entries[index].edge != OBELISK_RT_WAIT_EDGE_CHANGE ||
            !appendManagedSubscriptionUnlocked(
                context, entries[index].stable_id,
                designWaiter ? SignalSubscription::DesignManagedWait
                             : SignalSubscription::NativeManagedWait,
                waiterToken, suppressActiveSelf, latch.get(), subscriptions)) {
          obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                                     waiterToken, designWaiter);
          return false;
        }
        continue;
      }
      // A dynamic selection can be temporarily invalid while another entry
      // in the same implicit event expression (for example, its index)
      // remains watchable.  Preserve the positional slot so resuspension can
      // compare the wait exactly, but do not try to subscribe the sentinel.
      if (entries[index].stable_id == UINT64_MAX) {
        subscriptions.emplace_back(nullptr);
        continue;
      }
      if (!appendSignalSubscriptionUnlocked(
              context, entries[index].stable_id, entries[index].reserved,
              entries[index].edge,
              designWaiter ? SignalSubscription::DesignDirectWait
                           : SignalSubscription::NativeDirectWait,
              waiterToken, suppressActiveSelf, latch.get(), subscriptions)) {
        obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                                   waiterToken, designWaiter);
        return false;
      }
    }
    return true;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                             waiterToken, designWaiter);
  return false;
}

bool obelisk_rt_same_clock_occurrence_wait_unlocked(
    const obelisk_rt_context *context, const obelisk_rt_wait_record_v1 *wait,
    uint64_t waiterToken, bool designWaiter) {
  if (!context || !context->clockOccurrences || !wait || waiterToken == 0)
    return false;
  if ((wait->kind != OBELISK_RT_SUSPEND_CHANGE &&
       wait->kind != OBELISK_RT_SUSPEND_EDGE) ||
      !obelisk_rt_is_clock_occurrence_wait_flags(wait->flags) ||
      wait->payload == 0 || wait->count == 0 || wait->count > 128)
    return false;
  uint32_t conditionCount =
      static_cast<uint32_t>(__builtin_popcountll(wait->auxiliary));
  if (conditionCount >= wait->count)
    return false;
  uint32_t primaryCount = wait->count - conditionCount;
  if (primaryCount == 0 || primaryCount > 64 ||
      (primaryCount != 64 && (wait->auxiliary >> primaryCount) != 0))
    return false;
  uint64_t logicalToken =
      designWaiter ? waiterToken : kNativeLogicalProcessTag | waiterToken;
  const ClockOccurrenceFeatureState &feature = *context->clockOccurrences;
  auto state = feature.waits.find(logicalToken);
  auto subscriptions = feature.subscriptions.find(logicalToken);
  if (state == feature.waits.end() ||
      subscriptions == feature.subscriptions.end() ||
      state->second.occurrenceSite != wait->payload ||
      state->second.conditionMask != wait->auxiliary ||
      state->second.conditions.size() != primaryCount ||
      subscriptions->second.size() != primaryCount)
    return false;
  const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
  const auto *captures =
      reinterpret_cast<const obelisk_rt_computed_capture_v1 *>(entries +
                                                               wait->count);
  uint64_t captureCursor = 0;
  uint32_t conditionEntry = primaryCount;
  for (uint32_t index = 0; index != primaryCount; ++index) {
    const ClockOccurrenceSubscription *subscription =
        subscriptions->second[index].get();
    if (!subscription || subscription->stableID != entries[index].stable_id ||
        subscription->bitWidth != entries[index].reserved ||
        subscription->edge != entries[index].edge ||
        subscription->occurrenceBit != index ||
        subscription->native == designWaiter)
      return false;
    ClockOccurrenceCondition expected;
    uint32_t expectedCaptureCount = 0;
    if ((wait->auxiliary & (uint64_t{1} << index)) != 0) {
      const obelisk_rt_wait_entry_v1 &entry = entries[conditionEntry];
      bool observer = entry.edge >= OBELISK_RT_WAIT_CONDITION_OBSERVER &&
                      entry.edge <= OBELISK_RT_WAIT_CONDITION_OBSERVER_LAST;
      expected.predicate =
          entry.edge == OBELISK_RT_WAIT_EDGE_NONE
              ? static_cast<uint32_t>(OBELISK_RT_WAIT_CONDITION_KNOWN_ONE)
          : observer ? OBELISK_RT_WAIT_CONDITION_PREDICATE +
                           (entry.edge - OBELISK_RT_WAIT_CONDITION_OBSERVER)
                     : entry.edge;
      if (observer) {
        expected.observerCodeUnitID = entry.stable_id;
        expectedCaptureCount = entry.reserved;
      } else {
        expected.stableID = entry.stable_id;
        expected.width = entry.reserved;
      }
      ++conditionEntry;
    }
    const ClockOccurrenceCondition &actual = state->second.conditions[index];
    if (actual.stableID != expected.stableID ||
        actual.width != expected.width ||
        actual.predicate != expected.predicate ||
        actual.observerCodeUnitID != expected.observerCodeUnitID ||
        (actual.isObserver() &&
         actual.observerCaptures.size() != expectedCaptureCount))
      return false;
    if (!actual.isObserver())
      continue;
    if (expectedCaptureCount > UINT64_MAX - captureCursor)
      return false;
    for (uint32_t capture = 0; capture != expectedCaptureCount; ++capture) {
      const obelisk_rt_computed_capture_v1 &expectedCapture =
          captures[captureCursor + capture];
      const obelisk_rt_computed_capture_v1 &actualCapture =
          actual.observerCaptures[capture];
      if (actualCapture.stable_id != expectedCapture.stable_id ||
          actualCapture.payload0 != expectedCapture.payload0 ||
          actualCapture.payload1 != expectedCapture.payload1 ||
          actualCapture.payload2 != expectedCapture.payload2)
        return false;
    }
    captureCursor += expectedCaptureCount;
  }
  return conditionEntry == wait->count;
}

bool obelisk_rt_notify_managed_waiters_unlocked(obelisk_rt_context *context,
                                                uint64_t token) {
  if (!context || token == 0)
    return context != nullptr;
  auto found = context->managedWatchWaiters.find(token);
  if (found == context->managedWatchWaiters.end())
    return true;
  for (SignalSubscription *subscription : found->second) {
    if (!subscription || !subscription->latch || subscription->latch->triggered)
      continue;
    if (subscription->suppressActiveSelf && subscription->waiterToken != 0) {
      uint64_t logicalToken =
          subscription->target == SignalSubscription::NativeManagedWait
              ? kNativeLogicalProcessTag | subscription->waiterToken
              : subscription->waiterToken;
      if (context->activeLogicalProcessToken == logicalToken)
        continue;
    }
    subscription->latch->triggered = true;
    OBELISK_RT_TRY {
      auto &candidates =
          subscription->target == SignalSubscription::NativeManagedWait
              ? context->nativePollCandidates
              : context->designPollCandidates;
      candidates.insert(subscription->waiterToken);
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
      return false;
    }
    if (++context->schedulerSelectionGeneration == 0)
      context->schedulerSelectionGeneration = 1;
  }
  return true;
}

bool obelisk_rt_register_computed_signal_wait_unlocked(
    obelisk_rt_context *context, obelisk_rt_computed_wait_record_v1 *wait,
    uint64_t waiterToken, bool designWaiter,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    std::unique_ptr<SignalWaitLatch> &latch) {
  if (!context || !wait || waiterToken == 0)
    return false;
  const obelisk_rt_execution_descriptor_v1 *execution = context->execution;
  if (!designWaiter) {
    ScheduledProcess *process = findScheduledProcess(context, waiterToken);
    execution = process && process->instance && process->instance->descriptor
                    ? process->instance->descriptor->execution
                    : nullptr;
  }
  if (!resolveArgumentRefDependenciesUnlocked(context, execution, wait)) {
    context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
    return false;
  }
  // A computed waiter is outside the exact generated fanout inventory. It can
  // be installed dynamically after AOT startup selected its guarded fast
  // path, so invalidate that cached proof before registering dependencies.
  if (context->nativeSchedulePlan &&
      context->nativeSchedulePlan->specialization_fast)
    *context->nativeSchedulePlan->specialization_fast = 0;
  context->nativeScheduleGuardedFanoutActive = false;
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                             waiterToken, designWaiter);
  OBELISK_RT_TRY {
    if (!latch)
      latch = std::make_unique<SignalWaitLatch>();
    latch->triggered = false;
    latch->affected = false;
    const auto *dependencies =
        reinterpret_cast<const obelisk_rt_computed_dependency_v1 *>(
            reinterpret_cast<const uint8_t *>(wait) +
            wait->dependencies_offset);
    subscriptions.reserve(wait->dependency_count);
    for (uint32_t index = 0; index != wait->dependency_count; ++index) {
      const obelisk_rt_computed_dependency_v1 &dependency = dependencies[index];
      if (dependency.kind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT &&
          dependency.stable_id == OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT)
        context->preponedObserverPresent = true;
      if (dependency.kind != OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL)
        continue;
      if (!appendSignalSubscriptionUnlocked(
              context, dependency.stable_id, dependency.width,
              OBELISK_RT_WAIT_EDGE_NONE,
              designWaiter ? SignalSubscription::DesignComputedWait
                           : SignalSubscription::NativeComputedWait,
              waiterToken, false, latch.get(), subscriptions)) {
        obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                                   waiterToken, designWaiter);
        return false;
      }
    }
    bool *computedRegistered = nullptr;
    if (designWaiter) {
      if (context->activeDesignTaskID == waiterToken &&
          context->activeDesignTask)
        computedRegistered =
            &context->activeDesignTask->computedObserverWaitRegistered;
      else {
        auto indexed = context->scheduledDesignTaskIndices.find(waiterToken);
        if (indexed != context->scheduledDesignTaskIndices.end() &&
            indexed->second < context->scheduledDesignTasks.size())
          computedRegistered = &context->scheduledDesignTasks[indexed->second]
                                    .computedObserverWaitRegistered;
      }
    } else if (ScheduledProcess *process =
                   findScheduledProcess(context, waiterToken)) {
      computedRegistered = &process->computedObserverWaitRegistered;
    }
    if (computedRegistered && !*computedRegistered) {
      if (context->activeComputedObserverWaiterCount == UINT64_MAX) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                                   waiterToken, designWaiter);
        return false;
      }
      *computedRegistered = true;
      ++context->activeComputedObserverWaiterCount;
    }
    return true;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                             waiterToken, designWaiter);
  return false;
}

namespace {

bool latchConditionalSignalWaitersImpl(obelisk_rt_context *context,
                                       uint64_t bitOffset, uint32_t edges) {
  if (!obelisk_rt_has_conditional_signal_waiters(context))
    return true;
  auto readBit = [&](uint64_t handle, uint64_t bitIndex, bool &value,
                     bool &unknown) {
    obelisk_rt_stable_handle_v1 decoded;
    if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
        decoded.offset < 0 || bitIndex > uint64_t{INT64_MAX} ||
        static_cast<uint64_t>(decoded.offset) + bitIndex <
            static_cast<uint64_t>(decoded.offset))
      return false;
    uint64_t relative = static_cast<uint64_t>(decoded.offset) + bitIndex;
    if (decoded.kind == OBELISK_RT_STABLE_HANDLE_AUTOMATIC) {
      auto found = context->nativeAutomaticStates.find(decoded.id);
      if (found == context->nativeAutomaticStates.end() ||
          relative >= found->second.bitWidth)
        return false;
      value = !found->second.value.empty() &&
              ((found->second.value[relative / 8] >> (relative % 8)) & 1) != 0;
      unknown =
          !found->second.unknown.empty() &&
          ((found->second.unknown[relative / 8] >> (relative % 8)) & 1) != 0;
      return true;
    }
    uint64_t absolute = relative;
    if (decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC) {
      auto found = context->nativeStaticStates.find(decoded.id);
      if (found == context->nativeStaticStates.end() ||
          relative >= found->second.bitWidth)
        return false;
      absolute = found->second.bitOffset + relative;
    } else if (decoded.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL) {
      return false;
    }
    if (absolute >= context->stateValue.size() * uint64_t{64} ||
        absolute >= context->stateUnknown.size() * uint64_t{64})
      return false;
    uint64_t mask = uint64_t{1} << (absolute % 64);
    value = (context->stateValue[absolute / 64] & mask) != 0;
    unknown = (context->stateUnknown[absolute / 64] & mask) != 0;
    return true;
  };
  auto levelTrue = [&](const obelisk_rt_wait_entry_v1 &entry) {
    for (uint64_t index = 0; index != entry.reserved; ++index) {
      bool value = false;
      bool unknown = false;
      uint64_t indexed = index <= uint64_t{INT64_MAX}
                             ? obelisk_rt_stable_handle_offset(
                                   entry.stable_id, static_cast<int64_t>(index))
                             : UINT64_MAX;
      auto snapshot = indexed == UINT64_MAX
                          ? context->signalValueSnapshots.end()
                          : context->signalValueSnapshots.find(indexed);
      if (snapshot != context->signalValueSnapshots.end()) {
        value = snapshot->second.value;
        unknown = snapshot->second.unknown;
      } else if (!readBit(entry.stable_id, index, value, unknown)) {
        return false;
      }
      if (value && !unknown)
        return true;
    }
    return false;
  };
  auto consider = [&](const obelisk_rt_wait_record_v1 *wait,
                      uint32_t suspendKind, uint64_t logicalToken,
                      bool &latched) {
    if (!wait || latched || wait->version != OBELISK_RT_VERSION ||
        wait->kind != suspendKind)
      return;
    if ((wait->flags & OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF) != 0 &&
        context->activeLogicalProcessToken == logicalToken)
      return;
    const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
    if (wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE && wait->count == 1) {
      if (rangesOverlap(entries[0].stable_id, entries[0].reserved, bitOffset,
                        1))
        latched = levelTrue(entries[0]);
      return;
    }
    uint32_t behaviorFlags =
        wait->flags & ~OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF;
    if (behaviorFlags != OBELISK_RT_WAIT_EDGE_IFF || wait->count != 2 ||
        !rangesOverlap(entries[0].stable_id, entries[0].reserved, bitOffset,
                       1) ||
        !signalEdgeMatches(entries[0].edge, edges))
      return;
    latched = levelTrue(entries[1]);
  };

  for (uint64_t token : context->nativeConditionalSignalWaiters) {
    auto indexed = context->scheduledProcessIndices.find(token);
    if (indexed == context->scheduledProcessIndices.end() ||
        indexed->second >= context->scheduledProcesses.size())
      continue;
    ScheduledProcess &process = context->scheduledProcesses[indexed->second];
    if (process.token == token && process.instance && process.started) {
      bool wasTriggered = process.signalTriggered;
      consider(currentWait(process), process.suspendKind,
               kNativeLogicalProcessTag | token, process.signalTriggered);
      if (!wasTriggered && process.signalTriggered) {
        OBELISK_RT_TRY { context->nativePollCandidates.insert(token); }
        OBELISK_RT_CATCH(const std::bad_alloc &) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
          return false;
        }
        if (++context->schedulerSelectionGeneration == 0)
          context->schedulerSelectionGeneration = 1;
      }
    }
  }
  for (uint64_t id : context->designConditionalSignalWaiters) {
    auto indexed = context->scheduledDesignTaskIndices.find(id);
    if (indexed == context->scheduledDesignTaskIndices.end() ||
        indexed->second >= context->scheduledDesignTasks.size())
      continue;
    ScheduledDesignTask &task = context->scheduledDesignTasks[indexed->second];
    const obelisk_rt_wait_record_v1 *wait = nullptr;
    if (task.id == id && task.started && !task.terminated &&
        task.waitSize >= sizeof(obelisk_rt_wait_record_v1) &&
        task.waitOffset <= task.frame.size() &&
        task.waitSize <= task.frame.size() - task.waitOffset)
      wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
          task.frame.data() + task.waitOffset);
    bool wasTriggered = task.signalTriggered;
    consider(wait, task.suspendKind, id, task.signalTriggered);
    if (!wasTriggered && task.signalTriggered) {
      OBELISK_RT_TRY { context->designPollCandidates.insert(id); }
      OBELISK_RT_CATCH(const std::bad_alloc &) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
        return false;
      }
      if (++context->schedulerSelectionGeneration == 0)
        context->schedulerSelectionGeneration = 1;
    }
  }
  return true;
}

} // namespace

bool obelisk_rt_latch_conditional_signal_waiters_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint32_t edges) {
  return context && latchConditionalSignalWaitersImpl(context, stableID, edges);
}

bool obelisk_rt_latch_conditional_signal_range_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    uint32_t edges) {
  if (!context)
    return false;
  if (!obelisk_rt_has_conditional_signal_waiters(context))
    return true;
  for (uint64_t bit = 0; bit != bitWidth; ++bit) {
    if (bit > static_cast<uint64_t>(INT64_MAX))
      break;
    uint64_t handle =
        obelisk_rt_stable_handle_offset(stableID, static_cast<int64_t>(bit));
    if (handle != UINT64_MAX &&
        !latchConditionalSignalWaitersImpl(context, handle, edges))
      return false;
  }
  return true;
}

bool obelisk_rt_append_signal_event_unlocked(obelisk_rt_context *context,
                                             uint64_t bitOffset, bool oldValue,
                                             bool oldUnknown, bool newValue,
                                             bool newUnknown) {
  return obelisk_rt_append_signal_event_unlocked(
      context, bitOffset, oldValue, oldUnknown, newValue, newUnknown, true);
}

bool obelisk_rt_append_signal_event_unlocked(obelisk_rt_context *context,
                                             uint64_t bitOffset, bool oldValue,
                                             bool oldUnknown, bool newValue,
                                             bool newUnknown,
                                             bool evaluateComputedObservers) {
  if (!context)
    return false;
  uint32_t edges = transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
  if (edges == 0)
    return true;
  uint64_t sequence = 0;
  if (context->nativeSchedulePlan && !context->clockOccurrences &&
      canUseStaticAOTFanout(context)) {
    // The AOT publisher consumes packed byte planes. A scalar transition is
    // represented by one byte per plane, with bit zero populated.
    const uint8_t changedBits = 1;
    const uint8_t posedgeBits =
        (edges & OBELISK_RT_SIGNAL_POSEDGE) != 0 ? 1 : 0;
    const uint8_t negedgeBits =
        (edges & OBELISK_RT_SIGNAL_NEGEDGE) != 0 ? 1 : 0;
    const uint8_t oldValueBits = oldValue ? 1 : 0;
    const uint8_t oldUnknownBits = oldUnknown ? 1 : 0;
    const uint8_t newValueBits = newValue ? 1 : 0;
    const uint8_t newUnknownBits = newUnknown ? 1 : 0;
    if (!obelisk_rt_publish_signal_transition_batch_unlocked(
            context, bitOffset, 1, &changedBits, &posedgeBits, &negedgeBits, 0,
            &sequence, &oldValueBits, &oldUnknownBits, &newValueBits,
            &newUnknownBits))
      return false;
  } else {
    const uint8_t changedBits = 1;
    const uint8_t newValueBits = newValue ? 1 : 0;
    const uint8_t newUnknownBits = newUnknown ? 1 : 0;
    if (context->coverage)
      obelisk_rt_coverage_record_transition_unlocked(
          context, bitOffset, 1, &changedBits, &newValueBits, &newUnknownBits);
    if (!obelisk_rt_publish_signal_occurrence_unlocked(context, bitOffset, 1,
                                                       edges, &sequence))
      return false;
  }
  if (obelisk_rt_has_conditional_signal_waiters(context))
    context->signalValueSnapshots[bitOffset] = {sequence, newValue, newUnknown};
  if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(context, bitOffset,
                                                            edges))
    return false;
  if (evaluateComputedObservers &&
      !obelisk_rt_notify_observer_signal_unlocked(context, bitOffset, 1))
    return false;
  return true;
}
