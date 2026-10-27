//===- ProcessShared.h - Process/scheduler internal linkage ----*- C++ -*-===//
//
// Declarations shared between the process/scheduler translation units that
// were split out of Process.cpp (ProcessAOT.cpp, ProcessNBA.cpp,
// ProcessNativeState.cpp).  Everything here has internal-to-libtalos_rt
// linkage; nothing is part of the public runtime ABI.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_RUNTIME_LIB_PROCESSSHARED_H
#define TALOS_RUNTIME_LIB_PROCESSSHARED_H

#include "GeneratedNBAStorage.h"
#include "RuntimeInternal.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

constexpr uint64_t kNativeLogicalProcessTag =
    TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;

extern std::mutex nativeScheduleRegistryMutex;
extern std::unordered_set<const void *> installedNativeScheduleStates;

// Marks calls made from an installed native AOT kernel. This selects AOT-only
// leaf operations, but does not waive the mutex contract of public runtime
// entry points: observers may temporarily release that mutex while callbacks
// run.
extern thread_local talos_rt_context *activeNativeAOTContext;
// A static AOT frame may retain the real context mutex across the generated
// kernel. Nested runtime entry points then inherit that exclusion instead of
// repeatedly acquiring the same recursive mutex.
extern thread_local talos_rt_context *lockedNativeAOTContext;

class NativeAOTMutexScope {
public:
  explicit NativeAOTMutexScope(talos_rt_context *context)
      : previous(lockedNativeAOTContext), lock(context->mutex) {
    lockedNativeAOTContext = context;
  }
  NativeAOTMutexScope(const NativeAOTMutexScope &) = delete;
  NativeAOTMutexScope &operator=(const NativeAOTMutexScope &) = delete;
  ~NativeAOTMutexScope() { lockedNativeAOTContext = previous; }

private:
  talos_rt_context *previous;
  std::unique_lock<std::recursive_mutex> lock;
};

class NativeAOTContextScope {
public:
  explicit NativeAOTContextScope(talos_rt_context *context)
      : previous(activeNativeAOTContext) {
    activeNativeAOTContext = context;
  }
  NativeAOTContextScope(const NativeAOTContextScope &) = delete;
  NativeAOTContextScope &operator=(const NativeAOTContextScope &) = delete;
  ~NativeAOTContextScope() { activeNativeAOTContext = previous; }

private:
  talos_rt_context *previous;
};

inline bool isStaticControlAOT(const talos_rt_context *context) {
  return activeNativeAOTContext == context && context->nativeSchedulePlan &&
         (context->nativeSchedulePlan->flags &
          TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL) != 0;
}

//===----------------------------------------------------------------------===//
// Process frame bookkeeping (Process.cpp)
//===----------------------------------------------------------------------===//

void unregisterManagedFrameRoots(talos_rt_process_instance_v1 *instance);
void releaseOwnedNativeStates(talos_rt_context *context,
                              talos_rt_process_instance_v1 *instance);

//===----------------------------------------------------------------------===//
// Native state planes and dirty-root tracking (ProcessNativeState.cpp)
//===----------------------------------------------------------------------===//

const NativeStaticState *
findNativeStaticState(const talos_rt_context *context, uint32_t id);
// These leaf accesses also serve bytecode publication and NBA boundaries.
// Keep their bodies visible to callers without requiring whole-runtime LTO.
inline bool byteBit(const uint8_t *bytes, uint64_t bit) {
  return bytes && (bytes[bit / 8] & static_cast<uint8_t>(1u << (bit % 8))) != 0;
}

inline void setByteBit(uint8_t *bytes, uint64_t bit, bool value) {
  if (!bytes)
    return;
  uint8_t mask = static_cast<uint8_t>(1u << (bit % 8));
  if (value)
    bytes[bit / 8] |= mask;
  else
    bytes[bit / 8] &= static_cast<uint8_t>(~mask);
}
bool nativeMaskIntersectsRange(const std::vector<uint64_t> &mask,
                               uint64_t bitOffset, uint64_t bitWidth);
bool importNativeStatePlanesUnlocked(talos_rt_context *context,
                                     const uint8_t *value,
                                     const uint8_t *unknown, uint64_t bitCount);
