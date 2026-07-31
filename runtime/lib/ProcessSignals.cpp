//===- ProcessSignals.cpp - Signal subscription indexing ----------------===//

#include "ProcessSignals.h"
#include "ProcessValidation.h"
#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHandle.h"

#include <algorithm>
#include <memory>
#include <new>
#include <vector>

using namespace obelisk::process;

namespace {

bool appendSignalSubscriptionUnlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    uint32_t edge, SignalSubscription::Target target, uint64_t waiterToken,
    SignalWaitLatch *latch,
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
  subscription->latch = latch;
  subscription->bucketSlots.reserve(wide ? 1 : static_cast<size_t>(pageCount));
  subscriptions.push_back(std::move(subscription));
  SignalSubscription &stored = *subscriptions.back();
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
    try {
      auto &bucket = context->signalSubscriptionBuckets[key];
      bucket.push_back({&stored, slotIndex});
      bucketEntryAppended = true;
      stored.bucketSlots.push_back({key, bucket.size() - 1});
    } catch (...) {
      auto found = context->signalSubscriptionBuckets.find(key);
      if (found != context->signalSubscriptionBuckets.end()) {
        if (bucketEntryAppended && !found->second.empty() &&
            found->second.back().subscription == &stored &&
            found->second.back().slotIndex == slotIndex)
          found->second.pop_back();
        if (found->second.empty())
          context->signalSubscriptionBuckets.erase(found);
      }
      throw;
    }
    if (page == indexedLast)
      break;
  }
  return true;
}

} // namespace

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
  }
  for (const std::unique_ptr<SignalSubscription> &owned : subscriptions) {
    if (!owned)
      continue;
    SignalSubscription &subscription = *owned;
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
  if (wait->flags != OBELISK_RT_WAIT_FLAGS_NONE) {
    if ((wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE ||
         wait->flags == OBELISK_RT_WAIT_EDGE_IFF) &&
        waiterToken != 0) {
      try {
        if (designWaiter)
          context->designConditionalSignalWaiters.insert(waiterToken);
        else
          context->nativeConditionalSignalWaiters.insert(waiterToken);
      } catch (const std::bad_alloc &) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
        return false;
      }
    }
    return true;
  }
  try {
    if (!latch)
      latch = std::make_unique<SignalWaitLatch>();
  } catch (const std::bad_alloc &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return false;
  }
  const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
  try {
    subscriptions.reserve(wait->count);
    for (uint32_t index = 0; index != wait->count; ++index) {
      if (!appendSignalSubscriptionUnlocked(
              context, entries[index].stable_id, entries[index].reserved,
              entries[index].edge,
              designWaiter ? SignalSubscription::DesignDirectWait
                           : SignalSubscription::NativeDirectWait,
              waiterToken, latch.get(), subscriptions)) {
        obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                                   waiterToken, designWaiter);
        return false;
      }
    }
    return true;
  } catch (const std::bad_alloc &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                             waiterToken, designWaiter);
  return false;
}

bool obelisk_rt_register_computed_signal_wait_unlocked(
    obelisk_rt_context *context, const obelisk_rt_computed_wait_record_v1 *wait,
    uint64_t waiterToken, bool designWaiter,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    std::unique_ptr<SignalWaitLatch> &latch) {
  if (!context || !wait || waiterToken == 0)
    return false;
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                             waiterToken, designWaiter);
  try {
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
      if (dependency.kind != OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL)
        continue;
      if (!appendSignalSubscriptionUnlocked(
              context, dependency.stable_id, dependency.width,
              OBELISK_RT_WAIT_EDGE_NONE,
              designWaiter ? SignalSubscription::DesignComputedWait
                           : SignalSubscription::NativeComputedWait,
              waiterToken, latch.get(), subscriptions)) {
        obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                                   waiterToken, designWaiter);
        return false;
      }
    }
    return true;
  } catch (const std::bad_alloc &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                             waiterToken, designWaiter);
  return false;
}
