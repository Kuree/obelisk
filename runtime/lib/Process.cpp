//===- Process.cpp - Shared native/bytecode process instances ------------===//

#include "DesignBytecodeNets.h"
#include "ProcessAllocation.h"
#include "ProcessContext.h"
#include "ProcessObservers.h"
#include "ProcessPacking.h"
#include "ProcessSchedulerScope.h"
#include "ProcessShared.h"
#include "ProcessSignals.h"
#include "ProcessValidation.h"
#include "RuntimeInternal.h"
#include "SignalSemantics.h"
#include "obelisk/Runtime/ClockKernelReadySet.h"
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

// Definitions for the AOT context state declared in ProcessShared.h.
std::mutex nativeScheduleRegistryMutex;
std::unordered_set<const void *> installedNativeScheduleStates;
thread_local obelisk_rt_context *activeNativeAOTContext = nullptr;
thread_local obelisk_rt_context *lockedNativeAOTContext = nullptr;

namespace {

constexpr uint64_t kProcessAllocationMagic = UINT64_C(0x4f42454c4652414d);

struct ProcessAllocationMetadata {
  uint64_t magic;
  size_t size;
  size_t alignment;
  obelisk_rt_context *managedRootContext = nullptr;
};

constexpr size_t kProcessAllocationMetadataOffset =
    (sizeof(obelisk_rt_process_instance_v1) +
     alignof(ProcessAllocationMetadata) - 1) &
    ~(alignof(ProcessAllocationMetadata) - 1);

ProcessAllocationMetadata *
processMetadata(obelisk_rt_process_instance_v1 *instance) {
  return reinterpret_cast<ProcessAllocationMetadata *>(
      reinterpret_cast<uint8_t *>(instance) + kProcessAllocationMetadataOffset);
}
obelisk_rt_status
registerManagedFrameRoots(obelisk_rt_process_instance_v1 *instance,
                          obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_OK;
  ProcessAllocationMetadata *metadata = processMetadata(instance);
  if (metadata->managedRootContext)
    return metadata->managedRootContext == context
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_LIFECYCLE;

  const obelisk_rt_frame_layout_v1 &layout =
      *instance->descriptor->frame_layout;
  bool hasManagedRoots = false;
  for (uint32_t index = 0; index != layout.field_count; ++index)
    hasManagedRoots |=
        layout.fields[index].flags == OBELISK_RT_FRAME_MANAGED_ROOT ||
        layout.fields[index].flags == OBELISK_RT_FRAME_CANDIDATE_ROOT;
  if (!hasManagedRoots) {
    metadata->managedRootContext = context;
    return OBELISK_RT_OK;
  }
  ContextMutexLock lock(context);
  if (std::find(context->managedRootProcesses.begin(),
                context->managedRootProcesses.end(),
                instance) != context->managedRootProcesses.end())
    return OBELISK_RT_INVALID_LIFECYCLE;
  context->managedRootProcesses.push_back(instance);
  metadata->managedRootContext = context;
  return OBELISK_RT_OK;
}

} // namespace

void unregisterManagedFrameRoots(obelisk_rt_process_instance_v1 *instance) {
  ProcessAllocationMetadata *metadata = processMetadata(instance);
  if (!metadata->managedRootContext)
    return;
  obelisk_rt_context *context = metadata->managedRootContext;
  ContextMutexLock lock(context);
  auto found = std::find(context->managedRootProcesses.begin(),
                         context->managedRootProcesses.end(), instance);
  if (found != context->managedRootProcesses.end())
    context->managedRootProcesses.erase(found);
  metadata->managedRootContext = nullptr;
}

void initializeProcessContextLock(
    obelisk_rt_context *context, std::unique_lock<std::recursive_mutex> &lock) {
  if (lockedNativeAOTContext != context)
    lock = std::unique_lock<std::recursive_mutex>(context->mutex);
}

static bool semaphoreWaitTargets(const obelisk_rt_wait_record_v1 *wait,
                                 obelisk_rt_object_v1 *semaphore) {
  if (!wait || wait->count != 1)
    return false;
  const obelisk_rt_wait_entry_v1 *entry = waitEntries(wait);
  return obelisk_rt_object_from_managed_word(entry[0].stable_id) == semaphore;
}

static const obelisk_rt_wait_record_v1 *
designTaskWait(const ScheduledDesignTask &task) {
  if (task.waitSize < sizeof(obelisk_rt_wait_record_v1) ||
      task.waitOffset > task.scratchOffset ||
      task.waitSize > task.scratchOffset - task.waitOffset)
    return nullptr;
  return reinterpret_cast<const obelisk_rt_wait_record_v1 *>(task.frame.data() +
                                                             task.waitOffset);
}

bool obelisk_rt_initialize_event_order_wait_unlocked(
    obelisk_rt_context *context, const obelisk_rt_wait_record_v1 *wait,
    uint32_t &index, bool &ready, bool &failed) {
  index = 0;
  ready = false;
  failed = false;
  if (!context || !wait || wait->kind != OBELISK_RT_SUSPEND_EVENT_ORDER ||
      wait->count == 0)
    return false;
  const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
  auto triggeredThisSlot = [&](uint64_t stableID) {
    auto found = context->events.find(stableID);
    return found != context->events.end() && found->second.generation != 0 &&
           found->second.lastTriggeredTime == context->schedulerTime;
  };

  // IEEE 1800-2017 15.5.4 permits only the first event to consume the
  // persistent triggered state. Any later event already triggered in this
  // slot therefore determines an immediate out-of-order failure.
  for (uint32_t later = 1; later != wait->count; ++later) {
    if (triggeredThisSlot(entries[later].stable_id)) {
      ready = true;
      failed = true;
      return true;
    }
  }
  if (triggeredThisSlot(entries[0].stable_id)) {
    index = 1;
    ready = wait->count == 1;
  }
  return true;
}

static bool updateEventOrderWait(const obelisk_rt_wait_record_v1 *wait,
                                 uint64_t stableID, uint32_t &index,
                                 bool &ready, bool &failed) {
  if (!wait || wait->kind != OBELISK_RT_SUSPEND_EVENT_ORDER ||
      wait->count == 0 || index >= wait->count)
    return false;
  if (ready)
    return true;
  const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);

  if (entries[index].stable_id == stableID) {
    ++index;
    if (index == wait->count) {
      ready = true;
      return true;
    }
    // One occurrence cannot satisfy two positions. Because the later alias
    // now has persistent triggered state, it is already out of order at the
    // point the expected occurrence was consumed.
    for (uint32_t later = index; later != wait->count; ++later) {
      if (entries[later].stable_id == stableID) {
        ready = true;
        failed = true;
        return true;
      }
    }
    return true;
  }
  for (uint32_t later = index + 1; later != wait->count; ++later) {
    if (entries[later].stable_id == stableID) {
      ready = true;
      failed = true;
      return true;
    }
  }
  // Repetitions of an event already consumed in the prescribed order are
  // explicitly harmless. Events outside the inventory are irrelevant.
  return true;
}

bool obelisk_rt_notify_event_order_waiters_unlocked(obelisk_rt_context *context,
                                                    uint64_t stableID) {
  if (!context)
    return false;
  for (ScheduledProcess &process : context->scheduledProcesses) {
    if (!process.instance ||
        process.suspendKind != OBELISK_RT_SUSPEND_EVENT_ORDER ||
        process.waitOrderReady)
      continue;
    if (!updateEventOrderWait(currentWait(process), stableID,
                              process.waitOrderIndex, process.waitOrderReady,
                              process.waitOrderFailed)) {
      context->schedulerStatus = OBELISK_RT_INVALID_FRAME;
      return false;
    }
  }
  for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
    if (task.terminated || task.suspendKind != OBELISK_RT_SUSPEND_EVENT_ORDER ||
        task.waitOrderReady)
      continue;
    if (!updateEventOrderWait(designTaskWait(task), stableID,
                              task.waitOrderIndex, task.waitOrderReady,
                              task.waitOrderFailed)) {
      context->schedulerStatus = OBELISK_RT_INVALID_FRAME;
      return false;
    }
  }
  return true;
}

static obelisk_rt_status
semaphoreQueuedAcquisitionReady(obelisk_rt_context *context,
                                obelisk_rt_object_v1 *semaphore, bool &ready) {
  ready = false;
  uint64_t firstSequence = UINT64_MAX;
  const obelisk_rt_wait_record_v1 *first = nullptr;
  auto consider = [&](bool live, uint32_t suspendKind, uint64_t sequence,
                      const obelisk_rt_wait_record_v1 *wait) {
    if (live && suspendKind == OBELISK_RT_SUSPEND_SEMAPHORE && sequence != 0 &&
        sequence < firstSequence && semaphoreWaitTargets(wait, semaphore)) {
      firstSequence = sequence;
      first = wait;
    }
  };
  for (const ScheduledProcess &process : context->scheduledProcesses)
    consider(process.instance != nullptr &&
                 process.instance != context->activeNativeProcess,
             process.suspendKind, process.waitSequence, currentWait(process));
  for (const ScheduledDesignTask &task : context->scheduledDesignTasks)
    consider(!task.terminated, task.suspendKind, task.waitSequence,
             designTaskWait(task));
  if (!first)
    return OBELISK_RT_OK;
  if (first->flags != 0 || first->payload > UINT32_MAX ||
      static_cast<int32_t>(first->payload) < 0 || first->auxiliary != 0)
    return OBELISK_RT_INVALID_FRAME;
  return obelisk_rt_semaphore_keys_ready(
      semaphore, static_cast<int32_t>(first->payload), ready);
}

obelisk_rt_status
obelisk_rt_semaphore_wait_ready(obelisk_rt_context *context,
                                obelisk_rt_object_v1 *semaphore, int32_t keys,
                                uint64_t waitSequence, bool &ready) {
  ready = false;
  if (!context || !semaphore || waitSequence == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto isEarlier = [&](uint32_t suspendKind, uint64_t sequence,
                       const obelisk_rt_wait_record_v1 *wait) {
    if (suspendKind != OBELISK_RT_SUSPEND_SEMAPHORE || sequence == 0 ||
        sequence >= waitSequence || !wait || wait->count != 1)
      return false;
    const obelisk_rt_wait_entry_v1 *entry = waitEntries(wait);
    return obelisk_rt_object_from_managed_word(entry[0].stable_id) == semaphore;
  };
  for (const ScheduledProcess &process : context->scheduledProcesses)
    if (process.instance && isEarlier(process.suspendKind, process.waitSequence,
                                      currentWait(process)))
      return OBELISK_RT_OK;
  for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
    if (!task.terminated &&
        isEarlier(task.suspendKind, task.waitSequence, designTaskWait(task)))
      return OBELISK_RT_OK;
  }
  return obelisk_rt_semaphore_keys_ready(semaphore, keys, ready);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_semaphore_try_get(obelisk_rt_object_v1 *semaphore, int32_t keys,
                                uint32_t *outSuccess) {
  if (!semaphore || !outSuccess)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outSuccess = 0;
  if (keys < 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_context *context = obelisk_rt_managed_object_context(semaphore);
  if (!context)
    return OBELISK_RT_INVALID_HANDLE;
  ContextMutexLock lock(context);
  if (keys != 0) {
    bool queuedAcquisitionReady = false;
    obelisk_rt_status status = semaphoreQueuedAcquisitionReady(
        context, semaphore, queuedAcquisitionReady);
    if (status != OBELISK_RT_OK || queuedAcquisitionReady)
      return status;
  }
  return obelisk_rt_semaphore_try_get_raw(semaphore, keys, outSuccess);
}

obelisk_rt_status
obelisk_rt_semaphore_wait_acquire(const obelisk_rt_wait_record_v1 *wait,
                                  bool &acquired) {
  acquired = false;
  if (!wait || wait->flags != 0 || wait->count != 1 ||
      wait->payload > UINT32_MAX || static_cast<int32_t>(wait->payload) < 0 ||
      wait->auxiliary != 0)
    return OBELISK_RT_INVALID_FRAME;
  const obelisk_rt_wait_entry_v1 *entry = waitEntries(wait);
  uint32_t success = 0;
  obelisk_rt_status status = obelisk_rt_semaphore_try_get_raw(
      obelisk_rt_object_from_managed_word(entry[0].stable_id),
      static_cast<int32_t>(wait->payload), &success);
  acquired = success != 0;
  return status;
}

template <bool RecordSchedulerEffects>
__attribute__((always_inline)) static inline bool
nativeWaitReadyImpl(obelisk_rt_context &context,
                    const ScheduledProcess &process) {
  if constexpr (RecordSchedulerEffects)
    if (context.signalDiagnosticsEnabled)
      ++context.signalDiagnostics.readinessCalls;
  const obelisk_rt_wait_record_v1 *wait = currentWait(process);
  if (!wait)
    return false;
  const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
  switch (process.suspendKind) {
  case OBELISK_RT_SUSPEND_OBSERVER:
    return process.signalTriggered;
  case OBELISK_RT_SUSPEND_EVENT:
    if (process.waitGenerations.size() != wait->count)
      return false;
    for (uint32_t index = 0; index != wait->count; ++index) {
      auto found = context.events.find(entries[index].stable_id);
      uint64_t generation =
          found == context.events.end() ? 0 : found->second.generation;
      if (generation != process.waitGenerations[index])
        return true;
    }
    return false;
  case OBELISK_RT_SUSPEND_EVENT_ORDER:
    return process.waitOrderReady;
  case OBELISK_RT_SUSPEND_MAILBOX: {
    if (wait->count != 1)
      return false;
    bool ready = false;
    obelisk_rt_status status = obelisk_rt_mailbox_wait_ready(
        obelisk_rt_object_from_managed_word(entries[0].stable_id), wait->flags,
        ready);
    if constexpr (RecordSchedulerEffects)
      if (status != OBELISK_RT_OK)
        context.schedulerStatus = status;
    return status == OBELISK_RT_OK && ready;
  }
  case OBELISK_RT_SUSPEND_SEMAPHORE: {
    if (wait->count != 1 || wait->payload > UINT32_MAX)
      return false;
    bool ready = false;
    obelisk_rt_status status = obelisk_rt_semaphore_wait_ready(
        &context, obelisk_rt_object_from_managed_word(entries[0].stable_id),
        static_cast<int32_t>(wait->payload), process.waitSequence, ready);
    if constexpr (RecordSchedulerEffects)
      if (status != OBELISK_RT_OK)
        context.schedulerStatus = status;
    return status == OBELISK_RT_OK && ready;
  }
  case OBELISK_RT_SUSPEND_AWAIT:
    return wait->count == 1 && obelisk_rt_logical_process_terminated(
                                   &context, entries[0].stable_id);
  case OBELISK_RT_SUSPEND_JOIN: {
    bool ready = wait->flags == 0;
    for (uint32_t index = 0; index != wait->count; ++index) {
      bool terminated = obelisk_rt_logical_process_terminated(
          &context, entries[index].stable_id);
      if (wait->flags == 0)
        ready &= terminated;
      else
        ready |= terminated;
    }
    return ready;
  }
  case OBELISK_RT_SUSPEND_FRONTIER:
    return process.observedEpoch != context.schedulerEpoch;
  case OBELISK_RT_SUSPEND_CHILDREN: {
    uint64_t parent = kNativeLogicalProcessTag | process.token;
    for (const ScheduledProcess &child : context.scheduledProcesses)
      if (child.instance && child.parent == parent)
        return false;
    for (const ScheduledDesignTask &child : context.scheduledDesignTasks)
      if (!child.terminated && child.parent == parent)
        return false;
    return true;
  }
  case OBELISK_RT_SUSPEND_FOREVER:
    return false;
  default:
    return false;
  }
}

template <bool RecordSchedulerEffects>
__attribute__((always_inline)) static inline bool
nativeProcessReadyImpl(obelisk_rt_context &context,
                       const ScheduledProcess &process) {
  if (process.explicitlySuspended)
    return false;
  if (!process.started || process.suspendKind == OBELISK_RT_SUSPEND_NONE)
    return true;
  if (process.suspendKind == OBELISK_RT_SUSPEND_DELAY)
    return process.wakeTime <= context.schedulerTime;
  // Signal subscriptions are installed only after action/wait validation.
  // Publications latch their readiness; the serialized wait is needed again
  // when adopting a new suspension, not on every ready-queue comparison.
  // Keep this path ahead of wait-probe diagnostics, as installed nodes were
  // before unification. All executors now use the same constant-time check.
  if (process.suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
      process.suspendKind == OBELISK_RT_SUSPEND_EDGE)
    return process.signalTriggered ||
           (process.signalLatch && process.signalLatch->triggered);
  return nativeWaitReadyImpl<RecordSchedulerEffects>(context, process);
}

bool nativeProcessReady(obelisk_rt_context &context,
                        const ScheduledProcess &process) {
  return nativeProcessReadyImpl<true>(context, process);
}

static bool indexedSignalBlocked(const ScheduledProcess &process) {
  if (!process.instance || !process.started || process.signalTriggered)
    return false;
  bool signalSuspend = process.suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
                       process.suspendKind == OBELISK_RT_SUSPEND_EDGE;
  return (signalSuspend ||
          process.suspendKind == OBELISK_RT_SUSPEND_OBSERVER) &&
         (!process.signalLatch || !process.signalLatch->triggered);
}

// Slot-final timing coordinators sort after every ordinary executable region
// and NBA barrier at the current numeric time, but before true Postponed work.
// The stored queued/home region remains Observed so notifier publication is
// legal and no process ABI or event-region ordinal changes.
static uint32_t schedulerOrderingRegion(const obelisk_rt_context &context,
                                        const ScheduledProcess &process,
                                        bool signalResume) {
  // Registering a clock-occurrence wait creates this optional feature state
  // before it can publish readiness. Designs without timing observers need
  // no frame lookup to establish an ordinary actor's ordering region. Retain
  // the check for installed plans, which can own waits without subscriptions.
  if (signalResume && (context.clockOccurrences || context.nativeSchedulePlan) &&
      obelisk_rt_is_slot_final_clock_occurrence_wait(currentWait(process)))
    return OBELISK_RT_REGION_POSTPONED;
  return process.queuedRegion == OBELISK_RT_REGION_POSTPONED
             ? OBELISK_RT_REGION_POSTPONED + 1
             : process.queuedRegion;
}

void indexScheduledProcessDelayUnlocked(obelisk_rt_context *context,
                                        const ScheduledProcess &process) {
  if (process.suspendKind != OBELISK_RT_SUSPEND_DELAY || process.phase != 0)
    return;
  // AOT advances its own calendar and need not query the generic heap, so
  // lazy deletion on nextScheduledProcessDelayUnlocked alone is unbounded.
  // Rebuild amortized O(1) per insertion, retaining exactly the live calendar
  // needed if execution hands back to the generic scheduler. Do not touch the
  // ready queues or advance time here (IEEE 1800-2023 4.5).
  auto &heap = context->scheduledProcessDelayHeap;
  if (heap.size() > 64 &&
      heap.size() / 2 > context->scheduledProcesses.size()) {
    heap.clear();
    for (const ScheduledProcess &live : context->scheduledProcesses)
      if (live.instance && live.started && live.phase == 0 &&
          live.suspendKind == OBELISK_RT_SUSPEND_DELAY)
        heap.emplace_back(live.wakeTime, live.token);
    std::make_heap(heap.begin(), heap.end(), std::greater<>());
    return;
  }
  context->scheduledProcessDelayHeap.emplace_back(process.wakeTime,
                                                  process.token);
  std::push_heap(context->scheduledProcessDelayHeap.begin(),
                 context->scheduledProcessDelayHeap.end(), std::greater<>());
}

std::optional<uint64_t>
nextScheduledProcessDelayUnlocked(obelisk_rt_context *context) {
  auto &heap = context->scheduledProcessDelayHeap;
  while (!heap.empty()) {
    auto [wakeTime, token] = heap.front();
    auto indexed = context->scheduledProcessIndices.find(token);
    bool current = indexed != context->scheduledProcessIndices.end() &&
                   indexed->second < context->scheduledProcesses.size();
    if (current) {
      const ScheduledProcess &process =
          context->scheduledProcesses[indexed->second];
      current = process.instance && process.token == token &&
                process.phase == 0 && process.started &&
                process.suspendKind == OBELISK_RT_SUSPEND_DELAY &&
                process.wakeTime == wakeTime;
    }
    if (current)
      return wakeTime;
    std::pop_heap(heap.begin(), heap.end(), std::greater<>());
    heap.pop_back();
  }
  return std::nullopt;
}

obelisk_rt_status obelisk_rt_snapshot_future_time_queues_unlocked(
    const obelisk_rt_context *context, std::vector<uint64_t> &times) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  times.clear();
  const uint64_t now = context->schedulerTime;
  auto append = [&](uint64_t time) {
    if (time > now)
      times.push_back(time);
  };
  OBELISK_RT_TRY {
    for (const ScheduledProcess &process : context->scheduledProcesses)
      if (process.instance && process.phase == 0 && process.started &&
          process.suspendKind == OBELISK_RT_SUSPEND_DELAY)
        append(process.wakeTime);
    for (const ScheduledDesignTask &task : context->scheduledDesignTasks)
      if (!task.terminated && task.phase == 0 && task.started &&
          task.suspendKind == OBELISK_RT_SUSPEND_DELAY)
        append(task.wakeTime);
    for (const ScheduledNBA &update : context->scheduledNBAs)
      if (!update.cancelled)
        append(update.dueTime);
    for (const auto &entry : context->scheduledInertialPathNBAs)
      append(entry.first.first);
    for (const ScheduledManagedNBA &update : context->scheduledManagedNBAs)
      append(update.dueTime);
    for (const ScheduledDesignNBA &update : context->scheduledDesignNBAs)
      append(update.dueTime);
    for (const ScheduledDesignEvent &event : context->scheduledDesignEvents)
      append(event.dueTime);
    if (const ReplaceableEventFeatureState *replaceable =
            obelisk_rt_replaceable_events(context))
      for (const auto &entry : replaceable->calendar)
        append(entry.first.first);
    for (const auto &entry : context->scheduledPassSwitchEvents)
      append(entry.first.first);

    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    times.clear();
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    times.clear();
    return OBELISK_RT_INVALID_DESIGN;
  }
}

bool obelisk_rt_current_time_queue_pending_unlocked(
    obelisk_rt_context *context) {
  if (!context)
    return false;
  const uint64_t now = context->schedulerTime;
  // The VPI read-only synchronization point precedes Postponed. An event
  // currently executing there (or later) therefore cannot keep the current
  // time queue visible.
  if (context->activeExecRegion < OBELISK_RT_REGION_POSTPONED)
    return true;

  uint32_t activePhase = context->schedulerRunningFinals ? 1u : 0u;
  uint32_t unstartedActorRegion =
      obelisk_rt_peek_unstarted_actor_region(context, activePhase);
  for (const ScheduledProcess &process : context->scheduledProcesses) {
    if (!process.instance || process.phase != activePhase)
      continue;
    bool signalResume = process.signalTriggered ||
                        (process.signalLatch && process.signalLatch->triggered);
    bool runnable = nativeProcessReadyImpl<false>(*context, process);
    if (runnable && process.queuedRegion >= unstartedActorRegion &&
        signalResume && !process.urgent && !process.prioritySignal)
      runnable = false;
    if (!runnable)
      continue;
    if (process.urgent ||
        schedulerOrderingRegion(*context, process, signalResume) <=
            OBELISK_RT_REGION_POSTPONED)
      return true;
  }
  if (obelisk_rt_design_task_pending_before_read_only_unlocked(context))
    return true;

  for (const ScheduledNBA &update : context->scheduledNBAs)
    if (!update.cancelled && update.dueTime <= now &&
        update.execRegion < OBELISK_RT_REGION_POSTPONED)
      return true;
  if (!context->scheduledInertialPathNBAs.empty() &&
      context->scheduledInertialPathNBAs.begin()->first.first <= now)
    return true;
  auto hasDueBeforeReadOnly = [now](const auto &entries) {
    return std::any_of(entries.begin(), entries.end(),
                       [now](const auto &entry) {
                         return entry.dueTime <= now &&
                                entry.execRegion < OBELISK_RT_REGION_POSTPONED;
                       });
  };
  if (hasDueBeforeReadOnly(context->scheduledManagedNBAs) ||
      hasDueBeforeReadOnly(context->scheduledDesignNBAs) ||
      hasDueBeforeReadOnly(context->scheduledDesignEvents))
    return true;
  if (const ReplaceableEventFeatureState *replaceable =
          obelisk_rt_replaceable_events(context);
      replaceable &&
      std::any_of(replaceable->calendar.begin(), replaceable->calendar.end(),
                  [now](const auto &entry) {
                    return entry.first.first <= now &&
                           entry.second.execRegion <
                               OBELISK_RT_REGION_POSTPONED;
                  }))
    return true;
  if (!context->scheduledPassSwitchEvents.empty() &&
      context->scheduledPassSwitchEvents.begin()->first.first <= now)
    return true;

  for (const StaticNBAAccumulator &accumulator : context->staticNBAAccumulators)
    if (accumulator.valid &&
        accumulator.execRegion < OBELISK_RT_REGION_POSTPONED)
      return true;
  if (context->nativeScheduleHasGeneratedNBAAccumulators)
    for (uint32_t root = 0; root != context->nativeScheduleNBARootCount;
         ++root) {
      const obelisk_rt_generated_nba_accumulator_256 *generated =
          context->nativeScheduleNBARoots[root].generated_accumulator;
      if (generated && hasGeneratedNBAStages(*generated) &&
          generated->exec_region < OBELISK_RT_REGION_POSTPONED)
        return true;
    }
  return false;
}

void rebuildNativeSchedulerIndexUnlocked(obelisk_rt_context *context) {
  context->scheduledProcessIndices.clear();
  context->nativePollCandidates.clear();
  context->scheduledProcessDelayHeap.clear();
  std::fill(context->nativeScheduleActorIndices.begin(),
            context->nativeScheduleActorIndices.end(), SIZE_MAX);
  context->scheduledProcessIndices.reserve(context->scheduledProcesses.size());
  context->nativePollCandidates.reserve(context->scheduledProcesses.size());
  context->scheduledProcessDelayHeap.reserve(context->scheduledProcesses.size());
  for (size_t index = 0; index != context->scheduledProcesses.size(); ++index) {
    const ScheduledProcess &process = context->scheduledProcesses[index];
    context->scheduledProcessIndices[process.token] = index;
    if (process.aotActorSlot < context->nativeScheduleActorIndices.size())
      context->nativeScheduleActorIndices[process.aotActorSlot] = index;
    if (process.instance && !indexedSignalBlocked(process))
      context->nativePollCandidates.insert(process.token);
    // Periodic evaluation updates detached deadlines without maintaining the
    // generic calendar. Rebuild it from the restored actors at handoff too,
    // otherwise stale heap entries can hide every future clock edge.
    if (process.instance && process.started && process.phase == 0 &&
        process.suspendKind == OBELISK_RT_SUSPEND_DELAY)
      context->scheduledProcessDelayHeap.emplace_back(process.wakeTime,
                                                      process.token);
  }
  std::make_heap(context->scheduledProcessDelayHeap.begin(),
                 context->scheduledProcessDelayHeap.end(), std::greater<>());
}

void obelisk_rt_erase_automatic_bookkeeping_unlocked(
    obelisk_rt_context *context, uint32_t automaticID) {
  if (!context)
    return;
  if (auto state = context->nativeAutomaticStates.find(automaticID);
      state != context->nativeAutomaticStates.end() &&
      state->second.managedRootRegistered) {
    state->second.managedRootRegistered = false;
  }
  if (auto state = context->nativeAutomaticStates.find(automaticID);
      state != context->nativeAutomaticStates.end() &&
      !state->second.managedRootByteOffsets.empty()) {
    state->second.managedRootByteOffsets.clear();
  }
  for (auto snapshot = context->signalValueSnapshots.begin();
       snapshot != context->signalValueSnapshots.end();) {
    uint32_t id = 0;
    int64_t offset = 0;
    if (decodeNativeAutomatic(snapshot->first, id, offset) && id == automaticID)
      snapshot = context->signalValueSnapshots.erase(snapshot);
    else
      ++snapshot;
  }

  uint64_t first = obelisk_rt_stable_handle_encode(
      OBELISK_RT_STABLE_HANDLE_AUTOMATIC, automaticID, 0);
  if (first == UINT64_MAX)
    return;
  uint64_t last = first | UINT32_MAX;
  context->events.erase(context->events.lower_bound(first),
                        context->events.upper_bound(last));
}

void obelisk_rt_invalidate_signal_snapshots_unlocked(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth) {
  if (!context || bitWidth == 0)
    return;
  // Snapshots are stored per canonical stable bit handle. For the common
  // scalar and narrow-write cases, erase the affected handles directly
  // instead of walking every snapshot retained by unrelated signals. Retain
  // the range scan for wide invalidations so the cost is bounded by the
  // smaller of the publication width and the snapshot population.
  if (bitWidth <= context->signalValueSnapshots.size()) {
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      if (bit > static_cast<uint64_t>(INT64_MAX))
        break;
      uint64_t handle =
          nativeHandleOffset(bitOffset, static_cast<int64_t>(bit));
      if (handle != UINT64_MAX)
        context->signalValueSnapshots.erase(handle);
    }
    return;
  }
  for (auto snapshot = context->signalValueSnapshots.begin();
       snapshot != context->signalValueSnapshots.end();)
    if (rangesOverlap(snapshot->first, 1, bitOffset, bitWidth))
      snapshot = context->signalValueSnapshots.erase(snapshot);
    else
      ++snapshot;
}