bool importNativeRootInitializerPlanesUnlocked(talos_rt_context *context);
bool exportNativeStatePlanesUnlocked(const talos_rt_context *context,
                                     uint8_t *value, uint8_t *unknown,
                                     uint64_t bitCount);
bool reconcileNativeRootToPlanesUnlocked(
    const talos_rt_context *context,
    const talos_rt_native_schedule_plan *plan, uint32_t id);
bool reconcileNativeDirtyRootsToPlanesUnlocked(
    const talos_rt_context *context,
    const talos_rt_native_schedule_plan *plan);
bool nativeStaticRootDirty(const talos_rt_context *context,
                           uint32_t staticState);
bool nativeStaticSpecializationEnvironmentClean(
    const talos_rt_context *context);
bool nativePeriodicAOTEnvironmentClean(const talos_rt_context *context);
void markNativeDirtyRootUnlocked(talos_rt_context *context, uint32_t id,
                                 bool persistent);
void clearNativeDirtyRootUnlocked(talos_rt_context *context, uint32_t id,
                                  bool persistent);
void invalidateNativeStaticSpecializationFastUnlocked(
    talos_rt_context *context);
void invalidateNativeTwoStatePromotionUnlocked(talos_rt_context *context);
void invalidateNativeTwoStatePromotionRangeUnlocked(talos_rt_context *context,
                                                    uint64_t bitOffset,
                                                    uint64_t bitWidth);
void publishNativeKnownnessChangeUnlocked(
    const talos_rt_native_schedule_plan *plan, uint64_t bitOffset,
    uint64_t bitWidth, uint64_t oldUnknown, uint64_t newUnknown);
// The generated unknown summary: every runtime write into the generated
// unknown plane reports the roots it may have made differ from their
// patterns before generated code runs again. `Range` marks every root it
// overlaps; `Bits` marks only the roots whose pattern differs from `unknown`
// (bit 0 at `bitOffset`, at most 64 bits) in a `written` bit.
void markNativeUnknownRangeUnlocked(const talos_rt_context *context,
                                    uint64_t bitOffset, uint64_t bitWidth);
void markNativeUnknownBitsUnlocked(const talos_rt_context *context,
                                   uint64_t bitOffset, uint64_t bitWidth,
                                   uint64_t unknown,
                                   uint64_t written = UINT64_MAX);
void markNativeUnknownAllUnlocked(const talos_rt_context *context);
void verifyNativeUnknownSummaryUnlocked(const talos_rt_context *context);
void refreshNativeStaticSpecializationFastUnlocked(talos_rt_context *context);
bool storeNativeScheduleStateUnlocked(
    talos_rt_context *context, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t value, uint64_t unknown,
    std::optional<uint64_t> previousUnknown = std::nullopt);

//===----------------------------------------------------------------------===//
// Scheduler queue maintenance (Process.cpp)
//===----------------------------------------------------------------------===//

inline bool isReactiveSchedulerRegion(uint32_t region) {
  return region >= TALOS_RT_REGION_REACTIVE &&
         region <= TALOS_RT_REGION_RE_NBA;
}

inline bool schedulerRegionEligible(const talos_rt_context *context,
                                    uint32_t region) {
  return !context->schedulerDrainingReactive ||
         isReactiveSchedulerRegion(region);
}

inline uint32_t nativeNBABarrierLowerBound(const talos_rt_context *context) {
  // IEEE 1800-2023 4.4.2.4, 4.4.2.8: the compiler's runtime-calendar
  // certificate stages static roots only in NBA/Re-NBA. Keep the general
  // inventory contract for plans without that compiler proof.
  bool certified = context->nativeSchedulePlan &&
                   (context->nativeSchedulePlan->flags &
                    TALOS_RT_NATIVE_SCHEDULE_RUNTIME_CALENDAR_EVAL);
  if (context->schedulerDrainingReactive)
    return certified ? TALOS_RT_REGION_RE_NBA : TALOS_RT_REGION_REACTIVE;
  return certified ? TALOS_RT_REGION_NBA : TALOS_RT_REGION_ACTIVE;
}

inline void setSchedulerDrainingReactive(talos_rt_context *context,
                                         bool draining) {
  if (context->schedulerDrainingReactive == draining)
    return;
  context->schedulerDrainingReactive = draining;
  // Both native and bytecode ready caches depend on region eligibility.
  if (++context->schedulerSelectionGeneration == 0)
    context->schedulerSelectionGeneration = 1;
}

