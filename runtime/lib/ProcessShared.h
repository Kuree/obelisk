//===- ProcessShared.h - Process/scheduler internal linkage ----*- C++ -*-===//
//
// Declarations shared between the process/scheduler translation units that
// were split out of Process.cpp (ProcessAOT.cpp, ProcessNBA.cpp,
// ProcessNativeState.cpp).  Everything here has internal-to-libobelisk_rt
// linkage; nothing is part of the public runtime ABI.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_RUNTIME_LIB_PROCESSSHARED_H
#define OBELISK_RUNTIME_LIB_PROCESSSHARED_H

#include "RuntimeInternal.h"

#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

constexpr uint64_t kNativeLogicalProcessTag =
    OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG;

extern std::mutex nativeScheduleRegistryMutex;
extern std::unordered_set<const void *> installedNativeScheduleStates;

// Marks calls made from an installed native AOT kernel. This selects AOT-only
// leaf operations, but does not waive the mutex contract of public runtime
// entry points: observers may temporarily release that mutex while callbacks
// run.
extern thread_local obelisk_rt_context *activeNativeAOTContext;
// A static AOT frame may retain the real context mutex across the generated
// kernel. Nested runtime entry points then inherit that exclusion instead of
// repeatedly acquiring the same recursive mutex.
extern thread_local obelisk_rt_context *lockedNativeAOTContext;

class NativeAOTMutexScope {
public:
  explicit NativeAOTMutexScope(obelisk_rt_context *context)
      : previous(lockedNativeAOTContext), lock(context->mutex) {
    lockedNativeAOTContext = context;
  }
  NativeAOTMutexScope(const NativeAOTMutexScope &) = delete;
  NativeAOTMutexScope &operator=(const NativeAOTMutexScope &) = delete;
  ~NativeAOTMutexScope() { lockedNativeAOTContext = previous; }

private:
  obelisk_rt_context *previous;
  std::unique_lock<std::recursive_mutex> lock;
};

class NativeAOTContextScope {
public:
  explicit NativeAOTContextScope(obelisk_rt_context *context)
      : previous(activeNativeAOTContext) {
    activeNativeAOTContext = context;
  }
  NativeAOTContextScope(const NativeAOTContextScope &) = delete;
  NativeAOTContextScope &operator=(const NativeAOTContextScope &) = delete;
  ~NativeAOTContextScope() { activeNativeAOTContext = previous; }

private:
  obelisk_rt_context *previous;
};

inline bool isStaticControlAOT(const obelisk_rt_context *context) {
  return activeNativeAOTContext == context && context->nativeSchedulePlan &&
         (context->nativeSchedulePlan->flags &
          OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL) != 0;
}

//===----------------------------------------------------------------------===//
// Process frame bookkeeping (Process.cpp)
//===----------------------------------------------------------------------===//

void unregisterManagedFrameRoots(obelisk_rt_process_instance_v1 *instance);
void releaseOwnedNativeStates(obelisk_rt_context *context,
                              obelisk_rt_process_instance_v1 *instance);

//===----------------------------------------------------------------------===//
// Native state planes and dirty-root tracking (ProcessNativeState.cpp)
//===----------------------------------------------------------------------===//

const NativeStaticState *findNativeStaticState(const obelisk_rt_context *context,
                                               uint32_t id);
bool byteBit(const uint8_t *bytes, uint64_t bit);
void setByteBit(uint8_t *bytes, uint64_t bit, bool value);
bool nativeMaskIntersectsRange(const std::vector<uint64_t> &mask,
                               uint64_t bitOffset, uint64_t bitWidth);
bool importNativeStatePlanesUnlocked(obelisk_rt_context *context,
                                     const uint8_t *value,
                                     const uint8_t *unknown,
                                     uint64_t bitCount);
bool exportNativeStatePlanesUnlocked(const obelisk_rt_context *context,
                                     uint8_t *value, uint8_t *unknown,
                                     uint64_t bitCount);
bool reconcileNativeRootToPlanesUnlocked(
    const obelisk_rt_context *context,
    const obelisk_rt_native_schedule_plan *plan, uint32_t id);
bool reconcileNativeDirtyRootsToPlanesUnlocked(
    const obelisk_rt_context *context,
    const obelisk_rt_native_schedule_plan *plan);
bool nativeStaticRootDirty(const obelisk_rt_context *context,
                           uint32_t staticState);
bool nativeStaticSpecializationEnvironmentClean(
    const obelisk_rt_context *context);
void markNativeDirtyRootUnlocked(obelisk_rt_context *context, uint32_t id,
                                 bool persistent);
void clearNativeDirtyRootUnlocked(obelisk_rt_context *context, uint32_t id,
                                  bool persistent);
void invalidateNativeStaticSpecializationFastUnlocked(
    obelisk_rt_context *context);
void invalidateNativeTwoStatePromotionUnlocked(obelisk_rt_context *context);
void refreshNativeStaticSpecializationFastUnlocked(
    obelisk_rt_context *context);
bool storeNativeScheduleStateUnlocked(obelisk_rt_context *context,
                                      uint64_t bitOffset, uint64_t bitWidth,
                                      uint64_t value, uint64_t unknown);

