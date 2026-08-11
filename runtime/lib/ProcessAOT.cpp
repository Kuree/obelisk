//===- ProcessAOT.cpp - Native AOT schedule plan execution ---------------===//
//
// Execution of an installed native AOT schedule plan: trusted/untrusted node
// execution, the periodic clock fast path, checkpoint handoff back to the
// generic scheduler, deoptimization snapshots, and the external-write
// invalidation hooks that keep a plan's cached state coherent.  Split out of
// Process.cpp; the generic scheduler loop remains there.
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

//===------------------------------------------------------------------===//
// Plan readiness: dirty static roots, actor readiness, and the deadline heap
//===------------------------------------------------------------------===//

bool nativeStaticSpecializationEnvironmentClean(
    const obelisk_rt_context *context) {
  return context &&
         (!context->execution || context->execution->observer_count == 0) &&
         context->scheduledDesignTasks.empty() &&
         context->nativeComputedSignalSubscriptions == 0 &&
         context->nativeConditionalSignalWaiters.empty() &&
         context->designConditionalSignalWaiters.empty();
}

bool nativeAOTTransientBoundaryClean(const obelisk_rt_context *context) {
  if (!context || context->nativeScheduleExternalWritePending ||
      context->nativeScheduleDirtyRootsPresent)
    return false;
  auto anyOverride = [](const std::vector<uint64_t> &mask) {
    return std::any_of(mask.begin(), mask.end(),
                       [](uint64_t word) { return word != 0; });
  };
  return !anyOverride(context->forceMask) &&
         !anyOverride(context->assignMask);
}

bool canUseStaticAOTFanout(const obelisk_rt_context *context) {
  const obelisk_rt_native_schedule_plan *plan =
      context ? context->nativeSchedulePlan : nullptr;
  if (!plan ||
      ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT) == 0 &&
       !context->nativeScheduleGuardedFanoutActive) ||
      context->nativeScheduleDeoptimized || !context->execution)
    return false;
  // The activation guard's fast flag already represents this same clean
  // environment and is invalidated synchronously on every writable-VPI
  // handover. Reuse it instead of rescanning empty runtime inventories at
  // every NBA root commit.
  return (plan->specialization_fast && *plan->specialization_fast != 0) ||
         nativeStaticSpecializationEnvironmentClean(context);
}

bool canUseIndexedExternalAOTFanout(const obelisk_rt_context *context) {
  const obelisk_rt_native_schedule_plan *plan =
      context ? context->nativeSchedulePlan : nullptr;
  return plan &&
         (plan->flags & (OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT |
                         OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT)) != 0 &&
         !context->nativeScheduleDeoptimized &&
         !context->nativeScheduleExternalWritePending &&
         !context->nativeScheduleDirtyRootsPresent &&
         !context->nativeScheduleNodes.empty() &&
         !context->nativeScheduleReadyNodes.empty() &&
         nativeStaticSpecializationEnvironmentClean(context);
}

bool staticNBARootNeedsTransitions(const obelisk_rt_context *context,
                                   uint32_t rootIndex) {
  return !canUseStaticAOTFanout(context) ||
         rootIndex >= context->staticNBARootHasFanout.size() ||
         context->staticNBARootHasFanout[rootIndex] != 0;
}

bool nativeStaticRootDirty(const obelisk_rt_context *context,
                           uint32_t staticState) {
  auto dirty = [staticState](const std::vector<uint64_t> &mask,
                             const std::unordered_set<uint32_t> &sparse) {
    uint64_t word = staticState / 64;
    return word < mask.size()
               ? (mask[word] & (uint64_t{1} << (staticState % 64))) != 0
               : sparse.find(staticState) != sparse.end();
  };
  return context->nativeScheduleDirtyRootsPresent &&
         (dirty(context->nativeScheduleTransientDirtyMask,
                context->nativeScheduleTransientDirtyRoots) ||
          dirty(context->nativeSchedulePersistentDirtyMask,
                context->nativeSchedulePersistentDirtyRoots));
}

void markNativeDirtyRootUnlocked(obelisk_rt_context *context, uint32_t id,
                                 bool persistent) {
  auto &dirty = persistent ? context->nativeSchedulePersistentDirtyRoots
                           : context->nativeScheduleTransientDirtyRoots;
  auto &mask = persistent ? context->nativeSchedulePersistentDirtyMask
                          : context->nativeScheduleTransientDirtyMask;
  auto &summary = persistent ? context->nativeSchedulePersistentDirtySummary
                             : context->nativeScheduleTransientDirtySummary;
  dirty.insert(id);
  uint32_t word = id / 64;
  if (word < mask.size()) {
    mask[word] |= uint64_t{1} << (id % 64);
    uint32_t summaryWord = word / 64;
    if (summaryWord < summary.size())
      summary[summaryWord] |= uint64_t{1} << (word % 64);
  }
  context->nativeScheduleDirtyRootsPresent = true;
}

void clearNativeDirtyRootUnlocked(obelisk_rt_context *context, uint32_t id,
                                  bool persistent) {
  auto &dirty = persistent ? context->nativeSchedulePersistentDirtyRoots
                           : context->nativeScheduleTransientDirtyRoots;
  auto &mask = persistent ? context->nativeSchedulePersistentDirtyMask
                          : context->nativeScheduleTransientDirtyMask;
  auto &summary = persistent ? context->nativeSchedulePersistentDirtySummary
                             : context->nativeScheduleTransientDirtySummary;
  dirty.erase(id);
  uint32_t word = id / 64;
  if (word >= mask.size())
    return;
  mask[word] &= ~(uint64_t{1} << (id % 64));
  uint32_t summaryWord = word / 64;
  if (mask[word] == 0 && summaryWord < summary.size())
    summary[summaryWord] &= ~(uint64_t{1} << (word % 64));
}

void invalidateNativeStaticSpecializationFastUnlocked(
    obelisk_rt_context *context) {
  const obelisk_rt_native_schedule_plan *plan =
      context ? context->nativeSchedulePlan : nullptr;
  if (!plan || !plan->specialization_fast)
    return;
  *plan->specialization_fast = 0;
}

void invalidateNativeTwoStatePromotionUnlocked(obelisk_rt_context *context) {
  const obelisk_rt_native_schedule_plan *plan =
      context ? context->nativeSchedulePlan : nullptr;
  if (plan && plan->promotion_invalidate)
    plan->promotion_invalidate();
}

void refreshNativeStaticSpecializationFastUnlocked(
    obelisk_rt_context *context) {
  const obelisk_rt_native_schedule_plan *plan =
      context ? context->nativeSchedulePlan : nullptr;
  if (!plan || !plan->specialization_fast || *plan->specialization_fast != 0)
    return;
  bool slowNBA = context->staticNBASlowRootsPresent;
  *plan->specialization_fast =
      context->nativeScheduleRunning && !context->nativeScheduleDeoptimized &&
              !context->nativeScheduleExternalWritePending &&
              !context->nativeScheduleDirtyRootsPresent && !slowNBA &&
              nativeStaticSpecializationEnvironmentClean(context)
          ? 1
          : 0;
}

bool nativeAOTActorDirty(const obelisk_rt_context *context,
                         uint32_t actorSlot) {
  if (context->nativeScheduleTransientDirtyRoots.empty() &&
      context->nativeSchedulePersistentDirtyRoots.empty())
    return true;
  if (actorSlot >= context->nativeScheduleActorRootRanges.size())
    return true;
  auto [begin, end] = context->nativeScheduleActorRootRanges[actorSlot];
  bool described = begin != end;
  for (uint64_t index = begin; index != end; ++index) {
    const obelisk_rt_static_actor_root &dependency =
        context->nativeScheduleActorRoots[index];
    if (nativeStaticRootDirty(context, dependency.static_state))
      return true;
  }
  // Plans without dependency metadata retain the conservative handover.
  return !described;
}

bool nativeAOTNeedsSpecializationHandoverUnlocked(
    const obelisk_rt_context *context, uint32_t actorSlot) {
  bool dirtyActor = context->nativeScheduleDirtyRootsPresent &&
                    nativeAOTActorDirty(context, actorSlot);
  bool slowNBA = context->staticNBASlowRootsPresent;
  bool globalHandover = !context->nativeScheduleDirtyRootsPresent || slowNBA ||
                        !nativeStaticSpecializationEnvironmentClean(context);
  return dirtyActor || globalHandover;
}

uint32_t findNativeAOTNodeUnlocked(const obelisk_rt_context *context,
                                   uint32_t actorSlot, uint32_t continuation) {
  if (actorSlot >= context->nativeScheduleActorNodes.size())
    return UINT32_MAX;
  const auto &entries = context->nativeScheduleActorNodes[actorSlot];
  auto found =
      std::lower_bound(entries.begin(), entries.end(), continuation,
                       [](const std::pair<uint32_t, uint32_t> &entry,
                          uint32_t value) { return entry.first < value; });
  return found != entries.end() && found->first == continuation ? found->second
                                                                : UINT32_MAX;
}

bool markNativeAOTActorReadyUnlocked(obelisk_rt_context *context,
                                     uint32_t actorSlot) {
  if (context->nativeScheduleNodes.empty())
    return true;
  if (actorSlot >= context->nativeScheduleActors.size())
    return false;
  obelisk_rt_process_instance_v1 *actor =
      context->nativeScheduleActors[actorSlot];
  size_t processIndex = context->nativeScheduleActorIndices[actorSlot];
  if (!actor || processIndex >= context->scheduledProcesses.size())
    return false;
  const ScheduledProcess &scheduled = context->scheduledProcesses[processIndex];
  if (scheduled.instance != actor ||
      scheduled.phase != (context->schedulerRunningFinals ? 1u : 0u))
    return true;
  uint32_t node =
      findNativeAOTNodeUnlocked(context, actorSlot, actor->continuation);
  if (node == UINT32_MAX)
    return false;
  context->nativeScheduleReadyNodes[node / 64] |= uint64_t{1} << (node % 64);
  context->nativeScheduleMinimumActivatedNode =
      std::min(context->nativeScheduleMinimumActivatedNode, node);
  return true;
}

void clearNativeAOTNodeReadyUnlocked(obelisk_rt_context *context,
                                     uint32_t node) {
  if (node / 64 < context->nativeScheduleReadyNodes.size())
    context->nativeScheduleReadyNodes[node / 64] &=
        ~(uint64_t{1} << (node % 64));
}

static bool nativeAOTDeadlineLess(const obelisk_rt_context *context, uint32_t lhs,
                           uint32_t rhs) {
  return std::pair{context->nativeScheduleDeadlines[lhs], lhs} <
         std::pair{context->nativeScheduleDeadlines[rhs], rhs};
}

static void swapNativeAOTDeadlinesUnlocked(obelisk_rt_context *context, size_t lhs,
                                    size_t rhs) {
  std::swap(context->nativeScheduleDeadlineHeap[lhs],
            context->nativeScheduleDeadlineHeap[rhs]);
  context->nativeScheduleDeadlinePositions
      [context->nativeScheduleDeadlineHeap[lhs]] = lhs;
  context->nativeScheduleDeadlinePositions
      [context->nativeScheduleDeadlineHeap[rhs]] = rhs;
}

static void siftNativeAOTDeadlineUpUnlocked(obelisk_rt_context *context,
                                     size_t position) {
  while (position != 0) {
    size_t parent = (position - 1) / 2;
    if (!nativeAOTDeadlineLess(context,
                               context->nativeScheduleDeadlineHeap[position],
                               context->nativeScheduleDeadlineHeap[parent]))
      break;
    swapNativeAOTDeadlinesUnlocked(context, position, parent);
    position = parent;
  }
}

static void siftNativeAOTDeadlineDownUnlocked(obelisk_rt_context *context,
                                       size_t position) {
  size_t size = context->nativeScheduleDeadlineHeap.size();
  for (;;) {
    size_t child = position * 2 + 1;
    if (child >= size)
      return;
    if (child + 1 < size &&
        nativeAOTDeadlineLess(context,
                              context->nativeScheduleDeadlineHeap[child + 1],
                              context->nativeScheduleDeadlineHeap[child]))
      ++child;
    if (!nativeAOTDeadlineLess(context,
                               context->nativeScheduleDeadlineHeap[child],
                               context->nativeScheduleDeadlineHeap[position]))
      return;
    swapNativeAOTDeadlinesUnlocked(context, position, child);
    position = child;
  }
}

void removeNativeAOTDeadlineUnlocked(obelisk_rt_context *context,
                                     uint32_t actorSlot) {
  if (actorSlot >= context->nativeScheduleDeadlinePositions.size())
    return;
  uint32_t position = context->nativeScheduleDeadlinePositions[actorSlot];
  if (position == UINT32_MAX)
    return;
  size_t last = context->nativeScheduleDeadlineHeap.size() - 1;
  if (position != last)
    swapNativeAOTDeadlinesUnlocked(context, position, last);
  context->nativeScheduleDeadlineHeap.pop_back();
  context->nativeScheduleDeadlinePositions[actorSlot] = UINT32_MAX;
  context->nativeScheduleDeadlines[actorSlot] = UINT64_MAX;
  if (position < context->nativeScheduleDeadlineHeap.size()) {
    if (position != 0 &&
        nativeAOTDeadlineLess(
            context, context->nativeScheduleDeadlineHeap[position],
            context->nativeScheduleDeadlineHeap[(position - 1) / 2]))
      siftNativeAOTDeadlineUpUnlocked(context, position);
    else
      siftNativeAOTDeadlineDownUnlocked(context, position);
  }
}

void setNativeAOTDeadlineUnlocked(obelisk_rt_context *context,
                                  uint32_t actorSlot, uint64_t deadline) {
  if (actorSlot < context->nativeScheduleDeadlinePositions.size() &&
      context->nativeScheduleDeadlinePositions[actorSlot] == UINT32_MAX &&
      context->nativeScheduleDeadlineHeap.empty()) {
    context->nativeScheduleDeadlines[actorSlot] = deadline;
    context->nativeScheduleDeadlinePositions[actorSlot] = 0;
    context->nativeScheduleDeadlineHeap.push_back(actorSlot);
    context->signalDiagnostics.aotDeadlineHighWater =
        std::max<uint64_t>(context->signalDiagnostics.aotDeadlineHighWater, 1);
    return;
  }
  removeNativeAOTDeadlineUnlocked(context, actorSlot);
  context->nativeScheduleDeadlines[actorSlot] = deadline;
  context->nativeScheduleDeadlinePositions[actorSlot] =
      context->nativeScheduleDeadlineHeap.size();
  context->nativeScheduleDeadlineHeap.push_back(actorSlot);
  siftNativeAOTDeadlineUpUnlocked(
      context, context->nativeScheduleDeadlineHeap.size() - 1);
  context->signalDiagnostics.aotDeadlineHighWater =
      std::max<uint64_t>(context->signalDiagnostics.aotDeadlineHighWater,
                         context->nativeScheduleDeadlineHeap.size());
}

bool markDueNativeAOTDeadlinesUnlocked(obelisk_rt_context *context) {
  if (context->nativeScheduleDeadlineHeap.size() == 1) {
    uint32_t slot = context->nativeScheduleDeadlineHeap.front();
    if (context->nativeScheduleDeadlines[slot] > context->schedulerTime)
      return true;
    context->nativeScheduleDeadlineHeap.clear();
    context->nativeScheduleDeadlinePositions[slot] = UINT32_MAX;
    context->nativeScheduleDeadlines[slot] = UINT64_MAX;
    return markNativeAOTActorReadyUnlocked(context, slot);
  }
  while (!context->nativeScheduleDeadlineHeap.empty()) {
    uint32_t slot = context->nativeScheduleDeadlineHeap.front();
    if (context->nativeScheduleDeadlines[slot] > context->schedulerTime)
      break;
    removeNativeAOTDeadlineUnlocked(context, slot);
    if (!markNativeAOTActorReadyUnlocked(context, slot))
      return false;
  }
  return true;
}