bool nativeProcessReady(talos_rt_context &context,
                        const ScheduledProcess &process);
inline const talos_rt_wait_record_v1 *
designTaskCurrentWait(const ScheduledDesignTask &task) {
  if (task.waitSize < sizeof(talos_rt_wait_record_v1) ||
      task.waitOffset > task.frame.size() ||
      task.waitSize > task.frame.size() - task.waitOffset)
    return nullptr;
  return reinterpret_cast<const talos_rt_wait_record_v1 *>(task.frame.data() +
                                                             task.waitOffset);
}
inline uint32_t designTaskOrderingRegion(const ScheduledDesignTask &task,
                                         bool signalResume) {
  if (signalResume && talos_rt_is_slot_final_clock_occurrence_wait(
                          designTaskCurrentWait(task)))
    return TALOS_RT_REGION_POSTPONED;
  return task.queuedRegion == TALOS_RT_REGION_POSTPONED
             ? TALOS_RT_REGION_POSTPONED + 1
             : task.queuedRegion;
}
void indexScheduledProcessDelayUnlocked(talos_rt_context *context,
                                        const ScheduledProcess &process);
std::optional<uint64_t>
nextScheduledProcessDelayUnlocked(talos_rt_context *context);
void rebuildNativeSchedulerIndexUnlocked(talos_rt_context *context);
uint32_t nativeAOTContinuationRank(const ScheduledProcess &scheduled,
                                   uint32_t continuation);
void updateNativeAOTContinuationRank(ScheduledProcess &scheduled,
                                     uint32_t continuation);
talos_rt_status
adoptScheduledSuspendUnlocked(talos_rt_context *context,
                              ScheduledProcess &scheduled,
                              const talos_rt_fragment_action_v1 &action);
struct SchedulerRunOptions {
  // Select installed compiled executors for plan-owned actors. Other actors
  // and design tasks still execute under the same region arbitration.
  bool nativePlan = false;
  // A generated boundary may retain future clock deadlines. This restricts
  // time advancement, not which runnable regions the shared loop drains.
  bool currentSlotOnly = false;
  // Return checkpoint-generated ingress to the plan's continuation trampoline.
  // Bootstrap drains must still settle that ingress before clock admission.
  bool returnGeneratedIngress = false;
  // A generated eval continuation stays on the stack. Drain Active/Inactive
  // work and return before the common NBA barrier instead of reentering it.
  bool stopBeforeNBA = false;
  // A generated transaction may hold its NBA captures locally. Include that
  // region in the same arbitration even when the canonical dirty set is empty.
  uint32_t localPendingNBARegion = UINT32_MAX;
  // Finish exactly one already-started generated NBA barrier. Its compiled
  // prefix has run, so only the remaining runtime queues may execute here.
  uint32_t completeNBABarrier = UINT32_MAX;
  bool generatedNBAChanged = false;
  // Only write-capable private continuations request this foreign-code cut.
  // Their canonical wrapper must export before an unscoped actor executes.
  // Use trailing padding: exceeding 16 bytes makes AArch64 pass the whole
  // options argument indirectly, taxing off/read calls as well.
  bool stopBeforeUnscopedActors = false;
};
static_assert(sizeof(SchedulerRunOptions) <= 16);
talos_rt_status runScheduler(talos_rt_context *context,
                               SchedulerRunOptions options = {});
talos_rt_status executeNativeAOTNodeUnlocked(talos_rt_context *context,
                                               uint32_t actorSlot);
talos_rt_status runPreponedHooks(talos_rt_context *context);
// Initialize the current slot once, regardless of which executor entered it.
// Reentry in the same slot preserves its sampled state and progress cursor.
talos_rt_status enterSchedulerTimeSlotUnlocked(talos_rt_context *context);
bool hasSameDirectSignalWait(const ScheduledProcess &scheduled,
                             const talos_rt_wait_record_v1 *wait);
uint32_t nextDueNBABarrierRegionUnlocked(const talos_rt_context *context,
                                         bool includeGenerated = true);

//===----------------------------------------------------------------------===//
// Signal fanout and transitions (Process.cpp)
//===----------------------------------------------------------------------===//