//===----------------------------------------------------------------------===//
// Scheduler queue maintenance (Process.cpp)
//===----------------------------------------------------------------------===//

bool nativeWaitReady(obelisk_rt_context &context,
                     const ScheduledProcess &process);
bool nativeProcessReady(obelisk_rt_context &context,
                        const ScheduledProcess &process,
                        bool directStaticSignalWait);
void indexScheduledProcessDelayUnlocked(obelisk_rt_context *context,
                                        const ScheduledProcess &process);
void rebuildNativeSchedulerIndexUnlocked(obelisk_rt_context *context);
uint32_t nativeAOTContinuationRank(const ScheduledProcess &scheduled,
                                   uint32_t continuation);
void updateNativeAOTContinuationRank(ScheduledProcess &scheduled,
                                     uint32_t continuation);
obelisk_rt_status
adoptScheduledSuspendUnlocked(obelisk_rt_context *context,
                              ScheduledProcess &scheduled,
                              const obelisk_rt_fragment_action_v1 &action);
obelisk_rt_status runScheduler(obelisk_rt_context *context);
obelisk_rt_status runPreponedHooks(obelisk_rt_context *context);
obelisk_rt_status runStaticAOTControlStep(obelisk_rt_context *context,
                                          bool allowTimeAdvance = true,
                                          bool allowRuntimeTasks = false);
bool hasSameDirectSignalWait(const ScheduledProcess &scheduled,
                             const obelisk_rt_wait_record_v1 *wait);
uint32_t nextDueNBABarrierRegionUnlocked(const obelisk_rt_context *context,
                                         bool includeGenerated = true);

//===----------------------------------------------------------------------===//
// Signal fanout and transitions (Process.cpp)
//===----------------------------------------------------------------------===//

bool canUseStaticAOTFanout(const obelisk_rt_context *context);
bool canUseIndexedExternalAOTFanout(const obelisk_rt_context *context);
bool nativeAOTTransientBoundaryClean(const obelisk_rt_context *context);
bool validNativeStatePlanesUnlocked(const obelisk_rt_context *context,
                                    const uint8_t *value,
                                    const uint8_t *unknown, uint64_t bitCount);
bool publishStaticAOTSignalTransitionUnlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t *outSequence, bool indexedExternalDeposit = false);
bool publishNativeSignalTransitionUnlocked(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    const uint8_t *newValue, const uint8_t *newUnknown);
void wakeMonitorProcessUnlocked(obelisk_rt_context *context,
                                uint64_t logicalToken);

//===----------------------------------------------------------------------===//
// AOT deadline heap and readiness (ProcessAOT.cpp)
//===----------------------------------------------------------------------===//

bool nativeAOTActorDirty(const obelisk_rt_context *context,
                         uint32_t actorSlot);
bool nativeAOTNeedsSpecializationHandoverUnlocked(
    const obelisk_rt_context *context, uint32_t actorSlot);
bool markNativeAOTActorReadyUnlocked(obelisk_rt_context *context,
                                     uint32_t actorSlot);
void clearNativeAOTNodeReadyUnlocked(obelisk_rt_context *context,
                                     uint32_t node);
void setNativeAOTDeadlineUnlocked(obelisk_rt_context *context,
                                  uint32_t actorSlot, uint64_t deadline);
void removeNativeAOTDeadlineUnlocked(obelisk_rt_context *context,
                                     uint32_t actorSlot);
bool markDueNativeAOTDeadlinesUnlocked(obelisk_rt_context *context);
uint32_t findNativeAOTNodeUnlocked(const obelisk_rt_context *context,
                                   uint32_t actorSlot, uint32_t continuation);
obelisk_rt_status
refreshNativeAOTReadyPhaseUnlocked(obelisk_rt_context *context);
obelisk_rt_status
initializeNativeAOTNodesUnlocked(obelisk_rt_context *context,
                                 const obelisk_rt_native_schedule_node *nodes,
                                 uint32_t nodeCount);

//===----------------------------------------------------------------------===//
// Non-blocking assignment staging and commit (ProcessNBA.cpp)
//===----------------------------------------------------------------------===//

bool hasGeneratedNBAStages(
    const obelisk_rt_generated_nba_accumulator_256 &generated);
void markStaticNBAAccumulatorPending(obelisk_rt_context *context,
                                     uint32_t rootIndex,
                                     StaticNBAAccumulator &accumulator);
void refreshStaticNBAAccumulatorsPending(obelisk_rt_context *context);
bool staticNBARootNeedsTransitions(const obelisk_rt_context *context,
                                   uint32_t rootIndex);
obelisk_rt_status
commitStaticNBAAccumulatorsUnlocked(obelisk_rt_context *context,
                                    uint32_t barrierRegion, bool &changed);
bool canCommitInlineNativeNBABarrierUnlocked(obelisk_rt_context *context,
                                             uint32_t barrierRegion);
obelisk_rt_status
commitInlineNativeNBABarrierUnlocked(obelisk_rt_context *context,
                                     uint32_t barrierRegion, bool &changed);
obelisk_rt_status
materializeGeneratedNBAAccumulatorUnlocked(obelisk_rt_context *context,
                                           uint32_t rootIndex,
                                           uint32_t execRegion);

#endif // OBELISK_RUNTIME_LIB_PROCESSSHARED_H