obelisk_rt_status
refreshNativeAOTReadyPhaseUnlocked(obelisk_rt_context *context) {
  std::fill(context->nativeScheduleReadyNodes.begin(),
            context->nativeScheduleReadyNodes.end(), 0);
  for (uint32_t slot = 0; slot != context->nativeScheduleActors.size();
       ++slot) {
    obelisk_rt_process_instance_v1 *actor = context->nativeScheduleActors[slot];
    if (!actor)
      continue;
    size_t processIndex = context->nativeScheduleActorIndices[slot];
    if (processIndex >= context->scheduledProcesses.size())
      return OBELISK_RT_INVALID_LIFECYCLE;
    const ScheduledProcess &scheduled =
        context->scheduledProcesses[processIndex];
    if (scheduled.instance != actor)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (scheduled.phase != (context->schedulerRunningFinals ? 1u : 0u))
      continue;
    if (nativeProcessReady(*context, scheduled, true) &&
        !markNativeAOTActorReadyUnlocked(context, slot))
      return OBELISK_RT_INVALID_CONTINUATION;
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status
initializeNativeAOTNodesUnlocked(obelisk_rt_context *context,
                                 const obelisk_rt_native_schedule_node *nodes,
                                 uint32_t nodeCount) {
  if (!context->nativeScheduleNodes.empty()) {
    if (context->nativeScheduleNodes.size() != nodeCount ||
        !std::equal(context->nativeScheduleNodes.begin(),
                    context->nativeScheduleNodes.end(), nodes,
                    [](const obelisk_rt_native_schedule_node &lhs,
                       const obelisk_rt_native_schedule_node &rhs) {
                      return lhs.actor_slot == rhs.actor_slot &&
                             lhs.continuation == rhs.continuation &&
                             lhs.fusion_group == rhs.fusion_group;
                    }))
      return OBELISK_RT_INVALID_ARGUMENT;
    return OBELISK_RT_OK;
  }
  std::vector<obelisk_rt_native_schedule_node> installedNodes(
      nodes, nodes + nodeCount);
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> actorNodes(
      context->nativeScheduleActors.size());
  for (uint32_t nodeIndex = 0; nodeIndex != nodeCount; ++nodeIndex) {
    const obelisk_rt_native_schedule_node &node = nodes[nodeIndex];
    if (node.actor_slot >= context->nativeScheduleActors.size())
      return OBELISK_RT_INVALID_ARGUMENT;
    actorNodes[node.actor_slot].emplace_back(node.continuation, nodeIndex);
  }
  for (auto &entries : actorNodes) {
    std::sort(entries.begin(), entries.end());
    for (size_t index = 1; index != entries.size(); ++index)
      if (entries[index - 1].first == entries[index].first)
        return OBELISK_RT_INVALID_ARGUMENT;
  }
  std::vector<uint32_t> fanoutNodes(context->nativeScheduleFanoutEntryCount,
                                    UINT32_MAX);
  for (uint64_t index = 0; index != context->nativeScheduleFanoutEntryCount;
       ++index) {
    const obelisk_rt_static_fanout_entry &entry =
        context->nativeScheduleFanoutEntries[index];
    if (entry.actor_slot >= actorNodes.size() || entry.compute_node >= nodeCount)
      return OBELISK_RT_INVALID_ARGUMENT;
    const obelisk_rt_native_schedule_node &node = nodes[entry.compute_node];
    if (node.actor_slot != entry.actor_slot ||
        node.continuation != entry.continuation)
      return OBELISK_RT_INVALID_CONTINUATION;
    fanoutNodes[index] = entry.compute_node;
  }
  context->nativeScheduleNodes = std::move(installedNodes);
  context->nativeScheduleActorNodes = std::move(actorNodes);
  context->nativeScheduleFanoutNodes = std::move(fanoutNodes);
  context->nativeScheduleReadyNodes.assign((uint64_t{nodeCount} + 63) / 64, 0);
  for (uint32_t slot = 0; slot != context->nativeScheduleActors.size();
       ++slot) {
    obelisk_rt_process_instance_v1 *actor = context->nativeScheduleActors[slot];
    if (!actor)
      continue;
    size_t processIndex = context->nativeScheduleActorIndices[slot];
    if (processIndex >= context->scheduledProcesses.size())
      return OBELISK_RT_INVALID_LIFECYCLE;
    const ScheduledProcess &scheduled =
        context->scheduledProcesses[processIndex];
    if (scheduled.instance != actor)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (scheduled.started &&
        scheduled.suspendKind == OBELISK_RT_SUSPEND_DELAY) {
      setNativeAOTDeadlineUnlocked(context, slot, scheduled.wakeTime);
      continue;
    }
  }
  obelisk_rt_status status = refreshNativeAOTReadyPhaseUnlocked(context);
  if (status == OBELISK_RT_OK)
    return status;
  context->nativeScheduleNodes.clear();
  context->nativeScheduleActorNodes.clear();
  context->nativeScheduleFanoutNodes.clear();
  context->nativeScheduleReadyNodes.clear();
  context->nativeScheduleDeadlineHeap.clear();
  std::fill(context->nativeScheduleDeadlines.begin(),
            context->nativeScheduleDeadlines.end(), UINT64_MAX);
  std::fill(context->nativeScheduleDeadlinePositions.begin(),
            context->nativeScheduleDeadlinePositions.end(), UINT32_MAX);
  return status;
}

namespace {

obelisk_rt_status executeStaticNativeAOT(
    obelisk_rt_process_instance_v1 *instance, obelisk_rt_context *context,
    obelisk_rt_fragment_action_v1 &action, bool generatedActions) {
  action = {OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  if (!instance || !context || !instance->descriptor ||
      !instance->descriptor->native_execute ||
      (instance->descriptor->available_tiers & OBELISK_RT_TIER_MASK_NATIVE) ==
          0 ||
      instance->descriptor->execution != context->execution ||
      instance->lifecycle == OBELISK_RT_PROCESS_EXECUTING ||
      instance->lifecycle == OBELISK_RT_PROCESS_TERMINATED ||
      (!generatedActions &&
       !validContinuation(*instance->descriptor->frame_layout,
                          instance->continuation)) ||
      (instance->ownership_context && instance->ownership_context != context))
    return OBELISK_RT_INVALID_LIFECYCLE;
  instance->ownership_context = context;
  instance->tier = OBELISK_RT_TIER_NATIVE;
  instance->context = context;
  instance->action = &action;
  instance->status = OBELISK_RT_OK;
  instance->lifecycle = OBELISK_RT_PROCESS_EXECUTING;
  obelisk_rt_status status;
  try {
    status = instance->descriptor->native_execute(instance);
  } catch (const std::bad_alloc &) {
    status = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    status = OBELISK_RT_INVALID_ARGUMENT;
  }
  instance->context = nullptr;
  instance->action = nullptr;
  if (status == OBELISK_RT_OK && !generatedActions)
    status = validateAction(*instance, action, false);
  instance->status = status;
  if (status != OBELISK_RT_OK) {
    if (status == OBELISK_RT_AOT_CHECKPOINT) {
      // A generated branch checkpoint is a tier boundary, not a failed native
      // fragment. Preserve its frame and native resources so executeAOTNode
      // can run the same continuation in bytecode transactionally.
      instance->lifecycle = OBELISK_RT_PROCESS_SUSPENDED;
      return status;
    }
    if (instance->native_handle) {
      instance->descriptor->native_destroy(instance);
      if (instance->native_handle)
        instance->status = status = OBELISK_RT_INVALID_LIFECYCLE;
    }
    instance->lifecycle = OBELISK_RT_PROCESS_SUSPENDED;
    return status;
  }
  instance->continuation = action.continuation;
  instance->lifecycle = action.kind == OBELISK_RT_FRAGMENT_TERMINATE
                            ? OBELISK_RT_PROCESS_TERMINATED
                        : action.kind == OBELISK_RT_FRAGMENT_SUSPEND
                            ? OBELISK_RT_PROCESS_SUSPENDED
                            : OBELISK_RT_PROCESS_READY;
  if (instance->lifecycle == OBELISK_RT_PROCESS_TERMINATED) {
    unregisterManagedFrameRoots(instance);
    releaseOwnedNativeStates(context, instance);
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status executeTrustedAOTNode(obelisk_rt_context *context,
                                        uint32_t actorSlot) {
  if (!context->nativeSchedulePlan ||
      actorSlot >= context->nativeScheduleActors.size())
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (context->signalDiagnosticsEnabled && actorSlot < 64)
    ++context->signalDiagnostics.aotActorExecutions[actorSlot];
  obelisk_rt_process_instance_v1 *selected =
      context->nativeScheduleActors[actorSlot];
  uint64_t token = context->nativeScheduleActorTokens[actorSlot];
  size_t selectedIndex = context->nativeScheduleActorIndices[actorSlot];
  if (!selected || token == 0 ||
      selectedIndex >= context->scheduledProcesses.size() ||
      context->activeNativeProcess)
    return OBELISK_RT_INVALID_LIFECYCLE;
  {
    ScheduledProcess &scheduled = context->scheduledProcesses[selectedIndex];
    if (scheduled.instance != selected || scheduled.token != token ||
        scheduled.aotActorSlot != actorSlot)
      return OBELISK_RT_INVALID_LIFECYCLE;
    bool resuming =
        scheduled.started && scheduled.suspendKind != OBELISK_RT_SUSPEND_NONE;
    if (!scheduled.started)
      obelisk_rt_unregister_unstarted_actor(
          context, scheduled.phase, kNativeLogicalProcessTag | token);
    scheduled.started = true;
    scheduled.observedEpoch = context->schedulerEpoch;
    context->activeNativeProcess = selected;
    context->activeHomeRegion = scheduled.homeRegion;
    context->activeExecRegion = scheduled.queuedRegion;
    context->activeLogicalProcessToken = kNativeLogicalProcessTag | token;
    context->activeLogicalProcessParent = scheduled.parent;
    if (resuming)
      obelisk_rt_flush_deferred_immediate_reports_unlocked(
          context, context->activeLogicalProcessToken);
    // Static RTL actors almost never own disable/control memberships. Avoid
    // rotating three-word vector storage through the context on every fine
    // graph activation; if an execution creates a membership, the post-call
    // path below still transfers it back to the actor normally.
    if (!scheduled.controls.empty())
      context->activeControls = std::move(scheduled.controls);
  }

  obelisk_rt_fragment_action_v1 action{};
  obelisk_rt_status status =
      executeStaticNativeAOT(selected, context, action, true);

  bool actorValid =
      selectedIndex < context->scheduledProcesses.size() &&
      context->scheduledProcesses[selectedIndex].instance == selected;
  if (actorValid && !context->activeControls.empty())
    context->scheduledProcesses[selectedIndex].controls =
        std::move(context->activeControls);
  context->activeControls.clear();
  context->activeNativeProcess = nullptr;
  context->activeHomeRegion = UINT32_MAX;
  context->activeExecRegion = UINT32_MAX;
  context->activeLogicalProcessToken = 0;
  context->activeLogicalProcessParent = 0;
  if (!actorValid)
    return OBELISK_RT_INVALID_LIFECYCLE;
  ScheduledProcess &scheduled = context->scheduledProcesses[selectedIndex];
  bool terminationRequested = context->schedulerFinishRequested;
  if (terminationRequested) {
    context->schedulerRunningFinals = true;
    action = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  } else if (status != OBELISK_RT_OK) {
    return status;
  }

  bool destroy = false;
  bool requestFallback = false;
  switch (action.kind) {
  case OBELISK_RT_FRAGMENT_SUSPEND: {
    scheduled.suspendKind = action.suspend_kind;
    scheduled.waitOffset = action.payload;
    scheduled.waitSize = action.auxiliary;
    scheduled.observedEpoch = context->schedulerEpoch;
    scheduled.signalTriggered = false;
    scheduled.startupProcess = false;
    scheduled.urgent = false;
    if (action.suspend_kind != OBELISK_RT_SUSPEND_DELAY &&
        action.suspend_kind != OBELISK_RT_SUSPEND_CHANGE &&
        action.suspend_kind != OBELISK_RT_SUSPEND_EDGE) {
      status = adoptScheduledSuspendUnlocked(context, scheduled, action);
      if (status != OBELISK_RT_OK)
        return status;
      removeNativeAOTDeadlineUnlocked(context, actorSlot);
      requestFallback = true;
      break;
    }
    if (!scheduled.signalSubscriptions.empty() ||
        !scheduled.waitGenerations.empty())
      return OBELISK_RT_INVALID_LIFECYCLE;
    const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
        static_cast<const uint8_t *>(selected->frame) + action.payload);
    uint64_t delay =
        action.suspend_kind == OBELISK_RT_SUSPEND_DELAY ? wait->payload : 1;
    if (!obelisk_rt_next_queued_region(scheduled.homeRegion,
                                       action.suspend_kind, delay, action.flags,
                                       scheduled.queuedRegion))
      return OBELISK_RT_INVALID_ARGUMENT;
    if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY) {
      scheduled.wakeTime = delay > UINT64_MAX - context->schedulerTime
                               ? UINT64_MAX
                               : context->schedulerTime + delay;
      indexScheduledProcessDelayUnlocked(context, scheduled);
      if (delay == 0) {
        removeNativeAOTDeadlineUnlocked(context, actorSlot);
        requestFallback = true;
      } else {
        setNativeAOTDeadlineUnlocked(context, actorSlot, scheduled.wakeTime);
      }
    } else {
      // A signal-wait actor has no deadline. If this activation followed a
      // delay, markDueNativeAOTDeadlinesUnlocked removed its heap entry before
      // making the actor ready.
      scheduled.signalLatch.reset();
    }
    break;
  }
  case OBELISK_RT_FRAGMENT_CONTINUE:
    scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
    scheduled.waitOffset = 0;
    scheduled.waitSize = 0;
    scheduled.signalTriggered = false;
    scheduled.urgent = scheduled.startupProcess;
    scheduled.queuedRegion = scheduled.homeRegion;
    if (!markNativeAOTActorReadyUnlocked(context, actorSlot))
      return OBELISK_RT_INVALID_CONTINUATION;
    break;
  case OBELISK_RT_FRAGMENT_PROCESS_SUSPEND:
    scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
    scheduled.waitOffset = 0;
    scheduled.waitSize = 0;
    scheduled.waitGenerations.clear();
    scheduled.signalTriggered = false;
    scheduled.explicitlySuspended = true;
    scheduled.urgent = false;
    scheduled.queuedRegion = scheduled.homeRegion;
    removeNativeAOTDeadlineUnlocked(context, actorSlot);
    requestFallback = true;
    break;
  case OBELISK_RT_FRAGMENT_TERMINATE:
    if (!scheduled.callers.empty())
      return OBELISK_RT_INVALID_LIFECYCLE;
    status = context->nativeSchedulePlan->bind(
        context->nativeSchedulePlan->mutable_state, context, actorSlot,
        nullptr);
    if (status != OBELISK_RT_OK)
      return status;
    obelisk_rt_reparent_process_children_unlocked(
        context, kNativeLogicalProcessTag | token, scheduled.parent);
    context->terminatedNativeProcesses.insert(token);
    if (!scheduled.signalSubscriptions.empty())
      obelisk_rt_unregister_signal_wait_unlocked(
          context, scheduled.signalSubscriptions, token, false);
    scheduled.instance = nullptr;
    ++context->schedulerDeadProcessCount;
    context->schedulerCompactionPending = true;
    obelisk_rt_release_controls_unlocked(context, scheduled.controls);
    scheduled.controls.clear();
    scheduled.signalTriggered = false;
    scheduled.urgent = false;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    context->nativeScheduleActors[actorSlot] = nullptr;
    context->nativeScheduleActorTokens[actorSlot] = 0;
    context->nativeScheduleActorIndices[actorSlot] = SIZE_MAX;
    removeNativeAOTDeadlineUnlocked(context, actorSlot);
    destroy = true;
    break;
  default:
    return OBELISK_RT_INVALID_ARGUMENT;
  }

  if (terminationRequested) {
    status = refreshNativeAOTReadyPhaseUnlocked(context);
    if (status != OBELISK_RT_OK)
      return status;
  }
  if (context->schedulerSlotProgress == (UINT64_C(1) << 20)) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return context->schedulerStatus;
  }
  ++context->schedulerSlotProgress;
  ++context->signalDiagnostics.aotNodeExecutions;
  if (destroy) {
    status = obelisk_rt_v1_process_instance_destroy(selected);
    if (status != OBELISK_RT_OK)
      return status;
  }
  return requestFallback ? OBELISK_RT_TIER_UNAVAILABLE : OBELISK_RT_OK;
}

obelisk_rt_status executeAOTNode(obelisk_rt_context *context,
                                 uint32_t actorSlot) {
  obelisk_rt_process_instance_v1 *selected = nullptr;
  size_t selectedIndex = SIZE_MAX;
  obelisk_rt_execution_tier tier = OBELISK_RT_TIER_NATIVE;
  bool generatedActions = false;
  bool checkpointHandoff = false;
  {
    ContextMutexLock lock(context);
    if (!context->nativeSchedulePlan ||
        actorSlot >= context->nativeScheduleActors.size())
      return OBELISK_RT_INVALID_LIFECYCLE;
    selected = context->nativeScheduleActors[actorSlot];
    uint64_t token = context->nativeScheduleActorTokens[actorSlot];
    selectedIndex = context->nativeScheduleActorIndices[actorSlot];
    if (!selected || token == 0 ||
        selectedIndex >= context->scheduledProcesses.size())
      return OBELISK_RT_INVALID_LIFECYCLE;
    ScheduledProcess &scheduled = context->scheduledProcesses[selectedIndex];
    if (scheduled.instance != selected || scheduled.token != token ||
        scheduled.aotActorSlot != actorSlot)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (context->activeNativeProcess)
      return OBELISK_RT_INVALID_LIFECYCLE;
    checkpointHandoff =
        context->nativeScheduleCheckpointActorSlot == actorSlot;
    if (checkpointHandoff)
      context->nativeScheduleCheckpointActorSlot = UINT32_MAX;

    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    generatedActions =
        (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS) != 0;
    bool guardedSpecialization = plan->specialization_fast != nullptr;
    bool specializationFast =
        !guardedSpecialization || *plan->specialization_fast != 0;
    // A generated native fragment with a clean specialization guard and no
    // bytecode-only continuation is the common static-schedule case. Avoid
    // searching continuation metadata or dirty-root dependencies for it; the
    // run-level guard has already established the same exclusion held by this
    // mutex for the complete AOT invocation.
    bool cleanGeneratedNative = generatedActions && specializationFast &&
                                scheduled.bytecodeContinuations.empty();
    if (!cleanGeneratedNative) {
      bool bytecodeFragment = std::binary_search(
          scheduled.bytecodeContinuations.begin(),
          scheduled.bytecodeContinuations.end(), selected->continuation);
      bool nativeRootBootstrap =
          actorSlot == 0 && selected->continuation == 0 &&
          (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO) != 0;
      // The native fused body is the clean version. Root-local VPI dirtiness
      // hands only intersecting actors to bytecode; a global reason such as an
      // observer, conditional waiter, or generic NBA hands over every actor
      // for the remainder of the affected slot.
      bool specializationHandover =
          guardedSpecialization && !specializationFast &&
          nativeAOTNeedsSpecializationHandoverUnlocked(context, actorSlot);
      bool needsBytecode =
          !nativeRootBootstrap &&
          (bytecodeFragment || specializationHandover);
      if (needsBytecode) {
        if ((selected->descriptor->available_tiers &
             OBELISK_RT_TIER_MASK_BYTECODE) == 0)
          return OBELISK_RT_TIER_UNAVAILABLE;
        tier = OBELISK_RT_TIER_BYTECODE;
        if (specializationHandover && context->signalDiagnosticsEnabled)
          ++context->signalDiagnostics.aotStateSlowPaths;
      }
    }

    bool resuming =
        scheduled.started && scheduled.suspendKind != OBELISK_RT_SUSPEND_NONE;
    if (!scheduled.started)
      obelisk_rt_unregister_unstarted_actor(
          context, scheduled.phase,
          kNativeLogicalProcessTag | scheduled.token);
    if (scheduled.signalLatch) {
      scheduled.signalLatch->triggered = false;
      scheduled.signalLatch->affected = false;
    }
    scheduled.signalTriggered = false;
    scheduled.started = true;
    scheduled.observedEpoch = context->schedulerEpoch;
    context->activeNativeProcess = selected;
    context->activeHomeRegion = scheduled.homeRegion;
    context->activeExecRegion = scheduled.queuedRegion;
    context->activeLogicalProcessToken =
        kNativeLogicalProcessTag | scheduled.token;
    context->activeLogicalProcessParent = scheduled.parent;
    if (resuming)
      obelisk_rt_flush_deferred_immediate_reports_unlocked(
          context, context->activeLogicalProcessToken);
    context->activeControls = std::move(scheduled.controls);
  }

  obelisk_rt_fragment_action_v1 action{};
  obelisk_rt_status status = OBELISK_RT_OK;
  generatedActions &= tier == OBELISK_RT_TIER_NATIVE;
  if (tier == OBELISK_RT_TIER_BYTECODE) {
    ContextMutexLock lock(context);
    // Tier 3 may introduce X/Z through operations that do not cross the
    // external-write hook. Invalidate before executing it so the next
    // quiescent generated dispatch rescans canonical unknown planes instead
    // of reusing a stale two-state selection.
    invalidateNativeTwoStatePromotionUnlocked(context);
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    bool synchronized = plan && plan->state_bit_count == 0;
    if (plan && plan->state_bit_count != 0) {
      synchronized =
          (!context->nativeScheduleExternalWritePending ||
           reconcileNativeDirtyRootsToPlanesUnlocked(context, plan)) &&
          importNativeStatePlanesUnlocked(context, plan->state_value,
                                          plan->state_unknown,
                                          plan->state_bit_count);
    }
    if (!synchronized)
      status = OBELISK_RT_LAYOUT_MISMATCH;
  }
  if (status == OBELISK_RT_OK) {
    status = tier == OBELISK_RT_TIER_NATIVE
                 ? executeStaticNativeAOT(selected, context, action,
                                          generatedActions)
                 : obelisk_rt_v1_process_instance_execute(selected, context,
                                                          tier, &action);
  }
  // A generated branch checkpoint deliberately leaves the callback or other
  // unsupported operation outside the Tier-1/Tier-2 call graph. Resume that
  // same continuation in bytecode, then map its returned action back onto the
  // existing actor ready bit instead of deoptimizing the complete schedule.
  if (status == OBELISK_RT_AOT_CHECKPOINT && tier == OBELISK_RT_TIER_NATIVE &&
      (selected->descriptor->available_tiers &
       OBELISK_RT_TIER_MASK_BYTECODE) != 0) {
    {
      ContextMutexLock lock(context);
      const obelisk_rt_native_schedule_plan *plan =
          context->nativeSchedulePlan;
      if (!plan ||
          (plan->state_bit_count != 0 &&
           !importNativeStatePlanesUnlocked(context, plan->state_value,
                                            plan->state_unknown,
                                            plan->state_bit_count)))
        status = OBELISK_RT_LAYOUT_MISMATCH;
      else
        status = OBELISK_RT_OK;
    }
    if (status == OBELISK_RT_OK) {
      {
        ContextMutexLock lock(context);
        invalidateNativeTwoStatePromotionUnlocked(context);
      }
      tier = OBELISK_RT_TIER_BYTECODE;
      generatedActions = false;
      status = obelisk_rt_v1_process_instance_execute(
          selected, context, tier, &action);
    }
  }
  if (tier == OBELISK_RT_TIER_BYTECODE) {
    ContextMutexLock lock(context);
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    if ((!plan || (plan->state_bit_count != 0 &&
                   !exportNativeStatePlanesUnlocked(context, plan->state_value,
                                                    plan->state_unknown,
                                                    plan->state_bit_count))) &&
        status == OBELISK_RT_OK)
      status = OBELISK_RT_LAYOUT_MISMATCH;
  }
  bool terminationRequested = false;
  bool killRequested = false;
  bool handledGeneratedSuspend = false;
  {
    ContextMutexLock lock(context);
    if (selectedIndex < context->scheduledProcesses.size() &&
        context->scheduledProcesses[selectedIndex].instance == selected) {
      context->scheduledProcesses[selectedIndex].controls =
          std::move(context->activeControls);
      killRequested = context->killedNativeProcesses.count(
                          context->scheduledProcesses[selectedIndex].token) !=
                      0;
    }
    context->activeControls.clear();
    context->activeNativeProcess = nullptr;
    context->activeHomeRegion = UINT32_MAX;
    context->activeExecRegion = UINT32_MAX;
    context->activeLogicalProcessToken = 0;
    context->activeLogicalProcessParent = 0;
    terminationRequested = context->schedulerFinishRequested;
    if (terminationRequested)
      context->schedulerRunningFinals = true;
    if (!terminationRequested && status == OBELISK_RT_OK && generatedActions &&
        action.kind == OBELISK_RT_FRAGMENT_SUSPEND &&
        (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY ||
         action.suspend_kind == OBELISK_RT_SUSPEND_CHANGE ||
         action.suspend_kind == OBELISK_RT_SUSPEND_EDGE)) {
      if (selectedIndex >= context->scheduledProcesses.size() ||
          context->scheduledProcesses[selectedIndex].instance != selected)
        return OBELISK_RT_INVALID_LIFECYCLE;
      ScheduledProcess &scheduled = context->scheduledProcesses[selectedIndex];
      const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
          static_cast<const uint8_t *>(selected->frame) + action.payload);
      uint64_t delay =
          action.suspend_kind == OBELISK_RT_SUSPEND_DELAY ? wait->payload : 1;
      if (delay != 0 && canUseStaticAOTFanout(context) &&
          !context->nativeScheduleExternalWritePending &&
          scheduled.signalSubscriptions.empty() &&
          scheduled.waitGenerations.empty()) {
        scheduled.suspendKind = action.suspend_kind;
        scheduled.waitOffset = action.payload;
        scheduled.waitSize = action.auxiliary;
        updateNativeAOTContinuationRank(scheduled, action.continuation);
        scheduled.observedEpoch = context->schedulerEpoch;
        scheduled.signalTriggered = false;
        scheduled.startupProcess = false;
        scheduled.urgent = false;
        if (!obelisk_rt_next_queued_region(
                scheduled.homeRegion, action.suspend_kind, delay, action.flags,
                scheduled.queuedRegion))
          return OBELISK_RT_INVALID_ARGUMENT;
        if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY) {
          scheduled.wakeTime = delay > UINT64_MAX - context->schedulerTime
                                   ? UINT64_MAX
                                   : context->schedulerTime + delay;
          indexScheduledProcessDelayUnlocked(context, scheduled);
          setNativeAOTDeadlineUnlocked(context, actorSlot, scheduled.wakeTime);
        } else {
          removeNativeAOTDeadlineUnlocked(context, actorSlot);
          scheduled.signalLatch.reset();
        }
        if (context->schedulerSlotProgress == (UINT64_C(1) << 20)) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
          return context->schedulerStatus;
        }
        ++context->schedulerSlotProgress;
        ++context->signalDiagnostics.aotNodeExecutions;
        handledGeneratedSuspend = true;
      }
    }
  }
  if (!terminationRequested && status != OBELISK_RT_OK)
    return status;
  if (terminationRequested)
    action = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  if (handledGeneratedSuspend)
    return OBELISK_RT_OK;

  auto destroyPendingCallee =
      [](obelisk_rt_process_instance_v1 *instance) noexcept {
        if (instance)
          (void)obelisk_rt_v1_process_instance_destroy(instance);
      };
  std::unique_ptr<obelisk_rt_process_instance_v1,
                  decltype(destroyPendingCallee)>
      pendingCallee(nullptr, destroyPendingCallee);
  if (action.kind == OBELISK_RT_FRAGMENT_TASK_CALL) {
    auto *callee = reinterpret_cast<obelisk_rt_process_instance_v1 *>(
        static_cast<uintptr_t>(action.payload));
    if (!callee || callee == selected)
      return OBELISK_RT_INVALID_LIFECYCLE;
    pendingCallee.reset(callee);
    if (callee->lifecycle != OBELISK_RT_PROCESS_READY || !callee->descriptor ||
        callee->descriptor->execution != selected->descriptor->execution ||
        (callee->ownership_context && callee->ownership_context != context))
      return OBELISK_RT_INVALID_LIFECYCLE;
  }

  bool destroy = false;
  bool requestFallback = false;
  std::vector<obelisk_rt_process_instance_v1 *> terminatedCallers;
  {
    ContextMutexLock lock(context);
    if (selectedIndex >= context->scheduledProcesses.size() ||
        context->scheduledProcesses[selectedIndex].instance != selected)
      return OBELISK_RT_INVALID_LIFECYCLE;
    ScheduledProcess &scheduled = context->scheduledProcesses[selectedIndex];
    switch (action.kind) {
    case OBELISK_RT_FRAGMENT_TERMINATE:
      if (!scheduled.callers.empty() && !terminationRequested &&
          !killRequested) {
        obelisk_rt_process_instance_v1 *caller = scheduled.callers.back();
        status = context->nativeSchedulePlan->bind(
            context->nativeSchedulePlan->mutable_state, context, actorSlot,
            caller);
        if (status != OBELISK_RT_OK)
          return status;
        scheduled.callers.pop_back();
        scheduled.instance = caller;
        scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
        scheduled.waitOffset = 0;
        scheduled.waitSize = 0;
        scheduled.waitGenerations.clear();
        scheduled.signalTriggered = false;
        scheduled.urgent = true;
        scheduled.queuedRegion = scheduled.homeRegion;
        context->nativeScheduleActors[actorSlot] = caller;
        context->nativeScheduleActorTokens[actorSlot] = scheduled.token;
        context->nativeScheduleActorIndices[actorSlot] = selectedIndex;
        destroy = true;
        requestFallback = true;
        break;
      }
      status = context->nativeSchedulePlan->bind(
          context->nativeSchedulePlan->mutable_state, context, actorSlot,
          nullptr);
      if (status != OBELISK_RT_OK)
        return status;
      obelisk_rt_reparent_process_children_unlocked(
          context, kNativeLogicalProcessTag | scheduled.token,
          scheduled.parent);
      context->terminatedNativeProcesses.insert(scheduled.token);
      obelisk_rt_unregister_signal_wait_unlocked(
          context, scheduled.signalSubscriptions, scheduled.token, false);
      scheduled.instance = nullptr;
      ++context->schedulerDeadProcessCount;
      context->schedulerCompactionPending = true;
      if (terminationRequested || killRequested)
        terminatedCallers.swap(scheduled.callers);
      obelisk_rt_release_controls_unlocked(context, scheduled.controls);
      scheduled.controls.clear();
      scheduled.signalTriggered = false;
      scheduled.urgent = false;
      if (++context->schedulerEpoch == 0)
        context->schedulerEpoch = 1;
      destroy = true;
      break;
    case OBELISK_RT_FRAGMENT_SUSPEND: {
      if (action.suspend_kind != OBELISK_RT_SUSPEND_DELAY &&
          action.suspend_kind != OBELISK_RT_SUSPEND_CHANGE &&
          action.suspend_kind != OBELISK_RT_SUSPEND_EDGE) {
        status = adoptScheduledSuspendUnlocked(context, scheduled, action);
        if (status != OBELISK_RT_OK)
          return status;
        requestFallback = true;
        break;
      }
      scheduled.suspendKind = action.suspend_kind;
      scheduled.waitOffset = action.payload;
      scheduled.waitSize = action.auxiliary;
      updateNativeAOTContinuationRank(scheduled, action.continuation);
      scheduled.observedEpoch = context->schedulerEpoch;
      scheduled.waitGenerations.clear();
      scheduled.signalTriggered = false;
      scheduled.startupProcess = false;
      scheduled.urgent = false;
      const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
          static_cast<const uint8_t *>(selected->frame) + action.payload);
      uint64_t delayPayload =
          action.suspend_kind == OBELISK_RT_SUSPEND_DELAY ? wait->payload : 1;
      if (!obelisk_rt_next_queued_region(scheduled.homeRegion,
                                         action.suspend_kind, delayPayload,
                                         action.flags, scheduled.queuedRegion))
        return OBELISK_RT_INVALID_ARGUMENT;
      if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY) {
        if (!scheduled.signalSubscriptions.empty())
          obelisk_rt_unregister_signal_wait_unlocked(
              context, scheduled.signalSubscriptions, scheduled.token, false);
        scheduled.wakeTime = wait->payload > UINT64_MAX - context->schedulerTime
                                 ? UINT64_MAX
                                 : context->schedulerTime + wait->payload;
        indexScheduledProcessDelayUnlocked(context, scheduled);
        // A zero delay resumes in Inactive or Re-Inactive, before the
        // corresponding NBA barrier. The first static-control kernel does not
        // yet model those region queues explicitly, so transfer the complete
        // scheduler state transactionally rather than putting #0 in the
        // ordinary deadline heap and committing NBA too early.
        requestFallback = wait->payload == 0;
      } else {
        if (checkpointHandoff || canUseStaticAOTFanout(context)) {
          if (!scheduled.signalSubscriptions.empty())
            obelisk_rt_unregister_signal_wait_unlocked(
                context, scheduled.signalSubscriptions, scheduled.token, false);
          scheduled.signalLatch.reset();
          break;
        }
        wait = currentWait(scheduled);
        if (!wait)
          return OBELISK_RT_INVALID_FRAME;
        if (!hasSameDirectSignalWait(scheduled, wait) &&
            !obelisk_rt_register_signal_wait_unlocked(
                context, wait, scheduled.signalSubscriptions,
                scheduled.signalLatch, scheduled.token, false)) {
          return context->schedulerStatus;
        }
      }
      break;
    }
    case OBELISK_RT_FRAGMENT_CONTINUE:
      if (!scheduled.signalSubscriptions.empty())
        obelisk_rt_unregister_signal_wait_unlocked(
            context, scheduled.signalSubscriptions, scheduled.token, false);
      scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
      scheduled.waitOffset = 0;
      scheduled.waitSize = 0;
      scheduled.waitGenerations.clear();
      scheduled.signalTriggered = false;
      scheduled.urgent = scheduled.startupProcess;
      scheduled.queuedRegion = scheduled.homeRegion;
      break;
    case OBELISK_RT_FRAGMENT_PROCESS_SUSPEND:
      if (!scheduled.signalSubscriptions.empty())
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
      requestFallback = true;
      break;
    case OBELISK_RT_FRAGMENT_TASK_CALL: {
      obelisk_rt_process_instance_v1 *callee = pendingCallee.get();
      if (!callee)
        return OBELISK_RT_INVALID_LIFECYCLE;
      if (scheduled.callers.size() == std::numeric_limits<size_t>::max())
        return OBELISK_RT_OUT_OF_RESOURCES;
      scheduled.callers.reserve(scheduled.callers.size() + 1);
      if (!scheduled.signalSubscriptions.empty())
        obelisk_rt_unregister_signal_wait_unlocked(
            context, scheduled.signalSubscriptions, scheduled.token, false);
      status = context->nativeSchedulePlan->bind(
          context->nativeSchedulePlan->mutable_state, context, actorSlot,
          callee);
      if (status != OBELISK_RT_OK)
        return status;
      scheduled.callers.push_back(selected);
      scheduled.instance = callee;
      scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
      scheduled.waitOffset = 0;
      scheduled.waitSize = 0;
      scheduled.waitGenerations.clear();
      scheduled.signalTriggered = false;
      scheduled.urgent = true;
      scheduled.queuedRegion = scheduled.homeRegion;
      context->nativeScheduleActors[actorSlot] = callee;
      context->nativeScheduleActorTokens[actorSlot] = scheduled.token;
      context->nativeScheduleActorIndices[actorSlot] = selectedIndex;
      pendingCallee.release();
      requestFallback = true;
      break;
    }
    default:
      return OBELISK_RT_INVALID_ARGUMENT;
    }

    if (!destroy && !requestFallback &&
        action.kind == OBELISK_RT_FRAGMENT_SUSPEND &&
        action.suspend_kind == OBELISK_RT_SUSPEND_DELAY) {
      setNativeAOTDeadlineUnlocked(context, actorSlot, scheduled.wakeTime);
    } else {
      removeNativeAOTDeadlineUnlocked(context, actorSlot);
      if (!destroy && !requestFallback &&
          action.kind == OBELISK_RT_FRAGMENT_CONTINUE &&
          !markNativeAOTActorReadyUnlocked(context, actorSlot))
        return OBELISK_RT_INVALID_CONTINUATION;
    }
    if (destroy) {
      context->nativeScheduleActors[actorSlot] = nullptr;
      context->nativeScheduleActorTokens[actorSlot] = 0;
      context->nativeScheduleActorIndices[actorSlot] = SIZE_MAX;
    }
    if (terminationRequested) {
      status = refreshNativeAOTReadyPhaseUnlocked(context);
      if (status != OBELISK_RT_OK)
        return status;
    }
    if (context->schedulerSlotProgress == (UINT64_C(1) << 20)) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return context->schedulerStatus;
    }
    ++context->schedulerSlotProgress;
    ++context->signalDiagnostics.aotNodeExecutions;
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
  return requestFallback ? OBELISK_RT_TIER_UNAVAILABLE : OBELISK_RT_OK;
}