static obelisk_rt_status
createProcessInstance(obelisk_rt_context *context,
                      const obelisk_rt_process_descriptor_v1 *descriptor,
                      obelisk_rt_process_instance_v1 **outInstance) {
  if (!outInstance)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outInstance = nullptr;
  if (!descriptor)
    return OBELISK_RT_INVALID_ARGUMENT;

  uint64_t nativeSize;
  uint64_t nativeAlignment;
  uint64_t scratchOffset;
  uint64_t scratchSize;
  if (context && descriptor->execution &&
      context->execution != descriptor->execution)
    return OBELISK_RT_INVALID_DESIGN;
  obelisk_rt_status status =
      validateDescriptor(*descriptor, context, nativeSize, nativeAlignment,
                         scratchOffset, scratchSize);
  if (status != OBELISK_RT_OK)
    return status;

  uint64_t headerSize;
  uint64_t frameOffset;
  uint64_t tailSize;
  uint64_t totalSize;
  uint64_t frameAlignment = std::max<uint64_t>(
      descriptor->frame_layout->frame_alignment, nativeAlignment);
  uint64_t allocationAlignment = std::max<uint64_t>(
      {alignof(obelisk_rt_process_instance_v1), frameAlignment, uint64_t{16}});
  if (addOverflow(kProcessAllocationMetadataOffset,
                  sizeof(ProcessAllocationMetadata), headerSize) ||
      alignUp(headerSize, frameAlignment, frameOffset) ||
      addOverflow(scratchOffset, scratchSize, tailSize) ||
      addOverflow(frameOffset, tailSize, totalSize) ||
      alignUp(totalSize, allocationAlignment, totalSize) ||
      totalSize > std::numeric_limits<size_t>::max())
    return OBELISK_RT_OUT_OF_MEMORY;

  void *allocation = allocateProcessMemory(
      static_cast<size_t>(totalSize), static_cast<size_t>(allocationAlignment));
  if (!allocation)
    return OBELISK_RT_OUT_OF_MEMORY;
  std::memset(allocation, 0, static_cast<size_t>(totalSize));
  void *frame = static_cast<uint8_t *>(allocation) + frameOffset;
  auto *instance = ::new (allocation)
      obelisk_rt_process_instance_v1{descriptor,
                                     frame,
                                     frame,
                                     descriptor->frame_layout->frame_size,
                                     scratchOffset,
                                     scratchSize,
                                     nullptr,
                                     0,
                                     0,
                                     OBELISK_RT_PROCESS_READY,
                                     OBELISK_RT_OK,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     0,
                                     0};
  ::new (static_cast<uint8_t *>(allocation) + kProcessAllocationMetadataOffset)
      ProcessAllocationMetadata{
          kProcessAllocationMagic, static_cast<size_t>(totalSize),
          static_cast<size_t>(allocationAlignment), nullptr};
  *outInstance = instance;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_process_instance_create(
    const obelisk_rt_process_descriptor_v1 *descriptor,
    obelisk_rt_process_instance_v1 **outInstance) {
  return createProcessInstance(nullptr, descriptor, outInstance);
}

extern "C" obelisk_rt_status obelisk_rt_v1_process_instance_create_for_context(
    obelisk_rt_context *context,
    const obelisk_rt_process_descriptor_v1 *descriptor,
    obelisk_rt_process_instance_v1 **outInstance) {
  if (!context) {
    if (outInstance)
      *outInstance = nullptr;
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  return createProcessInstance(context, descriptor, outInstance);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_process_instance_frame(obelisk_rt_process_instance_v1 *instance,
                                     void **outFrame, uint64_t *outSize) {
  if (!outFrame || !outSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outFrame = nullptr;
  *outSize = 0;
  if (!instance)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (instance->lifecycle == OBELISK_RT_PROCESS_EXECUTING)
    return OBELISK_RT_INVALID_LIFECYCLE;
  *outFrame = instance->frame;
  *outSize = instance->frame_size;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_process_instance_execute(
    obelisk_rt_process_instance_v1 *instance, obelisk_rt_context *context,
    obelisk_rt_execution_tier requestedTier,
    obelisk_rt_fragment_action_v1 *outAction) {
  if (!outAction)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outAction = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  if (!instance || (requestedTier != OBELISK_RT_TIER_NATIVE &&
                    requestedTier != OBELISK_RT_TIER_BYTECODE))
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  if (instance->lifecycle == OBELISK_RT_PROCESS_EXECUTING ||
      instance->lifecycle == OBELISK_RT_PROCESS_TERMINATED)
    return OBELISK_RT_INVALID_LIFECYCLE;
  uint32_t mask = requestedTier == OBELISK_RT_TIER_NATIVE
                      ? OBELISK_RT_TIER_MASK_NATIVE
                      : OBELISK_RT_TIER_MASK_BYTECODE;
  if ((instance->descriptor->available_tiers & mask) == 0)
    return OBELISK_RT_TIER_UNAVAILABLE;
  if (context && instance->descriptor->execution &&
      context->execution != instance->descriptor->execution)
    return OBELISK_RT_INVALID_DESIGN;
  if (requestedTier == OBELISK_RT_TIER_BYTECODE &&
      instance->descriptor->design_bytecode && !context)
    return OBELISK_RT_INVALID_DESIGN;
  if (!validContinuation(*instance->descriptor->frame_layout,
                         instance->continuation))
    return OBELISK_RT_INVALID_CONTINUATION;
  if (requestedTier == OBELISK_RT_TIER_BYTECODE) {
    if (!instance->descriptor->bytecode &&
        !instance->descriptor->design_bytecode)
      return OBELISK_RT_TIER_UNAVAILABLE;
    obelisk_rt_status status =
        instance->descriptor->bytecode
            ? obelisk_rt_validate_bytecode_program(
                  *instance->descriptor->bytecode, instance->continuation)
            : obelisk_rt_validate_design_bytecode(
                  *instance->descriptor->design_bytecode, context, nullptr,
                  nullptr);
    if (status != OBELISK_RT_OK)
      return status;
  }

  // `tier` records the executor that owns the shared frame tail, not a future
  // scheduling preference. Perform every transition here so native coroutine
  // state is destroyed before bytecode can reuse that storage.
  if (instance->tier != 0 && instance->tier != requestedTier &&
      instance->tier == OBELISK_RT_TIER_NATIVE && instance->native_handle) {
    instance->descriptor->native_destroy(instance);
    if (instance->native_handle)
      return OBELISK_RT_INVALID_LIFECYCLE;
  }
  ManagedExecutionScope managedExecution(context);
  if (managedExecution.getStatus() != OBELISK_RT_OK)
    return managedExecution.getStatus();
  bool installedActiveProcess = false;
  bool installedOwnership = false;
  if (context) {
    ContextMutexLock lock(context);
    if (context->activeNativeProcess &&
        context->activeNativeProcess != instance)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (instance->ownership_context && instance->ownership_context != context)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (!context->activeNativeProcess) {
      context->activeNativeProcess = instance;
      installedActiveProcess = true;
    }
    installedOwnership = !instance->ownership_context;
    instance->ownership_context = context;
  }
  obelisk_rt_status rootStatus = guarded(
      context, [&] { return registerManagedFrameRoots(instance, context); });
  if (rootStatus != OBELISK_RT_OK) {
    if (installedActiveProcess) {
      ContextMutexLock lock(context);
      if (context->activeNativeProcess == instance)
        context->activeNativeProcess = nullptr;
    }
    if (installedOwnership)
      instance->ownership_context = nullptr;
    return rootStatus;
  }
  instance->tier = requestedTier;
  instance->context = context;
  instance->action = outAction;
  instance->status = OBELISK_RT_OK;
  instance->lifecycle = OBELISK_RT_PROCESS_EXECUTING;

  obelisk_rt_status status;
  if (requestedTier == OBELISK_RT_TIER_NATIVE) {
    OBELISK_RT_TRY { status = instance->descriptor->native_execute(instance); }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      status = OBELISK_RT_OUT_OF_MEMORY;
    }
    OBELISK_RT_CATCH_ALL { status = OBELISK_RT_INVALID_ARGUMENT; }
  } else {
    if (instance->descriptor->design_bytecode) {
      status = obelisk_rt_execute_design_bytecode(
          *instance->descriptor->design_bytecode, context, instance->frame,
          instance->scratch_offset + instance->scratch_size,
          instance->scratch_offset, instance->scratch_size,
          instance->continuation, 0, outAction);
    } else {
      obelisk_rt_fragment_descriptor_v1 descriptor{};
      descriptor.handle = {OBELISK_RT_DESCRIPTOR_FRAGMENT, 0,
                           instance->descriptor->handle.id};
      descriptor.code_kind = OBELISK_RT_FRAGMENT_BYTECODE;
      descriptor.code.bytecode = *instance->descriptor->bytecode;
      status = obelisk_rt_v1_fragment_execute(
          &descriptor, context, instance->frame,
          instance->scratch_offset + instance->scratch_size,
          instance->continuation, outAction);
    }
  }

  if (installedActiveProcess) {
    ContextMutexLock lock(context);
    if (context->activeNativeProcess == instance)
      context->activeNativeProcess = nullptr;
  }

  instance->context = nullptr;
  instance->action = nullptr;
  if (status == OBELISK_RT_OK)
    status = validateAction(*instance, *outAction,
                            requestedTier == OBELISK_RT_TIER_BYTECODE);
  instance->status = status;
  if (status != OBELISK_RT_OK) {
    if (requestedTier == OBELISK_RT_TIER_NATIVE && instance->native_handle) {
      instance->descriptor->native_destroy(instance);
      if (instance->native_handle)
        instance->status = status = OBELISK_RT_INVALID_LIFECYCLE;
    }
    instance->lifecycle = OBELISK_RT_PROCESS_SUSPENDED;
    return status;
  }
  instance->continuation = outAction->continuation;
  instance->lifecycle =
      outAction->kind == OBELISK_RT_FRAGMENT_TERMINATE
          ? OBELISK_RT_PROCESS_TERMINATED
      : outAction->kind == OBELISK_RT_FRAGMENT_SUSPEND ||
              outAction->kind == OBELISK_RT_FRAGMENT_PROCESS_SUSPEND
          ? OBELISK_RT_PROCESS_SUSPENDED
          : OBELISK_RT_PROCESS_READY;
  if (instance->lifecycle == OBELISK_RT_PROCESS_TERMINATED &&
      instance->ownership_context) {
    unregisterManagedFrameRoots(instance);
    releaseOwnedNativeStates(instance->ownership_context, instance);
  }
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_process_instance_destroy(
    obelisk_rt_process_instance_v1 *instance) {
  if (!instance)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (instance->lifecycle == OBELISK_RT_PROCESS_EXECUTING)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (instance->observer_pin_count != 0) {
    instance->observer_destroy_pending = 1;
    return OBELISK_RT_OK;
  }
  if (instance->ownership_context)
    releaseOwnedNativeStates(instance->ownership_context, instance);
  unregisterManagedFrameRoots(instance);
  if (instance->native_handle) {
    instance->descriptor->native_destroy(instance);
    if (instance->native_handle)
      return OBELISK_RT_INVALID_LIFECYCLE;
  }
  auto *metadata = processMetadata(instance);
  if (metadata->magic != kProcessAllocationMagic)
    return OBELISK_RT_INVALID_LIFECYCLE;
  size_t allocationSize = metadata->size;
  size_t allocationAlignment = metadata->alignment;
  void *allocation = instance;
  metadata->magic = 0;
  std::destroy_at(metadata);
  std::destroy_at(instance);
  releaseProcessMemory(allocation, allocationSize, allocationAlignment);
  return OBELISK_RT_OK;
}

static void requestImplicitProgramFinishUnlocked(obelisk_rt_context *context) {
  if (!context->programTrackingSeen || !context->programTrackingSealed ||
      context->liveProgramInstances != 0)
    return;
  context->schedulerFinishRequested = true;
  context->nativePeriodicTerminationRequested = 1;
  context->schedulerFinishVerbosity = 0;
}

void obelisk_rt_program_register_unlocked(obelisk_rt_context *context,
                                          uint64_t logicalProcess,
                                          uint64_t programOwner) {
  if (!context || !logicalProcess || !programOwner)
    return;
  auto [owner, insertedOwner] =
      context->programProcesses.try_emplace(programOwner);
  if (insertedOwner)
    ++context->liveProgramInstances;
  owner->second.insert(logicalProcess);
  context->programTrackingSeen = true;
}

void obelisk_rt_program_complete_unlocked(obelisk_rt_context *context,
                                          uint64_t logicalProcess,
                                          uint64_t programOwner) {
  if (!context || !logicalProcess)
    return;
  obelisk_rt_unregister_covergroup_clock_events_unlocked(context,
                                                         logicalProcess);
  if (!programOwner)
    return;
  auto owner = context->programProcesses.find(programOwner);
  if (owner == context->programProcesses.end() ||
      owner->second.erase(logicalProcess) == 0)
    return;
  if (owner->second.empty()) {
    context->programProcesses.erase(owner);
    if (context->liveProgramInstances != 0)
      --context->liveProgramInstances;
  }
  requestImplicitProgramFinishUnlocked(context);
}

void obelisk_rt_program_seal_unlocked(obelisk_rt_context *context) {
  if (!context)
    return;
  context->programTrackingSealed = true;
  requestImplicitProgramFinishUnlocked(context);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_program_register(obelisk_rt_context *context,
                                         uint64_t logicalProcess,
                                         uint64_t programOwner) {
  if (!context || !logicalProcess || !programOwner)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    bool native = (logicalProcess & OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG) != 0;
    if (!native)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t token = logicalProcess & ~OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG;
    auto indexed = context->scheduledProcessIndices.find(token);
    if (indexed == context->scheduledProcessIndices.end() ||
        indexed->second >= context->scheduledProcesses.size())
      return OBELISK_RT_INVALID_HANDLE;
    ScheduledProcess &process = context->scheduledProcesses[indexed->second];
    if (!process.instance || process.token != token)
      return OBELISK_RT_INVALID_LIFECYCLE;
    // A dynamically spawned program-domain helper already inherited its
    // caller's owner in scheduler_add. The generated root annotation is then
    // redundant and must not replace that dynamic ancestry.
    if (process.programOwner)
      return OBELISK_RT_OK;
    process.programOwner = programOwner;
    obelisk_rt_program_register_unlocked(context, logicalProcess, programOwner);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_add(obelisk_rt_context *context,
                            obelisk_rt_process_instance_v1 *instance,
                            uint32_t flags) {
  return obelisk_rt_v1_scheduler_add_ranked(context, instance, flags,
                                            UINT32_MAX);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_add_ranked(obelisk_rt_context *context,
                                   obelisk_rt_process_instance_v1 *instance,
                                   uint32_t flags, uint32_t scheduleRank) {
  return obelisk_rt_v1_scheduler_add_planned(context, instance, flags,
                                             scheduleRank, nullptr, nullptr, 0);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_add_planned(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t flags, uint32_t initialRank, const uint32_t *continuations,
    const uint32_t *ranks, uint32_t continuationCount) {
  uint32_t phase = 0;
  uint32_t homeRegion = OBELISK_RT_REGION_ACTIVE;
  if (!context || !instance ||
      !obelisk_rt_decode_schedule_flags(flags, phase, homeRegion))
    return OBELISK_RT_INVALID_ARGUMENT;
  if (continuationCount != 0 && (!continuations || !ranks))
    return OBELISK_RT_INVALID_ARGUMENT;
  for (uint32_t index = 0; index != continuationCount; ++index)
    if (continuations[index] == 0 ||
        (index != 0 && continuations[index - 1] >= continuations[index]))
      return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    // A child spawned while a Final process is executing belongs to the same
    // Final phase unless its own entry kind already selected that phase. This
    // matches design-bytecode spawn inheritance and prevents a Reactive
    // report child from being stranded in the already-drained ordinary
    // phase. The check is confined to dynamic process creation.
    if (phase == 0 && context->activeDesignTaskID != 0)
      phase = context->activeDesignTaskPhase;
    if (phase == 0 && context->activeNativeProcess &&
        (context->activeLogicalProcessToken & kNativeLogicalProcessTag) != 0) {
      uint64_t activeToken =
          context->activeLogicalProcessToken & ~kNativeLogicalProcessTag;
      auto indexed = context->scheduledProcessIndices.find(activeToken);
      if (indexed != context->scheduledProcessIndices.end() &&
          indexed->second < context->scheduledProcesses.size()) {
        const ScheduledProcess &active =
            context->scheduledProcesses[indexed->second];
        if (active.token == activeToken &&
            active.instance == context->activeNativeProcess)
          phase = active.phase;
      }
    }
    ScheduledProcess process;
    process.instance = instance;
    if (context->nextNativeProcessToken == 0 ||
        context->nextProcessInsertionSequence == 0 ||
        context->nextProcessInsertionSequence == UINT64_MAX) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    if (context->nextNativeProcessToken >= kNativeLogicalProcessTag) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    process.token = context->nextNativeProcessToken++;
    process.parent = (flags & OBELISK_RT_SCHEDULE_DETACHED_CONTROLS) == 0
                         ? context->activeLogicalProcessToken
                         : 0;
    if (process.parent != 0)
      context->logicalProcessParentsWithChildren.insert(process.parent);
    process.programOwner = context->activeProgramOwner;
    obelisk_rt_random_split_unlocked(context, process.random);
    if ((flags & OBELISK_RT_SCHEDULE_DETACHED_CONTROLS) == 0)
      process.controls = context->activeControls;
    process.insertionSequence = context->nextProcessInsertionSequence++;
    process.observedEpoch = context->schedulerEpoch;
    process.phase = phase;
    process.initialProcess = (flags & OBELISK_RT_SCHEDULE_INITIAL) != 0;
    process.startupProcess = (flags & OBELISK_RT_SCHEDULE_STARTUP) != 0;
    process.rootProcess = (flags & OBELISK_RT_SCHEDULE_ROOT) != 0;
    process.urgent = process.startupProcess;
    process.prioritySignal = (flags & OBELISK_RT_SCHEDULE_PRIORITY_SIGNAL) != 0;
    context->scheduledFinalProcessPresent |= phase == 1;
    process.homeRegion = homeRegion;
    process.queuedRegion = homeRegion;
    process.scheduleRank = initialRank;
    if (context->execution && (context->execution->flags &
                               OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0) {
      if ((instance->descriptor->available_tiers &
           OBELISK_RT_TIER_MASK_BYTECODE) == 0) {
        context->schedulerStatus = OBELISK_RT_TIER_UNAVAILABLE;
        return OBELISK_RT_TIER_UNAVAILABLE;
      }
      instance->tier = OBELISK_RT_TIER_BYTECODE;
    }
    process.continuationRanks.reserve(continuationCount);
    for (uint32_t index = 0; index != continuationCount; ++index)
      process.continuationRanks.emplace_back(continuations[index],
                                             ranks[index]);
    context->scheduledProcesses.push_back(std::move(process));
    uint64_t token = context->scheduledProcesses.back().token;
    OBELISK_RT_TRY {
      context->scheduledProcessIndices[token] =
          context->scheduledProcesses.size() - 1;
      context->nativePollCandidates.insert(token);
      if ((flags & OBELISK_RT_SCHEDULE_DETACHED_CONTROLS) == 0)
        obelisk_rt_register_unstarted_actor(context, phase,
                                            kNativeLogicalProcessTag | token);
      if (context->scheduledProcesses.back().programOwner)
        obelisk_rt_program_register_unlocked(
            context, kNativeLogicalProcessTag | token,
            context->scheduledProcesses.back().programOwner);
    }
    OBELISK_RT_CATCH_ALL {
      if (context->scheduledProcesses.back().programOwner)
        obelisk_rt_program_complete_unlocked(
            context, kNativeLogicalProcessTag | token,
            context->scheduledProcesses.back().programOwner);
      context->scheduledProcessIndices.erase(token);
      context->nativePollCandidates.erase(token);
      obelisk_rt_unregister_unstarted_actor(context, phase,
                                            kNativeLogicalProcessTag | token);
      context->scheduledProcesses.pop_back();
      OBELISK_RT_RETHROW;
    }
    obelisk_rt_retain_controls_unlocked(
        context, context->scheduledProcesses.back().controls);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" uint32_t
obelisk_rt_v1_scheduler_priority_signal_pending(obelisk_rt_context *context) {
  if (!context || !context->prioritySignalPending)
    return 0;
  ContextMutexLock lock(context);
  for (const ScheduledProcess &process : context->scheduledProcesses)
    if (process.instance && process.prioritySignal &&
        (process.signalTriggered ||
         (process.signalLatch && process.signalLatch->triggered)))
      return 1;
  for (const ScheduledDesignTask &task : context->scheduledDesignTasks)
    if (!task.terminated && task.prioritySignal && task.signalTriggered)
      return 1;
  context->prioritySignalPending = false;
  return 0;
}

extern "C" uint32_t
obelisk_rt_v1_scheduler_handoff_pending(obelisk_rt_context *context) {
  if (!context)
    return 1;
  ContextMutexLock lock(context);
  return context->schedulerStatus != OBELISK_RT_OK ||
                 obelisk_rt_covergroup_strobes_pending_unlocked(context)
             ? 1
             : 0;
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_install_aot(
    obelisk_rt_context *context, const obelisk_rt_native_schedule_plan *plan) {
  if (!context || !plan ||
      plan->size != sizeof(obelisk_rt_native_schedule_plan))
    return OBELISK_RT_INVALID_ARGUMENT;
  bool actorStorageFits = plan->mutable_state_size >=
                          uint64_t{plan->actor_capacity} * sizeof(void *);
  bool nbaTablesValid =
      plan->nba_reserved == 0 &&
      ((plan->nba_root_count == 0 && plan->nba_roots == nullptr) ||
       (plan->nba_root_count != 0 && plan->nba_roots != nullptr)) &&
      ((plan->nba_site_count == 0 && plan->nba_sites == nullptr) ||
       (plan->nba_site_count != 0 && plan->nba_sites != nullptr)) &&
      (plan->nba_site_count == 0 || plan->nba_root_count != 0) &&
      plan->nba_site_count <= std::numeric_limits<size_t>::max();
  bool fanoutTableValid =
      (plan->fanout_entry_count == 0 && plan->fanout_entries == nullptr) ||
      (plan->fanout_entry_count != 0 && plan->fanout_entries != nullptr);
  bool actorRootTableValid =
      (plan->actor_root_count == 0 && plan->actor_roots == nullptr) ||
      (plan->actor_root_count != 0 && plan->actor_roots != nullptr);
  bool clockKernelTableValid =
      plan->clock_kernel_reserved == 0 &&
      ((plan->clock_kernel_count == 0 && plan->clock_kernels == nullptr &&
        plan->merged_fragment_count == 0 && plan->merged_fragments == nullptr &&
        plan->timeslot_coordinator == nullptr) ||
       (plan->clock_kernel_count != 0 && plan->clock_kernels != nullptr &&
        plan->merged_fragment_count != 0 && plan->merged_fragments != nullptr &&
        plan->timeslot_coordinator != nullptr)) &&
      plan->merged_fragment_count <= std::numeric_limits<size_t>::max();
  bool nbaCommitValid =
      ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA) != 0) ==
      (plan->nba_commit != nullptr);
  uint32_t expectedNBADirtyWords = (plan->nba_root_count + 63) / 64;
  uint32_t expectedNBADirtySummaryWords = (expectedNBADirtyWords + 63) / 64;
  bool nbaDirtyRootsValid =
      plan->nba_dirty_reserved == 0 && plan->nba_dirty_summary_reserved == 0 &&
      ((plan->nba_dirty_word_count == 0 && plan->nba_dirty_roots == nullptr &&
        plan->nba_dirty_summary_word_count == 0 &&
        plan->nba_dirty_summary == nullptr) ||
       (plan->nba_dirty_word_count == expectedNBADirtyWords &&
        plan->nba_dirty_roots != nullptr &&
        plan->nba_dirty_summary_word_count == expectedNBADirtySummaryWords &&
        plan->nba_dirty_summary != nullptr));
  bool specializationFastValid =
      ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION) !=
       0) == (plan->specialization_fast != nullptr) &&
      ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION) == 0 ||
       (plan->flags & (OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE |
                       OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA)) != 0);
  constexpr uint32_t staticExecutionRequirements =
      OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
      OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS;
  bool exactFanout =
      (plan->flags & (OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT |
                      OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT)) != 0;
  bool cleanSuperstep =
      (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP) != 0;
  bool staticEvalIsland =
      (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_STATIC_EVAL_ISLAND) != 0;
  bool cleanSuperstepValid =
      !cleanSuperstep ||
      ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC) != 0 &&
       (plan->flags & staticExecutionRequirements) ==
           staticExecutionRequirements &&
       exactFanout && !staticEvalIsland);
  bool staticEvalIslandValid =
      !staticEvalIsland ||
      ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC) == 0 &&
       (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_EVAL) != 0 &&
       (plan->flags & staticExecutionRequirements) ==
           staticExecutionRequirements &&
       exactFanout && !cleanSuperstep);
  bool evalSchedulerValid =
      (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_EVAL) == 0 ||
      ((cleanSuperstep || staticEvalIsland) && plan->clock_kernel_count != 0 &&
       plan->timeslot_coordinator && plan->promotion_invalidate &&
       plan->promotion_ready);
  bool statePlanesValid =
      plan->state_bit_count == 0
          ? plan->state_value == nullptr && plan->state_unknown == nullptr
          : plan->state_value != nullptr && plan->state_unknown != nullptr;
  if (!plan->mutable_state || plan->mutable_state_size == 0 ||
      plan->actor_capacity == 0 || !actorStorageFits || !statePlanesValid ||
      !nbaTablesValid || !fanoutTableValid || !actorRootTableValid ||
      !clockKernelTableValid || !nbaCommitValid || !nbaDirtyRootsValid ||
      !specializationFastValid ||
      ((plan->promotion_invalidate_range || plan->promotion_recheck_range) &&
       !plan->promotion_invalidate) ||
      !cleanSuperstepValid || !staticEvalIslandValid || !evalSchedulerValid ||
      (plan->flags & ~(OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
                       OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO |
                       OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
                       OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT |
                       OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE |
                       OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA |
                       OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
                       OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT |
                       OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION |
                       OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
                       OBELISK_RT_NATIVE_SCHEDULE_EVAL |
                       OBELISK_RT_NATIVE_SCHEDULE_STATIC_EVAL_ISLAND)) != 0 ||
      !plan->bind || !plan->run || !plan->fallback_snapshot)
    return OBELISK_RT_INVALID_ARGUMENT;
  const obelisk_rt_static_nba_root *nbaRoots = plan->nba_roots;
  uint32_t nbaRootCount = plan->nba_root_count;
  const obelisk_rt_static_nba_site *nbaSites = plan->nba_sites;
  uint64_t nbaSiteCount = plan->nba_site_count;
  const obelisk_rt_static_fanout_entry *fanoutEntries = plan->fanout_entries;
  uint64_t fanoutEntryCount = plan->fanout_entry_count;
  const obelisk_rt_static_actor_root *actorRoots = plan->actor_roots;
  uint64_t actorRootCount = plan->actor_root_count;
  const obelisk_rt_native_clock_kernel *clockKernels = plan->clock_kernels;
  uint32_t clockKernelCount = plan->clock_kernel_count;
  const obelisk_rt_native_merged_fragment *mergedFragments =
      plan->merged_fragments;
  uint64_t mergedFragmentCount = plan->merged_fragment_count;
  for (uint32_t index = 0; index != nbaRootCount; ++index) {
    const obelisk_rt_static_nba_root &root = nbaRoots[index];
    if (root.static_state == 0 || root.bit_width == 0 ||
        root.bit_width > UINT64_MAX - 63 ||
        (root.bit_width + 7) / 8 > std::numeric_limits<size_t>::max() ||
        (root.generated_accumulator &&
         root.bit_width > OBELISK_RT_GENERATED_NBA_MAX_BITS))
      return OBELISK_RT_INVALID_ARGUMENT;
    for (uint32_t previous = 0; previous != index; ++previous)
      if (nbaRoots[previous].static_state == root.static_state)
        return OBELISK_RT_INVALID_ARGUMENT;
  }
  for (uint64_t index = 0; index != nbaSiteCount; ++index) {
    const obelisk_rt_static_nba_site &site = nbaSites[index];
    if (site.site == UINT64_MAX || site.root >= nbaRootCount ||
        (site.storage != OBELISK_RT_STATIC_NBA_FIXED_SLOT &&
         site.storage != OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR) ||
        (index != 0 && nbaSites[index - 1].site >= site.site))
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  for (uint64_t index = 0; index != fanoutEntryCount; ++index) {
    const obelisk_rt_static_fanout_entry &entry = fanoutEntries[index];
    uint32_t route = entry.reserved & OBELISK_RT_FANOUT_ROUTE_MASK;
    uint32_t unknownFlags =
        entry.reserved & ~(OBELISK_RT_FANOUT_ROUTE_MASK |
                           OBELISK_RT_FANOUT_SUPPRESS_ACTIVE_SELF);
    if (entry.static_state == 0 || entry.actor_slot >= plan->actor_capacity ||
        entry.continuation == 0 || entry.bit_width == 0 ||
        entry.compute_node == UINT32_MAX || unknownFlags != 0 ||
        (route != OBELISK_RT_FANOUT_RUNTIME &&
         (((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_EVAL) == 0) ||
          route > OBELISK_RT_FANOUT_PERIODIC_ALIAS)) ||
        entry.low_bit > UINT64_MAX - entry.bit_width ||
        entry.edge < OBELISK_RT_WAIT_EDGE_CHANGE ||
        entry.edge > OBELISK_RT_WAIT_EDGE_BOTH ||
        (clockKernelCount != 0 &&
         (entry.kernel >= clockKernelCount ||
          entry.merged_bit / 64 >=
              clockKernels[entry.kernel].ingress_word_count)) ||
        (index != 0 &&
         std::tuple{fanoutEntries[index - 1].static_state,
                    fanoutEntries[index - 1].low_bit,
                    fanoutEntries[index - 1].bit_width,
                    fanoutEntries[index - 1].edge,
                    fanoutEntries[index - 1].actor_slot,
                    fanoutEntries[index - 1].continuation} >=
             std::tuple{entry.static_state, entry.low_bit, entry.bit_width,
                        entry.edge, entry.actor_slot, entry.continuation}))
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  for (uint32_t index = 0; index != clockKernelCount; ++index) {
    const obelisk_rt_native_clock_kernel &kernel = clockKernels[index];
    if (kernel.static_state == 0 || kernel.bit_width == 0 ||
        kernel.low_bit > UINT64_MAX - kernel.bit_width ||
        kernel.edge < OBELISK_RT_WAIT_EDGE_CHANGE ||
        kernel.edge > OBELISK_RT_WAIT_EDGE_BOTH || !kernel.ingress_mask ||
        kernel.ingress_word_count == 0 ||
        kernel.ingress_word_count > (uint64_t{UINT32_MAX} + 63) / 64 ||
        (kernel.reserved & ~obelisk::runtime::indexedClockKernelReadySet) !=
            0 ||
        (((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_EVAL) != 0) &&
         !kernel.active_mask))
      return OBELISK_RT_INVALID_ARGUMENT;
    for (uint32_t previous = 0; previous != index; ++previous) {
      const obelisk_rt_native_clock_kernel &other = clockKernels[previous];
      if (std::tuple{other.static_state, other.low_bit, other.bit_width,
                     other.edge} >= std::tuple{kernel.static_state,
                                               kernel.low_bit, kernel.bit_width,
                                               kernel.edge})
        return OBELISK_RT_INVALID_ARGUMENT;
    }
  }
  for (uint64_t index = 0; index != mergedFragmentCount; ++index) {
    const obelisk_rt_native_merged_fragment &merged = mergedFragments[index];
    bool directFragmentValid =
        !merged.execute ||
        (merged.kernel < clockKernelCount && merged.continuation != 0 &&
         (merged.flags & OBELISK_RT_MERGED_FRAGMENT_FALLBACK) == 0 &&
         (plan->flags & (OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
                         OBELISK_RT_NATIVE_SCHEDULE_STATIC_EVAL_ISLAND)) != 0);
    if (merged.actor_slot >= plan->actor_capacity ||
        merged.compute_node == UINT32_MAX || !directFragmentValid ||
        (merged.flags & ~(OBELISK_RT_MERGED_FRAGMENT_SHARED |
                          OBELISK_RT_MERGED_FRAGMENT_FALLBACK)) != 0 ||
        (merged.kernel < clockKernelCount &&
         merged.bit / 64 >= clockKernels[merged.kernel].ingress_word_count))
      return OBELISK_RT_INVALID_ARGUMENT;
    for (uint64_t previous = 0; previous != index; ++previous)
      if (mergedFragments[previous].kernel == merged.kernel &&
          mergedFragments[previous].bit == merged.bit)
        return OBELISK_RT_INVALID_ARGUMENT;
  }
  for (uint64_t index = 0; index != actorRootCount; ++index) {
    const obelisk_rt_static_actor_root &entry = actorRoots[index];
    if (entry.actor_slot >= plan->actor_capacity || entry.static_state == 0 ||
        entry.reserved != 0 || entry.flags == 0 ||
        (entry.flags &
         ~(OBELISK_RT_STATIC_ROOT_READ | OBELISK_RT_STATIC_ROOT_WRITE)) != 0 ||
        !findNativeStaticState(context, entry.static_state) ||
        (index != 0 &&
         std::tuple{actorRoots[index - 1].actor_slot,
                    actorRoots[index - 1].static_state,
                    actorRoots[index - 1].flags} >=
             std::tuple{entry.actor_slot, entry.static_state, entry.flags}))
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (context->nativeSchedulePlan || !context->scheduledProcesses.empty() ||
        !context->scheduledDesignTasks.empty() ||
        context->activeNativeProcess || context->designTaskExecuting) {
      if (context->signalDiagnosticsEnabled)
        std::fprintf(stderr,
                     "obelisk-aot-install-reject plan=%u processes=%zu "
                     "tasks=%zu active=%u design=%u\n",
                     context->nativeSchedulePlan != nullptr,
                     context->scheduledProcesses.size(),
                     context->scheduledDesignTasks.size(),
                     context->activeNativeProcess != nullptr,
                     context->designTaskExecuting);
      return OBELISK_RT_INVALID_LIFECYCLE;
    }
    if (context->execution && context->execution->checksum != 0 &&
        plan->graph_layout_checksum != context->execution->checksum)
      return OBELISK_RT_LAYOUT_MISMATCH;
    if (plan->state_bit_count != 0 &&
        !validNativeStatePlanesUnlocked(context, plan->state_value,
                                        plan->state_unknown,
                                        plan->state_bit_count))
      return OBELISK_RT_LAYOUT_MISMATCH;
    for (uint32_t index = 0; index != nbaRootCount; ++index) {
      const obelisk_rt_static_nba_root &root = nbaRoots[index];
      const NativeStaticState *state =
          findNativeStaticState(context, root.static_state);
      if (!state || state->bitWidth != root.bit_width)
        return OBELISK_RT_LAYOUT_MISMATCH;
    }
    for (uint64_t index = 0; index != fanoutEntryCount; ++index) {
      const obelisk_rt_static_fanout_entry &entry = fanoutEntries[index];
      const NativeStaticState *state =
          findNativeStaticState(context, entry.static_state);
      if (!state || entry.low_bit > state->bitWidth ||
          entry.bit_width > state->bitWidth - entry.low_bit)
        return OBELISK_RT_LAYOUT_MISMATCH;
      if (clockKernelCount != 0 && entry.kernel != UINT32_MAX &&
          (entry.kernel >= clockKernelCount ||
           entry.merged_bit / 64 >=
               clockKernels[entry.kernel].ingress_word_count))
        return OBELISK_RT_LAYOUT_MISMATCH;
    }
    for (uint32_t index = 0; index != clockKernelCount; ++index) {
      const obelisk_rt_native_clock_kernel &kernel = clockKernels[index];
      const NativeStaticState *state =
          findNativeStaticState(context, kernel.static_state);
      if (!state || kernel.low_bit > state->bitWidth ||
          kernel.bit_width > state->bitWidth - kernel.low_bit)
        return OBELISK_RT_LAYOUT_MISMATCH;
    }
    {
      std::lock_guard<std::mutex> registryLock(nativeScheduleRegistryMutex);
      if (!installedNativeScheduleStates.insert(plan->mutable_state).second) {
        if (context->signalDiagnosticsEnabled)
          std::fprintf(stderr, "obelisk-aot-install-reject duplicate-state\n");
        return OBELISK_RT_INVALID_LIFECYCLE;
      }
    }
    if (plan->state_bit_count != 0 &&
        !importNativeStatePlanesUnlocked(context, plan->state_value,
                                         plan->state_unknown,
                                         plan->state_bit_count)) {
      std::lock_guard<std::mutex> registryLock(nativeScheduleRegistryMutex);
      installedNativeScheduleStates.erase(plan->mutable_state);
      return OBELISK_RT_LAYOUT_MISMATCH;
    }
    OBELISK_RT_TRY {
#if defined(__x86_64__) || defined(_M_X64)
      __builtin_cpu_init();
      context->nativeScheduleAVX2 = __builtin_cpu_supports("avx2");
#else
      context->nativeScheduleAVX2 = false;
#endif
      context->nativeScheduleActors.assign(plan->actor_capacity, nullptr);
      context->nativeScheduleActorTokens.assign(plan->actor_capacity, 0);
      context->nativeScheduleActorIndices.assign(plan->actor_capacity,
                                                 SIZE_MAX);
      auto &actorRootRanges = context->nativeScheduleActorRootRanges;
      actorRootRanges.assign(plan->actor_capacity,
                             {actorRootCount, actorRootCount});
      for (uint64_t index = 0; index != actorRootCount; ++index) {
        auto &range = actorRootRanges[actorRoots[index].actor_slot];
        if (range.first == actorRootCount)
          range.first = index;
        range.second = index + 1;
      }
      // Periodic re-entry is frequent for trace-heavy models. The ownership
      // table is immutable, so build its write footprint once rather than
      // allocating one hash node per actor/root at every cold checkpoint.
      auto &generatedWrites = context->nativePeriodicGeneratedWritableStates;
      generatedWrites.clear();
      std::vector<uint8_t> generatedActors(plan->actor_capacity, 0);
      for (uint64_t index = 0; index != mergedFragmentCount; ++index)
        if (mergedFragments[index].execute)
          generatedActors[mergedFragments[index].actor_slot] = 1;
      for (uint64_t index = 0; index != actorRootCount; ++index) {
        const auto &root = actorRoots[index];
        if ((root.flags & OBELISK_RT_STATIC_ROOT_WRITE) != 0 &&
            generatedActors[root.actor_slot])
          generatedWrites.insert(root.static_state);
      }
      for (uint32_t index = 0; index != nbaRootCount; ++index)
        generatedWrites.insert(nbaRoots[index].static_state);
      context->nativeScheduleNodes.clear();
      context->nativeScheduleActorNodes.clear();
      context->nativeScheduleFanoutNodes.clear();
      context->nativeScheduleFanoutRanges.clear();
      context->nativeScheduleReadyNodes.resize(0);
      context->nativeScheduleDeadlines.assign(plan->actor_capacity, UINT64_MAX);
      context->nativeScheduleDeadlineHeap.clear();
      context->nativeScheduleDeadlineHeap.reserve(plan->actor_capacity);
      context->nativeScheduleDeadlinePositions.assign(plan->actor_capacity,
                                                      UINT32_MAX);
      uint32_t maximumStaticID = 0;
      for (const auto &[id, state] : context->nativeStaticStates) {
        (void)state;
        maximumStaticID = std::max(maximumStaticID, id);
      }
      if (maximumStaticID <= context->nativeStaticStates.size()) {
        context->nativeScheduleStaticStateIndex.assign(
            static_cast<size_t>(maximumStaticID) + 1, {});
        for (const auto &[id, state] : context->nativeStaticStates)
          context->nativeScheduleStaticStateIndex[id] = state;
      }
      context->nativeScheduleStaticStateFanoutEdges.assign(
          context->nativeScheduleStaticStateIndex.size(), 0);
      context->nativeScheduleFanoutRanges.assign(
          context->nativeScheduleStaticStateIndex.size(),
          {fanoutEntryCount, fanoutEntryCount});
      for (uint64_t index = 0; index != fanoutEntryCount; ++index) {
        const obelisk_rt_static_fanout_entry &entry = fanoutEntries[index];
        if (entry.static_state < context->nativeScheduleFanoutRanges.size()) {
          auto &range = context->nativeScheduleFanoutRanges[entry.static_state];
          if (range.first == fanoutEntryCount)
            range.first = index;
          range.second = index + 1;
        }
        if (entry.static_state <
            context->nativeScheduleStaticStateFanoutEdges.size())
          context->nativeScheduleStaticStateFanoutEdges[entry.static_state] |=
              static_cast<uint8_t>(uint32_t{1} << entry.edge);
      }
      size_t dirtyWords =
          (context->nativeScheduleStaticStateIndex.size() + 63) / 64;
      size_t dirtySummaryWords = (dirtyWords + 63) / 64;
      context->nativeScheduleTransientDirtyMask.assign(dirtyWords, 0);
      context->nativeSchedulePersistentDirtyMask.assign(dirtyWords, 0);
      context->nativeScheduleTransientDirtySummary.assign(dirtySummaryWords, 0);
      context->nativeSchedulePersistentDirtySummary.assign(dirtySummaryWords,
                                                           0);
      context->staticNBAAccumulators.clear();
      context->staticNBAAccumulators.reserve(nbaRootCount);
      context->staticNBAAccumulatorsPending = false;
      context->staticNBASlowRoots.assign(nbaRootCount, 0);
      context->staticNBASlowRootsPresent = false;
      context->staticNBARootHasFanout.assign(nbaRootCount, 0);
      context->nativeScheduleGeneratedNBAStageCounts.assign(nbaRootCount, 0);
      context->nativeScheduleGeneratedNBAOffsets.assign(nbaRootCount,
                                                        UINT64_MAX);
      context->nativeScheduleGeneratedBatchEligible = nbaRootCount != 0;
      context->nativeScheduleHasGeneratedNBAAccumulators = false;
      for (uint32_t index = 0; index != nbaRootCount; ++index) {
        const obelisk_rt_static_nba_root &root = nbaRoots[index];
        context->nativeScheduleHasGeneratedNBAAccumulators |=
            root.generated_accumulator != nullptr;
        const obelisk_rt_static_fanout_entry *fanout = std::lower_bound(
            fanoutEntries, fanoutEntries + fanoutEntryCount, root.static_state,
            [](const auto &entry, uint32_t staticState) {
              return entry.static_state < staticState;
            });
        if (fanout != fanoutEntries + fanoutEntryCount &&
            fanout->static_state == root.static_state)
          context->staticNBARootHasFanout[index] = 1;
        const NativeStaticState *state =
            findNativeStaticState(context, root.static_state);
        bool generatedRoot =
            root.generated_accumulator && state &&
            state->bitWidth == root.bit_width &&
            state->bitOffset <= plan->state_bit_count &&
            root.bit_width <= plan->state_bit_count - state->bitOffset;
        bool generatedBatchRoot = generatedRoot && root.bit_width == 256 &&
                                  context->staticNBARootHasFanout[index] == 0;
        context->nativeScheduleGeneratedBatchEligible &= generatedBatchRoot;
        if (generatedRoot)
          context->nativeScheduleGeneratedNBAOffsets[index] = state->bitOffset;
        size_t words = static_cast<size_t>((root.bit_width + 63) / 64);
        StaticNBAAccumulator accumulator;
        accumulator.value.assign(words, 0);
        accumulator.unknown.assign(words, 0);
        accumulator.writeMask.assign(words, 0);
        accumulator.changed.assign(words, 0);
        accumulator.posedge.assign(words, 0);
        accumulator.negedge.assign(words, 0);
        context->staticNBAAccumulators.push_back(std::move(accumulator));
        if (root.generated_accumulator)
          *root.generated_accumulator = {};
      }
      context->nativeScheduleNBASiteIndex.clear();
      uint64_t denseSiteLimit = std::min<uint64_t>(
          UINT64_C(1) << 20,
          std::max<uint64_t>(1024, nbaSiteCount <= UINT64_MAX / 4
                                       ? nbaSiteCount * 4
                                       : UINT64_MAX));
      if (nbaSiteCount != 0 &&
          nbaSites[nbaSiteCount - 1].site <= denseSiteLimit) {
        context->nativeScheduleNBASiteIndex.assign(
            static_cast<size_t>(nbaSites[nbaSiteCount - 1].site) + 1,
            UINT32_MAX);
        for (uint64_t index = 0; index != nbaSiteCount; ++index)
          context->nativeScheduleNBASiteIndex[nbaSites[index].site] =
              nbaSites[index].root;
      }
    }
    OBELISK_RT_CATCH_ALL {
      std::lock_guard<std::mutex> registryLock(nativeScheduleRegistryMutex);
      installedNativeScheduleStates.erase(plan->mutable_state);
      OBELISK_RT_RETHROW;
    }
    // Generated promotion state is storage owned by the plan image, not by a
    // runtime context.  A plan may therefore be installed again after the
    // previous context is destroyed.  Reset its latch and all selected routes
    // before making the plan visible to this fresh context.
    if (plan->promotion_invalidate)
      plan->promotion_invalidate();
    if (context->nativeStateSpecializationFast) {
      *context->nativeStateSpecializationFast = 0;
      context->nativeStateSpecializationFast = nullptr;
    }
    context->nativeSchedulePlan = plan;
    if (plan->specialization_fast)
      *plan->specialization_fast = 0;
    context->nativeScheduleNBARoots = nbaRoots;
    context->nativeScheduleNBARootCount = nbaRootCount;
    context->nativeScheduleNBASites = nbaSites;
    context->nativeScheduleNBASiteCount = nbaSiteCount;
    context->nativeScheduleFanoutEntries = fanoutEntries;
    context->nativeScheduleFanoutEntryCount = fanoutEntryCount;
    context->nativeScheduleActorRoots = actorRoots;
    context->nativeScheduleActorRootCount = actorRootCount;
    context->nativeScheduleClockIngressPending = false;
    context->nativeScheduleDirectActorSlot = UINT32_MAX;
    context->nativeScheduleDeoptimized = false;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_activate_clock_kernel(
    obelisk_rt_context *context, uint32_t kernel, uint32_t mergedBit) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    if (!plan)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (kernel >= plan->clock_kernel_count)
      return OBELISK_RT_INVALID_ARGUMENT;
    const obelisk_rt_native_clock_kernel &record = plan->clock_kernels[kernel];
    uint32_t word = mergedBit / 64;
    if (word >= record.ingress_word_count)
      return OBELISK_RT_INVALID_ARGUMENT;
    // The scheduler mutex serializes both leaf and derived-index updates.
    // Parallel producers must merge private sets at this boundary.
    obelisk::runtime::publishClockKernelReady(record, mergedBit);
    context->nativeScheduleClockIngressPending = true;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_run_clock_coordinator(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_native_schedule_run run = nullptr;
  obelisk_rt_native_timeslot_coordinator coordinator = nullptr;
  void *mutableState = nullptr;
  bool hasDirectFragments = false;
  OBELISK_RT_TRY {
    {
      ContextMutexLock lock(context);
      const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
      if (!plan || !plan->timeslot_coordinator || !plan->run)
        return OBELISK_RT_INVALID_LIFECYCLE;
      // A deposit from an executing runtime checkpoint has already published
      // its ingress. The enclosing scheduler must settle it after the actor
      // returns; recursively entering run_until here can replay that actor or
      // spin on its still-active handoff. This also preserves statement order
      // for multiple writes in one VPI callback.
      if (context->nativeScheduleRunning || context->activeNativeProcess ||
          context->vpiTimeCallbackActive)
        return OBELISK_RT_OK;
      run = plan->run;
      coordinator = plan->timeslot_coordinator;
      mutableState = plan->mutable_state;
      for (uint64_t index = 0; index != plan->merged_fragment_count; ++index)
        hasDirectFragments |= plan->merged_fragments[index].execute != nullptr;
      // The metadata-only entry consumes this ingress now. Clear before the
      // callback so reentry cannot replay it, while a new publication made
      // by the callback remains pending for the enclosing shared driver.
      if (!hasDirectFragments)
        context->nativeScheduleClockIngressPending = false;
    }
    // Generated coordinators call scheduler services and therefore acquire
    // the context mutex themselves.  Never retain it across this boundary.
    // Legacy/metadata-only plans use the coordinator as their complete
    // external-deposit handoff. A plan with executable fragments must enter
    // through `run`, which establishes the trusted transaction before the
    // generated coordinator calls clean bodies.
    return hasDirectFragments ? run(mutableState, context)
                              : coordinator(mutableState, context);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_direct_fragment_enter(
    obelisk_rt_context *context, uint32_t actorSlot, uint32_t continuation,
    obelisk_rt_process_instance_v1 **outInstance) {
  if (!context || !outInstance)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outInstance = nullptr;
  if (activeNativeAOTContext != context || lockedNativeAOTContext != context ||
      context->nativeScheduleDirectActorSlot != UINT32_MAX ||
      context->activeNativeProcess ||
      actorSlot >= context->nativeScheduleActors.size())
    return OBELISK_RT_INVALID_LIFECYCLE;
  obelisk_rt_process_instance_v1 *actor =
      context->nativeScheduleActors[actorSlot];
  size_t index = context->nativeScheduleActorIndices[actorSlot];
  if (!actor || actor->continuation != continuation ||
      index >= context->scheduledProcesses.size())
    return OBELISK_RT_INVALID_CONTINUATION;
  ScheduledProcess &scheduled = context->scheduledProcesses[index];
  if (scheduled.instance != actor || !scheduled.started ||
      scheduled.aotActorSlot != actorSlot || scheduled.signalTriggered ||
      !scheduled.controls.empty() ||
      (scheduled.suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
       scheduled.suspendKind != OBELISK_RT_SUSPEND_EDGE &&
       scheduled.suspendKind != OBELISK_RT_SUSPEND_OBSERVER))
    return OBELISK_RT_INVALID_LIFECYCLE;
  // The generated ready bit selected this activation and was consumed before
  // entering the wrapper. Keep the runtime occurrence bit clear while the
  // body runs so a transition published by the body can represent a distinct
  // activation of the same wait.
  actor->context = context;
  actor->lifecycle = OBELISK_RT_PROCESS_EXECUTING;
  context->nativeScheduleDirectActorSlot = actorSlot;
  context->activeNativeProcess = actor;
  context->activeHomeRegion = scheduled.homeRegion;
  context->activeExecRegion = scheduled.queuedRegion;
  context->activeLogicalProcessToken =
      kNativeLogicalProcessTag | scheduled.token;
  context->activeProgramOwner = scheduled.programOwner;
  context->controlEscapePending = false;
  context->activeLogicalProcessParent = scheduled.parent;
  context->activeWaitOrderFailed = false;
  obelisk_rt_flush_deferred_immediate_reports_unlocked(
      context, context->activeLogicalProcessToken);
  *outInstance = actor;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_direct_fragment_leave(obelisk_rt_context *context,
                                              uint32_t actorSlot) {
  if (!context || activeNativeAOTContext != context ||
      lockedNativeAOTContext != context ||
      context->nativeScheduleDirectActorSlot != actorSlot ||
      actorSlot >= context->nativeScheduleActors.size())
    return OBELISK_RT_INVALID_LIFECYCLE;
  obelisk_rt_process_instance_v1 *actor =
      context->nativeScheduleActors[actorSlot];
  size_t index = context->nativeScheduleActorIndices[actorSlot];
  if (!actor || context->activeNativeProcess != actor ||
      index >= context->scheduledProcesses.size())
    return OBELISK_RT_INVALID_LIFECYCLE;
  actor->context = nullptr;
  actor->lifecycle = OBELISK_RT_PROCESS_SUSPENDED;
  // Do not clear signalTriggered here: the direct body can update a signal in
  // its own event expression before suspending on the same wait again. Such a
  // publication is the next activation, not the one consumed on entry.
  context->nativeScheduleDirectActorSlot = UINT32_MAX;
  context->activeNativeProcess = nullptr;
  context->activeHomeRegion = UINT32_MAX;
  context->activeExecRegion = UINT32_MAX;
  context->activeLogicalProcessToken = 0;
  context->activeProgramOwner = 0;
  context->controlEscapePending = false;
  context->activeLogicalProcessParent = 0;
  context->activeWaitOrderFailed = false;
  if (context->schedulerSlotProgress == UINT64_MAX) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return context->schedulerStatus;
  }
  ++context->schedulerSlotProgress;
  ++context->signalDiagnostics.aotNodeExecutions;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_add_aot(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t flags, uint32_t actorSlot, uint32_t initialRank,
    const uint32_t *continuations, const uint32_t *ranks,
    uint32_t continuationCount, const uint32_t *bytecodeContinuations,
    uint32_t bytecodeContinuationCount) {
  if (!context || !instance)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (bytecodeContinuationCount != 0 && !bytecodeContinuations)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (bytecodeContinuationCount != 0 && (instance->descriptor->available_tiers &
                                         OBELISK_RT_TIER_MASK_BYTECODE) == 0)
    return OBELISK_RT_TIER_UNAVAILABLE;
  for (uint32_t index = 0; index != bytecodeContinuationCount; ++index)
    if (!validContinuation(*instance->descriptor->frame_layout,
                           bytecodeContinuations[index]) ||
        (index != 0 &&
         bytecodeContinuations[index - 1] >= bytecodeContinuations[index]))
      return OBELISK_RT_INVALID_CONTINUATION;
  ContextTransaction transaction(context);
  const obelisk_rt_native_schedule_plan *plan = nullptr;
  {
    ContextMutexLock lock(context);
    plan = context->nativeSchedulePlan;
    if (!plan || context->nativeScheduleDeoptimized ||
        actorSlot >= context->nativeScheduleActors.size())
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (context->nativeScheduleActors[actorSlot])
      return OBELISK_RT_INVALID_ARGUMENT;
    if (uint64_t token = context->nativeScheduleActorTokens[actorSlot])
      if (ScheduledProcess *owner = findScheduledProcess(context, token);
          owner && owner->instance)
        return OBELISK_RT_INVALID_ARGUMENT;
  }
  obelisk_rt_status status = obelisk_rt_v1_scheduler_add_planned(
      context, instance, flags, initialRank, continuations, ranks,
      continuationCount);
  if (status != OBELISK_RT_OK)
    return status;
  auto rollback = [&] {
    ContextMutexLock lock(context);
    auto found = std::find_if(context->scheduledProcesses.begin(),
                              context->scheduledProcesses.end(),
                              [&](const ScheduledProcess &process) {
                                return process.instance == instance;
                              });
    if (found == context->scheduledProcesses.end())
      return;
    obelisk_rt_release_controls_unlocked(context, found->controls);
    context->nativePollCandidates.erase(found->token);
    context->scheduledProcessIndices.erase(found->token);
    context->scheduledProcesses.erase(found);
    rebuildNativeSchedulerIndexUnlocked(context);
  };
  uint64_t actorToken = 0;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    auto found = std::find_if(context->scheduledProcesses.begin(),
                              context->scheduledProcesses.end(),
                              [&](const ScheduledProcess &process) {
                                return process.instance == instance;
                              });
    if (found == context->scheduledProcesses.end())
      status = OBELISK_RT_INVALID_LIFECYCLE;
    else {
      if (bytecodeContinuationCount == 0)
        found->bytecodeContinuations.clear();
      else
        found->bytecodeContinuations.assign(bytecodeContinuations,
                                            bytecodeContinuations +
                                                bytecodeContinuationCount);
      found->aotActorSlot = actorSlot;
      actorToken = found->token;
    }
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    status = OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL { status = OBELISK_RT_INVALID_ARGUMENT; }
  if (status != OBELISK_RT_OK) {
    rollback();
    return status;
  }
  status = plan->bind(plan->mutable_state, context, actorSlot, instance);
  bool bound = status == OBELISK_RT_OK;
  if (bound) {
    ContextMutexLock lock(context);
    ScheduledProcess *scheduled = findScheduledProcess(context, actorToken);
    if (context->nativeSchedulePlan != plan ||
        context->nativeScheduleActors[actorSlot] || !scheduled ||
        scheduled->instance != instance) {
      status = OBELISK_RT_INVALID_LIFECYCLE;
    } else {
      auto index = context->scheduledProcessIndices.find(actorToken);
      if (index == context->scheduledProcessIndices.end()) {
        status = OBELISK_RT_INVALID_LIFECYCLE;
      } else {
        context->nativeScheduleActors[actorSlot] = instance;
        context->nativeScheduleActorTokens[actorSlot] = actorToken;
        context->nativeScheduleActorIndices[actorSlot] = index->second;
        if (!markNativeAOTActorReadyUnlocked(context, actorSlot))
          status = OBELISK_RT_INVALID_CONTINUATION;
        else
          return OBELISK_RT_OK;
      }
    }
  }
  if (bound) {
    obelisk_rt_status clearStatus =
        plan->bind(plan->mutable_state, context, actorSlot, nullptr);
    if (clearStatus != OBELISK_RT_OK)
      status = clearStatus;
    ContextMutexLock lock(context);
    context->nativeScheduleActors[actorSlot] = nullptr;
    context->nativeScheduleActorTokens[actorSlot] = 0;
    context->nativeScheduleActorIndices[actorSlot] = SIZE_MAX;
    removeNativeAOTDeadlineUnlocked(context, actorSlot);
  }
  // Roll back the generic ownership record as one transaction. The caller
  // retains ownership when add_aot fails.
  rollback();
  return status;
}

extern "C" uint64_t obelisk_rt_v1_scheduler_process_token(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance) {
  if (!context || !instance)
    return 0;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    for (const ScheduledProcess &process : context->scheduledProcesses)
      if (process.instance == instance)
        return OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | process.token;
  }
  OBELISK_RT_CATCH_ALL {}
  return 0;
}

extern "C" uint64_t obelisk_rt_v1_process_current(obelisk_rt_context *context) {
  if (!context)
    return 0;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    return context->activeLogicalProcessToken;
  }
  OBELISK_RT_CATCH_ALL { return 0; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_process_status(obelisk_rt_context *context,
                             uint64_t logicalProcess,
                             obelisk_rt_process_state *outState) {
  if (!context || logicalProcess == 0 || !outState)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outState = OBELISK_RT_PROCESS_FINISHED;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if ((logicalProcess & OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG) != 0) {
      uint64_t token = logicalProcess & ~OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG;
      if (context->killedNativeProcesses.count(token) != 0) {
        *outState = OBELISK_RT_PROCESS_KILLED;
        return OBELISK_RT_OK;
      }
      if (context->terminatedNativeProcesses.count(token) != 0)
        return OBELISK_RT_OK;
      if (context->activeLogicalProcessToken == logicalProcess) {
        *outState = OBELISK_RT_PROCESS_RUNNING;
        return OBELISK_RT_OK;
      }
      ScheduledProcess *process = findScheduledProcess(context, token);
      if (!process || !process->instance)
        return OBELISK_RT_INVALID_HANDLE;
      *outState = process->explicitlySuspended
                      ? OBELISK_RT_PROCESS_EXPLICITLY_SUSPENDED
                  : process->suspendKind != OBELISK_RT_SUSPEND_NONE
                      ? OBELISK_RT_PROCESS_WAITING
                      : OBELISK_RT_PROCESS_RUNNING;
      return OBELISK_RT_OK;
    }
    if (context->killedDesignTasks.count(logicalProcess) != 0) {
      *outState = OBELISK_RT_PROCESS_KILLED;
      return OBELISK_RT_OK;
    }
    if (context->terminatedDesignTasks.count(logicalProcess) != 0)
      return OBELISK_RT_OK;
    if (context->activeLogicalProcessToken == logicalProcess) {
      *outState = OBELISK_RT_PROCESS_RUNNING;
      return OBELISK_RT_OK;
    }
    auto indexed = context->scheduledDesignTaskIndices.find(logicalProcess);
    if (indexed == context->scheduledDesignTaskIndices.end() ||
        indexed->second >= context->scheduledDesignTasks.size())
      return OBELISK_RT_INVALID_HANDLE;
    const ScheduledDesignTask &task =
        context->scheduledDesignTasks[indexed->second];
    if (task.id != logicalProcess)
      return OBELISK_RT_INVALID_HANDLE;
    if (task.terminated)
      return OBELISK_RT_OK;
    *outState = task.explicitlySuspended
                    ? OBELISK_RT_PROCESS_EXPLICITLY_SUSPENDED
                : task.suspendKind != OBELISK_RT_SUSPEND_NONE
                    ? OBELISK_RT_PROCESS_WAITING
                    : OBELISK_RT_PROCESS_RUNNING;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_monitor_register(obelisk_rt_context *context,
                               uint64_t processToken, uint32_t designProcess) {
  if (!context || processToken == 0 || designProcess > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    uint64_t replacement =
        designProcess ? processToken
                      : OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | processToken;
    uint64_t previous = context->monitorLogicalProcessToken;
    context->monitorLogicalProcessToken = replacement;
    context->monitorEnabled = true;
    // A newly registered argument list always reports once.
    context->monitorReported = false;
    context->monitorReport.clear();
    if (previous != replacement)
      wakeMonitorProcessUnlocked(context, previous);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_monitor_register_logical(obelisk_rt_context *context,
                                       uint64_t logicalProcess) {
  if (logicalProcess == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  bool native = (logicalProcess & OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG) != 0;
  uint64_t token = native
                       ? logicalProcess & ~OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG
                       : logicalProcess;
  return obelisk_rt_v1_monitor_register(context, token, native ? 0 : 1);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_monitor_control(obelisk_rt_context *context, uint32_t enabled) {
  if (!context || enabled > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    context->monitorEnabled = enabled != 0;
    if (enabled)
      wakeMonitorProcessUnlocked(context, context->monitorLogicalProcessToken);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" uint32_t obelisk_rt_v1_monitor_current(obelisk_rt_context *context) {
  if (!context)
    return 0;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    return context->activeLogicalProcessToken != 0 &&
                   context->activeLogicalProcessToken ==
                       context->monitorLogicalProcessToken
               ? 1
               : 0;
  }
  OBELISK_RT_CATCH_ALL { return 0; }
}

extern "C" void obelisk_rt_v1_scheduler_fail(obelisk_rt_context *context,
                                             obelisk_rt_status status) {
  if (!context || status == OBELISK_RT_OK)
    return;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (context->schedulerStatus == OBELISK_RT_OK)
      context->schedulerStatus = status;
  }
  OBELISK_RT_CATCH_ALL {}
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_register_static(obelisk_rt_context *context,
                                           uint32_t id, uint64_t bitOffset,
                                           uint64_t bitWidth) {
  if (!context || id == 0 || id > OBELISK_RT_STABLE_HANDLE_MAX_STATIC_ID ||
      bitWidth == 0 || bitOffset > UINT64_MAX - bitWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    NativeStaticState state{bitOffset, bitWidth};
    auto [entry, inserted] = context->nativeStaticStates.emplace(id, state);
    if (!inserted && (entry->second.bitOffset != bitOffset ||
                      entry->second.bitWidth != bitWidth)) {
      context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
      return OBELISK_RT_INVALID_ARGUMENT;
    }
    if (inserted)
      context->nativeStaticStateRangesValid = false;
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
obelisk_rt_v1_native_state_sync(obelisk_rt_context *context, uint8_t *value,
                                uint8_t *unknown, uint64_t bitCount) {
  if (!context || !value || !unknown)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (!context->execution || context->execution->state_bit_count > bitCount ||
        (context->execution->state_bit_count + 7) / 8 != (bitCount + 7) / 8 ||
        !validNativeStatePlanesUnlocked(context, value, unknown,
                                        context->execution->state_bit_count))
      return OBELISK_RT_LAYOUT_MISMATCH;
    for (const auto &[id, state] : context->nativeStaticStates)
      if (state.bitOffset > context->execution->state_bit_count ||
          state.bitWidth >
              context->execution->state_bit_count - state.bitOffset)
        return OBELISK_RT_LAYOUT_MISMATCH;
    if (!importNativeStatePlanesUnlocked(context, value, unknown,
                                         context->execution->state_bit_count))
      return OBELISK_RT_LAYOUT_MISMATCH;
    context->nativeStateValue = value;
    context->nativeStateUnknown = unknown;
    context->nativeStateBitCount = bitCount;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_DESIGN; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_bind_specialization(obelisk_rt_context *context,
                                               uint32_t *fast) {
  if (!context || !fast)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextMutexLock lock(context);
  if (context->nativeSchedulePlan || !context->nativeStateValue ||
      !context->nativeStateUnknown || !context->nativeStateBitCount ||
      (context->nativeStateSpecializationFast &&
       context->nativeStateSpecializationFast != fast))
    return OBELISK_RT_INVALID_LIFECYCLE;
  *fast = 0;
  context->nativeStateSpecializationFast = fast;
  refreshNativeStaticSpecializationFastUnlocked(context);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_bind_continuous(obelisk_rt_context *context,
                                           uint8_t **value, uint8_t **unknown,
                                           uint8_t **mask) {
  if (!context || !value || !unknown || !mask)
    return OBELISK_RT_INVALID_ARGUMENT;
  *value = *unknown = *mask = nullptr;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (!context->nativeStateValue || !context->nativeStateUnknown ||
        !context->nativeStateBitCount)
      return OBELISK_RT_INVALID_LIFECYCLE;
    size_t limbs = context->stateValue.size();
    context->continuousValue.resize(limbs, 0);
    context->continuousUnknown.resize(limbs, 0);
    context->continuousMask.resize(limbs, 0);
    *value = reinterpret_cast<uint8_t *>(context->continuousValue.data());
    *unknown = reinterpret_cast<uint8_t *>(context->continuousUnknown.data());
    *mask = reinterpret_cast<uint8_t *>(context->continuousMask.data());
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_DESIGN; }
}

extern "C" void obelisk_rt_v1_scheduler_notify(obelisk_rt_context *context) {
  if (!context)
    return;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  }
  OBELISK_RT_CATCH_ALL {}
}

void latchSchedulerErrorUnlocked(obelisk_rt_context *context) noexcept {
  context->schedulerFinishStatus = OBELISK_RT_FATAL;
}

static void requestExplicitFinishUnlocked(obelisk_rt_context *context,
                                          uint32_t verbosity,
                                          obelisk_rt_status status) {
  if (context->schedulerRunningFinals)
    context->schedulerFinalsAborted = true;
  context->schedulerFinishRequested = true;
  context->nativePeriodicTerminationRequested = 1;
  context->schedulerFinishVerbosity = verbosity;
  if (status != OBELISK_RT_OK)
    context->schedulerFinishStatus = status;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_finish(obelisk_rt_context *context,
                               uint32_t verbosity) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    requestExplicitFinishUnlocked(context, verbosity, OBELISK_RT_OK);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_program_exit(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    uint64_t owner = 0;
    uint64_t current = 0;
    std::vector<uint64_t> members;
    {
      ContextMutexLock lock(context);
      owner = context->activeProgramOwner;
      current = context->activeLogicalProcessToken;
      if (!owner || !current) {
        setLastErrorUnlocked(
            context,
            "$exit is only valid in a thread owned by a program instance");
        return OBELISK_RT_INVALID_LIFECYCLE;
      }
      auto found = context->programProcesses.find(owner);
      if (found == context->programProcesses.end() ||
          found->second.count(current) == 0)
        return OBELISK_RT_INVALID_LIFECYCLE;
      members.reserve(found->second.size());
      for (uint64_t member : found->second)
        if (member != current)
          members.push_back(member);
      members.push_back(current);
    }
    for (uint64_t member : members) {
      obelisk_rt_process_control_disposition disposition =
          OBELISK_RT_PROCESS_CONTROL_CONTINUE;
      obelisk_rt_status status = obelisk_rt_v1_process_control(
          context, member, OBELISK_RT_PROCESS_CONTROL_KILL, &disposition);
      if (status != OBELISK_RT_OK)
        return status;
    }
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_stop(obelisk_rt_context *context, uint32_t verbosity) {
  // Standalone executables have no interactive command loop in which a
  // stopped design can remain suspended. Keep the request distinct at the ABI
  // boundary, but use the documented batch-mode termination policy.
  return obelisk_rt_v1_scheduler_finish(context, verbosity);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_fatal(obelisk_rt_context *context, uint32_t verbosity) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    requestExplicitFinishUnlocked(context, verbosity, OBELISK_RT_FATAL);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_error(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    latchSchedulerErrorUnlocked(context);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" uint32_t
obelisk_rt_v1_scheduler_termination_requested(obelisk_rt_context *context) {
  if (!context)
    return 0;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    return context->schedulerFinishRequested ? 1u : 0u;
  }
  OBELISK_RT_CATCH_ALL { return 0; }
}

extern "C" uint64_t obelisk_rt_v1_scheduler_time(obelisk_rt_context *context) {
  if (!context)
    return 0;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    return context->schedulerTime;
  }
  OBELISK_RT_CATCH_ALL { return 0; }
}

// Capture the canonical sampled plane before any executable region, then
// publish one private event to sampled computed observers. The event is
// enabled lazily by observer registration, so designs without asynchronous
// sampled conditions pay no per-slot observer-scan cost.
obelisk_rt_status runPreponedHooks(obelisk_rt_context *context) {
  obelisk_rt_status status = obelisk_rt_capture_preponed_unlocked(context);
  if (status != OBELISK_RT_OK || !context->preponedObserverPresent)
    return status;
  OBELISK_RT_TRY {
    EventState &event =
        context->events[OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT];
    if (++event.generation == 0)
      event.generation = 1;
    event.lastTriggeredTime = context->schedulerTime;
    if (!obelisk_rt_notify_event_order_waiters_unlocked(
            context, OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT))
      return context->schedulerStatus;
    if (!obelisk_rt_notify_observer_event_unlocked(
            context, OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT))
      return context->schedulerStatus;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

obelisk_rt_status enterSchedulerTimeSlotUnlocked(obelisk_rt_context *context) {
  if (context->schedulerPreponedTime == context->schedulerTime)
    return OBELISK_RT_OK;
  // IEEE 1800-2023 4.4-4.5, 16.5.1: sample before executable regions and
  // retain that sample through all Active/NBA iterations and tier changes.
  obelisk_rt_status status = runPreponedHooks(context);
  if (status != OBELISK_RT_OK) {
    context->schedulerStatus = status;
    return status;
  }
  context->schedulerPreponedTime = context->schedulerTime;
  context->schedulerSlotProgress = 0;
  if (context->staticNBASlowRootsPresent) {
    std::fill(context->staticNBASlowRoots.begin(),
              context->staticNBASlowRoots.end(), uint8_t{0});
    context->staticNBASlowRootsPresent = false;
  }
  refreshNativeStaticSpecializationFastUnlocked(context);
  return OBELISK_RT_OK;
}

uint32_t nextDueNBABarrierRegionUnlocked(const obelisk_rt_context *context,
                                         bool includeGenerated) {
  uint32_t barrierRegion = UINT32_MAX;
  auto considerBarrier = [&](const auto &entries) {
    for (const auto &entry : entries)
      if (entry.dueTime <= context->schedulerTime)
        barrierRegion = std::min(barrierRegion, entry.execRegion);
  };
  for (const ScheduledNBA &entry : context->scheduledNBAs)
    if (!entry.cancelled && entry.dueTime <= context->schedulerTime)
      barrierRegion = std::min(barrierRegion, entry.execRegion);
  if (!context->scheduledInertialPathNBAs.empty() &&
      context->scheduledInertialPathNBAs.begin()->first.first <=
          context->schedulerTime)
    barrierRegion = std::min(barrierRegion,
                             static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE));
  considerBarrier(context->scheduledManagedNBAs);
  considerBarrier(context->scheduledDesignNBAs);
  considerBarrier(context->scheduledDesignEvents);
  if (const ReplaceableEventFeatureState *replaceable =
          obelisk_rt_replaceable_events(context);
      replaceable && !replaceable->calendar.empty() &&
      replaceable->calendar.begin()->first.first <= context->schedulerTime)
    barrierRegion = std::min(barrierRegion,
                             replaceable->calendar.begin()->second.execRegion);
  if (!context->scheduledPassSwitchEvents.empty() &&
      context->scheduledPassSwitchEvents.begin()->first.first <=
          context->schedulerTime)
    barrierRegion = std::min(barrierRegion,
                             static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE));
  barrierRegion = std::min(
      barrierRegion, nextDueNativeNBABarrierRegionUnlocked(context,
                                                          includeGenerated));
  return barrierRegion;
}

uint32_t nativeAOTContinuationRank(const ScheduledProcess &scheduled,
                                   uint32_t continuation) {
  if (scheduled.continuationRanks.empty())
    return scheduled.scheduleRank;
  if (scheduled.continuationRanks.size() == 1) {
    if (scheduled.continuationRanks.front().first == continuation)
      return scheduled.continuationRanks.front().second;
    return scheduled.scheduleRank;
  }
  auto rank = std::lower_bound(
      scheduled.continuationRanks.begin(), scheduled.continuationRanks.end(),
      continuation,
      [](const std::pair<uint32_t, uint32_t> &entry, uint32_t continuation) {
        return entry.first < continuation;
      });
  if (rank != scheduled.continuationRanks.end() && rank->first == continuation)
    return rank->second;
  return scheduled.scheduleRank;
}

void updateNativeAOTContinuationRank(ScheduledProcess &scheduled,
                                     uint32_t continuation) {
  // Nested task continuations are not IDs in the suspended caller's rank
  // table. They retain their logical process's ordering until that caller
  // returns to its own generated continuation.
  if (scheduled.suspendedAOTActorSlot != UINT32_MAX)
    return;
  scheduled.scheduleRank = nativeAOTContinuationRank(scheduled, continuation);
}

obelisk_rt_status
suspendNativeAOTTaskCallerUnlocked(obelisk_rt_context *context,
                                   ScheduledProcess &scheduled) {
  uint32_t slot = scheduled.aotActorSlot;
  if (slot == UINT32_MAX)
    return OBELISK_RT_OK;
  const auto *plan = context->nativeSchedulePlan;
  if (!plan || slot >= context->nativeScheduleActors.size() ||
      context->nativeScheduleActors[slot] != scheduled.instance ||
      scheduled.suspendedAOTActorSlot != UINT32_MAX)
    return OBELISK_RT_INVALID_LIFECYCLE;
  auto status = plan->bind(plan->mutable_state, context, slot, nullptr);
  if (status != OBELISK_RT_OK)
    return status;
  // IEEE 1800-2023 13.3: the caller resumes after its task completes. Keep
  // its generated frame layout and continuation inventory out of the task's
  // descriptor execution, including during a disturbed-schedule handoff.
  scheduled.suspendedAOTActorSlot = slot;
  scheduled.aotActorSlot = UINT32_MAX;
  context->nativeScheduleActors[slot] = nullptr;
  // The token reserves this identity while the binding is parked. Remove
  // the caller's old waits; the task registers its own waits in the same loop.
  context->nativeScheduleActorTokens[slot] = scheduled.token;
  if (slot < context->nativeScheduleActorNodes.size())
    for (auto [continuation, node] : context->nativeScheduleActorNodes[slot]) {
      (void)continuation;
      clearNativeAOTNodeReadyUnlocked(context, node);
    }
  removeNativeAOTDeadlineUnlocked(context, slot);
  return OBELISK_RT_OK;
}

bool hasSameDirectSignalWait(const ScheduledProcess &scheduled,
                             const obelisk_rt_wait_record_v1 *wait) {
  if (!wait || !scheduled.signalLatch ||
      scheduled.signalSubscriptions.size() != wait->count)
    return false;
  const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
  bool suppressActiveSelf =
      (wait->flags & OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF) != 0;
  for (uint32_t index = 0; index != wait->count; ++index) {
    const SignalSubscription *subscription =
        scheduled.signalSubscriptions[index].get();
    if (entries[index].stable_id == UINT64_MAX) {
      if (subscription)
        return false;
      continue;
    }
    if (!subscription || subscription->stableID != entries[index].stable_id ||
        subscription->bitWidth != entries[index].reserved ||
        subscription->edge != entries[index].edge ||
        subscription->suppressActiveSelf != suppressActiveSelf ||
        subscription->target != SignalSubscription::NativeDirectWait)
      return false;
  }
  return true;
}

obelisk_rt_status
adoptScheduledSuspendUnlocked(obelisk_rt_context *context,
                              ScheduledProcess &scheduled,
                              const obelisk_rt_fragment_action_v1 &action) {
  scheduled.suspendKind = action.suspend_kind;
  scheduled.waitOffset = action.payload;
  scheduled.waitSize = action.auxiliary;
  if (action.suspend_kind == OBELISK_RT_SUSPEND_SEMAPHORE) {
    if (context->nextWaitSequence == 0)
      return OBELISK_RT_OUT_OF_RESOURCES;
    scheduled.waitSequence = context->nextWaitSequence++;
  } else {
    scheduled.waitSequence = 0;
  }
  updateNativeAOTContinuationRank(scheduled, action.continuation);
  scheduled.observedEpoch = context->schedulerEpoch;
  scheduled.waitGenerations.clear();
  scheduled.waitOrderIndex = 0;
  scheduled.waitOrderReady = false;
  scheduled.waitOrderFailed = false;
  scheduled.signalTriggered = false;
  scheduled.startupProcess = false;
  scheduled.urgent = false;
  const obelisk_rt_wait_record_v1 *wait = currentWait(scheduled);
  if (!wait && action.suspend_kind != OBELISK_RT_SUSPEND_OBSERVER)
    return OBELISK_RT_INVALID_FRAME;
  bool directSignalSuspend = action.suspend_kind == OBELISK_RT_SUSPEND_CHANGE ||
                             action.suspend_kind == OBELISK_RT_SUSPEND_EDGE;
  if (scheduled.computedObserverWaitRegistered ||
      (!directSignalSuspend && !scheduled.signalSubscriptions.empty()))
    obelisk_rt_unregister_signal_wait_unlocked(
        context, scheduled.signalSubscriptions, scheduled.token, false);
  uint64_t delayPayload =
      action.suspend_kind == OBELISK_RT_SUSPEND_DELAY ? wait->payload : 1;
  if (!obelisk_rt_next_queued_region(scheduled.homeRegion, action.suspend_kind,
                                     delayPayload, action.flags,
                                     scheduled.queuedRegion))
    return OBELISK_RT_INVALID_ARGUMENT;
  if (action.suspend_kind == OBELISK_RT_SUSPEND_EVENT) {
    const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
    scheduled.waitGenerations.reserve(wait->count);
    for (uint32_t index = 0; index != wait->count; ++index) {
      auto event = context->events.find(entries[index].stable_id);
      scheduled.waitGenerations.push_back(
          event == context->events.end() ? 0 : event->second.generation);
    }
  }
  if (action.suspend_kind == OBELISK_RT_SUSPEND_EVENT_ORDER &&
      !obelisk_rt_initialize_event_order_wait_unlocked(
          context, wait, scheduled.waitOrderIndex, scheduled.waitOrderReady,
          scheduled.waitOrderFailed))
    return OBELISK_RT_INVALID_FRAME;
  if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY)
    scheduled.wakeTime = wait->payload > UINT64_MAX - context->schedulerTime
                             ? UINT64_MAX
                             : context->schedulerTime + wait->payload;
  if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY)
    indexScheduledProcessDelayUnlocked(context, scheduled);
  if (action.suspend_kind == OBELISK_RT_SUSPEND_OBSERVER &&
      !obelisk_rt_register_computed_signal_wait_unlocked(
          context, computedWait(scheduled), scheduled.token, false,
          scheduled.signalSubscriptions, scheduled.signalLatch))
    return context->schedulerStatus;
  bool sameSignalWait =
      wait && obelisk_rt_is_clock_occurrence_wait_flags(wait->flags)
          ? obelisk_rt_same_clock_occurrence_wait_unlocked(
                context, wait, scheduled.token, false)
          : hasSameDirectSignalWait(scheduled, wait);
  if (directSignalSuspend && !sameSignalWait &&
      !obelisk_rt_register_signal_wait_unlocked(
          context, wait, scheduled.signalSubscriptions, scheduled.signalLatch,
          scheduled.token, false))
    return context->schedulerStatus;
  return OBELISK_RT_OK;
}

obelisk_rt_status runScheduler(obelisk_rt_context *context,
                               SchedulerRunOptions options) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (options.nativePlan &&
      (!context->nativeSchedulePlan || activeNativeAOTContext != context ||
       lockedNativeAOTContext != context))
    return OBELISK_RT_INVALID_LIFECYCLE;
  NativeReadyPublicationBatch publications;
  struct PublicationBatchScope {
    obelisk_rt_context *context;
    NativeReadyPublicationBatch *previous;
    explicit PublicationBatchScope(obelisk_rt_context *context)
        : context(context), previous(nullptr) {
      ContextMutexLock lock(context);
      previous = context->nativeReadyPublicationBatch;
      if (previous)
        previous->valid = false;
      context->nativeReadyPublicationBatch = nullptr;
    }
    ~PublicationBatchScope() {
      ContextMutexLock lock(context);
      context->nativeReadyPublicationBatch = previous;
    }
  } publicationScope(context);
  constexpr uint64_t maxSlotProgress = UINT64_MAX;
  auto recordSlotProgress = [&]() -> obelisk_rt_status {
    ContextMutexLock lock(context);
    if (context->schedulerSlotProgress == maxSlotProgress) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return context->schedulerStatus;
    }
    ++context->schedulerSlotProgress;
    return OBELISK_RT_OK;
  };
  // A signal publication commonly makes a large cohort ready at once. The
  // generic scheduler used to rescan the remaining unordered candidate set
  // after every member, making one edge cost N + (N-1) + ... readiness tests.
  // Cache startup and signal-ready batches in exact
  // scheduler-key order. Any other readiness source or selection-generation
  // change falls back to the fully general scan below.
  struct CachedNativeReady {
    uint64_t token = 0;
    bool urgent = false;
    size_t urgentDistance = 0;
    uint32_t region = UINT32_MAX;
    uint32_t rank = UINT32_MAX;
    uint64_t insertionSequence = UINT64_MAX;
  };
  struct CachedNativeReadyLater {
    bool operator()(const CachedNativeReady &lhs,
                    const CachedNativeReady &rhs) const {
      if (lhs.urgent != rhs.urgent)
        return !lhs.urgent;
      if (lhs.urgent) {
        if (lhs.urgentDistance != rhs.urgentDistance)
          return lhs.urgentDistance > rhs.urgentDistance;
        return lhs.token > rhs.token;
      }
      return std::tuple{lhs.region, lhs.rank, lhs.insertionSequence,
                        lhs.token} >
             std::tuple{rhs.region, rhs.rank, rhs.insertionSequence, rhs.token};
    }
  };
  // An initial publication batch is sorted once in later-first order and
  // consumed from the back in O(1) per member. The common already-ordered
  // process vector needs only a linear check and reversal. Rare incremental
  // additions use a separate heap so task-call, continue, and spawned-child
  // handling retain the fully general ordering contract.
  std::vector<CachedNativeReady> cachedNativeReady;
  std::vector<CachedNativeReady> cachedNativeDynamicReady;
  std::vector<uint64_t> cachedNativeSlowCandidates;
  CachedNativeReadyLater cachedNativeReadyLater;
  auto pushCachedNativeReady = [&](const CachedNativeReady &ready) {
    ContextMutexLock lock(context);
    if (context->nativeReadyPublicationBatch == &publications &&
        (!publications.queued.insert(ready.token).second || ready.urgent))
      publications.valid = false;
    cachedNativeDynamicReady.push_back(ready);
    std::push_heap(cachedNativeDynamicReady.begin(),
                   cachedNativeDynamicReady.end(), cachedNativeReadyLater);
  };
  auto pushCachedNativeSlowCandidate = [&](uint64_t token) {
    if (context->nativeReadyPublicationBatch == &publications)
      publications.queued.insert(token);
    cachedNativeSlowCandidates.push_back(token);
  };
  auto cachedNativeReadyEmpty = [&] {
    return cachedNativeReady.empty() && cachedNativeDynamicReady.empty();
  };
  auto nextCachedNativeReady = [&]() -> std::pair<CachedNativeReady, bool> {
    bool dynamic = cachedNativeReady.empty() ||
                   (!cachedNativeDynamicReady.empty() &&
                    cachedNativeReadyLater(cachedNativeReady.back(),
                                           cachedNativeDynamicReady.front()));
    return {dynamic ? cachedNativeDynamicReady.front()
                    : cachedNativeReady.back(),
            dynamic};
  };
  auto popCachedNativeReady = [&](bool dynamic) {
    if (context->nativeReadyPublicationBatch == &publications)
      publications.queued.erase(dynamic ? cachedNativeDynamicReady.front().token
                                        : cachedNativeReady.back().token);
    if (!dynamic) {
      cachedNativeReady.pop_back();
      return;
    }
    std::pop_heap(cachedNativeDynamicReady.begin(),
                  cachedNativeDynamicReady.end(), cachedNativeReadyLater);
    cachedNativeDynamicReady.pop_back();
  };
  bool cachedNativeReadyValid = false;
  uint64_t cachedNativeReadyGeneration = 0;
  uint64_t cachedNativeReadyTime = 0;
  bool cachedNativeReadyFinals = false;
  uint32_t cachedUnstartedRegion = UINT32_MAX;
  size_t cachedNativeReadyProcessCount = 0;
  uint64_t cachedNativeReadyLastToken = 0;
  size_t cachedNativeUrgentCount = 0;
  auto clearCachedNativeReady = [&] {
    context->nativeReadyPublicationBatch = nullptr;
    publications.valid = false;
    publications.tokens.clear();
    publications.queued.clear();
    cachedNativeReady.clear();
    cachedNativeDynamicReady.clear();
    cachedNativeSlowCandidates.clear();
    cachedNativeReadyValid = false;
    cachedNativeUrgentCount = 0;
  };
  auto selectionGenerationCompatible = [&] {
    if (context->nativeReadyPublicationBatch == &publications)
      return publications.valid &&
             publications.generation == context->schedulerSelectionGeneration;
    return cachedNativeReadyGeneration == context->schedulerSelectionGeneration;
  };
  auto readmitNativeCandidate = [&](const ScheduledProcess &scheduled,
                                    size_t selectedIndex, bool wasCached) {
    // The selected token was popped from the heap before execution and is
    // therefore absent from both cached containers. A task call, caller
    // return, continue action, or suspend can requeue that same token
    // without changing the process vector or signal generation. Re-admit
    // its finalized state once; mutations outside that narrow shape force
    // an exact rebuild on the next iteration.
    if (wasCached && context->nativePollCandidates.count(scheduled.token)) {
      bool exactProcessCount =
          context->scheduledProcesses.size() == cachedNativeReadyProcessCount;
      bool cacheShapeStable =
          cachedNativeReadyValid &&
          selectionGenerationCompatible() &&
          cachedNativeReadyTime == context->schedulerTime &&
          cachedNativeReadyFinals == context->schedulerRunningFinals &&
          exactProcessCount &&
          (cachedNativeReadyProcessCount == 0 ||
           context->scheduledProcesses[cachedNativeReadyProcessCount - 1]
                   .token == cachedNativeReadyLastToken);
      if (!cacheShapeStable) {
        clearCachedNativeReady();
      } else if (!scheduled.explicitlySuspended &&
                 scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE) {
        // Urgent distances are relative to the cursor at cache creation.
        // A requeued urgent token uses the post-selection cursor, so it is
        // comparable only after every old-cursor urgent entry is gone.
        if (scheduled.urgent && cachedNativeUrgentCount != 0) {
          clearCachedNativeReady();
        } else {
          bool signalResume =
              scheduled.signalTriggered ||
              (scheduled.signalLatch && scheduled.signalLatch->triggered);
          CachedNativeReady ready;
          ready.token = scheduled.token;
          ready.urgent = scheduled.urgent;
          size_t processCount = context->scheduledProcesses.size();
          ready.urgentDistance =
              processCount == 0 ? 0
                                : (selectedIndex + processCount -
                                   context->schedulerCursor % processCount) %
                                      processCount;
          ready.region =
              schedulerOrderingRegion(*context, scheduled, signalResume);
          if (scheduled.prioritySignal && signalResume) {
            ready.rank = 0;
            ready.insertionSequence = 0;
          } else {
            ready.rank = scheduled.scheduleRank;
            ready.insertionSequence = scheduled.insertionSequence;
          }
          pushCachedNativeReady(ready);
          cachedNativeUrgentCount += ready.urgent;
        }
      } else {
        pushCachedNativeSlowCandidate(scheduled.token);
      }
    }
  };
  // Generated execution can leave the descriptor candidate index dormant.
  // Rebuild once at this real boundary, then maintain it on publications and
  // returned actions rather than rescanning every actor after every action.
  if (options.nativePlan)
    rebuildNativeSchedulerIndexUnlocked(context);
  for (;;) {
    {
      ContextMutexLock lock(context);
      if (context->destroyPending)
        return OBELISK_RT_OK;
      if (context->signalDiagnosticsEnabled)
        ++context->signalDiagnostics.schedulerIterations;
      if (context->schedulerStatus != OBELISK_RT_OK)
        return context->schedulerStatus;
      if (context->schedulerPreponedTime != context->schedulerTime) {
        obelisk_rt_status status = enterSchedulerTimeSlotUnlocked(context);
        if (status != OBELISK_RT_OK)
          return status;
      }
      if (!context->schedulerRunningFinals &&
          !context->schedulerFinishRequested &&
          !context->vpiTimeCallbackActive && context->nextVPITimeCallback &&
          *context->nextVPITimeCallback <= context->schedulerTime) {
        obelisk_rt_status status =
            obelisk_rt_vpi_dispatch_time_callbacks_unlocked(context);
        if (status != OBELISK_RT_OK)
          return status;
        continue;
      }
      // No selected index survives across loop iterations. Reentrant scheduler
      // calls during an evaluator do have an outer selected index, so defer
      // compaction until both execution engines are quiescent.
      if (context->schedulerCompactionPending &&
          !context->activeNativeProcess && !context->designTaskExecuting) {
        auto &processes = context->scheduledProcesses;
        // Compact after roughly one quarter of the vector is dead. This keeps
        // scheduler scans bounded without turning sequential process churn
        // into one vector shift per termination.
        size_t processCompactionThreshold = std::max<size_t>(
            1, processes.size() / 4 + (processes.size() % 4 != 0));
        bool compactProcesses =
            context->schedulerDeadProcessCount == processes.size() ||
            context->schedulerDeadProcessCount >= processCompactionThreshold;
        if (compactProcesses && context->schedulerDeadProcessCount != 0) {
          size_t cursor = processes.empty()
                              ? 0
                              : context->schedulerCursor % processes.size();
          size_t adjustedCursor = static_cast<size_t>(
              std::count_if(processes.begin(), processes.begin() + cursor,
                            [](const ScheduledProcess &process) {
                              return process.instance != nullptr;
                            }));
          processes.erase(std::remove_if(processes.begin(), processes.end(),
                                         [](const ScheduledProcess &process) {
                                           return !process.instance;
                                         }),
                          processes.end());
          context->schedulerCursor =
              processes.empty() ? 0 : adjustedCursor % processes.size();
          context->schedulerDeadProcessCount = 0;
          rebuildNativeSchedulerIndexUnlocked(context);
        }

        auto &tasks = context->scheduledDesignTasks;
        if (context->schedulerDeadDesignTaskCount != 0) {
          for (ScheduledDesignTask &task : tasks)
            if (task.terminated)
              context->designTaskFrames.release(std::move(task.frame));
          tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                                     [](const ScheduledDesignTask &task) {
                                       return task.terminated;
                                     }),
                      tasks.end());
          context->schedulerDeadDesignTaskCount = 0;
          context->scheduledDesignTaskIndices.clear();
          context->designPollCandidates.clear();
          context->scheduledDesignTaskIndices.reserve(tasks.size());
          context->designPollCandidates.reserve(tasks.size());
          for (size_t index = 0; index != tasks.size(); ++index) {
            const ScheduledDesignTask &task = tasks[index];
            context->scheduledDesignTaskIndices[task.id] = index;
            if (!obelisk_rt_design_signal_wait_blocked(task))
              context->designPollCandidates.insert(task.id);
          }
        }
        context->schedulerCompactionPending =
            context->schedulerDeadProcessCount != 0;
      }
      if (context->schedulerFinishRequested) {
        context->schedulerRunningFinals = true;
        if (context->schedulerFinalsAborted)
          return context->schedulerFinishStatus;
      }
      if (options.nativePlan) {
        // Retain the existing boundary materialization until native and
        // descriptor accesses share one authoritative plane. Executor choice
        // does not give the adapter a separate queue or region cursor.
        const auto *plan = context->nativeSchedulePlan;
        if (plan->state_bit_count != 0 &&
            (!reconcileNativeDirtyRootsToPlanesUnlocked(context, plan) ||
             !importNativeStatePlanesUnlocked(context, plan->state_value,
                                              plan->state_unknown,
                                              plan->state_bit_count)))
          return OBELISK_RT_LAYOUT_MISMATCH;
      }
      if (context->nativeScheduleClockIngressPending &&
          !context->vpiTimeCallbackActive && !context->schedulerRunningFinals) {
        const auto *plan = context->nativeSchedulePlan;
        if (!plan)
          return OBELISK_RT_INVALID_LIFECYCLE;
        auto coordinator = plan->timeslot_coordinator;
        if (!coordinator)
          return OBELISK_RT_INVALID_LIFECYCLE;
        context->nativeScheduleClockIngressPending = false;
        NativeAOTContextScope aotScope(context);
        NativeAOTMutexScope mutexScope(context);
        obelisk_rt_status status = coordinator(plan->mutable_state, context);
        if (status != OBELISK_RT_OK)
          return status;
        continue;
      }
    }
    uint32_t nativeRegion = UINT32_MAX;
    uint32_t nativeRank = UINT32_MAX;
    uint64_t nativeInsertionSequence = UINT64_MAX;
    size_t nativeCandidateIndex = SIZE_MAX;
    uint64_t nativeCandidateToken = 0;
    size_t nativeScanProcessCount = 0;
    uint64_t nativeScanEpoch = 0;
    uint64_t nativeScanSelectionGeneration = 0;
    uint64_t nativeScanInsertionSequence = 0;
    uint32_t barrierRegion = UINT32_MAX;
    bool covergroupStrobePending = false;
    std::optional<CachedNativeReady> cachedNativeSelection;
    {
      ContextMutexLock lock(context);
      if (context->scheduledProcessIndices.size() !=
          context->scheduledProcesses.size())
        rebuildNativeSchedulerIndexUnlocked(context);
      nativeScanProcessCount = context->scheduledProcesses.size();
      nativeScanEpoch = context->schedulerEpoch;
      nativeScanSelectionGeneration = context->schedulerSelectionGeneration;
      nativeScanInsertionSequence = context->nextProcessInsertionSequence;
      uint32_t activePhase = context->schedulerRunningFinals ? 1u : 0u;
      uint32_t unstartedActorRegion =
          obelisk_rt_unstarted_actor_region(context, activePhase);
      size_t nativeUrgentDistance = SIZE_MAX;
      bool forcedNativeNode = context->nativeScheduleForcedSlot != UINT32_MAX;
      auto classifyNativeToken =
          [&](uint64_t token, bool &signalResume,
              size_t &index) -> std::optional<CachedNativeReady> {
        auto indexed = context->scheduledProcessIndices.find(token);
        if (indexed == context->scheduledProcessIndices.end() ||
            indexed->second >= nativeScanProcessCount)
          return std::nullopt;
        index = indexed->second;
        const ScheduledProcess &candidate = context->scheduledProcesses[index];
        if (context->signalDiagnosticsEnabled && !forcedNativeNode)
          ++context->signalDiagnostics.candidateScans;
        if (!candidate.instance ||
            candidate.phase != (context->schedulerRunningFinals ? 1u : 0u))
          return std::nullopt;
        bool runnable = nativeProcessReady(*context, candidate);
        signalResume =
            candidate.signalTriggered ||
            (candidate.signalLatch && candidate.signalLatch->triggered);
        if (runnable && candidate.queuedRegion >= unstartedActorRegion &&
            signalResume && !candidate.urgent && !candidate.prioritySignal)
          runnable = false;
        if (!runnable)
          return std::nullopt;
        CachedNativeReady ready;
        ready.token = candidate.token;
        ready.urgent = candidate.urgent;
        ready.urgentDistance =
            nativeScanProcessCount == 0
                ? 0
                : (index + nativeScanProcessCount -
                   context->schedulerCursor % nativeScanProcessCount) %
                      nativeScanProcessCount;
        if (candidate.prioritySignal && signalResume) {
          ready.region =
              schedulerOrderingRegion(*context, candidate, signalResume);
          ready.rank = 0;
          ready.insertionSequence = 0;
        } else {
          ready.region =
              schedulerOrderingRegion(*context, candidate, signalResume);
          ready.rank = candidate.scheduleRank;
          ready.insertionSequence = candidate.insertionSequence;
        }
        return ready;
      };
      auto considerNativeReady = [&](const CachedNativeReady &ready,
                                     size_t index) {
        if (ready.urgent) {
          if (ready.urgentDistance < nativeUrgentDistance) {
            nativeUrgentDistance = ready.urgentDistance;
            nativeRegion = 0;
            nativeRank = 0;
            nativeInsertionSequence = 0;
            nativeCandidateIndex = index;
            nativeCandidateToken = ready.token;
          }
          return;
        }
        if (nativeUrgentDistance != SIZE_MAX)
          return;
        auto key =
            std::tuple{ready.region, ready.rank, ready.insertionSequence};
        if (key <
            std::tuple{nativeRegion, nativeRank, nativeInsertionSequence}) {
          nativeRegion = ready.region;
          nativeRank = ready.rank;
          nativeInsertionSequence = ready.insertionSequence;
          nativeCandidateIndex = index;
          nativeCandidateToken = ready.token;
        }
      };
      auto considerNativeToken = [&](uint64_t token) {
        bool signalResume = false;
        size_t index = SIZE_MAX;
        if (auto ready = classifyNativeToken(token, signalResume, index))
          considerNativeReady(*ready, index);
      };
      if (forcedNativeNode) {
        clearCachedNativeReady();
        uint32_t slot = context->nativeScheduleForcedSlot;
        if (slot >= context->nativeScheduleActorTokens.size() ||
            context->nativeScheduleActorTokens[slot] == 0)
          return OBELISK_RT_INVALID_LIFECYCLE;
        considerNativeToken(context->nativeScheduleActorTokens[slot]);
      } else if (context->nativeScheduleProcessFilterActive) {
        clearCachedNativeReady();
        if (context->nativeScheduleForcedProcessToken != 0)
          considerNativeToken(context->nativeScheduleForcedProcessToken);
      } else if (context->nativeScheduleControlOnly) {
        clearCachedNativeReady();
        // A generated control-only step may return only after pending
        // slot-final Clause 31 work. Ordinary actors remain owned by the
        // generated node selected by the caller.
        for (uint64_t token : context->nativePollCandidates) {
          bool signalResume = false;
          size_t index = SIZE_MAX;
          auto ready = classifyNativeToken(token, signalResume, index);
          if (ready && signalResume &&
              obelisk_rt_is_slot_final_clock_occurrence_wait(
                  currentWait(context->scheduledProcesses[index])))
            considerNativeReady(*ready, index);
        }
      } else {
        bool cacheShapeValid =
            cachedNativeReadyValid &&
            selectionGenerationCompatible() &&
            cachedNativeReadyTime == context->schedulerTime &&
            cachedNativeReadyFinals == context->schedulerRunningFinals &&
            cachedUnstartedRegion == unstartedActorRegion &&
            nativeScanProcessCount >= cachedNativeReadyProcessCount &&
            (cachedNativeReadyProcessCount == 0 ||
             context->scheduledProcesses[cachedNativeReadyProcessCount - 1]
                     .token == cachedNativeReadyLastToken);
        if (!cacheShapeValid)
          clearCachedNativeReady();

        // IEEE 1800-2023 4.4--4.7: additions use the same complete ordering
        // key and still yield to NBA/region barriers below. Only previously
        // absent direct signal candidates are journaled; all other changes
        // invalidate the snapshot. Never replay an already queued activation.
        if (cachedNativeReadyValid && !publications.tokens.empty()) {
          for (uint64_t token : publications.tokens) {
            bool signalResume = false;
            size_t index = SIZE_MAX;
            auto ready = classifyNativeToken(token, signalResume, index);
            if (!ready || !signalResume || ready->urgent ||
                publications.queued.count(token)) {
              publications.valid = false;
              break;
            }
            pushCachedNativeReady(*ready);
          }
          if (!publications.valid) {
            clearCachedNativeReady();
          } else {
            publications.tokens.clear();
            cachedNativeReadyGeneration = context->schedulerSelectionGeneration;
          }
        }

        // A detached child is appended after its parent is selected. Admit
        // one urgent startup incrementally; retain any less common appended
        // shape in the small fallback list that is rescanned exactly.
        if (cachedNativeReadyValid &&
            nativeScanProcessCount > cachedNativeReadyProcessCount) {
          bool appendedOrdinaryActor = false;
          for (size_t index = cachedNativeReadyProcessCount;
               index != nativeScanProcessCount; ++index) {
            const ScheduledProcess &candidate =
                context->scheduledProcesses[index];
            if (!candidate.instance ||
                !context->nativePollCandidates.count(candidate.token))
              continue;
            bool signalResume = false;
            size_t classifiedIndex = SIZE_MAX;
            auto ready = classifyNativeToken(candidate.token, signalResume,
                                             classifiedIndex);
            if (ready && !candidate.started && !ready->urgent) {
              appendedOrdinaryActor = true;
              break;
            }
            if (ready && !candidate.started &&
                (!ready->urgent || cachedNativeUrgentCount == 0)) {
              pushCachedNativeReady(*ready);
              ++cachedNativeUrgentCount;
            } else {
              pushCachedNativeSlowCandidate(candidate.token);
            }
          }
          // An ordinary newly spawned actor must take its initial activation
          // ahead of existing same-region signal resumptions. Reclassify the
          // whole set instead of dropping those temporarily blocked resumes
          // from the cached heap.
          if (appendedOrdinaryActor)
            clearCachedNativeReady();
          if (cachedNativeReadyValid) {
            cachedNativeReadyProcessCount = nativeScanProcessCount;
            cachedNativeReadyLastToken =
                nativeScanProcessCount == 0
                    ? 0
                    : context->scheduledProcesses.back().token;
          }
        }

        if (!cachedNativeReadyValid) {
          // Preserve the old allocation-free scan for the overwhelmingly
          // common small process set. The cache starts only when it can
          // amortize heap construction over a substantial ready cohort.
          constexpr size_t minCachedSignalCohort = 16;
          if (context->nativePollCandidates.size() <= minCachedSignalCohort) {
            for (uint64_t token : context->nativePollCandidates)
              considerNativeToken(token);
          } else {
            std::vector<CachedNativeReady> batch;
            auto appendCandidate = [&](uint64_t token) {
              bool signalResume = false;
              size_t index = SIZE_MAX;
              auto ready = classifyNativeToken(token, signalResume, index);
              if (ready)
                considerNativeReady(*ready, index);
              auto indexed = context->scheduledProcessIndices.find(token);
              bool startup =
                  ready && indexed != context->scheduledProcessIndices.end() &&
                  !context->scheduledProcesses[indexed->second].started;
              if (ready && (signalResume || startup))
                batch.push_back(*ready);
              else
                pushCachedNativeSlowCandidate(token);
            };
            // A publication invalidates this cache whenever it wakes another
            // actor. In a large, mostly sleeping design, walking the complete
            // process inventory at every such publication is quadratic in
            // unrelated actors. Enumerate the indexed candidates when sparse;
            // retain the contiguous, usually already ordered walk when dense.
            // The complete scheduler key below makes either traversal order
            // equivalent, including urgent-distance and token tie breakers.
            if (context->nativePollCandidates.size() <
                context->scheduledProcesses.size() / 4) {
              for (uint64_t token : context->nativePollCandidates)
                appendCandidate(token);
              if (context->signalDiagnosticsEnabled)
                context->signalDiagnostics.candidateInventoryVisits +=
                    context->nativePollCandidates.size();
            } else {
              for (const ScheduledProcess &candidate :
                   context->scheduledProcesses)
                if (candidate.instance &&
                    context->nativePollCandidates.count(candidate.token))
                  appendCandidate(candidate.token);
              if (context->signalDiagnosticsEnabled)
                context->signalDiagnostics.candidateInventoryVisits +=
                    context->scheduledProcesses.size();
            }
            if (batch.size() > minCachedSignalCohort) {
              nativeUrgentDistance = SIZE_MAX;
              nativeRegion = UINT32_MAX;
              nativeRank = UINT32_MAX;
              nativeInsertionSequence = UINT64_MAX;
              nativeCandidateIndex = SIZE_MAX;
              nativeCandidateToken = 0;
              bool alreadyEarlierFirst =
                  std::is_sorted(batch.begin(), batch.end(),
                                 [&](const CachedNativeReady &lhs,
                                     const CachedNativeReady &rhs) {
                                   return cachedNativeReadyLater(rhs, lhs);
                                 });
              if (alreadyEarlierFirst)
                std::reverse(batch.begin(), batch.end());
              else
                std::sort(batch.begin(), batch.end(), cachedNativeReadyLater);
              cachedNativeUrgentCount = static_cast<size_t>(std::count_if(
                  batch.begin(), batch.end(),
                  [](const CachedNativeReady &ready) { return ready.urgent; }));
              cachedNativeReady = std::move(batch);
              cachedNativeReadyValid = true;
              cachedNativeReadyGeneration =
                  context->schedulerSelectionGeneration;
              cachedNativeReadyTime = context->schedulerTime;
              cachedNativeReadyFinals = context->schedulerRunningFinals;
              cachedUnstartedRegion = unstartedActorRegion;
              cachedNativeReadyProcessCount = nativeScanProcessCount;
              cachedNativeReadyLastToken =
                  nativeScanProcessCount == 0
                      ? 0
                      : context->scheduledProcesses.back().token;
              // Startup and urgent cohorts retain the general path. Polled
              // waits remain in the exact per-iteration slow scan; include
              // their tokens in membership so a changed slow wait cannot be
              // admitted a second time through the publication journal.
              // No storage or publication work is paid by dormant Tier-1
              // scheduling, or by the small allocation-free ready scanner.
              if (cachedNativeUrgentCount == 0 &&
                  unstartedActorRegion == UINT32_MAX) {
                publications.queued.reserve(cachedNativeReady.size() +
                                            cachedNativeSlowCandidates.size());
                for (const auto &ready : cachedNativeReady)
                  publications.queued.insert(ready.token);
                publications.queued.insert(cachedNativeSlowCandidates.begin(),
                                            cachedNativeSlowCandidates.end());
                publications.generation = context->schedulerSelectionGeneration;
                publications.valid = true;
                context->nativeReadyPublicationBatch = &publications;
              }
            }
          }
        }

        if (cachedNativeReadyValid)
          for (uint64_t token : cachedNativeSlowCandidates)
            considerNativeToken(token);

        while (cachedNativeReadyValid && !cachedNativeReadyEmpty()) {
          auto [ready, dynamicReady] = nextCachedNativeReady();
          auto indexed = context->scheduledProcessIndices.find(ready.token);
          bool signalResume = false;
          size_t index = SIZE_MAX;
          auto current = classifyNativeToken(ready.token, signalResume, index);
          bool same = indexed != context->scheduledProcessIndices.end() &&
                      context->nativePollCandidates.count(ready.token) &&
                      current && current->urgent == ready.urgent &&
                      (ready.urgent ||
                       (current->region == ready.region &&
                        current->rank == ready.rank &&
                        current->insertionSequence == ready.insertionSequence));
          if (!same) {
            popCachedNativeReady(dynamicReady);
            cachedNativeUrgentCount -= ready.urgent;
            // Cached signal/startup readiness is normally monotonic until
            // selection. Preserve any live pollable token whose state was
            // changed by a rarer reentrant operation in the exact slow set;
            // it may become ready again without another generation change.
            if (indexed != context->scheduledProcessIndices.end() &&
                context->nativePollCandidates.count(ready.token) &&
                indexed->second < context->scheduledProcesses.size() &&
                context->scheduledProcesses[indexed->second].instance)
              pushCachedNativeSlowCandidate(ready.token);
            continue;
          }
          considerNativeReady(*current, index);
          if (nativeCandidateToken != ready.token)
            break;
          popCachedNativeReady(dynamicReady);
          cachedNativeUrgentCount -= ready.urgent;
          cachedNativeSelection = ready;
          nativeCandidateIndex = index;
          nativeCandidateToken = ready.token;
          nativeRegion = ready.urgent ? 0 : ready.region;
          nativeRank = ready.urgent ? 0 : ready.rank;
          nativeInsertionSequence = ready.urgent ? 0 : ready.insertionSequence;
          break;
        }
      }
      barrierRegion = nextDueNBABarrierRegionUnlocked(context);
      covergroupStrobePending =
          obelisk_rt_covergroup_strobes_pending_unlocked(context);
    }
    bool cachedCandidateExpected =
        cachedNativeSelection &&
        (std::tuple{nativeRegion, nativeRank, nativeInsertionSequence} ==
             std::tuple{uint32_t{0}, uint32_t{0}, uint64_t{0}} ||
         std::tuple{nativeRegion, nativeRank, nativeInsertionSequence} <
             std::tuple{barrierRegion, uint32_t{0}, uint64_t{0}});
    if (cachedNativeSelection && !cachedCandidateExpected &&
        cachedNativeReadyValid) {
      pushCachedNativeReady(*cachedNativeSelection);
      cachedNativeUrgentCount += cachedNativeSelection->urgent;
      cachedNativeSelection.reset();
      nativeCandidateIndex = SIZE_MAX;
      nativeCandidateToken = 0;
      nativeRegion = UINT32_MAX;
      nativeRank = UINT32_MAX;
      nativeInsertionSequence = UINT64_MAX;
    }
    uint32_t maximumRegion = nativeRegion;
    uint32_t maximumRank = nativeRank;
    uint64_t maximumInsertionSequence = nativeInsertionSequence;
    if (std::tuple{barrierRegion, uint32_t{0}, uint64_t{0}} <
        std::tuple{maximumRegion, maximumRank, maximumInsertionSequence}) {
      maximumRegion = barrierRegion;
      maximumRank = 0;
      maximumInsertionSequence = 0;
    }
    const auto covergroupStrobeKey = std::tuple{
        uint32_t{OBELISK_RT_REGION_POSTPONED}, uint32_t{0}, uint64_t{0}};
    if (covergroupStrobePending &&
        covergroupStrobeKey <
            std::tuple{maximumRegion, maximumRank, maximumInsertionSequence}) {
      maximumRegion = OBELISK_RT_REGION_POSTPONED;
      maximumRank = 0;
      maximumInsertionSequence = 0;
    }
    bool designProgress = false;
    obelisk_rt_status designStatus = obelisk_rt_run_one_design_task(
        context, maximumRegion, maximumRank, maximumInsertionSequence,
        &designProgress);
    if (designStatus != OBELISK_RT_OK)
      return designStatus;
    if (designProgress) {
      if (cachedNativeSelection && cachedNativeReadyValid) {
        pushCachedNativeReady(*cachedNativeSelection);
        cachedNativeUrgentCount += cachedNativeSelection->urgent;
      }
      obelisk_rt_status status = recordSlotProgress();
      if (status != OBELISK_RT_OK)
        return status;
      if (context->nativeScheduleSingleStep)
        return OBELISK_RT_OK;
      continue;
    }
    if (covergroupStrobePending &&
        covergroupStrobeKey <=
            std::tuple{nativeRegion, nativeRank, nativeInsertionSequence} &&
        covergroupStrobeKey <=
            std::tuple{barrierRegion, uint32_t{0}, uint64_t{0}}) {
      if (cachedNativeSelection && cachedNativeReadyValid) {
        pushCachedNativeReady(*cachedNativeSelection);
        cachedNativeUrgentCount += cachedNativeSelection->urgent;
      }
      {
        ContextMutexLock lock(context);
        obelisk_rt_status status =
            obelisk_rt_drain_covergroup_strobes_unlocked(context);
        if (status != OBELISK_RT_OK)
          return status;
      }
      obelisk_rt_status status = recordSlotProgress();
      if (status != OBELISK_RT_OK)
        return status;
      if (context->nativeScheduleSingleStep)
        return OBELISK_RT_OK;
      continue;
    }
    obelisk_rt_process_instance_v1 *selected = nullptr;
    size_t selectedIndex = 0;
    bool selectedResuming = false;
    {
      ContextMutexLock lock(context);
      const size_t processCount = context->scheduledProcesses.size();
      uint32_t selectedRank = UINT32_MAX;
      uint32_t selectedRegion = UINT32_MAX;
      uint64_t selectedInsertionSequence = UINT64_MAX;
      bool scanStateUnchanged =
          processCount == nativeScanProcessCount &&
          context->schedulerEpoch == nativeScanEpoch &&
          context->schedulerSelectionGeneration ==
              nativeScanSelectionGeneration &&
          context->nextProcessInsertionSequence == nativeScanInsertionSequence;
      bool nativeCandidateExpected =
          nativeCandidateIndex != SIZE_MAX &&
          (std::tuple{nativeRegion, nativeRank, nativeInsertionSequence} ==
               std::tuple{uint32_t{0}, uint32_t{0}, uint64_t{0}} ||
           std::tuple{nativeRegion, nativeRank, nativeInsertionSequence} <
               std::tuple{barrierRegion, uint32_t{0}, uint64_t{0}});
      if (scanStateUnchanged && nativeCandidateExpected &&
          nativeCandidateIndex < processCount) {
        ScheduledProcess &candidate =
            context->scheduledProcesses[nativeCandidateIndex];
        bool runnable =
            candidate.instance && candidate.token == nativeCandidateToken &&
            candidate.phase == (context->schedulerRunningFinals ? 1u : 0u) &&
            nativeProcessReady(*context, candidate);
        bool signalResume =
            candidate.signalTriggered ||
            (candidate.signalLatch && candidate.signalLatch->triggered);
        uint32_t orderingRegion =
            schedulerOrderingRegion(*context, candidate, signalResume);
        auto key = candidate.prioritySignal && signalResume
                       ? std::tuple{orderingRegion, uint32_t{0}, uint64_t{0}}
                       : std::tuple{orderingRegion, candidate.scheduleRank,
                                    candidate.insertionSequence};
        if (runnable &&
            (candidate.urgent ||
             (key == std::tuple{nativeRegion, nativeRank,
                                nativeInsertionSequence} &&
              key < std::tuple{barrierRegion, uint32_t{0}, uint64_t{0}}))) {
          selected = candidate.instance;
          selectedIndex = nativeCandidateIndex;
          selectedRank = candidate.urgent ? 0 : std::get<1>(key);
          selectedRegion = candidate.urgent ? 0 : std::get<0>(key);
          selectedInsertionSequence = candidate.urgent ? 0 : std::get<2>(key);
        }
      }
      if (!selected && (!scanStateUnchanged || nativeCandidateExpected)) {
        if (context->signalDiagnosticsEnabled)
          ++context->signalDiagnostics.fallbackRescans;
        for (size_t step = 0; step < processCount; ++step) {
          size_t index = (context->schedulerCursor + step) % processCount;
          ScheduledProcess &candidate = context->scheduledProcesses[index];
          if (context->signalDiagnosticsEnabled)
            ++context->signalDiagnostics.candidateScans;
          if (!candidate.instance ||
              candidate.phase != (context->schedulerRunningFinals ? 1u : 0u))
            continue;
          bool runnable = nativeProcessReady(*context, candidate);
          if (!runnable)
            continue;
          if (candidate.urgent) {
            selected = candidate.instance;
            selectedIndex = index;
            selectedRank = 0;
            selectedRegion = 0;
            selectedInsertionSequence = 0;
            break;
          }
          bool signalResume =
              candidate.signalTriggered ||
              (candidate.signalLatch && candidate.signalLatch->triggered);
          uint32_t orderingRegion =
              schedulerOrderingRegion(*context, candidate, signalResume);
          auto key = candidate.prioritySignal && signalResume
                         ? std::tuple{orderingRegion, uint32_t{0}, uint64_t{0}}
                         : std::tuple{orderingRegion, candidate.scheduleRank,
                                      candidate.insertionSequence};
          if (!(key < std::tuple{barrierRegion, uint32_t{0}, uint64_t{0}}))
            continue;
          if (selected && !(key < std::tuple{selectedRegion, selectedRank,
                                             selectedInsertionSequence}))
            continue;
          selected = candidate.instance;
          selectedIndex = index;
          selectedRank = std::get<1>(key);
          selectedRegion = std::get<0>(key);
          selectedInsertionSequence = std::get<2>(key);
        }
      }
      if (selected) {
        ScheduledProcess &candidate =
            context->scheduledProcesses[selectedIndex];
        if (candidate.suspendedAOTActorSlot != UINT32_MAX &&
            candidate.callers.empty()) {
          // Restore only the original caller, including after task disable
          // unwinds its stack. Never install a nested task in this slot.
          uint32_t slot = candidate.suspendedAOTActorSlot;
          const auto *plan = context->nativeSchedulePlan;
          if (!plan || slot >= context->nativeScheduleActors.size() ||
              context->nativeScheduleActors[slot] != nullptr ||
              context->nativeScheduleActorTokens[slot] != candidate.token)
            return OBELISK_RT_INVALID_LIFECYCLE;
          if (options.nativePlan &&
              findNativeAOTNodeUnlocked(context, slot,
                                        candidate.instance->continuation) ==
                  UINT32_MAX)
            return OBELISK_RT_INVALID_CONTINUATION;
          obelisk_rt_status status = plan->bind(plan->mutable_state, context,
                                                slot, candidate.instance);
          if (status != OBELISK_RT_OK)
            return status;
          candidate.aotActorSlot = slot;
          candidate.suspendedAOTActorSlot = UINT32_MAX;
          context->nativeScheduleActors[slot] = candidate.instance;
          context->nativeScheduleActorTokens[slot] = candidate.token;
          context->nativeScheduleActorIndices[slot] = selectedIndex;
          updateNativeAOTContinuationRank(candidate,
                                          candidate.instance->continuation);
        }
        if (candidate.suspendKind == OBELISK_RT_SUSPEND_SEMAPHORE) {
          bool acquired = false;
          obelisk_rt_status status = obelisk_rt_semaphore_wait_acquire(
              currentWait(candidate), acquired);
          if (status != OBELISK_RT_OK) {
            context->schedulerStatus = status;
            return status;
          }
          if (!acquired)
            clearCachedNativeReady();
          if (!acquired)
            continue;
        }
        if (cachedNativeSelection &&
            candidate.token != cachedNativeSelection->token)
          clearCachedNativeReady();
        if (options.nativePlan && candidate.aotActorSlot != UINT32_MAX) {
          // The executor owns its frame/action ABI. Selection, updates, and
          // subsequent reactivation remain in this loop (1800-2023 4.5).
          uint32_t slot = candidate.aotActorSlot;
          uint64_t selectedToken = candidate.token;
          uint32_t node = findNativeAOTNodeUnlocked(
              context, slot, candidate.instance->continuation);
          if (node == UINT32_MAX)
            return OBELISK_RT_INVALID_CONTINUATION;
          context->schedulerCursor = (selectedIndex + 1) % processCount;
          clearNativeAOTNodeReadyUnlocked(context, node);
          context->nativeScheduleMinimumActivatedNode = UINT32_MAX;
          if (context->nativeScheduleForcedSlot != UINT32_MAX)
            context->nativeScheduleForcedExecuted = true;
          obelisk_rt_status status =
              executeNativeAOTNodeUnlocked(context, slot);
          if (status != OBELISK_RT_OK)
            return status;
          auto indexed = context->scheduledProcessIndices.find(selectedToken);
          if (indexed != context->scheduledProcessIndices.end() &&
              indexed->second < context->scheduledProcesses.size()) {
            const ScheduledProcess &resumed =
                context->scheduledProcesses[indexed->second];
            if (resumed.instance && !indexedSignalBlocked(resumed))
              context->nativePollCandidates.insert(resumed.token);
            else
              context->nativePollCandidates.erase(resumed.token);
            readmitNativeCandidate(resumed, indexed->second,
                                   cachedNativeSelection.has_value());
          }
          if (context->nativeScheduleSingleStep)
            return OBELISK_RT_OK;
          continue;
        }
        selectedResuming = candidate.started &&
                           candidate.suspendKind != OBELISK_RT_SUSPEND_NONE;
        context->schedulerCursor = (selectedIndex + 1) % processCount;
        if (candidate.aotActorSlot != UINT32_MAX) {
          uint32_t node =
              findNativeAOTNodeUnlocked(context, candidate.aotActorSlot,
                                        candidate.instance->continuation);
          if (node != UINT32_MAX)
            clearNativeAOTNodeReadyUnlocked(context, node);
        }
        // Keep the wait indexed while its process executes. A process may
        // update a signal in its own event expression and then suspend on the
        // same wait again; retaining the subscription lets that transition
        // latch the next activation instead of being lost between resume and
        // re-arm. Consume only the occurrence that selected this activation.
        if (candidate.signalLatch) {
          candidate.signalLatch->triggered = false;
          candidate.signalLatch->affected = false;
        }
        candidate.signalTriggered = false;
        context->nativePollCandidates.erase(candidate.token);
        if (!candidate.started)
          obelisk_rt_unregister_unstarted_actor(context, candidate.phase,
                                                kNativeLogicalProcessTag |
                                                    candidate.token);
        candidate.started = true;
        candidate.observedEpoch = context->schedulerEpoch;
      }
      if (cachedNativeSelection && !selected)
        clearCachedNativeReady();
      if (!selected && !context->schedulerRunningFinals &&
          barrierRegion != UINT32_MAX) {
        bool changed = false;
        bool eventTriggered = false;
        obelisk_rt_status clockingStatus =
            resolveClockingDriveConflictsUnlocked(context, barrierRegion);
        if (clockingStatus != OBELISK_RT_OK)
          return clockingStatus;
        std::vector<obelisk::designbytecode::NetPublication>
            delayedNetPublications;
        uint64_t delayedNetGroup = UINT64_MAX;
        uint64_t delayedNetDueTime = UINT64_MAX;
        auto flushDelayedNetPublications = [&] {
          if (delayedNetPublications.empty()) {
            delayedNetGroup = UINT64_MAX;
            delayedNetDueTime = UINT64_MAX;
            return true;
          }
          auto *cache = obelisk::designbytecode::getNetAliasCache(
              context->designBytecodeImage, context);
          if (!cache || !obelisk::designbytecode::publishNetBits(
                            context, *cache, delayedNetPublications, changed)) {
            if (context->schedulerStatus == OBELISK_RT_OK)
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
            return false;
          }
          delayedNetPublications.clear();
          delayedNetGroup = UINT64_MAX;
          delayedNetDueTime = UINT64_MAX;
          return true;
        };
        auto prepareDelayedNetBatch = [&](const ScheduledNBA &update) {
          if (update.inertialNetBit == UINT64_MAX)
            return flushDelayedNetPublications();
          if (delayedNetGroup != UINT64_MAX &&
              (delayedNetGroup != update.inertialNetGroup ||
               delayedNetDueTime != update.dueTime) &&
              !flushDelayedNetPublications())
            return false;
          delayedNetGroup = update.inertialNetGroup;
          delayedNetDueTime = update.dueTime;
          return true;
        };
        auto applyNative = [&](const ScheduledNBA &update) {
          bool publicationChanged = false;
          uint64_t oldRealBits = 0;
          uint64_t newRealBits = 0;
          auto realBitsChanged = [&](uint64_t oldBits, uint64_t newBits) {
            if (update.realWidth == 32) {
              uint32_t oldNarrow = static_cast<uint32_t>(oldBits);
              uint32_t newNarrow = static_cast<uint32_t>(newBits);
              float oldReal = 0.0f;
              float newReal = 0.0f;
              std::memcpy(&oldReal, &oldNarrow, sizeof(oldReal));
              std::memcpy(&newReal, &newNarrow, sizeof(newReal));
              return oldReal != newReal || std::isnan(newReal);
            }
            double oldReal = 0.0;
            double newReal = 0.0;
            std::memcpy(&oldReal, &oldBits, sizeof(oldReal));
            std::memcpy(&newReal, &newBits, sizeof(newReal));
            return oldReal != newReal || std::isnan(newReal);
          };
          uint32_t automaticID = 0;
          uint32_t staticID = 0;
          int64_t baseOffset = 0;
          bool automatic =
              decodeNativeAutomatic(update.bitOffset, automaticID, baseOffset);
          NativeAutomaticState *automaticState = nullptr;
          const NativeStaticState *staticState = nullptr;
          if (automatic) {
            auto found = context->nativeAutomaticStates.find(automaticID);
            if (found == context->nativeAutomaticStates.end()) {
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
              return;
            }
            automaticState = &found->second;
          } else if (decodeNativeStatic(update.bitOffset, staticID,
                                        baseOffset)) {
            const NativeStaticState *state =
                findNativeStaticState(context, staticID);
            if (!state || state->bitOffset > update.planeBitCount ||
                state->bitWidth > update.planeBitCount - state->bitOffset) {
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
              return;
            }
            staticState = state;
          } else if (!decodeNativeGlobal(update.bitOffset, baseOffset)) {
            context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
            return;
          }
          bool canonical =
              !automatic && context->execution &&
              context->execution->state_bit_count == update.planeBitCount;
          if (update.driver &&
              (automatic || update.managedValue || update.stringValue ||
               ((!canonical || !staticState) &&
                !update.deferDriverResolution))) {
            context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
            return;
          }
          if (canonical &&
              (context->stateValue.size() < (update.planeBitCount + 63) / 64 ||
               context->stateUnknown.size() <
                   (update.planeBitCount + 63) / 64)) {
            context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
            return;
          }
          if (update.managedValue) {
            if (update.bitWidth != 64 || baseOffset < 0 ||
                (static_cast<uint64_t>(baseOffset) & 63) != 0 ||
                update.value.size() != sizeof(obelisk_rt_managed_word_v1) ||
                update.unknown.size() != 0 ||
                (update.rootedManaged &&
                 obelisk_rt_managed_object_context(update.rootedManaged) !=
                     context)) {
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
              return;
            }
            obelisk_rt_object_v1 *previous = nullptr;
            uint64_t byteOffset = static_cast<uint64_t>(baseOffset) / 8;
            if (automatic) {
              if (automaticState->managedRootRegistered) {
                if (baseOffset != 0) {
                  context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                  return;
                }
                previous = automaticState->managedValue;
                automaticState->managedValue = update.rootedManaged;
              } else {
                if (byteOffset > automaticState->value.size() ||
                    sizeof(obelisk_rt_managed_word_v1) >
                        automaticState->value.size() - byteOffset ||
                    std::find(automaticState->managedRootByteOffsets.begin(),
                              automaticState->managedRootByteOffsets.end(),
                              byteOffset) ==
                        automaticState->managedRootByteOffsets.end()) {
                  context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                  return;
                }
                obelisk_rt_managed_word_v1 previousWord = 0;
                std::memcpy(&previousWord,
                            automaticState->value.data() + byteOffset,
                            sizeof(previousWord));
                previous = obelisk_rt_object_from_managed_word(previousWord);
                obelisk_rt_managed_word_v1 replacementWord =
                    obelisk_rt_managed_word_from_object(update.rootedManaged);
                std::memcpy(automaticState->value.data() + byteOffset,
                            &replacementWord, sizeof(replacementWord));
              }
            } else {
              if (!canonical) {
                context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
                return;
              }
              uint64_t planeBit = staticState
                                      ? staticState->bitOffset +
                                            static_cast<uint64_t>(baseOffset)
                                      : static_cast<uint64_t>(baseOffset);
              if ((planeBit & 63) != 0 ||
                  planeBit / 64 >= context->stateValue.size()) {
                context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                return;
              }
              obelisk_rt_managed_word_v1 previousWord =
                  context->stateValue[planeBit / 64];
              previous = obelisk_rt_object_from_managed_word(previousWord);
              context->stateValue[planeBit / 64] =
                  obelisk_rt_managed_word_from_object(update.rootedManaged);
              if (!storeNativeScheduleStateUnlocked(
                      context, planeBit, 64,
                      static_cast<uint64_t>(
                          reinterpret_cast<uintptr_t>(update.rootedManaged)),
                      0)) {
                context->schedulerStatus = OBELISK_RT_LAYOUT_MISMATCH;
                return;
              }
            }
            if (previous != update.rootedManaged) {
              changed = true;
              publicationChanged = true;
              if (!obelisk_rt_publish_signal_occurrence_unlocked(
                      context, update.bitOffset, 64, OBELISK_RT_SIGNAL_CHANGE))
                return;
              obelisk_rt_invalidate_signal_snapshots_unlocked(
                  context, update.bitOffset, 64);
              if (!obelisk_rt_latch_conditional_signal_range_unlocked(
                      context, update.bitOffset, 64, OBELISK_RT_SIGNAL_CHANGE))
                return;
              if (!obelisk_rt_notify_observer_signal_unlocked(
                      context, update.bitOffset, 64))
                return;
            }
            return;
          }
          bool equalStringContents = false;
          if (update.stringValue) {
            if (update.bitWidth != 64 || baseOffset < 0 ||
                (static_cast<uint64_t>(baseOffset) & 63) != 0 ||
                obelisk_rt_validate_string(context, update.rootedString) !=
                    OBELISK_RT_OK) {
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
              return;
            }
            obelisk_rt_string_v1 previous = 0;
            uint64_t byteOffset = static_cast<uint64_t>(baseOffset) / 8;
            if (automatic) {
              if (automaticState->managedRootRegistered) {
                if (baseOffset != 0) {
                  context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                  return;
                }
                previous = obelisk_rt_managed_word_from_object(
                    automaticState->managedValue);
              } else if (byteOffset > automaticState->value.size() ||
                         sizeof(previous) >
                             automaticState->value.size() - byteOffset) {
                context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                return;
              } else {
                std::memcpy(&previous,
                            automaticState->value.data() + byteOffset,
                            sizeof(previous));
              }
            } else {
              uint64_t planeBit = staticState
                                      ? staticState->bitOffset +
                                            static_cast<uint64_t>(baseOffset)
                                      : static_cast<uint64_t>(baseOffset);
              if ((planeBit & 63) != 0) {
                context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                return;
              }
              if (canonical) {
                if (planeBit / 64 >= context->stateValue.size()) {
                  context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                  return;
                }
                previous = context->stateValue[planeBit / 64];
              } else {
                uint64_t globalByte = planeBit / 8;
                uint64_t globalBytes = (update.planeBitCount + 7) / 8;
                if (!update.valuePlane || globalByte > globalBytes ||
                    sizeof(previous) > globalBytes - globalByte) {
                  context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                  return;
                }
                std::memcpy(&previous, update.valuePlane + globalByte,
                            sizeof(previous));
              }
            }
            if (obelisk_rt_validate_string(context, previous) !=
                OBELISK_RT_OK) {
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
              return;
            }
            equalStringContents = obelisk_rt_v1_string_compare(
                                      previous, update.rootedString) == 0;
          }
          PackedSignalTransitionBuffer transitions(update.bitWidth);
          uint64_t packedPlaneBit = 0;
          bool smallCanonicalPacked =
              canonical && !automatic && !update.stringValue &&
              baseOffset >= 0 && update.bitWidth <= 64 &&
              (update.inlinePacked ||
               update.value.size() == (update.bitWidth + 7) / 8);
          if (smallCanonicalPacked) {
            if (staticState)
              smallCanonicalPacked =
                  static_cast<uint64_t>(baseOffset) <= staticState->bitWidth &&
                  update.bitWidth <=
                      staticState->bitWidth - static_cast<uint64_t>(baseOffset);
            packedPlaneBit = staticState ? staticState->bitOffset +
                                               static_cast<uint64_t>(baseOffset)
                                         : static_cast<uint64_t>(baseOffset);
            smallCanonicalPacked &=
                packedPlaneBit <= update.planeBitCount &&
                update.bitWidth <= update.planeBitCount - packedPlaneBit;
          }
          auto rangeMasked = [&](const std::vector<uint64_t> &masks) {
            if (masks.empty())
              return false;
            uint64_t first = packedPlaneBit / 64;
            uint64_t last = (packedPlaneBit + update.bitWidth - 1) / 64;
            for (uint64_t limb = first; limb <= last && limb < masks.size();
                 ++limb) {
              uint64_t begin =
                  limb == first ? packedPlaneBit % 64 : uint64_t{0};
              uint64_t end =
                  limb == last ? (packedPlaneBit + update.bitWidth - 1) % 64 + 1
                               : uint64_t{64};
              uint64_t mask =
                  begin == 0 && end == 64
                      ? UINT64_MAX
                      : (end == 64 ? UINT64_MAX : (uint64_t{1} << end) - 1) &
                            (begin == 0 ? UINT64_MAX
                                        : ~((uint64_t{1} << begin) - 1));
              if ((masks[limb] & mask) != 0)
                return true;
            }
            return false;
          };
          smallCanonicalPacked &= !rangeMasked(context->forceMask) &&
                                  !rangeMasked(context->assignMask);
          if (smallCanonicalPacked) {
            auto *canonicalValue =
                reinterpret_cast<uint8_t *>(context->stateValue.data());
            auto *canonicalUnknown =
                reinterpret_cast<uint8_t *>(context->stateUnknown.data());
            uint64_t oldValue = loadPackedBytes(canonicalValue, packedPlaneBit,
                                                update.bitWidth);
            uint64_t oldUnknown = loadPackedBytes(
                canonicalUnknown, packedPlaneBit, update.bitWidth);
            uint64_t newValue =
                update.inlinePacked
                    ? update.inlineValue
                    : loadPackedBytes(update.value.data(), 0, update.bitWidth);
            uint64_t newUnknown = update.inlinePacked ? update.inlineUnknown
                                  : update.unknown.empty()
                                      ? 0
                                      : loadPackedBytes(update.unknown.data(),
                                                        0, update.bitWidth);
            uint64_t widthMask = update.bitWidth == 64
                                     ? UINT64_MAX
                                     : (uint64_t{1} << update.bitWidth) - 1;
            uint64_t changedBits =
                ((oldValue ^ newValue) | (oldUnknown ^ newUnknown)) & widthMask;
            oldRealBits = oldValue;
            newRealBits = newValue;
            uint64_t oldZero = ~oldUnknown & ~oldValue & widthMask;
            uint64_t oldOne = ~oldUnknown & oldValue & widthMask;
            uint64_t newZero = ~newUnknown & ~newValue & widthMask;
            uint64_t newOne = ~newUnknown & newValue & widthMask;
            uint64_t posedge = (oldZero & ~newZero) | (oldUnknown & newOne);
            uint64_t negedge = (oldOne & ~newOne) | (oldUnknown & newZero);
            uint64_t packedBytes = (update.bitWidth + 7) / 8;
            std::memcpy(transitions.changed(), &changedBits, packedBytes);
            std::memcpy(transitions.posedge(), &posedge, packedBytes);
            std::memcpy(transitions.negedge(), &negedge, packedBytes);
            if (update.valuePlane)
              storePackedBytes(update.valuePlane, packedPlaneBit,
                               update.bitWidth, newValue);
            if (update.unknownPlane)
              storePackedBytes(update.unknownPlane, packedPlaneBit,
                               update.bitWidth, newUnknown);
            storePackedBytes(canonicalValue, packedPlaneBit, update.bitWidth,
                             newValue);
            storePackedBytes(canonicalUnknown, packedPlaneBit, update.bitWidth,
                             newUnknown);
            if (!storeNativeScheduleStateUnlocked(context, packedPlaneBit,
                                                  update.bitWidth, newValue,
                                                  newUnknown)) {
              context->schedulerStatus = OBELISK_RT_LAYOUT_MISMATCH;
              return;
            }
            publicationChanged = update.realWidth != 0
                                     ? realBitsChanged(oldRealBits, newRealBits)
                                     : changedBits != 0;
            if (!update.driver)
              changed |= publicationChanged;
            if (update.inertialNetBit != UINT64_MAX)
              for (uint64_t bit = 0; bit != update.bitWidth; ++bit) {
                uint64_t mask = uint64_t{1} << bit;
                if ((changedBits & mask) == 0)
                  continue;
                delayedNetPublications.push_back(
                    {packedPlaneBit + bit, (oldValue & mask) != 0,
                     (oldUnknown & mask) != 0, (newValue & mask) != 0,
                     (newUnknown & mask) != 0});
              }
          } else
            for (uint64_t bit = 0; bit < update.bitWidth; ++bit) {
              uint64_t sourceByte = bit / 8;
              uint8_t sourceMask = static_cast<uint8_t>(1u << (bit % 8));
              if (!update.inlinePacked &&
                  (sourceByte >= update.value.size() ||
                   (!update.unknown.empty() &&
                    sourceByte >= update.unknown.size()))) {
                context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
                return;
              }
              int64_t coordinate = 0;
              if (!addHandleOffset(baseOffset, bit, coordinate) ||
                  coordinate < 0)
                continue;
              uint64_t localBit = static_cast<uint64_t>(coordinate);
              if (automatic) {
                if (localBit >= automaticState->bitWidth)
                  continue;
              } else if (staticState) {
                if (localBit >= staticState->bitWidth)
                  continue;
              } else if (localBit >= update.planeBitCount) {
                continue;
              }
              uint64_t planeBit =
                  staticState ? staticState->bitOffset + localBit : localBit;
              uint64_t canonicalMask = uint64_t{1} << (planeBit % 64);
              bool forced =
                  planeBit / 64 < context->forceMask.size() &&
                  (context->forceMask[planeBit / 64] & canonicalMask) != 0;
              bool assigned =
                  planeBit / 64 < context->assignMask.size() &&
                  (context->assignMask[planeBit / 64] & canonicalMask) != 0;
              if (canonical && (forced || assigned))
                continue;
              uint64_t storageBit = automatic ? localBit : planeBit;
              uint64_t destinationByte = storageBit / 8;
              uint8_t destinationMask =
                  static_cast<uint8_t>(1u << (storageBit % 8));
              auto read = [&](uint8_t *globalPlane, bool unknown) {
                if (automatic) {
                  const std::vector<uint8_t> &plane =
                      unknown ? automaticState->unknown : automaticState->value;
                  return !plane.empty() &&
                         (plane[destinationByte] & destinationMask) != 0;
                }
                if (canonical) {
                  const std::vector<uint64_t> &plane =
                      unknown ? context->stateUnknown : context->stateValue;
                  return ((plane[planeBit / 64] >> (planeBit % 64)) & 1) != 0;
                }
                return globalPlane &&
                       (globalPlane[destinationByte] & destinationMask) != 0;
              };
              bool oldValue = read(update.valuePlane, false);
              bool oldUnknown = read(update.unknownPlane, true);
              bool newValue =
                  update.inlinePacked
                      ? ((update.inlineValue >> bit) & uint64_t{1}) != 0
                      : (update.value[sourceByte] & sourceMask) != 0;
              bool newUnknown =
                  update.inlinePacked
                      ? ((update.inlineUnknown >> bit) & uint64_t{1}) != 0
                      : !update.unknown.empty() &&
                            (update.unknown[sourceByte] & sourceMask) != 0;
              if (update.realWidth != 0 && bit < 64) {
                oldRealBits |= static_cast<uint64_t>(oldValue) << bit;
                newRealBits |= static_cast<uint64_t>(newValue) << bit;
              }
              auto apply = [&](uint8_t *globalPlane, bool unknown, bool value) {
                uint8_t *plane = globalPlane;
                if (automatic) {
                  std::vector<uint8_t> &storage =
                      unknown ? automaticState->unknown : automaticState->value;
                  if (storage.empty())
                    return;
                  plane = storage.data();
                }
                if (plane) {
                  uint8_t old = plane[destinationByte];
                  uint8_t next =
                      value ? old | destinationMask
                            : old & static_cast<uint8_t>(~destinationMask);
                  plane[destinationByte] = next;
                }
                if (canonical) {
                  std::vector<uint64_t> &storage =
                      unknown ? context->stateUnknown : context->stateValue;
                  uint64_t mask = uint64_t{1} << (planeBit % 64);
                  uint64_t &limb = storage[planeBit / 64];
                  limb = value ? limb | mask : limb & ~mask;
                }
              };
              apply(update.valuePlane, false, newValue);
              apply(update.unknownPlane, true, newUnknown);
              if (canonical && !storeNativeScheduleStateUnlocked(
                                   context, planeBit, 1, newValue ? 1 : 0,
                                   newUnknown ? 1 : 0)) {
                context->schedulerStatus = OBELISK_RT_LAYOUT_MISMATCH;
                return;
              }
              uint32_t edges =
                  transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
              if (edges != 0 && !equalStringContents) {
                if (!update.driver)
                  changed = true;
                publicationChanged = true;
                transitions.record(bit, edges);
                if (update.inertialNetBit != UINT64_MAX)
                  delayedNetPublications.push_back(
                      {planeBit, oldValue, oldUnknown, newValue, newUnknown});
              }
            }
          if (update.realWidth != 0)
            publicationChanged = realBitsChanged(oldRealBits, newRealBits);
          if (publicationChanged &&
              (!update.driver || update.publishDriverTransition)) {
            // One collapsed simulated net can occupy several logical state
            // ranges. Commit all of its same-time inertial publications before
            // evaluating any observer so no callback can see a half-updated
            // alias set.
            if (update.inertialNetBit != UINT64_MAX)
              return;
            if (update.realWidth != 0) {
              if (!obelisk_rt_publish_signal_occurrence_unlocked(
                      context, update.bitOffset, update.bitWidth,
                      OBELISK_RT_SIGNAL_CHANGE))
                return;
              obelisk_rt_invalidate_signal_snapshots_unlocked(
                  context, update.bitOffset, update.bitWidth);
              if (!obelisk_rt_latch_conditional_signal_range_unlocked(
                      context, update.bitOffset, update.bitWidth,
                      OBELISK_RT_SIGNAL_CHANGE))
                return;
              if (!obelisk_rt_notify_observer_signal_unlocked(
                      context, update.bitOffset, update.bitWidth))
                return;
              if (++context->schedulerEpoch == 0)
                context->schedulerEpoch = 1;
              return;
            }
            uint64_t sequence = 0;
            const uint8_t *publishedValue =
                update.inlinePacked
                    ? reinterpret_cast<const uint8_t *>(&update.inlineValue)
                    : update.value.data();
            const uint8_t *publishedUnknown =
                update.inlinePacked
                    ? reinterpret_cast<const uint8_t *>(&update.inlineUnknown)
                : update.unknown.empty() ? nullptr
                                         : update.unknown.data();
            if (!obelisk_rt_publish_signal_transition_batch_unlocked(
                    context, update.bitOffset, update.bitWidth,
                    transitions.changed(), transitions.posedge(),
                    transitions.negedge(), 0, &sequence, nullptr, nullptr,
                    publishedValue, publishedUnknown))
              return;
            obelisk_rt_invalidate_signal_snapshots_unlocked(
                context, update.bitOffset, update.bitWidth);
            if (obelisk_rt_has_conditional_signal_waiters(context)) {
              for (uint64_t bit = 0; bit != update.bitWidth; ++bit) {
                if (!byteBit(transitions.changed(), bit) ||
                    bit > static_cast<uint64_t>(INT64_MAX))
                  continue;
                uint64_t eventHandle = nativeHandleOffset(
                    update.bitOffset, static_cast<int64_t>(bit));
                if (eventHandle == UINT64_MAX)
                  continue;
                bool finalValue =
                    update.inlinePacked
                        ? ((update.inlineValue >> bit) & uint64_t{1}) != 0
                        : byteBit(update.value.data(), bit);
                bool finalUnknown =
                    update.inlinePacked
                        ? ((update.inlineUnknown >> bit) & uint64_t{1}) != 0
                        : !update.unknown.empty() &&
                              byteBit(update.unknown.data(), bit);
                context->signalValueSnapshots[eventHandle] = {
                    sequence, finalValue, finalUnknown};
              }
              for (uint64_t bit = 0; bit != update.bitWidth; ++bit) {
                if (!byteBit(transitions.changed(), bit) ||
                    bit > static_cast<uint64_t>(INT64_MAX))
                  continue;
                uint64_t eventHandle = nativeHandleOffset(
                    update.bitOffset, static_cast<int64_t>(bit));
                if (eventHandle == UINT64_MAX)
                  continue;
                uint32_t edges = OBELISK_RT_SIGNAL_CHANGE;
                if (byteBit(transitions.posedge(), bit))
                  edges |= OBELISK_RT_SIGNAL_POSEDGE;
                if (byteBit(transitions.negedge(), bit))
                  edges |= OBELISK_RT_SIGNAL_NEGEDGE;
                if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
                        context, eventHandle, edges))
                  return;
              }
            }
            if (!obelisk_rt_notify_observer_signal_unlocked(
                    context, update.bitOffset, update.bitWidth))
              return;
          }
          if ((publicationChanged || update.forceDriverResolution) &&
              update.driver && !update.deferDriverResolution) {
            __int128 first = std::max<__int128>(baseOffset, 0);
            __int128 last = std::min<__int128>(
                static_cast<__int128>(baseOffset) + update.bitWidth,
                staticState->bitWidth);
            if (first < last) {
              uint64_t begin =
                  staticState->bitOffset + static_cast<uint64_t>(first);
              uint64_t end =
                  staticState->bitOffset + static_cast<uint64_t>(last);
              bool resolvedChanged = false;
              if (!obelisk::designbytecode::resolveDrivenNets(
                      context->designBytecodeImage, context,
                      static_cast<int64_t>(begin), static_cast<int64_t>(end),
                      resolvedChanged)) {
                context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                return;
              }
              changed |= resolvedChanged;
            }
          }
        };
        auto currentPathUpdate = [&](const ScheduledNBA &update) {
          if (!update.inertialPathDriver)
            return true;
          if (update.inertialPathStrengthPair) {
            auto pending =
                context->inertialStrengthPathPending.find(update.inertialSite);
            if (pending == context->inertialStrengthPathPending.end() ||
                update.inertialPathBit >= pending->second.width ||
                update.inertialPathGeneration !=
                    pending->second.generation[static_cast<size_t>(
                        update.inertialPathBit)])
              return false;
            size_t index = static_cast<size_t>(update.inertialPathBit);
            if (pending->second.pulseControlled) {
              const auto &live = pending->second.liveSequences[index];
              return std::find(live.begin(), live.end(), update.sequence) !=
                     live.end();
            }
            return (update.inertialPathStrengthFinal
                        ? pending->second.highSequence[index]
                        : pending->second.lowSequence[index]) ==
                   update.sequence;
          }
          auto pending = context->inertialPathPending.find(update.inertialSite);
          if (pending == context->inertialPathPending.end() ||
              update.inertialPathBit >= pending->second.width ||
              update.inertialPathGeneration !=
                  pending->second
                      .generation[static_cast<size_t>(update.inertialPathBit)])
            return false;
          size_t index = static_cast<size_t>(update.inertialPathBit);
          if (!pending->second.pulseControlled)
            return pending->second.scheduledSequence[index] == update.sequence;
          const auto &live = pending->second.liveSequences[index];
          return std::find(live.begin(), live.end(), update.sequence) !=
                 live.end();
        };
        auto completeInertial = [&](const ScheduledNBA &update) {
          if (update.inertialNetBit != UINT64_MAX) {
            auto pending =
                context->inertialNetPending.find(update.inertialNetBit);
            if (pending != context->inertialNetPending.end() &&
                pending->second.value == (update.inlineValue != 0) &&
                pending->second.unknown == (update.inlineUnknown != 0) &&
                pending->second.chargeDecay == update.inertialNetChargeDecay)
              context->inertialNetPending.erase(pending);
          }
          if (update.inertialSite.codeUnit == UINT64_MAX)
            return;
          if (update.inertialPathDriver) {
            if (update.inertialPathStrengthPair) {
              auto pending = context->inertialStrengthPathPending.find(
                  update.inertialSite);
              if (pending != context->inertialStrengthPathPending.end() &&
                  update.inertialPathBit < pending->second.width) {
                size_t index = static_cast<size_t>(update.inertialPathBit);
                if (pending->second.pulseControlled) {
                  auto &live = pending->second.liveSequences[index];
                  live.erase(
                      std::remove(live.begin(), live.end(), update.sequence),
                      live.end());
                }
                if (update.inertialPathStrengthFinal &&
                    pending->second.highSequence[index] == update.sequence) {
                  pending->second.valid[index] = 0;
                  pending->second.scheduledDueTime[index] = 0;
                  pending->second.lowSequence[index] = 0;
                  pending->second.highSequence[index] = 0;
                }
              }
              return;
            }
            auto pending =
                context->inertialPathPending.find(update.inertialSite);
            if (pending != context->inertialPathPending.end() &&
                update.inertialPathBit < pending->second.width) {
              size_t index = static_cast<size_t>(update.inertialPathBit);
              if (pending->second.pulseControlled) {
                auto &live = pending->second.liveSequences[index];
                live.erase(
                    std::remove(live.begin(), live.end(), update.sequence),
                    live.end());
              }
              if (pending->second.scheduledSequence[index] == update.sequence) {
                pending->second.valid[index] = 0;
                pending->second.delayed[index] = 0;
                pending->second.scheduledDueTime[index] = 0;
                pending->second.scheduledSequence[index] = 0;
              }
            }
            return;
          }
          auto pending =
              context->inertialDriverPending.find(update.inertialSite);
          if (pending == context->inertialDriverPending.end())
            return;
          if (update.inertialDriverVector &&
              (pending->second.destination != update.bitOffset ||
               pending->second.width != update.bitWidth ||
               pending->second.value != update.value ||
               pending->second.unknown != update.unknown))
            return;
          if (pending->second.remaining <= 1)
            context->inertialDriverPending.erase(pending);
          else
            --pending->second.remaining;
        };
        auto planeBit = [](const std::vector<uint64_t> &plane, uint64_t bit) {
          return bit / 64 < plane.size() &&
                 ((plane[bit / 64] >> (bit % 64)) & 1) != 0;
        };
        auto applyDesign = [&](const ScheduledDesignNBA &update) {
          if (update.handleKind != OBELISK_RT_DESCRIPTOR_STORAGE)
            return false;
          bool publicationChanged = false;
          uint64_t publicationBegin = UINT64_MAX;
          uint64_t publicationEnd = 0;
          uint64_t available =
              context->execution ? context->execution->state_bit_count : 0;
          PackedSignalTransitionBuffer transitions(update.bitWidth);
          bool equalStringContents = false;
          if (update.stringValue) {
            if (update.bitWidth != 64 || update.start < update.begin ||
                update.start < 0 || (update.start & 63) != 0 ||
                update.end < update.start || update.end - update.start < 64 ||
                static_cast<uint64_t>(update.start) > available ||
                64 > available - static_cast<uint64_t>(update.start) ||
                obelisk_rt_validate_string(context, update.rootedString) !=
                    OBELISK_RT_OK)
              return false;
            obelisk_rt_string_v1 previous =
                context->stateValue[static_cast<uint64_t>(update.start) / 64];
            if (obelisk_rt_validate_string(context, previous) != OBELISK_RT_OK)
              return false;
            equalStringContents = obelisk_rt_v1_string_compare(
                                      previous, update.rootedString) == 0;
          }
          for (uint64_t bit = 0; bit != update.bitWidth; ++bit) {
            if (bit > uint64_t{INT64_MAX} ||
                update.start > INT64_MAX - static_cast<int64_t>(bit))
              continue;
            int64_t coordinate = update.start + static_cast<int64_t>(bit);
            if (coordinate < update.begin || coordinate >= update.end ||
                coordinate < 0)
              continue;
            uint64_t destination = static_cast<uint64_t>(coordinate);
            if (destination >= available)
              return false;
            uint64_t limb = destination / 64;
            uint64_t mask = uint64_t{1} << (destination % 64);
            bool forced = destination / 64 < context->forceMask.size() &&
                          (context->forceMask[destination / 64] & mask) != 0;
            bool assigned = destination / 64 < context->assignMask.size() &&
                            (context->assignMask[destination / 64] & mask) != 0;
            if (forced || assigned)
              continue;
            bool oldValue = (context->stateValue[limb] & mask) != 0;
            bool oldUnknown = (context->stateUnknown[limb] & mask) != 0;
            bool newValue = planeBit(update.value, bit);
            bool newUnknown = planeBit(update.unknown, bit);
            publicationChanged |=
                !equalStringContents &&
                (oldValue != newValue || oldUnknown != newUnknown);
            publicationBegin = std::min(publicationBegin, destination);
            publicationEnd = std::max(publicationEnd, destination + 1);
            auto apply = [&](std::vector<uint64_t> &plane, bool value) {
              uint64_t old = plane[limb];
              uint64_t next = value ? old | mask : old & ~mask;
              changed |= !equalStringContents && old != next;
              plane[limb] = next;
            };
            apply(context->stateValue, newValue);
            apply(context->stateUnknown, newUnknown);
            if (!storeNativeScheduleStateUnlocked(context, destination, 1,
                                                  newValue ? 1 : 0,
                                                  newUnknown ? 1 : 0))
              return false;
            uint32_t edges =
                transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
            if (edges != 0 && !equalStringContents) {
              transitions.record(bit, edges);
            }
          }
          if (publicationChanged) {
            uint64_t publicationWidth = publicationEnd - publicationBegin;
            uint64_t edgeBitOffset = static_cast<uint64_t>(
                static_cast<__int128>(publicationBegin) - update.start);
            uint64_t sequence = 0;
            if (!obelisk_rt_publish_signal_transition_batch_unlocked(
                    context, publicationBegin, publicationWidth,
                    transitions.changed(), transitions.posedge(),
                    transitions.negedge(), edgeBitOffset, &sequence))
              return false;
            obelisk_rt_invalidate_signal_snapshots_unlocked(
                context, publicationBegin, publicationWidth);
            if (obelisk_rt_has_conditional_signal_waiters(context)) {
              for (uint64_t bit = 0; bit != publicationWidth; ++bit) {
                uint64_t sourceBit = edgeBitOffset + bit;
                if (!byteBit(transitions.changed(), sourceBit))
                  continue;
                uint64_t destination = publicationBegin + bit;
                uint64_t mask = uint64_t{1} << (destination % 64);
                context->signalValueSnapshots[destination] = {
                    sequence,
                    (context->stateValue[destination / 64] & mask) != 0,
                    (context->stateUnknown[destination / 64] & mask) != 0};
              }
              for (uint64_t bit = 0; bit != publicationWidth; ++bit) {
                uint64_t sourceBit = edgeBitOffset + bit;
                if (!byteBit(transitions.changed(), sourceBit))
                  continue;
                uint32_t edges = OBELISK_RT_SIGNAL_CHANGE;
                if (byteBit(transitions.posedge(), sourceBit))
                  edges |= OBELISK_RT_SIGNAL_POSEDGE;
                if (byteBit(transitions.negedge(), sourceBit))
                  edges |= OBELISK_RT_SIGNAL_NEGEDGE;
                if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
                        context, publicationBegin + bit, edges))
                  return false;
              }
            }
            if (!obelisk_rt_notify_observer_signal_unlocked(
                    context, publicationBegin, publicationWidth))
              return false;
          }
          return true;
        };
        // Native NBA entries are appended in global execution-sequence order.
        // Purely digital native designs therefore need neither the generic
        // four-queue merge nor one vector erase per committed assignment.
        // Compact future/other-region entries once after draining this
        // barrier; this changes the common case from quadratic to linear work.
        obelisk_rt_status staticNBAStatus = commitStaticNBAAccumulatorsUnlocked(
            context, barrierRegion, changed);
        if (staticNBAStatus != OBELISK_RT_OK)
          return staticNBAStatus;
        if (context->scheduledManagedNBAs.empty() &&
            context->scheduledDesignNBAs.empty() &&
            context->scheduledDesignEvents.empty() &&
            (!obelisk_rt_replaceable_events(context) ||
             obelisk_rt_replaceable_events(context)->calendar.empty()) &&
            context->scheduledPassSwitchEvents.empty() &&
            context->scheduledInertialPathNBAs.empty()) {
          size_t retained = 0;
          for (size_t index = 0;; ++index) {
            if (index == context->scheduledNBAs.size()) {
              if (!flushDelayedNetPublications())
                return context->schedulerStatus;
              if (index == context->scheduledNBAs.size())
                break;
            }
            ScheduledNBA &update = context->scheduledNBAs[index];
            bool due =
                update.cancelled || (update.dueTime <= context->schedulerTime &&
                                     update.execRegion == barrierRegion);
            if (!due) {
              if (retained != index)
                context->scheduledNBAs[retained] = std::move(update);
              ++retained;
              continue;
            }
            uint32_t retainedAutomaticID = update.retainedAutomaticID;
            if (!update.cancelled) {
              if (!prepareDelayedNetBatch(update))
                return context->schedulerStatus;
              ScheduledNBA &current = context->scheduledNBAs[index];
              bool pathCurrent = currentPathUpdate(current);
              if (pathCurrent)
                completeInertial(current);
              context->schedulerApplyingNativeUpdate = pathCurrent;
              if (pathCurrent)
                applyNative(current);
              context->schedulerApplyingNativeUpdate = false;
              if (context->schedulerStatus != OBELISK_RT_OK)
                return context->schedulerStatus;
            }
            if (retainedAutomaticID != 0) {
              auto found =
                  context->nativeAutomaticStates.find(retainedAutomaticID);
              if (found == context->nativeAutomaticStates.end() ||
                  found->second.referenceCount == 0)
                return OBELISK_RT_INVALID_HANDLE;
              if (--found->second.referenceCount == 0) {
                obelisk_rt_erase_automatic_bookkeeping_unlocked(
                    context, retainedAutomaticID);
                context->nativeAutomaticStates.erase(found);
              }
            }
          }
          context->scheduledNBAs.resize(retained);
        } else
          for (;;) {
            uint64_t nativeSequence = UINT64_MAX;
            size_t nativeIndex = 0;
            for (size_t index = 0; index != context->scheduledNBAs.size();
                 ++index) {
              const ScheduledNBA &update = context->scheduledNBAs[index];
              if (!update.cancelled &&
                  update.dueTime <= context->schedulerTime &&
                  update.execRegion == barrierRegion &&
                  update.sequence < nativeSequence) {
                nativeSequence = update.sequence;
                nativeIndex = index;
              }
            }
            uint64_t eventSequence = UINT64_MAX;
            size_t eventIndex = 0;
            for (size_t index = 0;
                 index != context->scheduledDesignEvents.size(); ++index) {
              const ScheduledDesignEvent &event =
                  context->scheduledDesignEvents[index];
              if (event.dueTime <= context->schedulerTime &&
                  event.execRegion == barrierRegion &&
                  event.sequence < eventSequence) {
                eventSequence = event.sequence;
                eventIndex = index;
              }
            }
            uint64_t replaceableSequence = UINT64_MAX;
            ReplaceableEventFeatureState *replaceable =
                obelisk_rt_replaceable_events(context);
            ReplaceableEventCalendar::iterator replaceableEvent;
            if (replaceable) {
              replaceableEvent = replaceable->calendar.end();
              auto event = replaceable->calendar.begin();
              if (event != replaceable->calendar.end() &&
                  event->first.first <= context->schedulerTime &&
                  event->second.execRegion == barrierRegion) {
                replaceableSequence = event->second.sequence;
                replaceableEvent = event;
              }
            }
            uint64_t passSequence = UINT64_MAX;
            if (barrierRegion == OBELISK_RT_REGION_ACTIVE &&
                !context->scheduledPassSwitchEvents.empty()) {
              const auto &next = *context->scheduledPassSwitchEvents.begin();
              if (next.first.first <= context->schedulerTime)
                passSequence = next.first.second;
            }
            uint64_t designSequence = UINT64_MAX;
            size_t designIndex = 0;
            for (size_t index = 0; index != context->scheduledDesignNBAs.size();
                 ++index) {
              const ScheduledDesignNBA &update =
                  context->scheduledDesignNBAs[index];
              if (update.dueTime <= context->schedulerTime &&
                  update.execRegion == barrierRegion &&
                  update.sequence < designSequence) {
                designSequence = update.sequence;
                designIndex = index;
              }
            }
            uint64_t managedSequence = UINT64_MAX;
            size_t managedIndex = 0;
            for (size_t index = 0;
                 index != context->scheduledManagedNBAs.size(); ++index) {
              const ScheduledManagedNBA &update =
                  context->scheduledManagedNBAs[index];
              if (update.dueTime <= context->schedulerTime &&
                  update.execRegion == barrierRegion &&
                  update.sequence < managedSequence) {
                managedSequence = update.sequence;
                managedIndex = index;
              }
            }
            uint64_t pathSequence = UINT64_MAX;
            auto pathUpdate = context->scheduledInertialPathNBAs.end();
            if (barrierRegion == OBELISK_RT_REGION_ACTIVE &&
                !context->scheduledInertialPathNBAs.empty()) {
              auto candidate = context->scheduledInertialPathNBAs.begin();
              if (candidate->first.first <= context->schedulerTime) {
                pathSequence = candidate->second.sequence;
                pathUpdate = candidate;
              }
            }
            uint64_t sequence = std::min(
                std::min(std::min(nativeSequence, managedSequence),
                         pathSequence),
                std::min(std::min(std::min(eventSequence, replaceableSequence),
                                  passSequence),
                         designSequence));
            if (sequence == UINT64_MAX) {
              bool hadDelayedPublications = !delayedNetPublications.empty();
              if (!flushDelayedNetPublications())
                return context->schedulerStatus;
              if (hadDelayedPublications)
                continue;
              break;
            }
            if (sequence == nativeSequence) {
              if (!prepareDelayedNetBatch(context->scheduledNBAs[nativeIndex]))
                return context->schedulerStatus;
            } else if (sequence == pathSequence) {
              if (!prepareDelayedNetBatch(pathUpdate->second))
                return context->schedulerStatus;
            } else if (!flushDelayedNetPublications()) {
              return context->schedulerStatus;
            }
            if (sequence == nativeSequence) {
              uint32_t retainedAutomaticID =
                  context->scheduledNBAs[nativeIndex].retainedAutomaticID;
              bool pathCurrent =
                  currentPathUpdate(context->scheduledNBAs[nativeIndex]);
              if (pathCurrent)
                completeInertial(context->scheduledNBAs[nativeIndex]);
              context->schedulerApplyingNativeUpdate = pathCurrent;
              if (pathCurrent)
                applyNative(context->scheduledNBAs[nativeIndex]);
              context->schedulerApplyingNativeUpdate = false;
              if (context->schedulerStatus != OBELISK_RT_OK)
                return context->schedulerStatus;
              if (retainedAutomaticID != 0) {
                auto found =
                    context->nativeAutomaticStates.find(retainedAutomaticID);
                if (found == context->nativeAutomaticStates.end() ||
                    found->second.referenceCount == 0)
                  return OBELISK_RT_INVALID_HANDLE;
                if (--found->second.referenceCount == 0) {
                  obelisk_rt_erase_automatic_bookkeeping_unlocked(
                      context, retainedAutomaticID);
                  context->nativeAutomaticStates.erase(found);
                }
              }
              context->scheduledNBAs.erase(context->scheduledNBAs.begin() +
                                           nativeIndex);
            } else if (sequence == pathSequence) {
              ScheduledNBA update = std::move(pathUpdate->second);
              context->scheduledInertialPathNBAs.erase(pathUpdate);
              if (update.execRegion != OBELISK_RT_REGION_ACTIVE) {
                context->scheduledNBAs.push_back(std::move(update));
                continue;
              }
              bool pathCurrent = currentPathUpdate(update);
              if (pathCurrent)
                completeInertial(update);
              context->schedulerApplyingNativeUpdate = pathCurrent;
              if (pathCurrent)
                applyNative(update);
              context->schedulerApplyingNativeUpdate = false;
              if (context->schedulerStatus != OBELISK_RT_OK)
                return context->schedulerStatus;
            } else if (sequence == managedSequence) {
              obelisk_rt_status status = obelisk_rt_apply_managed_nba(
                  context, context->scheduledManagedNBAs[managedIndex]);
              context->scheduledManagedNBAs.erase(
                  context->scheduledManagedNBAs.begin() + managedIndex);
              if (status != OBELISK_RT_OK)
                return status;
            } else if (sequence == passSequence) {
              auto scheduled = context->scheduledPassSwitchEvents.begin();
              ScheduledPassSwitchEvent event = scheduled->second;
              bool switchChanged = false;
              obelisk_rt_status status = OBELISK_RT_INVALID_DESIGN;
              if (event.delayedMosEdge != UINT64_MAX) {
                auto pending =
                    context->delayedMosPending.find(event.delayedMosEdge);
                if (pending == context->delayedMosPending.end() ||
                    pending->second.event != scheduled ||
                    pending->second.strengths != event.strengths)
                  return OBELISK_RT_INVALID_DESIGN;
                context->scheduledPassSwitchEvents.erase(scheduled);
                context->delayedMosPending.erase(pending);
                status = obelisk::designbytecode::applyDelayedMosEvent(
                    context, event.delayedMosEdge, event.strengths,
                    switchChanged);
              } else {
                auto pending =
                    context->delayedPassSwitchPending.find(event.passSwitchID);
                if (pending == context->delayedPassSwitchPending.end() ||
                    pending->second.event != scheduled ||
                    pending->second.state != event.state)
                  return OBELISK_RT_INVALID_DESIGN;
                context->scheduledPassSwitchEvents.erase(scheduled);
                context->delayedPassSwitchPending.erase(pending);
                status = obelisk::designbytecode::applyPassSwitchControl(
                    context, event.passSwitchID, event.state, switchChanged);
              }
              if (status != OBELISK_RT_OK)
                return status;
              changed |= switchChanged;
            } else if (sequence == eventSequence) {
              uint64_t stableID =
                  context->scheduledDesignEvents[eventIndex].stableID;
              uint32_t retainedAutomaticID =
                  context->scheduledDesignEvents[eventIndex]
                      .retainedAutomaticID;
              context->scheduledDesignEvents.erase(
                  context->scheduledDesignEvents.begin() + eventIndex);
              EventState &event = context->events[stableID];
              if (++event.generation == 0)
                event.generation = 1;
              event.lastTriggeredTime = context->schedulerTime;
              bool notified =
                  obelisk_rt_notify_event_order_waiters_unlocked(context,
                                                                 stableID) &&
                  obelisk_rt_notify_observer_event_unlocked(context, stableID);
              if (retainedAutomaticID != 0) {
                auto found =
                    context->nativeAutomaticStates.find(retainedAutomaticID);
                if (found == context->nativeAutomaticStates.end() ||
                    found->second.referenceCount == 0)
                  return OBELISK_RT_INVALID_HANDLE;
                if (--found->second.referenceCount == 0) {
                  obelisk_rt_erase_automatic_bookkeeping_unlocked(
                      context, retainedAutomaticID);
                  context->nativeAutomaticStates.erase(found);
                }
              }
              if (!notified)
                return context->schedulerStatus;
              eventTriggered = true;
            } else if (sequence == replaceableSequence) {
              if (!replaceable ||
                  replaceableEvent == replaceable->calendar.end())
                return OBELISK_RT_INVALID_DESIGN;
              ScheduledDesignEvent scheduled = replaceableEvent->second;
              auto pending = replaceable->pending.find(scheduled.stableID);
              if (pending == replaceable->pending.end() ||
                  !pending->second.scheduled ||
                  pending->second.event != replaceableEvent)
                return OBELISK_RT_INVALID_DESIGN;
              pending->second.scheduled = false;
              replaceable->calendar.erase(replaceableEvent);
              EventState &event = context->events[scheduled.stableID];
              if (++event.generation == 0)
                event.generation = 1;
              event.lastTriggeredTime = context->schedulerTime;
              if (!obelisk_rt_notify_event_order_waiters_unlocked(
                      context, scheduled.stableID) ||
                  !obelisk_rt_notify_observer_event_unlocked(
                      context, scheduled.stableID))
                return context->schedulerStatus;
              eventTriggered = true;
            } else {
              if (!applyDesign(context->scheduledDesignNBAs[designIndex]))
                return OBELISK_RT_INVALID_HANDLE;
              context->scheduledDesignNBAs.erase(
                  context->scheduledDesignNBAs.begin() + designIndex);
            }
          }
        if ((changed || eventTriggered) && ++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
        if (context->schedulerSlotProgress == maxSlotProgress) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
          return context->schedulerStatus;
        }
        ++context->schedulerSlotProgress;
        if (context->nativeScheduleSingleStep)
          return OBELISK_RT_OK;
        continue;
      }
      if (!selected && !context->schedulerRunningFinals) {
        if (context->staticNBASlowRootsPresent) {
          std::fill(context->staticNBASlowRoots.begin(),
                    context->staticNBASlowRoots.end(), uint8_t{0});
          context->staticNBASlowRootsPresent = false;
        }
        if (context->nativeScheduleExternalWritePending) {
          if (context->nativeSchedulePlan->state_bit_count != 0 &&
              !reconcileNativeDirtyRootsToPlanesUnlocked(
                  context, context->nativeSchedulePlan)) {
            context->schedulerStatus = OBELISK_RT_LAYOUT_MISMATCH;
            return context->schedulerStatus;
          }
          context->nativeScheduleExternalWritePending = false;
          std::fill(context->nativeScheduleTransientDirtyMask.begin(),
                    context->nativeScheduleTransientDirtyMask.end(),
                    uint64_t{0});
          std::fill(context->nativeScheduleTransientDirtySummary.begin(),
                    context->nativeScheduleTransientDirtySummary.end(),
                    uint64_t{0});
          context->nativeScheduleTransientDirtyRoots.clear();
          context->nativeScheduleDirtyRootsPresent =
              !context->nativeSchedulePersistentDirtyRoots.empty();
        }
        refreshNativeStaticSpecializationFastUnlocked(context);
        // Runtime intervention only needs to reach a canonical, quiescent
        // boundary before returning control.  Whether that state is
        // two-state is a generated-kernel routing decision: persistent X/Z
        // must re-enter the four-state Tier-1 variant instead of trapping the
        // whole model in the fine scheduler.
        if (context->nativeScheduleStopAtCleanBoundary &&
            nativeAOTTransientBoundaryClean(context)) {
          context->nativeScheduleCleanBoundaryReached = true;
          return context->schedulerFinishStatus;
        }
        // A control-only arbitration step is bounded by the generated node
        // selected by its caller. If no earlier design task or NBA exists,
        // return without consuming the next calendar entry so that caller
        // can execute that node directly.
        if (context->nativeScheduleControlOnly || options.currentSlotOnly)
          return context->schedulerFinishStatus;
        std::optional<uint64_t> nextTime;
        auto considerTime = [&](uint64_t candidate) {
          if (candidate > context->schedulerTime &&
              (!nextTime || candidate < *nextTime))
            nextTime = candidate;
        };
        if (context->nextVPITimeCallback)
          considerTime(*context->nextVPITimeCallback);
        if (std::optional<uint64_t> wakeTime =
                nextScheduledProcessDelayUnlocked(context))
          considerTime(*wakeTime);
        for (const ScheduledDesignTask &candidate :
             context->scheduledDesignTasks)
          if (!candidate.terminated && candidate.phase == 0 &&
              candidate.started &&
              candidate.suspendKind == OBELISK_RT_SUSPEND_DELAY)
            considerTime(candidate.wakeTime);
        for (const ScheduledDesignNBA &update : context->scheduledDesignNBAs)
          if (update.dueTime > context->schedulerTime)
            considerTime(update.dueTime);
        for (const ScheduledNBA &update : context->scheduledNBAs)
          if (!update.cancelled && update.dueTime > context->schedulerTime)
            considerTime(update.dueTime);
        if (!context->scheduledInertialPathNBAs.empty())
          considerTime(context->scheduledInertialPathNBAs.begin()->first.first);
        for (const ScheduledManagedNBA &update : context->scheduledManagedNBAs)
          if (update.dueTime > context->schedulerTime)
            considerTime(update.dueTime);
        for (const ScheduledDesignEvent &event : context->scheduledDesignEvents)
          if (event.dueTime > context->schedulerTime)
            considerTime(event.dueTime);
        if (const ReplaceableEventFeatureState *replaceable =
                obelisk_rt_replaceable_events(context);
            replaceable && !replaceable->calendar.empty())
          considerTime(replaceable->calendar.begin()->first.first);
        if (!context->scheduledPassSwitchEvents.empty())
          considerTime(context->scheduledPassSwitchEvents.begin()->first.first);
        if (nextTime) {
          obelisk_rt_dump_slot_unlocked(context);
          context->schedulerTime = *nextTime;
          if (context->nativeScheduleSingleStep)
            return OBELISK_RT_OK;
          continue;
        }
        bool hasFinal = false;
        if (context->scheduledFinalProcessPresent) {
          for (const ScheduledProcess &candidate : context->scheduledProcesses)
            hasFinal |= candidate.instance && candidate.phase == 1;
        }
        for (const ScheduledDesignTask &candidate :
             context->scheduledDesignTasks)
          hasFinal |= !candidate.terminated && candidate.phase == 1;
        if (hasFinal) {
          context->schedulerRunningFinals = true;
          if (context->nativeScheduleSingleStep)
            return OBELISK_RT_OK;
          continue;
        }
      }
    }
    if (!selected) {
      ContextMutexLock lock(context);
      return context->schedulerFinishStatus;
    }

    if (context->nativeScheduleForcedSlot != UINT32_MAX)
      context->nativeScheduleForcedExecuted = true;
    {
      ContextMutexLock lock(context);
      if (context->activeNativeProcess)
        return OBELISK_RT_INVALID_LIFECYCLE;
      context->activeNativeProcess = selected;
      context->activeHomeRegion =
          context->scheduledProcesses[selectedIndex].homeRegion;
      context->activeExecRegion =
          context->scheduledProcesses[selectedIndex].queuedRegion;
      context->activeLogicalProcessToken =
          kNativeLogicalProcessTag |
          context->scheduledProcesses[selectedIndex].token;
      context->activeProgramOwner =
          context->scheduledProcesses[selectedIndex].programOwner;
      context->controlEscapePending = false;
      context->activeLogicalProcessParent =
          context->scheduledProcesses[selectedIndex].parent;
      context->activeWaitOrderFailed =
          selectedResuming &&
          context->scheduledProcesses[selectedIndex].suspendKind ==
              OBELISK_RT_SUSPEND_EVENT_ORDER &&
          context->scheduledProcesses[selectedIndex].waitOrderReady &&
          context->scheduledProcesses[selectedIndex].waitOrderFailed;
      if (selectedResuming)
        obelisk_rt_flush_deferred_immediate_reports_unlocked(
            context, context->activeLogicalProcessToken);
      context->activeControls =
          std::move(context->scheduledProcesses[selectedIndex].controls);
    }
    obelisk_rt_fragment_action_v1 action{};
    obelisk_rt_execution_tier tier = selected->tier;
    const ScheduledProcess &scheduled =
        context->scheduledProcesses[selectedIndex];
    bool requireBytecode =
        context->execution && (context->execution->flags &
                               OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0;
    if (requireBytecode) {
      tier = OBELISK_RT_TIER_BYTECODE;
    } else if (scheduled.aotActorSlot != UINT32_MAX) {
      bool bytecodeFragment = std::binary_search(
          scheduled.bytecodeContinuations.begin(),
          scheduled.bytecodeContinuations.end(), selected->continuation);
      tier =
          bytecodeFragment ? OBELISK_RT_TIER_BYTECODE : OBELISK_RT_TIER_NATIVE;
      // External writes fracture the fused Tier-1 schedule, but supported
      // fragments remain native while the fine scheduler converges them.
      // Only an explicitly bytecode-only continuation enters Tier 3.
    }
    if (tier != OBELISK_RT_TIER_NATIVE && tier != OBELISK_RT_TIER_BYTECODE)
      tier =
          (selected->descriptor->available_tiers & OBELISK_RT_TIER_MASK_NATIVE)
              ? OBELISK_RT_TIER_NATIVE
              : OBELISK_RT_TIER_BYTECODE;
    obelisk_rt_status status = obelisk_rt_v1_process_instance_execute(
        selected, context, tier, &action);
    bool terminationRequested = false;
    bool killRequested = false;
    auto clearActiveProcess = [&](bool dpiDisabled) {
      ContextMutexLock lock(context);
      if (selectedIndex < context->scheduledProcesses.size() &&
          context->scheduledProcesses[selectedIndex].instance == selected) {
        context->scheduledProcesses[selectedIndex].controls =
            std::move(context->activeControls);
        killRequested |=
            context->killedNativeProcesses.count(
                context->scheduledProcesses[selectedIndex].token) != 0;
      }
      if (dpiDisabled && context->activeDpiExportTaskLogical ==
                             context->activeLogicalProcessToken)
        context->activeDpiExportTaskDisabled = true;
      context->activeControls.clear();
      context->activeNativeProcess = nullptr;
      context->activeHomeRegion = UINT32_MAX;
      context->activeExecRegion = UINT32_MAX;
      context->activeLogicalProcessToken = 0;
      context->activeProgramOwner = 0;
      context->controlEscapePending = false;
      context->activeLogicalProcessParent = 0;
      context->activeWaitOrderFailed = false;
      terminationRequested = context->schedulerFinishRequested;
    };
    if (status != OBELISK_RT_OK) [[unlikely]] {
      bool dpiDisabled = status == OBELISK_RT_DPI_DISABLE_UNSUPPORTED;
      clearActiveProcess(dpiDisabled);
      if (dpiDisabled) {
        killRequested = true;
        status = OBELISK_RT_OK;
        action = {
            OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
      } else if (!terminationRequested) {
        return status;
      }
    } else {
      clearActiveProcess(false);
    }
    auto destroyPendingCallee =
        [](obelisk_rt_process_instance_v1 *instance) noexcept {
          if (instance)
            (void)obelisk_rt_v1_process_instance_destroy(instance);
        };
    std::unique_ptr<obelisk_rt_process_instance_v1,
                    decltype(destroyPendingCallee)>
        pendingCallee(nullptr, destroyPendingCallee);
    if (status == OBELISK_RT_OK &&
        action.kind == OBELISK_RT_FRAGMENT_TASK_CALL) {
      auto *callee = reinterpret_cast<obelisk_rt_process_instance_v1 *>(
          static_cast<uintptr_t>(action.payload));
      if (!callee || callee == selected)
        return OBELISK_RT_INVALID_LIFECYCLE;
      pendingCallee.reset(callee);
      if (callee->lifecycle != OBELISK_RT_PROCESS_READY ||
          !callee->descriptor ||
          callee->descriptor->execution != selected->descriptor->execution ||
          (callee->ownership_context && callee->ownership_context != context))
        return OBELISK_RT_INVALID_LIFECYCLE;
    }
    if (terminationRequested)
      action = {
          OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    bool destroy = false;
    std::vector<obelisk_rt_process_instance_v1 *> terminatedCallers;
    {
      ContextMutexLock lock(context);
      if (selectedIndex >= context->scheduledProcesses.size() ||
          context->scheduledProcesses[selectedIndex].instance != selected)
        return OBELISK_RT_INVALID_LIFECYCLE;
      ScheduledProcess &scheduled = context->scheduledProcesses[selectedIndex];
      if (action.kind == OBELISK_RT_FRAGMENT_TERMINATE) {
        if (!scheduled.signalSubscriptions.empty() ||
            scheduled.computedObserverWaitRegistered)
          obelisk_rt_unregister_signal_wait_unlocked(
              context, scheduled.signalSubscriptions, scheduled.token, false);
        if (!scheduled.callers.empty() && !context->schedulerFinishRequested &&
            !killRequested) {
          if (scheduled.callerControlDepths.size() != scheduled.callers.size())
            return OBELISK_RT_INVALID_LIFECYCLE;
          scheduled.instance = scheduled.callers.back();
          scheduled.callers.pop_back();
          scheduled.callerControlDepths.pop_back();
          scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
          scheduled.waitOffset = 0;
          scheduled.waitSize = 0;
          scheduled.waitGenerations.clear();
          scheduled.signalTriggered = false;
          scheduled.urgent = true;
          scheduled.queuedRegion = scheduled.homeRegion;
          destroy = true;
        } else {
          uint64_t token = scheduled.token;
          obelisk_rt_program_complete_unlocked(context,
                                               kNativeLogicalProcessTag | token,
                                               scheduled.programOwner);
          if (scheduled.rootProcess) {
            if (tier == OBELISK_RT_TIER_NATIVE &&
                !importNativeRootInitializerPlanesUnlocked(context))
              return OBELISK_RT_LAYOUT_MISMATCH;
            obelisk_rt_program_seal_unlocked(context);
          }
          obelisk_rt_reparent_process_children_unlocked(
              context, kNativeLogicalProcessTag | token, scheduled.parent);
          context->terminatedNativeProcesses.insert(token, scheduled.random);
          scheduled.instance = nullptr;
          ++context->schedulerDeadProcessCount;
          context->schedulerCompactionPending = true;
          if (terminationRequested || killRequested)
            terminatedCallers.swap(scheduled.callers);
          scheduled.callerControlDepths.clear();
          obelisk_rt_release_controls_unlocked(context, scheduled.controls);
          scheduled.controls.clear();
          destroy = true;
          scheduled.signalTriggered = false;
          scheduled.urgent = false;
          if (++context->schedulerEpoch == 0)
            context->schedulerEpoch = 1;
        }
      } else if (action.kind == OBELISK_RT_FRAGMENT_SUSPEND) {
        status = adoptScheduledSuspendUnlocked(context, scheduled, action);
        if (status != OBELISK_RT_OK)
          return status;
      } else if (action.kind == OBELISK_RT_FRAGMENT_PROCESS_SUSPEND) {
        if (!scheduled.signalSubscriptions.empty() ||
            scheduled.computedObserverWaitRegistered)
          obelisk_rt_unregister_signal_wait_unlocked(
              context, scheduled.signalSubscriptions, scheduled.token, false);
        scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
        scheduled.waitOffset = 0;
        scheduled.waitSize = 0;
        scheduled.waitGenerations.clear();
        scheduled.signalTriggered = false;
        scheduled.explicitlySuspended = true;
        scheduled.urgent = false;
        scheduled.queuedRegion = scheduled.homeRegion;
      } else if (action.kind == OBELISK_RT_FRAGMENT_TASK_CALL) {
        auto *callee = pendingCallee.get();
        if (!callee)
          return OBELISK_RT_INVALID_LIFECYCLE;
        if (scheduled.callers.size() == std::numeric_limits<size_t>::max())
          return OBELISK_RT_OUT_OF_RESOURCES;
        scheduled.callers.reserve(scheduled.callers.size() + 1);
        scheduled.callerControlDepths.reserve(
            scheduled.callerControlDepths.size() + 1);
        if (!scheduled.signalSubscriptions.empty() ||
            scheduled.computedObserverWaitRegistered)
          obelisk_rt_unregister_signal_wait_unlocked(
              context, scheduled.signalSubscriptions, scheduled.token, false);
        status = suspendNativeAOTTaskCallerUnlocked(context, scheduled);
        if (status != OBELISK_RT_OK)
          return status;
        scheduled.callers.push_back(selected);
        scheduled.callerControlDepths.push_back(scheduled.controls.size());
        scheduled.instance = callee;
        scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
        scheduled.waitOffset = 0;
        scheduled.waitSize = 0;
        scheduled.waitGenerations.clear();
        scheduled.signalTriggered = false;
        scheduled.urgent = true;
        scheduled.queuedRegion = scheduled.homeRegion;
        pendingCallee.release();
      } else {
        if (!scheduled.signalSubscriptions.empty() ||
            scheduled.computedObserverWaitRegistered)
          obelisk_rt_unregister_signal_wait_unlocked(
              context, scheduled.signalSubscriptions, scheduled.token, false);
        scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
        scheduled.waitOffset = 0;
        scheduled.waitSize = 0;
        scheduled.waitGenerations.clear();
        scheduled.signalTriggered = false;
        scheduled.urgent = false;
        scheduled.queuedRegion = scheduled.homeRegion;
      }
      if (!scheduled.instance &&
          scheduled.suspendedAOTActorSlot != UINT32_MAX) {
        uint32_t slot = scheduled.suspendedAOTActorSlot;
        context->nativeScheduleActorTokens[slot] = 0;
        context->nativeScheduleActorIndices[slot] = SIZE_MAX;
        scheduled.suspendedAOTActorSlot = UINT32_MAX;
      }
      if (destroy && scheduled.aotActorSlot != UINT32_MAX) {
        uint32_t slot = scheduled.aotActorSlot;
        if (!context->nativeSchedulePlan ||
            slot >= context->nativeScheduleActors.size())
          return OBELISK_RT_INVALID_LIFECYCLE;
        if (context->nativeScheduleActors[slot] != scheduled.instance) {
          if (context->nativeScheduleActors[slot] != selected)
            return OBELISK_RT_INVALID_LIFECYCLE;
          status = context->nativeSchedulePlan->bind(
              context->nativeSchedulePlan->mutable_state, context, slot,
              scheduled.instance);
          if (status != OBELISK_RT_OK)
            return status;
          context->nativeScheduleActors[slot] = scheduled.instance;
          context->nativeScheduleActorTokens[slot] =
              scheduled.instance ? scheduled.token : 0;
          context->nativeScheduleActorIndices[slot] =
              scheduled.instance ? selectedIndex : SIZE_MAX;
        }
      }
      if (scheduled.instance && !indexedSignalBlocked(scheduled))
        context->nativePollCandidates.insert(scheduled.token);
      else
        context->nativePollCandidates.erase(scheduled.token);
      readmitNativeCandidate(scheduled, selectedIndex,
                             cachedNativeSelection.has_value());
    }
    if (destroy) {
      status = obelisk_rt_v1_process_instance_destroy(selected);
      if (status != OBELISK_RT_OK)
        return status;
    }
    for (auto caller = terminatedCallers.rbegin();
         caller != terminatedCallers.rend(); ++caller) {
      status = obelisk_rt_v1_process_instance_destroy(*caller);
      if (status != OBELISK_RT_OK)
        return status;
    }
    status = recordSlotProgress();
    if (status != OBELISK_RT_OK)
      return status;
    if (context->nativeScheduleSingleStep)
      return OBELISK_RT_OK;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_prime(obelisk_rt_context *context,
                              obelisk_rt_process_instance_v1 *instance) {
  if (!context || !instance)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  struct ActiveStateGuard {
    obelisk_rt_context *context;
    obelisk_rt_process_instance_v1 *native = context->activeNativeProcess;
    uint64_t logical = context->activeLogicalProcessToken;
    uint64_t logicalParent = context->activeLogicalProcessParent;
    uint64_t design = context->activeDesignTaskID;
    ScheduledDesignTask *designTask = context->activeDesignTask;
    uint32_t phase = context->activeDesignTaskPhase;
    uint32_t home = context->activeHomeRegion;
    uint32_t region = context->activeExecRegion;
    bool waitOrderFailed = context->activeWaitOrderFailed;
    bool designExecuting = context->designTaskExecuting;
    bool escapePending = context->controlEscapePending;
    obelisk_rt_random_state_v1 *random = context->activeRandom;
    bool designFilter = context->nativeScheduleDesignTaskFilterActive;
    uint64_t forcedDesignTask = context->nativeScheduleForcedDesignTask;
    std::vector<uint64_t> controls = std::move(context->activeControls);

    ~ActiveStateGuard() noexcept {
      context->activeControls = std::move(controls);
      context->activeNativeProcess = native;
      context->activeLogicalProcessToken = logical;
      context->activeLogicalProcessParent = logicalParent;
      context->activeDesignTaskID = design;
      context->activeDesignTask = designTask;
      context->activeDesignTaskPhase = phase;
      context->activeHomeRegion = home;
      context->activeExecRegion = region;
      context->activeWaitOrderFailed = waitOrderFailed;
      context->designTaskExecuting = designExecuting;
      context->controlEscapePending = escapePending;
      context->activeRandom = random;
      obelisk_rt_set_design_task_filter_unlocked(context, designFilter,
                                                 forcedDesignTask);
    }
  } activeState{context};
  OBELISK_RT_TRY {
    uint64_t token = 0;
    {
      ContextMutexLock lock(context);
      auto found = context->scheduledProcesses.end();
      // prime_on_spawn is normally called immediately after appending the
      // detached actor. Avoid rescanning every prior actor in a large spawn
      // cohort, while retaining the exact general lookup for direct callers.
      if (!context->scheduledProcesses.empty() &&
          context->scheduledProcesses.back().instance == instance)
        found = std::prev(context->scheduledProcesses.end());
      else
        found = std::find_if(context->scheduledProcesses.begin(),
                             context->scheduledProcesses.end(),
                             [&](const ScheduledProcess &entry) {
                               return entry.instance == instance;
                             });
      if (found == context->scheduledProcesses.end() || found->started) {
        return OBELISK_RT_INVALID_LIFECYCLE;
      }
      token = found->token;
      found->started = true;
      found->observedEpoch = context->schedulerEpoch;
      if (found->aotActorSlot != UINT32_MAX) {
        uint32_t node = findNativeAOTNodeUnlocked(
            context, found->aotActorSlot, found->instance->continuation);
        if (node != UINT32_MAX)
          clearNativeAOTNodeReadyUnlocked(context, node);
      }
    }

    for (uint32_t step = 0; step != 1024; ++step) {
      size_t index = 0;
      obelisk_rt_execution_tier tier = OBELISK_RT_TIER_NATIVE;
      {
        ContextMutexLock lock(context);
        auto indexed = context->scheduledProcessIndices.find(token);
        if (indexed == context->scheduledProcessIndices.end() ||
            indexed->second >= context->scheduledProcesses.size())
          return OBELISK_RT_INVALID_LIFECYCLE;
        index = indexed->second;
        ScheduledProcess &scheduled = context->scheduledProcesses[index];
        if (scheduled.instance != instance)
          return OBELISK_RT_INVALID_LIFECYCLE;
        context->activeNativeProcess = nullptr;
        context->activeLogicalProcessToken = kNativeLogicalProcessTag | token;
        context->controlEscapePending = false;
        context->activeLogicalProcessParent = scheduled.parent;
        context->activeHomeRegion = scheduled.homeRegion;
        context->activeExecRegion = scheduled.queuedRegion;
        context->activeWaitOrderFailed = false;
        context->activeControls = std::move(scheduled.controls);
        bool requireBytecode =
            context->execution && (context->execution->flags &
                                   OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0;
        if (requireBytecode)
          tier = OBELISK_RT_TIER_BYTECODE;
        else if (scheduled.aotActorSlot != UINT32_MAX) {
          bool bytecodeFragment = std::binary_search(
              scheduled.bytecodeContinuations.begin(),
              scheduled.bytecodeContinuations.end(), instance->continuation);
          tier = bytecodeFragment ? OBELISK_RT_TIER_BYTECODE
                                  : OBELISK_RT_TIER_NATIVE;
        } else if (instance->tier == OBELISK_RT_TIER_NATIVE ||
                   instance->tier == OBELISK_RT_TIER_BYTECODE)
          tier = static_cast<obelisk_rt_execution_tier>(instance->tier);
        else if ((instance->descriptor->available_tiers &
                  OBELISK_RT_TIER_MASK_NATIVE) == 0)
          tier = OBELISK_RT_TIER_BYTECODE;
      }

      obelisk_rt_fragment_action_v1 action{};
      obelisk_rt_status status = obelisk_rt_v1_process_instance_execute(
          instance, context, tier, &action);
      {
        ContextMutexLock lock(context);
        auto indexed = context->scheduledProcessIndices.find(token);
        if (indexed == context->scheduledProcessIndices.end() ||
            indexed->second >= context->scheduledProcesses.size() ||
            context->scheduledProcesses[indexed->second].instance != instance)
          return OBELISK_RT_INVALID_LIFECYCLE;
        ScheduledProcess &scheduled =
            context->scheduledProcesses[indexed->second];
        scheduled.controls = std::move(context->activeControls);
        if (status == OBELISK_RT_OK &&
            action.kind == OBELISK_RT_FRAGMENT_SUSPEND)
          status = adoptScheduledSuspendUnlocked(context, scheduled, action);
        else if (status == OBELISK_RT_OK &&
                 action.kind == OBELISK_RT_FRAGMENT_CONTINUE) {
          scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
          scheduled.waitOffset = 0;
          scheduled.waitSize = 0;
          scheduled.waitGenerations.clear();
          scheduled.signalTriggered = false;
          scheduled.urgent = false;
          scheduled.queuedRegion = scheduled.homeRegion;
          updateNativeAOTContinuationRank(scheduled, action.continuation);
        } else if (status == OBELISK_RT_OK) {
          status = OBELISK_RT_INVALID_LIFECYCLE;
        }
        if (status == OBELISK_RT_OK &&
            action.kind == OBELISK_RT_FRAGMENT_SUSPEND) {
          if (indexedSignalBlocked(scheduled))
            context->nativePollCandidates.erase(token);
          else
            context->nativePollCandidates.insert(token);
          return OBELISK_RT_OK;
        }
        if (status != OBELISK_RT_OK)
          return status;
      }
    }
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_run(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    // One scheduler invocation is one serialized runtime transaction.  Hold
    // the context mutex across the generated/native execution scope so nested
    // state, fanout, and NBA helpers do not repeatedly acquire the recursive
    // lock.  Evaluator callbacks use ContextCallbackUnlock and therefore keep
    // the existing reentrancy contract while arbitrary user code runs.
    NativeAOTMutexScope schedulerLock(context);
    return runScheduler(context);
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

obelisk_rt_status
obelisk_rt_run_dpi_export_task_logical(obelisk_rt_context *context,
                                       uint64_t logical) noexcept {
  if (!context || logical == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  // Re-entry originates while the importing logical process is executing C.
  // Detach that host activation temporarily, leaving its canonical frame and
  // C stack intact while the exported task advances in its selected tier.
  struct ActiveStateGuard {
    obelisk_rt_context *context;
    obelisk_rt_process_instance_v1 *native = context->activeNativeProcess;
    uint64_t logical = context->activeLogicalProcessToken;
    uint64_t logicalParent = context->activeLogicalProcessParent;
    uint64_t design = context->activeDesignTaskID;
    ScheduledDesignTask *designTask = context->activeDesignTask;
    uint32_t phase = context->activeDesignTaskPhase;
    uint32_t home = context->activeHomeRegion;
    uint32_t region = context->activeExecRegion;
    bool waitOrderFailed = context->activeWaitOrderFailed;
    bool designExecuting = context->designTaskExecuting;
    bool escapePending = context->controlEscapePending;
    obelisk_rt_random_state_v1 *random = context->activeRandom;
    uint64_t programOwner = context->activeProgramOwner;
    uint64_t dpiLogical = context->activeDpiExportTaskLogical;
    bool dpiDisabled = context->activeDpiExportTaskDisabled;
    bool nativeWasExplicitlySuspended = false;
    bool nativeWasScheduled = false;
    std::vector<uint64_t> controls = std::move(context->activeControls);

    ~ActiveStateGuard() noexcept {
      context->activeControls = std::move(controls);
      context->activeNativeProcess = native;
      context->activeLogicalProcessToken = logical;
      context->activeLogicalProcessParent = logicalParent;
      context->activeDesignTaskID = design;
      context->activeDesignTask = designTask;
      context->activeDesignTaskPhase = phase;
      context->activeHomeRegion = home;
      context->activeExecRegion = region;
      context->activeWaitOrderFailed = waitOrderFailed;
      context->designTaskExecuting = designExecuting;
      context->controlEscapePending = escapePending;
      context->activeRandom = random;
      context->activeProgramOwner = programOwner;
      context->activeDpiExportTaskLogical = dpiLogical;
      context->activeDpiExportTaskDisabled = dpiDisabled;
      if (nativeWasScheduled)
        for (ScheduledProcess &process : context->scheduledProcesses)
          if (process.instance == native) {
            process.explicitlySuspended = nativeWasExplicitlySuspended;
            break;
          }
    }
  } active{context};
  {
    ContextMutexLock lock(context);
    if (active.native)
      for (ScheduledProcess &process : context->scheduledProcesses)
        if (process.instance == active.native) {
          active.nativeWasExplicitlySuspended = process.explicitlySuspended;
          active.nativeWasScheduled = true;
          process.explicitlySuspended = true;
          break;
        }
    context->activeNativeProcess = nullptr;
    context->activeLogicalProcessToken = 0;
    context->activeLogicalProcessParent = 0;
    context->activeDesignTaskID = 0;
    context->activeDesignTask = nullptr;
    context->activeDesignTaskPhase = 0;
    context->activeHomeRegion = UINT32_MAX;
    context->activeExecRegion = UINT32_MAX;
    context->activeWaitOrderFailed = false;
    context->designTaskExecuting = false;
    context->controlEscapePending = false;
    context->activeRandom = nullptr;
    context->activeProgramOwner = 0;
    context->activeControls.clear();
    context->activeDpiExportTaskLogical = logical;
    context->activeDpiExportTaskDisabled = false;
  }

  // Suspend the caller's executor restriction as well as its activation.
  // The exported task runs in the same queue/time universe and restores the
  // exact outer selection when it returns, including through an error path.
  NativeScheduleStepScope exportSelection(context, UINT32_MAX, false);
  for (;;) {
    obelisk_rt_process_state state = OBELISK_RT_PROCESS_WAITING;
    obelisk_rt_status status =
        obelisk_rt_v1_process_status(context, logical, &state);
    if (status != OBELISK_RT_OK)
      return status;
    if (context->activeDpiExportTaskDisabled)
      return OBELISK_RT_DPI_DISABLE_UNSUPPORTED;
    if (state == OBELISK_RT_PROCESS_FINISHED)
      return OBELISK_RT_OK;
    if (state == OBELISK_RT_PROCESS_KILLED)
      return OBELISK_RT_OK;
    uint64_t progress = 0, time = 0, epoch = 0, selectionGeneration = 0;
    bool runningFinals = false;
    {
      ContextMutexLock lock(context);
      progress = context->schedulerSlotProgress;
      time = context->schedulerTime;
      epoch = context->schedulerEpoch;
      selectionGeneration = context->schedulerSelectionGeneration;
      runningFinals = context->schedulerRunningFinals;
    }
    status = runScheduler(context);
    if (status != OBELISK_RT_OK)
      return status;
    {
      ContextMutexLock lock(context);
      if (context->schedulerSlotProgress == progress &&
          context->schedulerTime == time && context->schedulerEpoch == epoch &&
          context->schedulerSelectionGeneration == selectionGeneration &&
          context->schedulerRunningFinals == runningFinals)
        return OBELISK_RT_INVALID_LIFECYCLE;
    }
  }
}

extern "C" OBELISK_RT_FEATURE_TEXT obelisk_rt_status
obelisk_rt_v1_dpi_export_task_run(obelisk_rt_context *context,
                                  obelisk_rt_process_instance_v1 *instance) {
  if (!context || !instance)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    uint32_t home = OBELISK_RT_REGION_ACTIVE;
    {
      ContextMutexLock lock(context);
      if (context->activeHomeRegion <= OBELISK_RT_REGION_POSTPONED)
        home = context->activeHomeRegion;
    }
    obelisk_rt_status status = obelisk_rt_v1_scheduler_add(
        context, instance, OBELISK_RT_SCHEDULE_HOME(home));
    if (status != OBELISK_RT_OK) {
      (void)obelisk_rt_v1_process_instance_destroy(instance);
      return status;
    }
    uint64_t logical = obelisk_rt_v1_scheduler_process_token(context, instance);
    if (logical == 0)
      return OBELISK_RT_INVALID_LIFECYCLE;
    return obelisk_rt_run_dpi_export_task_logical(context, logical);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}