bool canUseStaticAOTFanout(const talos_rt_context *context);
bool canUseIndexedExternalAOTFanout(const talos_rt_context *context);
bool nativeAOTTransientBoundaryClean(const talos_rt_context *context);
bool validNativeStatePlanesUnlocked(const talos_rt_context *context,
                                    const uint8_t *value,
                                    const uint8_t *unknown, uint64_t bitCount);
bool publishStaticAOTSignalTransitionUnlocked(
    talos_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t *outSequence, bool indexedExternalDeposit = false);
// `establishesOverride` marks the publication that installs a force or assign
// procedural continuous assignment. Every other publication is filtered
// against the override masks; that one is the write the masks describe.
bool publishNativeSignalTransitionUnlocked(
    talos_rt_context *context, uint64_t bitOffset, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    const uint8_t *oldValue, const uint8_t *oldUnknown, const uint8_t *newValue,
    const uint8_t *newUnknown, bool establishesOverride = false);
void publishOverrideTransition(talos_rt_context *context, uint64_t bitOffset,
                               uint64_t bitWidth, const uint8_t *oldValue,
                               const uint8_t *oldUnknown,
                               const uint8_t *newValue,
                               const uint8_t *newUnknown);
void wakeMonitorProcessUnlocked(talos_rt_context *context,
                                uint64_t logicalToken);

//===----------------------------------------------------------------------===//
// AOT deadline heap and readiness (ProcessAOT.cpp)
//===----------------------------------------------------------------------===//

bool markNativeAOTActorReadyUnlocked(talos_rt_context *context,
                                     uint32_t actorSlot);
talos_rt_status
suspendNativeAOTTaskCallerUnlocked(talos_rt_context *context,
                                   ScheduledProcess &scheduled);
void clearNativeAOTNodeReadyUnlocked(talos_rt_context *context,
                                     uint32_t node);
void setNativeAOTDeadlineUnlocked(talos_rt_context *context,
                                  uint32_t actorSlot, uint64_t deadline);
void removeNativeAOTDeadlineUnlocked(talos_rt_context *context,
                                     uint32_t actorSlot);
bool markDueNativeAOTDeadlinesUnlocked(talos_rt_context *context);
uint32_t findNativeAOTNodeUnlocked(const talos_rt_context *context,
                                   uint32_t actorSlot, uint32_t continuation);
talos_rt_status
refreshNativeAOTReadyPhaseUnlocked(talos_rt_context *context);
talos_rt_status
initializeNativeAOTNodesUnlocked(talos_rt_context *context,
                                 const talos_rt_native_schedule_node *nodes,
                                 uint32_t nodeCount);
bool nativeClockOccurrencePrimaryReadsGeneratedState(
    const talos_rt_context *context,
    const std::unordered_set<uint32_t> &generatedWritableStates);

//===----------------------------------------------------------------------===//
// Non-blocking assignment staging and commit (ProcessNBA.cpp)
//===----------------------------------------------------------------------===//

// Shared native pending-update selection for every execution policy. With an
// installed dirty index, retained generated payloads are not pending work.
uint32_t
nextDueNativeNBABarrierRegionUnlocked(const talos_rt_context *context,
                                      bool includeGenerated);

bool hasGeneratedNBAStages(const GeneratedNBAStorage &generated);
bool hasGeneratedNBASlots(const talos_rt_static_nba_root &root);
void markStaticNBAAccumulatorPending(talos_rt_context *context,
                                     uint32_t rootIndex,
                                     StaticNBAAccumulator &accumulator);
void refreshStaticNBAAccumulatorsPending(talos_rt_context *context);
bool staticNBARootNeedsTransitions(const talos_rt_context *context,
                                   uint32_t rootIndex);
talos_rt_status
commitStaticNBAAccumulatorsUnlocked(talos_rt_context *context,
                                    uint32_t barrierRegion, bool &changed);
talos_rt_status
resolveClockingDriveConflictsUnlocked(talos_rt_context *context,
                                      uint32_t barrierRegion);
talos_rt_status materializeGeneratedNBAAccumulatorUnlocked(
    talos_rt_context *context, uint32_t rootIndex, uint32_t execRegion);

#endif // TALOS_RUNTIME_LIB_PROCESSSHARED_H