class NativeScheduleStepScope {
public:
  NativeScheduleStepScope(obelisk_rt_context *context, uint32_t actorSlot,
                          bool controlOnly, bool processFilterActive = false,
                          uint64_t processToken = 0)
      : context(context) {
    ContextMutexLock lock(context);
    context->nativeScheduleForcedSlot = actorSlot;
    context->nativeScheduleSingleStep = true;
    context->nativeScheduleForcedExecuted = false;
    context->nativeScheduleControlOnly = controlOnly;
    context->nativeScheduleProcessFilterActive = processFilterActive;
    context->nativeScheduleForcedProcessToken = processToken;
  }

  NativeScheduleStepScope(const NativeScheduleStepScope &) = delete;
  NativeScheduleStepScope &operator=(const NativeScheduleStepScope &) = delete;

  ~NativeScheduleStepScope() {
    ContextMutexLock lock(context);
    context->nativeScheduleForcedSlot = UINT32_MAX;
    context->nativeScheduleSingleStep = false;
    context->nativeScheduleForcedExecuted = false;
    context->nativeScheduleControlOnly = false;
    context->nativeScheduleProcessFilterActive = false;
    context->nativeScheduleForcedProcessToken = 0;
  }

  bool executed() const {
    ContextMutexLock lock(context);
    return context->nativeScheduleForcedExecuted;
  }

private:
  obelisk_rt_context *context;
};

class NativeScheduleDesignTaskScope {
public:
  NativeScheduleDesignTaskScope(obelisk_rt_context *context, uint64_t task)
      : context(context) {
    ContextMutexLock lock(context);
    context->nativeScheduleDesignTaskFilterActive = true;
    context->nativeScheduleForcedDesignTask = task;
  }

  NativeScheduleDesignTaskScope(const NativeScheduleDesignTaskScope &) =
      delete;
  NativeScheduleDesignTaskScope &
  operator=(const NativeScheduleDesignTaskScope &) = delete;

  ~NativeScheduleDesignTaskScope() {
    ContextMutexLock lock(context);
    context->nativeScheduleDesignTaskFilterActive = false;
    context->nativeScheduleForcedDesignTask = 0;
  }

private:
  obelisk_rt_context *context;
};

// Lets the fine scheduler own as many event slots as an asynchronous
// intervention requires, but returns as soon as force/assign and dirty-root
// state are reconciled at a quiescent boundary. This is a transient Tier-2/3
// handoff, not a permanent AOT deoptimization.
class NativeScheduleCleanBoundaryScope {
public:
  explicit NativeScheduleCleanBoundaryScope(obelisk_rt_context *context)
      : context(context) {
    ContextMutexLock lock(context);
    context->nativeScheduleStopAtCleanBoundary = true;
    context->nativeScheduleCleanBoundaryReached = false;
  }
  NativeScheduleCleanBoundaryScope(const NativeScheduleCleanBoundaryScope &) =
      delete;
  NativeScheduleCleanBoundaryScope &
  operator=(const NativeScheduleCleanBoundaryScope &) = delete;
  ~NativeScheduleCleanBoundaryScope() {
    ContextMutexLock lock(context);
    context->nativeScheduleStopAtCleanBoundary = false;
  }
  bool reached() const {
    ContextMutexLock lock(context);
    return context->nativeScheduleCleanBoundaryReached;
  }

private:
  obelisk_rt_context *context;
};

// The clean generated schedule owns the context mutex for its complete run.
// Keep its ready-worklist loop separate from the hybrid implementation so the
// hot path does not repeatedly construct recursive-lock guards or retain the
// generic actor/control branches. Fine node bits remain canonical fracture
// points for an indexed external write or later bytecode handoff.
template <bool RunClockCoordinator>
obelisk_rt_status runTrustedAOTNodesUnlocked(obelisk_rt_context *context) {
  if (!context || activeNativeAOTContext != context ||
      lockedNativeAOTContext != context || !context->nativeSchedulePlan)
    return OBELISK_RT_INVALID_LIFECYCLE;

  uint32_t nodeCursor = 0;
  bool passProgress = false;
  for (;;) {
    if constexpr (RunClockCoordinator) {
      if (context->nativeScheduleClockIngressPending) {
        obelisk_rt_native_timeslot_coordinator coordinator =
            context->nativeSchedulePlan->timeslot_coordinator;
        if (!coordinator)
          return OBELISK_RT_INVALID_LIFECYCLE;
        context->nativeScheduleClockIngressPending = false;
        obelisk_rt_status status = coordinator(
            context->nativeSchedulePlan->mutable_state, context);
        if (status != OBELISK_RT_OK)
          return status;
        // A direct body may publish a lower graph-order ingress while this
        // pass is in flight. Restart selection so the next generated drain
        // observes it before any later fine fallback node.
        nodeCursor = 0;
      }
    }
    uint32_t selectedNode = UINT32_MAX;
    bool readyBeforeCursor = false;
    bool schedulerOrderedBootstrap = obelisk_rt_unstarted_actor_pending(
        context, context->schedulerRunningFinals ? 1u : 0u);
    if (schedulerOrderedBootstrap) {
      using SchedulerKey = std::tuple<uint32_t, uint32_t, uint64_t>;
      SchedulerKey selectedKey{UINT32_MAX, UINT32_MAX, UINT64_MAX};
      for (uint32_t wordIndex = 0;
           wordIndex < context->nativeScheduleReadyNodes.size(); ++wordIndex) {
        uint64_t word = context->nativeScheduleReadyNodes[wordIndex];
        while (word != 0) {
          uint32_t bit = static_cast<uint32_t>(__builtin_ctzll(word));
          uint32_t candidate = wordIndex * 64 + bit;
          word &= word - 1;
          if (candidate >= context->nativeScheduleNodes.size())
            return OBELISK_RT_INVALID_CONTINUATION;
          const auto &node = context->nativeScheduleNodes[candidate];
          if (node.actor_slot >= context->nativeScheduleActorIndices.size())
            return OBELISK_RT_INVALID_CONTINUATION;
          size_t processIndex =
              context->nativeScheduleActorIndices[node.actor_slot];
          if (processIndex >= context->scheduledProcesses.size())
            return OBELISK_RT_INVALID_LIFECYCLE;
          const ScheduledProcess &scheduled =
              context->scheduledProcesses[processIndex];
          SchedulerKey key{scheduled.urgent ? 0 : scheduled.queuedRegion,
                           scheduled.urgent ? 0 : scheduled.scheduleRank,
                           scheduled.insertionSequence};
          if (key < selectedKey ||
              (key == selectedKey && candidate < selectedNode)) {
            selectedNode = candidate;
            selectedKey = key;
          }
        }
      }
    } else {
      uint32_t cursorWord = nodeCursor / 64;
      uint32_t cursorBit = nodeCursor % 64;
      for (uint32_t wordIndex = cursorWord;
           wordIndex < context->nativeScheduleReadyNodes.size(); ++wordIndex) {
        uint64_t word = context->nativeScheduleReadyNodes[wordIndex];
        if (wordIndex == cursorWord && cursorBit != 0)
          word &= UINT64_MAX << cursorBit;
        if (word == 0)
          continue;
        selectedNode =
            wordIndex * 64 + static_cast<uint32_t>(__builtin_ctzll(word));
        break;
      }
      if (selectedNode == UINT32_MAX)
        for (uint32_t wordIndex = 0;
             wordIndex <= cursorWord &&
             wordIndex < context->nativeScheduleReadyNodes.size();
             ++wordIndex) {
          uint64_t word = context->nativeScheduleReadyNodes[wordIndex];
          if (wordIndex == cursorWord && cursorBit != 0)
            word &= (uint64_t{1} << cursorBit) - 1;
          if (word != 0) {
            readyBeforeCursor = true;
            break;
          }
        }
    }

    if (selectedNode != UINT32_MAX) {
      const obelisk_rt_native_schedule_node &node =
          context->nativeScheduleNodes[selectedNode];
      if (node.actor_slot >= context->nativeScheduleActors.size() ||
          !context->nativeScheduleActors[node.actor_slot] ||
          context->nativeScheduleActors[node.actor_slot]->continuation !=
              node.continuation)
        return OBELISK_RT_INVALID_CONTINUATION;
      clearNativeAOTNodeReadyUnlocked(context, selectedNode);

      uint32_t lastNode = selectedNode;
      bool restartBeforeCursor = false;
      for (;;) {
        const obelisk_rt_native_schedule_node &selected =
            context->nativeScheduleNodes[lastNode];
        context->nativeScheduleMinimumActivatedNode = UINT32_MAX;
        obelisk_rt_status status =
            executeTrustedAOTNode(context, selected.actor_slot);
        if (status != OBELISK_RT_OK)
          return status;
        passProgress = true;

        uint32_t nextNode = lastNode + 1;
        restartBeforeCursor =
            context->nativeScheduleMinimumActivatedNode < nextNode;
        bool fuseNext = false;
        // Startup ordering is defined by event regions, not graph adjacency.
        // Re-enter selection after each bootstrap actor so a fused successor
        // cannot pull Reactive work ahead of an unstarted Active actor or the
        // deferred time-zero always_comb activation.
        if (!schedulerOrderedBootstrap && !restartBeforeCursor &&
            selected.fusion_group != UINT32_MAX &&
            nextNode < context->nativeScheduleNodes.size()) {
          const obelisk_rt_native_schedule_node &next =
              context->nativeScheduleNodes[nextNode];
          uint64_t mask = uint64_t{1} << (nextNode % 64);
          fuseNext =
              next.fusion_group == selected.fusion_group &&
              (context->nativeScheduleReadyNodes[nextNode / 64] & mask) != 0 &&
              next.actor_slot < context->nativeScheduleActors.size() &&
              context->nativeScheduleActors[next.actor_slot] &&
              context->nativeScheduleActors[next.actor_slot]->continuation ==
                  next.continuation;
          if (fuseNext)
            clearNativeAOTNodeReadyUnlocked(context, nextNode);
        }
        if (!fuseNext)
          break;
        lastNode = nextNode;
      }
      nodeCursor = schedulerOrderedBootstrap || restartBeforeCursor
                       ? 0
                       : lastNode + 1;
      continue;
    }

    if (passProgress)
      ++context->signalDiagnostics.aotRegionPasses;
    if (readyBeforeCursor) {
      nodeCursor = 0;
      passProgress = false;
      continue;
    }

    uint64_t previousProgress = context->schedulerSlotProgress;
    uint64_t previousTime = context->schedulerTime;
    bool previousFinals = context->schedulerRunningFinals;
    obelisk_rt_status status = runStaticAOTControlStep(context);
    if (status == OBELISK_RT_TIER_UNAVAILABLE) {
      NativeScheduleStepScope step(context, UINT32_MAX, true);
      status = runScheduler(context);
      if (status == OBELISK_RT_OK &&
          !markDueNativeAOTDeadlinesUnlocked(context))
        return OBELISK_RT_INVALID_CONTINUATION;
    }
    if (context->schedulerRunningFinals != previousFinals) {
      obelisk_rt_status refresh = refreshNativeAOTReadyPhaseUnlocked(context);
      if (refresh != OBELISK_RT_OK)
        return refresh;
    }
    bool controlProgress = context->schedulerSlotProgress != previousProgress ||
                           context->schedulerTime != previousTime ||
                           context->schedulerRunningFinals != previousFinals;
    if (status != OBELISK_RT_OK || !controlProgress)
      return status;
    nodeCursor = 0;
    passProgress = false;
  }
}

// Cold bootstrap for generated run_until. It executes the same trusted actor
// nodes and NBA barriers as the ordinary AOT loop, but deliberately stops at
// time-zero quiescence instead of advancing the calendar heap.
obelisk_rt_status drainNativeAOTCurrentSlotUnlocked(
    obelisk_rt_context *context, bool allowBytecode = false) {
  if (!context || activeNativeAOTContext != context ||
      lockedNativeAOTContext != context || !context->nativeSchedulePlan)
    return OBELISK_RT_INVALID_LIFECYCLE;
  for (;;) {
    if (context->nativeScheduleClockIngressPending) {
      auto coordinator = context->nativeSchedulePlan->timeslot_coordinator;
      if (!coordinator)
        return OBELISK_RT_INVALID_LIFECYCLE;
      context->nativeScheduleClockIngressPending = false;
      obelisk_rt_status status = coordinator(
          context->nativeSchedulePlan->mutable_state, context);
      if (status != OBELISK_RT_OK)
        return status;
      continue;
    }

    using SchedulerKey = std::tuple<uint32_t, uint32_t, uint64_t>;
    uint32_t selectedNode = UINT32_MAX;
    uint32_t nodeRegion = UINT32_MAX;
    uint32_t nodeRank = UINT32_MAX;
    uint64_t nodeInsertionSequence = UINT64_MAX;
    SchedulerKey selectedNodeKey{UINT32_MAX, UINT32_MAX, UINT64_MAX};
    // Ready-node IDs are layout artifacts, not scheduler priority. Bootstrap
    // can have multiple entry actors ready across Active and Reactive regions,
    // including urgent startup actors, so select by the same key as the
    // generic scheduler before executing a generated node.
    for (uint32_t word = 0; word != context->nativeScheduleReadyNodes.size();
         ++word) {
      uint64_t ready = context->nativeScheduleReadyNodes[word];
      while (ready != 0) {
        uint32_t bit = static_cast<uint32_t>(__builtin_ctzll(ready));
        uint32_t candidateNode = word * 64 + bit;
        ready &= ready - 1;
        if (candidateNode >= context->nativeScheduleNodes.size())
          return OBELISK_RT_INVALID_CONTINUATION;
        const obelisk_rt_native_schedule_node &node =
            context->nativeScheduleNodes[candidateNode];
        if (node.actor_slot >= context->nativeScheduleActors.size() ||
            !context->nativeScheduleActors[node.actor_slot] ||
            context->nativeScheduleActors[node.actor_slot]->continuation !=
                node.continuation)
          return OBELISK_RT_INVALID_CONTINUATION;
        size_t processIndex =
            context->nativeScheduleActorIndices[node.actor_slot];
        if (processIndex >= context->scheduledProcesses.size())
          return OBELISK_RT_INVALID_LIFECYCLE;
        const ScheduledProcess &scheduled =
            context->scheduledProcesses[processIndex];
        if (!scheduled.instance || scheduled.aotActorSlot != node.actor_slot)
          return OBELISK_RT_INVALID_LIFECYCLE;
        SchedulerKey key{scheduled.urgent ? 0 : scheduled.queuedRegion,
                         scheduled.urgent ? 0 : scheduled.scheduleRank,
                         scheduled.insertionSequence};
        if (key < selectedNodeKey ||
            (key == selectedNodeKey && candidateNode < selectedNode)) {
          selectedNode = candidateNode;
          selectedNodeKey = key;
        }
      }
    }
    if (selectedNode != UINT32_MAX) {
      nodeRegion = std::get<0>(selectedNodeKey);
      nodeRank = std::get<1>(selectedNodeKey);
      nodeInsertionSequence = std::get<2>(selectedNodeKey);
    }

    SchedulerKey nodeKey{nodeRegion, nodeRank, nodeInsertionSequence};
    SchedulerKey runtimeProcessKey{UINT32_MAX, UINT32_MAX, UINT64_MAX};
    uint64_t runtimeProcessToken = 0;
    size_t urgentDistance = SIZE_MAX;
    size_t processCount = context->scheduledProcesses.size();
    for (size_t index = 0; index != processCount; ++index) {
      const ScheduledProcess &candidate = context->scheduledProcesses[index];
      if (!candidate.instance || candidate.aotActorSlot != UINT32_MAX ||
          context->nativePollCandidates.find(candidate.token) ==
              context->nativePollCandidates.end() ||
          candidate.phase != (context->schedulerRunningFinals ? 1u : 0u) ||
          !nativeProcessReady(*context, candidate, false))
        continue;
      if (candidate.urgent) {
        size_t distance =
            processCount == 0
                ? 0
                : (index + processCount -
                   context->schedulerCursor % processCount) %
                      processCount;
        if (distance < urgentDistance) {
          urgentDistance = distance;
          runtimeProcessKey = SchedulerKey{0, 0, 0};
          runtimeProcessToken = candidate.token;
        }
        continue;
      }
      if (urgentDistance != SIZE_MAX)
        continue;
      bool prioritySignalResume =
          candidate.prioritySignal &&
          (candidate.signalTriggered ||
           (candidate.signalLatch && candidate.signalLatch->triggered));
      SchedulerKey key{candidate.queuedRegion,
                       prioritySignalResume ? 0 : candidate.scheduleRank,
                       prioritySignalResume ? 0 : candidate.insertionSequence};
      if (key < runtimeProcessKey) {
        runtimeProcessKey = key;
        runtimeProcessToken = candidate.token;
      }
    }
    uint32_t barrierRegion = context->schedulerRunningFinals
                                 ? UINT32_MAX
                                 : nextDueNBABarrierRegionUnlocked(
                                       context, /*includeGenerated=*/false);
    SchedulerKey barrierKey =
        barrierRegion == UINT32_MAX
            ? SchedulerKey{UINT32_MAX, UINT32_MAX, UINT64_MAX}
            : SchedulerKey{barrierRegion, 0, 0};
    SchedulerKey runtimeKey = std::min(runtimeProcessKey, barrierKey);
    SchedulerKey runtimeUpperBound = std::min(nodeKey, runtimeKey);

    if (allowBytecode) {
      // At a runtime checkpoint, arbitrate one design task only when its
      // scheduler key precedes both the next generated native node and any
      // same-slot generic process or NBA/control barrier.
      bool designProgress = false;
      obelisk_rt_status status = obelisk_rt_run_one_design_task(
          context, std::get<0>(runtimeUpperBound),
          std::get<1>(runtimeUpperBound), std::get<2>(runtimeUpperBound),
          &designProgress);
      if (status != OBELISK_RT_OK)
        return status;
      if (designProgress) {
        if (context->schedulerSlotProgress == (UINT64_C(1) << 20)) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
          return context->schedulerStatus;
        }
        ++context->schedulerSlotProgress;
        continue;
      }
    }

    if (allowBytecode && runtimeKey < nodeKey) {
      // Execute exactly one same-slot runtime action. Filtering the process
      // inventory preserves direct ownership of generated actors, while the
      // generic scheduler still applies its normal ordering between the
      // selected non-AOT process and all due NBA/event barriers. Because
      // runtimeKey is known runnable at schedulerTime, this single step cannot
      // advance the calendar.
      uint64_t previousTime = context->schedulerTime;
      if (runtimeProcessToken == 0 &&
          (context->nativeSchedulePlan->flags &
           OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL) != 0) {
        obelisk_rt_status status =
            runStaticAOTControlStep(context, false, true);
        if (status == OBELISK_RT_OK)
          continue;
        if (status != OBELISK_RT_TIER_UNAVAILABLE)
          return status;
      }
      NativeScheduleStepScope step(context, UINT32_MAX, false,
                                   /*processFilterActive=*/true,
                                   runtimeProcessToken);
      obelisk_rt_status status = runScheduler(context);
      if (status != OBELISK_RT_OK)
        return status;
      if (context->schedulerTime != previousTime)
        return OBELISK_RT_INVALID_LIFECYCLE;
      continue;
    }

    if (selectedNode != UINT32_MAX) {
      const obelisk_rt_native_schedule_node &node =
          context->nativeScheduleNodes[selectedNode];
      clearNativeAOTNodeReadyUnlocked(context, selectedNode);
      context->nativeScheduleMinimumActivatedNode = UINT32_MAX;
      obelisk_rt_status status =
          allowBytecode ? executeAOTNode(context, node.actor_slot)
                        : executeTrustedAOTNode(context, node.actor_slot);
      if (status != OBELISK_RT_OK)
        return status;
      continue;
    }

    if (allowBytecode)
      return OBELISK_RT_OK;

    uint64_t progress = context->schedulerSlotProgress;
    obelisk_rt_status status =
        runStaticAOTControlStep(context, false, allowBytecode);
    if (status != OBELISK_RT_OK)
      return status;
    if (context->schedulerSlotProgress == progress)
      return OBELISK_RT_OK;
  }
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_prepare_periodic_aot(
    obelisk_rt_context *context, const obelisk_rt_native_schedule_node *nodes,
    uint32_t nodeCount, const obelisk_rt_native_periodic_clock_v1 *clocks,
    uint32_t clockCount, const obelisk_rt_native_periodic_alias_v1 *aliases,
    uint32_t aliasCount, uint64_t *nextEdges,
    obelisk_rt_native_periodic_control_v1 *outControl) {
  if (!context || !nodes || nodeCount == 0 || !clocks || clockCount == 0 ||
      (aliasCount != 0 && !aliases) || !nextEdges || !outControl ||
      activeNativeAOTContext != context ||
      lockedNativeAOTContext != context)
    return OBELISK_RT_INVALID_ARGUMENT;
  // Generated run_until owns `schedulerTime` directly, so time slots complete
  // without re-entering the runtime and the waveform difference would never
  // run. Decline the tier while a dump is open rather than silently dropping
  // slots.
  if (obelisk_rt_dump_active_unlocked(context))
    return OBELISK_RT_TIER_UNAVAILABLE;
  try {
    obelisk_rt_status status =
        initializeNativeAOTNodesUnlocked(context, nodes, nodeCount);
    if (status != OBELISK_RT_OK)
      return status;
    status = drainNativeAOTCurrentSlotUnlocked(context);
    if (status != OBELISK_RT_OK)
      return status;
    // Tier-3 initialization and finite synchronous stimulus must reach a
    // quiescent handoff before generated run_until takes ownership of the
    // periodic sources.  Execute one trusted generic action at a time while a
    // non-AOT process is runnable or subscribed to one of those sources.  A
    // persistent bytecode clock consumer remains a correctness fallback; a
    // finite consumer (for example a reset sequence) naturally disappears
    // after its last edge and pays only this cold-prefix cost.
    auto subscriptionTouchesClock = [&](const SignalSubscription &subscription) {
      uint32_t staticID = 0;
      int64_t offset = 0;
      if (!decodeNativeStatic(subscription.stableID, staticID, offset) ||
          offset < 0)
        return false;
      const NativeStaticState *state = findNativeStaticState(context, staticID);
      if (!state)
        return false;
      uint64_t begin = state->bitOffset + static_cast<uint64_t>(offset);
      uint64_t width = std::max<uint64_t>(subscription.bitWidth, 1);
      uint64_t end =
          width > UINT64_MAX - begin ? UINT64_MAX : begin + width;
      for (uint32_t index = 0; index != clockCount; ++index)
        if (clocks[index].bit_offset >= begin &&
            clocks[index].bit_offset < end)
          return true;
      for (uint32_t index = 0; index != aliasCount; ++index)
        if (aliases[index].target_static_state == staticID &&
            aliases[index].target_bit_offset >= begin &&
            aliases[index].target_bit_offset < end)
          return true;
      return false;
    };
    auto hasGeneratedPeriodicOwner = [&](uint32_t actorSlot,
                                         uint32_t continuation,
                                         const SignalSubscription &subscription) {
      if (actorSlot == UINT32_MAX)
        return false;
      uint32_t staticID = 0;
      int64_t offset = 0;
      if (!decodeNativeStatic(subscription.stableID, staticID, offset) ||
          offset < 0)
        return false;
      uint64_t subscriptionLow = static_cast<uint64_t>(offset);
      uint64_t subscriptionWidth =
          std::max<uint64_t>(subscription.bitWidth, 1);
      for (uint64_t index = 0;
           index != context->nativeScheduleFanoutEntryCount; ++index) {
        const auto &entry = context->nativeScheduleFanoutEntries[index];
        if (entry.reserved == OBELISK_RT_FANOUT_RUNTIME ||
            entry.actor_slot != actorSlot ||
            entry.continuation != continuation ||
            entry.static_state != staticID || entry.bit_width == 0)
          continue;
        uint64_t entryEnd = entry.low_bit + entry.bit_width;
        uint64_t subscriptionEnd = subscriptionLow + subscriptionWidth;
        if (entry.low_bit < subscriptionEnd && subscriptionLow < entryEnd)
          return true;
      }
      return false;
    };
    auto hasTier3PeriodicWork = [&] {
      ContextMutexLock lock(context);
      for (const ScheduledProcess &scheduled : context->scheduledProcesses) {
        if (!scheduled.instance || scheduled.phase != 0)
          continue;
        if (!scheduled.started ||
            scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE) {
          if (context->signalDiagnosticsEnabled)
            std::fprintf(stderr,
                         "obelisk-periodic-bootstrap=runnable actor=%u "
                         "continuation=%u initial=%u started=%u suspend=%u\n",
                         scheduled.aotActorSlot,
                         scheduled.instance->continuation,
                         scheduled.initialProcess, scheduled.started,
                         scheduled.suspendKind);
          return true;
        }
        for (const auto &subscription : scheduled.signalSubscriptions)
          if (subscription && subscriptionTouchesClock(*subscription) &&
              !hasGeneratedPeriodicOwner(scheduled.aotActorSlot,
                                         scheduled.instance->continuation,
                                         *subscription)) {
            if (context->signalDiagnosticsEnabled)
              std::fprintf(stderr,
                           "obelisk-periodic-bootstrap=subscription actor=%u "
                           "continuation=%u initial=%u\n",
                           scheduled.aotActorSlot,
                           scheduled.instance->continuation,
                           scheduled.initialProcess);
            return true;
          }
      }
      for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
        if (task.terminated || task.phase != 0)
          continue;
        if (!task.started || task.suspendKind == OBELISK_RT_SUSPEND_NONE)
          return true;
        for (const auto &subscription : task.signalSubscriptions)
          if (subscription && subscriptionTouchesClock(*subscription))
            return true;
      }
      return false;
    };
    constexpr uint32_t maxPeriodicBootstrapSteps = 4096;
    uint32_t periodicBootstrapSteps = 0;
    while (hasTier3PeriodicWork()) {
      if (periodicBootstrapSteps++ == maxPeriodicBootstrapSteps) {
        if (context->signalDiagnosticsEnabled)
          std::fprintf(stderr, "obelisk-periodic-reject=tier3-bootstrap\n");
        return OBELISK_RT_TIER_UNAVAILABLE;
      }
      NativeScheduleStepScope step(context, UINT32_MAX, false);
      status = runScheduler(context);
      if (status != OBELISK_RT_OK)
        return status;
      status = drainNativeAOTCurrentSlotUnlocked(context, true);
      if (status != OBELISK_RT_OK)
        return status;
      if (context->schedulerFinishRequested)
        break;
    }

    auto periodicFanoutMatches = [&](const obelisk_rt_static_fanout_entry &entry,
                                     const obelisk_rt_native_periodic_clock_v1 &clock) {
      if (entry.static_state != clock.static_state || entry.bit_width == 0)
        return false;
      const NativeStaticState *state =
          findNativeStaticState(context, clock.static_state);
      if (!state || clock.bit_offset < state->bitOffset ||
          clock.bit_offset - state->bitOffset >= state->bitWidth)
        return false;
      uint64_t localBit = clock.bit_offset - state->bitOffset;
      return entry.low_bit <= localBit &&
             localBit - entry.low_bit < entry.bit_width;
    };
    auto aliasTargetFanoutMatches = [
        &](const obelisk_rt_static_fanout_entry &entry,
           const obelisk_rt_native_periodic_alias_v1 &alias) {
      if (entry.static_state != alias.target_static_state ||
          entry.bit_width == 0)
        return false;
      const NativeStaticState *state =
          findNativeStaticState(context, alias.target_static_state);
      if (!state || alias.target_bit_offset < state->bitOffset ||
          alias.target_bit_offset - state->bitOffset >= state->bitWidth)
        return false;
      uint64_t localBit = alias.target_bit_offset - state->bitOffset;
      return entry.low_bit <= localBit &&
             localBit - entry.low_bit < entry.bit_width;
    };
    auto isLiveFanout =
        [&](const obelisk_rt_static_fanout_entry &entry) {
          if (entry.actor_slot >= context->nativeScheduleActors.size() ||
              entry.actor_slot >= context->nativeScheduleActorIndices.size())
            return false;
          obelisk_rt_process_instance_v1 *actor =
              context->nativeScheduleActors[entry.actor_slot];
          if (!actor || actor->continuation != entry.continuation)
            return false;
          size_t processIndex =
              context->nativeScheduleActorIndices[entry.actor_slot];
          if (processIndex >= context->scheduledProcesses.size())
            return false;
          const ScheduledProcess &scheduled =
              context->scheduledProcesses[processIndex];
          return scheduled.instance == actor && scheduled.started &&
                 scheduled.phase == 0 && !scheduled.signalTriggered &&
                 actor->lifecycle == OBELISK_RT_PROCESS_SUSPENDED &&
                 (scheduled.suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
                  scheduled.suspendKind == OBELISK_RT_SUSPEND_EDGE ||
                  scheduled.suspendKind == OBELISK_RT_SUSPEND_OBSERVER);
        };
    auto missingPeriodicFanoutActive = [&] {
      for (uint64_t index = 0;
           index != context->nativeScheduleFanoutEntryCount; ++index) {
        const auto &entry = context->nativeScheduleFanoutEntries[index];
        // A generated owner remains generated even when its source process is
        // classified as an initial process (always/always_comb use that
        // lifecycle flag too). Finite unsupported stimulus remains explicitly
        // runtime-owned until its continuation terminates.
        if (entry.reserved != OBELISK_RT_FANOUT_RUNTIME)
          continue;
        bool periodic = false;
        for (uint32_t clockIndex = 0; clockIndex != clockCount; ++clockIndex)
          periodic |= periodicFanoutMatches(entry, clocks[clockIndex]);
        for (uint32_t aliasIndex = 0; aliasIndex != aliasCount; ++aliasIndex)
          periodic |= aliasTargetFanoutMatches(entry, aliases[aliasIndex]);
        if (!periodic ||
            entry.actor_slot >= context->nativeScheduleActors.size())
          continue;
        obelisk_rt_process_instance_v1 *actor =
            context->nativeScheduleActors[entry.actor_slot];
        if (actor && actor->continuation == entry.continuation &&
            isLiveFanout(entry)) {
          if (context->signalDiagnosticsEnabled)
            std::fprintf(stderr,
                         "obelisk-periodic-bootstrap=fanout actor=%u "
                         "continuation=%u initial=%u reserved=%u\n",
                         entry.actor_slot, entry.continuation,
                         1u, entry.reserved);
          return true;
        }
      }
      return false;
    };
    while (missingPeriodicFanoutActive()) {
      if (periodicBootstrapSteps++ == maxPeriodicBootstrapSteps) {
        if (context->signalDiagnosticsEnabled)
          std::fprintf(stderr, "obelisk-periodic-reject=fanout-bootstrap\n");
        return OBELISK_RT_TIER_UNAVAILABLE;
      }
      uint64_t nextTime = UINT64_MAX;
      for (uint32_t clockIndex = 0; clockIndex != clockCount; ++clockIndex) {
        size_t processIndex =
            context->nativeScheduleActorIndices[clocks[clockIndex].actor_slot];
        if (processIndex >= context->scheduledProcesses.size())
          return OBELISK_RT_INVALID_CONTINUATION;
        nextTime = std::min(nextTime,
                            context->scheduledProcesses[processIndex].wakeTime);
      }
      if (nextTime == UINT64_MAX || nextTime <= context->schedulerTime)
        return OBELISK_RT_INVALID_CONTINUATION;
      obelisk_rt_dump_slot_unlocked(context);
      context->schedulerTime = nextTime;
      status = runPreponedHooks(context);
      if (status != OBELISK_RT_OK)
        return status;
      context->schedulerPreponedTime = nextTime;
      context->schedulerSlotProgress = 0;

      struct MissingActivation {
        uint32_t node;
        uint32_t actor;
        uint32_t continuation;
      };
      std::vector<MissingActivation> activated;
      for (uint32_t clockIndex = 0; clockIndex != clockCount; ++clockIndex) {
        const auto &clock = clocks[clockIndex];
        size_t clockProcessIndex =
            context->nativeScheduleActorIndices[clock.actor_slot];
        ScheduledProcess &clockProcess =
            context->scheduledProcesses[clockProcessIndex];
        if (clockProcess.wakeTime != nextTime)
          continue;
        uint8_t *values = context->nativeSchedulePlan->state_value;
        uint8_t *unknown = context->nativeSchedulePlan->state_unknown;
        if (!values || !unknown)
          return OBELISK_RT_LAYOUT_MISMATCH;
        uint8_t mask = uint8_t{1} << (clock.bit_offset % 8);
        bool rising = (values[clock.bit_offset / 8] & mask) == 0;
        values[clock.bit_offset / 8] ^= mask;
        unknown[clock.bit_offset / 8] &= static_cast<uint8_t>(~mask);
        if (clock.half_period > UINT64_MAX - clockProcess.wakeTime)
          return OBELISK_RT_OUT_OF_RESOURCES;
        clockProcess.wakeTime += clock.half_period;
        setNativeAOTDeadlineUnlocked(context, clock.actor_slot,
                                     clockProcess.wakeTime);

        auto queueFanout = [&](const obelisk_rt_static_fanout_entry &entry)
            -> obelisk_rt_status {
          bool edgeMatches =
              entry.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
              entry.edge == OBELISK_RT_WAIT_EDGE_BOTH ||
              (rising && entry.edge == OBELISK_RT_WAIT_EDGE_POSEDGE) ||
              (!rising && entry.edge == OBELISK_RT_WAIT_EDGE_NEGEDGE);
          if (!edgeMatches || entry.actor_slot >=
                                  context->nativeScheduleActors.size() ||
              entry.kernel >=
                  context->nativeSchedulePlan->clock_kernel_count)
            return OBELISK_RT_OK;
          if (entry.reserved == OBELISK_RT_FANOUT_DIRECT) {
            const auto &kernel =
                context->nativeSchedulePlan->clock_kernels[entry.kernel];
            if (entry.merged_bit / 64 >= kernel.ingress_word_count)
              return OBELISK_RT_INVALID_CONTINUATION;
            kernel.ingress_mask[entry.merged_bit / 64] |=
                uint64_t{1} << (entry.merged_bit % 64);
            context->nativeScheduleClockIngressPending = true;
            return OBELISK_RT_OK;
          }
          obelisk_rt_process_instance_v1 *actor =
              context->nativeScheduleActors[entry.actor_slot];
          // A finite framed process makes the whole periodic slot a runtime
          // transaction.  Run every live actor in event order, then let the
          // generated coordinator drain publications and the shared NBA
          // barrier.  Mixing a runtime reset continuation with generated
          // clocked actors in the same slot can expose a partially reset
          // four-state model at the eventual handoff.
          if (actor && actor->continuation == entry.continuation &&
              isLiveFanout(entry)) {
            if (std::find_if(activated.begin(), activated.end(),
                             [&](const MissingActivation &candidate) {
                               return candidate.actor == entry.actor_slot &&
                                      candidate.continuation ==
                                          entry.continuation;
                             }) == activated.end())
              activated.push_back(
                  {entry.compute_node, entry.actor_slot, entry.continuation});
            return OBELISK_RT_OK;
          }
          return OBELISK_RT_OK;
        };
        for (uint64_t index = 0;
             index != context->nativeScheduleFanoutEntryCount; ++index) {
          const auto &entry = context->nativeScheduleFanoutEntries[index];
          if (!periodicFanoutMatches(entry, clock))
            continue;
          bool forwardingAlias = false;
          for (uint32_t aliasIndex = 0; aliasIndex != aliasCount;
               ++aliasIndex) {
            const auto &alias = aliases[aliasIndex];
            forwardingAlias |=
                alias.source_static_state == clock.static_state &&
                alias.source_bit_offset == clock.bit_offset &&
                alias.forwarding_actor_slot == entry.actor_slot &&
                alias.forwarding_continuation == entry.continuation;
          }
          if (forwardingAlias)
            continue;
          status = queueFanout(entry);
          if (status != OBELISK_RT_OK)
            return status;
        }
        for (uint32_t aliasIndex = 0; aliasIndex != aliasCount; ++aliasIndex) {
          const auto &alias = aliases[aliasIndex];
          if (alias.source_static_state != clock.static_state ||
              alias.source_bit_offset != clock.bit_offset)
            continue;
          auto writeKnownBit = [&](uint64_t bitOffset) {
            uint8_t aliasMask = uint8_t{1} << (bitOffset % 8);
            if (rising)
              values[bitOffset / 8] |= aliasMask;
            else
              values[bitOffset / 8] &= static_cast<uint8_t>(~aliasMask);
            unknown[bitOffset / 8] &= static_cast<uint8_t>(~aliasMask);
          };
          writeKnownBit(alias.driver_bit_offset);
          writeKnownBit(alias.target_bit_offset);
          for (uint64_t index = 0;
               index != context->nativeScheduleFanoutEntryCount; ++index) {
            const auto &entry = context->nativeScheduleFanoutEntries[index];
            if (!aliasTargetFanoutMatches(entry, alias))
              continue;
            status = queueFanout(entry);
            if (status != OBELISK_RT_OK)
              return status;
          }
        }
      }
      std::sort(activated.begin(), activated.end(),
                [](const MissingActivation &lhs,
                   const MissingActivation &rhs) {
        return std::tuple{lhs.node, lhs.actor, lhs.continuation} <
               std::tuple{rhs.node, rhs.actor, rhs.continuation};
      });
      for (const MissingActivation &activation : activated) {
        size_t processIndex =
            context->nativeScheduleActorIndices[activation.actor];
        if (processIndex >= context->scheduledProcesses.size())
          return OBELISK_RT_INVALID_CONTINUATION;
        context->scheduledProcesses[processIndex].signalTriggered = true;
        if (context->signalDiagnosticsEnabled)
          std::fprintf(stderr,
                       "obelisk-periodic-debug=before-actor time=%llu actor=%u "
                       "ingress=%u nba=%u\n",
                       static_cast<unsigned long long>(context->schedulerTime),
                       activation.actor,
                       context->nativeScheduleClockIngressPending ? 1u : 0u,
                       context->staticNBAAccumulatorsPending ? 1u : 0u);
        NativeScheduleStepScope step(context, activation.actor, false);
        status = runScheduler(context);
        if (context->signalDiagnosticsEnabled)
          std::fprintf(stderr,
                       "obelisk-periodic-debug=after-actor time=%llu actor=%u "
                       "status=%u executed=%u ingress=%u nba=%u\n",
                       static_cast<unsigned long long>(context->schedulerTime),
                       activation.actor, static_cast<unsigned>(status),
                       step.executed() ? 1u : 0u,
                       context->nativeScheduleClockIngressPending ? 1u : 0u,
                       context->staticNBAAccumulatorsPending ? 1u : 0u);
        if (status != OBELISK_RT_OK || !step.executed())
          return status != OBELISK_RT_OK ? status
                                         : OBELISK_RT_INVALID_CONTINUATION;
      }
      // An unsupported cold-prefix activation may stage into a generated NBA
      // accumulator without publishing direct ingress.  It still participates
      // in the same slot-wide NBA barrier before the next periodic edge.
      if (!activated.empty())
        context->nativeScheduleClockIngressPending = true;
      if (context->signalDiagnosticsEnabled)
        std::fprintf(stderr,
                     "obelisk-periodic-debug=before-drain time=%llu ingress=%u "
                     "nba=%u\n",
                     static_cast<unsigned long long>(context->schedulerTime),
                     context->nativeScheduleClockIngressPending ? 1u : 0u,
                     context->staticNBAAccumulatorsPending ? 1u : 0u);
      status = drainNativeAOTCurrentSlotUnlocked(context);
      if (context->signalDiagnosticsEnabled)
        std::fprintf(stderr,
                     "obelisk-periodic-debug=after-drain time=%llu status=%u "
                     "ingress=%u nba=%u\n",
                     static_cast<unsigned long long>(context->schedulerTime),
                     static_cast<unsigned>(status),
                     context->nativeScheduleClockIngressPending ? 1u : 0u,
                     context->staticNBAAccumulatorsPending ? 1u : 0u);
      if (status != OBELISK_RT_OK)
        return status;
    }

    auto anyOverride = [](const std::vector<uint64_t> &mask) {
      return std::any_of(mask.begin(), mask.end(),
                         [](uint64_t word) { return word != 0; });
    };
    // Check cleanliness only after the finite Tier-3/bootstrap prefix has
    // drained. Initialization and transient deposits legitimately enter with
    // dirty roots and reconcile above; persistent force/assign state must keep
    // the model on the mask-aware scheduler until release.
    std::unordered_set<uint32_t> generatedWritableStates;
    generatedWritableStates.reserve(
        context->nativeSchedulePlan->merged_fragment_count +
        context->nativeScheduleNBARootCount);
    std::unordered_set<uint32_t> generatedActors;
    generatedActors.reserve(
        context->nativeSchedulePlan->merged_fragment_count);
    if (context->signalDiagnosticsEnabled)
      std::fprintf(stderr,
                   "obelisk-periodic-debug=ownership-counts merged=%llu "
                   "actor-roots=%llu nba-roots=%u fanout=%llu\n",
                   static_cast<unsigned long long>(
                       context->nativeSchedulePlan->merged_fragment_count),
                   static_cast<unsigned long long>(
                       context->nativeScheduleActorRootCount),
                   context->nativeScheduleNBARootCount,
                   static_cast<unsigned long long>(
                       context->nativeScheduleFanoutEntryCount));
    for (uint64_t index = 0;
         index != context->nativeSchedulePlan->merged_fragment_count;
         ++index) {
      const obelisk_rt_native_merged_fragment &fragment =
          context->nativeSchedulePlan->merged_fragments[index];
      if (fragment.execute)
        generatedActors.insert(fragment.actor_slot);
    }
    for (uint64_t index = 0;
         index != context->nativeScheduleActorRootCount; ++index) {
      const obelisk_rt_static_actor_root &root =
          context->nativeScheduleActorRoots[index];
      if ((root.flags & OBELISK_RT_STATIC_ROOT_WRITE) != 0 &&
          generatedActors.find(root.actor_slot) != generatedActors.end())
        generatedWritableStates.insert(root.static_state);
    }
    for (uint32_t index = 0;
         index != context->nativeScheduleNBARootCount; ++index)
      generatedWritableStates.insert(
          context->nativeScheduleNBARoots[index].static_state);
    if (context->signalDiagnosticsEnabled)
      std::fprintf(stderr,
                   "obelisk-periodic-debug=ownership-ready actors=%zu "
                   "states=%zu\n",
                   generatedActors.size(), generatedWritableStates.size());
    auto generatedWritesState = [&](uint32_t staticState) {
      return generatedWritableStates.find(staticState) !=
             generatedWritableStates.end();
    };
    // Delayed Tier-3 work is compatible with run_until: the earliest runtime
    // deadline below becomes a branch-only checkpoint.  Reject only state
    // that invalidates direct access, callback inventories that require a
    // runtime publication, or a live runtime subscription that can actually
    // be reached by the generated closure.  Requiring scheduledDesignTasks to
    // be empty here incorrectly rejects ordinary timeout processes.
    if (context->nativeScheduleExternalWritePending ||
        context->nativeScheduleDirtyRootsPresent ||
        anyOverride(context->forceMask) || anyOverride(context->assignMask) ||
        (context->execution && context->execution->observer_count != 0) ||
        !context->nativeConditionalSignalWaiters.empty() ||
        !context->designConditionalSignalWaiters.empty())
      {
        if (context->signalDiagnosticsEnabled)
          std::fprintf(stderr, "obelisk-periodic-reject=dirty-environment\n");
        return OBELISK_RT_TIER_UNAVAILABLE;
      }
    auto subscriptionReadsGeneratedState =
        [&](const SignalSubscription &subscription) {
          uint32_t staticID = 0;
          int64_t offset = 0;
          return decodeNativeStatic(subscription.stableID, staticID, offset) &&
                 offset >= 0 && generatedWritesState(staticID);
        };
    for (const ScheduledProcess &scheduled : context->scheduledProcesses) {
      if (!scheduled.instance || scheduled.phase != 0 ||
          scheduled.aotActorSlot != UINT32_MAX)
        continue;
      for (const auto &subscription : scheduled.signalSubscriptions)
        if (subscription && subscriptionReadsGeneratedState(*subscription))
          {
            if (context->signalDiagnosticsEnabled)
              std::fprintf(stderr,
                           "obelisk-periodic-reject=process-subscription\n");
            return OBELISK_RT_TIER_UNAVAILABLE;
          }
    }
    for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
      if (task.terminated || task.phase != 0)
        continue;
      for (const auto &subscription : task.signalSubscriptions)
        if (subscription && subscriptionReadsGeneratedState(*subscription))
          {
            if (context->signalDiagnosticsEnabled)
              std::fprintf(stderr,
                           "obelisk-periodic-reject=task-subscription\n");
            return OBELISK_RT_TIER_UNAVAILABLE;
          }
    }
    // A direct publication has no runtime subscriber scan.  Reject only an
    // unsupported live waiter whose source is actually writable by the
    // generated closure; dormant fanout on runtime-only state cannot be
    // reached while run_until owns the calendar and must not pessimize it.
    for (uint64_t index = 0;
         index != context->nativeScheduleFanoutEntryCount; ++index) {
      const auto &entry = context->nativeScheduleFanoutEntries[index];
      if (entry.reserved != OBELISK_RT_FANOUT_RUNTIME ||
          !generatedWritesState(entry.static_state) ||
          entry.actor_slot >= context->nativeScheduleActors.size())
        continue;
      obelisk_rt_process_instance_v1 *actor =
          context->nativeScheduleActors[entry.actor_slot];
      if (actor && actor->continuation == entry.continuation) {
        if (context->signalDiagnosticsEnabled)
          std::fprintf(stderr, "obelisk-periodic-reject=live-fanout\n");
        return OBELISK_RT_TIER_UNAVAILABLE;
      }
    }

    std::vector<uint8_t> claimed(context->nativeScheduleActors.size(), 0);
    std::vector<size_t> claimedProcessIndices;
    claimedProcessIndices.reserve(clockCount);
    for (uint32_t index = 0; index != clockCount; ++index) {
      const auto &clock = clocks[index];
      if (clock.reserved != 0 || clock.half_period == 0 ||
          clock.actor_slot >= context->nativeScheduleActors.size() ||
          claimed[clock.actor_slot])
        return OBELISK_RT_INVALID_ARGUMENT;
      claimed[clock.actor_slot] = 1;
      size_t processIndex = context->nativeScheduleActorIndices[clock.actor_slot];
      if (processIndex >= context->scheduledProcesses.size())
        return OBELISK_RT_INVALID_LIFECYCLE;
      ScheduledProcess &scheduled = context->scheduledProcesses[processIndex];
      obelisk_rt_process_instance_v1 *instance =
          context->nativeScheduleActors[clock.actor_slot];
      const NativeStaticState *state =
          findNativeStaticState(context, clock.static_state);
      if (!instance || scheduled.instance != instance || !scheduled.started ||
          scheduled.suspendKind != OBELISK_RT_SUSPEND_DELAY ||
          instance->continuation != clock.continuation || !state ||
          clock.bit_offset < state->bitOffset ||
          clock.bit_offset - state->bitOffset >= state->bitWidth ||
          !context->nativeSchedulePlan->state_unknown ||
          (context->nativeSchedulePlan
               ->state_unknown[clock.bit_offset / 8] &
           (uint8_t{1} << (clock.bit_offset % 8))) != 0 ||
          scheduled.wakeTime < context->schedulerTime ||
          (scheduled.wakeTime > context->schedulerTime &&
           scheduled.wakeTime - context->schedulerTime > clock.half_period))
        return OBELISK_RT_INVALID_CONTINUATION;
      nextEdges[index] = scheduled.wakeTime;
      claimedProcessIndices.push_back(processIndex);
      removeNativeAOTDeadlineUnlocked(context, clock.actor_slot);
    }
    context->nativePeriodicClockActorSlots.clear();
    context->nativePeriodicClockActorSlots.reserve(clockCount);
    for (uint32_t index = 0; index != clockCount; ++index)
      context->nativePeriodicClockActorSlots.push_back(
          clocks[index].actor_slot);
    context->nativePeriodicTerminationRequested =
        context->schedulerFinishRequested ? 1u : 0u;
    outControl->scheduler_time = &context->schedulerTime;
    outControl->termination_requested =
        &context->nativePeriodicTerminationRequested;
    uint64_t nextRuntimeDeadline = UINT64_MAX;
    auto considerDeadline = [&](uint64_t candidate) {
      if (candidate > context->schedulerTime)
        nextRuntimeDeadline = std::min(nextRuntimeDeadline, candidate);
    };
    for (size_t index = 0; index != context->scheduledProcesses.size();
         ++index) {
      if (std::find(claimedProcessIndices.begin(), claimedProcessIndices.end(),
                    index) != claimedProcessIndices.end())
        continue;
      const ScheduledProcess &scheduled = context->scheduledProcesses[index];
      if (scheduled.instance && scheduled.phase == 0 && scheduled.started &&
          scheduled.suspendKind == OBELISK_RT_SUSPEND_DELAY)
        considerDeadline(scheduled.wakeTime);
    }
    for (const ScheduledDesignTask &task : context->scheduledDesignTasks)
      if (!task.terminated && task.phase == 0 && task.started &&
          task.suspendKind == OBELISK_RT_SUSPEND_DELAY)
        considerDeadline(task.wakeTime);
    for (const ScheduledDesignNBA &update : context->scheduledDesignNBAs)
      considerDeadline(update.dueTime);
    for (const ScheduledNBA &update : context->scheduledNBAs)
      considerDeadline(update.dueTime);
    for (const ScheduledManagedNBA &update : context->scheduledManagedNBAs)
      considerDeadline(update.dueTime);
    for (const ScheduledDesignEvent &event : context->scheduledDesignEvents)
      considerDeadline(event.dueTime);
    outControl->next_runtime_deadline = nextRuntimeDeadline;
    context->nativePeriodicRuntimeDeadline = nextRuntimeDeadline;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_handoff_periodic_aot(
    obelisk_rt_context *context,
    const obelisk_rt_native_periodic_clock_v1 *clocks, uint32_t clockCount,
    const uint64_t *nextEdges) {
  if (!context || !clocks || clockCount == 0 || !nextEdges ||
      activeNativeAOTContext != context || lockedNativeAOTContext != context)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    for (uint32_t index = 0; index != clockCount; ++index) {
      const auto &clock = clocks[index];
      if (clock.actor_slot >= context->nativeScheduleActors.size())
        return OBELISK_RT_INVALID_ARGUMENT;
      size_t processIndex = context->nativeScheduleActorIndices[clock.actor_slot];
      if (processIndex >= context->scheduledProcesses.size())
        return OBELISK_RT_INVALID_LIFECYCLE;
      ScheduledProcess &scheduled = context->scheduledProcesses[processIndex];
      if (!scheduled.instance ||
          scheduled.suspendKind != OBELISK_RT_SUSPEND_DELAY ||
          scheduled.instance->continuation != clock.continuation ||
          nextEdges[index] <= context->schedulerTime)
        return OBELISK_RT_INVALID_CONTINUATION;
      scheduled.wakeTime = nextEdges[index];
      setNativeAOTDeadlineUnlocked(context, clock.actor_slot, nextEdges[index]);
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_execute_aot_actor(
    obelisk_rt_context *context, uint32_t actorSlot) {
  if (!context || activeNativeAOTContext != context ||
      lockedNativeAOTContext != context)
    return OBELISK_RT_INVALID_LIFECYCLE;
  return executeTrustedAOTNode(context, actorSlot);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_queue_aot_checkpoint(
    obelisk_rt_context *context, uint32_t actorSlot, uint32_t continuation,
    obelisk_rt_native_checkpoint_callback callback) {
  if (!context || activeNativeAOTContext != context ||
      lockedNativeAOTContext != context ||
      actorSlot >= context->nativeScheduleActors.size() ||
      continuation == 0 || !callback ||
      context->nativeScheduleCheckpointActorSlot != UINT32_MAX ||
      context->nativeScheduleCheckpointCallback)
    return OBELISK_RT_INVALID_LIFECYCLE;
  obelisk_rt_process_instance_v1 *actor =
      context->nativeScheduleActors[actorSlot];
  size_t processIndex = context->nativeScheduleActorIndices[actorSlot];
  if (!actor || processIndex >= context->scheduledProcesses.size())
    return OBELISK_RT_INVALID_CONTINUATION;
  ScheduledProcess &scheduled = context->scheduledProcesses[processIndex];
  if (scheduled.instance != actor || !scheduled.started ||
      scheduled.aotActorSlot != actorSlot)
    return OBELISK_RT_INVALID_LIFECYCLE;
  // The callback executes an outlined cold leaf; it does not resume the
  // coroutine, whose live continuation intentionally remains at the source
  // wait while Tier 1 owns this activation.  Validate the published identity
  // against the actor descriptor and installed AOT plan instead of comparing
  // it with that dormant coroutine state.
  if (!actor->descriptor || !actor->descriptor->frame_layout ||
      !validContinuation(*actor->descriptor->frame_layout, continuation) ||
      actor->continuation != continuation)
    return OBELISK_RT_INVALID_CONTINUATION;
  context->nativeScheduleCheckpointActorSlot = actorSlot;
  context->nativeScheduleCheckpointContinuation = continuation;
  context->nativeScheduleCheckpointCallback = callback;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_run_aot_nodes(
    obelisk_rt_context *context, const obelisk_rt_native_schedule_node *nodes,
    uint32_t nodeCount) {
  if (!context || !nodes || nodeCount == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  bool trustedSuperstep = false;
  {
    ContextMutexLock lock(context);
    if (!context->nativeSchedulePlan || context->nativeScheduleSingleStep ||
        context->nativeScheduleForcedSlot != UINT32_MAX ||
        context->nativeScheduleControlOnly)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_status status =
        initializeNativeAOTNodesUnlocked(context, nodes, nodeCount);
    if (status != OBELISK_RT_OK)
      return status;
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    trustedSuperstep =
        (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP) != 0 &&
        activeNativeAOTContext == context &&
        lockedNativeAOTContext == context && canUseStaticAOTFanout(context) &&
        !context->nativeScheduleExternalWritePending &&
        !context->nativeScheduleDirtyRootsPresent &&
        nativeStaticSpecializationEnvironmentClean(context) &&
        (!plan->specialization_fast || *plan->specialization_fast != 0);
  }

  if (trustedSuperstep) {
    bool hasClockCoordinator =
        context->nativeSchedulePlan->clock_kernel_count != 0;
    return hasClockCoordinator ? runTrustedAOTNodesUnlocked<true>(context)
                               : runTrustedAOTNodesUnlocked<false>(context);
  }

  uint32_t nodeCursor = 0;
  bool passProgress = false;
  for (;;) {
    uint32_t selectedNode = UINT32_MAX;
    bool readyBeforeCursor = false;
    bool schedulerOrderedBootstrap = false;
    {
      ContextMutexLock lock(context);
      schedulerOrderedBootstrap = obelisk_rt_unstarted_actor_pending(
          context, context->schedulerRunningFinals ? 1u : 0u);
      if (schedulerOrderedBootstrap) {
        using SchedulerKey = std::tuple<uint32_t, uint32_t, uint64_t>;
        SchedulerKey selectedKey{UINT32_MAX, UINT32_MAX, UINT64_MAX};
        for (uint32_t wordIndex = 0;
             wordIndex < context->nativeScheduleReadyNodes.size();
             ++wordIndex) {
          uint64_t word = context->nativeScheduleReadyNodes[wordIndex];
          while (word != 0) {
            uint32_t bit = static_cast<uint32_t>(__builtin_ctzll(word));
            uint32_t candidate = wordIndex * 64 + bit;
            word &= word - 1;
            if (candidate >= context->nativeScheduleNodes.size())
              return OBELISK_RT_INVALID_CONTINUATION;
            const auto &node = context->nativeScheduleNodes[candidate];
            if (node.actor_slot >=
                context->nativeScheduleActorIndices.size())
              return OBELISK_RT_INVALID_CONTINUATION;
            size_t processIndex =
                context->nativeScheduleActorIndices[node.actor_slot];
            if (processIndex >= context->scheduledProcesses.size())
              return OBELISK_RT_INVALID_LIFECYCLE;
            const ScheduledProcess &scheduled =
                context->scheduledProcesses[processIndex];
            SchedulerKey key{scheduled.urgent ? 0 : scheduled.queuedRegion,
                             scheduled.urgent ? 0 : scheduled.scheduleRank,
                             scheduled.insertionSequence};
            if (key < selectedKey ||
                (key == selectedKey && candidate < selectedNode)) {
              selectedNode = candidate;
              selectedKey = key;
            }
          }
        }
      } else if (context->nativeScheduleReadyNodes.size() == 1) {
        uint64_t ready = context->nativeScheduleReadyNodes.front();
        uint64_t afterMask =
            nodeCursor >= 64 ? uint64_t{0} : UINT64_MAX << nodeCursor;
        uint64_t after = ready & afterMask;
        if (after != 0)
          selectedNode = static_cast<uint32_t>(__builtin_ctzll(after));
        else
          readyBeforeCursor = (ready & ~afterMask) != 0;
      } else {
        uint32_t cursorWord = nodeCursor / 64;
        uint32_t cursorBit = nodeCursor % 64;
        for (uint32_t wordIndex = cursorWord;
             wordIndex != context->nativeScheduleReadyNodes.size();
             ++wordIndex) {
          uint64_t word = context->nativeScheduleReadyNodes[wordIndex];
          if (wordIndex == cursorWord && cursorBit != 0)
            word &= UINT64_MAX << cursorBit;
          if (word == 0)
            continue;
          selectedNode =
              wordIndex * 64 + static_cast<uint32_t>(__builtin_ctzll(word));
          break;
        }
        // A lower-order ready node matters only after this pass exhausts the
        // suffix at or above nodeCursor. Do not rescan the already-visited
        // prefix for every selected fragment in a coarse graph pass.
        if (selectedNode == UINT32_MAX) {
          for (uint32_t wordIndex = 0;
               wordIndex <= cursorWord &&
               wordIndex < context->nativeScheduleReadyNodes.size();
               ++wordIndex) {
            uint64_t word = context->nativeScheduleReadyNodes[wordIndex];
            if (wordIndex == cursorWord && cursorBit != 0)
              word &= (uint64_t{1} << cursorBit) - 1;
            if (word != 0) {
              readyBeforeCursor = true;
              break;
            }
          }
        }
      }
      if (selectedNode != UINT32_MAX) {
        const obelisk_rt_native_schedule_node &node =
            context->nativeScheduleNodes[selectedNode];
        if (node.actor_slot >= context->nativeScheduleActors.size() ||
            !context->nativeScheduleActors[node.actor_slot] ||
            context->nativeScheduleActors[node.actor_slot]->continuation !=
                node.continuation)
          return OBELISK_RT_INVALID_CONTINUATION;
        clearNativeAOTNodeReadyUnlocked(context, selectedNode);
      }
    }
    if (selectedNode != UINT32_MAX) {
      uint32_t lastNode = selectedNode;
      bool restartBeforeCursor = false;
      for (;;) {
        const obelisk_rt_native_schedule_node &selected =
            context->nativeScheduleNodes[lastNode];
        {
          ContextMutexLock lock(context);
          context->nativeScheduleMinimumActivatedNode = UINT32_MAX;
        }
        obelisk_rt_status status =
            executeAOTNode(context, selected.actor_slot);
        if (status != OBELISK_RT_OK)
          return status;
        passProgress = true;

        uint32_t nextNode = lastNode + 1;
        bool fuseNext = false;
        {
          ContextMutexLock lock(context);
          restartBeforeCursor =
              context->nativeScheduleMinimumActivatedNode < nextNode;
          if (!schedulerOrderedBootstrap && !restartBeforeCursor &&
              selected.fusion_group != UINT32_MAX &&
              nextNode < context->nativeScheduleNodes.size() &&
              (!context->nativeSchedulePlan->specialization_fast ||
               *context->nativeSchedulePlan->specialization_fast != 0)) {
            const obelisk_rt_native_schedule_node &next =
                context->nativeScheduleNodes[nextNode];
            uint64_t mask = uint64_t{1} << (nextNode % 64);
            fuseNext =
                next.fusion_group == selected.fusion_group &&
                (context->nativeScheduleReadyNodes[nextNode / 64] & mask) !=
                    0 &&
                next.actor_slot < context->nativeScheduleActors.size() &&
                context->nativeScheduleActors[next.actor_slot] &&
                context->nativeScheduleActors[next.actor_slot]->continuation ==
                    next.continuation;
            if (fuseNext)
              clearNativeAOTNodeReadyUnlocked(context, nextNode);
          }
        }
        if (!fuseNext)
          break;
        lastNode = nextNode;
      }
      nodeCursor = schedulerOrderedBootstrap || restartBeforeCursor
                       ? 0
                       : lastNode + 1;
      continue;
    }
    if (passProgress) {
      ContextMutexLock lock(context);
      ++context->signalDiagnostics.aotRegionPasses;
    }
    if (readyBeforeCursor) {
      nodeCursor = 0;
      passProgress = false;
      continue;
    }

    uint64_t previousProgress;
    uint64_t previousTime;
    bool previousFinals;
    bool controlOnly;
    {
      ContextMutexLock lock(context);
      previousProgress = context->schedulerSlotProgress;
      previousTime = context->schedulerTime;
      previousFinals = context->schedulerRunningFinals;
      controlOnly = (context->nativeSchedulePlan->flags &
                     OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL) != 0;
    }
    obelisk_rt_status status;
    bool genericControl = !controlOnly;
    if (controlOnly) {
      status = runStaticAOTControlStep(context);
      if (status == OBELISK_RT_TIER_UNAVAILABLE) {
        genericControl = true;
        NativeScheduleStepScope step(context, UINT32_MAX, true);
        status = runScheduler(context);
      }
    } else {
      // The generic scheduler owns hybrid time/region control, but actor
      // execution must return through executeAOTNode so state-plane handoff
      // and continuation validation remain atomic.
      NativeScheduleStepScope step(context, UINT32_MAX, true);
      status = runScheduler(context);
    }
    bool controlProgress;
    {
      ContextMutexLock lock(context);
      if (genericControl && !markDueNativeAOTDeadlinesUnlocked(context))
        return OBELISK_RT_INVALID_CONTINUATION;
      if (context->schedulerRunningFinals != previousFinals) {
        obelisk_rt_status refresh = refreshNativeAOTReadyPhaseUnlocked(context);
        if (refresh != OBELISK_RT_OK)
          return refresh;
      }
      controlProgress = context->schedulerSlotProgress != previousProgress ||
                        context->schedulerTime != previousTime ||
                        context->schedulerRunningFinals != previousFinals;
    }
    if (status != OBELISK_RT_OK || !controlProgress)
      return status;
    nodeCursor = 0;
    passProgress = false;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_snapshot_aot(
    obelisk_rt_context *context, obelisk_rt_aot_deopt_snapshot *outSnapshot) {
  if (!context || !outSnapshot)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    if (!plan || context->nativeScheduleRunning ||
        context->nativeScheduleDeoptimized || context->activeNativeProcess)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (plan->state_bit_count != 0 &&
        (!reconcileNativeDirtyRootsToPlanesUnlocked(context, plan) ||
         !importNativeStatePlanesUnlocked(context, plan->state_value,
                                          plan->state_unknown,
                                          plan->state_bit_count)))
      return OBELISK_RT_LAYOUT_MISMATCH;
    for (uint32_t root = 0; root != context->nativeScheduleNBARootCount;
         ++root) {
      const obelisk_rt_generated_nba_accumulator_256 *generated =
          context->nativeScheduleNBARoots[root].generated_accumulator;
      if (!generated || !hasGeneratedNBAStages(*generated))
        continue;
      if (obelisk_rt_status status = materializeGeneratedNBAAccumulatorUnlocked(
              context, root, generated->exec_region);
          status != OBELISK_RT_OK)
        return status;
    }

    auto &actors = context->nativeScheduleSnapshotActors;
    auto &nbas = context->nativeScheduleSnapshotNBAs;
    actors.clear();
    nbas.clear();
    actors.reserve(context->nativeScheduleActors.size());
    uint32_t readyCount = 0;
    for (uint32_t slot = 0; slot != context->nativeScheduleActors.size();
         ++slot) {
      obelisk_rt_process_instance_v1 *instance =
          context->nativeScheduleActors[slot];
      if (!instance)
        continue;
      size_t index = context->nativeScheduleActorIndices[slot];
      if (index >= context->scheduledProcesses.size())
        return OBELISK_RT_INVALID_LIFECYCLE;
      const ScheduledProcess &scheduled = context->scheduledProcesses[index];
      if (scheduled.instance != instance ||
          scheduled.token != context->nativeScheduleActorTokens[slot] ||
          scheduled.aotActorSlot != slot)
        return OBELISK_RT_INVALID_LIFECYCLE;
      bool ready = !scheduled.started ||
                   scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE ||
                   (scheduled.suspendKind == OBELISK_RT_SUSPEND_DELAY
                        ? scheduled.wakeTime <= context->schedulerTime
                        : scheduled.signalTriggered);
      readyCount += ready;
      obelisk_rt_fragment_action_v1 action{
          scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE
              ? OBELISK_RT_FRAGMENT_CONTINUE
              : OBELISK_RT_FRAGMENT_SUSPEND,
          scheduled.suspendKind,
          instance->continuation,
          scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE
              ? OBELISK_RT_FRAGMENT_FLAGS_NONE
              : OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
          scheduled.waitOffset,
          scheduled.waitSize};
      actors.push_back(
          {slot, scheduled.phase,
           nativeAOTContinuationRank(scheduled, instance->continuation),
           scheduled.queuedRegion, scheduled.insertionSequence,
           scheduled.wakeTime, scheduled.waitOffset, scheduled.waitSize, action,
           scheduled.started ? 1u : 0u, ready ? 1u : 0u});
    }

    uint32_t nbaSlot = 0;
    auto appendNBA = [&](uint32_t region, uint64_t sequence, uint64_t dueTime) {
      nbas.push_back({nbaSlot++, region, sequence, dueTime});
    };
    for (const ScheduledNBA &nba : context->scheduledNBAs)
      appendNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledManagedNBA &nba : context->scheduledManagedNBAs)
      appendNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledDesignNBA &nba : context->scheduledDesignNBAs)
      appendNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledDesignEvent &event : context->scheduledDesignEvents)
      appendNBA(event.execRegion, event.sequence, event.dueTime);
    for (const StaticNBAAccumulator &accumulator :
         context->staticNBAAccumulators)
      if (accumulator.valid)
        appendNBA(accumulator.execRegion, accumulator.sequence,
                  context->schedulerTime);
    if (nbas.size() > UINT32_MAX)
      return OBELISK_RT_OUT_OF_RESOURCES;

    *outSnapshot = {sizeof(obelisk_rt_aot_deopt_snapshot),
                    context->schedulerTime,
                    actors.empty() ? nullptr : actors.data(),
                    static_cast<uint32_t>(actors.size()),
                    readyCount,
                    nbas.empty() ? nullptr : nbas.data(),
                    static_cast<uint32_t>(nbas.size()),
                    0,
                    context->nextSchedulerSequence};
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

// This is the cold runtime boundary around a generated run-until invocation.
// Keeping it out of its caller prevents checkpoint/deoptimization state from
// extending live ranges through the generated hot loop after whole-program
// optimization. A native plan normally crosses this boundary only at startup,
// completion, or an asynchronous handoff.
extern "C" [[gnu::noinline]] obelisk_rt_status
obelisk_rt_v1_scheduler_run_aot(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  const obelisk_rt_native_schedule_plan *plan = nullptr;
  bool guardedFanout = false;
  bool specializationFast = false;
  {
    ContextMutexLock lock(context);
    plan = context->nativeSchedulePlan;
    if (!plan)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (context->nativeScheduleRunning)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (context->nativeScheduleDeoptimized)
      return runScheduler(context);
    guardedFanout =
        (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT) != 0 &&
        !context->nativeScheduleExternalWritePending &&
        !context->nativeScheduleDirtyRootsPresent &&
        nativeStaticSpecializationEnvironmentClean(context);
    specializationFast = plan->specialization_fast &&
                         !context->nativeScheduleExternalWritePending &&
                         !context->nativeScheduleDirtyRootsPresent &&
                         nativeStaticSpecializationEnvironmentClean(context);
    if (plan->specialization_fast)
      *plan->specialization_fast = specializationFast ? 1 : 0;
    if (specializationFast && context->signalDiagnosticsEnabled)
      ++context->signalDiagnostics.aotStateFastPaths;
    context->nativeScheduleGuardedFanoutActive = guardedFanout;
    context->nativeScheduleRunning = true;
  }
  auto needsTransientHandoff = [&] {
    ContextMutexLock lock(context);
    auto anyOverride = [](const std::vector<uint64_t> &mask) {
      return std::any_of(mask.begin(), mask.end(),
                         [](uint64_t word) { return word != 0; });
    };
    return context->nativeScheduleExternalWritePending ||
           context->nativeScheduleDirtyRootsPresent ||
           anyOverride(context->forceMask) || anyOverride(context->assignMask);
  };
  auto runTransientHandoff = [&](bool &reachedBoundary) {
    {
      ContextMutexLock lock(context);
      if (plan->state_bit_count != 0 &&
          (!reconcileNativeDirtyRootsToPlanesUnlocked(context, plan) ||
           !importNativeStatePlanesUnlocked(context, plan->state_value,
                                            plan->state_unknown,
                                            plan->state_bit_count)))
        return OBELISK_RT_LAYOUT_MISMATCH;
      context->nativeScheduleRunning = true;
    }
    obelisk_rt_status transientStatus = OBELISK_RT_OK;
    {
      NativeAOTContextScope aotScope(context);
      NativeAOTMutexScope mutexScope(context);
      NativeScheduleCleanBoundaryScope boundaryScope(context);
      transientStatus = runScheduler(context);
      reachedBoundary = boundaryScope.reached();
    }
    {
      ContextMutexLock lock(context);
      if (plan->state_bit_count != 0 &&
          !exportNativeStatePlanesUnlocked(context, plan->state_value,
                                           plan->state_unknown,
                                           plan->state_bit_count) &&
          transientStatus == OBELISK_RT_OK)
        transientStatus = OBELISK_RT_LAYOUT_MISMATCH;
      // The fine scheduler has reached a canonical quiescent handback. Let the
      // generated plan perform its one promotion scan now, after canonical
      // export and before its Tier-1 entry is invoked again. Promotion is a
      // routing choice, so a false result does not delay the handback.
      if (transientStatus == OBELISK_RT_OK && reachedBoundary &&
          plan->promotion_ready)
        (void)plan->promotion_ready();
      context->nativeScheduleRunning = false;
    }
    return transientStatus;
  };

retryNativeSchedule:;
  obelisk_rt_status status = OBELISK_RT_OK;
  if (needsTransientHandoff()) {
    bool reachedBoundary = false;
    status = runTransientHandoff(reachedBoundary);
    if (status != OBELISK_RT_OK || !reachedBoundary)
      return status;
    {
      ContextMutexLock lock(context);
      guardedFanout =
          (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT) != 0 &&
          !context->nativeScheduleDirtyRootsPresent &&
          nativeStaticSpecializationEnvironmentClean(context);
      specializationFast =
          plan->specialization_fast &&
          nativeStaticSpecializationEnvironmentClean(context);
      if (plan->specialization_fast)
        *plan->specialization_fast = specializationFast ? 1 : 0;
      context->nativeScheduleGuardedFanoutActive = guardedFanout;
      context->nativeScheduleRunning = true;
    }
  }
  {
    NativeAOTContextScope aotScope(context);
    auto invoke = [&] {
      try {
        return plan->run(plan->mutable_state, context);
      } catch (const std::bad_alloc &) {
        return OBELISK_RT_OUT_OF_MEMORY;
      } catch (...) {
        return OBELISK_RT_INVALID_ARGUMENT;
      }
    };
    if ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT) != 0 ||
        guardedFanout || specializationFast) {
      NativeAOTMutexScope mutexScope(context);
      status = invoke();
    } else {
      status = invoke();
    }
  }
  {
    ContextMutexLock lock(context);
    if (plan->specialization_fast)
      *plan->specialization_fast = 0;
    context->nativeScheduleGuardedFanoutActive = false;
    if (specializationFast && status != OBELISK_RT_TIER_UNAVAILABLE &&
        plan->state_bit_count != 0) {
      bool synchronized =
          (!context->nativeScheduleExternalWritePending ||
           reconcileNativeDirtyRootsToPlanesUnlocked(context, plan)) &&
          importNativeStatePlanesUnlocked(context, plan->state_value,
                                          plan->state_unknown,
                                          plan->state_bit_count);
      if (!synchronized)
        status = OBELISK_RT_LAYOUT_MISMATCH;
    }
    context->nativeScheduleRunning = false;
  }
  if (status == OBELISK_RT_AOT_CHECKPOINT ||
      status == OBELISK_RT_AOT_TIMED_CHECKPOINT) {
    bool timedCheckpoint = status == OBELISK_RT_AOT_TIMED_CHECKPOINT;
    bool generatedBranchCheckpoint = false;
    obelisk_rt_native_checkpoint_callback checkpointCallback = nullptr;
    auto takeGeneratedCheckpointUnlocked = [&]() -> obelisk_rt_status {
      uint32_t actorSlot = context->nativeScheduleCheckpointActorSlot;
      obelisk_rt_process_instance_v1 *actor =
          actorSlot < context->nativeScheduleActors.size()
              ? context->nativeScheduleActors[actorSlot]
              : nullptr;
      if (!actor || !context->nativeScheduleCheckpointCallback ||
          !actor->descriptor || !actor->descriptor->frame_layout ||
          !validContinuation(*actor->descriptor->frame_layout,
                             context->nativeScheduleCheckpointContinuation) ||
          actor->continuation !=
              context->nativeScheduleCheckpointContinuation) {
        context->nativeScheduleCheckpointCallback = nullptr;
        context->nativeScheduleCheckpointActorSlot = UINT32_MAX;
        context->nativeScheduleCheckpointContinuation = 0;
        return OBELISK_RT_INVALID_CONTINUATION;
      }
      checkpointCallback = context->nativeScheduleCheckpointCallback;
      context->nativeScheduleCheckpointCallback = nullptr;
      context->nativeScheduleCheckpointActorSlot = UINT32_MAX;
      context->nativeScheduleCheckpointContinuation = 0;
      return OBELISK_RT_OK;
    };
    status = OBELISK_RT_OK;
    uint64_t checkpointProgressBefore = 0;
    uint64_t checkpointTimeBefore = 0;
    {
      ContextMutexLock lock(context);
      if (plan->state_bit_count != 0 &&
          (!reconcileNativeDirtyRootsToPlanesUnlocked(context, plan) ||
           !importNativeStatePlanesUnlocked(context, plan->state_value,
                                            plan->state_unknown,
                                            plan->state_bit_count)))
        return OBELISK_RT_LAYOUT_MISMATCH;
      // run_until has already advanced schedulerTime to the checkpoint. Make
      // framed AOT continuations due at that exact time visible before the
      // one-step runtime action. Without this refresh an unsupported timed
      // continuation is absent from the generic poll index, so the step can
      // advance to the next periodic clock and retry the same checkpoint
      // forever (most visibly with two independent generated clocks).
      if (!markDueNativeAOTDeadlinesUnlocked(context))
        return OBELISK_RT_INVALID_CONTINUATION;
      generatedBranchCheckpoint =
          context->nativeScheduleCheckpointActorSlot != UINT32_MAX &&
          context->nativeScheduleCheckpointCallback;
      if (generatedBranchCheckpoint) {
        obelisk_rt_status checkpointStatus =
            takeGeneratedCheckpointUnlocked();
        if (checkpointStatus != OBELISK_RT_OK)
          return checkpointStatus;
      }
      checkpointProgressBefore = context->schedulerSlotProgress;
      checkpointTimeBefore = context->schedulerTime;
    }
    // run_until stops on the last periodic edge preceding the runtime
    // deadline. Advance directly to that proven checkpoint and execute only
    // continuations due there. Polling all generic design evaluators would
    // duplicate Tier-1 ownership and can recreate the preceding slot forever.
    bool finishing = false;
    {
      NativeAOTContextScope aotScope(context);
      NativeAOTMutexScope mutexScope(context);
      if (!timedCheckpoint) {
        if (generatedBranchCheckpoint) {
          do {
            status = checkpointCallback(context);
            {
              ContextMutexLock lock(context);
              if (context->schedulerSlotProgress == (UINT64_C(1) << 20)) {
                status = OBELISK_RT_OUT_OF_RESOURCES;
                break;
              }
              ++context->schedulerSlotProgress;
              finishing |= context->schedulerFinishRequested;
            }
            if (status != OBELISK_RT_AOT_GENERATED_CHECKPOINT)
              break;
            ContextMutexLock lock(context);
            status = takeGeneratedCheckpointUnlocked();
          } while (status == OBELISK_RT_OK);
          if (status != OBELISK_RT_OK)
            goto checkpointDone;
          if (finishing) {
            status = runScheduler(context);
            if (status != OBELISK_RT_OK)
              goto checkpointDone;
          }
        } else {
          NativeScheduleStepScope step(context, UINT32_MAX, false);
          status = runScheduler(context);
        }
        if (status == OBELISK_RT_OK && plan->state_bit_count != 0) {
          ContextMutexLock lock(context);
          if (!exportNativeStatePlanesUnlocked(
                  context, plan->state_value, plan->state_unknown,
                  plan->state_bit_count))
            status = OBELISK_RT_LAYOUT_MISMATCH;
        }
        goto checkpointDone;
      }
      {
        ContextMutexLock lock(context);
        uint64_t deadline = context->nativePeriodicRuntimeDeadline;
        if (deadline == UINT64_MAX || deadline <= context->schedulerTime)
          status = OBELISK_RT_TIER_UNAVAILABLE;
        else {
          obelisk_rt_dump_slot_unlocked(context);
          context->schedulerTime = deadline;
          status = runPreponedHooks(context);
          if (status == OBELISK_RT_OK) {
            context->schedulerPreponedTime = deadline;
            context->schedulerSlotProgress = 0;
            if (context->staticNBASlowRootsPresent) {
              std::fill(context->staticNBASlowRoots.begin(),
                        context->staticNBASlowRoots.end(), uint8_t{0});
              context->staticNBASlowRootsPresent = false;
            }
            refreshNativeStaticSpecializationFastUnlocked(context);
          }
        }
      }
      if (status != OBELISK_RT_OK)
        goto checkpointDone;
      {
        ContextMutexLock lock(context);
        // handoff_periodic_aot restored every detached clock deadline so a
        // non-checkpoint exit remains transactionally complete. At an exact
        // runtime/clock tie, reserve those sources for the generated loop:
        // Tier 2 drains only genuine runtime work and run_until then observes
        // nextEdges == schedulerTime and executes the coincident edge.
        for (uint32_t actorSlot : context->nativePeriodicClockActorSlots)
          removeNativeAOTDeadlineUnlocked(context, actorSlot);
        if (!markDueNativeAOTDeadlinesUnlocked(context))
          return OBELISK_RT_INVALID_CONTINUATION;
      }
      // Drain the checkpoint slot with generic ordering arbitration around
      // direct generated Tier-2 nodes. Future calendar entries remain owned
      // by run_until.
      status = drainNativeAOTCurrentSlotUnlocked(context, true);
      if (status != OBELISK_RT_OK)
        return status;
      {
        ContextMutexLock lock(context);
        finishing = context->schedulerFinishRequested;
      }
      if (finishing) {
        // Finish/final callbacks are an enabled synchronization checkpoint,
        // not evidence that the generated schedule is invalid. Run them on
        // the runtime side and return without snapshot deoptimization.
        status = runScheduler(context);
        if (status != OBELISK_RT_OK)
          return status;
      }
      {
        ContextMutexLock lock(context);
        if (plan->state_bit_count != 0 &&
            !exportNativeStatePlanesUnlocked(context, plan->state_value,
                                             plan->state_unknown,
                                             plan->state_bit_count))
          return OBELISK_RT_LAYOUT_MISMATCH;
      }
    }
  checkpointDone:
    if (status != OBELISK_RT_OK && status != OBELISK_RT_TIER_UNAVAILABLE)
      return status;
    if (finishing && status == OBELISK_RT_OK)
      return status;
    bool checkpointProgress = false;
    {
      ContextMutexLock lock(context);
      checkpointProgress =
          status == OBELISK_RT_OK &&
          (context->schedulerSlotProgress != checkpointProgressBefore ||
           context->schedulerTime != checkpointTimeBefore ||
           context->schedulerFinishRequested);
      if (!checkpointProgress) {
        // A checkpointed continuation that is not directly runnable by either
        // current-slot drain must deopt transactionally. Retrying run_until
        // without observable progress would rediscover the same deadline.
        status = OBELISK_RT_TIER_UNAVAILABLE;
      }
      guardedFanout =
          (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT) != 0 &&
          !context->nativeScheduleExternalWritePending &&
          !context->nativeScheduleDirtyRootsPresent &&
          nativeStaticSpecializationEnvironmentClean(context);
      specializationFast =
          plan->specialization_fast &&
          !context->nativeScheduleExternalWritePending &&
          !context->nativeScheduleDirtyRootsPresent &&
          nativeStaticSpecializationEnvironmentClean(context);
      if (plan->specialization_fast)
        *plan->specialization_fast = specializationFast ? 1 : 0;
      context->nativeScheduleGuardedFanoutActive =
          checkpointProgress ? guardedFanout : false;
      context->nativeScheduleRunning = checkpointProgress;
    }
    if (checkpointProgress)
      goto retryNativeSchedule;
  }
  if (status == OBELISK_RT_TIER_UNAVAILABLE && needsTransientHandoff())
    goto retryNativeSchedule;
  if (status != OBELISK_RT_TIER_UNAVAILABLE)
    return status;

  {
    ContextMutexLock lock(context);
    if (plan->state_bit_count != 0 &&
        (!reconcileNativeDirtyRootsToPlanesUnlocked(context, plan) ||
         !importNativeStatePlanesUnlocked(context, plan->state_value,
                                          plan->state_unknown,
                                          plan->state_bit_count)))
      return OBELISK_RT_LAYOUT_MISMATCH;
  }
  obelisk_rt_aot_deopt_snapshot snapshot{};
  status = plan->fallback_snapshot(plan->mutable_state, context, &snapshot);
  if (status != OBELISK_RT_OK)
    return status;
  {
    ContextMutexLock lock(context);
    uint32_t liveActors = 0;
    for (obelisk_rt_process_instance_v1 *actor : context->nativeScheduleActors)
      liveActors += actor != nullptr;
    uint64_t pendingNBAs = context->scheduledNBAs.size();
    pendingNBAs += context->scheduledManagedNBAs.size();
    pendingNBAs += context->scheduledDesignNBAs.size();
    pendingNBAs += context->scheduledDesignEvents.size();
    for (const StaticNBAAccumulator &accumulator :
         context->staticNBAAccumulators)
      pendingNBAs += accumulator.valid;
    bool valid = snapshot.size == sizeof(obelisk_rt_aot_deopt_snapshot) &&
                 snapshot.current_time == context->schedulerTime &&
                 snapshot.actor_count == liveActors &&
                 (snapshot.actor_count == 0 || snapshot.actors) &&
                 pendingNBAs <= UINT32_MAX &&
                 snapshot.nba_count == pendingNBAs &&
                 (snapshot.nba_count == 0 || snapshot.nbas) &&
                 snapshot.ready_count <= snapshot.actor_count &&
                 snapshot.reserved == 0 &&
                 snapshot.next_sequence == context->nextSchedulerSequence;
    uint32_t readyCount = 0;
    uint32_t previousSlot = UINT32_MAX;
    for (uint32_t index = 0; valid && index != snapshot.actor_count; ++index) {
      const obelisk_rt_aot_deopt_actor &actor = snapshot.actors[index];
      if (actor.slot >= context->nativeScheduleActors.size() ||
          (index != 0 && actor.slot <= previousSlot)) {
        valid = false;
        break;
      }
      previousSlot = actor.slot;
      size_t processIndex = context->nativeScheduleActorIndices[actor.slot];
      obelisk_rt_process_instance_v1 *instance =
          context->nativeScheduleActors[actor.slot];
      if (!instance || processIndex >= context->scheduledProcesses.size()) {
        valid = false;
        break;
      }
      const ScheduledProcess &scheduled =
          context->scheduledProcesses[processIndex];
      bool ready = !scheduled.started ||
                   scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE ||
                   (scheduled.suspendKind == OBELISK_RT_SUSPEND_DELAY
                        ? scheduled.wakeTime <= context->schedulerTime
                        : scheduled.signalTriggered);
      readyCount += ready;
      valid =
          scheduled.instance == instance &&
          scheduled.aotActorSlot == actor.slot &&
          actor.flags == scheduled.phase &&
          actor.schedule_rank ==
              nativeAOTContinuationRank(scheduled, instance->continuation) &&
          actor.queued_region == scheduled.queuedRegion &&
          actor.insertion_sequence == scheduled.insertionSequence &&
          actor.wake_time == scheduled.wakeTime &&
          actor.wait_offset == scheduled.waitOffset &&
          actor.wait_size == scheduled.waitSize &&
          actor.started == (scheduled.started ? 1u : 0u) &&
          actor.ready == (ready ? 1u : 0u) &&
          actor.action.kind == (scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE
                                    ? OBELISK_RT_FRAGMENT_CONTINUE
                                    : OBELISK_RT_FRAGMENT_SUSPEND) &&
          actor.action.suspend_kind == scheduled.suspendKind &&
          actor.action.continuation == instance->continuation &&
          actor.action.payload == scheduled.waitOffset &&
          actor.action.auxiliary == scheduled.waitSize;
    }
    valid &= readyCount == snapshot.ready_count;
    uint32_t nbaIndex = 0;
    auto validateNBA = [&](uint32_t region, uint64_t sequence,
                           uint64_t dueTime) {
      if (!valid || nbaIndex >= snapshot.nba_count)
        return;
      const obelisk_rt_aot_deopt_nba &nba = snapshot.nbas[nbaIndex];
      valid = nba.slot == nbaIndex && nba.exec_region == region &&
              nba.sequence == sequence && nba.due_time == dueTime;
      ++nbaIndex;
    };
    for (const ScheduledNBA &nba : context->scheduledNBAs)
      validateNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledManagedNBA &nba : context->scheduledManagedNBAs)
      validateNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledDesignNBA &nba : context->scheduledDesignNBAs)
      validateNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledDesignEvent &event : context->scheduledDesignEvents)
      validateNBA(event.execRegion, event.sequence, event.dueTime);
    for (const StaticNBAAccumulator &accumulator :
         context->staticNBAAccumulators)
      if (accumulator.valid)
        validateNBA(accumulator.execRegion, accumulator.sequence,
                    context->schedulerTime);
    valid &= nbaIndex == snapshot.nba_count;
    if (!valid)
      return OBELISK_RT_INVALID_ARGUMENT;
    for (uint32_t index = 0; index != snapshot.actor_count; ++index) {
      const obelisk_rt_aot_deopt_actor &actor = snapshot.actors[index];
      size_t processIndex = context->nativeScheduleActorIndices[actor.slot];
      ScheduledProcess &scheduled = context->scheduledProcesses[processIndex];
      scheduled.scheduleRank = actor.schedule_rank;
      // Direct AOT change/edge waits use static fanout instead of runtime
      // subscriptions. A different actor can force whole-plan fallback while
      // such a wait remains blocked; rehydrate it before pending NBAs are
      // committed by the generic scheduler so the publication cannot be
      // lost across the handoff.
      bool directSignalWait =
          actor.action.kind == OBELISK_RT_FRAGMENT_SUSPEND &&
          (actor.action.suspend_kind == OBELISK_RT_SUSPEND_CHANGE ||
           actor.action.suspend_kind == OBELISK_RT_SUSPEND_EDGE);
      if (!actor.ready && directSignalWait &&
          scheduled.signalSubscriptions.empty()) {
        status = adoptScheduledSuspendUnlocked(context, scheduled,
                                               actor.action);
        if (status != OBELISK_RT_OK)
          return status;
      }
    }
    context->nativeScheduleDeoptimized = true;
    rebuildNativeSchedulerIndexUnlocked(context);
    ++context->signalDiagnostics.aotFallbacks;
  }
  return runScheduler(context);
}

void obelisk_rt_release_native_schedule_plan(
    obelisk_rt_context *context) noexcept {
  if (!context || !context->nativeSchedulePlan)
    return;
  try {
    if (context->nativeSchedulePlan->specialization_fast)
      *context->nativeSchedulePlan->specialization_fast = 0;
    if (context->nativeSchedulePlan->promotion_invalidate)
      context->nativeSchedulePlan->promotion_invalidate();
    for (uint32_t root = 0; root != context->nativeScheduleNBARootCount; ++root)
      if (context->nativeScheduleNBARoots[root].generated_accumulator)
        *context->nativeScheduleNBARoots[root].generated_accumulator = {};
    for (uint32_t slot = 0; slot != context->nativeScheduleActors.size();
         ++slot)
      if (context->nativeScheduleActors[slot])
        (void)context->nativeSchedulePlan->bind(
            context->nativeSchedulePlan->mutable_state, context, slot, nullptr);
    std::lock_guard<std::mutex> lock(nativeScheduleRegistryMutex);
    installedNativeScheduleStates.erase(
        context->nativeSchedulePlan->mutable_state);
  } catch (...) {
  }
  context->nativeSchedulePlan = nullptr;
  context->nativeScheduleNBARoots = nullptr;
  context->nativeScheduleNBARootCount = 0;
  context->nativeScheduleNBASites = nullptr;
  context->nativeScheduleNBASiteCount = 0;
  context->nativeScheduleFanoutEntries = nullptr;
  context->nativeScheduleFanoutEntryCount = 0;
  context->nativeScheduleActorRoots = nullptr;
  context->nativeScheduleActorRootCount = 0;
  context->nativeScheduleActorRootRanges.clear();
  context->nativeScheduleNBASiteIndex.clear();
  context->nativeScheduleActors.clear();
  context->nativeScheduleActorTokens.clear();
  context->nativeScheduleActorIndices.clear();
  context->nativeScheduleNodes.clear();
  context->nativeScheduleActorNodes.clear();
  context->nativeScheduleFanoutNodes.clear();
  context->nativeScheduleFanoutRanges.clear();
  context->nativeScheduleReadyNodes.clear();
  context->nativeScheduleDeadlines.clear();
  context->nativeScheduleDeadlineHeap.clear();
  context->nativeScheduleDeadlinePositions.clear();
  context->nativeScheduleStaticStateIndex.clear();
  context->nativeScheduleStaticStateFanoutEdges.clear();
  context->nativeScheduleSnapshotActors.clear();
  context->nativeScheduleSnapshotNBAs.clear();
  context->staticNBAAccumulators.clear();
  context->staticNBAAccumulatorsPending = false;
  context->staticNBASlowRoots.clear();
  context->staticNBASlowRootsPresent = false;
  context->staticNBARootHasFanout.clear();
  context->nativeScheduleGeneratedNBAStageCounts.clear();
  context->nativeScheduleGeneratedNBAOffsets.clear();
  context->nativeScheduleGeneratedBatchEligible = false;
  context->nativeScheduleHasGeneratedNBAAccumulators = false;
  context->nativeScheduleTransientDirtyRoots.clear();
  context->nativeSchedulePersistentDirtyRoots.clear();
  context->nativeScheduleTransientDirtyMask.clear();
  context->nativeSchedulePersistentDirtyMask.clear();
  context->nativeScheduleTransientDirtySummary.clear();
  context->nativeSchedulePersistentDirtySummary.clear();
  context->nativeScheduleDirtyRootsPresent = false;
  context->nativeScheduleAVX2 = false;
  context->nativeScheduleGuardedFanoutActive = false;
  context->nativeScheduleForcedSlot = UINT32_MAX;
  context->nativeScheduleSingleStep = false;
  context->nativeScheduleForcedExecuted = false;
  context->nativeScheduleControlOnly = false;
  context->nativeScheduleProcessFilterActive = false;
  context->nativeScheduleForcedProcessToken = 0;
  context->nativeScheduleStopAtCleanBoundary = false;
  context->nativeScheduleCleanBoundaryReached = false;
  context->nativeScheduleDesignTaskFilterActive = false;
  context->nativeScheduleForcedDesignTask = 0;
  context->nativePeriodicRuntimeDeadline = UINT64_MAX;
  context->nativePeriodicClockActorSlots.clear();
  context->nativeScheduleClockIngressPending = false;
  context->nativeScheduleDirectActorSlot = UINT32_MAX;
  context->nativeScheduleCheckpointActorSlot = UINT32_MAX;
  context->nativeScheduleCheckpointContinuation = 0;
  context->nativeScheduleCheckpointCallback = nullptr;
}

void obelisk_rt_aot_external_write_unlocked(obelisk_rt_context *context) {
  if (!context || !context->nativeSchedulePlan ||
      context->nativeScheduleDeoptimized)
    return;
  if (context->nativeSchedulePlan->specialization_fast)
    *context->nativeSchedulePlan->specialization_fast = 0;
  invalidateNativeTwoStatePromotionUnlocked(context);
  context->nativeScheduleExternalWritePending = true;
}

void obelisk_rt_aot_external_write_range_unlocked(obelisk_rt_context *context,
                                                  uint64_t bitOffset,
                                                  uint64_t bitWidth,
                                                  bool persistent) {
  if (!context || bitWidth == 0)
    return;
  __int128 dirtyEnd = static_cast<__int128>(bitOffset) + bitWidth;
  for (const auto &[id, state] : context->nativeStaticStates)
    if (static_cast<__int128>(state.bitOffset) < dirtyEnd &&
        static_cast<__int128>(bitOffset) <
            static_cast<__int128>(state.bitOffset) + state.bitWidth) {
      markNativeDirtyRootUnlocked(context, id, persistent);
    }
  obelisk_rt_aot_external_write_unlocked(context);
}

void obelisk_rt_aot_external_write_handle_unlocked(obelisk_rt_context *context,
                                                   uint64_t stableID,
                                                   uint64_t bitOffset,
                                                   uint64_t bitWidth,
                                                   bool persistent) {
  if (!context || bitWidth == 0)
    return;
  if (!context->nativeStaticStateRangesValid) {
    obelisk_rt_aot_external_write_range_unlocked(context, bitOffset, bitWidth,
                                                 persistent);
    return;
  }
  auto mark = [&](uint32_t id) {
    markNativeDirtyRootUnlocked(context, id, persistent);
  };

  uint32_t selectedID = 0;
  int64_t selectedOffset = 0;
  if (decodeNativeStatic(stableID, selectedID, selectedOffset))
    mark(selectedID);

  uint64_t bitEnd =
      bitWidth <= UINT64_MAX - bitOffset ? bitOffset + bitWidth : UINT64_MAX;
  const auto &ranges = context->nativeStaticStateRanges;
  auto upper =
      std::lower_bound(ranges.begin(), ranges.end(), bitEnd,
                       [](const NativeStaticStateRange &range, uint64_t end) {
                         return range.bitOffset < end;
                       });
  size_t index = static_cast<size_t>(upper - ranges.begin());
  while (index != 0) {
    const NativeStaticStateRange &range = ranges[--index];
    if (range.bitEnd > bitOffset && range.id != selectedID)
      mark(range.id);
    if (index == 0 || ranges[index - 1].prefixEnd <= bitOffset)
      break;
  }
  obelisk_rt_aot_external_write_unlocked(context);
}

bool obelisk_rt_aot_external_deposit_unlocked(obelisk_rt_context *context,
                                              uint64_t stableID,
                                              uint64_t bitOffset,
                                              uint64_t bitWidth) {
  if (!context || bitWidth == 0 || bitOffset > UINT64_MAX - bitWidth)
    return false;
  const obelisk_rt_native_schedule_plan *plan =
      context->nativeSchedulePlan;
  if (!plan || !context->execution || context->nativeScheduleDeoptimized ||
      context->nativeScheduleExternalWritePending ||
      (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE) == 0 ||
      plan->state_bit_count != context->execution->state_bit_count ||
      !context->nativeStaticStateRangesValid)
    return false;

  uint32_t selectedID = 0;
  int64_t selectedOffset = 0;
  bool hasSelected = decodeNativeStatic(stableID, selectedID, selectedOffset);
  if (!hasSelected)
    return false;
  uint64_t bitEnd = bitOffset + bitWidth;
  const auto &ranges = context->nativeStaticStateRanges;
  auto upper =
      std::lower_bound(ranges.begin(), ranges.end(), bitEnd,
                       [](const NativeStaticStateRange &range, uint64_t end) {
                         return range.bitOffset < end;
                       });
  auto visitRoots = [&](auto &&visitor) {
    if (hasSelected && !visitor(selectedID))
      return false;
    size_t index = static_cast<size_t>(upper - ranges.begin());
    while (index != 0) {
      const NativeStaticStateRange &range = ranges[--index];
      if (range.bitEnd > bitOffset &&
          (!hasSelected || range.id != selectedID) && !visitor(range.id))
        return false;
      if (index == 0 || ranges[index - 1].prefixEnd <= bitOffset)
        break;
    }
    return true;
  };
  bool synchronized =
      visitRoots([&](uint32_t id) {
        return !nativeStaticRootDirty(context, id);
      }) &&
      visitRoots([&](uint32_t id) {
        return reconcileNativeRootToPlanesUnlocked(context, plan, id);
      });
  if (!synchronized)
    return false;
  for (uint64_t bit = bitOffset; bit != bitEnd; ++bit)
    if (byteBit(plan->state_unknown, bit)) {
      invalidateNativeTwoStatePromotionUnlocked(context);
      break;
    }
  return true;
}

void obelisk_rt_aot_release_range_unlocked(obelisk_rt_context *context,
                                           uint64_t bitOffset,
                                           uint64_t bitWidth) {
  if (!context || bitWidth == 0)
    return;
  __int128 releasedEnd = static_cast<__int128>(bitOffset) + bitWidth;
  for (const auto &[id, state] : context->nativeStaticStates)
    if (static_cast<__int128>(state.bitOffset) < releasedEnd &&
        static_cast<__int128>(bitOffset) <
            static_cast<__int128>(state.bitOffset) + state.bitWidth) {
      if (nativeMaskIntersectsRange(context->forceMask, state.bitOffset,
                                    state.bitWidth) ||
          nativeMaskIntersectsRange(context->assignMask, state.bitOffset,
                                    state.bitWidth))
        continue;
      if (context->nativeSchedulePlan && !context->nativeScheduleDeoptimized &&
          !reconcileNativeRootToPlanesUnlocked(
              context, context->nativeSchedulePlan, id)) {
        if (context->schedulerStatus == OBELISK_RT_OK)
          context->schedulerStatus = OBELISK_RT_LAYOUT_MISMATCH;
        continue;
      }
      clearNativeDirtyRootUnlocked(context, id, true);
    }
  context->nativeScheduleDirtyRootsPresent =
      !context->nativeScheduleTransientDirtyRoots.empty() ||
      !context->nativeSchedulePersistentDirtyRoots.empty();
  refreshNativeStaticSpecializationFastUnlocked(context);
}

extern "C" uint32_t obelisk_rt_v1_static_specialization_guard(
    obelisk_rt_context *context, uint32_t actorSlot, uint32_t staticState,
    uint32_t flags) {
  if (!context || !context->nativeSchedulePlan ||
      (actorSlot != UINT32_MAX &&
       actorSlot >= context->nativeScheduleActors.size()) ||
      staticState == 0 || flags == 0 ||
      (flags & ~(OBELISK_RT_STATIC_ROOT_READ | OBELISK_RT_STATIC_ROOT_WRITE)) !=
          0)
    return 0;
  auto dirty = [&](const std::vector<uint64_t> &mask,
                   const std::unordered_set<uint32_t> &sparse) {
    uint64_t word = staticState / 64;
    return word < mask.size()
               ? (mask[word] & (uint64_t{1} << (staticState % 64))) != 0
               : sparse.find(staticState) != sparse.end();
  };
  auto record = [&](bool fast) {
    if (!context->signalDiagnosticsEnabled)
      return;
    if (fast)
      ++context->signalDiagnostics.aotStateFastPaths;
    else
      ++context->signalDiagnostics.aotStateSlowPaths;
  };
  if (context->nativeScheduleDeoptimized ||
      dirty(context->nativeScheduleTransientDirtyMask,
            context->nativeScheduleTransientDirtyRoots) ||
      dirty(context->nativeSchedulePersistentDirtyMask,
            context->nativeSchedulePersistentDirtyRoots)) {
    record(false);
    return 0;
  }
  if (actorSlot == UINT32_MAX) {
    bool allowed = findNativeStaticState(context, staticState) != nullptr;
    record(allowed);
    return allowed;
  }
  if (actorSlot >= context->nativeScheduleActorRootRanges.size()) {
    record(false);
    return 0;
  }
  auto [begin, end] = context->nativeScheduleActorRootRanges[actorSlot];
  for (uint64_t index = begin; index != end; ++index) {
    const obelisk_rt_static_actor_root &dependency =
        context->nativeScheduleActorRoots[index];
    if (dependency.static_state == staticState &&
        (dependency.flags & flags) == flags) {
      record(true);
      return 1;
    }
  }
  record(false);
  return 0;
}

extern "C" uint32_t
obelisk_rt_v1_static_nba_specialization_guard(obelisk_rt_context *context,
                                              uint32_t rootIndex) {
  if (!context || activeNativeAOTContext != context ||
      !context->nativeSchedulePlan || context->nativeScheduleDeoptimized ||
      rootIndex >= context->nativeScheduleNBARootCount ||
      rootIndex >= context->staticNBASlowRoots.size())
    return 0;
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  return context->staticNBASlowRoots[rootIndex] == 0 &&
                 !nativeStaticRootDirty(context, root.static_state)
             ? 1
             : 0;
}
