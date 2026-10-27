//===- ProcessTest.cpp - Shared process instance ABI tests ---------------===//

#include "TestEnvironment.h"
#include "talos/Runtime/ClockKernelReadySet.h"
#include "talos/Runtime/Runtime.h"
#include "talos/Runtime/StableHandle.h"

#include "../lib/DesignBytecodeExecution.h"
#include "../lib/DesignBytecodeImage.h"
#include "../lib/DesignBytecodeNets.h"
#include "../lib/ProcessObservers.h"
#include "../lib/ProcessPacking.h"
#include "../lib/ProcessSchedulerScope.h"
#include "../lib/ProcessShared.h"
#include "../lib/ProcessSignals.h"
#include "../lib/RuntimeInternal.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

uint64_t appendHash(uint64_t hash, const void *data, size_t size) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  for (size_t index = 0; index != size; ++index) {
    hash ^= bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

uint64_t checksum(const talos_rt_frame_layout_v1 &layout) {
  uint64_t hash = UINT64_C(14695981039346656037);
  hash = appendHash(hash, &layout.version, sizeof(layout.version));
  hash = appendHash(hash, &layout.flags, sizeof(layout.flags));
  hash = appendHash(hash, &layout.frame_size, sizeof(layout.frame_size));
  hash =
      appendHash(hash, &layout.frame_alignment, sizeof(layout.frame_alignment));
  hash = appendHash(hash, &layout.field_count, sizeof(layout.field_count));
  hash = appendHash(hash, &layout.continuation_count,
                    sizeof(layout.continuation_count));
  for (uint32_t index = 0; index != layout.field_count; ++index)
    hash =
        appendHash(hash, &layout.fields[index], sizeof(layout.fields[index]));
  for (uint32_t index = 0; index != layout.continuation_count; ++index)
    hash = appendHash(hash, &layout.continuations[index],
                      sizeof(layout.continuations[index]));
  return hash;
}

void appendInstruction(std::vector<uint8_t> &code, uint8_t opcode, uint8_t type,
                       uint16_t destination, uint16_t source0, uint16_t source1,
                       uint64_t immediate) {
  size_t offset = code.size();
  code.resize(offset + TALOS_RT_BYTECODE_INSTRUCTION_SIZE);
  code[offset] = opcode;
  code[offset + 1] = type;
  auto write16 = [&](size_t field, uint16_t value) {
    code[offset + field] = static_cast<uint8_t>(value);
    code[offset + field + 1] = static_cast<uint8_t>(value >> 8);
  };
  write16(2, destination);
  write16(4, source0);
  write16(6, source1);
  for (unsigned byte = 0; byte != 8; ++byte)
    code[offset + 8 + byte] = static_cast<uint8_t>(immediate >> (byte * 8));
}

int nativeDestroyCount;
talos_rt_status nativeExecuteStatus;
bool emitInvalidNativeWait;
bool emitInvalidNativeTerminate;
bool emitExistingNativeWait;
bool emitInvalidResumeRegion;

std::vector<uint32_t> collapsedAliasObserverSamples;

talos_rt_status
collapsedAliasObserverEvaluator(talos_rt_context *context, const uint64_t *,
                                uint32_t captureCount, uint64_t *value,
                                uint64_t *unknown, uint32_t limbCount) {
  if (!context || captureCount != 0 || !value || !unknown || limbCount != 1)
    return TALOS_RT_INVALID_ARGUMENT;
  collapsedAliasObserverSamples.push_back(
      static_cast<uint32_t>(context->stateValue[0] & 3) |
      (static_cast<uint32_t>(context->stateUnknown[0] & 3) << 2));
  value[0] = context->stateValue[0] & 1;
  unknown[0] = context->stateUnknown[0] & 1;
  return TALOS_RT_OK;
}
talos_rt_status frameDuringExecute;
talos_rt_status destroyDuringExecute;
talos_rt_context *observedContext;

talos_rt_status nativeRequirements(uint64_t *size, uint64_t *alignment) {
  if (!size || !alignment)
    return TALOS_RT_INVALID_ARGUMENT;
  *size = 64;
  *alignment = 16;
  return TALOS_RT_OK;
}

talos_rt_status nativeExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  void *frame = nullptr;
  uint64_t frameSize = 0;
  frameDuringExecute =
      talos_rt_v1_process_instance_frame(instance, &frame, &frameSize);
  destroyDuringExecute = talos_rt_v1_process_instance_destroy(instance);
  observedContext = instance->context;
  instance->native_handle = instance;
  if (nativeExecuteStatus != TALOS_RT_OK)
    return nativeExecuteStatus;
  if (instance->continuation == 0) {
    auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(
        static_cast<uint8_t *>(instance->frame) + 8);
    if (!emitExistingNativeWait)
      *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_DELAY, 0, 0, 17, 0};
    uint32_t actionFlags = TALOS_RT_ACTION_FRAME_WAIT_RECORD;
    if (emitInvalidResumeRegion)
      actionFlags |=
          TALOS_RT_ACTION_RESUME_REGION_VALID |
          (TALOS_RT_REGION_NBA << TALOS_RT_ACTION_RESUME_REGION_SHIFT);
    *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                         wait->kind,
                         1,
                         actionFlags,
                         emitInvalidNativeWait ? 9u : 8u,
                         instance->frame_size - 8};
  } else {
    *instance->action = {TALOS_RT_FRAGMENT_TERMINATE,
                         TALOS_RT_SUSPEND_NONE,
                         0,
                         0,
                         emitInvalidNativeTerminate ? 1u : 0u,
                         0};
  }
  return TALOS_RT_OK;
}

void nativeDestroy(talos_rt_process_instance_v1 *instance) {
  ++nativeDestroyCount;
  instance->native_handle = nullptr;
}

struct Fixture {
  std::array<talos_rt_frame_field_v1, 2> fields{{
      {TALOS_RT_FRAME_CAPTURE, TALOS_RT_FRAME_FIELD_FLAGS_NONE, 0, 8, 8, 0},
      {TALOS_RT_FRAME_WAIT, TALOS_RT_FRAME_FIELD_FLAGS_NONE, 8, 32, 8, 0},
  }};
  std::array<uint32_t, 2> continuations{{0, 1}};
  talos_rt_frame_layout_v1 layout{};
  std::vector<uint8_t> code;
  std::array<talos_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  talos_rt_bytecode_v1 bytecode{};
  talos_rt_process_descriptor_v1 descriptor{};

  Fixture() {
    nativeExecuteStatus = TALOS_RT_OK;
    layout = {TALOS_RT_VERSION,
              0,
              40,
              8,
              fields.data(),
              static_cast<uint32_t>(fields.size()),
              static_cast<uint32_t>(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = checksum(layout);
    appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0, 0,
                      0, 8);
    appendInstruction(code, TALOS_RT_BC_SUSPEND, TALOS_RT_BC_TYPE_NONE, 0,
                      TALOS_RT_SUSPEND_DELAY, 0, 1);
    appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0, 0,
                      0, 8);
    appendInstruction(code, TALOS_RT_BC_SUSPEND, TALOS_RT_BC_TYPE_NONE, 0,
                      TALOS_RT_SUSPEND_DELAY, 0, 1);
    bytecode = {code.data(),
                code.size(),
                entries.data(),
                static_cast<uint32_t>(entries.size()),
                1,
                48,
                nullptr,
                nullptr,
                0,
                nullptr,
                0,
                0,
                nullptr,
                0};
    descriptor = {{TALOS_RT_DESCRIPTOR_PROCESS, 0, 9},
                  TALOS_RT_VERSION,
                  0,
                  TALOS_RT_TIER_MASK_NATIVE | TALOS_RT_TIER_MASK_BYTECODE,
                  0,
                  &layout,
                  nativeRequirements,
                  nativeExecute,
                  nativeDestroy,
                  &bytecode,
                  nullptr,
                  nullptr};
  }
};

uint32_t schedulerWaitKind;
uint32_t schedulerWaitEdge;
uint64_t schedulerWaitHandle;
uint32_t schedulerWaitWidth;
uint64_t schedulerWaitOffset;
uint64_t schedulerWaitDelay;
unsigned schedulerResumeCount;
unsigned schedulerSelfTriggerCount;
uint32_t schedulerSelfTriggerStaticState;
unsigned schedulerDestroyCount;
std::vector<uint64_t> schedulerOrder;
uint64_t cachedCohortPrimarySignal;
uint64_t cachedCohortSecondarySignal;
uint64_t cachedCohortSecondaryID;
uint64_t cachedCohortSpawnID;
uint64_t cachedCohortPublishID;
const talos_rt_process_descriptor_v1 *cachedCohortUrgentChild;
const talos_rt_process_descriptor_v1 *cachedCohortOrdinaryChild;
const talos_rt_process_descriptor_v1 *cachedCohortTaskCallee;
uint64_t cachedCohortTaskCallID;
bool cachedCohortTaskCalled;
uint64_t cachedCohortContinueID;
bool cachedCohortContinued;
bool cachedCohortContinuePublishes;
uint64_t cachedCohortFrontierID;
bool cachedCohortFrontierSuspended;
const uint8_t *cachedCohortNBAPlane;
uint8_t cachedCohortNBAExpected;
bool cachedCohortNBAVisible;
unsigned schedulerCheckpointCount;
unsigned generatedCheckpointCallbackCount;
talos_rt_status invalidGeneratedCheckpointStatus;
talos_rt_status validGeneratedCheckpointStatus;
unsigned schedulerPromotionInvalidationCount;
unsigned schedulerPromotionReadyCount;
bool schedulerPromotionReadyValue;

void schedulerInvalidatePromotion() { ++schedulerPromotionInvalidationCount; }

uint32_t schedulerPromotionReady() {
  ++schedulerPromotionReadyCount;
  return schedulerPromotionReadyValue ? 1u : 0u;
}

struct AOTTestState {
  using RunHook = talos_rt_status (*)(AOTTestState *, talos_rt_context *);

  std::array<talos_rt_process_instance_v1 *, 2> actors{};
  std::vector<talos_rt_native_schedule_node> testNodes;
  bool requestFallback = false;
  bool corruptSnapshot = false;
  uint32_t observedSpecializationFast = UINT32_MAX;
  uint32_t observedSpecializationAfterSlot = UINT32_MAX;
  RunHook runHook = nullptr;
  talos_rt_generated_nba_accumulator_256 *generatedNBA = nullptr;
  bool generatedNBAFirst = false;
  uint64_t generatedNBAValue = 0;
  uint64_t claimedNBAValue = 0;
  uint32_t runCalls = 0;
  uint8_t *authorityPlane = nullptr;
  uint64_t authorityHandle = UINT64_MAX;
  uint8_t ordinaryAOTLoad = 0;
  uint8_t nestedObserverLoad = 0;
  uint8_t canonicalObserverLoad = 0;
};

talos_rt_status aotBind(void *opaque, talos_rt_context *, uint32_t slot,
                          talos_rt_process_instance_v1 *instance) {
  auto *state = static_cast<AOTTestState *>(opaque);
  if (!state || slot >= state->actors.size())
    return TALOS_RT_INVALID_ARGUMENT;
  if (!instance) {
    state->actors[slot] = nullptr;
    return TALOS_RT_OK;
  }
  state->actors[slot] = instance;
  return TALOS_RT_OK;
}

talos_rt_status aotRun(void *opaque, talos_rt_context *context) {
  auto *state = static_cast<AOTTestState *>(opaque);
  if (!state)
    return TALOS_RT_INVALID_ARGUMENT;
  if (context->nativeSchedulePlan &&
      context->nativeSchedulePlan->specialization_fast)
    state->observedSpecializationFast =
        *context->nativeSchedulePlan->specialization_fast;
  if (state->requestFallback)
    return TALOS_RT_TIER_UNAVAILABLE;
  if (state->runHook)
    return state->runHook(state, context);
  return talos_rt_v1_scheduler_run(context);
}

talos_rt_status aotCommitOneNBARoot(void *, talos_rt_context *context,
                                      uint32_t barrierRegion,
                                      uint32_t *outChanged) {
  return talos_rt_v1_static_nba_commit_roots(context, 1, barrierRegion,
                                               outChanged);
}

talos_rt_status aotCommitAllNBARoots(void *, talos_rt_context *context,
                                       uint32_t barrierRegion,
                                       uint32_t *outChanged) {
  return talos_rt_v1_static_nba_commit_roots(
      context, context->nativeScheduleNBARootCount, barrierRegion, outChanged);
}

talos_rt_status runGuardedNBAOrdering(AOTTestState *state,
                                        talos_rt_context *context) {
  if (!state || !state->generatedNBA || !context ||
      !context->nativeSchedulePlan)
    return TALOS_RT_INVALID_ARGUMENT;
  auto stageGenerated = [&] {
    state->generatedNBA->value[0] = state->generatedNBAValue;
    state->generatedNBA->write_mask[0] = UINT32_MAX;
    state->generatedNBA->valid = 1;
    state->generatedNBA->exec_region = TALOS_RT_REGION_NBA;
  };
  if (state->generatedNBAFirst)
    stageGenerated();
  talos_rt_status status = talos_rt_v1_static_nba_claim(
      context, 0, context->nativeSchedulePlan->state_value, nullptr,
      context->nativeSchedulePlan->state_bit_count, 0, 32,
      state->claimedNBAValue, 0);
  if (status == TALOS_RT_OK && !state->generatedNBAFirst)
    stageGenerated();
  return status;
}

talos_rt_status runSpecializationFastRearm(AOTTestState *state,
                                             talos_rt_context *context) {
  if (!state || !context || !context->nativeSchedulePlan ||
      !context->nativeSchedulePlan->specialization_fast ||
      context->staticNBASlowRoots.empty())
    return TALOS_RT_INVALID_ARGUMENT;
  context->staticNBASlowRoots[0] = 1;
  context->staticNBASlowRootsPresent = true;
  *context->nativeSchedulePlan->specialization_fast = 0;
  talos_rt_status status = talos_rt_v1_scheduler_run(context);
  state->observedSpecializationAfterSlot =
      *context->nativeSchedulePlan->specialization_fast;
  return status;
}

talos_rt_status runCheckpointThenReenter(AOTTestState *state,
                                           talos_rt_context *) {
  if (!state)
    return TALOS_RT_INVALID_ARGUMENT;
  ++state->runCalls;
  return state->runCalls == 1 ? TALOS_RT_AOT_CHECKPOINT : TALOS_RT_OK;
}

talos_rt_status runTimedCheckpointThenReenter(AOTTestState *state,
                                                talos_rt_context *context) {
  if (!state || !context)
    return TALOS_RT_INVALID_ARGUMENT;
  ++state->runCalls;
  if (state->runCalls != 1)
    return TALOS_RT_OK;
  context->nativePeriodicRuntimeDeadline = 5;
  return TALOS_RT_AOT_TIMED_CHECKPOINT;
}

talos_rt_status runCountOK(AOTTestState *state, talos_rt_context *) {
  if (!state)
    return TALOS_RT_INVALID_ARGUMENT;
  ++state->runCalls;
  return TALOS_RT_OK;
}

talos_rt_status runObserverPlaneAuthority(AOTTestState *state,
                                            talos_rt_context *context) {
  if (!state || !context || !state->authorityPlane)
    return TALOS_RT_INVALID_ARGUMENT;
  auto load = [&](uint8_t &value) {
    return talos_rt_v1_native_state_load_plane(context, state->authorityPlane,
                                                 8, state->authorityHandle, 8,
                                                 0, 0, &value);
  };
  if (talos_rt_status status = load(state->ordinaryAOTLoad);
      status != TALOS_RT_OK)
    return status;
  ++context->observerDepth;
  talos_rt_status status = load(state->nestedObserverLoad);
  --context->observerDepth;
  if (status != TALOS_RT_OK)
    return status;
  bool previous = context->observerForcesCanonicalPlane;
  context->observerForcesCanonicalPlane = true;
  status = load(state->canonicalObserverLoad);
  context->observerForcesCanonicalPlane = previous;
  return status;
}

talos_rt_status runDirectSelfTransition(AOTTestState *state,
                                          talos_rt_context *context) {
  if (!state || !context)
    return TALOS_RT_INVALID_ARGUMENT;
  talos_rt_process_instance_v1 *instance = nullptr;
  talos_rt_status status =
      talos_rt_v1_scheduler_direct_fragment_enter(context, 0, 1, &instance);
  if (status != TALOS_RT_OK || instance != state->actors[0])
    return status != TALOS_RT_OK ? status : TALOS_RT_INVALID_LIFECYCLE;
  talos_rt_v1_scheduler_static_transition(context, 1, 0, 1, 0, 0, 1, 0);
  return talos_rt_v1_scheduler_direct_fragment_leave(context, 0);
}

talos_rt_status aotRunNodes(void *opaque, talos_rt_context *context) {
  if (!opaque)
    return TALOS_RT_INVALID_ARGUMENT;
  constexpr talos_rt_native_schedule_node nodes[] = {{0, 0, 0}, {0, 1, 0}};
  return talos_rt_v1_scheduler_run_aot_nodes(context, nodes,
                                               std::size(nodes));
}

talos_rt_status generatedCheckpointCallback(talos_rt_context *context) {
  if (!context)
    return TALOS_RT_INVALID_ARGUMENT;
  ++generatedCheckpointCallbackCount;
  if (generatedCheckpointCallbackCount == 1) {
    talos_rt_status status = talos_rt_v1_scheduler_queue_aot_checkpoint(
        context, 0, 1, generatedCheckpointCallback);
    return status == TALOS_RT_OK ? TALOS_RT_AOT_GENERATED_CHECKPOINT
                                   : status;
  }
  return TALOS_RT_OK;
}

talos_rt_status runGeneratedCheckpoint(AOTTestState *state,
                                         talos_rt_context *context) {
  if (!state || !context)
    return TALOS_RT_INVALID_ARGUMENT;
  ++state->runCalls;
  if (state->runCalls != 1)
    return TALOS_RT_OK;
  // Model run_until reaching a new slot without runtime slot entry. Callback
  // progress must survive the subsequent current-slot drain.
  context->schedulerTime = 17;
  context->schedulerSlotProgress = 0;
  invalidGeneratedCheckpointStatus =
      talos_rt_v1_scheduler_queue_aot_checkpoint(context, 0, 2,
                                                   generatedCheckpointCallback);
  validGeneratedCheckpointStatus = talos_rt_v1_scheduler_queue_aot_checkpoint(
      context, 0, 1, generatedCheckpointCallback);
  return validGeneratedCheckpointStatus == TALOS_RT_OK
             ? TALOS_RT_AOT_CHECKPOINT
             : validGeneratedCheckpointStatus;
}

talos_rt_status aotRunWaitNodes(void *opaque, talos_rt_context *context) {
  if (!opaque)
    return TALOS_RT_INVALID_ARGUMENT;
  constexpr talos_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
  };
  return talos_rt_v1_scheduler_run_aot_nodes(context, nodes,
                                               std::size(nodes));
}

talos_rt_status aotRunOneNode(void *opaque, talos_rt_context *context) {
  if (!opaque)
    return TALOS_RT_INVALID_ARGUMENT;
  constexpr talos_rt_native_schedule_node nodes[] = {{0, 0, 0}};
  return talos_rt_v1_scheduler_run_aot_nodes(context, nodes,
                                               std::size(nodes));
}

talos_rt_status runAOTNodes(AOTTestState *state,
                              talos_rt_context *context) {
  if (!state)
    return TALOS_RT_INVALID_ARGUMENT;
  ++state->runCalls;
  return aotRunNodes(state, context);
}

talos_rt_status aotRunOneNodeThenFallback(void *opaque,
                                            talos_rt_context *context) {
  talos_rt_status status = aotRunOneNode(opaque, context);
  return status == TALOS_RT_OK ? TALOS_RT_TIER_UNAVAILABLE : status;
}

talos_rt_status runGroupedStaticActivationNodes(AOTTestState *state,
                                                  talos_rt_context *context) {
  if (!state->testNodes.empty())
    return talos_rt_v1_scheduler_run_aot_nodes(
        context, state->testNodes.data(), state->testNodes.size());
  constexpr talos_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {1, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
      {1, 1, UINT32_MAX},
  };
  return talos_rt_v1_scheduler_run_aot_nodes(context, nodes,
                                               std::size(nodes));
}

talos_rt_status aotSnapshot(void *opaque, talos_rt_context *context,
                              talos_rt_aot_deopt_snapshot *snapshot) {
  auto *state = static_cast<AOTTestState *>(opaque);
  if (!state || !context || !snapshot)
    return TALOS_RT_INVALID_ARGUMENT;
  talos_rt_status status =
      talos_rt_v1_scheduler_snapshot_aot(context, snapshot);
  if (status == TALOS_RT_OK && state->corruptSnapshot)
    snapshot->size = 0;
  return status;
}

uint64_t *clockCoordinatorIngress = nullptr;
uint32_t clockCoordinatorWords = 0;
uint32_t clockCoordinatorCalls = 0;

talos_rt_status clockCoordinator(void *, talos_rt_context *context) {
  if (!context || !clockCoordinatorIngress)
    return TALOS_RT_INVALID_ARGUMENT;
  ++clockCoordinatorCalls;
  std::fill_n(clockCoordinatorIngress, clockCoordinatorWords, uint64_t{0});
  return TALOS_RT_OK;
}

talos_rt_status clockWaitCoordinator(void *opaque,
                                       talos_rt_context *context) {
  if (!context || !clockCoordinatorIngress || clockCoordinatorWords != 1)
    return TALOS_RT_INVALID_ARGUMENT;
  uint64_t ready = (clockCoordinatorIngress[0] & 1) != 0 ? uint64_t{2} : 0;
  clockCoordinatorIngress[0] = 0;
  ++clockCoordinatorCalls;
  talos_rt_v1_scheduler_activate_static_nodes(context, &ready, 1);
  if (context->schedulerStatus != TALOS_RT_OK)
    return context->schedulerStatus;
  return aotRunWaitNodes(opaque, context);
}

talos_rt_native_schedule_plan makeAOTPlan(AOTTestState &state,
                                            uint32_t actors = 2) {
  return {sizeof(talos_rt_native_schedule_plan),
          0,
          &state,
          sizeof(state),
          actors,
          0,
          nullptr,
          nullptr,
          0,
          aotBind,
          aotRun,
          aotSnapshot,
          nullptr,
          0,
          0,
          nullptr,
          0,
          nullptr,
          0,
          nullptr,
          0,
          nullptr,
          nullptr,
          nullptr,
          0,
          0,
          nullptr,
          0,
          0,
          nullptr,
          0,
          0,
          nullptr,
          0,
          0,
          0,
          0,
          0,
          0,
          nullptr,
          nullptr};
}

talos_rt_status schedulerRequirements(uint64_t *size, uint64_t *alignment) {
  if (!size || !alignment)
    return TALOS_RT_INVALID_ARGUMENT;
  *size = 0;
  *alignment = 1;
  return TALOS_RT_OK;
}

talos_rt_status schedulerExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  if (instance->descriptor->handle.id >= 100) {
    schedulerOrder.push_back(instance->descriptor->handle.id);
    *instance->action = {
        TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return TALOS_RT_OK;
  }
  if (instance->continuation != 0) {
    schedulerOrder.push_back(instance->descriptor->handle.id);
    ++schedulerResumeCount;
    *instance->action = {
        TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return TALOS_RT_OK;
  }
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(
      static_cast<uint8_t *>(instance->frame) + schedulerWaitOffset);
  auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  if (schedulerWaitKind == TALOS_RT_SUSPEND_DELAY) {
    *wait = {
        TALOS_RT_VERSION, schedulerWaitKind, 0, 0, schedulerWaitDelay, 0};
  } else {
    *wait = {TALOS_RT_VERSION, schedulerWaitKind, 0, 1, 0, 0};
    *entry = {schedulerWaitHandle, schedulerWaitEdge,
              schedulerWaitKind == TALOS_RT_SUSPEND_CHANGE ||
                      schedulerWaitKind == TALOS_RT_SUSPEND_EDGE
                  ? schedulerWaitWidth
                  : 0};
  }
  *instance->action = {
      TALOS_RT_FRAGMENT_SUSPEND,         schedulerWaitKind,   1,
      TALOS_RT_ACTION_FRAME_WAIT_RECORD, schedulerWaitOffset, 48};
  return TALOS_RT_OK;
}

talos_rt_status
cachedSignalCohortExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  uint64_t id = instance->descriptor->handle.id;
  if (instance->continuation != 0) {
    schedulerOrder.push_back(id);
    if (cachedCohortNBAPlane &&
        *cachedCohortNBAPlane != cachedCohortNBAExpected)
      cachedCohortNBAVisible = false;
    if (id == cachedCohortContinueID && !cachedCohortContinued) {
      cachedCohortContinued = true;
      if (cachedCohortContinuePublishes)
        talos_rt_v1_scheduler_signal(instance->context,
                                       cachedCohortSecondarySignal, 1,
                                       TALOS_RT_SIGNAL_CHANGE);
      *instance->action = {
          TALOS_RT_FRAGMENT_CONTINUE, TALOS_RT_SUSPEND_NONE, 1, 0, 0, 0};
      return TALOS_RT_OK;
    }
    if (id == cachedCohortFrontierID && !cachedCohortFrontierSuspended) {
      cachedCohortFrontierSuspended = true;
      auto *wait =
          reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
      auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
      *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_FRONTIER, 0, 1, 0, 0};
      *entry = {0, TALOS_RT_WAIT_EDGE_NONE, 0};
      *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                           TALOS_RT_SUSPEND_FRONTIER,
                           1,
                           TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                           0,
                           48};
      return TALOS_RT_OK;
    }
    if (id == cachedCohortTaskCallID && !cachedCohortTaskCalled) {
      if (!cachedCohortTaskCallee)
        return TALOS_RT_INVALID_LIFECYCLE;
      talos_rt_process_instance_v1 *callee = nullptr;
      talos_rt_status status = talos_rt_v1_process_instance_create(
          cachedCohortTaskCallee, &callee);
      if (status != TALOS_RT_OK)
        return status;
      cachedCohortTaskCalled = true;
      *instance->action = {
          TALOS_RT_FRAGMENT_TASK_CALL,
          TALOS_RT_SUSPEND_NONE,
          1,
          0,
          static_cast<uint64_t>(reinterpret_cast<uintptr_t>(callee)),
          0};
      return TALOS_RT_OK;
    }
    if (id == cachedCohortSpawnID) {
      if (!cachedCohortUrgentChild || !cachedCohortOrdinaryChild)
        return TALOS_RT_INVALID_LIFECYCLE;
      talos_rt_process_instance_v1 *ordinary = nullptr;
      talos_rt_status status = talos_rt_v1_process_instance_create(
          cachedCohortOrdinaryChild, &ordinary);
      if (status != TALOS_RT_OK)
        return status;
      status = talos_rt_v1_scheduler_add(instance->context, ordinary, 0);
      if (status != TALOS_RT_OK) {
        talos_rt_v1_process_instance_destroy(ordinary);
        return status;
      }
      talos_rt_process_instance_v1 *urgent = nullptr;
      status = talos_rt_v1_process_instance_create(cachedCohortUrgentChild,
                                                     &urgent);
      if (status != TALOS_RT_OK)
        return status;
      status = talos_rt_v1_scheduler_add(instance->context, urgent,
                                           TALOS_RT_SCHEDULE_STARTUP);
      if (status != TALOS_RT_OK) {
        talos_rt_v1_process_instance_destroy(urgent);
        return status;
      }
    }
    if (id == cachedCohortPublishID)
      talos_rt_v1_scheduler_signal(instance->context,
                                     cachedCohortSecondarySignal, 1,
                                     TALOS_RT_SIGNAL_CHANGE);
    *instance->action = {
        TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return TALOS_RT_OK;
  }

  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  *entry = {id == cachedCohortSecondaryID ? cachedCohortSecondarySignal
                                          : cachedCohortPrimarySignal,
            TALOS_RT_WAIT_EDGE_CHANGE, 1};
  *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                       TALOS_RT_SUSPEND_CHANGE,
                       1,
                       TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       48};
  return TALOS_RT_OK;
}

constexpr uint64_t postObservedSignal = 730;
bool postObservedDelayed;

// Process 1 and 2 wait for a post-Observed resumption, process 3 resumes in
// Observed like a clocking-input sample, and process 4 is a Reactive-home
// post-Observed waiter. After its resumption, process 1 takes a `#0`.
talos_rt_status
postObservedExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  uint64_t id = instance->descriptor->handle.id;
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
  if (instance->continuation != 0) {
    if (id == 1 && postObservedDelayed) {
      schedulerOrder.push_back(11);
    } else {
      schedulerOrder.push_back(id);
      if (id == 1) {
        postObservedDelayed = true;
        *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_DELAY, 0, 0, 0, 0};
        *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                             TALOS_RT_SUSPEND_DELAY,
                             1,
                             TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                             0,
                             48};
        return TALOS_RT_OK;
      }
    }
    *instance->action = {
        TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return TALOS_RT_OK;
  }
  auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  *entry = {postObservedSignal, TALOS_RT_WAIT_EDGE_CHANGE, 1};
  uint32_t resume =
      id == 3 ? TALOS_RT_ACTION_RESUME_REGION(TALOS_RT_REGION_OBSERVED)
              : TALOS_RT_ACTION_RESUME_POST_OBSERVED;
  *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                       TALOS_RT_SUSPEND_CHANGE,
                       1,
                       TALOS_RT_ACTION_FRAME_WAIT_RECORD | resume,
                       0,
                       48};
  return TALOS_RT_OK;
}

constexpr uint64_t incrementalCohortSize = 256;
constexpr uint64_t incrementalCohortSignal = 8000;
bool incrementalCohortDisturbRank = false;
const talos_rt_process_descriptor_v1 *incrementalCohortReentry = nullptr;

talos_rt_status
incrementalCohortExecute(talos_rt_process_instance_v1 *instance) {
  instance->native_handle = instance;
  uint64_t id = instance->descriptor->handle.id;
  if (instance->continuation != 0) {
    schedulerOrder.push_back(id);
    if (id <= incrementalCohortSize)
      talos_rt_v1_scheduler_signal(instance->context,
                                     incrementalCohortSignal + id, 1,
                                     TALOS_RT_SIGNAL_CHANGE);
    if (id == 1 && incrementalCohortDisturbRank) {
      // An unrelated ordering change between two direct publications must
      // invalidate the entire cached ordering, even with a pending journal.
      for (auto &process : instance->context->scheduledProcesses)
        if (process.instance->descriptor->handle.id == incrementalCohortSize)
          process.scheduleRank = 0;
      ++instance->context->schedulerSelectionGeneration;
    }
    if (id == 1 && incrementalCohortReentry) {
      talos_rt_process_instance_v1 *callee = nullptr;
      auto status = talos_rt_v1_process_instance_create(
          incrementalCohortReentry, &callee);
      if (status != TALOS_RT_OK)
        return status;
      status = talos_rt_v1_dpi_export_task_run(instance->context, callee);
      if (status != TALOS_RT_OK)
        return status;
    }
  }
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  *entry = {incrementalCohortSignal +
                (id <= incrementalCohortSize ? 0 : id - incrementalCohortSize),
            TALOS_RT_WAIT_EDGE_CHANGE, 1};
  *instance->action = {TALOS_RT_FRAGMENT_SUSPEND, TALOS_RT_SUSPEND_CHANGE,
                       1, TALOS_RT_ACTION_FRAME_WAIT_RECORD, 0, 48};
  return TALOS_RT_OK;
}

talos_rt_status
schedulerSelfTriggerExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !instance->context)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  if (instance->continuation != 0) {
    ++schedulerSelfTriggerCount;
    if (schedulerSelfTriggerCount == 3) {
      *instance->action = {
          TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
      return TALOS_RT_OK;
    }
    if (schedulerSelfTriggerStaticState != 0) {
      uint64_t oldValue = schedulerSelfTriggerCount & 1;
      talos_rt_v1_scheduler_static_transition(
          instance->context, schedulerSelfTriggerStaticState, 0, 1, oldValue, 0,
          oldValue ^ 1, 0);
    } else {
      talos_rt_v1_scheduler_signal(instance->context, schedulerWaitHandle,
                                     schedulerWaitWidth,
                                     TALOS_RT_SIGNAL_CHANGE);
    }
  }
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  *entry = {schedulerWaitHandle, TALOS_RT_WAIT_EDGE_CHANGE,
            schedulerWaitWidth};
  *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                       TALOS_RT_SUSPEND_CHANGE,
                       1,
                       TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       48};
  return TALOS_RT_OK;
}

unsigned schedulerWaitWakeCount;

// Count resumptions and wait again on schedulerWaitHandle.
talos_rt_status
schedulerWaitCountExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !instance->context)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  if (instance->continuation != 0)
    ++schedulerWaitWakeCount;
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  *entry = {schedulerWaitHandle, TALOS_RT_WAIT_EDGE_CHANGE,
            schedulerWaitWidth};
  *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                       TALOS_RT_SUSPEND_CHANGE,
                       1,
                       TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       48};
  return TALOS_RT_OK;
}

talos_rt_status
schedulerCheckpointThenNativeExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  if (instance->continuation == 0) {
    instance->native_handle = instance;
    ++schedulerCheckpointCount;
    return TALOS_RT_AOT_CHECKPOINT;
  }
  return schedulerExecute(instance);
}

talos_rt_status
groupedStaticActivationExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  uint64_t id = instance->descriptor->handle.id;
  if (instance->continuation != 0) {
    schedulerOrder.push_back(id);
    if (id == 90) {
      std::vector<uint64_t> nodes(
          instance->context->nativeScheduleReadyNodes.wordCount());
      const auto &inventory = instance->context->nativeScheduleNodes;
      for (uint32_t node = 0; node < inventory.size(); ++node)
        if (inventory[node].actor_slot == 1 &&
            inventory[node].continuation == 1)
          nodes[node / 64] |= uint64_t{1} << (node % 64);
      // The second publication must leave one ready bit and one resume.
      talos_rt_v1_scheduler_activate_static_nodes(instance->context,
                                                    nodes.data(), nodes.size());
      talos_rt_v1_scheduler_activate_static_nodes(instance->context,
                                                    nodes.data(), nodes.size());
    } else {
      ++schedulerResumeCount;
    }
    *instance->action = {
        TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return TALOS_RT_OK;
  }

  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
  if (id == 90) {
    *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_DELAY, 0, 0, 1, 0};
  } else {
    auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
    *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
    *entry = {1, TALOS_RT_WAIT_EDGE_CHANGE, 1};
  }
  *instance->action = {
      TALOS_RT_FRAGMENT_SUSPEND,
      static_cast<talos_rt_suspend_kind>(
          id == 90 ? TALOS_RT_SUSPEND_DELAY : TALOS_RT_SUSPEND_CHANGE),
      1,
      TALOS_RT_ACTION_FRAME_WAIT_RECORD,
      0,
      48};
  return TALOS_RT_OK;
}

void schedulerDestroy(talos_rt_process_instance_v1 *instance) {
  ++schedulerDestroyCount;
  instance->native_handle = nullptr;
}

unsigned finishDestroyCount;
const talos_rt_process_descriptor_v1 *taskCalleeDescriptor;
const talos_rt_process_descriptor_v1 *nestedTaskCalleeDescriptor;
unsigned taskCallerExecutions;
unsigned taskCalleeExecutions;

talos_rt_status finishExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  talos_rt_status status =
      talos_rt_v1_scheduler_finish(instance->context, 0);
  if (status != TALOS_RT_OK)
    return status;
  *instance->action = {
      TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return TALOS_RT_OK;
}

void finishDestroy(talos_rt_process_instance_v1 *instance) {
  ++finishDestroyCount;
  instance->native_handle = nullptr;
}

talos_rt_status taskCallerExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !taskCalleeDescriptor)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  ++taskCallerExecutions;
  if (instance->continuation != 0) {
    *instance->action = {
        TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return TALOS_RT_OK;
  }
  talos_rt_process_instance_v1 *callee = nullptr;
  talos_rt_status status =
      talos_rt_v1_process_instance_create(taskCalleeDescriptor, &callee);
  if (status != TALOS_RT_OK)
    return status;
  *instance->action = {
      TALOS_RT_FRAGMENT_TASK_CALL,
      TALOS_RT_SUSPEND_NONE,
      1,
      0,
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(callee)),
      0};
  return TALOS_RT_OK;
}

talos_rt_status taskCalleeExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  ++taskCalleeExecutions;
  *instance->action = {
      TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return TALOS_RT_OK;
}

uint64_t processAutomaticHandle;
uint64_t retainedAutomaticHandle;
uint64_t nbaAutomaticHandle;
uint8_t nbaDummyPlane;

talos_rt_status
automaticStateExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  uint8_t initial = 0x5a;
  talos_rt_status status = talos_rt_v1_native_state_alloc(
      instance->context, 8, &initial, nullptr, &processAutomaticHandle);
  if (status != TALOS_RT_OK)
    return status;
  *instance->action = {
      TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return TALOS_RT_OK;
}

talos_rt_status
retainedAutomaticStateExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  uint8_t initial = 0xa5;
  talos_rt_status status = talos_rt_v1_native_state_alloc(
      instance->context, 8, &initial, nullptr, &retainedAutomaticHandle);
  if (status != TALOS_RT_OK)
    return status;
  status = talos_rt_v1_native_state_retain(instance->context,
                                             retainedAutomaticHandle);
  if (status != TALOS_RT_OK)
    return status;
  status = talos_rt_v1_native_state_release(instance->context,
                                              retainedAutomaticHandle, 1);
  if (status != TALOS_RT_OK)
    return status;
  *instance->action = {
      TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return TALOS_RT_OK;
}

talos_rt_status
automaticNBAExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  uint8_t initial = 0;
  talos_rt_status status = talos_rt_v1_native_state_alloc(
      instance->context, 8, &initial, nullptr, &nbaAutomaticHandle);
  if (status != TALOS_RT_OK)
    return status;
  uint8_t replacement = 0xa5;
  status = talos_rt_v1_scheduler_nba(instance->context, &nbaDummyPlane,
                                       nullptr, 8, nbaAutomaticHandle, 8, 3,
                                       &replacement, nullptr);
  if (status != TALOS_RT_OK)
    return status;
  status = talos_rt_v1_native_state_release(instance->context,
                                              nbaAutomaticHandle, 1);
  if (status != TALOS_RT_OK)
    return status;
  *instance->action = {
      TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return TALOS_RT_OK;
}

struct SchedulerFixture {
  std::array<talos_rt_frame_field_v1, 2> fields{{
      {TALOS_RT_FRAME_WAIT, TALOS_RT_FRAME_FIELD_FLAGS_NONE, 0, 48, 8, 0},
      {TALOS_RT_FRAME_WAIT, TALOS_RT_FRAME_FIELD_FLAGS_NONE, 48, 48, 8, 0},
  }};
  std::array<uint32_t, 2> continuations{{0, 1}};
  talos_rt_frame_layout_v1 layout{};
  talos_rt_process_descriptor_v1 descriptor{};

  explicit SchedulerFixture(uint64_t id) {
    schedulerWaitOffset = 0;
    schedulerWaitDelay = 17;
    layout = {TALOS_RT_VERSION,
              0,
              96,
              8,
              fields.data(),
              static_cast<uint32_t>(fields.size()),
              static_cast<uint32_t>(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = checksum(layout);
    descriptor = {{TALOS_RT_DESCRIPTOR_PROCESS, 0, id},
                  TALOS_RT_VERSION,
                  0,
                  TALOS_RT_TIER_MASK_NATIVE,
                  0,
                  &layout,
                  schedulerRequirements,
                  schedulerExecute,
                  schedulerDestroy,
                  nullptr,
                  nullptr,
                  nullptr};
  }
};

bool clockOccurrenceLifecycleChangesWait;
unsigned clockOccurrenceLifecycleResumes;

talos_rt_status
clockOccurrenceLifecycleExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !instance->frame)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
  auto *entries = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  if (instance->continuation == 0 ||
      (instance->continuation == 1 && !clockOccurrenceLifecycleChangesWait)) {
    if (instance->continuation != 0)
      ++clockOccurrenceLifecycleResumes;
    *wait = {TALOS_RT_VERSION,
             TALOS_RT_SUSPEND_EDGE,
             TALOS_RT_WAIT_CLOCK_OCCURRENCE,
             2,
             91,
             0};
    entries[0] = {16, TALOS_RT_WAIT_EDGE_POSEDGE, 1};
    entries[1] = {32, TALOS_RT_WAIT_EDGE_POSEDGE, 1};
    *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                         TALOS_RT_SUSPEND_EDGE,
                         instance->continuation + 1,
                         TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                         0,
                         sizeof(talos_rt_wait_record_v1) +
                             2 * sizeof(talos_rt_wait_entry_v1)};
    return TALOS_RT_OK;
  }
  ++clockOccurrenceLifecycleResumes;
  if (instance->continuation == 1 && clockOccurrenceLifecycleChangesWait) {
    *wait = {TALOS_RT_VERSION,
             TALOS_RT_SUSPEND_EDGE,
             TALOS_RT_WAIT_FLAGS_NONE,
             1,
             0,
             0};
    entries[0] = {48, TALOS_RT_WAIT_EDGE_POSEDGE, 1};
    *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                         TALOS_RT_SUSPEND_EDGE,
                         2,
                         TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                         0,
                         64};
    return TALOS_RT_OK;
  }
  *instance->action = {
      TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return TALOS_RT_OK;
}

struct ClockOccurrenceLifecycleFixture {
  std::array<talos_rt_frame_field_v1, 1> fields{{
      {TALOS_RT_FRAME_WAIT, TALOS_RT_FRAME_FIELD_FLAGS_NONE, 0, 64, 8, 0},
  }};
  std::array<uint32_t, 3> continuations{{0, 1, 2}};
  talos_rt_frame_layout_v1 layout{};
  talos_rt_process_descriptor_v1 descriptor{};

  explicit ClockOccurrenceLifecycleFixture(uint64_t id) {
    layout = {TALOS_RT_VERSION,
              0,
              64,
              8,
              fields.data(),
              static_cast<uint32_t>(fields.size()),
              static_cast<uint32_t>(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = checksum(layout);
    descriptor = {{TALOS_RT_DESCRIPTOR_PROCESS, 0, id},
                  TALOS_RT_VERSION,
                  0,
                  TALOS_RT_TIER_MASK_NATIVE,
                  0,
                  &layout,
                  schedulerRequirements,
                  clockOccurrenceLifecycleExecute,
                  schedulerDestroy,
                  nullptr,
                  nullptr,
                  nullptr};
  }
};

constexpr uint64_t eventOnlyComputedObserverID = 0x7e01;
constexpr uint64_t eventOnlyComputedDependencyID = 0x7e02;
unsigned eventOnlyComputedResumeCount;

struct EventOnlyComputedWait {
  talos_rt_computed_wait_record_v1 wait{};
  talos_rt_computed_observer_v1 observer{};
  talos_rt_computed_dependency_v1 dependency{};
  talos_rt_computed_clause_v1 clause{};
  uint64_t previousValue = 0;
  uint64_t previousUnknown = 0;
};

talos_rt_status eventOnlyComputedEvaluator(talos_rt_context *context,
                                             const uint64_t *,
                                             uint32_t captureCount,
                                             uint64_t *value, uint64_t *unknown,
                                             uint32_t limbCount) {
  if (!context || captureCount != 0 || !value || !unknown || limbCount != 1)
    return TALOS_RT_INVALID_ARGUMENT;
  value[0] = 1;
  unknown[0] = 0;
  return TALOS_RT_OK;
}

talos_rt_status
eventOnlyComputedExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !instance->frame ||
      instance->frame_size < sizeof(EventOnlyComputedWait))
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  if (instance->continuation != 0) {
    ++eventOnlyComputedResumeCount;
    *instance->action = {
        TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return TALOS_RT_OK;
  }
  auto &record = *static_cast<EventOnlyComputedWait *>(instance->frame);
  record = {};
  record.wait = {TALOS_RT_VERSION,
                 TALOS_RT_SUSPEND_OBSERVER,
                 TALOS_RT_COMPUTED_WAIT_INTERLEAVED,
                 1,
                 1,
                 0,
                 1,
                 1,
                 offsetof(EventOnlyComputedWait, observer),
                 offsetof(EventOnlyComputedWait, dependency),
                 offsetof(EventOnlyComputedWait, dependency),
                 offsetof(EventOnlyComputedWait, clause),
                 offsetof(EventOnlyComputedWait, previousValue),
                 0,
                 sizeof(EventOnlyComputedWait),
                 0};
  record.observer = {
      eventOnlyComputedObserverID,
      0,
      0,
      0,
      1,
      static_cast<uint32_t>(offsetof(EventOnlyComputedWait, previousValue)),
      0};
  // IEEE 1800-2017 Clause 31.7 permits timing-check expressions whose only
  // live dependency is a named event; such a waiter has no signal bucket.
  record.dependency = {eventOnlyComputedDependencyID,
                       TALOS_RT_OBSERVER_DEPENDENCY_EVENT, 1};
  record.clause = {0, TALOS_RT_OBSERVER_CONDITION_NONE,
                   TALOS_RT_WAIT_EDGE_POSEDGE, 0};
  *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                       TALOS_RT_SUSPEND_OBSERVER,
                       1,
                       TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       sizeof(EventOnlyComputedWait)};
  return TALOS_RT_OK;
}

struct EventOnlyComputedFixture {
  std::array<talos_rt_frame_field_v1, 1> fields{{
      {TALOS_RT_FRAME_WAIT, TALOS_RT_FRAME_FIELD_FLAGS_NONE, 0,
       sizeof(EventOnlyComputedWait), alignof(EventOnlyComputedWait), 0},
  }};
  std::array<uint32_t, 2> continuations{{0, 1}};
  talos_rt_frame_layout_v1 layout{};
  talos_rt_process_descriptor_v1 descriptor{};

  explicit EventOnlyComputedFixture(uint64_t id) {
    layout = {TALOS_RT_VERSION,
              0,
              sizeof(EventOnlyComputedWait),
              alignof(EventOnlyComputedWait),
              fields.data(),
              static_cast<uint32_t>(fields.size()),
              static_cast<uint32_t>(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = checksum(layout);
    descriptor = {{TALOS_RT_DESCRIPTOR_PROCESS, 0, id},
                  TALOS_RT_VERSION,
                  0,
                  TALOS_RT_TIER_MASK_NATIVE,
                  0,
                  &layout,
                  schedulerRequirements,
                  eventOnlyComputedExecute,
                  schedulerDestroy,
                  nullptr,
                  nullptr,
                  nullptr};
  }
};

talos_rt_process_instance_v1 *
makeClockOccurrenceLifecycleInstance(ClockOccurrenceLifecycleFixture &fixture) {
  talos_rt_process_instance_v1 *instance = nullptr;
  EXPECT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  return instance;
}

talos_rt_process_instance_v1 *
makeSchedulerInstance(SchedulerFixture &fixture) {
  talos_rt_process_instance_v1 *instance = nullptr;
  EXPECT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  return instance;
}

TEST(RuntimeInternals, SharedPackedByteAccessMatchesBitwiseReference) {
  // Reflection and the scheduler must use the same helper definition. Cover
  // its aligned 8/16/32/64-bit fast paths, unaligned nine-byte reads, and
  // preservation of neighboring bits on stores.
  std::mt19937_64 random(0x6279746573);
  for (uint64_t offset = 0; offset != 16; ++offset)
    for (uint64_t width = 1; width <= 64; ++width) {
      SCOPED_TRACE(::testing::Message()
                   << "offset=" << offset << " width=" << width);
      std::vector<uint8_t> bytes((offset + width + 7) / 8);
      for (uint8_t &byte : bytes)
        byte = static_cast<uint8_t>(random());
      uint64_t expected = 0;
      for (uint64_t bit = 0; bit != width; ++bit)
        expected |=
            uint64_t{(bytes[(offset + bit) / 8] >> ((offset + bit) % 8)) & 1u}
            << bit;
      EXPECT_EQ(loadPackedBytes(bytes.data(), offset, width), expected);
      std::vector<uint8_t> reference = bytes;
      uint64_t next = random();
      for (uint64_t bit = 0; bit != width; ++bit) {
        uint8_t mask = uint8_t{1} << ((offset + bit) % 8);
        uint8_t &byte = reference[(offset + bit) / 8];
        byte = static_cast<uint8_t>((byte & ~mask) |
                                    (((next >> bit) & 1u) ? mask : 0));
      }
      storePackedBytes(bytes.data(), offset, width, next);
      EXPECT_EQ(bytes, reference);
      EXPECT_EQ(loadPackedBytes(bytes.data(), offset, width),
                next & packedWidthMask(width));
    }
}

TEST(RuntimeInternals, PackedRangeExistenceMatchesIndividualBits) {
  EXPECT_FALSE(anyPackedBits(nullptr, 0, 0));
  EXPECT_FALSE(anyPackedBits(nullptr, 7, 100));
  for (uint64_t offset = 0; offset != 16; ++offset)
    for (uint64_t width = 0; width <= 130; ++width) {
      SCOPED_TRACE(::testing::Message()
                   << "offset=" << offset << " width=" << width);
      std::vector<uint8_t> bytes((offset + width + 7) / 8, 0);
      EXPECT_FALSE(anyPackedBits(bytes.data(), offset, width));
      for (uint64_t bit = 0; bit < bytes.size() * 8; ++bit) {
        bytes[bit / 8] = uint8_t{1} << (bit % 8);
        EXPECT_EQ(anyPackedBits(bytes.data(), offset, width),
                  bit >= offset && bit - offset < width);
        bytes[bit / 8] = 0;
      }
    }
}

TEST(RuntimeInternals, PackedPublicationPreservesUnchangedAndForcedBits) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 256;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 53, 128),
            TALOS_RT_OK);
  uint64_t handle = talos_rt_v1_native_state_static_handle(1);
  for (uint64_t width : {1, 7, 8, 31, 32, 63, 64, 65, 97, 127, 128})
    for (bool forced : {false, true})
      for (bool establishesOverride : {false, true}) {
        context->stateValue.assign(4, UINT64_C(0x9696969696969696));
        context->stateUnknown.assign(4, UINT64_C(0x6969696969696969));
        context->forceMask.assign(4, forced ? UINT64_C(0x2222222222222222) : 0);
        auto expectedValue = context->stateValue;
        auto expectedUnknown = context->stateUnknown;
        std::array<uint8_t, 16> changed{}, value{}, unknown{};
        changed.fill(0x5a);
        value.fill(0xc3);
        unknown.fill(0x3c);
        for (uint64_t bit = 0; bit != width; ++bit) {
          uint64_t absolute = 53 + bit;
          uint64_t mask = uint64_t{1} << (absolute % 64);
          if (!((changed[bit / 8] >> (bit % 8)) & 1) ||
              (!establishesOverride &&
               (context->forceMask[absolute / 64] & mask)))
            continue;
          auto apply = [&](auto &plane, const auto &input) {
            // CanonicalPlane indexing yields a proxy; bind either form.
            auto &&word = plane[absolute / 64];
            word = ((input[bit / 8] >> (bit % 8)) & 1) ? word | mask
                                                       : word & ~mask;
          };
          apply(expectedValue, value);
          apply(expectedUnknown, unknown);
        }
        ASSERT_TRUE(publishNativeSignalTransitionUnlocked(
            context, handle, width, changed.data(), nullptr, nullptr, nullptr,
            nullptr, value.data(), unknown.data(), establishesOverride));
        EXPECT_EQ(context->stateValue, expectedValue);
        EXPECT_EQ(context->stateUnknown, expectedUnknown);
      }
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ReusableByteBuffersAreSafeAcrossWorkers) {
  ReusableByteBufferPool pool;
  std::atomic<unsigned> failures{0};
  std::vector<std::thread> workers;
  for (unsigned worker = 0; worker != 8; ++worker)
    workers.emplace_back([&, worker] {
      size_t size = size_t{1} << (6 + worker);
      for (unsigned iteration = 0; iteration != 1000; ++iteration) {
        std::vector<uint8_t> buffer = pool.acquire(size);
        if (buffer.size() != size ||
            !std::all_of(buffer.begin(), buffer.end(),
                         [](uint8_t value) { return value == 0; }))
          ++failures;
        std::fill(buffer.begin(), buffer.end(),
                  static_cast<uint8_t>(worker + 1));
        pool.release(std::move(buffer));
      }
    });
  for (std::thread &worker : workers)
    worker.join();

  EXPECT_EQ(failures.load(), 0u);
  EXPECT_LE(pool.size(), 64u);
  EXPECT_LE(pool.byteSize(), 16u * 1024 * 1024);
}

TEST(RuntimeInternals, ReusableByteBuffersHonorAggregateBounds) {
  {
    ReusableByteBufferPool pool;
    for (unsigned index = 0; index != 100; ++index)
      pool.release(std::vector<uint8_t>(1));
    EXPECT_EQ(pool.size(), 64u);
    EXPECT_LE(pool.byteSize(), 16u * 1024 * 1024);
  }
  {
    ReusableByteBufferPool pool;
    for (unsigned index = 0; index != 20; ++index)
      pool.release(std::vector<uint8_t>(1024 * 1024));
    EXPECT_LE(pool.size(), 16u);
    EXPECT_LE(pool.byteSize(), 16u * 1024 * 1024);
  }
}

TEST(RuntimeInternals, TerminatedTokenRangesMergeAndSplitExactly) {
  TerminatedTokenSet tokens;
  EXPECT_TRUE(tokens.insert(0).second);
  EXPECT_TRUE(tokens.insert(1).second);
  EXPECT_EQ(tokens.rangeCount(), 1u);
  EXPECT_EQ(tokens.erase(1), 1u);
  EXPECT_TRUE(tokens.insert(UINT64_MAX).second);
  EXPECT_TRUE(tokens.insert(UINT64_MAX - 1).second);
  EXPECT_EQ(tokens.rangeCount(), 2u);
  EXPECT_EQ(tokens.erase(UINT64_MAX - 1), 1u);
  EXPECT_EQ(tokens.erase(0), 1u);
  EXPECT_EQ(tokens.erase(UINT64_MAX), 1u);
  EXPECT_TRUE(tokens.insert(1).second);
  EXPECT_TRUE(tokens.insert(3).second);
  EXPECT_EQ(tokens.rangeCount(), 2u);
  EXPECT_TRUE(tokens.insert(2).second);
  EXPECT_EQ(tokens.rangeCount(), 1u);
  EXPECT_FALSE(tokens.insert(2).second);

  EXPECT_EQ(tokens.erase(2), 1u);
  EXPECT_EQ(tokens.rangeCount(), 2u);
  EXPECT_EQ(tokens.count(1), 1u);
  EXPECT_EQ(tokens.count(2), 0u);
  EXPECT_EQ(tokens.count(3), 1u);
  EXPECT_EQ(tokens.erase(2), 0u);
  EXPECT_TRUE(tokens.insert(2).second);
  EXPECT_EQ(tokens.rangeCount(), 1u);
}

TEST(RuntimeInternals, TerminatedTokenRangesMatchReferenceSet) {
  TerminatedTokenSet tokens;
  std::unordered_set<uint64_t> reference;
  std::mt19937_64 random(0x4f42454c49534bULL);
  for (unsigned iteration = 0; iteration != 10000; ++iteration) {
    uint64_t token = random() % 257;
    if ((random() & 1) != 0)
      EXPECT_EQ(tokens.insert(token).second, reference.insert(token).second);
    else
      EXPECT_EQ(tokens.erase(token), reference.erase(token));
    for (uint64_t probe = 0; probe != 257; ++probe)
      ASSERT_EQ(tokens.count(probe), reference.count(probe));
  }
}

TEST(RuntimeInternals, ResumeOverridesRequireExecutableHomeRegions) {
  uint32_t queuedRegion = UINT32_MAX;
  bool postObserved = true;
  uint32_t postponed =
      TALOS_RT_ACTION_RESUME_REGION_VALID |
      (TALOS_RT_REGION_POSTPONED << TALOS_RT_ACTION_RESUME_REGION_SHIFT);
  EXPECT_TRUE(talos_rt_next_queued_region(
      TALOS_RT_REGION_ACTIVE, TALOS_RT_SUSPEND_CHANGE, 1, postponed,
      queuedRegion, postObserved));
  EXPECT_EQ(queuedRegion, TALOS_RT_REGION_POSTPONED);
  EXPECT_FALSE(postObserved);

  uint32_t nba =
      TALOS_RT_ACTION_RESUME_REGION_VALID |
      (TALOS_RT_REGION_NBA << TALOS_RT_ACTION_RESUME_REGION_SHIFT);
  EXPECT_FALSE(talos_rt_next_queued_region(TALOS_RT_REGION_ACTIVE,
                                             TALOS_RT_SUSPEND_CHANGE, 1, nba,
                                             queuedRegion, postObserved));
}

TEST(RuntimeInternals, PostObservedResumesTheHomeIterationAfterObserved) {
  // IEEE 1800-2023 9.4.2.4, 14.10: an Active-home process queues with
  // Observed, after its other work; a Reactive home resumes in Reactive.
  uint32_t queuedRegion = UINT32_MAX;
  bool postObserved = false;
  EXPECT_TRUE(talos_rt_next_queued_region(
      TALOS_RT_REGION_ACTIVE, TALOS_RT_SUSPEND_CHANGE, 1,
      TALOS_RT_ACTION_RESUME_POST_OBSERVED, queuedRegion, postObserved));
  EXPECT_EQ(queuedRegion, TALOS_RT_REGION_OBSERVED);
  EXPECT_TRUE(postObserved);
  EXPECT_TRUE(talos_rt_next_queued_region(
      TALOS_RT_REGION_REACTIVE, TALOS_RT_SUSPEND_CHANGE, 1,
      TALOS_RT_ACTION_RESUME_POST_OBSERVED, queuedRegion, postObserved));
  EXPECT_EQ(queuedRegion, TALOS_RT_REGION_REACTIVE);
  EXPECT_FALSE(postObserved);
  EXPECT_FALSE(talos_rt_next_queued_region(
      TALOS_RT_REGION_OBSERVED, TALOS_RT_SUSPEND_CHANGE, 1,
      TALOS_RT_ACTION_RESUME_POST_OBSERVED, queuedRegion, postObserved));
  EXPECT_FALSE(talos_rt_valid_resume_action_flags(
      TALOS_RT_ACTION_RESUME_POST_OBSERVED |
      TALOS_RT_ACTION_RESUME_REGION(TALOS_RT_REGION_REACTIVE)));

  ScheduledProcess process;
  process.scheduleRank = 3;
  process.postObserved = true;
  process.queuedRegion = TALOS_RT_REGION_OBSERVED;
  EXPECT_EQ(talos_rt_scheduler_rank(process), UINT32_MAX);
  process.queuedRegion = TALOS_RT_REGION_ACTIVE;
  EXPECT_EQ(talos_rt_scheduler_rank(process), 3u);
}

TEST(RuntimeInternals, MonitorRepeatsOnlyWhenItsDisplayedValuesChange) {
  // IEEE 1800-2017 21.2.3: a monitor reports when one of its arguments
  // changes value. Waking it without a change -- an unrelated bit of a
  // watched variable, say -- must not repeat the report.
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  std::string path =
      (std::filesystem::temp_directory_path() / "talos-monitor.bin").string();
  std::string mode = "w+b";
  uint32_t descriptor = 0;
  ASSERT_EQ(talos_rt_v1_file_open(context, path.data(), path.size(),
                                    mode.data(), mode.size(), &descriptor),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_monitor_register(context, 21, 1), TALOS_RT_OK);
  context->activeLogicalProcessToken = 21;

  std::string formatString = "bit=%b";
  uint64_t zero = 0;
  uint64_t one = 1;
  auto report = [&](const uint64_t &value) {
    talos_rt_arg_v1 items[2] = {
        {TALOS_RT_ARG_STRING, TALOS_RT_ARG_FORMAT_STRING,
         formatString.size(), formatString.data(), nullptr},
        {TALOS_RT_ARG_LOGIC, 0, 1, &value, nullptr}};
    return talos_rt_v1_display(context, descriptor, 1,
                                 TALOS_RT_RADIX_DECIMAL, items, 2, nullptr);
  };
  ASSERT_EQ(report(zero), TALOS_RT_OK);
  ASSERT_EQ(report(zero), TALOS_RT_OK);
  ASSERT_EQ(report(one), TALOS_RT_OK);
  ASSERT_EQ(report(one), TALOS_RT_OK);

  // A simulation time argument advances on its own and never triggers a
  // report, so a list carrying one still reports only on a value change.
  std::string timedFormat = "%0t bit=%b";
  uint64_t early = 10;
  uint64_t late = 20;
  auto timedReport = [&](const uint64_t &time, const uint64_t &value) {
    talos_rt_arg_v1 items[3] = {
        {TALOS_RT_ARG_STRING, TALOS_RT_ARG_FORMAT_STRING,
         timedFormat.size(), timedFormat.data(), nullptr},
        {TALOS_RT_ARG_TIME, 0, 64, &time, nullptr},
        {TALOS_RT_ARG_LOGIC, 0, 1, &value, nullptr}};
    return talos_rt_v1_display(context, descriptor, 1,
                                 TALOS_RT_RADIX_DECIMAL, items, 3, nullptr);
  };
  ASSERT_EQ(timedReport(early, one), TALOS_RT_OK);
  ASSERT_EQ(timedReport(late, one), TALOS_RT_OK);
  ASSERT_EQ(timedReport(late, zero), TALOS_RT_OK);
  context->activeLogicalProcessToken = 0;

  ASSERT_EQ(talos_rt_v1_file_flush(context, descriptor), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptor), TALOS_RT_OK);
  char bytes[64]{};
  uint64_t read = 0;
  ASSERT_EQ(
      talos_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      TALOS_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)),
            "bit=0\nbit=1\n10 bit=1\n20 bit=0\n");
  talos_rt_v1_context_destroy(context);
  std::filesystem::remove(path);
}

TEST(RuntimeInternals, ClosingAChannelDisablesTheMonitorWritingToIt) {
  // IEEE 1800-2017 21.3.5: closing a channel disables any $fmonitor or
  // $fstrobe associated with it. The report that would follow the close is
  // dropped, and the monitor stays unregistered rather than failing every
  // time one of its arguments changes.
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  std::string path =
      (std::filesystem::temp_directory_path() / "talos-monitor-closed.bin")
          .string();
  std::string mode = "w+b";
  uint32_t descriptor = 0;
  ASSERT_EQ(talos_rt_v1_file_open(context, path.data(), path.size(),
                                    mode.data(), mode.size(), &descriptor),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_monitor_register(context, 21, 1), TALOS_RT_OK);
  context->activeLogicalProcessToken = 21;

  std::string formatString = "bit=%b";
  uint64_t zero = 0;
  uint64_t one = 1;
  auto report = [&](const uint64_t &value) {
    talos_rt_arg_v1 items[2] = {
        {TALOS_RT_ARG_STRING, TALOS_RT_ARG_FORMAT_STRING,
         formatString.size(), formatString.data(), nullptr},
        {TALOS_RT_ARG_LOGIC, 0, 1, &value, nullptr}};
    return talos_rt_v1_display(context, descriptor, 1,
                                 TALOS_RT_RADIX_DECIMAL, items, 2, nullptr);
  };
  ASSERT_EQ(report(zero), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
  EXPECT_EQ(report(one), TALOS_RT_OK);
  EXPECT_EQ(context->monitorLogicalProcessToken, 0u);
  context->activeLogicalProcessToken = 0;

  std::ifstream written(path, std::ios::binary);
  std::string contents((std::istreambuf_iterator<char>(written)),
                       std::istreambuf_iterator<char>());
  EXPECT_EQ(contents, "bit=0\n");
  written.close();
  talos_rt_v1_context_destroy(context);
  std::filesystem::remove(path);
}

TEST(RuntimeInternals, ReplacedAndReenabledMonitorsAreWokenInPostponed) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  uint64_t selectionGeneration = context->schedulerSelectionGeneration;
  ScheduledDesignTask oldMonitor;
  oldMonitor.id = 17;
  oldMonitor.suspendKind = TALOS_RT_SUSPEND_FOREVER;
  oldMonitor.queuedRegion = TALOS_RT_REGION_ACTIVE;
  context->scheduledDesignTasks.push_back(std::move(oldMonitor));

  ASSERT_EQ(talos_rt_v1_monitor_register(context, 17, 1), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_monitor_register(context, 18, 1), TALOS_RT_OK);
  EXPECT_EQ(context->scheduledDesignTasks.front().suspendKind,
            TALOS_RT_SUSPEND_NONE);
  EXPECT_EQ(context->scheduledDesignTasks.front().queuedRegion,
            TALOS_RT_REGION_POSTPONED);
  EXPECT_NE(context->schedulerSelectionGeneration, selectionGeneration);

  context->scheduledDesignTasks.front().suspendKind =
      TALOS_RT_SUSPEND_FOREVER;
  selectionGeneration = context->schedulerSelectionGeneration;
  ASSERT_EQ(talos_rt_v1_monitor_register(context, 17, 1), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_monitor_control(context, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_monitor_control(context, 1), TALOS_RT_OK);
  EXPECT_EQ(context->scheduledDesignTasks.front().suspendKind,
            TALOS_RT_SUSPEND_NONE);
  EXPECT_EQ(context->scheduledDesignTasks.front().queuedRegion,
            TALOS_RT_REGION_POSTPONED);
  EXPECT_NE(context->schedulerSelectionGeneration, selectionGeneration);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ManagedObjectWaitsRemainRootsAcrossCollection) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);

  for (uint32_t suspendKind :
       {TALOS_RT_SUSPEND_SEMAPHORE, TALOS_RT_SUSPEND_MAILBOX}) {
    talos_rt_string_v1 string = 0;
    ASSERT_EQ(talos_rt_v1_string_create(lane, "waiting", 7, &string),
              TALOS_RT_OK);
    const talos_rt_string_v1 stableID = string;

    struct {
      talos_rt_wait_record_v1 wait;
      talos_rt_wait_entry_v1 entry;
    } record{{TALOS_RT_VERSION, suspendKind, 0, 1, 0, 0},
             {stableID, TALOS_RT_WAIT_EDGE_NONE, 0}};
    ScheduledDesignTask task;
    task.id = suspendKind;
    task.started = true;
    task.suspendKind = suspendKind;
    task.waitSize = sizeof(record);
    task.scratchOffset = sizeof(record);
    task.frame.resize(sizeof(record));
    std::memcpy(task.frame.data(), &record, sizeof(record));
    context->scheduledDesignTasks.push_back(std::move(task));

    string = 0;
    ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
    char inlineBytes[8]{};
    const char *bytes = nullptr;
    uint64_t size = 0;
    ASSERT_EQ(talos_rt_v1_string_view(stableID, inlineBytes, &bytes, &size),
              TALOS_RT_OK);
    EXPECT_EQ(std::string_view(bytes, size), "waiting");

    context->scheduledDesignTasks.clear();
    ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  }

  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, SignalSubscriptionsAreRangeIndexedStableAndBounded) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->signalDiagnosticsEnabled = true;

  struct {
    talos_rt_wait_record_v1 wait;
    std::array<talos_rt_wait_entry_v1, 2> entries;
  } record{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE, 0, 2, 0, 0},
           {{{16, TALOS_RT_WAIT_EDGE_POSEDGE, 8},
             {80, TALOS_RT_WAIT_EDGE_NEGEDGE, 4}}}};

  ScheduledProcess process;
  process.token = 7;
  process.instance = reinterpret_cast<talos_rt_process_instance_v1 *>(1);
  process.started = true;
  process.suspendKind = TALOS_RT_SUSPEND_EDGE;
  context->scheduledProcesses.push_back(std::move(process));
  ScheduledDesignTask task;
  task.id = 9;
  task.started = true;
  task.suspendKind = TALOS_RT_SUSPEND_EDGE;
  context->scheduledDesignTasks.push_back(std::move(task));

  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &record.wait,
      context->scheduledProcesses.back().signalSubscriptions,
      context->scheduledProcesses.back().signalLatch, 7, false));
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &record.wait,
      context->scheduledDesignTasks.back().signalSubscriptions,
      context->scheduledDesignTasks.back().signalLatch, 9, true));
  EXPECT_EQ(context->scheduledProcesses.back().signalSubscriptions.size(), 2u);
  EXPECT_EQ(context->scheduledDesignTasks.back().signalSubscriptions.size(),
            2u);
  EXPECT_EQ(context->signalDiagnostics.subscriptionsCurrent, 4u);
  EXPECT_EQ(context->signalDiagnostics.subscriptionsHighWater, 4u);

  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 18, 1, TALOS_RT_SIGNAL_CHANGE));
  EXPECT_FALSE(context->scheduledProcesses.back().signalLatch->triggered);
  EXPECT_FALSE(context->scheduledDesignTasks.back().signalLatch->triggered);
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 18, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  EXPECT_TRUE(context->scheduledProcesses.back().signalLatch->triggered);
  EXPECT_TRUE(context->scheduledDesignTasks.back().signalLatch->triggered);
  EXPECT_EQ(context->nativePollCandidates.count(7), 1u);
  EXPECT_EQ(context->designPollCandidates.count(9), 1u);

  talos_rt_unregister_signal_wait_unlocked(
      context, context->scheduledProcesses.back().signalSubscriptions);
  talos_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks.back().signalSubscriptions);
  EXPECT_TRUE(context->signalSubscriptionBuckets.empty());
  EXPECT_EQ(context->signalDiagnostics.subscriptionsCurrent, 0u);
  EXPECT_EQ(context->signalDiagnostics.publications, 2u);
  EXPECT_EQ(context->signalDiagnostics.subscribersExamined, 8u);

  struct {
    talos_rt_wait_record_v1 wait;
    talos_rt_wait_entry_v1 entry;
  } positiveRecord{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE, 0, 1, 0, 0},
                   {17, TALOS_RT_WAIT_EDGE_POSEDGE, 1}},
      negativeRecord{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE, 0, 1, 0, 0},
                     {17, TALOS_RT_WAIT_EDGE_NEGEDGE, 1}};
  std::vector<std::unique_ptr<SignalSubscription>> positiveSubscriptions;
  std::vector<std::unique_ptr<SignalSubscription>> negativeSubscriptions;
  std::unique_ptr<SignalWaitLatch> positiveLatch;
  std::unique_ptr<SignalWaitLatch> negativeLatch;
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &positiveRecord.wait, positiveSubscriptions, positiveLatch));
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &negativeRecord.wait, negativeSubscriptions, negativeLatch));
  const uint8_t changedTransitions = 0b11;
  const uint8_t posedgeTransitions = 0b01;
  const uint8_t negedgeTransitions = 0b10;
  ASSERT_TRUE(talos_rt_publish_signal_transition_batch_unlocked(
      context, 16, 2, &changedTransitions, &posedgeTransitions,
      &negedgeTransitions));
  EXPECT_FALSE(positiveLatch->triggered);
  EXPECT_TRUE(negativeLatch->triggered);
  talos_rt_unregister_signal_wait_unlocked(context, positiveSubscriptions);
  talos_rt_unregister_signal_wait_unlocked(context, negativeSubscriptions);

  struct PageWait {
    struct {
      talos_rt_wait_record_v1 wait;
      talos_rt_wait_entry_v1 entry;
    } record{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0},
             {0, TALOS_RT_WAIT_EDGE_CHANGE, 1}};
    std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
    std::unique_ptr<SignalWaitLatch> latch;
  };
  std::vector<PageWait> pageWaits(65);
  for (size_t index = 0; index != pageWaits.size(); ++index) {
    pageWaits[index].record.entry.stable_id =
        index == 0 ? 16 : index * 256 + 16;
    ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
        context, &pageWaits[index].record.wait, pageWaits[index].subscriptions,
        pageWaits[index].latch));
  }
  uint64_t examinedBefore = context->signalDiagnostics.subscribersExamined;
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(pageWaits[0].latch->triggered);
  for (size_t index = 1; index != pageWaits.size(); ++index)
    EXPECT_FALSE(pageWaits[index].latch->triggered);
  EXPECT_EQ(context->signalDiagnostics.subscribersExamined - examinedBefore,
            1u);
  for (PageWait &waiter : pageWaits)
    talos_rt_unregister_signal_wait_unlocked(context, waiter.subscriptions);

  PageWait wideWait;
  wideWait.record.entry.reserved = UINT32_MAX;
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &wideWait.record.wait, wideWait.subscriptions, wideWait.latch));
  ASSERT_EQ(wideWait.subscriptions.size(), 1u);
  EXPECT_EQ(wideWait.subscriptions.front()->bucketSlots.size(), 1u);
  examinedBefore = context->signalDiagnostics.subscribersExamined;
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 8192, 1, TALOS_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(wideWait.latch->triggered);
  EXPECT_EQ(context->signalDiagnostics.subscribersExamined - examinedBefore,
            1u);
  talos_rt_unregister_signal_wait_unlocked(context, wideWait.subscriptions);

  struct {
    talos_rt_computed_wait_record_v1 wait{};
    std::array<talos_rt_computed_dependency_v1, 2> dependencies{};
  } computed;
  computed.wait.dependency_count = computed.dependencies.size();
  computed.wait.dependencies_offset = sizeof(computed.wait);
  computed.dependencies[0] = {16, TALOS_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
  computed.dependencies[1] = {4096, TALOS_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
  std::vector<std::unique_ptr<SignalSubscription>> computedSubscriptions;
  std::unique_ptr<SignalWaitLatch> computedLatch;
  ASSERT_TRUE(talos_rt_register_computed_signal_wait_unlocked(
      context, &computed.wait, 42, false, computedSubscriptions,
      computedLatch));
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 0u);
  examinedBefore = context->signalDiagnostics.subscribersExamined;
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE));
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE));
  EXPECT_EQ(context->pendingNativeComputedWaiters, std::vector<uint64_t>({42}));
  EXPECT_TRUE(context->pendingDesignComputedWaiters.empty());
  EXPECT_EQ(context->signalDiagnostics.subscribersExamined - examinedBefore,
            2u);
  talos_rt_unregister_signal_wait_unlocked(context, computedSubscriptions, 42,
                                             false);
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 0u);
  context->pendingNativeComputedWaiters.clear();

  struct {
    talos_rt_wait_record_v1 wait;
    talos_rt_wait_entry_v1 entry;
  } objectRecord{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0},
                 {0, TALOS_RT_WAIT_EDGE_CHANGE, 4}};
  std::vector<std::unique_ptr<SignalSubscription>> objectSubscriptions;
  std::unique_ptr<SignalWaitLatch> objectLatch;
  objectRecord.entry.stable_id =
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_STATIC, 3, 4);
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &objectRecord.wait, objectSubscriptions, objectLatch));
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context,
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_STATIC, 4, 5), 1,
      TALOS_RT_SIGNAL_CHANGE));
  EXPECT_FALSE(objectLatch->triggered);
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context,
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_STATIC, 3, 5), 1,
      TALOS_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(objectLatch->triggered);
  talos_rt_unregister_signal_wait_unlocked(context, objectSubscriptions);

  objectRecord.entry.stable_id =
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_AUTOMATIC, 7, 4);
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &objectRecord.wait, objectSubscriptions, objectLatch));
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context,
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_AUTOMATIC, 8, 5),
      1, TALOS_RT_SIGNAL_CHANGE));
  EXPECT_FALSE(objectLatch->triggered);
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context,
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_AUTOMATIC, 7, 5),
      1, TALOS_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(objectLatch->triggered);
  talos_rt_unregister_signal_wait_unlocked(context, objectSubscriptions);

  context->scheduledProcesses.back().instance = nullptr;
  context->nativeConditionalSignalWaiters.insert(UINT64_C(0xdeadbeef));
  ASSERT_TRUE(talos_rt_append_signal_event_unlocked(context, 16, false, false,
                                                      true, false, false));
  ASSERT_TRUE(talos_rt_append_signal_event_unlocked(
      context, 4096, false, false, true, false, false));
  ASSERT_EQ(context->signalValueSnapshots.size(), 2u);
  talos_rt_invalidate_signal_snapshots_unlocked(context, 16, 1);
  EXPECT_EQ(context->signalValueSnapshots.count(16), 0u);
  EXPECT_EQ(context->signalValueSnapshots.count(4096), 1u);
  talos_rt_invalidate_signal_snapshots_unlocked(context, 0, 8192);
  EXPECT_TRUE(context->signalValueSnapshots.empty());

  for (unsigned iteration = 0; iteration != 1000; ++iteration) {
    ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
        context, &record.wait,
        context->scheduledDesignTasks.back().signalSubscriptions,
        context->scheduledDesignTasks.back().signalLatch));
    talos_rt_unregister_signal_wait_unlocked(
        context, context->scheduledDesignTasks.back().signalSubscriptions);
  }
  EXPECT_TRUE(context->signalSubscriptionBuckets.empty());
  EXPECT_EQ(context->signalDiagnostics.subscriptionsCurrent, 0u);
  EXPECT_EQ(context->signalDiagnostics.subscriptionsHighWater, 65u);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, SignalObjectFilterRetainsCollidingSparseSubscriptions) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  constexpr uint32_t kind = TALOS_RT_STABLE_HANDLE_STATIC;
  constexpr uint32_t first = 3, second = 0x10000003;
  ASSERT_EQ(signalSubscriptionObjectFilterIndex(kind, first),
            signalSubscriptionObjectFilterIndex(kind, second));
  struct {
    talos_rt_wait_record_v1 wait;
    talos_rt_wait_entry_v1 entry;
  } record{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0},
           {0, TALOS_RT_WAIT_EDGE_CHANGE, 8192}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions[2];
  std::unique_ptr<SignalWaitLatch> latches[2];
  const uint32_t ids[] = {first, second};
  for (unsigned i = 0; i < 2; ++i) {
    record.entry.stable_id = talos_rt_stable_handle_encode(kind, ids[i], 0);
    ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
        context, &record.wait, subscriptions[i], latches[i]));
  }
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, talos_rt_stable_handle_encode(kind, first, 4096), 1,
      TALOS_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(latches[0]->triggered);
  EXPECT_FALSE(latches[1]->triggered);
  // Removing every page of one object must retain the colliding object.
  talos_rt_unregister_signal_wait_unlocked(context, subscriptions[0]);
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, talos_rt_stable_handle_encode(kind, second, 4096), 1,
      TALOS_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(latches[1]->triggered);
  talos_rt_unregister_signal_wait_unlocked(context, subscriptions[1]);
  EXPECT_TRUE(context->signalSubscriptionBuckets.empty());
  EXPECT_TRUE(std::all_of(context->signalSubscriptionObjectFilter.begin(),
                          context->signalSubscriptionObjectFilter.end(),
                          [](uint64_t count) { return count == 0; }));
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceCohortsFinalizeByProducerWaveAndRemainTokenLocal) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);

  struct ClockWait {
    talos_rt_wait_record_v1 wait;
    std::array<talos_rt_wait_entry_v1, 2> entries;
  } first{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE,
           TALOS_RT_WAIT_CLOCK_OCCURRENCE, 2, 71, 0},
          {{{16, TALOS_RT_WAIT_EDGE_POSEDGE, 1},
            {32, TALOS_RT_WAIT_EDGE_POSEDGE, 1}}}},
      second{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE,
              TALOS_RT_WAIT_CLOCK_OCCURRENCE, 2, 72, 0},
             {{{48, TALOS_RT_WAIT_EDGE_POSEDGE, 1},
               {64, TALOS_RT_WAIT_EDGE_POSEDGE, 1}}}};

  auto addTask = [&](uint64_t token, ClockWait &record) {
    ScheduledDesignTask task;
    task.id = token;
    task.started = true;
    task.suspendKind = TALOS_RT_SUSPEND_EDGE;
    context->scheduledDesignTaskIndices[token] =
        context->scheduledDesignTasks.size();
    context->scheduledDesignTasks.push_back(std::move(task));
    ScheduledDesignTask &scheduled = context->scheduledDesignTasks.back();
    ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
        context, &record.wait, scheduled.signalSubscriptions,
        scheduled.signalLatch, token, true));
  };
  addTask(11, first);
  addTask(12, second);
  ASSERT_TRUE(context->clockOccurrences);
  ASSERT_EQ(context->clockOccurrences->waits.size(), 2u);

  context->schedulerTime = 9;
  context->schedulerSlotProgress = 20;
  context->activeExecRegion = TALOS_RT_REGION_ACTIVE;
  context->activeLogicalProcessToken = 11;
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  // The coordinator can run before another producer boundary, but a partial
  // cohort from the still-open publication wave is deliberately invisible.
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 71), 0u);
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 71), 0u);

  ++context->schedulerSlotProgress;
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 71), 3u);
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 72), 0u);

  // Two occurrences of clock 0 in one producer wave form ordinal cohorts;
  // they are not coalesced merely because simulation time and region match.
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 71), 3u);
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 71), 1u);

  // Same-time publications separated by a producer boundary are distinct,
  // even when no simulation time advances.
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 71), 1u);
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 71), 2u);

  // Region changes are part of the exact occurrence key as well. Active and
  // Reactive publications cannot fuse merely because time/progress match in a
  // synthetic or nested scheduler handoff.
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  context->activeExecRegion = TALOS_RT_REGION_REACTIVE;
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 71), 1u);
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 71), 2u);

  // An exact token/site pair owns each queue; one coordinator cannot consume
  // another coordinator's coincident mask.
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 48, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 64, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 72), 0u);
  context->activeLogicalProcessToken = 12;
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 72), 3u);
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 71), 0u);

  talos_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks[0].signalSubscriptions, 11, true);
  talos_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks[1].signalSubscriptions, 12, true);
  EXPECT_FALSE(context->clockOccurrences);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, NoChangeUsesPayForPlaySignedOpenWindows) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  EXPECT_FALSE(context->clockOccurrences);
  EXPECT_FALSE(context->noChangeChecks);
  context->clockOccurrences = std::make_unique<ClockOccurrenceFeatureState>();
  context->activeLogicalProcessToken = 41;

  auto update = [&](uint64_t site, uint64_t time, uint64_t mask, int64_t start,
                    int64_t end) {
    context->schedulerTime = time;
    return talos_rt_v1_nochange_update(context, site, mask, start, end);
  };

  // Positive start retains data before the leading edge. Both endpoints are
  // open, so t=7 and t=20 are excluded from (10-3, 20+0).
  EXPECT_EQ(update(91, 7, 2, 3, 0), 0u);
  EXPECT_EQ(update(91, 8, 2, 3, 0), 0u);
  EXPECT_EQ(update(91, 10, 1, 3, 0), 0u);
  // Positive-start history is certain at the leading slot; Clause 31.6 does
  // not wait for the later trailing edge to publish its one report.
  EXPECT_EQ(update(91, 10, 0, 3, 0), 1u);
  EXPECT_EQ(update(91, 20, 6, 3, 0), 0u);
  EXPECT_EQ(update(91, 20, 0, 3, 0), 0u);

  // Negative end defers all data until the trailing edge fixes t=28.
  EXPECT_EQ(update(92, 20, 1, -2, -2), 0u);
  EXPECT_EQ(update(92, 21, 2, -2, -2), 0u);
  EXPECT_EQ(update(92, 23, 2, -2, -2), 0u);
  EXPECT_EQ(update(92, 23, 2, -2, -2), 0u);
  EXPECT_EQ(update(92, 27, 2, -2, -2), 0u);
  EXPECT_EQ(update(92, 28, 2, -2, -2), 0u);
  EXPECT_EQ(update(92, 30, 4, -2, -2), 0u);
  EXPECT_EQ(update(92, 30, 0, -2, -2), 3u);

  // A completed positive-end window can overlap the next open window. One
  // data occurrence is then one violation for each Clause 31.4.6 interval.
  EXPECT_EQ(update(93, 40, 1, 0, 10), 0u);
  EXPECT_EQ(update(93, 45, 4, 0, 10), 0u);
  EXPECT_EQ(update(93, 50, 1, 0, 10), 0u);
  EXPECT_EQ(update(93, 52, 2, 0, 10), 0u);
  // The data is in both the first positive-end tail and the second open
  // window, so multiplicity is published in the data slot.
  EXPECT_EQ(update(93, 52, 0, 0, 10), 2u);
  EXPECT_EQ(update(93, 55, 4, 0, 10), 0u);
  EXPECT_EQ(update(93, 55, 0, 0, 10), 0u);

  context->activeLogicalProcessToken = 42;
  EXPECT_EQ(update(94, 60, 1, 0, 0), 0u);
  EXPECT_EQ(update(94, 60, 0, 0, 0), 0u);
  ASSERT_TRUE(context->noChangeChecks);
  ASSERT_EQ(context->noChangeChecks->checks.size(), 4u);
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  talos_rt_unregister_signal_wait_unlocked(context, subscriptions, 41, true);
  ASSERT_TRUE(context->noChangeChecks);
  EXPECT_EQ(context->noChangeChecks->checks.size(), 1u);
  EXPECT_FALSE(context->clockOccurrences);
  talos_rt_unregister_signal_wait_unlocked(context, subscriptions, 42, true);
  EXPECT_FALSE(context->noChangeChecks);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ClockOccurrenceStateIsLazyAndOrdinaryWaitLayoutStable) {
  static_assert(sizeof(void *) != 8 || sizeof(SignalSubscription) == 72);
  static_assert(sizeof(talos_rt_wait_record_v1) == 32);
  // The maps' sizes depend on the C++ standard library. RuntimeInternal.h
  // checks their exact aggregate layout; keep the fixed libstdc++ footprint.
#if defined(__GLIBCXX__)
  static_assert(sizeof(void *) != 8 ||
                sizeof(ClockOccurrenceFeatureState) == 240);
#endif

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  const auto *contextBegin = reinterpret_cast<const std::byte *>(context);
  const auto *featureBegin =
      reinterpret_cast<const std::byte *>(&context->clockOccurrences);
  const auto *randomBegin =
      reinterpret_cast<const std::byte *>(&context->random);
  EXPECT_GT(featureBegin, randomBegin);
  EXPECT_LE(featureBegin + sizeof(context->clockOccurrences),
            contextBegin + sizeof(*context));
  ASSERT_FALSE(context->clockOccurrences);
  ASSERT_FALSE(context->noChangeChecks);
  struct {
    talos_rt_wait_record_v1 wait;
    talos_rt_wait_entry_v1 entry;
  } ordinary{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE,
              TALOS_RT_WAIT_FLAGS_NONE, 1, 0, 0},
             {16, TALOS_RT_WAIT_EDGE_POSEDGE, 1}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &ordinary.wait, subscriptions, latch, 9, false));
  ASSERT_EQ(subscriptions.size(), 1u);
  EXPECT_FALSE(context->clockOccurrences);
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  EXPECT_FALSE(context->clockOccurrences);
  talos_rt_unregister_signal_wait_unlocked(context, subscriptions, 9, false);
  EXPECT_FALSE(context->clockOccurrences);

  struct {
    talos_rt_wait_record_v1 wait;
    talos_rt_wait_entry_v1 entry;
  } slotFinal{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE,
               TALOS_RT_WAIT_CLOCK_OCCURRENCE |
                   TALOS_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL,
               1, 91, 0},
              {32, TALOS_RT_WAIT_EDGE_POSEDGE, 1}};
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &slotFinal.wait, subscriptions, latch, 10, false));
  EXPECT_TRUE(context->clockOccurrences);
  talos_rt_unregister_signal_wait_unlocked(context, subscriptions, 10, false);
  EXPECT_FALSE(context->clockOccurrences);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ReplaceableTimingEventIsLazyBoundedSaturatingAndPermanentlyGuarded) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);

  // This service is owned by a slot-final timing coordinator.  Its feature
  // state consequently shares the exact lifecycle of that coordinator's
  // clock-occurrence wait and is absent from ordinary designs.
  context->clockOccurrences = std::make_unique<ClockOccurrenceFeatureState>();
  context->clockOccurrences->waits.try_emplace(21);
  context->activeLogicalProcessToken = 21;
  ASSERT_FALSE(context->clockOccurrences->replaceableEvents);

  context->schedulerTime = 7;
  for (uint64_t restart = 0; restart != 10000; ++restart) {
    talos_rt_v1_scheduler_event_replace_after(context, 91, 1, 100 + restart);
    ASSERT_EQ(context->schedulerStatus, TALOS_RT_OK);
    ASSERT_TRUE(context->clockOccurrences->replaceableEvents);
    EXPECT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(),
              1u);
    EXPECT_EQ(context->clockOccurrences->replaceableEvents->pending.size(), 1u);
    // Restart replacement is indexed and never enters the ordinary delayed
    // vector, so its live population cannot grow or induce a linear scan.
    EXPECT_TRUE(context->scheduledDesignEvents.empty());
  }
  talos_rt_v1_scheduler_event_replace_after(context, 91, 0, 0);
  EXPECT_TRUE(context->clockOccurrences->replaceableEvents->calendar.empty());
  EXPECT_EQ(context->clockOccurrences->replaceableEvents->pending.size(), 1u);

  context->schedulerTime = UINT64_MAX - 2;
  talos_rt_v1_scheduler_event_replace_after(context, 91, 1, 10);
  ASSERT_EQ(context->schedulerStatus, TALOS_RT_OK);
  ASSERT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(), 1u);
  EXPECT_EQ(context->clockOccurrences->replaceableEvents->calendar.begin()
                ->first.first,
            UINT64_MAX);

  auto &pending = context->clockOccurrences->replaceableEvents->pending.at(91);
  pending.generation = UINT64_MAX;
  talos_rt_v1_scheduler_event_replace_after(context, 91, 0, 0);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(pending.generation, UINT64_MAX);
  EXPECT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(), 1u);

  // Exhaustion is permanent: clearing the status cannot permit generation
  // wrap to make an ancient maturity current again.
  context->schedulerStatus = TALOS_RT_OK;
  talos_rt_v1_scheduler_event_replace_after(context, 91, 1, 1);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(pending.generation, UINT64_MAX);
  EXPECT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(), 1u);

  context->schedulerStatus = TALOS_RT_OK;
  context->nextSchedulerSequence = 0;
  talos_rt_v1_scheduler_event_replace_after(context, 92, 1, 1);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(
      context->clockOccurrences->replaceableEvents->pending.at(92).generation,
      0u);
  EXPECT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(), 1u);

  context->activeLogicalProcessToken = 0;
  context->clockOccurrences.reset();
  EXPECT_FALSE(context->clockOccurrences);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ReplaceableTimingEventTeardownIsTokenLocalAndRejectsInvalidOwners) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->clockOccurrences = std::make_unique<ClockOccurrenceFeatureState>();
  context->clockOccurrences->waits.try_emplace(31);
  context->clockOccurrences->waits.try_emplace(32);

  // Coordinator A owns a permanently saturated deadline while coordinator B
  // owns an ordinary live deadline. Both records share the cold calendar but
  // retain independent clock-wait lifetimes.
  context->activeLogicalProcessToken = 31;
  talos_rt_v1_scheduler_event_replace_after(context, 301, 1, UINT64_MAX);
  ASSERT_EQ(context->schedulerStatus, TALOS_RT_OK);
  context->activeLogicalProcessToken = 32;
  talos_rt_v1_scheduler_event_replace_after(context, 302, 1, 5);
  ASSERT_EQ(context->schedulerStatus, TALOS_RT_OK);
  auto &feature = *context->clockOccurrences->replaceableEvents;
  ASSERT_EQ(feature.calendar.size(), 2u);
  ASSERT_EQ(feature.pending.size(), 2u);
  ASSERT_EQ(feature.ownedTimers.size(), 2u);

  // The compiler-private service is valid only in the active coordinator
  // that owns a registered clock-occurrence wait. A second coordinator may
  // not replace another coordinator's static timer event.
  context->activeLogicalProcessToken = 0;
  talos_rt_v1_scheduler_event_replace_after(context, 303, 1, 1);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_INVALID_LIFECYCLE);
  context->schedulerStatus = TALOS_RT_OK;
  context->activeLogicalProcessToken = 33;
  talos_rt_v1_scheduler_event_replace_after(context, 303, 1, 1);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_INVALID_LIFECYCLE);
  context->schedulerStatus = TALOS_RT_OK;
  context->activeLogicalProcessToken = 32;
  talos_rt_v1_scheduler_event_replace_after(context, 301, 1, 1);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_INVALID_LIFECYCLE);
  context->schedulerStatus = TALOS_RT_OK;
  EXPECT_EQ(feature.calendar.size(), 2u);
  EXPECT_EQ(feature.pending.size(), 2u);

  std::vector<std::unique_ptr<SignalSubscription>> noSubscriptions;
  talos_rt_unregister_signal_wait_unlocked(context, noSubscriptions, 31,
                                             true);
  ASSERT_TRUE(context->clockOccurrences);
  ASSERT_TRUE(context->clockOccurrences->replaceableEvents);
  auto &remaining = *context->clockOccurrences->replaceableEvents;
  EXPECT_EQ(remaining.calendar.size(), 1u);
  EXPECT_EQ(remaining.pending.size(), 1u);
  EXPECT_EQ(remaining.pending.count(301), 0u);
  EXPECT_EQ(remaining.pending.count(302), 1u);
  EXPECT_EQ(remaining.ownedTimers.count(31), 0u);
  EXPECT_EQ(remaining.ownedTimers.count(32), 1u);
  ASSERT_EQ(remaining.calendar.begin()->second.stableID, 302u);

  // A's UINT64_MAX node was physically removed, so it can neither retain
  // state nor mature after teardown. B remains scheduled and fires normally.
  context->activeLogicalProcessToken = 0;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->schedulerTime, 5u);
  EXPECT_EQ(context->events.count(301), 0u);
  ASSERT_EQ(context->events.count(302), 1u);
  EXPECT_NE(context->events.at(302).generation, 0u);
  EXPECT_TRUE(remaining.calendar.empty());
  EXPECT_EQ(remaining.pending.size(), 1u);

  talos_rt_unregister_signal_wait_unlocked(context, noSubscriptions, 32,
                                             true);
  EXPECT_FALSE(context->clockOccurrences);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceWaitReuseComparesCompleteNativeAndDesignRecords) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  struct ClockWait {
    talos_rt_wait_record_v1 wait;
    std::array<talos_rt_wait_entry_v1, 4> entries;
  } record{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE,
            TALOS_RT_WAIT_CLOCK_OCCURRENCE, 4, 81, 3},
           {{{16, TALOS_RT_WAIT_EDGE_POSEDGE, 1},
             {32, TALOS_RT_WAIT_EDGE_NEGEDGE, 1},
             {48, TALOS_RT_WAIT_EDGE_NONE, 1},
             {64, TALOS_RT_WAIT_EDGE_NONE, 1}}}};

  auto exercise = [&](uint64_t token, bool design) {
    std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
    std::unique_ptr<SignalWaitLatch> latch;
    ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
        context, &record.wait, subscriptions, latch, token, design));
    ASSERT_TRUE(talos_rt_same_clock_occurrence_wait_unlocked(
        context, &record.wait, token, design));

    ClockWait changed = record;
    ++changed.wait.payload;
    EXPECT_FALSE(talos_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));
    changed = record;
    changed.wait.auxiliary = 1;
    EXPECT_FALSE(talos_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));
    changed = record;
    changed.entries[0].reserved = 2;
    EXPECT_FALSE(talos_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));
    changed = record;
    changed.entries[1].edge = TALOS_RT_WAIT_EDGE_POSEDGE;
    EXPECT_FALSE(talos_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));
    changed = record;
    changed.entries[3].stable_id = 80;
    EXPECT_FALSE(talos_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));
    changed = record;
    changed.entries[2].reserved = 2;
    EXPECT_FALSE(talos_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));

    talos_rt_unregister_signal_wait_unlocked(context, subscriptions, token,
                                               design);
    EXPECT_FALSE(talos_rt_same_clock_occurrence_wait_unlocked(
        context, &record.wait, token, design));
    EXPECT_FALSE(context->clockOccurrences);
    ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
        context, 16, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
    EXPECT_EQ(context->schedulerStatus, TALOS_RT_OK);
    EXPECT_FALSE(context->clockOccurrences);
    ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
        context, &record.wait, subscriptions, latch, token, design));
    EXPECT_TRUE(talos_rt_same_clock_occurrence_wait_unlocked(
        context, &record.wait, token, design));
    talos_rt_unregister_signal_wait_unlocked(context, subscriptions, token,
                                               design);
    EXPECT_FALSE(context->clockOccurrences);
  };
  exercise(21, false);
  exercise(22, true);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ClockOccurrenceWaitAccepts64AndRejects65Clocks) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  struct ClockWait {
    talos_rt_wait_record_v1 wait;
    std::array<talos_rt_wait_entry_v1, 65> entries;
  } record{};
  record.wait = {TALOS_RT_VERSION,
                 TALOS_RT_SUSPEND_EDGE,
                 TALOS_RT_WAIT_CLOCK_OCCURRENCE,
                 64,
                 82,
                 0};
  for (auto &entry : record.entries)
    entry = {16, TALOS_RT_WAIT_EDGE_POSEDGE, 1};

  auto exercise = [&](uint64_t token, bool design) {
    std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
    std::unique_ptr<SignalWaitLatch> latch;
    ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
        context, &record.wait, subscriptions, latch, token, design));
    ASSERT_TRUE(context->clockOccurrences);
    EXPECT_EQ(
        context->clockOccurrences->subscriptions
            .at(design ? token : TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | token)
            .size(),
        64u);
    talos_rt_unregister_signal_wait_unlocked(context, subscriptions, token,
                                               design);
    EXPECT_FALSE(context->clockOccurrences);

    record.wait.count = 65;
    EXPECT_FALSE(talos_rt_register_signal_wait_unlocked(
        context, &record.wait, subscriptions, latch, token, design));
    EXPECT_FALSE(context->clockOccurrences);
    record.wait.count = 64;
  };
  exercise(31, false);
  exercise(32, true);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ClockOccurrenceIffSamplesCommittedPublicationValue) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->stateValue.assign(2, 0);
  context->stateUnknown.assign(2, 0);

  struct ClockWait {
    talos_rt_wait_record_v1 wait;
    std::array<talos_rt_wait_entry_v1, 3> entries;
  } record{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE,
            TALOS_RT_WAIT_CLOCK_OCCURRENCE, 3, 73, 1},
           {{{16, TALOS_RT_WAIT_EDGE_POSEDGE, 1},
             {32, TALOS_RT_WAIT_EDGE_POSEDGE, 1},
             {80, TALOS_RT_WAIT_EDGE_NONE, 1}}}};

  ScheduledDesignTask task;
  task.id = 13;
  task.started = true;
  task.suspendKind = TALOS_RT_SUSPEND_EDGE;
  context->scheduledDesignTaskIndices[task.id] = 0;
  context->scheduledDesignTasks.push_back(std::move(task));
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &record.wait,
      context->scheduledDesignTasks.front().signalSubscriptions,
      context->scheduledDesignTasks.front().signalLatch, 13, true));
  ASSERT_TRUE(context->clockOccurrences);
  EXPECT_EQ(context->clockOccurrences->conditionalWaitCount, 1u);

  context->activeLogicalProcessToken = 13;
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 73), 2u);

  context->stateValue[1] |= uint64_t{1} << 16;
  ASSERT_TRUE(talos_rt_append_signal_event_unlocked(context, 80, false, false,
                                                      true, false));
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 73), 3u);

  talos_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks.front().signalSubscriptions, 13,
      true);
  EXPECT_FALSE(context->clockOccurrences);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceObserverConditionIsActorLocalAndCaptureExact) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);

  struct ObserverClockWait {
    talos_rt_wait_record_v1 wait;
    std::array<talos_rt_wait_entry_v1, 2> entries;
    talos_rt_computed_capture_v1 capture;
  } record{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE,
            TALOS_RT_WAIT_CLOCK_OCCURRENCE |
                TALOS_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS,
            2, 97, 1},
           {{{16, TALOS_RT_WAIT_EDGE_POSEDGE, 1},
             {71, TALOS_RT_WAIT_CONDITION_OBSERVER, 1}}},
           {32, 3, 5, 7}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &record.wait, subscriptions, latch, 19, false));
  ASSERT_TRUE(context->clockOccurrences);
  EXPECT_TRUE(context->signalSubscriptionBuckets.empty());
  EXPECT_EQ(context->nativeDynamicSignalSubscriptions, 0u);
  EXPECT_TRUE(context->pendingNativeComputedWaiters.empty());
  EXPECT_TRUE(context->nativeConditionalSignalWaiters.empty());
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 0u);
  EXPECT_TRUE(talos_rt_same_clock_occurrence_wait_unlocked(
      context, &record.wait, 19, false));

  // IEEE 1800-2017 31.7 samples the compiled condition only after the
  // controlled event matches. Its captures therefore belong to this exact
  // actor-local clock wait: they are neither subscriptions nor a reusable
  // wait when any ABI word differs.
  const talos_rt_computed_capture_v1 original = record.capture;
  auto differs = [&](uint64_t &word) {
    ++word;
    EXPECT_FALSE(talos_rt_same_clock_occurrence_wait_unlocked(
        context, &record.wait, 19, false));
    record.capture = original;
  };
  differs(record.capture.stable_id);
  differs(record.capture.payload0);
  differs(record.capture.payload1);
  differs(record.capture.payload2);

  talos_rt_unregister_signal_wait_unlocked(context, subscriptions, 19, false);
  EXPECT_FALSE(context->clockOccurrences);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     StaticEvalIslandRejectsOnlyGeneratedClockOccurrencePrimaries) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->clockOccurrences = std::make_unique<ClockOccurrenceFeatureState>();

  constexpr uint64_t token = 29;
  constexpr uint32_t generatedState = 7;
  constexpr uint32_t dormantState = 8;
  auto &wait = context->clockOccurrences->waits[token];
  wait.conditions.resize(1);
  wait.conditions[0].stableID = talos_rt_stable_handle_encode(
      TALOS_RT_STABLE_HANDLE_STATIC, generatedState, 0);
  auto primary = std::make_unique<ClockOccurrenceSubscription>();
  primary->stableID = talos_rt_stable_handle_encode(
      TALOS_RT_STABLE_HANDLE_STATIC, dormantState, 0);
  primary->bitWidth = 1;
  context->clockOccurrences->subscriptions[token].push_back(std::move(primary));

  std::unordered_set<uint32_t> generatedWritableStates{generatedState};
  // Clause 31.7 condition reads do not wake the coordinator. A condition-only
  // overlap with generated state therefore leaves the dormant primary safe.
  EXPECT_FALSE(nativeClockOccurrencePrimaryReadsGeneratedState(
      context, generatedWritableStates));

  context->clockOccurrences->subscriptions[token].front()->stableID =
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_STATIC,
                                      generatedState, 0);
  EXPECT_TRUE(nativeClockOccurrencePrimaryReadsGeneratedState(
      context, generatedWritableStates));

  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ClockConditionPublicationViewMergesOnlyCapturedOverlap) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 64;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->clockOccurrences = std::make_unique<ClockOccurrenceFeatureState>();
  context->observerForcesCanonicalPlane = true;

  uint8_t publishedValue = 0xa5;
  uint8_t publishedUnknown = 0x03;
  ClockConditionPublicationView publication{32, uint64_t{1} << 40, 0,
                                            &publishedValue, &publishedUnknown};
  context->conditionPublication = &publication;
  uint8_t globalPlane[8] = {};
  uint8_t value = 0xff;
  uint8_t unknown = 0xff;
  // A huge unrelated publication must not be copied, walked, or even require
  // storage proportional to its declared width. The captured one-bit load is
  // resolved wholly outside the publication interval.
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(context, globalPlane, 64, 16,
                                                  1, 0, 0, &value),
            TALOS_RT_OK);
  EXPECT_EQ(value, 0);

  // IEEE 1800-2017 31.7 instead exposes the post-transition value and X plane
  // when the condition capture aliases the controlled publication.
  publication = {16, 8, 0, &publishedValue, &publishedUnknown};
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(context, globalPlane, 64, 16,
                                                  8, 0, 0, &value),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(context, globalPlane, 64, 16,
                                                  8, 1, 0, &unknown),
            TALOS_RT_OK);
  EXPECT_EQ(value, publishedValue);
  EXPECT_EQ(unknown, publishedUnknown);

  // A negative view base must not hide its in-bounds publication overlap.
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 64),
            TALOS_RT_OK);
  context->stateUnknown[0] = 0;
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  publication.stableID = talos_rt_v1_native_handle_offset(root, 16);
  std::array<uint8_t, 4> partialValue{}, partialUnknown{};
  uint64_t partial = talos_rt_v1_native_handle_offset(root, -3);
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(
                context, globalPlane, 64, partial, 32, 0, 0,
                partialValue.data()),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(
                context, globalPlane, 64, partial, 32, 1, 1,
                partialUnknown.data()),
            TALOS_RT_OK);
  EXPECT_EQ(loadPackedBytes(partialValue.data(), 0, 32),
            uint64_t{publishedValue} << 19);
  EXPECT_EQ(loadPackedBytes(partialUnknown.data(), 0, 32),
            (uint64_t{publishedUnknown} << 19) | 7);

  context->conditionPublication = nullptr;
  context->observerForcesCanonicalPlane = false;
  context->clockOccurrences.reset();
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceCustomDescriptorUsesOnlyLazyFeatureStateAndCoalesces) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->stateValue.assign(1, 0);
  context->stateUnknown.assign(1, 0);

  struct {
    talos_rt_wait_record_v1 wait;
    talos_rt_wait_entry_v1 entry;
  } record{
      {TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE,
       TALOS_RT_WAIT_CLOCK_OCCURRENCE, 1, 74, 0},
      {16, TALOS_RT_WAIT_EDGE_TRANSITION_MASK | (1u << 0) | (1u << 3), 4}};
  ScheduledDesignTask task;
  task.id = 14;
  task.started = true;
  task.suspendKind = TALOS_RT_SUSPEND_EDGE;
  context->scheduledDesignTaskIndices[task.id] = 0;
  context->scheduledDesignTasks.push_back(std::move(task));
  context->activeLogicalProcessToken = 14;
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &record.wait,
      context->scheduledDesignTasks.front().signalSubscriptions,
      context->scheduledDesignTasks.front().signalLatch, 14, true));
  ASSERT_TRUE(context->clockOccurrences);
  EXPECT_EQ(context->nativeDynamicSignalSubscriptions, 0u);

  // Four matching 01 bit transitions are one Clause 31.8 vector occurrence.
  uint8_t changed = 0xf;
  uint8_t posedge = 0xf;
  uint8_t negedge = 0;
  context->stateValue[0] |= uint64_t{0xf} << 16;
  ASSERT_TRUE(talos_rt_publish_signal_transition_batch_unlocked(
      context, 16, 4, &changed, &posedge, &negedge, 0, nullptr));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 74), 1u);

  // 1x is the other selected Clause 31.5 class.
  context->stateUnknown[0] |= uint64_t{0xf} << 16;
  posedge = 0;
  ASSERT_TRUE(talos_rt_publish_signal_transition_batch_unlocked(
      context, 16, 4, &changed, &posedge, &negedge, 0, nullptr));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 74), 1u);

  talos_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks.front().signalSubscriptions, 14,
      true);
  EXPECT_FALSE(context->clockOccurrences);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceCustomDescriptorRollsBackPartialRegistration) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->stateValue.assign(1, 0);
  context->stateUnknown.assign(1, 0);
  struct {
    talos_rt_wait_record_v1 wait;
    talos_rt_wait_entry_v1 entries[2];
  } record{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE,
            TALOS_RT_WAIT_CLOCK_OCCURRENCE, 2, 75, 0},
           {{16, TALOS_RT_WAIT_EDGE_TRANSITION_MASK | 1u, 4},
            {UINT64_MAX, TALOS_RT_WAIT_EDGE_TRANSITION_MASK | 1u, 4}}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;

  EXPECT_FALSE(talos_rt_register_signal_wait_unlocked(
      context, &record.wait, subscriptions, latch, 15, false));
  EXPECT_TRUE(subscriptions.empty());
  EXPECT_EQ(context->nativeDynamicSignalSubscriptions, 0u);
  EXPECT_FALSE(context->clockOccurrences);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceWideCustomDescriptorCoalescesPartialPageOverlapAndCleans) {
  constexpr uint64_t width = 65 * kSignalSubscriptionPageBits;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->stateValue.assign((width + 63) / 64, 0);
  context->stateUnknown.assign((width + 63) / 64, 0);
  struct {
    talos_rt_wait_record_v1 wait;
    talos_rt_wait_entry_v1 entry;
  } record{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE,
            TALOS_RT_WAIT_CLOCK_OCCURRENCE, 1, 76, 0},
           {0, TALOS_RT_WAIT_EDGE_TRANSITION_MASK | 1u,
            static_cast<uint32_t>(width)}};
  ScheduledDesignTask task;
  task.id = 16;
  task.started = true;
  task.suspendKind = TALOS_RT_SUSPEND_EDGE;
  context->scheduledDesignTaskIndices[task.id] = 0;
  context->scheduledDesignTasks.push_back(std::move(task));
  context->activeLogicalProcessToken = 16;
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &record.wait,
      context->scheduledDesignTasks.front().signalSubscriptions,
      context->scheduledDesignTasks.front().signalLatch, 16, true));
  ASSERT_TRUE(context->clockOccurrences);
  EXPECT_EQ(context->clockOccurrences->subscriptionBuckets.size(), 1u);

  // This three-bit publication straddles the indexed page boundary. Two 01
  // bits still form one Clause 31.8 occurrence for the wide subscription.
  uint8_t changed = 0x5;
  uint8_t posedge = 0x5;
  uint8_t negedge = 0;
  context->stateValue[255 / 64] |= uint64_t{1} << (255 % 64);
  context->stateValue[257 / 64] |= uint64_t{1} << (257 % 64);
  ASSERT_TRUE(talos_rt_publish_signal_transition_batch_unlocked(
      context, 255, 3, &changed, &posedge, &negedge, 0, nullptr));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(talos_rt_v1_clock_occurrence_consume(context, 76), 1u);

  talos_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks.front().signalSubscriptions, 16,
      true);
  EXPECT_FALSE(context->clockOccurrences);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, DirectSignalWaitCanSuppressActiveSelfPublication) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);

  struct {
    talos_rt_wait_record_v1 wait;
    talos_rt_wait_entry_v1 entry;
  } record{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE,
            TALOS_RT_WAIT_SUPPRESS_ACTIVE_SELF, 1, 0, 0},
           {16, TALOS_RT_WAIT_EDGE_CHANGE, 1}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;
  constexpr uint64_t token = 7;
  ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(
      context, &record.wait, subscriptions, latch, token, false));

  context->activeLogicalProcessToken =
      TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | token;
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE));
  EXPECT_FALSE(latch->triggered);
  EXPECT_EQ(context->nativePollCandidates.count(token), 0u);

  context->activeLogicalProcessToken = 0;
  ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, TALOS_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(latch->triggered);
  EXPECT_EQ(context->nativePollCandidates.count(token), 1u);

  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ConditionalWakeUpdatesSchedulerSelectionGeneration) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);

  constexpr uint32_t automaticID = 3;
  uint64_t watched = talos_rt_stable_handle_encode(
      TALOS_RT_STABLE_HANDLE_AUTOMATIC, automaticID, 0);
  uint64_t condition = talos_rt_stable_handle_encode(
      TALOS_RT_STABLE_HANDLE_AUTOMATIC, automaticID, 1);
  ASSERT_NE(watched, UINT64_MAX);
  ASSERT_NE(condition, UINT64_MAX);
  NativeAutomaticState state;
  state.bitWidth = 2;
  state.value = {0b10};
  state.unknown = {0};
  context->nativeAutomaticStates.emplace(automaticID, std::move(state));

  ScheduledDesignTask task;
  task.id = 11;
  task.started = true;
  task.suspendKind = TALOS_RT_SUSPEND_EDGE;
  task.waitOffset = 0;
  task.waitSize =
      sizeof(talos_rt_wait_record_v1) + 2 * sizeof(talos_rt_wait_entry_v1);
  task.frame.resize(task.waitSize);
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(task.frame.data());
  *wait = {TALOS_RT_VERSION,
           TALOS_RT_SUSPEND_EDGE,
           TALOS_RT_WAIT_EDGE_IFF,
           2,
           0,
           0};
  auto *entries = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  entries[0] = {watched, TALOS_RT_WAIT_EDGE_POSEDGE, 1};
  entries[1] = {condition, TALOS_RT_WAIT_EDGE_NONE, 1};
  context->scheduledDesignTasks.push_back(std::move(task));
  context->scheduledDesignTaskIndices[11] = 0;
  context->designConditionalSignalWaiters.insert(11);

  uint64_t selectionGeneration = context->schedulerSelectionGeneration;
  ASSERT_TRUE(talos_rt_latch_conditional_signal_waiters_unlocked(
      context, watched, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
  EXPECT_TRUE(context->scheduledDesignTasks.front().signalTriggered);
  EXPECT_EQ(context->designPollCandidates.count(11), 1u);
  EXPECT_NE(context->schedulerSelectionGeneration, selectionGeneration);

  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, NamedEventWaitersExcludeReadyAndTerminatedThreads) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(13);
  schedulerWaitKind = TALOS_RT_SUSPEND_EVENT;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_NONE;
  schedulerWaitHandle = 0; // Zero is a valid executable event identity.
  schedulerResumeCount = 0;
  auto *instance = makeSchedulerInstance(fixture);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  const uint64_t token =
      talos_rt_v1_scheduler_process_token(context, instance);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  std::vector<uint64_t> waiters;
  EXPECT_EQ(talos_rt_named_event_waiters(context, 0, waiters), TALOS_RT_OK);
  EXPECT_EQ(waiters, (std::vector<uint64_t>{token}));
  EXPECT_EQ(talos_rt_named_event_waiters(context, 1, waiters), TALOS_RT_OK);
  EXPECT_TRUE(waiters.empty());
  EXPECT_EQ(talos_rt_named_event_waiters(context, UINT64_MAX, waiters),
            TALOS_RT_OK);
  EXPECT_TRUE(waiters.empty());

  talos_rt_process_control_disposition disposition{};
  ASSERT_EQ(talos_rt_v1_process_control(context, token,
                                          TALOS_RT_PROCESS_CONTROL_SUSPEND,
                                          &disposition),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_named_event_waiters(context, 0, waiters), TALOS_RT_OK);
  EXPECT_EQ(waiters, (std::vector<uint64_t>{token}));
  talos_rt_v1_scheduler_event(context, 0, 0);
  // Readiness, rather than dispatch or explicit suspension, ends the event
  // wait.
  EXPECT_EQ(talos_rt_named_event_waiters(context, 0, waiters), TALOS_RT_OK);
  EXPECT_TRUE(waiters.empty());
  EXPECT_EQ(schedulerResumeCount, 0u);
  EXPECT_EQ(context->events.at(0).generation, 1u);
  ASSERT_EQ(talos_rt_v1_process_control(
                context, token, TALOS_RT_PROCESS_CONTROL_KILL, &disposition),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_named_event_waiters(context, 0, waiters), TALOS_RT_OK);
  EXPECT_TRUE(waiters.empty());
  waiters.push_back(token);
  EXPECT_EQ(talos_rt_named_event_waiters(nullptr, 0, waiters),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_TRUE(waiters.empty());
  talos_rt_v1_context_destroy(context);
}

struct NamedEventOrderWait {
  talos_rt_wait_record_v1 wait{};
  std::array<talos_rt_wait_entry_v1, 3> entries{};
};

talos_rt_status
namedEventOrderExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !instance->frame ||
      instance->frame_size < sizeof(NamedEventOrderWait))
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  if (instance->continuation != 0) {
    *instance->action = {
        TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return TALOS_RT_OK;
  }
  auto &record = *static_cast<NamedEventOrderWait *>(instance->frame);
  record.wait = {
      TALOS_RT_VERSION, TALOS_RT_SUSPEND_EVENT_ORDER, 0, 3, 0, 0};
  record.entries = {{{41, TALOS_RT_WAIT_EDGE_NONE, 0},
                     {42, TALOS_RT_WAIT_EDGE_NONE, 0},
                     {43, TALOS_RT_WAIT_EDGE_NONE, 0}}};
  *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                       TALOS_RT_SUSPEND_EVENT_ORDER,
                       1,
                       TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       sizeof(NamedEventOrderWait)};
  return TALOS_RT_OK;
}

TEST(Scheduler, NamedEventWaitersTrackOnlyRemainingEventOrder) {
  for (bool failOrder : {false, true}) {
    SCOPED_TRACE(failOrder);
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    SchedulerFixture fixture(14);
    fixture.fields[0].size = sizeof(NamedEventOrderWait);
    fixture.layout.field_count = 1;
    fixture.layout.checksum = checksum(fixture.layout);
    fixture.descriptor.native_execute = namedEventOrderExecute;
    auto *instance = makeSchedulerInstance(fixture);
    ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
    const uint64_t token =
        talos_rt_v1_scheduler_process_token(context, instance);
    ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
    std::vector<uint64_t> waiters;
    for (uint64_t event : {41, 42, 43}) {
      EXPECT_EQ(talos_rt_named_event_waiters(context, event, waiters),
                TALOS_RT_OK);
      EXPECT_EQ(waiters, (std::vector<uint64_t>{token}));
    }
    talos_rt_v1_scheduler_event(context, 41, 0);
    EXPECT_EQ(talos_rt_named_event_waiters(context, 41, waiters),
              TALOS_RT_OK);
    EXPECT_TRUE(waiters.empty());
    for (uint64_t event : {42, 43}) {
      EXPECT_EQ(talos_rt_named_event_waiters(context, event, waiters),
                TALOS_RT_OK);
      EXPECT_EQ(waiters, (std::vector<uint64_t>{token}));
    }
    if (!failOrder)
      talos_rt_v1_scheduler_event(context, 42, 0);
    talos_rt_v1_scheduler_event(context, 43, 0);
    for (uint64_t event : {41, 42, 43}) {
      EXPECT_EQ(talos_rt_named_event_waiters(context, event, waiters),
                TALOS_RT_OK);
      EXPECT_TRUE(waiters.empty());
    }
    EXPECT_EQ(context->scheduledProcesses.front().waitOrderFailed, failOrder);
    ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
    EXPECT_TRUE(context->scheduledProcesses.empty());
    talos_rt_v1_context_destroy(context);
  }
}

struct NamedEventConditionalWait {
  talos_rt_computed_wait_record_v1 wait{};
  std::array<talos_rt_computed_observer_v1, 2> observers{};
  std::array<talos_rt_computed_dependency_v1, 3> dependencies{};
  talos_rt_computed_clause_v1 clause{};
  uint64_t previousValue = 0;
  uint64_t previousUnknown = 0;
};

talos_rt_status
namedEventConditionalExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !instance->frame ||
      instance->frame_size < sizeof(NamedEventConditionalWait))
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  if (instance->continuation != 0) {
    *instance->action = {
        TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return TALOS_RT_OK;
  }
  auto &record = *static_cast<NamedEventConditionalWait *>(instance->frame);
  record = {};
  record.wait = {TALOS_RT_VERSION,
                 TALOS_RT_SUSPEND_OBSERVER,
                 TALOS_RT_COMPUTED_WAIT_INTERLEAVED,
                 1,
                 2,
                 0,
                 3,
                 1,
                 offsetof(NamedEventConditionalWait, observers),
                 offsetof(NamedEventConditionalWait, dependencies),
                 offsetof(NamedEventConditionalWait, dependencies),
                 offsetof(NamedEventConditionalWait, clause),
                 offsetof(NamedEventConditionalWait, previousValue),
                 0,
                 sizeof(NamedEventConditionalWait),
                 0};
  record.observers[0] = {
      eventOnlyComputedObserverID,
      0,
      0,
      0,
      2,
      static_cast<uint32_t>(offsetof(NamedEventConditionalWait, previousValue)),
      0};
  record.observers[1] = {
      eventOnlyComputedObserverID, 0, 0, 2, 1, UINT32_MAX, 0};
  record.dependencies = {{{91, TALOS_RT_OBSERVER_DEPENDENCY_EVENT, 1},
                          {91, TALOS_RT_OBSERVER_DEPENDENCY_EVENT, 1},
                          {92, TALOS_RT_OBSERVER_DEPENDENCY_EVENT, 1}}};
  // Computed clauses carry an ordinary edge kind; EVENT_PRIMARY makes a
  // named-event occurrence trigger independently of a value transition.
  record.clause = {0, 1, TALOS_RT_WAIT_EDGE_CHANGE,
                   TALOS_RT_COMPUTED_CLAUSE_EVENT_PRIMARY};
  *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                       TALOS_RT_SUSPEND_OBSERVER,
                       1,
                       TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       sizeof(NamedEventConditionalWait)};
  return TALOS_RT_OK;
}

TEST(Scheduler, NamedEventWaitersIgnoreObserverConditionsAndDeduplicate) {
  talos_rt_observer_descriptor_v1 observer{eventOnlyComputedObserverID,
                                             nullptr,
                                             0,
                                             1,
                                             0,
                                             TALOS_RT_OBSERVER_NO_BYTECODE,
                                             eventOnlyComputedEvaluator,
                                             0};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.observers = &observer;
  execution.observer_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  EventOnlyComputedFixture fixture(15);
  fixture.fields[0].size = sizeof(NamedEventConditionalWait);
  fixture.layout.frame_size = sizeof(NamedEventConditionalWait);
  fixture.layout.checksum = checksum(fixture.layout);
  fixture.descriptor.execution = &execution;
  fixture.descriptor.native_execute = namedEventConditionalExecute;
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  const uint64_t token =
      talos_rt_v1_scheduler_process_token(context, instance);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  std::vector<uint64_t> waiters;
  EXPECT_EQ(talos_rt_named_event_waiters(context, 91, waiters),
            TALOS_RT_OK);
  EXPECT_EQ(waiters, (std::vector<uint64_t>{token}));
  EXPECT_EQ(talos_rt_named_event_waiters(context, 92, waiters),
            TALOS_RT_OK);
  EXPECT_TRUE(waiters.empty());
  talos_rt_v1_scheduler_event(context, 92, 0);
  EXPECT_EQ(talos_rt_named_event_waiters(context, 91, waiters),
            TALOS_RT_OK);
  EXPECT_EQ(waiters, (std::vector<uint64_t>{token}));
  const auto &scheduled = context->scheduledProcesses.front();
  EXPECT_FALSE(scheduled.signalTriggered);
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 1u);
  // A corrupt frame is rejected without publishing a partial waiter snapshot.
  auto &record = *static_cast<NamedEventConditionalWait *>(instance->frame);
  record.observers[0].dependency_count = UINT32_MAX;
  EXPECT_EQ(talos_rt_named_event_waiters(context, 91, waiters),
            TALOS_RT_INVALID_FRAME);
  EXPECT_TRUE(waiters.empty());
  record.observers[0].dependency_count = 2;
  talos_rt_v1_scheduler_event(context, 91, 0);
  EXPECT_EQ(talos_rt_named_event_waiters(context, 91, waiters),
            TALOS_RT_OK);
  EXPECT_TRUE(waiters.empty());
  EXPECT_TRUE(context->scheduledProcesses.front().signalTriggered);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, SignalWaitsAreSelectiveAndEdgeAware) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(1);
  schedulerWaitKind = TALOS_RT_SUSPEND_EDGE;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_POSEDGE;
  schedulerWaitHandle = 16;
  schedulerWaitWidth = 8;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  talos_rt_v1_scheduler_signal(context, 80, 1, TALOS_RT_SIGNAL_POSEDGE);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);
  talos_rt_v1_scheduler_signal(context, 18, 1, TALOS_RT_SIGNAL_CHANGE);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);
  talos_rt_v1_scheduler_signal(
      context, 18, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ClockOccurrenceWaitExitReclaimsFeatureState) {
  for (bool changesWait : {false, true}) {
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    ClockOccurrenceLifecycleFixture fixture(changesWait ? 81 : 80);
    clockOccurrenceLifecycleChangesWait = changesWait;
    clockOccurrenceLifecycleResumes = 0;
    ASSERT_EQ(talos_rt_v1_scheduler_add(
                  context, makeClockOccurrenceLifecycleInstance(fixture), 0),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
    ASSERT_TRUE(context->clockOccurrences);
    ASSERT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(),
              1u);
    ASSERT_FALSE(
        context->scheduledProcesses.front().signalSubscriptions.front());

    context->activeLogicalProcessToken =
        TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG |
        context->scheduledProcesses.front().token;
    talos_rt_v1_scheduler_event_replace_after(context, 191, 1, 100);
    context->activeLogicalProcessToken = 0;
    ASSERT_TRUE(context->clockOccurrences->replaceableEvents);
    ASSERT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(),
              1u);

    ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
        context, 16, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
    ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
        context, 32, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
    ++context->schedulerSlotProgress;
    ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
    EXPECT_EQ(clockOccurrenceLifecycleResumes, 1u);
    if (!changesWait) {
      // Re-entering the identical wait reuses the exact token/site record.
      ASSERT_TRUE(context->clockOccurrences);
      ASSERT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(),
                1u);
      ASSERT_FALSE(
          context->scheduledProcesses.front().signalSubscriptions.front());
      ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
          context, 16, 1,
          TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
      ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
          context, 32, 1,
          TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
      ++context->schedulerSlotProgress;
      ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
      EXPECT_EQ(clockOccurrenceLifecycleResumes, 2u);
    }
    EXPECT_FALSE(context->clockOccurrences);

    // The old sources are no longer subscribed after either termination or a
    // change to an ordinary direct wait. Publishing them again is inert and
    // cannot resurrect a stale feature record.
    ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
        context, 16, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
    ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
        context, 32, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
    ++context->schedulerSlotProgress;
    ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
    EXPECT_FALSE(context->clockOccurrences);
    if (changesWait) {
      ASSERT_EQ(context->scheduledProcesses.size(), 1u);
      EXPECT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(),
                1u);
      ASSERT_TRUE(talos_rt_publish_signal_occurrence_unlocked(
          context, 48, 1,
          TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE));
      ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
      EXPECT_EQ(clockOccurrenceLifecycleResumes, 2u);
    }
    EXPECT_TRUE(context->scheduledProcesses.empty());
    EXPECT_FALSE(context->clockOccurrences);
    talos_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, PrimeFindsTailAndNonTailActors) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture first(1);
  SchedulerFixture second(2);
  schedulerWaitKind = TALOS_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = 700;
  schedulerWaitWidth = 1;
  talos_rt_process_instance_v1 *firstInstance = makeSchedulerInstance(first);
  talos_rt_process_instance_v1 *secondInstance =
      makeSchedulerInstance(second);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, firstInstance, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, secondInstance, 0),
            TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_scheduler_prime(context, firstInstance),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_prime(context, secondInstance),
            TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledProcesses[0].started);
  EXPECT_TRUE(context->scheduledProcesses[1].started);
  EXPECT_EQ(context->scheduledProcesses[0].suspendKind,
            TALOS_RT_SUSPEND_CHANGE);
  EXPECT_EQ(context->scheduledProcesses[1].suspendKind,
            TALOS_RT_SUSPEND_CHANGE);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSignalCohortPreservesOrderingChildrenAndCompaction) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  cachedCohortPrimarySignal = 710;
  cachedCohortSecondarySignal = 711;
  cachedCohortSecondaryID = 0;
  cachedCohortSpawnID = 1;
  cachedCohortPublishID = 0;
  cachedCohortNBAPlane = nullptr;
  cachedCohortNBAVisible = true;
  schedulerOrder.clear();

  SchedulerFixture ordinaryChild(101);
  SchedulerFixture urgentChild(100);
  cachedCohortOrdinaryChild = &ordinaryChild.descriptor;
  cachedCohortUrgentChild = &urgentChild.descriptor;
  std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
  for (uint64_t id = 1; id <= 20; ++id) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
    ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  id == 1 ? 0 : static_cast<uint32_t>(id)),
              TALOS_RT_OK);
  }
  for (uint64_t id : {uint64_t{30}, uint64_t{31}}) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
    ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()),
                  TALOS_RT_SCHEDULE_PRIORITY_SIGNAL, 100),
              TALOS_RT_OK);
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_TRUE(schedulerOrder.empty());

  talos_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 TALOS_RT_SIGNAL_CHANGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  std::vector<uint64_t> expected{30, 31, 1, 100, 101};
  for (uint64_t id = 2; id <= 20; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  EXPECT_EQ(context->schedulerDeadProcessCount, 0u);
  cachedCohortOrdinaryChild = nullptr;
  cachedCohortUrgentChild = nullptr;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedEqualRankSignalCohortPreservesInsertionOrder) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  cachedCohortPrimarySignal = 715;
  cachedCohortSecondarySignal = 716;
  cachedCohortSecondaryID = 0;
  cachedCohortSpawnID = 0;
  cachedCohortPublishID = 0;
  cachedCohortTaskCallID = 0;
  cachedCohortContinueID = 0;
  cachedCohortFrontierID = 0;
  cachedCohortNBAPlane = nullptr;
  schedulerOrder.clear();

  std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
  for (uint64_t id = 1; id <= 17; ++id) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
    ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0, 0),
              TALOS_RT_OK);
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  talos_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 TALOS_RT_SIGNAL_CHANGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  std::vector<uint64_t> expected;
  for (uint64_t id = 1; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSignalCohortInvalidatesForNewPriorityPublication) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  cachedCohortPrimarySignal = 720;
  cachedCohortSecondarySignal = 721;
  cachedCohortSecondaryID = 40;
  cachedCohortSpawnID = 0;
  cachedCohortPublishID = 1;
  cachedCohortUrgentChild = nullptr;
  cachedCohortOrdinaryChild = nullptr;
  cachedCohortNBAPlane = nullptr;
  schedulerOrder.clear();

  std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
  for (uint64_t id = 1; id <= 17; ++id) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
    ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  static_cast<uint32_t>(id)),
              TALOS_RT_OK);
  }
  fixtures.push_back(std::make_unique<SchedulerFixture>(40));
  fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
  ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(*fixtures.back()),
                TALOS_RT_SCHEDULE_PRIORITY_SIGNAL, 100),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  talos_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 TALOS_RT_SIGNAL_CHANGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  std::vector<uint64_t> expected{1, 40};
  for (uint64_t id = 2; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, PostObservedResumptionsRunTogetherAfterObserved) {
  // IEEE 1800-2023 4.5, 14.10: a clocking block event triggers in Observed. A
  // module process waiting on it runs in the next Active iteration, after the
  // Observed samples whatever their rank, and alongside every other such
  // waiter before the Inactive `#0` of any of them. A program process runs in
  // Reactive.
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  schedulerOrder.clear();
  postObservedDelayed = false;
  std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
  for (auto [id, flags, rank] :
       {std::tuple{uint64_t{4},
                   TALOS_RT_SCHEDULE_HOME(TALOS_RT_REGION_REACTIVE), 0u},
        std::tuple{uint64_t{1}, 0u, 0u}, std::tuple{uint64_t{2}, 0u, 1u},
        std::tuple{uint64_t{3}, 0u, 5u}}) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = postObservedExecute;
    ASSERT_EQ(
        talos_rt_v1_scheduler_add_ranked(
            context, makeSchedulerInstance(*fixtures.back()), flags, rank),
        TALOS_RT_OK);
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_TRUE(schedulerOrder.empty());

  talos_rt_v1_scheduler_signal(context, postObservedSignal, 1,
                                 TALOS_RT_SIGNAL_CHANGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{3, 1, 2, 11, 4}));
  EXPECT_TRUE(context->scheduledProcesses.empty());
  EXPECT_TRUE(context->postObservedResumptions.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, IncrementalSignalCohortOrdersNewWakesAndInvalidatesDisturbances) {
  for (unsigned mode : {0u, 1u, 2u}) {
    SCOPED_TRACE(mode);
    bool disturb = mode == 1;
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    incrementalCohortDisturbRank = disturb;
    SchedulerFixture exported(incrementalCohortSize * 2 + 2);
    exported.descriptor.native_execute =
        +[](talos_rt_process_instance_v1 *instance) -> talos_rt_status {
      instance->native_handle = instance;
      *instance->action = {TALOS_RT_FRAGMENT_TERMINATE,
                           TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
      return TALOS_RT_OK;
    };
    incrementalCohortReentry = mode == 2 ? &exported.descriptor : nullptr;
    schedulerOrder.clear();
    std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
    for (uint64_t id = 1; id <= incrementalCohortSize * 2; ++id) {
      fixtures.push_back(std::make_unique<SchedulerFixture>(id));
      fixtures.back()->descriptor.native_execute = incrementalCohortExecute;
      ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                    context, makeSchedulerInstance(*fixtures.back()), 0,
                    id <= incrementalCohortSize ? static_cast<uint32_t>(id) : 0),
                TALOS_RT_OK);
    }
    // Future timed work remains in the exact slow candidate scan. Its
    // presence must not disable incremental admission of independent wakes.
    SchedulerFixture timer(incrementalCohortSize * 2 + 1);
    timer.descriptor.native_execute =
        +[](talos_rt_process_instance_v1 *instance) -> talos_rt_status {
      instance->native_handle = instance;
      if (instance->continuation != 0)
        return TALOS_RT_INVALID_LIFECYCLE;
      auto *wait =
          reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
      *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_DELAY, 0, 0, 1, 0};
      *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                           TALOS_RT_SUSPEND_DELAY, 1,
                           TALOS_RT_ACTION_FRAME_WAIT_RECORD, 0, 48};
      return TALOS_RT_OK;
    };
    ASSERT_EQ(talos_rt_v1_scheduler_add(
                  context, makeSchedulerInstance(timer), 0),
              TALOS_RT_OK);
    auto drainCurrentSlot = [&] {
      NativeAOTMutexScope lock(context);
      return runScheduler(context, {/*nativePlan=*/false,
                                    /*currentSlotOnly=*/true});
    };
    ASSERT_EQ(drainCurrentSlot(), TALOS_RT_OK);
    ASSERT_TRUE(schedulerOrder.empty());
    context->signalDiagnosticsEnabled = true;
    context->signalDiagnostics.candidateScans = 0;
    talos_rt_v1_scheduler_signal(context, incrementalCohortSignal, 1,
                                   TALOS_RT_SIGNAL_CHANGE);
    ASSERT_EQ(drainCurrentSlot(), TALOS_RT_OK);
    std::vector<uint64_t> expected;
    if (disturb)
      expected = {1, incrementalCohortSize, incrementalCohortSize + 1,
                  incrementalCohortSize * 2};
    for (uint64_t id = disturb ? 2 : 1;
         id <= incrementalCohortSize - (disturb ? 1 : 0); ++id) {
      expected.push_back(id);
      expected.push_back(id + incrementalCohortSize);
    }
    EXPECT_EQ(schedulerOrder, expected);
    // A publication per actor must not repeatedly scan the remaining large
    // cohort. Existing scalar readiness diagnostics verify the intended path.
    if (!disturb) {
      EXPECT_LT(context->signalDiagnostics.candidateScans,
                incrementalCohortSize * 16);
    }
    EXPECT_EQ(context->nativeReadyPublicationBatch, nullptr);
    EXPECT_EQ(context->schedulerTime, 0u);
    talos_rt_v1_context_destroy(context);
  }
  incrementalCohortDisturbRank = false;
  incrementalCohortReentry = nullptr;
}

TEST(Scheduler, CachedReactiveSignalCohortYieldsToSameSlotNBA) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  cachedCohortPrimarySignal = 730;
  cachedCohortSecondarySignal = 731;
  cachedCohortSecondaryID = 0;
  cachedCohortSpawnID = 0;
  cachedCohortPublishID = 0;
  cachedCohortUrgentChild = nullptr;
  cachedCohortOrdinaryChild = nullptr;
  uint8_t plane = 0;
  uint8_t replacement = 0xa5;
  cachedCohortNBAPlane = &plane;
  cachedCohortNBAExpected = replacement;
  cachedCohortNBAVisible = true;
  schedulerOrder.clear();

  std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
  for (uint64_t id = 1; id <= 17; ++id) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
    ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()),
                  TALOS_RT_SCHEDULE_HOME(TALOS_RT_REGION_REACTIVE),
                  static_cast<uint32_t>(id)),
              TALOS_RT_OK);
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_nba(context, &plane, nullptr, 8, 0, 8, 0,
                                        &replacement, nullptr),
            TALOS_RT_OK);
  talos_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 TALOS_RT_SIGNAL_CHANGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_TRUE(cachedCohortNBAVisible);
  EXPECT_EQ(plane, replacement);
  EXPECT_EQ(schedulerOrder.size(), 17u);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  cachedCohortNBAPlane = nullptr;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSignalCohortRequeuesUrgentTaskCallToken) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  cachedCohortPrimarySignal = 740;
  cachedCohortSecondarySignal = 741;
  cachedCohortSecondaryID = 0;
  cachedCohortSpawnID = 0;
  cachedCohortPublishID = 0;
  cachedCohortUrgentChild = nullptr;
  cachedCohortOrdinaryChild = nullptr;
  cachedCohortTaskCallID = 1;
  cachedCohortTaskCalled = false;
  cachedCohortNBAPlane = nullptr;
  schedulerOrder.clear();

  SchedulerFixture callee(150);
  cachedCohortTaskCallee = &callee.descriptor;
  std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
  for (uint64_t id = 1; id <= 17; ++id) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
    ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  static_cast<uint32_t>(id)),
              TALOS_RT_OK);
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  talos_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 TALOS_RT_SIGNAL_CHANGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  std::vector<uint64_t> expected{1, 150, 1};
  for (uint64_t id = 2; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  cachedCohortTaskCallee = nullptr;
  cachedCohortTaskCallID = 0;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedUrgentRequeueRebuildsOldCursorDistances) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  cachedCohortSpawnID = 0;
  cachedCohortPublishID = 0;
  cachedCohortTaskCallID = 1;
  cachedCohortTaskCalled = false;
  cachedCohortContinueID = 0;
  cachedCohortFrontierID = 0;
  cachedCohortNBAPlane = nullptr;
  schedulerDestroyCount = 0;
  schedulerOrder.clear();

  SchedulerFixture callee(150);
  cachedCohortTaskCallee = &callee.descriptor;
  std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
  for (uint64_t id = 1; id <= 17; ++id) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
    talos_rt_process_instance_v1 *instance =
        makeSchedulerInstance(*fixtures.back());
    ASSERT_NE(instance, nullptr);
    instance->continuation = 1;
    ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance,
                                          TALOS_RT_SCHEDULE_STARTUP),
              TALOS_RT_OK);
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  std::vector<uint64_t> expected{1};
  for (uint64_t id = 2; id <= 17; ++id)
    expected.push_back(id);
  expected.push_back(150);
  expected.push_back(1);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_EQ(schedulerDestroyCount, 18u);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  cachedCohortTaskCallee = nullptr;
  cachedCohortTaskCallID = 0;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSignalCohortReadmitsSameTokenContinue) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  cachedCohortPrimarySignal = 750;
  cachedCohortSecondarySignal = 751;
  cachedCohortSecondaryID = 0;
  cachedCohortSpawnID = 0;
  cachedCohortPublishID = 0;
  cachedCohortTaskCallID = 0;
  cachedCohortContinueID = 1;
  cachedCohortContinued = false;
  cachedCohortContinuePublishes = false;
  cachedCohortNBAPlane = nullptr;
  schedulerOrder.clear();

  std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
  for (uint64_t id = 1; id <= 17; ++id) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
    ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  static_cast<uint32_t>(id)),
              TALOS_RT_OK);
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  talos_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 TALOS_RT_SIGNAL_CHANGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  std::vector<uint64_t> expected{1, 1};
  for (uint64_t id = 2; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  cachedCohortContinueID = 0;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSameTokenPublicationInvalidatesBeforeContinue) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  cachedCohortPrimarySignal = 760;
  cachedCohortSecondarySignal = 761;
  cachedCohortSecondaryID = 40;
  cachedCohortSpawnID = 0;
  cachedCohortPublishID = 0;
  cachedCohortTaskCallID = 0;
  cachedCohortContinueID = 1;
  cachedCohortContinued = false;
  cachedCohortContinuePublishes = true;
  cachedCohortNBAPlane = nullptr;
  schedulerOrder.clear();

  std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
  for (uint64_t id = 1; id <= 17; ++id) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
    ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  static_cast<uint32_t>(id)),
              TALOS_RT_OK);
  }
  fixtures.push_back(std::make_unique<SchedulerFixture>(40));
  fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
  ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(*fixtures.back()),
                TALOS_RT_SCHEDULE_PRIORITY_SIGNAL, 100),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  talos_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 TALOS_RT_SIGNAL_CHANGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  std::vector<uint64_t> expected{1, 40, 1};
  for (uint64_t id = 2; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  cachedCohortContinueID = 0;
  cachedCohortContinuePublishes = false;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSameTokenWaitMovesToSlowUntilEpochReady) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  cachedCohortPrimarySignal = 770;
  cachedCohortSecondarySignal = 771;
  cachedCohortSecondaryID = 0;
  cachedCohortSpawnID = 0;
  cachedCohortPublishID = 0;
  cachedCohortTaskCallID = 0;
  cachedCohortContinueID = 0;
  cachedCohortFrontierID = 1;
  cachedCohortFrontierSuspended = false;
  cachedCohortNBAPlane = nullptr;
  schedulerOrder.clear();

  std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
  for (uint64_t id = 1; id <= 17; ++id) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
    ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  static_cast<uint32_t>(id)),
              TALOS_RT_OK);
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  talos_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 TALOS_RT_SIGNAL_CHANGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  std::vector<uint64_t> expected{1, 2, 1};
  for (uint64_t id = 3; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  cachedCohortFrontierID = 0;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, SignalChangedWhileExecutingRetriggersTheSameWait) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(42);
  fixture.descriptor.native_execute = schedulerSelfTriggerExecute;
  schedulerWaitHandle = 16;
  schedulerWaitWidth = 1;
  schedulerSelfTriggerCount = 0;
  schedulerSelfTriggerStaticState = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  talos_rt_v1_scheduler_signal(context, schedulerWaitHandle,
                                 schedulerWaitWidth, TALOS_RT_SIGNAL_CHANGE);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerSelfTriggerCount, 3u);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticTransitionRetriggersTheExecutingWait) {
  AOTTestState state;
  const talos_rt_static_fanout_entry fanout[] = {
      {1, 0, 1, TALOS_RT_WAIT_EDGE_CHANGE, 1, 0, 0, 1, 0, 0},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.fanout_entries = fanout;
  plan.fanout_entry_count = std::size(fanout);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(43);
  fixture.descriptor.execution = &execution;
  fixture.descriptor.native_execute = schedulerSelfTriggerExecute;
  schedulerWaitHandle = talos_rt_v1_native_state_static_handle(1);
  schedulerWaitWidth = 1;
  schedulerSelfTriggerCount = 0;
  schedulerSelfTriggerStaticState = 1;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  constexpr talos_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
  };
  ASSERT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);

  uint8_t oldValue = 0;
  uint8_t newValue = 1;
  talos_rt_v1_scheduler_signal_transition(
      context, schedulerWaitHandle, 1, &oldValue, nullptr, &newValue, nullptr);
  EXPECT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);
  EXPECT_EQ(schedulerSelfTriggerCount, 3u);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  schedulerSelfTriggerStaticState = 0;
  talos_rt_v1_context_destroy(context);
}

// IEEE 1800-2023 4.7, 9.4.2: writes held by a publication batch wake each
// static-fanout waiter exactly as per-write publication does: once, even when
// the waited bits change and change back, and never a waiter whose bits no
// write changed. An observer of individual occurrences (here VPI) disables
// holding, and the outcome never depends on the batch. A waiter beyond the
// first window makes the batch deliver one window before holding the next.
TEST(Scheduler, PublicationBatchMergesStaticTransitions) {
  constexpr uint64_t rootWidth = PublicationBatch::kWindowBits + 16;
  auto wakes = [](uint64_t waitLow, bool batched, bool vpiObserved,
                  bool touchWaitedBits) {
    AOTTestState state;
    const talos_rt_static_fanout_entry fanout[] = {
        {1, 0, 1, TALOS_RT_WAIT_EDGE_CHANGE, 1, 0, waitLow, 8, 0, 0},
    };
    talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
    plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
                 TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
    plan.fanout_entries = fanout;
    plan.fanout_entry_count = std::size(fanout);
    talos_rt_execution_descriptor_v1 execution{};
    execution.version = TALOS_RT_VERSION;
    execution.state_bit_count = rootWidth;
    talos_rt_context *context = nullptr;
    EXPECT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
              TALOS_RT_OK);
    EXPECT_EQ(
        talos_rt_v1_native_state_register_static(context, 1, 0, rootWidth),
        TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
              TALOS_RT_OK);
    SchedulerFixture fixture(44);
    fixture.descriptor.execution = &execution;
    fixture.descriptor.native_execute = schedulerWaitCountExecute;
    schedulerWaitHandle = talos_rt_stable_handle_encode(
        TALOS_RT_STABLE_HANDLE_STATIC, 1, static_cast<int64_t>(waitLow));
    schedulerWaitWidth = 8;
    schedulerWaitWakeCount = 0;
    EXPECT_EQ(talos_rt_v1_scheduler_add_aot(context,
                                              makeSchedulerInstance(fixture), 0,
                                              0, 0, nullptr, nullptr, 0,
                                              nullptr, 0),
              TALOS_RT_OK);
    constexpr talos_rt_native_schedule_node nodes[] = {
        {0, 0, UINT32_MAX},
        {0, 1, UINT32_MAX},
    };
    EXPECT_EQ(
        talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
        TALOS_RT_OK);
    EXPECT_EQ(schedulerWaitWakeCount, 0u);

    context->vpiObservationDemand = vpiObserved;
    if (batched) {
      EXPECT_EQ(talos_rt_v1_scheduler_publication_batch_begin(context),
                TALOS_RT_OK);
      EXPECT_EQ(talos_rt_v1_scheduler_publication_batch_begin(context),
                TALOS_RT_OK);
    }
    talos_rt_v1_scheduler_static_transition(context, 1, 0, 8, 0, 0, 0xa5, 0);
    if (touchWaitedBits) {
      talos_rt_v1_scheduler_static_transition(context, 1, waitLow, 8, 0, 0,
                                                0x5a, 0);
      talos_rt_v1_scheduler_static_transition(context, 1, waitLow, 8, 0x5a,
                                                0, 0, 0);
    }
    if (batched) {
      bool held = !vpiObserved;
      EXPECT_EQ(context->publicationBatch.staticID != 0, held);
      EXPECT_EQ(talos_rt_v1_scheduler_publication_batch_end(context),
                TALOS_RT_OK);
      // Only the outermost end delivers the held window.
      EXPECT_EQ(context->publicationBatch.staticID != 0, held);
      EXPECT_EQ(talos_rt_v1_scheduler_publication_batch_end(context),
                TALOS_RT_OK);
      EXPECT_EQ(context->publicationBatch.staticID, 0u);
      EXPECT_EQ(talos_rt_v1_scheduler_publication_batch_end(context),
                TALOS_RT_INVALID_LIFECYCLE);
    }
    context->vpiObservationDemand = false;
    EXPECT_EQ(
        talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
        TALOS_RT_OK);
    talos_rt_v1_context_destroy(context);
    return schedulerWaitWakeCount;
  };
  for (uint64_t waitLow : {uint64_t{8}, PublicationBatch::kWindowBits + 8}) {
    for (bool touchWaitedBits : {false, true}) {
      unsigned expected = touchWaitedBits ? 1u : 0u;
      EXPECT_EQ(wakes(waitLow, true, false, touchWaitedBits), expected);
      EXPECT_EQ(wakes(waitLow, false, false, touchWaitedBits), expected);
      EXPECT_EQ(wakes(waitLow, true, true, touchWaitedBits),
                wakes(waitLow, false, true, touchWaitedBits));
    }
  }
}

TEST(Scheduler, AOTStaticTransitionCanSuppressTheExecutingWait) {
  AOTTestState state;
  const talos_rt_static_fanout_entry fanout[] = {
      {1, 0, 1, TALOS_RT_WAIT_EDGE_CHANGE, 1,
       TALOS_RT_FANOUT_SUPPRESS_ACTIVE_SELF, 0, 1, 0, 0},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.fanout_entries = fanout;
  plan.fanout_entry_count = std::size(fanout);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(44);
  fixture.descriptor.execution = &execution;
  fixture.descriptor.native_execute = schedulerSelfTriggerExecute;
  schedulerWaitHandle = talos_rt_v1_native_state_static_handle(1);
  schedulerWaitWidth = 1;
  schedulerSelfTriggerCount = 0;
  schedulerSelfTriggerStaticState = 1;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  constexpr talos_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
  };
  ASSERT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);

  uint8_t oldValue = 0;
  uint8_t newValue = 1;
  talos_rt_v1_scheduler_signal_transition(
      context, schedulerWaitHandle, 1, &oldValue, nullptr, &newValue, nullptr);
  EXPECT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);
  EXPECT_EQ(schedulerSelfTriggerCount, 1u);
  EXPECT_EQ(context->nativeScheduleReadyNodes.findFirst(), UINT32_MAX);
  schedulerSelfTriggerStaticState = 0;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTDirectFragmentRetainsSelfRetrigger) {
  AOTTestState state;
  state.runHook = runDirectSelfTransition;
  const talos_rt_static_fanout_entry fanout[] = {
      {1, 0, 1, TALOS_RT_WAIT_EDGE_CHANGE, 1, 0, 0, 1, 0, 0},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.fanout_entries = fanout;
  plan.fanout_entry_count = std::size(fanout);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(45);
  fixture.descriptor.execution = &execution;
  fixture.descriptor.native_execute = schedulerSelfTriggerExecute;
  schedulerWaitHandle = talos_rt_v1_native_state_static_handle(1);
  schedulerWaitWidth = 1;
  schedulerSelfTriggerCount = 0;
  schedulerSelfTriggerStaticState = 1;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  constexpr talos_rt_native_schedule_node initial[] = {
      {0, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
  };
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot_nodes(context, initial,
                                                  std::size(initial)),
            TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_TRUE(context->scheduledProcesses.front().signalTriggered);
  EXPECT_TRUE(context->nativeScheduleReadyNodes.test(1));
  schedulerSelfTriggerStaticState = 0;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CompactBytecodeUsesDirectSignalSubscriptions) {
  SchedulerFixture fixture(31);
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, TALOS_RT_BC_SUSPEND, TALOS_RT_BC_TYPE_NONE, 0,
                    TALOS_RT_SUSPEND_EDGE, 0, 1);
  appendInstruction(code, TALOS_RT_BC_TERMINATE, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<talos_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  talos_rt_bytecode_v1 bytecode{code.data(),
                                  code.size(),
                                  entries.data(),
                                  static_cast<uint32_t>(entries.size()),
                                  1,
                                  fixture.layout.frame_size,
                                  nullptr,
                                  nullptr,
                                  0,
                                  nullptr,
                                  0,
                                  0,
                                  nullptr,
                                  0};
  fixture.descriptor.available_tiers = TALOS_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.native_requirements = nullptr;
  fixture.descriptor.native_execute = nullptr;
  fixture.descriptor.native_destroy = nullptr;
  fixture.descriptor.bytecode = &bytecode;

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->signalDiagnosticsEnabled = true;
  talos_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE, 0, 1, 0, 0};
  *entry = {16, TALOS_RT_WAIT_EDGE_NEGEDGE, 8};
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(), 1u);

  talos_rt_v1_scheduler_signal(
      context, 18, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(), 1u);
  talos_rt_v1_scheduler_signal(
      context, 18, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledProcesses.empty() ||
              context->scheduledProcesses.front().signalSubscriptions.empty());
  EXPECT_TRUE(context->signalSubscriptionBuckets.empty());
  EXPECT_EQ(context->signalDiagnostics.subscriptionsHighWater, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ImmediateAndDeferredEventsWakeOnlyTheirWaiters) {
  for (uint32_t nonblocking : {0u, 1u}) {
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    SchedulerFixture fixture(2 + nonblocking);
    schedulerWaitKind = TALOS_RT_SUSPEND_EVENT;
    schedulerWaitEdge = TALOS_RT_WAIT_EDGE_NONE;
    schedulerWaitHandle = 42;
    schedulerWaitWidth = 0;
    schedulerResumeCount = 0;
    ASSERT_EQ(
        talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
        TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
    talos_rt_v1_scheduler_event(context, 41, nonblocking);
    EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
    EXPECT_EQ(schedulerResumeCount, 0u);
    talos_rt_v1_scheduler_event(context, 42, nonblocking);
    EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
    EXPECT_EQ(schedulerResumeCount, 1u);
    talos_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, EventTriggeredSpansExactlyOneTimeSlot) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_event_triggered(context, 91), 0u);

  talos_rt_v1_scheduler_event(context, 91, 0);
  EXPECT_EQ(talos_rt_v1_scheduler_event_triggered(context, 91), 1u);
  EXPECT_EQ(talos_rt_v1_scheduler_event_triggered(context, 92), 0u);

  SchedulerFixture fixture(91);
  schedulerWaitKind = TALOS_RT_SUSPEND_DELAY;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(talos_rt_v1_scheduler_event_triggered(context, 91), 0u);

  talos_rt_v1_scheduler_event(context, 91, 1);
  EXPECT_EQ(talos_rt_v1_scheduler_event_triggered(context, 91), 0u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_event_triggered(context, 91), 1u);

  talos_rt_v1_scheduler_event_after(context, 93, 1, 4);
  EXPECT_EQ(talos_rt_v1_scheduler_event_triggered(context, 93), 0u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_event_triggered(context, 91), 0u);
  EXPECT_EQ(talos_rt_v1_scheduler_event_triggered(context, 93), 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, TriggeringNullEventHasNoEffect) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);

  EXPECT_TRUE(context->events.empty());
  talos_rt_v1_scheduler_event(context, UINT64_MAX, 0);
  EXPECT_TRUE(context->events.empty());
  EXPECT_EQ(talos_rt_v1_scheduler_event_triggered(context, UINT64_MAX), 0u);

  talos_rt_v1_scheduler_event_after(context, UINT64_MAX, 1, 7);
  EXPECT_TRUE(context->scheduledDesignEvents.empty());
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_TRUE(context->events.empty());
  EXPECT_EQ(talos_rt_v1_scheduler_event_triggered(context, UINT64_MAX), 0u);

  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, WaitActionPayloadSelectsTheExactFrameField) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(5);
  schedulerWaitKind = TALOS_RT_SUSPEND_EVENT;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_NONE;
  schedulerWaitHandle = 73;
  schedulerWaitWidth = 0;
  schedulerWaitOffset = 48;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  talos_rt_v1_scheduler_event(context, 73, 0);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ComputeGraphRanksOrderRunnableProcesses) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture later(100);
  SchedulerFixture earlier(200);
  schedulerOrder.clear();
  ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(later), 0, 9),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(earlier), 0, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{200, 100}));
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, DynamicNativeChildrenInheritPhaseThroughIndexedParent) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(250);

  // Keep the active parent behind a large existing cohort. Child creation
  // must use the token index rather than rescanning that cohort per child.
  constexpr uint32_t cohortSize = 4096;
  for (uint32_t index = 0; index != cohortSize; ++index)
    ASSERT_EQ(
        talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
        TALOS_RT_OK);
  talos_rt_process_instance_v1 *parent = makeSchedulerInstance(fixture);
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, parent, TALOS_RT_SCHEDULE_FINAL),
      TALOS_RT_OK);
  uint64_t parentToken = context->scheduledProcesses.back().token;
  context->activeNativeProcess = parent;
  context->activeLogicalProcessToken =
      TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | parentToken;

  size_t firstChild = context->scheduledProcesses.size();
  for (uint32_t index = 0; index != cohortSize; ++index)
    ASSERT_EQ(
        talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
        TALOS_RT_OK);
  ASSERT_EQ(context->scheduledProcessIndices.size(),
            context->scheduledProcesses.size());
  for (size_t index = firstChild; index != context->scheduledProcesses.size();
       ++index)
    EXPECT_EQ(context->scheduledProcesses[index].phase, 1u);

  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;
  talos_rt_v1_context_destroy(context);
}

std::vector<std::pair<uint64_t, uint32_t>> finalPollObservations;

// Poll the termination latch, request $finish, and poll again.
talos_rt_status finalPollExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  uint64_t id = instance->descriptor->handle.id;
  finalPollObservations.push_back(
      {id, talos_rt_v1_scheduler_termination_requested(instance->context)});
  EXPECT_EQ(talos_rt_v1_scheduler_finish(instance->context, 0),
            TALOS_RT_OK);
  finalPollObservations.push_back(
      {id, talos_rt_v1_scheduler_termination_requested(instance->context)});
  *instance->action = {
      TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return TALOS_RT_OK;
}

TEST(Scheduler, FinalProceduresPollOnlyTheirOwnTerminationRequests) {
  // IEEE 1800-2023 9.2.3: final procedures run after $finish, so the request
  // that started the Final phase must not stop them at their first poll. A
  // $finish executed inside one still ends the simulation immediately, and
  // no other final procedure runs.
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture initial(1), first(2), second(3);
  for (SchedulerFixture *fixture : {&initial, &first, &second})
    fixture->descriptor.native_execute = finalPollExecute;
  finalPollObservations.clear();
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(initial), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, makeSchedulerInstance(first),
                                        TALOS_RT_SCHEDULE_FINAL),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, makeSchedulerInstance(second),
                                        TALOS_RT_SCHEDULE_FINAL),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_EQ(finalPollObservations.size(), 4u);
  EXPECT_EQ(finalPollObservations[0], std::make_pair(uint64_t{1}, 0u));
  EXPECT_EQ(finalPollObservations[1], std::make_pair(uint64_t{1}, 1u));
  uint64_t final = finalPollObservations[2].first;
  EXPECT_TRUE(final == 2 || final == 3);
  EXPECT_EQ(finalPollObservations[2], std::make_pair(final, 0u));
  EXPECT_EQ(finalPollObservations[3], std::make_pair(final, 1u));
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, UnstartedPostponedActorsDoNotPreemptActiveResumes) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture active(10);
  SchedulerFixture monitor(200);
  schedulerWaitKind = TALOS_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = 16;
  schedulerWaitWidth = 1;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(active), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_TRUE(schedulerOrder.empty());

  // An actor that has never run does take its first activation ahead of
  // signal resumptions, but only within its own region: a postponed actor
  // that preempted this active resume would observe the values from before
  // the active region ran, and would leave the Postponed region reachable a
  // second time in the same time slot (IEEE 1800-2017 4.4.2.9).
  talos_rt_v1_scheduler_signal(context, schedulerWaitHandle,
                                 schedulerWaitWidth, TALOS_RT_SIGNAL_CHANGE);
  ASSERT_EQ(talos_rt_v1_scheduler_add(
                context, makeSchedulerInstance(monitor),
                TALOS_RT_SCHEDULE_HOME(TALOS_RT_REGION_POSTPONED)),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{10, 200}));
  EXPECT_EQ(schedulerResumeCount, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, PlannedContinuationRanksApplyAfterResume) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture later(10);
  SchedulerFixture earlier(20);
  schedulerWaitKind = TALOS_RT_SUSPEND_DELAY;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_NONE;
  schedulerWaitHandle = 0;
  schedulerWaitWidth = 0;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  const uint32_t continuation = 1;
  const uint32_t laterRank = 9;
  const uint32_t earlierRank = 1;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_planned(context, makeSchedulerInstance(later),
                                          0, 0, &continuation, &laterRank, 1),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add_planned(
                context, makeSchedulerInstance(earlier), 0, 0, &continuation,
                &earlierRank, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{20, 10}));
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTPlanInstallBindRunAndExclusiveMutableState) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  plan.promotion_ready = schedulerPromotionReady;
  schedulerPromotionInvalidationCount = 0;
  schedulerPromotionReadyCount = 0;
  talos_rt_context *first = nullptr;
  talos_rt_context *second = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&first), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_context_create(&second), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(first, &plan), TALOS_RT_OK);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 1u);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(first, &plan),
            TALOS_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(second, &plan),
            TALOS_RT_INVALID_LIFECYCLE);
  talos_rt_native_schedule_plan undersized = plan;
  undersized.mutable_state_size = sizeof(void *);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(second, &undersized),
            TALOS_RT_INVALID_ARGUMENT);
  undersized = plan;
  --undersized.size;
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(second, &undersized),
            TALOS_RT_INVALID_ARGUMENT);

  SchedulerFixture fixture(100);
  talos_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(first, instance, 0, 0, 7, nullptr,
                                            nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  EXPECT_EQ(state.actors[0], instance);
  EXPECT_NE(talos_rt_v1_scheduler_process_token(first, instance), 0u);

  talos_rt_process_instance_v1 *duplicate = makeSchedulerInstance(fixture);
  ASSERT_NE(duplicate, nullptr);
  EXPECT_EQ(talos_rt_v1_scheduler_add_aot(first, duplicate, 0, 0, 7, nullptr,
                                            nullptr, 0, nullptr, 0),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(duplicate), TALOS_RT_OK);

  schedulerOrder.clear();
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(first), TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{100}));
  EXPECT_EQ(state.actors[0], nullptr);
  talos_rt_v1_context_destroy(first);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 2u);

  state.actors = {};
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(second, &plan), TALOS_RT_OK);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 3u);
  EXPECT_EQ(second->nativeScheduleNBARootCount, 0u);
  EXPECT_EQ(second->nativeScheduleNBASiteCount, 0u);
  talos_rt_v1_context_destroy(second);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 4u);
  // Installation and release reset promotion state but never scan it; only a
  // reached quiescent transient handback may query readiness.
  EXPECT_EQ(schedulerPromotionReadyCount, 0u);
}

TEST(Scheduler, ScopedRootInventoryRequiresEveryActorExactlyOnce) {
  AOTTestState state;
  auto plan = makeAOTPlan(state);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  const talos_rt_static_actor_root incomplete[] = {
      {0, 0, TALOS_RT_STATIC_ROOT_COMPLETE, 0}};
  plan.actor_roots = incomplete;
  plan.actor_root_count = std::size(incomplete);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);
  const talos_rt_static_actor_root duplicate[] = {
      {0, 0, TALOS_RT_STATIC_ROOT_UNSCOPED, 0},
      {0, 0, TALOS_RT_STATIC_ROOT_COMPLETE, 0}};
  plan.actor_roots = duplicate;
  plan.actor_root_count = std::size(duplicate);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);
  const talos_rt_static_actor_root complete[] = {
      {0, 0, TALOS_RT_STATIC_ROOT_COMPLETE, 0},
      {1, 0, TALOS_RT_STATIC_ROOT_UNSCOPED, 0}};
  plan.actor_roots = complete;
  plan.actor_root_count = std::size(complete);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  // A complete inventory alone cannot authorize local disturbance routing.
  // This plan has no guarded Tier 1 coordinator and allocates no index.
  EXPECT_FALSE(context->nativeScopedWrite);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, PeriodicWriteFootprintTracksInstalledPlanOwnership) {
  AOTTestState state;
  uint64_t ingress = 0;
  uint64_t active = 3;
  uint8_t value = 0, unknown = 0;
  constexpr uint32_t sparseNBAState = UINT32_C(0x10000000);
  const talos_rt_native_clock_kernel clocks[] = {
      {1, TALOS_RT_WAIT_EDGE_POSEDGE, 0, 1, &ingress, 1, 0, &active},
  };
  talos_rt_native_merged_fragment merged[] = {
      {0, 1, 0, 0, 0, 0, generatedCheckpointCallback},
      {1, 1, 0, 1, 1, 0, nullptr},
  };
  const talos_rt_static_actor_root roots[] = {
      {0, 1, TALOS_RT_STATIC_ROOT_READ, 0},
      {0, 2, TALOS_RT_STATIC_ROOT_WRITE, 0},
      {1, 3, TALOS_RT_STATIC_ROOT_WRITE, 0},
  };
  const talos_rt_static_nba_root nba[] = {
      {2, sparseNBAState, 1, nullptr, nullptr, 0}};
  auto plan = makeAOTPlan(state);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.clock_kernels = clocks;
  plan.clock_kernel_count = std::size(clocks);
  plan.merged_fragments = merged;
  plan.merged_fragment_count = std::size(merged);
  plan.timeslot_coordinator = clockCoordinator;
  plan.actor_roots = roots;
  plan.actor_root_count = std::size(roots);
  plan.nba_roots = nba;
  plan.nba_root_count = std::size(nba);
  plan.state_value = &value;
  plan.state_unknown = &unknown;
  plan.state_bit_count = 4;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 4;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  for (uint32_t id = 1; id <= 3; ++id)
    ASSERT_EQ(talos_rt_v1_native_state_register_static(context, id, id - 1, 1),
              TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, sparseNBAState,
                                                       3, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  // Include direct-owner writes and all NBA roots, not read-only dependencies
  // or runtime-only writers. Sparse IDs must not require a huge dense bitmap.
  EXPECT_EQ(context->nativePeriodicGeneratedWritableStates,
            (std::unordered_set<uint32_t>{2, sparseNBAState}));
  talos_rt_release_native_schedule_plan(context);
  EXPECT_TRUE(context->nativePeriodicGeneratedWritableStates.empty());
  // The image may be reused with new ownership only after release. Its cache
  // belongs to this installation, never to the plan pointer or process-global
  // mutable state address.
  merged[0].execute = nullptr;
  merged[1].execute = generatedCheckpointCallback;
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  EXPECT_EQ(context->nativePeriodicGeneratedWritableStates,
            (std::unordered_set<uint32_t>{3, sparseNBAState}));
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTCheckpointRunsOneRuntimeActionAndReentersNatively) {
  AOTTestState state;
  state.runHook = runCheckpointThenReenter;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(321);
  schedulerOrder.clear();
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(state.runCalls, 2u);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{321}));
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, SharedDriverArbitratesPlanAndDescriptorContinuations) {
  AOTTestState state;
  auto plan = makeAOTPlan(state);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  SchedulerFixture native(1), interpreted(2), descriptor(3);
  schedulerWaitKind = TALOS_RT_SUSPEND_DELAY;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, TALOS_RT_BC_SUSPEND, TALOS_RT_BC_TYPE_NONE, 0,
                    TALOS_RT_SUSPEND_DELAY, 0, 1);
  appendInstruction(code, TALOS_RT_BC_TERMINATE, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<talos_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  talos_rt_bytecode_v1 bytecode{};
  bytecode.code = code.data();
  bytecode.code_size = code.size();
  bytecode.entries = entries.data();
  bytecode.entry_count = entries.size();
  bytecode.register_count = 1;
  bytecode.register_offset = interpreted.layout.frame_size;
  interpreted.descriptor.available_tiers |= TALOS_RT_TIER_MASK_BYTECODE;
  interpreted.descriptor.bytecode = &bytecode;
  auto *nativeInstance = makeSchedulerInstance(native);
  auto *bytecodeInstance = makeSchedulerInstance(interpreted);
  auto *wait =
      reinterpret_cast<talos_rt_wait_record_v1 *>(bytecodeInstance->frame);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_DELAY, 0, 0, 17, 0};
  uint32_t bytecodeEntry = 0;
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context, nativeInstance, 0, 0, 3,
                                            nullptr, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context, bytecodeInstance, 0, 1, 2,
                                            nullptr, nullptr, 0, &bytecodeEntry,
                                            1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(descriptor), 0, 1),
            TALOS_RT_OK);
  // Deliberately scramble node layout: scheduler rank governs execution.
  const talos_rt_native_schedule_node nodes[] = {{1, 1, UINT32_MAX},
                                                   {0, 0, UINT32_MAX},
                                                   {0, 1, UINT32_MAX},
                                                   {1, 0, UINT32_MAX}};
  ASSERT_EQ(initializeNativeAOTNodesUnlocked(context, nodes, std::size(nodes)),
            TALOS_RT_OK);
  uint8_t futurePlane = 0, replacement = 9;
  ASSERT_EQ(talos_rt_v1_scheduler_nba(context, &futurePlane, nullptr, 8, 0, 8,
                                        99, &replacement, nullptr),
            TALOS_RT_OK);
  {
    NativeAOTContextScope active(context);
    NativeAOTMutexScope locked(context);
    ASSERT_EQ(runScheduler(context, {true, true}), TALOS_RT_OK);
    EXPECT_EQ(context->schedulerTime, 0u);
    EXPECT_EQ(nativeInstance->continuation, 1u);
    EXPECT_EQ(bytecodeInstance->continuation, 1u);
    EXPECT_EQ(bytecodeInstance->tier, TALOS_RT_TIER_BYTECODE);
    EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 2u);
    EXPECT_EQ(futurePlane, 0u);
    EXPECT_EQ(context->scheduledNBAs.size(), 1u);
    EXPECT_TRUE(schedulerOrder.empty());
    ASSERT_EQ(runScheduler(context, {true, false}), TALOS_RT_OK);
  }
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{3, 2, 1}));
  EXPECT_EQ(schedulerResumeCount, 3u);
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 4u);
  EXPECT_EQ(context->schedulerTime, 99u);
  EXPECT_EQ(futurePlane, 9u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, SynchronousDrivesKeepOnlyTheLastValue) {
  // IEEE 1800-2023 14.16.2: of several synchronous drives of one clocking
  // output maturing in the same region, only the last value is driven. A
  // whole-vector drive followed by a drive of bits [2, 4) leaves the first
  // drive's low bits and the second drive's high bits, and the first drive
  // already carries the second's bits so its commit cannot glitch.
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  uint8_t plane = 0b1111;
  uint8_t whole = 0b0000, high = 0b01;
  ASSERT_EQ(talos_rt_v1_scheduler_clocking_nba(context, &plane, nullptr, 8, 0,
                                                 4, 0, &whole, nullptr, 7),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_clocking_nba(context, &plane, nullptr, 8, 2,
                                                 2, 0, &high, nullptr, 7),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  uint32_t region = context->scheduledNBAs.front().execRegion;
  ASSERT_EQ(resolveClockingDriveConflictsUnlocked(context, region),
            TALOS_RT_OK);
  const ScheduledNBA &first = context->scheduledNBAs.front();
  uint8_t firstValue = first.inlinePacked
                           ? static_cast<uint8_t>(first.inlineValue)
                           : first.value.front();
  EXPECT_EQ(firstValue & 0b1111, 0b0100);

  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(plane, 0b0100);
  EXPECT_EQ(context->schedulerFinishStatus, TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, SharedDriverReactivatesPlanAfterEachSameSlotNBA) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  AOTTestState state;
  auto plan = makeAOTPlan(state, 1);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  SchedulerFixture fixture(5);
  fixture.descriptor.execution = &execution;
  fixture.descriptor.native_execute =
      [](talos_rt_process_instance_v1 *instance) {
        if (instance->continuation == 0)
          return schedulerExecute(instance);
        auto *context = instance->context;
        uint8_t value = context->stateValue[0];
        schedulerOrder.push_back(value);
        EXPECT_EQ(context->stateUnknown[0] & 255, 0u);
        if (value == 3)
          return schedulerExecute(instance);
        uint8_t next = value + 1;
        auto status = talos_rt_v1_scheduler_nba(
            context, &nbaDummyPlane, nullptr, 8, schedulerWaitHandle, 8, 0,
            &next, nullptr);
        *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                             TALOS_RT_SUSPEND_CHANGE,
                             1,
                             TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                             0,
                             48};
        return status;
      };
  schedulerWaitKind = TALOS_RT_SUSPEND_CHANGE;
  schedulerWaitHandle = talos_rt_v1_native_state_static_handle(1);
  schedulerWaitWidth = 8;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_CHANGE;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  nbaDummyPlane = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  const talos_rt_native_schedule_node nodes[] = {{0, 0, UINT32_MAX},
                                                   {0, 1, UINT32_MAX}};
  ASSERT_EQ(initializeNativeAOTNodesUnlocked(context, nodes, std::size(nodes)),
            TALOS_RT_OK);
  uint8_t first = 1;
  ASSERT_EQ(talos_rt_v1_scheduler_nba(context, &nbaDummyPlane, nullptr, 8,
                                        schedulerWaitHandle, 8, 0, &first,
                                        nullptr),
            TALOS_RT_OK);
  {
    NativeAOTContextScope active(context);
    NativeAOTMutexScope locked(context);
    ASSERT_EQ(runScheduler(context, {true, true}), TALOS_RT_OK);
  }
  // Three separate NBA transitions, each followed by Active reactivation;
  // the final entry records source-process termination exactly once.
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{1, 2, 3, 5}));
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->schedulerTime, 0u);
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 4u);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTNodeAdapterHonorsRequiredBytecode) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  // This unit test uses the compact process image below. Production required
  // images carry this policy in the separately encoded design descriptor.
  execution.flags = TALOS_RT_EXECUTION_REQUIRE_BYTECODE;
  AOTTestState state;
  auto plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT |
               TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               TALOS_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO;
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  SchedulerFixture fixture(100);
  fixture.descriptor.execution = &execution;
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_TERMINATE, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<talos_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 0}}};
  talos_rt_bytecode_v1 bytecode{code.data(),
                                  code.size(),
                                  entries.data(),
                                  2,
                                  0,
                                  fixture.layout.frame_size,
                                  nullptr,
                                  nullptr,
                                  0,
                                  nullptr,
                                  0,
                                  0,
                                  nullptr,
                                  0};
  fixture.descriptor.available_tiers |= TALOS_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.bytecode = &bytecode;
  schedulerOrder.clear();
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  const talos_rt_native_schedule_node nodes[] = {{0, 0, UINT32_MAX}};
  {
    NativeAOTContextScope active(context);
    NativeAOTMutexScope locked(context);
    ASSERT_EQ(talos_rt_v1_scheduler_run_aot_nodes(context, nodes, 1),
              TALOS_RT_OK);
  }
  EXPECT_TRUE(schedulerOrder.empty()); // A native call would append 100.
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 1u);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ScopedDisturbanceRetainsCompiledExecutors) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 2;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  for (uint32_t root = 1; root <= 2; ++root)
    ASSERT_EQ(
        talos_rt_v1_native_state_register_static(context, root, root - 1, 1),
        TALOS_RT_OK);
  AOTTestState state;
  uint32_t fast = 0;
  auto plan = makeAOTPlan(state);
  const talos_rt_static_actor_root roots[] = {
      {0, 1, TALOS_RT_STATIC_ROOT_READ, 0},
      {1, 2, TALOS_RT_STATIC_ROOT_READ, 0}};
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE |
               TALOS_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION;
  plan.specialization_fast = &fast;
  plan.actor_roots = roots;
  plan.actor_root_count = std::size(roots);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  SchedulerFixture first(100), second(101);
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_TERMINATE, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<talos_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 0}}};
  talos_rt_bytecode_v1 bytecode{code.data(),
                                  code.size(),
                                  entries.data(),
                                  2,
                                  0,
                                  first.layout.frame_size,
                                  nullptr,
                                  nullptr,
                                  0,
                                  nullptr,
                                  0,
                                  0,
                                  nullptr,
                                  0};
  schedulerOrder.clear();
  for (auto *fixture : {&first, &second}) {
    fixture->descriptor.execution = &execution;
    fixture->descriptor.available_tiers |= TALOS_RT_TIER_MASK_BYTECODE;
    fixture->descriptor.bytecode = &bytecode;
    fixture->descriptor
        .native_execute = [](talos_rt_process_instance_v1 *instance) {
      uint32_t root = instance->descriptor->handle.id - 99;
      auto direct = talos_rt_v1_static_specialization_guard(
          instance->context, root - 1, root, TALOS_RT_STATIC_ROOT_READ);
      schedulerOrder.push_back(root * 10 + direct);
      instance->native_handle = instance;
      *instance->action = {
          TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
      return TALOS_RT_OK;
    };
    ASSERT_EQ(talos_rt_v1_scheduler_add_aot(
                  context, makeSchedulerInstance(*fixture), 0,
                  fixture->descriptor.handle.id - 100, 0, nullptr, nullptr, 0,
                  nullptr, 0),
              TALOS_RT_OK);
  }
  talos_rt_aot_external_write_handle_unlocked(
      context, talos_rt_v1_native_state_static_handle(1), 0, 1, false);
  EXPECT_TRUE(context->nativeScheduleExternalWritePending);
  const talos_rt_native_schedule_node nodes[] = {{0, 0, UINT32_MAX},
                                                   {1, 0, UINT32_MAX}};
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot_nodes(context, nodes, 2),
            TALOS_RT_OK);
  // Both fragments remain compiled. Only the affected root loses direct
  // access; the unrelated fragment retains its indexed fast route.
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{10, 21}));
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 2u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_FALSE(context->nativeScheduleExternalWritePending);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedCheckpointValidatesAndConsumesExactContinuation) {
  AOTTestState state;
  state.runHook = runGeneratedCheckpoint;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(22);
  schedulerWaitKind = TALOS_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = 17;
  schedulerWaitWidth = 1;
  schedulerWaitOffset = 0;
  generatedCheckpointCallbackCount = 0;
  invalidGeneratedCheckpointStatus = TALOS_RT_OK;
  validGeneratedCheckpointStatus = TALOS_RT_INVALID_CONTINUATION;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  // Establish the source wait before the generated transaction takes direct
  // ownership of continuation 1.
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(invalidGeneratedCheckpointStatus, TALOS_RT_INVALID_CONTINUATION);
  EXPECT_EQ(validGeneratedCheckpointStatus, TALOS_RT_OK);
  EXPECT_EQ(generatedCheckpointCallbackCount, 2u);
  EXPECT_EQ(state.runCalls, 2u);
  EXPECT_EQ(context->schedulerPreponedTime, 17u);
  EXPECT_EQ(context->schedulerSlotProgress, 2u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->nativeScheduleCheckpointActorSlot, UINT32_MAX);
  EXPECT_EQ(context->nativeScheduleCheckpointContinuation, 0u);
  EXPECT_EQ(context->nativeScheduleCheckpointCallback, nullptr);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTTimedCheckpointCommitsSameSlotNBAAndReentersNatively) {
  AOTTestState state;
  state.runHook = runTimedCheckpointThenReenter;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  uint8_t plane = 0;
  uint8_t replacement = 0xa5;
  ASSERT_EQ(talos_rt_v1_scheduler_nba(context, &plane, nullptr, 8, 0, 8, 5,
                                        &replacement, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(state.runCalls, 2u);
  EXPECT_EQ(context->schedulerTime, 5u);
  EXPECT_EQ(plane, replacement);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBAQueueGrowsWithoutLosingOrderOrUnknowns) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos::runtime::EvalNBAQueue queue;
  for (uint32_t i = 0; i != 513; ++i) {
    ASSERT_EQ(talos_rt_v1_eval_nba_reserve(context, &queue), TALOS_RT_OK);
    queue.data[queue.size++] = {i % 3, i * 7u, i, ~uint64_t{i}};
  }
  EXPECT_GE(queue.capacity, 513u);
  for (uint32_t i = 0; i != queue.size; ++i) {
    EXPECT_EQ(queue.data[i].site, i % 3);
    EXPECT_EQ(queue.data[i].offset, i * 7u);
    EXPECT_EQ(queue.data[i].value, i);
    EXPECT_EQ(queue.data[i].unknown, ~uint64_t{i});
  }
  auto *storage = queue.data;
  queue.size = 0; // A barrier consumes the sequence, not the allocation.
  ASSERT_EQ(talos_rt_v1_eval_nba_reserve(context, &queue), TALOS_RT_OK);
  EXPECT_EQ(queue.data, storage);
  queue.data[queue.size++] = {99, 0, 0, UINT64_MAX};
  EXPECT_EQ(queue.data[0].site, 99u);
  queue.error = TALOS_RT_OUT_OF_MEMORY;
  talos_rt_v1_context_destroy(context);
  EXPECT_EQ(queue.data, nullptr);
  EXPECT_EQ(queue.capacity, 0u);
  EXPECT_EQ(queue.size, 0u);
  EXPECT_EQ(queue.error, TALOS_RT_OK);
}

TEST(Scheduler, GeneratedNBAQueueRejectsMalformedOrExhaustedDescriptors) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos::runtime::EvalNBAQueue queue;
  EXPECT_EQ(talos_rt_v1_eval_nba_reserve(nullptr, &queue),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_eval_nba_reserve(context, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  queue.size = 1;
  EXPECT_EQ(talos_rt_v1_eval_nba_reserve(context, &queue),
            TALOS_RT_INVALID_ARGUMENT);
  talos::runtime::EvalNBARecord sentinel{};
  queue = {&sentinel, UINT32_MAX, UINT32_MAX};
  EXPECT_EQ(talos_rt_v1_eval_nba_reserve(context, &queue),
            TALOS_RT_OUT_OF_RESOURCES);
  EXPECT_TRUE(context->nativeEvalNBAQueues.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, IndexedClockKernelIngressMaintainsReadySetAcrossReentry) {
  for (uint32_t capacity : {65u, 129u, 2049u, 4097u}) {
    SCOPED_TRACE(capacity);
    uint32_t words = (capacity + 63) / 64;
    const auto layout = talos::runtime::clockKernelReadySetLayout(words);
    std::vector<uint64_t> ingress(layout.storageWords);
    talos::runtime::ReadySetView ready(ingress.data(), layout);
    ready.clear();
    AOTTestState state;
    talos_rt_native_clock_kernel clock{
        1,
        TALOS_RT_WAIT_EDGE_POSEDGE,
        0,
        1,
        ingress.data(),
        words,
        talos::runtime::indexedClockKernelReadySet,
        nullptr};
    talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
    plan.clock_kernels = &clock;
    plan.clock_kernel_count = 1;
    talos_rt_native_merged_fragment merged{0, 0, 0,      capacity - 1,
                                             0, 0, nullptr};
    plan.merged_fragments = &merged;
    plan.merged_fragment_count = 1;
    plan.timeslot_coordinator = clockCoordinator;
    talos_rt_execution_descriptor_v1 execution{};
    execution.version = TALOS_RT_VERSION;
    execution.state_bit_count = 1;
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
              TALOS_RT_OK);
    clock.reserved =
        2; // Unknown index layout must be rejected, not read as leaves.
    EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
              TALOS_RT_INVALID_ARGUMENT);
    clock.reserved = talos::runtime::indexedClockKernelReadySet;
    ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
              TALOS_RT_OK);
    for (unsigned iteration = 0; iteration != 3; ++iteration) {
      ASSERT_EQ(talos_rt_v1_scheduler_activate_clock_kernel(context, 0,
                                                              capacity - 1),
                TALOS_RT_OK);
      EXPECT_EQ(ready.findFirst(), capacity - 1);
      ASSERT_EQ(talos_rt_v1_scheduler_activate_clock_kernel(context, 0, 0),
                TALOS_RT_OK);
      ASSERT_EQ(talos_rt_v1_scheduler_activate_clock_kernel(context, 0, 0),
                TALOS_RT_OK);
      EXPECT_EQ(ready.popFirst(), 0u);
      EXPECT_EQ(ready.popFirst(), capacity - 1);
      EXPECT_EQ(ready.findFirst(), UINT32_MAX);
    }
    talos_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, AOTClockKernelIngressSuppressesDuplicateBits) {
  AOTTestState state;
  uint64_t ingress[2] = {};
  const talos_rt_native_clock_kernel clocks[] = {
      {1, TALOS_RT_WAIT_EDGE_POSEDGE, 0, 1, ingress, 2, 0, nullptr},
  };
  const talos_rt_native_merged_fragment merged[] = {
      {0, 0, 0, 65, 0, 0, nullptr},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.clock_kernels = clocks;
  plan.clock_kernel_count = std::size(clocks);
  plan.merged_fragments = merged;
  plan.merged_fragment_count = std::size(merged);
  plan.timeslot_coordinator = clockCoordinator;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_scheduler_activate_clock_kernel(context, 0, 65),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_activate_clock_kernel(context, 0, 65),
            TALOS_RT_OK);
  EXPECT_EQ(ingress[0], 0u);
  EXPECT_EQ(ingress[1], 2u);
  EXPECT_EQ(talos_rt_v1_scheduler_activate_clock_kernel(context, 0, 128),
            TALOS_RT_INVALID_ARGUMENT);

  clockCoordinatorIngress = ingress;
  clockCoordinatorWords = std::size(ingress);
  clockCoordinatorCalls = 0;
  // A VPI deposit inside an active scheduler/checkpoint publishes ingress but
  // must not recursively run the clock loop. The outer drain consumes it once
  // the current actor has returned.
  context->nativeScheduleRunning = true;
  EXPECT_EQ(talos_rt_v1_scheduler_run_clock_coordinator(context),
            TALOS_RT_OK);
  EXPECT_EQ(clockCoordinatorCalls, 0u);
  EXPECT_EQ(ingress[1], 2u);
  context->nativeScheduleRunning = false;
  talos_rt_process_instance_v1 active{};
  context->activeNativeProcess = &active;
  EXPECT_EQ(talos_rt_v1_scheduler_run_clock_coordinator(context),
            TALOS_RT_OK);
  EXPECT_EQ(clockCoordinatorCalls, 0u);
  EXPECT_EQ(ingress[1], 2u);
  context->activeNativeProcess = nullptr;
  EXPECT_EQ(talos_rt_v1_scheduler_run_clock_coordinator(context),
            TALOS_RT_OK);
  EXPECT_EQ(clockCoordinatorCalls, 1u);
  EXPECT_EQ(ingress[0], 0u);
  EXPECT_EQ(ingress[1], 0u);
  clockCoordinatorIngress = nullptr;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTInstallRejectsRepeatedKeysAnywhereInItsTables) {
  // Installation checks uniqueness by sorting and kernel order against each
  // predecessor, so a repeated key is rejected wherever it appears.
  AOTTestState state;
  uint64_t ingress[2] = {};
  talos_rt_native_clock_kernel clocks[] = {
      {1, TALOS_RT_WAIT_EDGE_POSEDGE, 0, 1, ingress, 2, 0, nullptr},
      {1, TALOS_RT_WAIT_EDGE_NEGEDGE, 0, 1, ingress, 2, 0, nullptr},
      {2, TALOS_RT_WAIT_EDGE_POSEDGE, 0, 1, ingress, 2, 0, nullptr},
  };
  talos_rt_native_merged_fragment merged[] = {
      {0, 0, 0, 1, 0, 0, nullptr},
      {0, 0, 1, 1, 0, 0, nullptr},
      {0, 0, 0, 2, 0, 0, nullptr},
  };
  talos_rt_static_nba_root roots[] = {
      {17, 1, 1, nullptr, nullptr, 0},
      {18, 2, 1, nullptr, nullptr, 0},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.clock_kernels = clocks;
  plan.clock_kernel_count = std::size(clocks);
  plan.merged_fragments = merged;
  plan.merged_fragment_count = std::size(merged);
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.timeslot_coordinator = clockCoordinator;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 2;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 1, 1),
            TALOS_RT_OK);
  merged[2].bit = 1;
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);
  merged[2].bit = 2;
  std::swap(clocks[1], clocks[2]);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);
  std::swap(clocks[1], clocks[2]);
  clocks[1] = clocks[0];
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);
  clocks[1].edge = TALOS_RT_WAIT_EDGE_NEGEDGE;
  roots[1].static_state = 1;
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);
  roots[1].static_state = 2;
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTCleanSuperstepRequiresACompleteStaticPlan) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);

  plan.flags = TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  EXPECT_TRUE(canUseStaticAOTFanout(context));
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticEvalIslandIsDistinctFromFullyStatic) {
  AOTTestState state;
  uint64_t ingress = 0;
  uint64_t active = 1;
  const talos_rt_native_clock_kernel clocks[] = {
      {1, TALOS_RT_WAIT_EDGE_POSEDGE, 0, 1, &ingress, 1, 0, &active},
  };
  const talos_rt_native_merged_fragment merged[] = {
      {0, 1, 0, 0, 0, 0, generatedCheckpointCallback},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.clock_kernels = clocks;
  plan.clock_kernel_count = std::size(clocks);
  plan.merged_fragments = merged;
  plan.merged_fragment_count = std::size(merged);
  plan.timeslot_coordinator = clockCoordinator;
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  plan.promotion_ready = schedulerPromotionReady;
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_EVAL |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_EVAL_ISLAND |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);

  plan.flags |= TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  // The bootstrap may enter scheduler_run_aot_nodes from inside periodic
  // preparation. It must retain publication until the primary-overlap audit
  // has certified this island; a full static plan above remains unaffected.
  EXPECT_FALSE(context->nativeStaticEvalIslandCertified);
  EXPECT_FALSE(canUseStaticAOTFanout(context));
  context->nativeStaticEvalIslandCertified = true;
  EXPECT_TRUE(canUseStaticAOTFanout(context));
  talos_rt_v1_context_destroy(context);

  // A compile-time publication proof allows generated fanout during the
  // runtime calendar/bootstrap without claiming whole-clock ownership
  // (IEEE 1800-2023 4.4, 9.4.2). Dynamic observer demand still blocks it.
  plan.flags |= TALOS_RT_NATIVE_SCHEDULE_RUNTIME_CALENDAR_EVAL;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  execution.flags = TALOS_RT_EXECUTION_DPI_EXPORTS;
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);
  execution.flags =
      TALOS_RT_EXECUTION_VPI_READ | TALOS_RT_EXECUTION_VPI_WRITE;
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  EXPECT_FALSE(context->nativeStaticEvalIslandCertified);
  EXPECT_TRUE(canUseStaticAOTFanout(context));
  talos_rt_aot_observation_demand_changed_unlocked(context, true);
  EXPECT_FALSE(canUseStaticAOTFanout(context));
  talos_rt_aot_observation_demand_changed_unlocked(context, false);
  EXPECT_TRUE(canUseStaticAOTFanout(context));
  context->activeComputedObserverWaiterCount = 1;
  EXPECT_FALSE(canUseStaticAOTFanout(context));
  context->activeComputedObserverWaiterCount = 0;
  context->forceMask.assign(1, 1);
  EXPECT_FALSE(canUseStaticAOTFanout(context));
  context->forceMask.clear();
  context->nativeScheduleExternalWritePending = true;
  EXPECT_FALSE(canUseStaticAOTFanout(context));
  context->nativeScheduleExternalWritePending = false;
  talos_rt_v1_context_destroy(context);

  // The island certifies only its residual eval closure. Claiming the
  // whole-design FULLY_STATIC invariant at the same time is rejected.
  plan.flags |= TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTCleanSuperstepExecutesCertifiedNativeActor) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.run = aotRunOneNode;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(100);
  fixture.descriptor.execution = &execution;
  schedulerOrder.clear();
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{100}));
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 1u);
  EXPECT_EQ(state.actors[0], nullptr);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTCleanSuperstepDirectNodeCallUsesLockedGenericPath) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(100);
  fixture.descriptor.execution = &execution;
  schedulerOrder.clear();
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  constexpr talos_rt_native_schedule_node nodes[] = {{0, 0, 0}};
  ASSERT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{100}));
  EXPECT_EQ(state.actors[0], nullptr);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTGroupedStaticActivationSuppressesDuplicateWake) {
  AOTTestState state;
  state.runHook = runGroupedStaticActivationNodes;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 2);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture source(90);
  SchedulerFixture target(91);
  source.descriptor.execution = &execution;
  target.descriptor.execution = &execution;
  source.descriptor.native_execute = groupedStaticActivationExecute;
  target.descriptor.native_execute = groupedStaticActivationExecute;
  schedulerOrder.clear();
  schedulerResumeCount = 0;
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(source), 0, 0,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(target), 0, 1,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{90, 91}));
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->signalDiagnostics.aotFanoutEntries, 1u);
  EXPECT_EQ(state.actors[0], nullptr);
  EXPECT_EQ(state.actors[1], nullptr);
  talos_rt_v1_context_destroy(context);
}

class SchedulerReadySet : public testing::TestWithParam<uint32_t> {};

TEST_P(SchedulerReadySet, SparseCrossWordActivationAndDuplicateSuppression) {
  for (bool backward : {false, true}) {
    SCOPED_TRACE(backward);
    AOTTestState state;
    state.runHook = runGroupedStaticActivationNodes;
    uint32_t count = GetParam();
    // Unreached continuations space the two live actors across word and
    // summary boundaries without thousands of unrelated actor executions.
    for (uint32_t node = 0; node < count; ++node)
      state.testNodes.push_back({0, node + 2, UINT32_MAX});
    state.testNodes[1] = {0, 0, UINT32_MAX};
    state.testNodes[2] = {1, 0, UINT32_MAX};
    state.testNodes[backward ? count - 1 : 0] = {0, 1, UINT32_MAX};
    state.testNodes[backward ? 0 : count - 1] = {1, 1, UINT32_MAX};
    auto plan = makeAOTPlan(state);
    plan.flags = TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
                 TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
                 TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
                 TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
                 TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
    talos_rt_execution_descriptor_v1 execution{};
    execution.version = TALOS_RT_VERSION;
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
              TALOS_RT_OK);
    SchedulerFixture source(90), target(91);
    std::vector<uint32_t> continuations;
    for (uint32_t continuation = 0; continuation < count + 2; ++continuation)
      continuations.push_back(continuation);
    source.layout.continuation_count = continuations.size();
    source.layout.continuations = continuations.data();
    source.layout.checksum = checksum(source.layout);
    for (auto *fixture : {&source, &target}) {
      fixture->descriptor.execution = &execution;
      fixture->descriptor.native_execute = groupedStaticActivationExecute;
    }
    schedulerOrder.clear();
    schedulerResumeCount = 0;
    ASSERT_EQ(talos_rt_v1_scheduler_add_aot(
                  context, makeSchedulerInstance(source), 0, 0, 0, nullptr,
                  nullptr, 0, nullptr, 0),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_add_aot(
                  context, makeSchedulerInstance(target), 0, 1, 0, nullptr,
                  nullptr, 0, nullptr, 0),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
    EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{90, 91}));
    EXPECT_EQ(schedulerResumeCount, 1u);
    EXPECT_EQ(context->signalDiagnostics.aotFanoutEntries, 1u);
    EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 4u);
    EXPECT_EQ(context->nativeScheduleReadyNodes.findFirst(), UINT32_MAX);
    EXPECT_EQ(context->nativeScheduleReadyNodes.capacity(), count);
    talos_rt_v1_context_destroy(context);
  }
}

INSTANTIATE_TEST_SUITE_P(Boundaries, SchedulerReadySet,
                         testing::Values(4u, 64u, 65u, 128u, 129u, 2048u, 2049u,
                                         4097u));

TEST(Scheduler, AOTCleanSuperstepSnapshotsContinuationRankForHandover) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.run = aotRunOneNodeThenFallback;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(4);
  fixture.descriptor.execution = &execution;
  schedulerWaitKind = TALOS_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = 1;
  schedulerWaitWidth = 1;
  constexpr uint32_t continuation = 1;
  constexpr uint32_t continuationRank = 42;
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(
                context, makeSchedulerInstance(fixture), 0, 0, 0, &continuation,
                &continuationRank, 1, nullptr, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_TRUE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 1u);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_EQ(context->scheduledProcesses.front().scheduleRank, continuationRank);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GenericSpecializationUsesBoundPlanesAndScopedOverrides) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 16;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  uint32_t fast = 0;
  EXPECT_EQ(talos_rt_v1_native_state_bind_specialization(context, &fast),
            TALOS_RT_INVALID_LIFECYCLE);
  std::array<uint8_t, 2> value{0x12, 0x34}, unknown{0, 0};
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 8, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_sync(context, value.data(),
                                            unknown.data(), 16),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_bind_specialization(context, &fast),
            TALOS_RT_OK);
  EXPECT_EQ(fast, 1u);
  uint64_t handle =
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_STATIC, 1, 0);
  uint8_t forced = 0xa5;
  ASSERT_EQ(talos_rt_v1_native_override(
                context, value.data(), unknown.data(), 16, handle, 8,
                TALOS_RT_DESCRIPTOR_STORAGE, 0, &forced, nullptr),
            TALOS_RT_OK);
  EXPECT_EQ(fast, 0u);
  auto allowed = [&](uint32_t root) {
    return talos_rt_v1_static_specialization_guard(
        context, UINT32_MAX, root,
        TALOS_RT_STATIC_ROOT_READ | TALOS_RT_STATIC_ROOT_WRITE);
  };
  EXPECT_EQ(allowed(1), 0u);
  EXPECT_EQ(allowed(2), 1u);
  EXPECT_EQ(allowed(3), 0u);
  refreshNativeStaticSpecializationFastUnlocked(context);
  EXPECT_EQ(fast, 0u);
  context->observerForcesCanonicalPlane = true;
  EXPECT_EQ(allowed(2), 0u);
  context->observerForcesCanonicalPlane = false;
  context->forceMask[0] = 0;
  refreshNativeStaticSpecializationFastUnlocked(context);
  EXPECT_EQ(fast, 1u);
  talos_rt_aot_observation_demand_changed_unlocked(context, true);
  EXPECT_EQ(fast, 0u);
  refreshNativeStaticSpecializationFastUnlocked(context);
  EXPECT_EQ(fast, 0u);
  talos_rt_aot_observation_demand_changed_unlocked(context, false);
  refreshNativeStaticSpecializationFastUnlocked(context);
  EXPECT_EQ(fast, 1u);
  talos_rt_aot_external_write_unlocked(context);
  EXPECT_EQ(fast, 0u);
  refreshNativeStaticSpecializationFastUnlocked(context);
  EXPECT_EQ(fast, 1u);
  talos_rt_v1_context_destroy(context);
  EXPECT_EQ(fast, 0u);
}

TEST(Scheduler, BoundContinuousPlanesRetainPartialContributions) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 16;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  uint8_t *retained = nullptr, *unknown = nullptr, *mask = nullptr;
  EXPECT_EQ(talos_rt_v1_native_state_bind_continuous(context, &retained,
                                                       &unknown, &mask),
            TALOS_RT_INVALID_LIFECYCLE);
  std::array<uint8_t, 2> visible{0x12, 0x34}, visibleUnknown{0, 0};
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 16),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_sync(context, visible.data(),
                                            visibleUnknown.data(), 16),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_bind_continuous(context, &retained,
                                                       &unknown, &mask),
            TALOS_RT_OK);
  EXPECT_EQ(mask[0], 0);
  EXPECT_EQ(mask[1], 0);
  // Simulate a generated partial clean store, retaining X and its exact mask.
  retained[0] = 0xa5;
  unknown[0] = 0xf0;
  mask[0] = 0xff;
  uint64_t high =
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_STATIC, 1, 8);
  uint8_t contribution = 0x78, changed = 0;
  ASSERT_EQ(
      talos_rt_v1_native_state_store_continuous_plane(
          context, visible.data(), 16, high, 8, 0, &contribution, &changed),
      TALOS_RT_OK);
  EXPECT_EQ(retained[0], 0xa5);
  EXPECT_EQ(unknown[0], 0xf0);
  EXPECT_EQ(retained[1], 0x78);
  EXPECT_EQ(mask[1], 0xff);
  uint8_t *againValue = nullptr, *againUnknown = nullptr, *againMask = nullptr;
  ASSERT_EQ(talos_rt_v1_native_state_bind_continuous(
                context, &againValue, &againUnknown, &againMask),
            TALOS_RT_OK);
  EXPECT_EQ(againValue, retained);
  EXPECT_EQ(againUnknown, unknown);
  EXPECT_EQ(againMask, mask);
  EXPECT_EQ(context->continuousValue[0], 0x78a5u);
  EXPECT_EQ(context->continuousUnknown[0], 0xf0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, StaticSpecializationGuardsIntersectOnlyDirtyRoots) {
  AOTTestState state;
  constexpr talos_rt_static_actor_root dependencies[] = {
      {0, 1, TALOS_RT_STATIC_ROOT_READ, 0},
      {1, 2, TALOS_RT_STATIC_ROOT_WRITE, 0},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  uint8_t stateValue[2] = {};
  uint8_t stateUnknown[2] = {};
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = stateValue;
  plan.state_unknown = stateUnknown;
  plan.state_bit_count = 16;
  plan.actor_roots = dependencies;
  plan.actor_root_count = std::size(dependencies);
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 16;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 8, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture cleanFixture(202);
  cleanFixture.descriptor.execution = &execution;
  talos_rt_process_instance_v1 *cleanActor =
      makeSchedulerInstance(cleanFixture);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context, cleanActor, 1, 0, 0,
                                            nullptr, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  // Tier construction was already validated. Advertising bytecode here is
  // enough to exercise root-local handover selection without building an
  // unrelated interpreter program for this guard test.
  cleanFixture.descriptor.available_tiers |= TALOS_RT_TIER_MASK_BYTECODE;

  EXPECT_EQ(talos_rt_v1_static_specialization_guard(
                context, 0, 1, TALOS_RT_STATIC_ROOT_READ),
            1u);
  EXPECT_EQ(talos_rt_v1_static_specialization_guard(
                context, 1, 2, TALOS_RT_STATIC_ROOT_WRITE),
            1u);
  EXPECT_EQ(cleanActor->tier, 0u);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_external_write_range_unlocked(context, 0, 8, false);
  }
  EXPECT_EQ(talos_rt_v1_static_specialization_guard(
                context, 0, 1, TALOS_RT_STATIC_ROOT_READ),
            0u);
  EXPECT_EQ(talos_rt_v1_static_specialization_guard(
                context, 1, 2, TALOS_RT_STATIC_ROOT_WRITE),
            1u);

  context->nativeScheduleTransientDirtyRoots.clear();
  std::fill(context->nativeScheduleTransientDirtyMask.begin(),
            context->nativeScheduleTransientDirtyMask.end(), uint64_t{0});
  std::fill(context->nativeScheduleTransientDirtySummary.begin(),
            context->nativeScheduleTransientDirtySummary.end(), uint64_t{0});
  context->nativeScheduleDirtyRootsPresent = false;
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_external_write_range_unlocked(context, 8, 8, true);
  }
  EXPECT_EQ(talos_rt_v1_static_specialization_guard(
                context, 1, 2, TALOS_RT_STATIC_ROOT_WRITE),
            0u);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_release_range_unlocked(context, 8, 8);
  }
  EXPECT_EQ(talos_rt_v1_static_specialization_guard(
                context, 1, 2, TALOS_RT_STATIC_ROOT_WRITE),
            1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, StaticStateRangeIndexPreservesCanonicalHandleSemantics) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 512;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 9, 400, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 7, 100, 300),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 3, 120, 64),
            TALOS_RT_OK);

  talos_rt_stable_handle_v1 decoded{};
  uint64_t handle = talos_rt_canonical_state_handle_unlocked(context, 130, 4);
  ASSERT_TRUE(talos_rt_stable_handle_decode(handle, &decoded));
  EXPECT_EQ(decoded.kind, TALOS_RT_STABLE_HANDLE_STATIC);
  EXPECT_EQ(decoded.id, 3u);
  EXPECT_EQ(decoded.offset, 10);
  EXPECT_TRUE(context->nativeStaticStateRangesValid);

  // Registering another range invalidates the lazy index. The lowest static
  // ID remains the canonical choice when multiple roots contain a range.
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 128, 8),
            TALOS_RT_OK);
  EXPECT_FALSE(context->nativeStaticStateRangesValid);
  handle = talos_rt_canonical_state_handle_unlocked(context, 130, 4);
  ASSERT_TRUE(talos_rt_stable_handle_decode(handle, &decoded));
  EXPECT_EQ(decoded.kind, TALOS_RT_STABLE_HANDLE_STATIC);
  EXPECT_EQ(decoded.id, 2u);
  EXPECT_EQ(decoded.offset, 2);

  handle = talos_rt_canonical_state_handle_unlocked(context, 450, 4);
  ASSERT_TRUE(talos_rt_stable_handle_decode(handle, &decoded));
  EXPECT_EQ(decoded.kind, TALOS_RT_STABLE_HANDLE_GLOBAL);
  EXPECT_EQ(decoded.offset, 450);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ExternalDirtyRootIndexSummarizesLeafPages) {
  AOTTestState state;
  uint8_t valuePlane = 0;
  uint8_t unknownPlane = 0;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = &valuePlane;
  plan.state_unknown = &unknownPlane;
  plan.state_bit_count = 2;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 2;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  for (uint32_t id = 1; id != 65; ++id)
    ASSERT_EQ(talos_rt_v1_native_state_register_static(context, id, 0, 1),
              TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 65, 1, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  uint64_t root = talos_rt_canonical_state_handle_unlocked(context, 1, 1);
  ASSERT_NE(root, UINT64_MAX);

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_external_write_handle_unlocked(context, root, 1, 1, true);
  }
  ASSERT_EQ(context->nativeSchedulePersistentDirtyMask.size(), 2u);
  ASSERT_EQ(context->nativeSchedulePersistentDirtySummary.size(), 1u);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyMask[0], 0u);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyMask[1], uint64_t{1} << 1);
  EXPECT_EQ(context->nativeSchedulePersistentDirtySummary[0], uint64_t{1} << 1);
  EXPECT_EQ(talos_rt_v1_static_specialization_guard(
                context, UINT32_MAX, 65, TALOS_RT_STATIC_ROOT_READ),
            0u);

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_release_range_unlocked(context, 1, 1);
  }
  EXPECT_EQ(context->nativeSchedulePersistentDirtyMask[1], 0u);
  EXPECT_EQ(context->nativeSchedulePersistentDirtySummary[0], 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ExternalDepositTouchesOnlyIntersectingStaticRoots) {
  AOTTestState state;
  std::array<uint8_t, 2> valuePlane{};
  std::array<uint8_t, 2> unknownPlane{};
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 16;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 16;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 8, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 3, 4, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  uint64_t handle = talos_rt_canonical_state_handle_unlocked(context, 8, 4);
  context->stateValue[0] = UINT64_C(0x0ab0);
  ASSERT_TRUE(talos_rt_aot_external_deposit_unlocked(context, handle, 8, 4));
  EXPECT_EQ(valuePlane[0], 0xb0);
  EXPECT_EQ(valuePlane[1] & 0x0f, 0x0a);
  EXPECT_FALSE(context->nativeScheduleDirtyRootsPresent);

  talos_rt_aot_external_write_handle_unlocked(context, handle, 8, 4, false);
  EXPECT_EQ(context->nativeScheduleTransientDirtyRoots.count(1), 0u);
  EXPECT_EQ(context->nativeScheduleTransientDirtyRoots.count(2), 1u);
  EXPECT_EQ(context->nativeScheduleTransientDirtyRoots.count(3), 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ExactMutationFootprintsUseScopedProofInvalidation) {
  AOTTestState state;
  std::array<uint8_t, 2> value{}, unknown{};
  auto plan = makeAOTPlan(state);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = value.data();
  plan.state_unknown = unknown.data();
  plan.state_bit_count = 16;
  static std::vector<std::pair<uint64_t, uint64_t>> invalidated;
  static std::vector<std::pair<uint64_t, uint64_t>> rechecked;
  static std::array<bool, 3> certificates;
  invalidated.clear();
  rechecked.clear();
  certificates.fill(true);
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  plan.promotion_invalidate_range = [](uint64_t offset, uint64_t width) {
    invalidated.emplace_back(offset, width);
    constexpr uint64_t starts[] = {0, 4, 12};
    for (size_t index = 0; index != certificates.size(); ++index)
      if (offset < starts[index] + 4 && starts[index] < offset + width)
        certificates[index] = false;
  };
  plan.promotion_recheck_range = [](uint64_t offset, uint64_t width) {
    rechecked.emplace_back(offset, width);
  };
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 16;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 16),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  schedulerPromotionInvalidationCount = 0;
  uint64_t handle = talos_rt_canonical_state_handle_unlocked(context, 3, 1);

  // VPI first synchronizes the bound coroutine plane, then reconciles the
  // deposited root. The first copy must publish the delta: reconciliation
  // cannot discover it once the native unknown bits have been overwritten.
  context->nativeStateValue = value.data();
  context->nativeStateUnknown = unknown.data();
  context->nativeStateBitCount = 16;

  // Indexed X deposits invalidate their canonical packed range before a
  // dependent executor can run. Unrelated certificates remain valid.
  context->stateUnknown[0] = uint64_t{1} << 3;
  talos_rt_sync_native_state_range_unlocked(context, 3, 1);
  EXPECT_EQ(invalidated, (std::vector<std::pair<uint64_t, uint64_t>>{{3, 1}}));
  ASSERT_TRUE(talos_rt_aot_external_deposit_unlocked(context, handle, 3, 1));
  EXPECT_EQ(invalidated, (std::vector<std::pair<uint64_t, uint64_t>>{{3, 1}}));
  EXPECT_EQ(certificates, (std::array<bool, 3>{false, true, true}));
  EXPECT_EQ(schedulerPromotionInvalidationCount, 0u);
  EXPECT_FALSE(context->nativeScheduleExternalWritePending);
  EXPECT_EQ(unknown[0], uint8_t{8});

  // A known write to another lane does not invalidate knownness because an
  // unrelated lane still contains X. Recovery queues a failed proof without
  // demoting positive certificates or prematurely setting a failed one true.
  uint64_t other = talos_rt_canonical_state_handle_unlocked(context, 8, 1);
  context->stateValue[0] = uint64_t{1} << 8;
  ASSERT_TRUE(talos_rt_aot_external_deposit_unlocked(context, other, 8, 1));
  EXPECT_EQ(invalidated.size(), 1u);
  EXPECT_TRUE(rechecked.empty());
  EXPECT_EQ(unknown[0], uint8_t{8});
  context->stateUnknown[0] = 0;
  talos_rt_sync_native_state_range_unlocked(context, 3, 1);
  EXPECT_EQ(rechecked, (std::vector<std::pair<uint64_t, uint64_t>>{{3, 1}}));
  ASSERT_TRUE(talos_rt_aot_external_deposit_unlocked(context, handle, 3, 1));
  EXPECT_EQ(invalidated.size(), 1u);
  EXPECT_EQ(rechecked, (std::vector<std::pair<uint64_t, uint64_t>>{{3, 1}}));
  EXPECT_EQ(certificates, (std::array<bool, 3>{false, true, true}));
  EXPECT_EQ(unknown[0], uint8_t{0});

  // Force/assign scheduling disturbance is separate from the proof footprint.
  talos_rt_aot_external_write_handle_unlocked(context, other, 8, 1, true);
  EXPECT_TRUE(context->nativeScheduleExternalWritePending);
  EXPECT_EQ(invalidated.back(), (std::pair<uint64_t, uint64_t>{8, 1}));
  EXPECT_EQ(certificates, (std::array<bool, 3>{false, true, true}));
  EXPECT_EQ(schedulerPromotionInvalidationCount, 0u);
  talos_rt_aot_external_write_range_unlocked(context, 4, 1, false);
  EXPECT_EQ(certificates, (std::array<bool, 3>{false, false, true}));

  // Shared native stores must publish the same exact loss and recovery even
  // when no later deposit reconciliation takes place.
  invalidated.clear();
  rechecked.clear();
  ASSERT_TRUE(storeNativeScheduleStateUnlocked(context, 12, 4, 0, 3));
  EXPECT_EQ(invalidated, (std::vector<std::pair<uint64_t, uint64_t>>{{12, 2}}));
  ASSERT_TRUE(storeNativeScheduleStateUnlocked(context, 12, 4, 9, 0));
  EXPECT_EQ(rechecked, (std::vector<std::pair<uint64_t, uint64_t>>{{12, 2}}));
  ASSERT_TRUE(storeNativeScheduleStateUnlocked(context, 12, 4, 5, 0));
  EXPECT_EQ(invalidated.size(), 1u);
  EXPECT_EQ(rechecked.size(), 1u);

  // Empty writes do nothing; malformed and unknown footprints retain the
  // mandatory conservative invalidator even when a scoped hook is installed.
  invalidateNativeTwoStatePromotionRangeUnlocked(context, UINT64_MAX, 0);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 0u);
  invalidateNativeTwoStatePromotionRangeUnlocked(context, UINT64_MAX, 2);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 1u);
  talos_rt_aot_external_write_unlocked(context);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 2u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ScopedPromotionHookRequiresUnknownFootprintFallback) {
  AOTTestState state;
  auto plan = makeAOTPlan(state);
  plan.promotion_invalidate_range = [](uint64_t, uint64_t) {};
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);
  plan.promotion_invalidate_range = nullptr;
  plan.promotion_recheck_range = [](uint64_t, uint64_t) {};
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, KnownnessPublicationMatchesScalarBitTransitions) {
  static std::array<uint8_t, 128> notifications;
  talos_rt_native_schedule_plan plan{};
  plan.state_bit_count = notifications.size();
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  plan.promotion_invalidate_range = [](uint64_t offset, uint64_t width) {
    ASSERT_GT(width, 0u);
    ASSERT_LE(offset + width, notifications.size());
    for (uint64_t bit = offset; bit != offset + width; ++bit) {
      EXPECT_EQ(notifications[bit], 0u);
      notifications[bit] = 1;
    }
  };
  plan.promotion_recheck_range = [](uint64_t offset, uint64_t width) {
    ASSERT_GT(width, 0u);
    ASSERT_LE(offset + width, notifications.size());
    for (uint64_t bit = offset; bit != offset + width; ++bit) {
      EXPECT_EQ(notifications[bit], 0u);
      notifications[bit] = 2;
    }
  };
  schedulerPromotionInvalidationCount = 0;
  uint64_t random = 0x941da377ab192403;
  auto next = [&] {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    return random;
  };
  for (unsigned trial = 0; trial != 260; ++trial) {
    uint64_t before = trial < 2 ? (trial ? UINT64_MAX : 0) : next();
    uint64_t after = trial < 2 ? ~before : next();
    unsigned width = trial < 2 ? 64 : trial % 65;
    notifications.fill(0);
    publishNativeKnownnessChangeUnlocked(&plan, 13, width, before, after);
    for (unsigned bit = 0; bit != notifications.size(); ++bit) {
      unsigned expected = 0;
      if (bit >= 13 && bit < 13 + width) {
        bool oldUnknown = (before >> (bit - 13)) & 1;
        bool newUnknown = (after >> (bit - 13)) & 1;
        if (oldUnknown != newUnknown)
          expected = newUnknown ? 1 : 2;
      }
      EXPECT_EQ(notifications[bit], expected) << trial << ":" << bit;
    }
  }
  EXPECT_EQ(schedulerPromotionInvalidationCount, 0u);
  // Reject unrepresentable widths before evaluating any shift or end offset.
  publishNativeKnownnessChangeUnlocked(&plan, 0, 65, 0, 1);
  publishNativeKnownnessChangeUnlocked(&plan, UINT64_MAX, 2, 0, 1);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 2u);
  plan.promotion_recheck_range = nullptr;
  publishNativeKnownnessChangeUnlocked(&plan, 0, 64, UINT64_MAX, 0);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 3u);
}

TEST(Scheduler, NativePlaneFillsPreserveBitsAndInitializeOnce) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 195;
  alignas(8) std::array<uint8_t, 40> value, unknown;
  constexpr talos_rt_native_state_fill_v1 fills[] = {
      {3, 4, 1, 0}, {9, 71, 0, 1}, {81, 66, 1, 1}, {160, 35, 1, 0}};
  std::array<uint8_t, 40> expectedValue{}, expectedUnknown{};
  for (size_t i = 33; i < 40; ++i)
    expectedValue[i] = expectedUnknown[i] = 0xa5;
  for (const auto &fill : fills)
    for (uint64_t bit = fill.bit_offset; bit < fill.bit_offset + fill.bit_width;
         ++bit) {
      if (fill.value)
        expectedValue[bit / 8] |= uint8_t(1u << (bit % 8));
      if (fill.unknown)
        expectedUnknown[bit / 8] |= uint8_t(1u << (bit % 8));
    }
  for (unsigned instance = 0; instance < 2; ++instance) {
    value.fill(0xa5);
    unknown.fill(0xa5);
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
              TALOS_RT_OK);
    const talos_rt_native_state_fill_v1 malformed[] = {{0, 8, 1, 1},
                                                         {7, 1, 0, 1}};
    EXPECT_EQ(talos_rt_v1_native_state_initialize(
                  context, value.data(), unknown.data(), 195, malformed, 2),
              TALOS_RT_INVALID_ARGUMENT);
    EXPECT_EQ(value.front(), 0xa5);
    EXPECT_EQ(unknown.front(), 0xa5);
    ASSERT_EQ(talos_rt_v1_native_state_initialize(
                  context, value.data(), unknown.data(), 195, fills, 4),
              TALOS_RT_OK);
    EXPECT_EQ(value, expectedValue);
    EXPECT_EQ(unknown, expectedUnknown);
    value[0] = 0x5a;
    EXPECT_EQ(talos_rt_v1_native_state_initialize(
                  context, value.data(), unknown.data(), 195, fills, 4),
              TALOS_RT_INVALID_LIFECYCLE);
    EXPECT_EQ(value[0], 0x5a);
    ASSERT_EQ(talos_rt_v1_native_state_bind_shared(context, value.data(),
                                                     unknown.data(), 195),
              TALOS_RT_OK);
    EXPECT_EQ(context->stateValue.data(),
              reinterpret_cast<uint64_t *>(value.data()));
    EXPECT_EQ(context->stateValue[0] & 0xff, 0x5au);
    talos_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, SharedPlanesKeepInitializersTailAndKnownnessNotifications) {
  // IEEE 1800-2023 6.8, 9.4.2: sharing must preserve compiler initial values
  // and invalidate/recheck two-state proofs even after a runtime write has
  // overwritten the old unknown bits in the authoritative storage.
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 67;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  std::array<uint64_t, 2> value{0x1234, UINT64_MAX};
  std::array<uint64_t, 2> unknown{0, UINT64_MAX};
  auto *v = reinterpret_cast<uint8_t *>(value.data());
  auto *u = reinterpret_cast<uint8_t *>(unknown.data());
  // A DPI export shares the image (IEEE 1800-2023 35.5.3); a coverage
  // schema keeps its own.
  execution.flags = TALOS_RT_EXECUTION_COVERAGE_SCHEMA;
  EXPECT_EQ(talos_rt_v1_native_state_bind_shared(context, v, u, 67),
            TALOS_RT_INVALID_LIFECYCLE);
  execution.flags = 0;
  execution.observer_count = 1;
  EXPECT_EQ(talos_rt_v1_native_state_bind_shared(context, v, u, 67),
            TALOS_RT_INVALID_LIFECYCLE);
  execution.observer_count = 0;
  EXPECT_EQ(talos_rt_v1_native_state_bind_shared(context, v + 1, u, 67),
            TALOS_RT_INVALID_ARGUMENT);
  ASSERT_EQ(talos_rt_v1_native_state_bind_shared(context, v, u, 67),
            TALOS_RT_OK);
  EXPECT_EQ(context->stateValue.data(), value.data());
  EXPECT_EQ(context->stateUnknown.data(), unknown.data());
  EXPECT_EQ(value[0], 0x1234u);
  EXPECT_EQ(value[1], 7u);
  EXPECT_EQ(unknown[1], 7u);
  EXPECT_TRUE(importNativeStatePlanesUnlocked(context, v, u, 67));
  EXPECT_TRUE(exportNativeStatePlanesUnlocked(context, v, u, 67));
  EXPECT_EQ(value[0], 0x1234u);
  EXPECT_EQ(talos_rt_v1_native_state_bind_shared(context, v, u, 67),
            TALOS_RT_INVALID_LIFECYCLE);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 64, 3),
            TALOS_RT_OK);
  static uint64_t lost, recovered;
  lost = recovered = 0;
  talos_rt_native_schedule_plan plan{};
  plan.state_value = v;
  plan.state_unknown = u;
  plan.state_bit_count = 67;
  plan.promotion_invalidate_range = [](uint64_t offset, uint64_t width) {
    EXPECT_GE(offset, 64u);
    EXPECT_LE(offset + width, 67u);
    lost |= packedWidthMask(width) << (offset - 64);
  };
  plan.promotion_recheck_range = [](uint64_t offset, uint64_t width) {
    EXPECT_GE(offset, 64u);
    EXPECT_LE(offset + width, 67u);
    recovered |= packedWidthMask(width) << (offset - 64);
  };
  context->nativeSchedulePlan = &plan;
  context->stateUnknown[1] = 2;
  talos_rt_sync_native_state_range_unlocked(context, 64, 3);
  EXPECT_EQ(lost, 2u);
  EXPECT_EQ(recovered, 5u);
  lost = recovered = 0;
  context->stateUnknown[1] = 0;
  EXPECT_TRUE(storeNativeScheduleStateUnlocked(context, 64, 3, 4, 0));
  EXPECT_EQ(lost, 0u);
  EXPECT_EQ(recovered, 7u);
  EXPECT_EQ(value[1], 4u);
  EXPECT_EQ(unknown[1], 0u);
  // LRM 6.11.2, 38.34: a captured pre-commit plane identifies only the
  // bits that lose or recover knownness, including a write of unchanged X.
  lost = recovered = 0;
  EXPECT_TRUE(storeNativeScheduleStateUnlocked(context, 64, 3, 4, 2, 0));
  EXPECT_EQ(lost, 2u);
  EXPECT_EQ(recovered, 0u);
  lost = recovered = 0;
  EXPECT_TRUE(storeNativeScheduleStateUnlocked(context, 64, 3, 4, 2, 2));
  EXPECT_EQ(lost | recovered, 0u);
  EXPECT_TRUE(storeNativeScheduleStateUnlocked(context, 64, 3, 4, 0, 2));
  EXPECT_EQ(lost, 0u);
  EXPECT_EQ(recovered, 2u);
  // IEEE 1800-2023 4.6(a): publishing an earlier source write must not
  // restore its value over a subsequent store already in the shared plane.
  uint8_t oldValue = 0, newValue = 1, known = 0;
  talos_rt_v1_scheduler_signal_transition(
      context, talos_rt_v1_native_state_static_handle(1), 3, &oldValue, &known,
      &newValue, &known);
  EXPECT_EQ(value[1], 4u);
  std::array<uint8_t, 16> other{};
  EXPECT_FALSE(importNativeStatePlanesUnlocked(context, other.data(), u, 67));
  context->nativeSchedulePlan = nullptr;
  talos_rt_v1_context_destroy(context);
  EXPECT_EQ(value[0], 0x1234u);
}

TEST(Scheduler, AOTSpecializationFastFlagIsScopedAndInvalidated) {
  AOTTestState state;
  uint32_t specializationFast = UINT32_MAX;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE |
               TALOS_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION;
  plan.specialization_fast = &specializationFast;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  EXPECT_EQ(specializationFast, 0u);

  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(state.observedSpecializationFast, 1u);
  EXPECT_EQ(specializationFast, 0u);

  specializationFast = 1;
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_external_write_unlocked(context);
  }
  EXPECT_EQ(specializationFast, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, InactiveVPIUsesSharedPlanesAndReadersDetachAtBoundaries) {
  // IEEE 1800-2023 4.6, 38.36: readers retain a publication snapshot;
  // declaring VPI capabilities alone does not require a second state image.
  for (uint32_t flags :
       {TALOS_RT_EXECUTION_VPI_READ,
        TALOS_RT_EXECUTION_VPI_READ | TALOS_RT_EXECUTION_VPI_WRITE}) {
    talos_rt_execution_descriptor_v1 execution{};
    execution.version = TALOS_RT_VERSION;
    execution.state_bit_count = 16;
    execution.flags = flags;
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
              TALOS_RT_OK);
    std::array<uint64_t, 2> value{0x1234, 0}, unknown{0x8000, 0};
    auto *v = reinterpret_cast<uint8_t *>(value.data());
    auto *u = reinterpret_cast<uint8_t *>(unknown.data());
    talos_rt_aot_observation_demand_changed_unlocked(context, true);
    EXPECT_EQ(talos_rt_v1_native_state_bind_shared(context, v, u, 16),
              TALOS_RT_INVALID_LIFECYCLE);
    talos_rt_aot_observation_demand_changed_unlocked(context, false);
    ASSERT_EQ(talos_rt_v1_native_state_bind_shared(context, v, u, 16),
              TALOS_RT_OK);
    uint32_t fast = 1;
    talos_rt_native_schedule_plan plan{};
    plan.state_value = v;
    plan.state_unknown = u;
    plan.state_bit_count = 16;
    plan.specialization_fast = &fast;
    plan.flags = TALOS_RT_NATIVE_SCHEDULE_GUARDED_FANOUT;
    context->nativeSchedulePlan = &plan;
    context->nativeScheduleRunning = true;
    uint32_t stateFast = 0;
    context->nativeStateSpecializationFast = &stateFast;
    context->staticNBASlowRootsPresent = true;
    fast = 0;
    refreshNativeStaticSpecializationFastUnlocked(context);
    EXPECT_EQ(stateFast, 1u);
    EXPECT_EQ(fast, 0u);
    context->staticNBASlowRootsPresent = false;
    refreshNativeStaticSpecializationFastUnlocked(context);
    EXPECT_EQ(fast, 1u);
    EXPECT_TRUE(canUseStaticAOTFanout(context));
    talos_rt_aot_observation_demand_changed_unlocked(context, true);
    EXPECT_FALSE(context->stateValue.shared());
    EXPECT_FALSE(context->stateUnknown.shared());
    EXPECT_EQ(fast, 0u);
    EXPECT_EQ(stateFast, 0u);
    EXPECT_FALSE(canUseStaticAOTFanout(context));
    value[0] = 0xfeed;
    unknown[0] = 0;
    EXPECT_EQ(context->stateValue[0], 0x1234u);
    EXPECT_EQ(context->stateUnknown[0], 0x8000u);
    context->stateValue[0] = 0xbeef;
    context->stateUnknown[0] = 0x4000;
    context->observerDepth = 1;
    talos_rt_aot_observation_demand_changed_unlocked(context, false);
    refreshNativeStaticSpecializationFastUnlocked(context);
    EXPECT_EQ(fast, 0u);
    EXPECT_FALSE(context->stateValue.shared());
    context->observerDepth = 0;
    // Resuming a generated plan may have already enabled its clean flag.
    fast = 1;
    refreshNativeStaticSpecializationFastUnlocked(context);
    EXPECT_EQ(fast, 1u);
    EXPECT_TRUE(canUseStaticAOTFanout(context));
    EXPECT_TRUE(context->stateValue.shared());
    EXPECT_TRUE(context->stateUnknown.shared());
    EXPECT_EQ(value[0], 0xbeefu);
    EXPECT_EQ(unknown[0], 0x4000u);
    EXPECT_EQ(context->schedulerStatus, TALOS_RT_OK);
    context->nativeScheduleRunning = false;
    fast = 0;
    refreshNativeStaticSpecializationFastUnlocked(context);
    EXPECT_EQ(fast, 0u);
    plan.flags |= TALOS_RT_NATIVE_SCHEDULE_RUNTIME_CALENDAR_EVAL;
    refreshNativeStaticSpecializationFastUnlocked(context);
    EXPECT_EQ(fast, 1u);
    fast = 0;
    context->nativeScheduleExternalWritePending = true;
    refreshNativeStaticSpecializationFastUnlocked(context);
    EXPECT_EQ(fast, 0u);
    context->nativeScheduleExternalWritePending = false;
    context->forceMask.assign(1, 1);
    refreshNativeStaticSpecializationFastUnlocked(context);
    EXPECT_EQ(fast, 0u);
    EXPECT_EQ(stateFast, 0u);
    context->nativeSchedulePlan = nullptr;
    talos_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, VPIObservationDemandIsAColdReversibleAOTHandoff) {
  AOTTestState state;
  uint32_t specializationFast = 1;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE |
               TALOS_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION;
  plan.specialization_fast = &specializationFast;
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  schedulerPromotionInvalidationCount = 0;

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  schedulerPromotionInvalidationCount = 0;
  specializationFast = 1;

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_observation_demand_changed_unlocked(context, true);
  }
  EXPECT_TRUE(context->vpiObservationDemand);
  // The generic static-inventory predicate remains identical to VPI-off and
  // therefore carries no observer-demand load in generated/node hot paths.
  EXPECT_TRUE(nativeStaticSpecializationEnvironmentClean(context));
  EXPECT_FALSE(nativeAOTTransientBoundaryClean(context));
  EXPECT_EQ(specializationFast, 0u);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 0u);
  EXPECT_FALSE(context->nativeScheduleExternalWritePending);
  EXPECT_FALSE(context->nativeScheduleDirtyRootsPresent);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  context->nativeScheduleRunning = true;
  refreshNativeStaticSpecializationFastUnlocked(context);
  EXPECT_EQ(specializationFast, 0u);

  // Duplicate notification is idempotent; the VPI registry owns the actual
  // registration count and calls this only for first/last transitions.
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_observation_demand_changed_unlocked(context, true);
    talos_rt_aot_observation_demand_changed_unlocked(context, false);
    refreshNativeStaticSpecializationFastUnlocked(context);
  }
  EXPECT_FALSE(context->vpiObservationDemand);
  EXPECT_TRUE(nativeStaticSpecializationEnvironmentClean(context));
  EXPECT_TRUE(nativeAOTTransientBoundaryClean(context));
  EXPECT_EQ(specializationFast, 1u);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 0u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  context->nativeScheduleRunning = false;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, VPIObservationDemandRoutesAroundTier1UntilReleased) {
  AOTTestState state;
  state.runHook = [](AOTTestState *state, talos_rt_context *context) {
    ++state->runCalls;
    return talos_rt_v1_scheduler_run(context);
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(state.runCalls, 1u);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_observation_demand_changed_unlocked(context, true);
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(state.runCalls, 1u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_observation_demand_changed_unlocked(context, false);
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(state.runCalls, 2u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTObserverInventoryTracksLiveComputedWaitsOnly) {
  constexpr uint64_t observerID = 101;
  talos_rt_observer_descriptor_v1 observer{observerID,
                                             nullptr,
                                             0,
                                             1,
                                             0,
                                             TALOS_RT_OBSERVER_NO_BYTECODE,
                                             collapsedAliasObserverEvaluator,
                                             0};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.observers = &observer;
  execution.observer_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);

  // A descriptor is immutable evaluator inventory. Clause 31.7 invokes its
  // timing evaluator synchronously from an already-matched clock occurrence,
  // so the mere descriptor must not poison unrelated static AOT closure.
  EXPECT_TRUE(nativeStaticSpecializationEnvironmentClean(context));

  struct ComputedWait {
    talos_rt_computed_wait_record_v1 wait{};
    talos_rt_computed_dependency_v1 dependency{};
  } computed;
  computed.wait.dependency_count = 1;
  computed.wait.dependencies_offset = sizeof(computed.wait);
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;

  context->scheduledProcesses.emplace_back();
  context->scheduledProcesses.back().token = 43;
  context->scheduledProcessIndices[43] = 0;

  // Live event-only and managed-only computed waits have no signal
  // subscription counter. The lazy waiter inventory must still keep both off
  // the static fast path until exact unregister.
  computed.dependency = {37, TALOS_RT_OBSERVER_DEPENDENCY_EVENT, 1};
  ASSERT_TRUE(talos_rt_register_computed_signal_wait_unlocked(
      context, &computed.wait, 43, false, subscriptions, latch));
  EXPECT_FALSE(nativeStaticSpecializationEnvironmentClean(context));
  talos_rt_unregister_signal_wait_unlocked(context, subscriptions, 43, false);
  EXPECT_TRUE(nativeStaticSpecializationEnvironmentClean(context));

  computed.dependency = {41, TALOS_RT_OBSERVER_DEPENDENCY_MANAGED, 64};
  ASSERT_TRUE(talos_rt_register_computed_signal_wait_unlocked(
      context, &computed.wait, 43, false, subscriptions, latch));
  EXPECT_FALSE(nativeStaticSpecializationEnvironmentClean(context));
  talos_rt_unregister_signal_wait_unlocked(context, subscriptions, 43, false);
  EXPECT_TRUE(nativeStaticSpecializationEnvironmentClean(context));

  context->scheduledProcessIndices.clear();
  context->scheduledProcesses.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, EventOnlyComputedWaitUnregistersWhenNativeProcessTerminates) {
  talos_rt_observer_descriptor_v1 observer{eventOnlyComputedObserverID,
                                             nullptr,
                                             0,
                                             1,
                                             0,
                                             TALOS_RT_OBSERVER_NO_BYTECODE,
                                             eventOnlyComputedEvaluator,
                                             0};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.observers = &observer;
  execution.observer_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);

  EventOnlyComputedFixture fixture(0x7e03);
  fixture.descriptor.execution = &execution;
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  eventOnlyComputedResumeCount = 0;
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_TRUE(context->scheduledProcesses.front().signalSubscriptions.empty());
  EXPECT_TRUE(
      context->scheduledProcesses.front().computedObserverWaitRegistered);
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 1u);
  EXPECT_FALSE(nativeStaticSpecializationEnvironmentClean(context));

  talos_rt_v1_scheduler_event(context, eventOnlyComputedDependencyID, 0);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(eventOnlyComputedResumeCount, 1u);
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 0u);
  EXPECT_TRUE(nativeStaticSpecializationEnvironmentClean(context));
  EXPECT_TRUE(context->scheduledProcesses.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTSpecializationFastFlagRearmsAfterSlowSlot) {
  AOTTestState state;
  uint32_t specializationFast = 0;
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 256, nullptr, nullptr, 0},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE |
               TALOS_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.specialization_fast = &specializationFast;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 256;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 256),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  state.runHook = runSpecializationFastRearm;
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(state.observedSpecializationFast, 1u);
  EXPECT_EQ(state.observedSpecializationAfterSlot, 1u);
  EXPECT_EQ(specializationFast, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTNodesValidateInventoryAndExecuteExactActor) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(100);
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 7, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  constexpr talos_rt_native_schedule_node duplicateNodes[] = {{0, 0, 0},
                                                                {0, 0, 0}};
  EXPECT_EQ(talos_rt_v1_scheduler_run_aot_nodes(context, duplicateNodes,
                                                  std::size(duplicateNodes)),
            TALOS_RT_INVALID_ARGUMENT);
  constexpr talos_rt_native_schedule_node missingEntry[] = {{0, 1, 0}};
  EXPECT_EQ(talos_rt_v1_scheduler_run_aot_nodes(context, missingEntry,
                                                  std::size(missingEntry)),
            TALOS_RT_INVALID_CONTINUATION);
  EXPECT_FALSE(context->nativeScheduleSingleStep);
  EXPECT_EQ(context->nativeScheduleForcedSlot, UINT32_MAX);
  EXPECT_FALSE(context->nativeScheduleControlOnly);

  constexpr talos_rt_native_schedule_node nodes[] = {{0, 0, 0}};
  schedulerOrder.clear();
  ASSERT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{100}));
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 1u);
  EXPECT_EQ(context->signalDiagnostics.candidateScans, 0u);
  EXPECT_EQ(state.actors[0], nullptr);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, SharedAOTCandidateIndexHandlesLargeStaticPublication) {
  constexpr uint32_t actors = 128;
  AOTTestState state;
  auto plan = makeAOTPlan(state, actors);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT |
               TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP;
  std::array<talos_rt_process_instance_v1 *, actors> actorStorage{};
  plan.mutable_state = actorStorage.data();
  plan.mutable_state_size = sizeof(actorStorage);
  plan.bind = [](void *storage, talos_rt_context *, uint32_t slot,
                 talos_rt_process_instance_v1 *instance) {
    static_cast<talos_rt_process_instance_v1 **>(storage)[slot] = instance;
    return TALOS_RT_OK;
  };
  plan.run = [](void *, talos_rt_context *) { return TALOS_RT_OK; };
  plan.fallback_snapshot = [](void *, talos_rt_context *context,
                              talos_rt_aot_deopt_snapshot *snapshot) {
    return talos_rt_v1_scheduler_snapshot_aot(context, snapshot);
  };
  talos_rt_context *context = nullptr;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  context->signalDiagnosticsEnabled = true;
  schedulerWaitKind = TALOS_RT_SUSPEND_CHANGE;
  schedulerWaitHandle = 887;
  schedulerWaitWidth = 1;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_NONE;
  schedulerWaitOffset = 0;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  std::vector<std::unique_ptr<SchedulerFixture>> fixtures;
  std::vector<talos_rt_native_schedule_node> nodes;
  for (uint32_t slot = 0; slot != actors; ++slot) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(slot + 1));
    fixtures.back()->descriptor.execution = &execution;
    fixtures.back()->descriptor.native_execute =
        [](talos_rt_process_instance_v1 *instance) {
          instance->native_handle = instance;
          if (instance->continuation == 0) {
            auto *wait =
                static_cast<talos_rt_wait_record_v1 *>(instance->frame);
            *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
            auto *entry =
                reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
            *entry = {887, TALOS_RT_WAIT_EDGE_NONE, 1};
            *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                                 TALOS_RT_SUSPEND_CHANGE,
                                 1,
                                 TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                                 0,
                                 48};
          } else {
            schedulerOrder.push_back(instance->descriptor->handle.id);
            ++schedulerResumeCount;
            *instance->action = {TALOS_RT_FRAGMENT_TERMINATE,
                                 TALOS_RT_SUSPEND_NONE,
                                 0,
                                 0,
                                 0,
                                 0};
          }
          return TALOS_RT_OK;
        };
    ASSERT_EQ(talos_rt_v1_scheduler_add_aot(
                  context, makeSchedulerInstance(*fixtures.back()), 0, slot,
                  actors - slot, nullptr, nullptr, 0, nullptr, 0),
              TALOS_RT_OK);
    nodes.push_back({slot, 0, 0});
    nodes.push_back({slot, 1, 0});
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot_nodes(context, nodes.data(),
                                                  nodes.size()),
            TALOS_RT_OK);
  EXPECT_TRUE(schedulerOrder.empty());
  EXPECT_TRUE(context->nativePollCandidates.empty());
  // More than 64 owners, deliberately reverse the node order with ranks.
  // This publication bypasses descriptor subscriptions and must invalidate
  // the same ready cache used by fragment and bytecode arbitration.
  std::array<uint64_t, actors * 2 / 64> ready;
  ready.fill(UINT64_C(0xaaaaaaaaaaaaaaaa));
  {
    NativeAOTContextScope aotScope(context);
    NativeAOTMutexScope mutexScope(context);
    talos_rt_v1_scheduler_activate_static_nodes(context, ready.data(),
                                                  ready.size());
  }
  ASSERT_EQ(context->schedulerStatus, TALOS_RT_OK);
  EXPECT_EQ(context->nativePollCandidates.size(), actors);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot_nodes(context, nodes.data(),
                                                  nodes.size()),
            TALOS_RT_OK);
  std::vector<uint64_t> expected;
  for (uint32_t slot = actors; slot != 0; --slot)
    expected.push_back(slot);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_EQ(schedulerResumeCount, actors);
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, actors * 2);
  EXPECT_LT(context->signalDiagnostics.candidateScans, actors * 12);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTPartialInventoryRejectsBeforePublishingReadyActors) {
  AOTTestState state;
  auto plan = makeAOTPlan(state);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  SchedulerFixture first(100), second(101);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(first), 0, 0,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(second), 0, 1,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  constexpr talos_rt_native_schedule_node partial[] = {{0, 0, 0}};
  EXPECT_EQ(talos_rt_v1_scheduler_run_aot_nodes(context, partial, 1),
            TALOS_RT_INVALID_CONTINUATION);
  EXPECT_TRUE(context->nativeScheduleNodes.empty());
  EXPECT_EQ(context->nativeScheduleReadyNodes.findFirst(), UINT32_MAX);
  EXPECT_FALSE(context->scheduledProcesses[0].started);
  EXPECT_FALSE(context->scheduledProcesses[1].started);
  constexpr talos_rt_native_schedule_node complete[] = {{0, 0, 0}, {1, 0, 0}};
  schedulerOrder.clear();
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot_nodes(context, complete, 2),
            TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{100, 101}));
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTFusedReadyNodesRetainStaticNodeOrder) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture first(100);
  SchedulerFixture second(200);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(first), 0, 0,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(second), 0, 1,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  constexpr talos_rt_native_schedule_node nodes[] = {
      {0, 0, 7},
      {1, 0, 7},
  };
  schedulerOrder.clear();
  ASSERT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{100, 200}));
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 2u);
  EXPECT_EQ(context->signalDiagnostics.candidateScans, 0u);
  talos_rt_v1_context_destroy(context);
}

// IEEE 1800-2017 10.6.2: a force overrides every driver of its target until a
// release, so a driver that changes behind the override is not a value change.
// Generated code reports the transition it computed from its own plane, which
// does not model the override; the overridden bit must not reach the canonical
// plane, because release resolves the target from its drivers and compares the
// result against that plane to decide whether the value changed.
TEST(Scheduler, NativeTransitionKeepsOverriddenBits) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);

  uint8_t globalValue = 0;
  uint8_t globalUnknown = 0;
  uint8_t forced = 1;
  uint8_t forcedUnknown = 0;
  ASSERT_EQ(talos_rt_v1_native_override(
                context, &globalValue, &globalUnknown, 8,
                talos_rt_v1_native_handle_offset(root, 1), 1,
                TALOS_RT_DESCRIPTOR_STORAGE, 0, &forced, &forcedUnknown),
            TALOS_RT_OK);
  ASSERT_EQ((context->stateValue[0] >> 1) & 1, 1u);

  // A driver behind the override reports bit 1 going to z and bit 2 to one.
  uint8_t oldValue = 1u << 1;
  uint8_t oldUnknown = 0;
  uint8_t newValue = (1u << 1) | (1u << 2);
  uint8_t newUnknown = 1u << 1;
  talos_rt_v1_scheduler_signal_transition(
      context, root, 8, &oldValue, &oldUnknown, &newValue, &newUnknown);

  // The forced bit keeps its overridden value; the unforced bit still lands.
  EXPECT_EQ((context->stateValue[0] >> 1) & 1, 1u);
  EXPECT_EQ((context->stateUnknown[0] >> 1) & 1, 0u);
  EXPECT_EQ((context->stateValue[0] >> 2) & 1, 1u);

  // After release the driver value becomes visible again.
  ASSERT_EQ(talos_rt_v1_native_release_override(
                context, &globalValue, &globalUnknown, 8,
                talos_rt_v1_native_handle_offset(root, 1), 1,
                TALOS_RT_DESCRIPTOR_STORAGE, 0),
            TALOS_RT_OK);
  talos_rt_v1_scheduler_signal_transition(
      context, root, 8, &oldValue, &oldUnknown, &newValue, &newUnknown);
  EXPECT_EQ((context->stateUnknown[0] >> 1) & 1, 1u);

  talos_rt_v1_context_destroy(context);
}

// IEEE 1800-2017 9.4.2: an implicit event is detected on any change in the
// value of its expression, and 10.6.2 makes a force one such change — it
// overrides every driver of the target until a release. The publication that
// installs the override therefore has to reach waiters like any other write.
// It is the one transition the override mask must not filter: the mask exists
// to drop driver transitions arriving behind an established override.
TEST(Scheduler, NativeOverrideWakesWaitersOnTheForcedValue) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);

  SchedulerFixture fixture(5);
  fixture.descriptor.execution = &execution;
  schedulerWaitKind = TALOS_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = root;
  schedulerWaitWidth = 8;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_EQ(schedulerResumeCount, 0u);

  uint8_t globalValue = 0;
  uint8_t globalUnknown = 0;
  uint8_t forced = 1;
  uint8_t forcedUnknown = 0;
  ASSERT_EQ(talos_rt_v1_native_override(
                context, &globalValue, &globalUnknown, 8, root, 8,
                TALOS_RT_DESCRIPTOR_STORAGE, 0, &forced, &forcedUnknown),
            TALOS_RT_OK);
  EXPECT_EQ(context->stateValue[0] & 0xffu, 1u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);

  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, NativeForceReleaseRestoresAssignedValueOnce) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);

  uint8_t globalValue = 0;
  uint8_t globalUnknown = 0;
  constexpr uint8_t zero = 0;
  constexpr uint8_t one = 1;
  ASSERT_EQ(talos_rt_v1_native_override(
                context, &globalValue, &globalUnknown, 1, root, 1,
                TALOS_RT_DESCRIPTOR_STORAGE, 1, &one, &zero),
            TALOS_RT_OK);
  ASSERT_EQ(globalValue, 1u);

  // Rebase toggle coverage to the already-active assign. The force contributes
  // one 1->0 transition; release must publish the restored assign as exactly
  // one 0->1 transition even though assignMask remains active.
  ASSERT_EQ(talos_rt_v1_coverage_finalize(
                context, 0, 1, &one, &zero,
                TALOS_RT_COVERAGE_PERSIST_ALL),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_coverage_toggle_bind(context, 0, 1, root),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_coverage_toggle_seal(context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_override(
                context, &globalValue, &globalUnknown, 1, root, 1,
                TALOS_RT_DESCRIPTOR_STORAGE, 0, &zero, &zero),
            TALOS_RT_OK);
  ASSERT_EQ(globalValue, 0u);

  SchedulerFixture fixture(6);
  fixture.descriptor.execution = &execution;
  schedulerWaitKind = TALOS_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = root;
  schedulerWaitWidth = 1;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_EQ(schedulerResumeCount, 0u);

  ASSERT_EQ(talos_rt_v1_native_release_override(
                context, &globalValue, &globalUnknown, 1, root, 1,
                TALOS_RT_DESCRIPTOR_STORAGE, 0),
            TALOS_RT_OK);
  EXPECT_EQ(globalValue, 1u);
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);

  ASSERT_NE(context->coverage, nullptr);
  ASSERT_EQ(context->coverage->toggleCounters.size(), 4u);
  EXPECT_EQ(context->coverage->toggleCounters[0], 1u);
  EXPECT_EQ(context->coverage->toggleCounters[1], 1u);
  EXPECT_EQ(context->coverage->toggleCounters[2], 0u);
  EXPECT_EQ(context->coverage->toggleCounters[3], 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticFanoutMatchesRangeAndFourStateEdgeExactly) {
  AOTTestState state;
  const talos_rt_static_fanout_entry fanout[] = {
      {1, 0, 1, TALOS_RT_WAIT_EDGE_POSEDGE, 1, 0, 2, 2, 0, 0},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.fanout_entries = fanout;
  plan.fanout_entry_count = std::size(fanout);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->signalDiagnosticsEnabled = true;
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(4);
  fixture.descriptor.execution = &execution;
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  schedulerWaitKind = TALOS_RT_SUSPEND_EDGE;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_POSEDGE;
  schedulerWaitHandle = talos_rt_v1_native_handle_offset(root, 2);
  schedulerWaitWidth = 2;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  constexpr talos_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
  };
  ASSERT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);

  uint8_t oldValue = 0;
  uint8_t newValue = 1;
  talos_rt_v1_scheduler_signal_transition(context, root, 8, &oldValue,
                                            nullptr, &newValue, nullptr);
  ASSERT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);

  oldValue = 1u << 2;
  newValue = 0;
  talos_rt_v1_scheduler_signal_transition(context, root, 8, &oldValue,
                                            nullptr, &newValue, nullptr);
  ASSERT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);

  oldValue = 0;
  newValue = 0;
  uint8_t oldUnknown = 0;
  uint8_t newUnknown = 1u << 2;
  talos_rt_v1_scheduler_signal_transition(
      context, root, 8, &oldValue, &oldUnknown, &newValue, &newUnknown);
  ASSERT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->signalDiagnostics.aotFanoutEntries, 3u);
  // Exact fanout does not publish the out-of-range change or the unmatched
  // negedge. Only the four-state transition that satisfies the posedge wait
  // receives a scheduler sequence.
  EXPECT_EQ(context->signalDiagnostics.publications, 1u);

  uint64_t unobserved = talos_rt_v1_native_handle_offset(root, 6);
  oldValue = 0;
  newValue = 1;
  talos_rt_v1_scheduler_signal_transition(context, unobserved, 1, &oldValue,
                                            nullptr, &newValue, nullptr);
  EXPECT_EQ(context->signalDiagnostics.publications, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, IndexedExternalDepositResumesFourStateAOTWithoutBytecode) {
  AOTTestState state;
  const talos_rt_static_fanout_entry fanout[] = {
      {1, 0, 1, TALOS_RT_WAIT_EDGE_CHANGE, 1, 0, 0, 1, 0, 0},
  };
  uint8_t valuePlane = 0;
  uint8_t unknownPlane = 0;
  uint64_t ingress = 0;
  const talos_rt_native_clock_kernel clocks[] = {
      {1, TALOS_RT_WAIT_EDGE_CHANGE, 0, 1, &ingress, 1, 0, nullptr},
  };
  const talos_rt_native_merged_fragment merged[] = {
      {0, 1, 0, 0, 1, 0, nullptr},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT |
               TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP;
  plan.state_value = &valuePlane;
  plan.state_unknown = &unknownPlane;
  plan.state_bit_count = 1;
  plan.fanout_entries = fanout;
  plan.fanout_entry_count = std::size(fanout);
  plan.run = aotRunWaitNodes;
  plan.clock_kernels = clocks;
  plan.clock_kernel_count = std::size(clocks);
  plan.merged_fragments = merged;
  plan.merged_fragment_count = std::size(merged);
  plan.timeslot_coordinator = clockWaitCoordinator;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  constexpr uint8_t zero = 0;
  ASSERT_EQ(talos_rt_v1_coverage_finalize(
                context, 0, 1, &zero, &zero,
                TALOS_RT_COVERAGE_PERSIST_ALL),
            TALOS_RT_OK);
  uint64_t coveredState = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(coveredState, UINT64_MAX);
  ASSERT_EQ(talos_rt_v1_coverage_toggle_bind(context, 0, 1, coveredState),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_coverage_toggle_seal(context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(5);
  fixture.descriptor.execution = &execution;
  // Bytecode is available, but only the native resume records execution.
  // Availability must not be confused with REQUIRE_BYTECODE policy.
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_TERMINATE, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<talos_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 0}}};
  talos_rt_bytecode_v1 bytecode{code.data(),
                                  code.size(),
                                  entries.data(),
                                  2,
                                  0,
                                  fixture.layout.frame_size,
                                  nullptr,
                                  nullptr,
                                  0,
                                  nullptr,
                                  0,
                                  0,
                                  nullptr,
                                  0};
  fixture.descriptor.available_tiers |= TALOS_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.bytecode = &bytecode;
  uint64_t root = talos_rt_canonical_state_handle_unlocked(context, 0, 1);
  ASSERT_NE(root, UINT64_MAX);
  schedulerWaitKind = TALOS_RT_SUSPEND_CHANGE;
  schedulerWaitHandle = root;
  schedulerWaitWidth = 1;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);

  context->stateValue[0] = 1;
  ASSERT_TRUE(talos_rt_aot_external_deposit_unlocked(context, root, 0, 1));
  uint8_t changed = 1;
  uint8_t posedge = 1;
  uint8_t negedge = 0;
  uint8_t newValue = 1;
  uint8_t newUnknown = 0;
  ASSERT_TRUE(talos_rt_publish_native_signal_transition_unlocked(
      context, root, 1, &changed, &posedge, &negedge, &zero, &zero, &newValue,
      &newUnknown, true));
  EXPECT_EQ(ingress, 1u);
  EXPECT_FALSE(context->nativeScheduleExternalWritePending);
  EXPECT_FALSE(context->nativeScheduleDirtyRootsPresent);
  clockCoordinatorIngress = &ingress;
  clockCoordinatorWords = 1;
  clockCoordinatorCalls = 0;
  ASSERT_EQ(talos_rt_v1_scheduler_run_clock_coordinator(context),
            TALOS_RT_OK);
  EXPECT_EQ(clockCoordinatorCalls, 1u);
  EXPECT_EQ(ingress, 0u);
  clockCoordinatorIngress = nullptr;
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  uint64_t covered = 0, total = 0;
  double percentage = 0;
  ASSERT_EQ(talos_rt_v1_coverage_query(context, TALOS_RT_COVERAGE_TOGGLE,
                                         &covered, &total, &percentage),
            TALOS_RT_OK);
  EXPECT_EQ(covered, 1u);
  EXPECT_EQ(total, 2u);
  talos_rt_v1_context_destroy(context);
}

TEST(Coverage, GenericScalarNetPublicationRecordsToggleTransition) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  constexpr uint8_t zero = 0;
  ASSERT_EQ(talos_rt_v1_coverage_finalize(
                context, 0, 1, &zero, &zero,
                TALOS_RT_COVERAGE_PERSIST_ALL),
            TALOS_RT_OK);
  uint64_t state = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(state, UINT64_MAX);
  ASSERT_EQ(talos_rt_v1_coverage_toggle_bind(context, 0, 1, state),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_coverage_toggle_seal(context), TALOS_RT_OK);

  ASSERT_TRUE(talos_rt_append_signal_event_unlocked(context, state, false,
                                                      false, true, false));
  uint64_t covered = 0, total = 0;
  double percentage = 0;
  ASSERT_EQ(talos_rt_v1_coverage_query(context, TALOS_RT_COVERAGE_TOGGLE,
                                         &covered, &total, &percentage),
            TALOS_RT_OK);
  EXPECT_EQ(covered, 1u);
  EXPECT_EQ(total, 2u);
  talos_rt_v1_context_destroy(context);
}

TEST(Coverage, TransitionFastPathsObserveLateToggleBinding) {
  for (bool staticAOT : {false, true}) {
    talos_rt_execution_descriptor_v1 execution{};
    execution.version = TALOS_RT_VERSION;
    execution.state_bit_count = 1;
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
              TALOS_RT_OK);
    context->stateValue[0] = context->stateUnknown[0] = 0;
    AOTTestState state;
    auto plan = makeAOTPlan(state, 1);
    plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
                 TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
    ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
              TALOS_RT_OK);
    uint64_t handle = talos_rt_v1_native_state_static_handle(1);
    auto publish = [&](uint8_t oldValue, uint8_t newValue) {
      if (staticAOT) {
        NativeAOTContextScope active(context);
        NativeAOTMutexScope locked(context);
        talos_rt_v1_scheduler_static_transition(context, 1, 0, 1, oldValue, 0,
                                                  newValue, 0);
      } else {
        talos_rt_v1_scheduler_signal_transition(context, handle, 1, &oldValue,
                                                  nullptr, &newValue, nullptr);
      }
    };
    publish(0, 1);
    publish(1, 0);
    EXPECT_EQ(context->coverage, nullptr);
    constexpr uint8_t zero = 0;
    ASSERT_EQ(talos_rt_v1_coverage_finalize(context, 0, 1, &zero, &zero,
                                              TALOS_RT_COVERAGE_PERSIST_ALL),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_coverage_toggle_bind(context, 0, 1, handle),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_coverage_toggle_seal(context), TALOS_RT_OK);
    publish(0, 1);
    publish(1, 0);
    uint64_t covered = 0, total = 0;
    double percentage = 0;
    ASSERT_EQ(talos_rt_v1_coverage_query(context, TALOS_RT_COVERAGE_TOGGLE,
                                           &covered, &total, &percentage),
              TALOS_RT_OK);
    EXPECT_EQ(covered, 2u);
    EXPECT_EQ(total, 2u);
    talos_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, AOTTiedDeadlinesUseFixedHeapAndDeterministicNodeOrder) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture first(1);
  SchedulerFixture second(2);
  schedulerWaitKind = TALOS_RT_SUSPEND_DELAY;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(first), 0, 0,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(second), 0, 1,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  constexpr talos_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {1, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
      {1, 1, UINT32_MAX},
  };
  ASSERT_EQ(
      talos_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      TALOS_RT_OK);
  EXPECT_EQ(context->schedulerTime, 17u);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{1, 2}));
  EXPECT_EQ(context->signalDiagnostics.aotDeadlineHighWater, 2u);
  EXPECT_TRUE(context->nativeScheduleDeadlineHeap.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTDelayIndexStaysBoundedWithoutGenericCalendarQueries) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_process_instance_v1 instances[3]{};
  context->scheduledProcesses.resize(3);
  for (unsigned index = 0; index != 3; ++index) {
    auto &process = context->scheduledProcesses[index];
    process.instance = &instances[index];
    process.token = index + 1;
    process.started = true;
    process.suspendKind = TALOS_RT_SUSPEND_DELAY;
    process.wakeTime = 1000000;
    context->scheduledProcessIndices[process.token] = index;
    indexScheduledProcessDelayUnlocked(context, process);
  }
  auto &frequent = context->scheduledProcesses[0];
  // Include a terminated actor and a live far-future actor while repeatedly
  // replacing one deadline, without ever consulting the generic calendar.
  context->scheduledProcesses[2].instance = nullptr;
  for (unsigned time = 1; time != 10000; ++time) {
    frequent.wakeTime = time;
    indexScheduledProcessDelayUnlocked(context, frequent);
    ASSERT_LE(context->scheduledProcessDelayHeap.size(), 65u);
  }
  const auto &heap = context->scheduledProcessDelayHeap;
  EXPECT_NE(std::find(heap.begin(), heap.end(),
                      std::make_pair(uint64_t{9999}, frequent.token)),
            heap.end());
  EXPECT_NE(std::find(heap.begin(), heap.end(),
                      std::make_pair(uint64_t{1000000}, uint64_t{2})),
            heap.end());
  EXPECT_TRUE(std::none_of(heap.begin(), heap.end(),
                           [](auto entry) { return entry.second == 3; }));
  context->scheduledProcesses.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, FutureDelaysLeavePollCandidatesUntilTheirTimeSlot) {
  // IEEE 1800-2023 4.4-4.5: a delayed process becomes runnable only in the
  // time slot of its wake time. The delay calendar admits it there, so no
  // scheduler pass of an earlier slot visits it.
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_process_instance_v1 instances[4]{};
  constexpr uint64_t wakeTimes[] = {5, 10, 1000, 0};
  context->scheduledProcesses.resize(4);
  for (unsigned index = 0; index != 4; ++index) {
    auto &process = context->scheduledProcesses[index];
    process.instance = &instances[index];
    process.token = index + 1;
    process.started = true;
    process.suspendKind = TALOS_RT_SUSPEND_DELAY;
    process.wakeTime = wakeTimes[index];
  }
  // Final-phase delays are not in the calendar and stay polled.
  context->scheduledProcesses[3].phase = 1;
  context->scheduledProcesses[3].wakeTime = 7;
  rebuildNativeSchedulerIndexUnlocked(context);
  ASSERT_EQ(enterSchedulerTimeSlotUnlocked(context), TALOS_RT_OK);
  EXPECT_EQ(context->nativePollCandidates,
            (std::unordered_set<uint64_t>{4}));

  // Re-suspending replaces the calendar entry; the stale one admits nothing.
  auto &moved = context->scheduledProcesses[1];
  moved.wakeTime = 20;
  indexScheduledProcessDelayUnlocked(context, moved);
  context->schedulerTime = 5;
  ASSERT_EQ(enterSchedulerTimeSlotUnlocked(context), TALOS_RT_OK);
  EXPECT_EQ(context->nativePollCandidates,
            (std::unordered_set<uint64_t>{1, 4}));
  context->schedulerTime = 10;
  ASSERT_EQ(enterSchedulerTimeSlotUnlocked(context), TALOS_RT_OK);
  EXPECT_EQ(context->nativePollCandidates,
            (std::unordered_set<uint64_t>{1, 4}));
  // A slot past the wake time admits it as well.
  context->schedulerTime = 25;
  ASSERT_EQ(enterSchedulerTimeSlotUnlocked(context), TALOS_RT_OK);
  EXPECT_EQ(context->nativePollCandidates,
            (std::unordered_set<uint64_t>{1, 2, 4}));
  EXPECT_EQ(nextScheduledProcessDelayUnlocked(context),
            std::optional<uint64_t>{1000});
  context->scheduledProcesses.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTZeroDelayUsesInactiveBeforeNBAWithoutWholePlanFallback) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 2);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL;
  plan.run = [](void *, talos_rt_context *context) {
    constexpr talos_rt_native_schedule_node nodes[] = {
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    return talos_rt_v1_scheduler_run_aot_nodes(context, nodes,
                                                 std::size(nodes));
  };
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(3);
  SchedulerFixture independent(200);
  uint8_t plane = 0, next = 1;
  cachedCohortNBAPlane = &plane;
  cachedCohortNBAVisible = false;
  fixture.descriptor.native_execute =
      [](talos_rt_process_instance_v1 *actor) {
        if (actor->continuation == 1)
          cachedCohortNBAVisible =
              *cachedCohortNBAPlane == 0 &&
              actor->context->activeExecRegion == TALOS_RT_REGION_INACTIVE;
        return schedulerExecute(actor);
      };
  schedulerOrder.clear();
  schedulerWaitKind = TALOS_RT_SUSPEND_DELAY;
  schedulerWaitDelay = 0;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(
                context, makeSchedulerInstance(independent), 0, 1, 0, nullptr,
                nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_nba(context, &plane, nullptr, 1, 0, 1, 0,
                                        &next, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_TRUE(cachedCohortNBAVisible);
  EXPECT_EQ(plane, 1u);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{200, 3}));
  cachedCohortNBAPlane = nullptr;
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  EXPECT_EQ(context->schedulerTime, 0u);
  EXPECT_EQ(schedulerResumeCount, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTFallbackSnapshotIsValidatedBeforeGenericResume) {
  AOTTestState state;
  state.requestFallback = true;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  SchedulerFixture fixture(200);
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  schedulerOrder.clear();
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_TRUE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 1u);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{200}));
  EXPECT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 1u);
  talos_rt_v1_context_destroy(context);

  AOTTestState invalid;
  invalid.requestFallback = true;
  invalid.corruptSnapshot = true;
  plan = makeAOTPlan(invalid);
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_run_aot(context),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTEventWaitResumesInSharedDriverWithoutWholePlanFallback) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.run = aotRunNodes;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(42);
  schedulerWaitKind = TALOS_RT_SUSPEND_EVENT;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_NONE;
  schedulerWaitHandle = 91;
  schedulerWaitWidth = 0;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  ASSERT_FALSE(context->nativeScheduleDeoptimized);
  ASSERT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_EQ(context->scheduledProcesses.front().instance->continuation, 1u);
  EXPECT_EQ(context->scheduledProcesses.front().suspendKind,
            TALOS_RT_SUSPEND_EVENT);
  EXPECT_EQ(schedulerResumeCount, 0u);

  talos_rt_v1_scheduler_event(context, 91, 0);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTFinishDestroysTheCompleteTaskCallerStack) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.run = aotRunNodes;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(43);
  fixture.descriptor.native_execute = finishExecute;
  fixture.descriptor.native_destroy = finishDestroy;
  talos_rt_process_instance_v1 *callee = makeSchedulerInstance(fixture);
  talos_rt_process_instance_v1 *caller = makeSchedulerInstance(fixture);
  ASSERT_NE(callee, nullptr);
  ASSERT_NE(caller, nullptr);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context, callee, 0, 0, 0, nullptr,
                                            nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  // A real suspended caller retains its native coroutine handle while the
  // callee occupies the actor slot.
  caller->native_handle = caller;
  context->scheduledProcesses.front().callers.push_back(caller);
  finishDestroyCount = 0;

  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(finishDestroyCount, 2u);
  EXPECT_EQ(state.actors[0], nullptr);
  EXPECT_TRUE(context->scheduledProcesses.empty() ||
              (!context->scheduledProcesses.front().instance &&
               context->scheduledProcesses.front().callers.empty()));
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ThreadOriginKeepsOutermostNativeActivation) {
  for (bool aot : {false, true}) {
    SCOPED_TRACE(aot);
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    AOTTestState state;
    auto plan = makeAOTPlan(state, 1);
    plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
    plan.run = aotRunNodes;
    if (aot)
      ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
                TALOS_RT_OK);
    SchedulerFixture outer(44), middle(45), inner(46);
    outer.descriptor.native_execute = [](talos_rt_process_instance_v1 *p) {
      uint64_t origin = UINT64_MAX;
      EXPECT_EQ(
          talos_rt_logical_process_origin(
              p->context, talos_rt_v1_process_current(p->context), &origin),
          TALOS_RT_OK);
      EXPECT_EQ(origin, 44u);
      return taskCallerExecute(p);
    };
    middle.descriptor.native_execute = [](talos_rt_process_instance_v1 *p) {
      uint64_t origin = UINT64_MAX;
      EXPECT_EQ(
          talos_rt_logical_process_origin(
              p->context, talos_rt_v1_process_current(p->context), &origin),
          TALOS_RT_OK);
      EXPECT_EQ(origin, 44u);
      if (p->continuation) {
        *p->action = {
            TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
        return TALOS_RT_OK;
      }
      talos_rt_process_instance_v1 *callee = nullptr;
      auto status = talos_rt_v1_process_instance_create(
          nestedTaskCalleeDescriptor, &callee);
      if (status != TALOS_RT_OK)
        return status;
      *p->action = {TALOS_RT_FRAGMENT_TASK_CALL,
                    TALOS_RT_SUSPEND_NONE,
                    1,
                    0,
                    reinterpret_cast<uintptr_t>(callee),
                    0};
      return TALOS_RT_OK;
    };
    inner.descriptor.native_execute = [](talos_rt_process_instance_v1 *p) {
      uint64_t origin = UINT64_MAX;
      EXPECT_EQ(
          talos_rt_logical_process_origin(
              p->context, talos_rt_v1_process_current(p->context), &origin),
          TALOS_RT_OK);
      EXPECT_EQ(origin, 44u);
      return taskCalleeExecute(p);
    };
    taskCalleeDescriptor = &middle.descriptor;
    nestedTaskCalleeDescriptor = &inner.descriptor;
    taskCallerExecutions = taskCalleeExecutions = 0;
    auto *instance = makeSchedulerInstance(outer);
    ASSERT_NE(instance, nullptr);
    ASSERT_EQ(aot ? talos_rt_v1_scheduler_add_aot(context, instance, 0, 0, 0,
                                                    nullptr, nullptr, 0,
                                                    nullptr, 0)
                  : talos_rt_v1_scheduler_add(context, instance, 0),
              TALOS_RT_OK);
    const auto token = talos_rt_v1_scheduler_process_token(context, instance);
    uint64_t origin = UINT64_MAX;
    ASSERT_EQ(talos_rt_logical_process_origin(context, token, &origin),
              TALOS_RT_OK);
    EXPECT_EQ(origin, 44u);
    ASSERT_EQ(aot ? talos_rt_v1_scheduler_run_aot(context)
                  : talos_rt_v1_scheduler_run(context),
              TALOS_RT_OK);
    EXPECT_EQ(taskCallerExecutions, 2u);
    EXPECT_EQ(taskCalleeExecutions, 1u);
    EXPECT_EQ(talos_rt_logical_process_origin(context, token, &origin),
              TALOS_RT_INVALID_HANDLE);
    taskCalleeDescriptor = nestedTaskCalleeDescriptor = nullptr;
    talos_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, AOTTaskCallLocallySuspendsAndResumesTheCaller) {
  for (bool descriptorDriver : {false, true}) {
    SCOPED_TRACE(descriptorDriver);
    AOTTestState state;
    talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
    plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
    plan.run = aotRunNodes;
    if (descriptorDriver)
      plan.run = [](void *, talos_rt_context *context) {
        return talos_rt_v1_scheduler_run(context);
      };
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
              TALOS_RT_OK);

    SchedulerFixture caller(44);
    SchedulerFixture callee(45);
    caller.descriptor.native_execute = taskCallerExecute;
    callee.descriptor.native_execute = [](talos_rt_process_instance_v1 *p) {
      EXPECT_EQ(p->context->nativeScheduleActors[0], nullptr);
      EXPECT_EQ(p->context->scheduledProcesses.front().aotActorSlot,
                UINT32_MAX);
      return taskCalleeExecute(p);
    };
    taskCalleeDescriptor = &callee.descriptor;
    taskCallerExecutions = 0;
    taskCalleeExecutions = 0;
    ASSERT_EQ(talos_rt_v1_scheduler_add_aot(
                  context, makeSchedulerInstance(caller), 0, 0, 0, nullptr,
                  nullptr, 0, nullptr, 0),
              TALOS_RT_OK);

    ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
    EXPECT_FALSE(context->nativeScheduleDeoptimized);
    EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
    EXPECT_EQ(taskCallerExecutions, 2u);
    EXPECT_EQ(taskCalleeExecutions, 1u);
    EXPECT_EQ(state.actors[0], nullptr);
    taskCalleeDescriptor = nullptr;
    talos_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, AOTTaskWaitKeepsOtherActorsAndCallerBinding) {
  for (uint32_t waitKind :
       {TALOS_RT_SUSPEND_DELAY, TALOS_RT_SUSPEND_EDGE}) {
    SCOPED_TRACE(waitKind);
    AOTTestState state;
    talos_rt_native_schedule_plan plan = makeAOTPlan(state, 2);
    plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
    plan.bind = [](void *opaque, talos_rt_context *context, uint32_t slot,
                   talos_rt_process_instance_v1 *instance) {
      // A generated binding knows only its own frame layout. In particular,
      // the task's coincident continuation IDs must never borrow slot zero.
      if (instance && instance->descriptor->handle.id != (slot ? 143u : 44u))
        return TALOS_RT_INVALID_FRAME;
      return aotBind(opaque, context, slot, instance);
    };
    plan.run = [](void *, talos_rt_context *context) {
      constexpr talos_rt_native_schedule_node nodes[] = {
          {0, 0, 0}, {0, 1, 0}, {1, 0, 0}};
      return talos_rt_v1_scheduler_run_aot_nodes(context, nodes,
                                                   std::size(nodes));
    };
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
              TALOS_RT_OK);
    context->signalDiagnosticsEnabled = true;
    SchedulerFixture caller(44), callee(45), independent(143);
    caller.descriptor.native_execute = taskCallerExecute;
    taskCalleeDescriptor = &callee.descriptor;
    taskCallerExecutions = 0;
    schedulerResumeCount = 0;
    schedulerOrder.clear();
    schedulerWaitKind = waitKind;
    schedulerWaitDelay = 5;
    schedulerWaitEdge = TALOS_RT_WAIT_EDGE_POSEDGE;
    schedulerWaitHandle = 17;
    schedulerWaitWidth = 1;
    ASSERT_EQ(talos_rt_v1_scheduler_add_aot(
                  context, makeSchedulerInstance(caller), 0, 0, 0, nullptr,
                  nullptr, 0, nullptr, 0),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_add_aot(
                  context, makeSchedulerInstance(independent), 0, 1, 1, nullptr,
                  nullptr, 0, nullptr, 0),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
    if (waitKind == TALOS_RT_SUSPEND_EDGE) {
      EXPECT_EQ(taskCallerExecutions, 1u);
      EXPECT_EQ(state.actors[0], nullptr);
      EXPECT_EQ(context->nativeDynamicSignalSubscriptions, 1u);
      auto *duplicate = makeSchedulerInstance(caller);
      ASSERT_NE(duplicate, nullptr);
      EXPECT_EQ(talos_rt_v1_scheduler_add_aot(context, duplicate, 0, 0, 0,
                                                nullptr, nullptr, 0, nullptr,
                                                0),
                TALOS_RT_INVALID_ARGUMENT);
      EXPECT_EQ(talos_rt_v1_process_instance_destroy(duplicate),
                TALOS_RT_OK);
      talos_rt_v1_scheduler_signal(context, 17, 1, TALOS_RT_SIGNAL_POSEDGE);
      ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
    }
    EXPECT_EQ(taskCallerExecutions, 2u);
    EXPECT_EQ(schedulerResumeCount, 1u);
    EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{143, 45}));
    EXPECT_EQ(context->signalDiagnostics.aotActorExecutions[0], 2u);
    EXPECT_EQ(context->signalDiagnostics.aotActorExecutions[1], 1u);
    EXPECT_EQ(context->nativeDynamicSignalSubscriptions, 0u);
    EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
    EXPECT_FALSE(context->nativeScheduleDeoptimized);
    EXPECT_EQ(state.actors[0], nullptr);
    EXPECT_EQ(state.actors[1], nullptr);
    taskCalleeDescriptor = nullptr;
    talos_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, AOTNestedTaskReturnsOrFinishesWithoutRebindingInnerFrames) {
  for (bool finish : {false, true}) {
    SCOPED_TRACE(finish);
    AOTTestState state;
    talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
    plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
    plan.run = aotRunNodes;
    plan.bind = [](void *opaque, talos_rt_context *context, uint32_t slot,
                   talos_rt_process_instance_v1 *instance) {
      if (instance && instance->descriptor->handle.id != 44)
        return TALOS_RT_INVALID_FRAME;
      return aotBind(opaque, context, slot, instance);
    };
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
              TALOS_RT_OK);
    SchedulerFixture caller(44), outer(45), inner(46);
    caller.descriptor.native_execute = taskCallerExecute;
    outer.descriptor.native_execute = [](talos_rt_process_instance_v1 *p) {
      EXPECT_EQ(p->context->nativeScheduleActors[0], nullptr);
      EXPECT_EQ(p->context->scheduledProcesses.front().aotActorSlot,
                UINT32_MAX);
      p->native_handle = p;
      schedulerOrder.push_back(p->continuation ? 451 : 450);
      if (p->continuation) {
        *p->action = {
            TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
        return TALOS_RT_OK;
      }
      talos_rt_process_instance_v1 *callee = nullptr;
      auto status = talos_rt_v1_process_instance_create(
          nestedTaskCalleeDescriptor, &callee);
      if (status != TALOS_RT_OK)
        return status;
      *p->action = {TALOS_RT_FRAGMENT_TASK_CALL,
                    TALOS_RT_SUSPEND_NONE,
                    1,
                    0,
                    reinterpret_cast<uintptr_t>(callee),
                    0};
      return TALOS_RT_OK;
    };
    if (finish)
      inner.descriptor.native_execute = finishExecute;
    caller.descriptor.native_destroy = finishDestroy;
    outer.descriptor.native_destroy = finishDestroy;
    inner.descriptor.native_destroy = finishDestroy;
    taskCalleeDescriptor = &outer.descriptor;
    nestedTaskCalleeDescriptor = &inner.descriptor;
    taskCallerExecutions = 0;
    finishDestroyCount = 0;
    schedulerResumeCount = 0;
    schedulerOrder.clear();
    schedulerWaitKind = TALOS_RT_SUSPEND_DELAY;
    schedulerWaitDelay = 5;
    ASSERT_EQ(talos_rt_v1_scheduler_add_aot(
                  context, makeSchedulerInstance(caller), 0, 0, 0, nullptr,
                  nullptr, 0, nullptr, 0),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
    EXPECT_EQ(taskCallerExecutions, finish ? 1u : 2u);
    EXPECT_EQ(finishDestroyCount, 3u);
    EXPECT_EQ(schedulerOrder, (finish ? std::vector<uint64_t>{450}
                                      : std::vector<uint64_t>{450, 46, 451}));
    EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
    EXPECT_FALSE(context->nativeScheduleDeoptimized);
    EXPECT_EQ(state.actors[0], nullptr);
    taskCalleeDescriptor = nullptr;
    nestedTaskCalleeDescriptor = nullptr;
    talos_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, AOTExternalWriteUsesNativeFineSchedulerUntilCleanBoundary) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  SchedulerFixture fixture(33);
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, TALOS_RT_BC_SUSPEND, TALOS_RT_BC_TYPE_NONE, 0,
                    TALOS_RT_SUSPEND_EDGE, 0, 1);
  appendInstruction(code, TALOS_RT_BC_TERMINATE, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<talos_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  talos_rt_bytecode_v1 bytecode{code.data(),
                                  code.size(),
                                  entries.data(),
                                  static_cast<uint32_t>(entries.size()),
                                  1,
                                  fixture.layout.frame_size,
                                  nullptr,
                                  nullptr,
                                  0,
                                  nullptr,
                                  0,
                                  0,
                                  nullptr,
                                  0};
  fixture.descriptor.available_tiers =
      TALOS_RT_TIER_MASK_NATIVE | TALOS_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.bytecode = &bytecode;
  schedulerWaitKind = TALOS_RT_SUSPEND_EDGE;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_NEGEDGE;
  schedulerWaitHandle = 17;
  schedulerWaitWidth = 8;
  talos_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE, 0, 1, 0, 0};
  *entry = {17, TALOS_RT_WAIT_EDGE_NEGEDGE, 8};
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context, instance, 0, 0, 0, nullptr,
                                            nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  EXPECT_EQ(instance->tier, 0u);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_external_write_unlocked(context);
  }
  EXPECT_TRUE(context->nativeScheduleExternalWritePending);
  EXPECT_EQ(instance->tier, 0u);
  schedulerResumeCount = 0;
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_FALSE(context->nativeScheduleExternalWritePending);
  EXPECT_EQ(instance->tier, TALOS_RT_TIER_NATIVE);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  EXPECT_EQ(schedulerResumeCount, 0u);
  talos_rt_v1_scheduler_signal(
      context, 17, 8, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTExternalWriteReturnsToFourStateAtCleanBoundary) {
  AOTTestState state;
  state.runHook = runCountOK;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  plan.promotion_ready = schedulerPromotionReady;
  schedulerPromotionInvalidationCount = 0;
  schedulerPromotionReadyCount = 0;
  schedulerPromotionReadyValue = false;

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_external_write_unlocked(context);
  }

  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(state.runCalls, 1u);
  // The callback scans exactly once after canonical export at the quiescent
  // Tier-2 handback. A false result selects four-state Tier 1 without delaying
  // that handback.
  EXPECT_EQ(schedulerPromotionReadyCount, 1u);

  // An ordinary Tier-1 invocation does not rescan an unchanged unknown
  // plane. Only a new transient write invalidates the promotion boundary.
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(state.runCalls, 2u);
  EXPECT_EQ(schedulerPromotionReadyCount, 1u);

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_external_write_unlocked(context);
  }
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(state.runCalls, 3u);
  EXPECT_EQ(schedulerPromotionReadyCount, 2u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTExternalWriteExecutesNativeActorAtCleanBoundary) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(34);
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, TALOS_RT_BC_SUSPEND, TALOS_RT_BC_TYPE_NONE, 0,
                    TALOS_RT_SUSPEND_EDGE, 0, 1);
  appendInstruction(code, TALOS_RT_BC_TERMINATE, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<talos_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  talos_rt_bytecode_v1 bytecode{code.data(),
                                  code.size(),
                                  entries.data(),
                                  static_cast<uint32_t>(entries.size()),
                                  1,
                                  fixture.layout.frame_size,
                                  nullptr,
                                  nullptr,
                                  0,
                                  nullptr,
                                  0,
                                  0,
                                  nullptr,
                                  0};
  fixture.descriptor.available_tiers =
      TALOS_RT_TIER_MASK_NATIVE | TALOS_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.bytecode = &bytecode;
  schedulerWaitKind = TALOS_RT_SUSPEND_EDGE;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = 17;
  schedulerWaitWidth = 8;
  schedulerDestroyCount = 0;
  talos_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, context, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  ASSERT_NE(instance->native_handle, nullptr);
  ASSERT_EQ(instance->tier, TALOS_RT_TIER_NATIVE);
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context, instance, 0, 0, 0, nullptr,
                                            nullptr, 0, nullptr, 0),
            TALOS_RT_OK);

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_aot_external_write_unlocked(context);
  }
  EXPECT_EQ(instance->tier, TALOS_RT_TIER_NATIVE);
  EXPECT_NE(instance->native_handle, nullptr);
  EXPECT_EQ(schedulerDestroyCount, 0u);

  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(instance->tier, TALOS_RT_TIER_NATIVE);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(schedulerDestroyCount, 1u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTBytecodeFragmentReturnsToNativeAtContinuationBoundary) {
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  SchedulerFixture fixture(32);
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, TALOS_RT_BC_SUSPEND, TALOS_RT_BC_TYPE_NONE, 0,
                    TALOS_RT_SUSPEND_EDGE, 0, 1);
  appendInstruction(code, TALOS_RT_BC_TERMINATE, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<talos_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  talos_rt_bytecode_v1 bytecode{code.data(),
                                  code.size(),
                                  entries.data(),
                                  static_cast<uint32_t>(entries.size()),
                                  1,
                                  fixture.layout.frame_size,
                                  nullptr,
                                  nullptr,
                                  0,
                                  nullptr,
                                  0,
                                  0,
                                  nullptr,
                                  0};
  fixture.descriptor.available_tiers =
      TALOS_RT_TIER_MASK_NATIVE | TALOS_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.bytecode = &bytecode;
  talos_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE, 0, 1, 0, 0};
  *entry = {16, TALOS_RT_WAIT_EDGE_NEGEDGE, 8};
  const uint32_t bytecodeContinuation = 0;
  schedulerResumeCount = 0;
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context, instance, 0, 0, 0, nullptr,
                                            nullptr, 0, &bytecodeContinuation,
                                            1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);
  talos_rt_v1_scheduler_signal(
      context, 16, 8, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  // Continuation zero suspended through bytecode. Continuation one then
  // resumed through the native wrapper without deoptimizing the AOT plan.
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTNativeCheckpointRunsBytecodeIslandAndReturnsToNative) {
  AOTTestState state;
  state.runHook = runAOTNodes;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  SchedulerFixture fixture(35);
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, TALOS_RT_BC_SUSPEND, TALOS_RT_BC_TYPE_NONE, 0,
                    TALOS_RT_SUSPEND_EDGE, 0, 1);
  appendInstruction(code, TALOS_RT_BC_TERMINATE, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<talos_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  talos_rt_bytecode_v1 bytecode{code.data(),
                                  code.size(),
                                  entries.data(),
                                  static_cast<uint32_t>(entries.size()),
                                  1,
                                  fixture.layout.frame_size,
                                  nullptr,
                                  nullptr,
                                  0,
                                  nullptr,
                                  0,
                                  0,
                                  nullptr,
                                  0};
  fixture.descriptor.available_tiers =
      TALOS_RT_TIER_MASK_NATIVE | TALOS_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.native_execute = schedulerCheckpointThenNativeExecute;
  fixture.descriptor.bytecode = &bytecode;
  schedulerCheckpointCount = 0;
  schedulerPromotionInvalidationCount = 0;
  schedulerResumeCount = 0;
  talos_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE, 0, 1, 0, 0};
  *entry = {16, TALOS_RT_WAIT_EDGE_NEGEDGE, 8};
  ASSERT_EQ(talos_rt_v1_scheduler_add_aot(context, instance, 0, 0, 0, nullptr,
                                            nullptr, 0, nullptr, 0),
            TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerCheckpointCount, 1u);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 1u);
  EXPECT_EQ(instance->tier, TALOS_RT_TIER_BYTECODE);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  ASSERT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(), 1u);
  EXPECT_EQ(
      context->scheduledProcesses.front().signalSubscriptions.front()->stableID,
      16u);
  // Exercise the real signal subscription and AOT ready-bit routing on the
  // Tier-3 return continuation instead of mutating scheduler internals.
  talos_rt_v1_scheduler_signal(
      context, 16, 8, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 1u);
  EXPECT_EQ(instance->tier, TALOS_RT_TIER_NATIVE);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AwaitUsesStableNonAddressProcessTokens) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture completed(100);
  talos_rt_process_instance_v1 *oldInstance =
      makeSchedulerInstance(completed);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, oldInstance, 0),
            TALOS_RT_OK);
  uint64_t oldToken =
      talos_rt_v1_scheduler_process_token(context, oldInstance);
  ASSERT_NE(oldToken, 0u);
  EXPECT_NE(oldToken,
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(oldInstance)));
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  talos_rt_process_instance_v1 *child = makeSchedulerInstance(completed);
  ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(context, child, 0, 1),
            TALOS_RT_OK);
  uint64_t childToken = talos_rt_v1_scheduler_process_token(context, child);
  ASSERT_NE(childToken, 0u);
  EXPECT_NE(childToken, oldToken);
  EXPECT_NE(childToken,
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(child)));

  SchedulerFixture waiter(4);
  schedulerWaitKind = TALOS_RT_SUSPEND_AWAIT;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_NONE;
  schedulerWaitHandle = childToken;
  schedulerWaitWidth = 0;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(waiter), 0, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ProcessAssociativeKeysUseStableTombstoneIdentity) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture first(100);
  SchedulerFixture second(200);
  talos_rt_process_instance_v1 *firstInstance = makeSchedulerInstance(first);
  talos_rt_process_instance_v1 *secondInstance =
      makeSchedulerInstance(second);
  ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(context, firstInstance, 0, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add_ranked(context, secondInstance, 0, 1),
            TALOS_RT_OK);
  uint64_t firstToken =
      talos_rt_v1_scheduler_process_token(context, firstInstance);
  uint64_t secondToken =
      talos_rt_v1_scheduler_process_token(context, secondInstance);
  ASSERT_NE(firstToken, 0u);
  ASSERT_GT(secondToken, firstToken);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  const talos_rt_element_type_v1 wordElement{
      TALOS_RT_VERSION, TALOS_RT_ELEMENT_BITS, 92, 0,       0,
      sizeof(uint64_t),   alignof(uint64_t),       64, nullptr, nullptr};
  talos_rt_object_v1 *array = nullptr;
  ASSERT_EQ(talos_rt_v1_assoc_create(lane, &wordElement,
                                       TALOS_RT_ASSOC_KEY_PROCESS, 0, &array),
            TALOS_RT_OK);
  talos_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            TALOS_RT_OK);

  const uint64_t tokens[] = {0, firstToken, secondToken};
  for (uint64_t index = 0; index != std::size(tokens); ++index) {
    talos_rt_assoc_key_v1 key{
        TALOS_RT_ASSOC_KEY_PROCESS, 0, 0, {tokens[index]}, {0}, 0};
    uint64_t value = index + 20;
    ASSERT_EQ(talos_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              TALOS_RT_OK);
  }

  talos_rt_assoc_key_v1 invalid{
      TALOS_RT_ASSOC_KEY_PROCESS,
      0,
      0,
      {TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG | UINT64_C(0x123456)},
      {0},
      0};
  uint64_t value = 99;
  EXPECT_EQ(talos_rt_v1_assoc_write(lane, array, &invalid, &value, nullptr),
            TALOS_RT_INVALID_HANDLE);

  talos_rt_assoc_key_v1 cursor{};
  uint32_t success = 0;
  for (uint64_t expected : tokens) {
    ASSERT_EQ(talos_rt_v1_assoc_first(lane, array, &cursor, &success),
              TALOS_RT_OK);
    ASSERT_EQ(success, 1u);
    EXPECT_EQ(cursor.kind, TALOS_RT_ASSOC_KEY_PROCESS);
    EXPECT_EQ(cursor.value, expected);
    ASSERT_EQ(talos_rt_v1_assoc_delete(array, &cursor), TALOS_RT_OK);
  }
  EXPECT_EQ(talos_rt_v1_container_size(array), 0u);

  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &arrayRoot), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ProcessObjectIdentitySharesManagedNamespaceAndSurvivesExit) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(100);
  auto *first = makeSchedulerInstance(fixture);
  auto *second = makeSchedulerInstance(fixture);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, first, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, second, 0), TALOS_RT_OK);
  const uint64_t firstToken =
      talos_rt_v1_scheduler_process_token(context, first);
  const uint64_t secondToken =
      talos_rt_v1_scheduler_process_token(context, second);
  uint64_t firstIdentity = 0;
  ASSERT_EQ(
      talos_rt_process_object_identity(context, firstToken, &firstIdentity),
      TALOS_RT_OK);
  ASSERT_NE(firstIdentity, 0u);

  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  talos_rt_object_v1 *semaphore = nullptr;
  ASSERT_EQ(talos_rt_v1_semaphore_create(lane, 1, &semaphore), TALOS_RT_OK);
  const uint64_t managedIdentity = talos_rt_v1_object_id(semaphore);
  ASSERT_NE(managedIdentity, 0u);
  EXPECT_NE(managedIdentity, firstIdentity);
  uint64_t secondIdentity = 0;
  ASSERT_EQ(
      talos_rt_process_object_identity(context, secondToken, &secondIdentity),
      TALOS_RT_OK);
  ASSERT_NE(secondIdentity, 0u);
  EXPECT_NE(secondIdentity, firstIdentity);
  EXPECT_NE(secondIdentity, managedIdentity);

  talos_rt_process_control_disposition disposition{};
  ASSERT_EQ(talos_rt_v1_process_control(context, secondToken,
                                          TALOS_RT_PROCESS_CONTROL_KILL,
                                          &disposition),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  talos_rt_process_state state{};
  ASSERT_EQ(talos_rt_v1_process_status(context, firstToken, &state),
            TALOS_RT_OK);
  EXPECT_EQ(state, TALOS_RT_PROCESS_FINISHED);
  ASSERT_EQ(talos_rt_v1_process_status(context, secondToken, &state),
            TALOS_RT_OK);
  EXPECT_EQ(state, TALOS_RT_PROCESS_KILLED);

  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  semaphore = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  EXPECT_GE(statistics.reclaimed_objects, 1u);
  for (auto [token, identity] : {std::pair{firstToken, firstIdentity},
                                 std::pair{secondToken, secondIdentity}}) {
    uint64_t afterExit = 0;
    ASSERT_EQ(talos_rt_process_object_identity(context, token, &afterExit),
              TALOS_RT_OK);
    EXPECT_EQ(afterExit, identity);
  }
  ASSERT_EQ(talos_rt_v1_semaphore_create(lane, 1, &semaphore), TALOS_RT_OK);
  const uint64_t replacementIdentity = talos_rt_v1_object_id(semaphore);
  EXPECT_NE(replacementIdentity, firstIdentity);
  EXPECT_NE(replacementIdentity, secondIdentity);
  EXPECT_NE(replacementIdentity, managedIdentity);
  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ProcessObjectIdentityPreservesTokenKindAndRejectsInvalid) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(100);
  auto *instance = makeSchedulerInstance(fixture);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  const uint64_t nativeToken =
      talos_rt_v1_scheduler_process_token(context, instance);
  const uint64_t designToken =
      nativeToken & ~TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG;
  // Model an independently scheduled bytecode activation with the same low
  // token. The shared process API must retain the native/design distinction.
  ScheduledDesignTask task;
  task.id = designToken;
  task.started = true;
  context->scheduledDesignTaskIndices.emplace(task.id, 0);
  context->scheduledDesignTasks.push_back(std::move(task));

  uint64_t nativeIdentity = 0, designIdentity = 0;
  ASSERT_EQ(
      talos_rt_process_object_identity(context, nativeToken, &nativeIdentity),
      TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_process_object_identity(context, designToken, &designIdentity),
      TALOS_RT_OK);
  EXPECT_NE(nativeIdentity, 0u);
  EXPECT_NE(designIdentity, 0u);
  EXPECT_NE(nativeIdentity, designIdentity);
  uint64_t output = 1;
  EXPECT_EQ(talos_rt_process_object_identity(nullptr, nativeToken, &output),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(output, 0u);
  output = 1;
  EXPECT_EQ(talos_rt_process_object_identity(context, 0, &output),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(output, 0u);
  EXPECT_EQ(talos_rt_process_object_identity(context, nativeToken, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  for (uint64_t unknown : {designToken + 1, nativeToken + 1}) {
    output = 1;
    EXPECT_EQ(talos_rt_process_object_identity(context, unknown, &output),
              TALOS_RT_INVALID_HANDLE);
    EXPECT_EQ(output, 0u);
  }
  talos_rt_process_control_disposition disposition{};
  // Terminate the real native activation. The independent bytecode identity
  // with the same low token must stay live; this metadata fixture does not
  // execute or terminate a bytecode frame.
  ASSERT_EQ(talos_rt_v1_process_control(context, nativeToken,
                                          TALOS_RT_PROCESS_CONTROL_KILL,
                                          &disposition),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_process_object_identity(context, designToken, &output),
            TALOS_RT_OK);
  EXPECT_EQ(output, designIdentity);
  talos_rt_process_state state{};
  ASSERT_EQ(talos_rt_v1_process_status(context, designToken, &state),
            TALOS_RT_OK);
  EXPECT_EQ(state, TALOS_RT_PROCESS_RUNNING);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ConcurrentProcessObjectQueriesShareOneIdentity) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(100);
  auto *instance = makeSchedulerInstance(fixture);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  const uint64_t token =
      talos_rt_v1_scheduler_process_token(context, instance);
  std::array<uint64_t, 8> identities{};
  std::array<talos_rt_status, 8> statuses{};
  std::array<std::thread, 8> threads;
  for (size_t index = 0; index != threads.size(); ++index)
    threads[index] = std::thread([&, index] {
      statuses[index] = talos_rt_process_object_identity(context, token,
                                                           &identities[index]);
    });
  for (std::thread &thread : threads)
    thread.join();
  EXPECT_NE(identities.front(), 0u);
  for (size_t index = 0; index != identities.size(); ++index) {
    EXPECT_EQ(statuses[index], TALOS_RT_OK);
    EXPECT_EQ(identities[index], identities.front());
  }
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CompletedProcessStorageStaysCompactAcrossChurn) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture completed(100);
  constexpr uint64_t processCount = 1000;
  for (uint64_t iteration = 0; iteration != processCount; ++iteration) {
    ASSERT_EQ(talos_rt_v1_scheduler_add(context,
                                          makeSchedulerInstance(completed), 0),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  }

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    EXPECT_TRUE(context->scheduledProcesses.empty());
    EXPECT_EQ(context->terminatedNativeProcesses.rangeCount(), 1u);
    EXPECT_EQ(context->terminatedNativeProcesses.count(1), 1u);
    EXPECT_EQ(context->terminatedNativeProcesses.count(processCount), 1u);
  }
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CompactionPreservesCursorAcrossInterleavedDeadSlots) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture first(101);
  SchedulerFixture second(102);
  SchedulerFixture third(103);
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(first), 0),
      TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(second), 0),
      TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(third), 0),
      TALOS_RT_OK);
  context->scheduledProcesses.insert(context->scheduledProcesses.begin() + 1,
                                     ScheduledProcess{});
  context->scheduledProcesses.insert(context->scheduledProcesses.begin() + 3,
                                     ScheduledProcess{});
  for (ScheduledProcess &process : context->scheduledProcesses)
    if (process.instance)
      process.urgent = true;
  context->schedulerCursor = 3;
  context->schedulerDeadProcessCount = 2;
  context->schedulerCompactionPending = true;
  schedulerOrder.clear();

  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{103, 101, 102}));
  EXPECT_TRUE(context->scheduledProcesses.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, TerminatedJoinNoneParentsPreserveDescendantAncestry) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);

  constexpr uint64_t tag = TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
  auto live = reinterpret_cast<talos_rt_process_instance_v1 *>(
      static_cast<uintptr_t>(1));
  ScheduledProcess sibling;
  sibling.instance = live;
  sibling.token = 3;
  sibling.parent = tag | 1;
  ScheduledProcess nativeGrandchild;
  nativeGrandchild.instance = live;
  nativeGrandchild.token = 5;
  nativeGrandchild.parent = tag | 4;
  context->scheduledProcesses.push_back(std::move(sibling));
  context->scheduledProcesses.push_back(std::move(nativeGrandchild));
  ScheduledDesignTask designGrandchild;
  designGrandchild.id = 7;
  designGrandchild.parent = tag | 4;
  context->scheduledDesignTasks.push_back(std::move(designGrandchild));
  context->logicalProcessParentsWithChildren.insert(tag | 4);

  // IEEE 1800-2023 9.6.1: the survivors stay children of the finished
  // process, so they are not immediate children of its parent.
  talos_rt_retire_process_parent_unlocked(context, tag | 4, tag | 2);
  EXPECT_EQ(context->scheduledProcesses[0].parent, tag | 1);
  EXPECT_EQ(context->scheduledProcesses[1].parent, tag | 4);
  EXPECT_EQ(context->scheduledDesignTasks[0].parent, tag | 4);
  EXPECT_EQ(context->logicalProcessParentsWithChildren.count(tag | 4), 0u);
  ASSERT_EQ(context->finishedProcessParents.count(tag | 4), 1u);
  EXPECT_EQ(context->finishedProcessParents[tag | 4], tag | 2);

  // Descendant collection from the parent reaches them through the link.
  bool rootLive = true;
  EXPECT_EQ(
      talos_rt_collect_process_tree_unlocked(context, tag | 2, rootLive),
      (std::unordered_set<uint64_t>{tag | 2, tag | 4, tag | 5, 7}));
  EXPECT_FALSE(rootLive);

  // The link survives while anything descends from it.
  talos_rt_prune_finished_process_parents_unlocked(context);
  EXPECT_EQ(context->finishedProcessParents.count(tag | 4), 1u);
  context->scheduledProcesses[1].instance = nullptr;
  talos_rt_prune_finished_process_parents_unlocked(context);
  EXPECT_EQ(context->finishedProcessParents.count(tag | 4), 1u);
  context->scheduledDesignTasks[0].terminated = true;
  talos_rt_prune_finished_process_parents_unlocked(context);
  EXPECT_TRUE(context->finishedProcessParents.empty());
  context->scheduledProcesses.clear();
  context->scheduledDesignTasks.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ProcessTreeCollectionFollowsChainsInAnyOrder) {
  // IEEE 1800-2023 9.6.3 and 9.7: descendants include those reached through
  // finished processes and the executing activation, in any vector order.
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  constexpr uint64_t tag = TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
  auto live = reinterpret_cast<talos_rt_process_instance_v1 *>(
      static_cast<uintptr_t>(1));
  auto addNative = [&](uint64_t token, uint64_t parent) {
    ScheduledProcess process;
    process.instance = live;
    process.token = token;
    process.parent = parent;
    context->scheduledProcesses.push_back(std::move(process));
  };
  auto addTask = [&](uint64_t id, uint64_t parent) {
    ScheduledDesignTask task;
    task.id = id;
    task.parent = parent;
    context->scheduledDesignTasks.push_back(std::move(task));
  };
  // Native 1 -> task 10 -> finished native 2 -> executing task 11 -> native
  // 3 -> task 12, with every child ahead of its parent. Native 4 descends
  // from a finished process without a link; task 13 is top level.
  addTask(12, tag | 3);
  addNative(3, 11);
  addNative(4, tag | 9);
  addTask(10, tag | 1);
  addTask(13, 0);
  addNative(1, 0);
  context->logicalProcessParentsWithChildren = {tag | 1, 10, 11, tag | 3};
  context->finishedProcessParents[tag | 2] = 10;
  context->activeLogicalProcessToken = 11;
  context->activeLogicalProcessParent = tag | 2;

  bool rootLive = false;
  EXPECT_EQ(
      talos_rt_collect_process_tree_unlocked(context, tag | 1, rootLive),
      (std::unordered_set<uint64_t>{tag | 1, 10, tag | 2, 11, tag | 3, 12}));
  EXPECT_TRUE(rootLive);
  EXPECT_EQ(
      talos_rt_collect_process_tree_unlocked(context, tag | 2, rootLive),
      (std::unordered_set<uint64_t>{tag | 2, 11, tag | 3, 12}));
  EXPECT_FALSE(rootLive);
  EXPECT_EQ(talos_rt_collect_process_tree_unlocked(context, 11, rootLive),
            (std::unordered_set<uint64_t>{11, tag | 3, 12}));
  EXPECT_TRUE(rootLive);

  context->activeLogicalProcessToken = 0;
  context->activeLogicalProcessParent = 0;
  context->scheduledProcesses.clear();
  context->scheduledDesignTasks.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ProcessTreeTakesScheduledParentsFromTheScheduler) {
  // IEEE 1800-2023 9.7: kill() reaches only descendants. An observer runs
  // as its scheduled waiter, so only a dequeued design task contributes its
  // parent from the active state.
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  constexpr uint64_t tag = TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
  auto live = reinterpret_cast<talos_rt_process_instance_v1 *>(
      static_cast<uintptr_t>(1));
  for (auto [token, parent] :
       {std::pair<uint64_t, uint64_t>{1, 0}, {2, tag | 1}, {3, 0}}) {
    ScheduledProcess process;
    process.instance = live;
    process.token = token;
    process.parent = parent;
    context->scheduledProcesses.push_back(std::move(process));
  }
  context->logicalProcessParentsWithChildren = {tag | 1};
  // Waiter 3 is active with the parent of the producer, process 2.
  context->activeLogicalProcessToken = tag | 3;
  context->activeLogicalProcessParent = tag | 1;
  bool rootLive = false;
  EXPECT_EQ(
      talos_rt_collect_process_tree_unlocked(context, tag | 1, rootLive),
      (std::unordered_set<uint64_t>{tag | 1, tag | 2}));
  EXPECT_TRUE(rootLive);

  context->activeLogicalProcessToken = 0;
  context->activeLogicalProcessParent = 0;
  context->scheduledProcesses.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, FinishedParentLinksPruneOnlyWithProcessCompaction) {
  // A prune scans every scheduled process. A dead slot below the compaction
  // threshold keeps compaction pending on every scheduler iteration, so it
  // must not repeat that scan.
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  constexpr uint64_t tag = TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
  SchedulerFixture fixture(1);
  schedulerWaitKind = TALOS_RT_SUSPEND_EVENT;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_NONE;
  schedulerWaitWidth = 0;
  schedulerResumeCount = 0;
  schedulerWaitHandle = 42;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  schedulerWaitHandle = 43;
  for (int index = 0; index != 7; ++index)
    ASSERT_EQ(
        talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
        TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  // Nothing descends from this finished parent.
  context->finishedProcessParents[tag | 1000] = 0;

  talos_rt_v1_scheduler_event(context, 42, 0);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->scheduledProcesses.size(), 8u);
  EXPECT_EQ(context->finishedProcessParents.count(tag | 1000), 1u);

  talos_rt_v1_scheduler_event(context, 43, 0);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 8u);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  EXPECT_TRUE(context->finishedProcessParents.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, FinishedParentPrunesAmortizeTheLiveProcessScan) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  constexpr uint64_t tag = TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
  constexpr uint64_t liveCount = 1000;
  auto live = reinterpret_cast<talos_rt_process_instance_v1 *>(
      static_cast<uintptr_t>(1));
  for (uint64_t token = 1; token <= liveCount; ++token) {
    ScheduledProcess process;
    process.instance = live;
    process.token = token;
    context->scheduledProcesses.push_back(std::move(process));
  }
  // Every retired parent is childless by now, so each recorded link is stale.
  uint64_t nextToken = liveCount + 1;
  auto retire = [&] {
    uint64_t finished = tag | nextToken++;
    context->logicalProcessParentsWithChildren.insert(finished);
    talos_rt_retire_process_parent_unlocked(context, finished, 0);
  };
  for (int index = 0; index != 64; ++index)
    retire();
  EXPECT_EQ(context->finishedProcessParents.size(), 64u);
  retire();
  EXPECT_EQ(context->finishedProcessParents.size(), 1u);

  // That prune scanned every live process. The next one waits until as many
  // links have been recorded.
  for (uint64_t index = 1; index != liveCount; ++index)
    retire();
  EXPECT_EQ(context->finishedProcessParents.size(), liveCount);
  retire();
  EXPECT_EQ(context->finishedProcessParents.size(), 1u);
  context->scheduledProcesses.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, NativeChildAttachIndexesActualProcess) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  constexpr uint64_t tag = TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
  context->activeLogicalProcessToken = tag | 41;
  SchedulerFixture fixture(151);
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_EQ(context->scheduledProcesses.front().parent, tag | 41);
  EXPECT_EQ(context->logicalProcessParentsWithChildren.count(tag | 41), 1u);

  talos_rt_retire_process_parent_unlocked(context, tag | 41, tag | 40);
  EXPECT_EQ(context->scheduledProcesses.front().parent, tag | 41);
  EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  EXPECT_EQ(context->finishedProcessParents.count(tag | 41), 1u);
  context->activeLogicalProcessToken = 0;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, DisabledBlockOwnerKeepsItsEarlierChildrenAsDescendants) {
  // IEEE 1800-2023 9.6.2: disabling a block terminates only the activities
  // enabled within it. A child its owner forked before entering the block
  // survives, even when the owner has no exit continuation and is terminated
  // instead of resumed. disable fork in an ancestor still reaches the child
  // through the KILLED owner (9.6.3), and so does kill() of the owner (9.7).
  constexpr uint64_t tag = TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
  SchedulerFixture ancestorFixture(172);
  SchedulerFixture ownerFixture(173);
  SchedulerFixture childFixture(174);
  struct Scenario {
    talos_rt_context *context = nullptr;
    uint64_t ancestor = 0;
    uint64_t owner = 0;
    uint64_t child = 0;
  };
  auto disableOwnerBlock = [&](Scenario &scenario) {
    talos_rt_context *&context = scenario.context;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    auto spawn = [&](SchedulerFixture &fixture, uint64_t parent) {
      context->activeLogicalProcessToken = parent;
      talos_rt_process_instance_v1 *instance =
          makeSchedulerInstance(fixture);
      EXPECT_EQ(talos_rt_v1_scheduler_add(context, instance, 0),
                TALOS_RT_OK);
      return talos_rt_v1_scheduler_process_token(context, instance);
    };
    scenario.ancestor = spawn(ancestorFixture, 0);
    scenario.owner = spawn(ownerFixture, scenario.ancestor);
    scenario.child = spawn(childFixture, scenario.owner);
    // The owner enters the block after forking and suspends inside it.
    context->activeLogicalProcessToken = scenario.owner;
    uint64_t activation = 0;
    ASSERT_EQ(talos_rt_v1_control_enter(context, 901, &activation),
              TALOS_RT_OK);
    ScheduledProcess *owner =
        findScheduledProcess(context, scenario.owner & ~tag);
    ASSERT_NE(owner, nullptr);
    owner->controls.swap(context->activeControls);

    context->activeLogicalProcessToken = tag | 900;
    ASSERT_EQ(talos_rt_v1_control_disable(context, 901, 0, 1),
              TALOS_RT_OK);
    context->activeLogicalProcessToken = 0;
    talos_rt_process_state state{};
    ASSERT_EQ(talos_rt_v1_process_status(context, scenario.owner, &state),
              TALOS_RT_OK);
    EXPECT_EQ(state, TALOS_RT_PROCESS_KILLED);
    // The unstarted child remains schedulable.
    ASSERT_EQ(talos_rt_v1_process_status(context, scenario.child, &state),
              TALOS_RT_OK);
    EXPECT_EQ(state, TALOS_RT_PROCESS_RUNNING);
  };
  auto childState = [](const Scenario &scenario) {
    talos_rt_process_state state{};
    EXPECT_EQ(talos_rt_v1_process_status(scenario.context, scenario.child,
                                           &state),
              TALOS_RT_OK);
    return state;
  };

  Scenario disableFork;
  disableOwnerBlock(disableFork);
  disableFork.context->activeLogicalProcessToken = disableFork.ancestor;
  ASSERT_EQ(talos_rt_v1_scheduler_disable_children(disableFork.context),
            TALOS_RT_OK);
  disableFork.context->activeLogicalProcessToken = 0;
  EXPECT_EQ(childState(disableFork), TALOS_RT_PROCESS_KILLED);
  talos_rt_v1_context_destroy(disableFork.context);

  Scenario kill;
  disableOwnerBlock(kill);
  talos_rt_process_control_disposition disposition{};
  ASSERT_EQ(talos_rt_v1_process_control(kill.context, kill.owner,
                                          TALOS_RT_PROCESS_CONTROL_KILL,
                                          &disposition),
            TALOS_RT_OK);
  EXPECT_EQ(childState(kill), TALOS_RT_PROCESS_KILLED);
  talos_rt_v1_context_destroy(kill.context);
}

TEST(Scheduler, KillOfFinishedProcessTerminatesLiveDescendants) {
  // IEEE 1800-2023 9.7: kill() of a FINISHED process still terminates its
  // descendants that are alive.
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  constexpr uint64_t tag = TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
  context->activeLogicalProcessToken = tag | 41;
  SchedulerFixture fixture(152);
  talos_rt_process_instance_v1 *child = makeSchedulerInstance(fixture);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, child, 0), TALOS_RT_OK);
  uint64_t childToken = talos_rt_v1_scheduler_process_token(context, child);
  context->activeLogicalProcessToken = 0;
  context->terminatedNativeProcesses.insert(41);
  talos_rt_retire_process_parent_unlocked(context, tag | 41, 0);

  talos_rt_process_control_disposition disposition{};
  ASSERT_EQ(talos_rt_v1_process_control(context, tag | 41,
                                          TALOS_RT_PROCESS_CONTROL_KILL,
                                          &disposition),
            TALOS_RT_OK);
  EXPECT_EQ(disposition, TALOS_RT_PROCESS_CONTROL_CONTINUE);
  talos_rt_process_state state{};
  ASSERT_EQ(talos_rt_v1_process_status(context, childToken, &state),
            TALOS_RT_OK);
  EXPECT_EQ(state, TALOS_RT_PROCESS_KILLED);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, DetachedChildlessTerminationDoesNotCreateParentIndex) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  constexpr uint64_t tag = TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
  context->activeLogicalProcessToken = tag | 99;
  SchedulerFixture fixture(150);
  schedulerDestroyCount = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture),
                                  TALOS_RT_SCHEDULE_STARTUP |
                                      TALOS_RT_SCHEDULE_DETACHED_CONTROLS),
      TALOS_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_EQ(context->scheduledProcesses.front().parent, 0u);
  EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  context->activeLogicalProcessToken = 0;

  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  EXPECT_EQ(schedulerDestroyCount, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ParentIndexStaysBoundedAcrossNaturalAndKilledChurn) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture urgentChild(160);
  SchedulerFixture ordinaryChild(161);
  cachedCohortUrgentChild = &urgentChild.descriptor;
  cachedCohortOrdinaryChild = &ordinaryChild.descriptor;
  cachedCohortSpawnID = 1;
  cachedCohortTaskCallID = 0;
  cachedCohortContinueID = 0;
  cachedCohortFrontierID = 0;
  cachedCohortPublishID = 0;
  cachedCohortNBAPlane = nullptr;

  for (unsigned iteration = 0; iteration != 16; ++iteration) {
    SchedulerFixture parent(1);
    parent.descriptor.native_execute = cachedSignalCohortExecute;
    talos_rt_process_instance_v1 *instance = makeSchedulerInstance(parent);
    ASSERT_NE(instance, nullptr);
    instance->continuation = 1;
    ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance,
                                          TALOS_RT_SCHEDULE_STARTUP),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
    EXPECT_TRUE(context->scheduledProcesses.empty());
    EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  }

  constexpr uint64_t tag = TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
  for (unsigned iteration = 0; iteration != 16; ++iteration) {
    context->activeLogicalProcessToken = tag | 900;
    uint64_t activation = 0;
    ASSERT_EQ(talos_rt_v1_control_enter(context, 901, &activation),
              TALOS_RT_OK);
    context->activeLogicalProcessToken = 0;
    SchedulerFixture parent(170);
    SchedulerFixture child(171);
    ASSERT_EQ(
        talos_rt_v1_scheduler_add(context, makeSchedulerInstance(parent), 0),
        TALOS_RT_OK);
    ASSERT_FALSE(context->scheduledProcesses.empty());
    uint64_t parentToken = tag | context->scheduledProcesses.back().token;
    context->activeLogicalProcessToken = parentToken;
    ASSERT_EQ(
        talos_rt_v1_scheduler_add(context, makeSchedulerInstance(child), 0),
        TALOS_RT_OK);
    EXPECT_EQ(context->logicalProcessParentsWithChildren.count(parentToken),
              1u);

    context->activeLogicalProcessToken = tag | 900;
    ASSERT_EQ(talos_rt_v1_control_disable(context, 901, activation, 0),
              TALOS_RT_OK);
    context->activeLogicalProcessToken = 0;
    ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
    EXPECT_TRUE(context->scheduledProcesses.empty());
    EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  }
  cachedCohortUrgentChild = nullptr;
  cachedCohortOrdinaryChild = nullptr;
  cachedCohortSpawnID = 0;
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, InstanceIndexTracksCallsReturnsAndAddressReuse) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_process_instance_v1 caller{}, callee{};
  context->scheduledProcesses.resize(2);
  context->scheduledProcesses[0].instance = &caller;
  context->scheduledProcesses[0].token = 1;
  context->scheduledProcesses[1].instance = &caller;
  context->scheduledProcesses[1].token = 2;
  rebuildNativeSchedulerIndexUnlocked(context);
  EXPECT_EQ(findScheduledProcessByInstance(context, &caller)->token, 1u);
  auto &first = context->scheduledProcesses.front();
  updateScheduledProcessInstance(context, first, &callee);
  EXPECT_EQ(findScheduledProcessByInstance(context, &caller)->token, 2u);
  EXPECT_EQ(findScheduledProcessByInstance(context, &callee)->token, 1u);
  updateScheduledProcessInstance(context, first, &caller);
  EXPECT_EQ(findScheduledProcessByInstance(context, &callee), nullptr);
  EXPECT_EQ(findScheduledProcessByInstance(context, &caller)->token, 1u);
  updateScheduledProcessInstance(context, first, nullptr);
  EXPECT_EQ(findScheduledProcessByInstance(context, &caller)->token, 2u);
  // Reusing the callee address cannot recover its old logical process.
  callee = {};
  updateScheduledProcessInstance(context, context->scheduledProcesses[1],
                                 &callee);
  EXPECT_EQ(findScheduledProcessByInstance(context, &caller), nullptr);
  EXPECT_EQ(findScheduledProcessByInstance(context, &callee)->token, 2u);
  context->scheduledProcesses.erase(context->scheduledProcesses.begin());
  rebuildNativeSchedulerIndexUnlocked(context);
  EXPECT_EQ(findScheduledProcessByInstance(context, &callee)->token, 2u);
  updateScheduledProcessInstance(context, context->scheduledProcesses[0],
                                 nullptr);
  EXPECT_TRUE(context->scheduledProcessTokens.empty());
  context->scheduledProcesses.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticNBASitesMergeAndCommitEachRootOnce) {
  AOTTestState state;
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 8, nullptr, nullptr, 0},
  };
  const talos_rt_static_nba_site sites[] = {
      {7, 0, TALOS_RT_STATIC_NBA_ROOT_ACCUMULATOR},
      {8, 0, TALOS_RT_STATIC_NBA_FIXED_SLOT},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_sites = sites;
  plan.nba_site_count = std::size(sites);

  talos_rt_context *context = nullptr;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  uint8_t plane = 0;
  uint8_t first = 0xa;
  uint8_t second = 0x3;
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  ASSERT_EQ(talos_rt_v1_scheduler_static_nba(context, 7, &plane, nullptr, 8,
                                               root, 4, &first, nullptr),
            TALOS_RT_OK);
  EXPECT_EQ(context->staticNBAPendingAccumulatorCount, 1u);
  ASSERT_EQ(talos_rt_v1_scheduler_static_nba(
                context, 8, &plane, nullptr, 8,
                talos_rt_v1_native_handle_offset(root, 2), 4, &second,
                nullptr),
            TALOS_RT_OK);
  // Repeated partial stages of one root remain one pending accumulator.
  EXPECT_EQ(context->staticNBAPendingAccumulatorCount, 1u);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(plane, 0x0e);
  EXPECT_EQ(context->stateValue[0], 0x0e);
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  EXPECT_EQ(context->staticNBAPendingAccumulatorCount, 0u);
  EXPECT_FALSE(context->staticNBAAccumulatorsPending);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, WideStaticNBACapturesClippedRHSAndOrderedOverwrites) {
  // LRM 10.4.2 captures the selected RHS now, 4.6 preserves overwrite order,
  // and 9.4.2 requires even a round trip to be visible to change waiters.
  constexpr uint64_t rootWidth = 197, rootOffset = 5, planeWidth = 211;
  for (bool fourState : {false, true})
    for (bool packedSlice : {false, true})
      for (int64_t offset : {-71, -3, 0, 1, 63, 64, 129, 190}) {
        SCOPED_TRACE(fourState);
        SCOPED_TRACE(packedSlice);
        SCOPED_TRACE(offset);
        AOTTestState state;
        const talos_rt_static_nba_root roots[] = {
            {17, 1, rootWidth, nullptr, nullptr, 0}};
        const talos_rt_static_nba_site sites[] = {
            {7, 0, TALOS_RT_STATIC_NBA_ROOT_ACCUMULATOR}};
        auto plan = makeAOTPlan(state, 1);
        plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
        plan.nba_roots = roots;
        plan.nba_root_count = std::size(roots);
        plan.nba_sites = sites;
        plan.nba_site_count = std::size(sites);
        talos_rt_execution_descriptor_v1 execution{};
        execution.version = TALOS_RT_VERSION;
        execution.state_bit_count = planeWidth;
        talos_rt_context *context = nullptr;
        ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
                  TALOS_RT_OK);
        context->stateUnknown.assign(4, 0);
        ASSERT_EQ(talos_rt_v1_native_state_register_static(
                      context, 1, rootOffset, rootWidth), TALOS_RT_OK);
        ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
                  TALOS_RT_OK);
        std::array<uint8_t, (planeWidth + 7) / 8> value{}, unknown{};
        value.fill(0xa5);
        unknown.fill(fourState ? 0x3c : 0);
        auto expectedValue = value, expectedUnknown = unknown;
        std::array<bool, rootWidth> captured{}, stagedValue{}, stagedUnknown{},
            transient{};
        uint64_t handle = talos_rt_v1_native_state_static_handle(1);
        auto setBit = [](auto &bytes, uint64_t bit, bool set) {
          uint8_t mask = uint8_t(1u << (bit % 8));
          bytes[bit / 8] = (bytes[bit / 8] & ~mask) | (set ? mask : 0);
        };
        for (unsigned stage = 0; stage != 3; ++stage) {
          constexpr uint64_t sourceWidth = 257;
          std::array<uint8_t, (sourceWidth + 7) / 8> rhs{}, rhsUnknown{};
          for (size_t i = 0; i != rhs.size(); ++i) {
            rhs[i] = uint8_t(37 * i + (stage == 1 ? 0x59 : 0xa6));
            rhsUnknown[i] = uint8_t(19 * i + (stage == 1 ? 0xc3 : 0x3c));
          }
          for (uint64_t source = 0; source != sourceWidth; ++source) {
            int64_t dest = offset + int64_t(source);
            if (dest < 0 || dest >= int64_t(rootWidth))
              continue;
            bool v = (rhs[source / 8] >> (source % 8)) & 1;
            bool u = fourState &&
                     ((rhsUnknown[source / 8] >> (source % 8)) & 1);
            transient[dest] |= captured[dest] &&
                               (stagedValue[dest] != v || stagedUnknown[dest] != u);
            captured[dest] = true;
            stagedValue[dest] = v;
            stagedUnknown[dest] = u;
            setBit(expectedValue, rootOffset + dest, v);
            setBit(expectedUnknown, rootOffset + dest, u);
          }
          auto *uPlane = fourState ? unknown.data() : nullptr;
          auto *uRHS = fourState ? rhsUnknown.data() : nullptr;
          auto status = packedSlice
              ? talos_rt_v1_scheduler_packed_slice_nba(
                    context, value.data(), uPlane, planeWidth, handle, rootWidth,
                    offset, 1, sourceWidth, 0, 7, UINT64_MAX, rhs.data(), uRHS)
              : talos_rt_v1_scheduler_static_nba(
                    context, 7, value.data(), uPlane, planeWidth,
                    talos_rt_v1_native_handle_offset(handle, offset),
                    sourceWidth, rhs.data(), uRHS);
          ASSERT_EQ(status, TALOS_RT_OK);
          EXPECT_TRUE(std::all_of(value.begin(), value.end(),
                                  [](uint8_t byte) { return byte == 0xa5; }));
          // Destroy the source after scheduling: commit must use the capture.
          rhs.fill(0);
          rhsUnknown.fill(0);
        }
        const auto &acc = context->staticNBAAccumulators[0];
        for (uint64_t bit = 0; bit != rootWidth; ++bit)
          EXPECT_EQ(bool((acc.transient[bit / 64] >> (bit % 64)) & 1),
                    transient[bit]);
        ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
        EXPECT_EQ(value, expectedValue);
        EXPECT_EQ(unknown, expectedUnknown);
        EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 3u);
        EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
        talos_rt_v1_context_destroy(context);
      }
}

TEST(Scheduler, GeneratedNBA256SnapshotsAndPreservesGenericLastWrite) {
  AOTTestState state;
  talos_rt_generated_nba_accumulator_256 generated{};
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated, nullptr, 0},
  };
  const talos_rt_static_nba_site sites[] = {
      {7, 0, TALOS_RT_STATIC_NBA_ROOT_ACCUMULATOR},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_sites = sites;
  plan.nba_site_count = std::size(sites);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 256;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 256),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  generated.value[0] = 0x55;
  generated.write_mask[0] = UINT32_MAX;
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_NBA;
  talos_rt_aot_deopt_snapshot snapshot{};
  ASSERT_EQ(talos_rt_v1_scheduler_snapshot_aot(context, &snapshot),
            TALOS_RT_OK);
  ASSERT_EQ(snapshot.nba_count, 1u);
  EXPECT_TRUE(std::all_of(std::begin(generated.write_mask),
                          std::end(generated.write_mask),
                          [](uint64_t mask) { return mask == 0; }));
  EXPECT_EQ(generated.valid, 0u);
  EXPECT_TRUE(context->staticNBAAccumulators[0].valid);

  uint8_t newer = 0xaa;
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  ASSERT_EQ(talos_rt_v1_scheduler_static_nba(context, 7, valuePlane.data(),
                                               nullptr, 256, root, 8, &newer,
                                               nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(valuePlane[0], 0xaa);
  EXPECT_EQ(context->stateValue[0] & 0xff, 0xaa);
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, WritableNBADirectCommitRequiresCleanLockedBoundary) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.flags =
      TALOS_RT_EXECUTION_VPI_READ | TALOS_RT_EXECUTION_VPI_WRITE;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  uint32_t fast = 1;
  talos_rt_native_schedule_plan plan{};
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP;
  plan.specialization_fast = &fast;
  context->nativeSchedulePlan = &plan;
  context->nativeScheduleGuardedFanoutActive = true;
  EXPECT_EQ(talos_rt_v1_static_nba_direct_commit_guard(context), 0u);
  {
    NativeAOTContextScope active(context);
    EXPECT_EQ(talos_rt_v1_static_nba_direct_commit_guard(context), 0u);
    NativeAOTMutexScope locked(context);
    EXPECT_EQ(talos_rt_v1_static_nba_direct_commit_guard(context), 1u);
    for (bool *condition : {&context->nativeScheduleExternalWritePending,
                            &context->nativeScheduleDirtyRootsPresent,
                            &context->nativeScheduleDeoptimized}) {
      *condition = true;
      EXPECT_EQ(talos_rt_v1_static_nba_direct_commit_guard(context), 0u);
      *condition = false;
    }
    context->nativeDynamicSignalSubscriptions = 1;
    EXPECT_EQ(talos_rt_v1_static_nba_direct_commit_guard(context), 0u);
    context->nativeDynamicSignalSubscriptions = 0;
    context->activeComputedObserverWaiterCount = 1;
    EXPECT_EQ(talos_rt_v1_static_nba_direct_commit_guard(context), 0u);
    context->activeComputedObserverWaiterCount = 0;
    fast = 0;
    EXPECT_EQ(talos_rt_v1_static_nba_direct_commit_guard(context), 0u);
    fast = 1;
    EXPECT_EQ(talos_rt_v1_static_nba_direct_commit_guard(context), 1u);
  }
  context->nativeSchedulePlan = nullptr;
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, PrivateNBABatchPreservesMasksTransientsAndFallback) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 64;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  alignas(uint64_t) std::array<uint8_t, 16> value{}, unknown{};
  ASSERT_EQ(talos_rt_v1_native_state_bind_shared(context, value.data(),
                                                   unknown.data(), 64),
            TALOS_RT_OK);
  talos_rt_generated_nba_accumulator_256 staged[3]{};
  talos_rt_static_nba_root roots[] = {{0, 1, 8, &staged[0], nullptr, 0},
                                        {1, 2, 8, &staged[1], nullptr, 0},
                                        {2, 3, 8, &staged[2], nullptr, 0}};
  uint64_t dirty = 7, summary = 1, ingress = 0;
  talos_rt_native_clock_kernel kernel{};
  kernel.ingress_mask = &ingress;
  kernel.ingress_word_count = 1;
  talos_rt_static_fanout_entry fanout[2]{};
  for (auto &entry : fanout) {
    entry.edge = TALOS_RT_WAIT_EDGE_CHANGE;
    entry.reserved = TALOS_RT_FANOUT_DIRECT;
    entry.bit_width = 8;
    entry.merged_bit = 5;
  }
  fanout[0].static_state = 1;
  fanout[1].static_state = 2;
  talos_rt_native_schedule_plan plan{};
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.state_value = value.data();
  plan.state_unknown = unknown.data();
  plan.state_bit_count = 64;
  plan.clock_kernels = &kernel;
  plan.clock_kernel_count = 1;
  plan.nba_dirty_roots = &dirty;
  plan.nba_dirty_word_count = 1;
  plan.nba_dirty_summary = &summary;
  plan.nba_dirty_summary_word_count = 1;
  context->nativeSchedulePlan = &plan;
  context->nativeScheduleNBARoots = roots;
  context->nativeScheduleNBARootCount = 3;
  context->nativeScheduleGeneratedNBAOffsets = {3, 16, 32};
  context->staticNBAAccumulators.resize(3);
  context->staticNBASlowRoots.resize(3);
  context->nativeScheduleFanoutEntries = fanout;
  context->nativeScheduleFanoutEntryCount = 2;
  context->nativeScheduleFanoutRanges = {{0, 0}, {0, 1}, {1, 2}, {2, 2}};
  for (auto &record : staged) {
    record.valid = 1;
    record.exec_region = TALOS_RT_REGION_NBA;
    record.write_mask[0] = 0xf;
  }
  // Partial, unaligned write; X/Z must preserve both neighbouring bits and
  // the unwritten part of the root. The second root's round trip ends at zero.
  value[0] = 0x87;
  unknown[0] = 0x80;
  staged[0].value[0] = 5;
  staged[0].unknown[0] = 2;
  staged[1].transient[0] = 1;
  staged[2].exec_region = TALOS_RT_REGION_RE_NBA;
  const uint32_t candidates[] = {0, 1, 2};
  uint32_t changed = 0;
  {
    NativeAOTContextScope active(context);
    NativeAOTMutexScope locked(context);
    // Neither a region mismatch nor external observation consumes staging.
    EXPECT_EQ(talos_rt_v1_static_nba_batch_guard(context, &plan,
                                                   TALOS_RT_REGION_NBA),
              1u);
    EXPECT_EQ(talos_rt_v1_static_nba_batch_guard(context, &plan,
                                                   TALOS_RT_REGION_RE_NBA),
              0u);
    context->staticNBAAccumulatorsPending = true;
    EXPECT_EQ(talos_rt_v1_static_nba_batch_guard(context, &plan,
                                                   TALOS_RT_REGION_NBA),
              0u);
    context->staticNBAAccumulatorsPending = false;
    context->staticNBASlowRootsPresent = true;
    EXPECT_EQ(talos_rt_v1_static_nba_batch_guard(context, &plan,
                                                   TALOS_RT_REGION_NBA),
              0u);
    context->staticNBASlowRootsPresent = false;
    ASSERT_EQ(talos_rt_v1_static_nba_commit_batch(
                  context, candidates, 3, TALOS_RT_REGION_RE_NBA, &changed),
              TALOS_RT_OK);
    context->vpiObservationDemand = true;
    EXPECT_EQ(talos_rt_v1_static_nba_batch_guard(context, &plan,
                                                   TALOS_RT_REGION_NBA),
              0u);
    ASSERT_EQ(talos_rt_v1_static_nba_commit_batch(
                  context, candidates, 3, TALOS_RT_REGION_NBA, &changed),
              TALOS_RT_OK);
    context->vpiObservationDemand = false;
    context->forceMask.assign(1, 8);
    EXPECT_EQ(talos_rt_v1_static_nba_batch_guard(context, &plan,
                                                   TALOS_RT_REGION_NBA),
              0u);
    ASSERT_EQ(talos_rt_v1_static_nba_commit_batch(
                  context, candidates, 3, TALOS_RT_REGION_NBA, &changed),
              TALOS_RT_OK);
    EXPECT_EQ(dirty, 7u);
    EXPECT_EQ(staged[0].valid, 1u);
    EXPECT_EQ(ingress | changed, 0u);
    context->forceMask.clear();
    ASSERT_EQ(talos_rt_v1_static_nba_commit_batch(
                  context, candidates, 3, TALOS_RT_REGION_NBA, &changed),
              TALOS_RT_OK);
  }
  EXPECT_EQ(value[0], 0xafu);
  EXPECT_EQ(unknown[0], 0x90u);
  EXPECT_EQ(value[2], 0u);
  EXPECT_EQ(staged[0].valid | staged[1].valid, 0u);
  EXPECT_EQ(staged[0].write_mask[0] | staged[1].transient[0], 0u);
  EXPECT_EQ(staged[2].valid, 1u);
  EXPECT_EQ(dirty, 4u);
  EXPECT_EQ(summary, 1u);
  EXPECT_EQ(ingress, 1u << 5);
  EXPECT_EQ(changed, 1u);
  EXPECT_EQ(context->signalDiagnostics.aotNBAPrivateCommits, 2u);
  EXPECT_EQ(context->signalDiagnostics.publications, 0u);
  // An unchanged canonical value still has to refresh a detached native mirror.
  std::array<uint8_t, 16> mirrorValue, mirrorUnknown;
  mirrorValue.fill(0xff);
  mirrorUnknown.fill(0xff);
  context->nativeStateValue = mirrorValue.data();
  context->nativeStateUnknown = mirrorUnknown.data();
  context->nativeStateBitCount = 64;
  staged[1].valid = 1;
  staged[1].write_mask[0] = 0xf;
  dirty |= 2;
  {
    NativeAOTContextScope active(context);
    NativeAOTMutexScope locked(context);
    EXPECT_EQ(talos_rt_v1_static_nba_batch_guard(context, &plan,
                                                   TALOS_RT_REGION_NBA),
              0u);
    ASSERT_EQ(talos_rt_v1_static_nba_commit_batch(
                  context, candidates + 1, 1, TALOS_RT_REGION_NBA, &changed),
              TALOS_RT_OK);
  }
  EXPECT_EQ(mirrorValue[2] | mirrorUnknown[2], 0u);
  EXPECT_EQ(dirty, 4u);
  context->nativeStateValue = value.data();
  context->nativeStateUnknown = unknown.data();
  context->nativeSchedulePlan = nullptr;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedFixedNBASlotsShareBarrierAndSnapshotWithoutQueue) {
  AOTTestState state;
  std::array<int64_t, 2> offsets{0, 64};
  std::array<uint64_t, 2> values{}, unknowns{};
  std::array<uint32_t, 2> valid{};
  std::array<std::array<uint64_t, 4>, 2> transitions{};
  const talos_rt_generated_nba_slot slots[] = {
      {&offsets[0], &values[0], &unknowns[0], &valid[0], transitions[0].data(), 64},
      {&offsets[1], &values[1], &unknowns[1], &valid[1], transitions[1].data(), 64}};
  const talos_rt_static_nba_root roots[] = {{17, 1, 128, nullptr, slots, 2}};
  alignas(uint64_t) std::array<uint8_t, 16> valuePlane{}, unknownPlane{};
  uint64_t dirtyRoots = 0, dirtySummary = 0;
  auto plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 128;
  plan.nba_roots = roots;
  plan.nba_root_count = 1;
  plan.nba_dirty_roots = &dirtyRoots;
  plan.nba_dirty_word_count = 1;
  plan.nba_dirty_summary = &dirtySummary;
  plan.nba_dirty_summary_word_count = 1;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 128;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context), TALOS_RT_OK);
  context->stateUnknown.assign(2, 0);
  ASSERT_EQ(talos_rt_v1_native_state_bind_shared(
                context, valuePlane.data(), unknownPlane.data(), 128),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 128), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  // LRM 9.4.2, 10.6.2: a forced bit and unchanged X bits do not produce a
  // transition, but the NBA still drains at its barrier (LRM 10.4.2).
  ASSERT_TRUE(context->stateValue.shared());
  ASSERT_TRUE(context->stateUnknown.shared());
  context->stateValue[0] = 0x20;
  context->stateUnknown[0] = 0x80;
  context->forceMask.assign(2, 0);
  context->forceMask[0] = 0x20;
  values = {0, 0};
  unknowns = {0x80, 0};
  valid = {1, 1};
  dirtyRoots = dirtySummary = 1;
  uint32_t unchanged = 0;
  ASSERT_EQ(talos_rt_v1_static_nba_commit_roots(
                context, 1, TALOS_RT_REGION_NBA, &unchanged),
            TALOS_RT_OK);
  EXPECT_EQ(unchanged, 0u);
  EXPECT_EQ(context->stateValue[0], 0x20u);
  EXPECT_EQ(context->stateUnknown[0], 0x80u);
  EXPECT_EQ(valid[0] | valid[1], 0u);
  EXPECT_EQ(dirtyRoots | dirtySummary, 0u);
  EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context), UINT32_MAX);
  context->forceMask.clear();

  // A runtime handover can detach the canonical planes from generated storage.
  // Both images must receive the fixed-slot commit before its publication.
  context->stateValue.assign(2, 0);
  context->stateUnknown.assign(2, 0);
  ASSERT_FALSE(context->stateValue.shared());
  ASSERT_FALSE(context->stateUnknown.shared());

  values = {0x1234, 0x5678};
  unknowns = {0x80, 0x40};
  valid = {1, 1};
  dirtyRoots = dirtySummary = 1;
  EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context), TALOS_RT_REGION_NBA);
  uint32_t changed = 0;
  ASSERT_EQ(talos_rt_v1_static_nba_commit_roots(
      context, 1, TALOS_RT_REGION_ACTIVE, &changed), TALOS_RT_OK);
  EXPECT_EQ(valid[0], 1u);
  EXPECT_EQ(dirtyRoots, 1u);
  ASSERT_EQ(talos_rt_v1_static_nba_commit_roots(
      context, 1, TALOS_RT_REGION_NBA, &changed), TALOS_RT_OK);
  EXPECT_EQ(context->stateValue[0], values[0]);
  EXPECT_EQ(context->stateValue[1], values[1]);
  EXPECT_EQ(context->stateUnknown[0], unknowns[0]);
  EXPECT_EQ(context->stateUnknown[1], unknowns[1]);
  std::array<uint64_t, 2> generatedValues{}, generatedUnknowns{};
  std::memcpy(generatedValues.data(), valuePlane.data(), valuePlane.size());
  std::memcpy(generatedUnknowns.data(), unknownPlane.data(), unknownPlane.size());
  EXPECT_EQ(generatedValues, values);
  EXPECT_EQ(generatedUnknowns, unknowns);
  EXPECT_EQ(valid[0] | valid[1], 0u);
  EXPECT_EQ(dirtyRoots | dirtySummary, 0u);
  EXPECT_FALSE(context->staticNBAAccumulators[0].valid);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context), UINT32_MAX);

  // A clipped selection stages only its overlapping source bits. Snapshot
  // materializes this fixed storage once, so deoptimization cannot lose it.
  offsets[0] = -4;
  values[0] = 0xabcd;
  unknowns[0] = 0x120;
  valid[0] = 1;
  dirtyRoots = dirtySummary = 1;
  talos_rt_aot_deopt_snapshot snapshot{};
  ASSERT_EQ(talos_rt_v1_scheduler_snapshot_aot(context, &snapshot), TALOS_RT_OK);
  EXPECT_EQ(snapshot.nba_count, 1u);
  EXPECT_EQ(valid[0], 0u);
  EXPECT_TRUE(context->staticNBAAccumulators[0].valid);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  ASSERT_EQ(talos_rt_v1_static_nba_commit_roots(
      context, 1, TALOS_RT_REGION_NBA, &changed), TALOS_RT_OK);
  EXPECT_EQ(context->stateValue[0], 0xabcu);
  EXPECT_EQ(context->stateUnknown[0], 0x12u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBAScalarCommitsValueUnknownAndPartMaskDirectly) {
  AOTTestState state;
  talos_rt_generated_nba_accumulator_256 generated{};
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 8, &generated, nullptr, 0},
  };
  uint8_t valuePlane = 0xc0;
  uint8_t unknownPlane = 0xf0;
  uint8_t nativeValue = valuePlane;
  uint8_t nativeUnknown = unknownPlane;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.state_value = &valuePlane;
  plan.state_unknown = &unknownPlane;
  plan.state_bit_count = 8;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_sync(context, &nativeValue,
                                            &nativeUnknown, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  generated.value[0] = 0x0a;
  generated.unknown[0] = 0x05;
  generated.write_mask[0] = 0x0f;
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_NBA;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(valuePlane, 0xca);
  EXPECT_EQ(unknownPlane, 0xf5);
  EXPECT_EQ(nativeValue, valuePlane);
  EXPECT_EQ(nativeUnknown, unknownPlane);
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 1u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  EXPECT_EQ(generated.write_mask[0], 0u);
  EXPECT_EQ(generated.valid, 0u);
  EXPECT_FALSE(context->staticNBAAccumulators[0].valid);

  // Inside the scheduler-owned AOT transaction the same record commits
  // directly to the canonical generated planes without materialization.
  generated.value[0] = 0x20;
  generated.unknown[0] = 0x40;
  generated.write_mask[0] = 0xf0;
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_NBA;
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(valuePlane, 0x2a);
  EXPECT_EQ(unknownPlane, 0x45);
  EXPECT_EQ(nativeValue, valuePlane);
  EXPECT_EQ(nativeUnknown, unknownPlane);
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 2u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBAScalarRecordsCoveredTransitionsExactlyOnce) {
  AOTTestState state;
  talos_rt_generated_nba_accumulator_256 generated{};
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 1, &generated, nullptr, 0},
  };
  uint8_t valuePlane = 0;
  uint8_t unknownPlane = 0;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.state_value = &valuePlane;
  plan.state_unknown = &unknownPlane;
  plan.state_bit_count = 1;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  constexpr std::array<uint8_t, 1> zero{0};
  ASSERT_EQ(
      talos_rt_v1_coverage_finalize(
          context, 0, 1, zero.data(), zero.data(),
          TALOS_RT_COVERAGE_PERSIST_ALL),
      TALOS_RT_OK);
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  ASSERT_EQ(talos_rt_v1_coverage_toggle_bind(context, 0, 1, root),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_coverage_toggle_seal(context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  generated.value[0] = 1;
  generated.write_mask[0] = 1;
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_NBA;
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);

  generated.value[0] = 0;
  generated.write_mask[0] = 1;
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_NBA;
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);

  ASSERT_NE(context->coverage, nullptr);
  ASSERT_EQ(context->coverage->toggleCounters.size(), 4u);
  EXPECT_EQ(context->coverage->toggleCounters[0], 1u);
  EXPECT_EQ(context->coverage->toggleCounters[1], 1u);
  EXPECT_EQ(context->coverage->toggleCounters[2], 0u);
  EXPECT_EQ(context->coverage->toggleCounters[3], 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, RebuiltCalendarRetainsDetachedPeriodicDeadline) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(42);
  schedulerWaitKind = TALOS_RT_SUSPEND_DELAY;
  schedulerWaitDelay = 5;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  auto *instance = makeSchedulerInstance(fixture);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_prime(context, instance), TALOS_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  ASSERT_EQ(context->scheduledProcesses.front().wakeTime, 5u);
  // The generated loop advances its detached source without maintaining the
  // generic calendar. Whole-plan fallback restores the actor's next edge,
  // then rebuilds the scheduler indices. A stale heap entry for time 5 must
  // not make the scheduler exit at time 20 with a live deadline at time 25.
  context->schedulerTime = 20;
  context->scheduledProcesses.front().wakeTime = 25;
  rebuildNativeSchedulerIndexUnlocked(context);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->schedulerTime, 25u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, DeoptimizedNBACommitDoesNotReenterGeneratedBarrier) {
  AOTTestState state;
  talos_rt_generated_nba_accumulator_256 generated{};
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 4, &generated, nullptr, 0}};
  std::array<uint8_t, 8> valuePlane{};
  std::array<uint8_t, 8> unknownPlane{};
  uint64_t dirtyRoots = 1;
  uint64_t dirtySummary = 1;
  auto plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_STATIC_NBA;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 4;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_dirty_roots = &dirtyRoots;
  plan.nba_dirty_word_count = 1;
  plan.nba_dirty_summary = &dirtySummary;
  plan.nba_dirty_summary_word_count = 1;
  // The compiled barrier owns Tier-1 publication and may only run while its
  // plan is valid. Use a sentinel rather than an infinite scheduler loop if
  // the fine scheduler accidentally calls it after deoptimization.
  plan.nba_commit = [](void *, talos_rt_context *, uint32_t, uint32_t *) {
    return TALOS_RT_TIER_UNAVAILABLE;
  };

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 4;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 4),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  generated.value[0] = 5;
  generated.unknown[0] = 2;
  generated.write_mask[0] = 15;
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_NBA;

  bool changed = false;
  EXPECT_EQ(commitStaticNBAAccumulatorsUnlocked(
                context, TALOS_RT_REGION_NBA, changed),
            TALOS_RT_TIER_UNAVAILABLE);
  EXPECT_EQ(generated.valid, 1u);
  context->nativeScheduleDeoptimized = true;
  ASSERT_EQ(commitStaticNBAAccumulatorsUnlocked(
                context, TALOS_RT_REGION_NBA, changed),
            TALOS_RT_OK);
  EXPECT_TRUE(changed);
  EXPECT_EQ(valuePlane[0], 5u);
  EXPECT_EQ(unknownPlane[0], 2u);
  EXPECT_EQ(context->stateValue[0] & 15, 5u);
  EXPECT_EQ(context->stateUnknown[0] & 15, 2u);
  EXPECT_EQ(generated.valid, 0u);
  EXPECT_EQ(dirtyRoots, 0u);
  EXPECT_EQ(dirtySummary, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBADirtyHierarchySkipsEmptyLeafPages) {
  constexpr uint32_t rootCount = 65;
  AOTTestState state;
  std::array<talos_rt_generated_nba_accumulator_256, rootCount> generated{};
  std::array<talos_rt_static_nba_root, rootCount> roots{};
  for (uint32_t root = 0; root != rootCount; ++root)
    roots[root] = {root, root + 1, 1, &generated[root], nullptr, 0};
  std::array<uint8_t, 9> valuePlane{};
  std::array<uint8_t, 9> unknownPlane{};
  std::array<uint64_t, 2> dirtyRoots{0, 1};
  std::array<uint64_t, 1> dirtySummary{uint64_t{1} << 1};
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = rootCount;
  plan.nba_roots = roots.data();
  plan.nba_root_count = rootCount;
  plan.nba_dirty_roots = dirtyRoots.data();
  plan.nba_dirty_word_count = dirtyRoots.size();
  plan.nba_dirty_summary = dirtySummary.data();
  plan.nba_dirty_summary_word_count = dirtySummary.size();

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = rootCount;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  for (uint32_t root = 0; root != rootCount; ++root)
    ASSERT_EQ(
        talos_rt_v1_native_state_register_static(context, root + 1, root, 1),
        TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  generated[64].value[0] = 1;
  generated[64].write_mask[0] = 1;
  generated[64].valid = 1;
  generated[64].exec_region = TALOS_RT_REGION_NBA;
  // Fixed-site Tier-1 stages retain constant valid/mask fields after their
  // dirty bit is consumed. They must not create a phantom runtime barrier.
  generated[0].valid = 1;
  generated[0].write_mask[0] = 1;
  generated[0].exec_region = TALOS_RT_REGION_ACTIVE;
  EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context), TALOS_RT_REGION_NBA);
  uint32_t changed = 0;
  ASSERT_EQ(talos_rt_v1_static_nba_commit_roots(
                context, rootCount, TALOS_RT_REGION_NBA, &changed),
            TALOS_RT_OK);
  EXPECT_EQ(changed, 1u);
  EXPECT_EQ(valuePlane[8] & 1, 1u);
  EXPECT_EQ(dirtyRoots[0], 0u);
  EXPECT_EQ(dirtyRoots[1], 0u);
  EXPECT_EQ(dirtySummary[0], 0u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context), UINT32_MAX);
  // A subsequent real stage must become visible again, including in the
  // first bitmap leaf after the only pending second-leaf stage was drained.
  dirtyRoots[0] = 1;
  dirtySummary[0] = 1;
  EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context), TALOS_RT_REGION_ACTIVE);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, SlotEntryPreservesProgressAndSamplesAcrossTierBoundaries) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->preponedObserverPresent = true;
  context->schedulerTime = 37;
  context->schedulerSlotProgress = 9;
  context->staticNBASlowRoots = {1};
  context->staticNBASlowRootsPresent = true;
  ASSERT_EQ(enterSchedulerTimeSlotUnlocked(context), TALOS_RT_OK);
  EXPECT_EQ(context->schedulerPreponedTime, 37u);
  EXPECT_EQ(context->schedulerSlotProgress, 0u);
  EXPECT_FALSE(context->staticNBASlowRootsPresent);
  const auto eventID = TALOS_RT_STABLE_HANDLE_PREPONED_EVENT;
  ASSERT_EQ(context->events.count(eventID), 1u);
  uint64_t generation = context->events.at(eventID).generation;
  context->schedulerSlotProgress = 4;
  context->staticNBASlowRoots[0] = 1;
  context->staticNBASlowRootsPresent = true;
  ASSERT_EQ(enterSchedulerTimeSlotUnlocked(context), TALOS_RT_OK);
  EXPECT_EQ(context->events.at(eventID).generation, generation);
  EXPECT_EQ(context->schedulerSlotProgress, 4u);
  EXPECT_TRUE(context->staticNBASlowRootsPresent);
  ++context->schedulerTime;
  ASSERT_EQ(enterSchedulerTimeSlotUnlocked(context), TALOS_RT_OK);
  EXPECT_EQ(context->events.at(eventID).generation, generation + 1);
  EXPECT_EQ(context->schedulerSlotProgress, 0u);
  EXPECT_FALSE(context->staticNBASlowRootsPresent);
  EXPECT_EQ(context->staticNBASlowRoots[0], 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ReentrantExecutorSelectionRestoresSuspendedCaller) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->schedulerTime = 37;
  context->nextSchedulerSequence = 81;
  context->activeExecRegion = TALOS_RT_REGION_REACTIVE;
  {
    NativeScheduleStepScope caller(context, 5, false, true, 19);
    context->nativeScheduleForcedExecuted = true;
    {
      NativeScheduleStepScope callback(context, UINT32_MAX, true);
      EXPECT_FALSE(callback.executed());
      EXPECT_EQ(context->nativeScheduleForcedSlot, UINT32_MAX);
      EXPECT_TRUE(context->nativeScheduleControlOnly);
      EXPECT_FALSE(context->nativeScheduleProcessFilterActive);
      {
        NativeScheduleStepScope nested(context, 8, false, true, 23);
        context->nativeScheduleForcedExecuted = true;
        EXPECT_TRUE(nested.executed());
      }
      // Completion belongs to the selected executor, not to its caller.
      EXPECT_FALSE(callback.executed());
      EXPECT_TRUE(context->nativeScheduleSingleStep);
      EXPECT_TRUE(context->nativeScheduleControlOnly);
    }
    EXPECT_TRUE(caller.executed());
    EXPECT_EQ(context->nativeScheduleForcedSlot, 5u);
    EXPECT_TRUE(context->nativeScheduleSingleStep);
    EXPECT_FALSE(context->nativeScheduleControlOnly);
    EXPECT_TRUE(context->nativeScheduleProcessFilterActive);
    EXPECT_EQ(context->nativeScheduleForcedProcessToken, 19u);
  }
  EXPECT_EQ(context->nativeScheduleForcedSlot, UINT32_MAX);
  EXPECT_FALSE(context->nativeScheduleSingleStep);
  EXPECT_FALSE(context->nativeScheduleForcedExecuted);
  EXPECT_FALSE(context->nativeScheduleControlOnly);
  EXPECT_FALSE(context->nativeScheduleProcessFilterActive);
  EXPECT_EQ(context->nativeScheduleForcedProcessToken, 0u);
  EXPECT_EQ(context->schedulerTime, 37u);
  EXPECT_EQ(context->nextSchedulerSequence, 81u);
  EXPECT_EQ(context->activeExecRegion, TALOS_RT_REGION_REACTIVE);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, DPIExportTemporarilyReleasesOuterExecutorSelection) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture exported(7);
  schedulerWaitKind = TALOS_RT_SUSPEND_DELAY;
  schedulerWaitDelay = 3;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ScheduledDesignEvent later;
  later.stableID = 1;
  later.dueTime = 99;
  later.execRegion = TALOS_RT_REGION_NBA;
  context->scheduledDesignEvents.push_back(later);
  {
    NativeScheduleStepScope caller(context, 5, true, true, 19);
    context->nativeScheduleForcedExecuted = true;
    EXPECT_EQ(talos_rt_v1_dpi_export_task_run(
                  context, makeSchedulerInstance(exported)),
              TALOS_RT_OK);
    EXPECT_EQ(schedulerResumeCount, 1u);
    EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{7}));
    EXPECT_EQ(context->schedulerTime, 3u);
    EXPECT_TRUE(caller.executed());
    EXPECT_EQ(context->nativeScheduleForcedSlot, 5u);
    EXPECT_TRUE(context->nativeScheduleSingleStep);
    EXPECT_TRUE(context->nativeScheduleControlOnly);
    EXPECT_TRUE(context->nativeScheduleProcessFilterActive);
    EXPECT_EQ(context->nativeScheduleForcedProcessToken, 19u);
    ASSERT_EQ(context->scheduledDesignEvents.size(), 1u);
    EXPECT_EQ(context->scheduledDesignEvents.front().dueTime, 99u);
  }
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ReactiveIterationSurvivesSingleStepReentry) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->nativeScheduleSingleStep = true;
  auto enqueue = [&](uint64_t id, uint32_t region) {
    ScheduledDesignEvent event;
    event.stableID = id;
    event.dueTime = 0;
    event.sequence = context->nextSchedulerSequence++;
    event.execRegion = region;
    context->scheduledDesignEvents.push_back(event);
  };
  enqueue(1, TALOS_RT_REGION_RE_NBA);
  ASSERT_EQ(runScheduler(context), TALOS_RT_OK);
  EXPECT_TRUE(context->schedulerDrainingReactive);
  EXPECT_EQ(context->events.count(1), 1u);

  // IEEE 1800-2023 4.5: work published during the Reactive iteration
  // cannot make a scheduler checkpoint resume the Active/NBA group early.
  enqueue(2, TALOS_RT_REGION_ACTIVE);
  enqueue(3, TALOS_RT_REGION_NBA);
  enqueue(4, TALOS_RT_REGION_RE_INACTIVE);
  enqueue(5, TALOS_RT_REGION_RE_NBA);
  ASSERT_EQ(runScheduler(context), TALOS_RT_OK);
  EXPECT_EQ(context->events.count(4), 1u);
  EXPECT_EQ(context->events.count(2), 0u);
  EXPECT_EQ(context->events.count(3), 0u);
  EXPECT_EQ(context->events.count(5), 0u);
  ASSERT_EQ(runScheduler(context), TALOS_RT_OK);
  EXPECT_EQ(context->events.count(5), 1u);
  EXPECT_EQ(context->events.count(2), 0u);
  EXPECT_EQ(context->events.count(3), 0u);
  ASSERT_EQ(runScheduler(context), TALOS_RT_OK);
  EXPECT_FALSE(context->schedulerDrainingReactive);
  EXPECT_EQ(context->events.count(2), 1u);
  EXPECT_EQ(context->events.count(3), 0u);
  ASSERT_EQ(runScheduler(context), TALOS_RT_OK);
  EXPECT_EQ(context->events.count(3), 1u);
  EXPECT_EQ(context->schedulerTime, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, LocalEvalNBACapturesUseTheCommonBarrier) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_native_schedule_plan plan{};
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_RUNTIME_CALENDAR_EVAL;
  context->nativeSchedulePlan = &plan;
  auto enqueue = [&](uint64_t id, uint32_t region, uint64_t time = 0) {
    ScheduledDesignEvent event;
    event.stableID = id;
    event.execRegion = region;
    event.dueTime = time;
    event.sequence = context->nextSchedulerSequence++;
    context->scheduledDesignEvents.push_back(event);
  };
  enqueue(1, TALOS_RT_REGION_ACTIVE);
  enqueue(2, TALOS_RT_REGION_INACTIVE);
  enqueue(3, TALOS_RT_REGION_POSTPONED);
  enqueue(4, TALOS_RT_REGION_ACTIVE, 7);
  {
    ContextMutexLock lock(context);
    NativeAOTContextScope aot(context);
    NativeAOTMutexScope mutex(context);
    uint32_t region = UINT32_MAX;
    // LRM 4.5, 4.9.4: no global NBA payload or ingress exists, but the local
    // capture still waits for Active/Inactive and precedes later observations.
    ASSERT_EQ(talos_rt_v1_scheduler_prepare_eval_nba_local(
                  context, TALOS_RT_REGION_NBA, &region),
              TALOS_RT_OK);
    EXPECT_EQ(region, TALOS_RT_REGION_NBA);
    EXPECT_EQ(context->events.count(1), 1u);
    EXPECT_EQ(context->events.count(2), 1u);
    EXPECT_EQ(context->events.count(3), 0u);
    EXPECT_EQ(context->events.count(4), 0u);
    EXPECT_EQ(context->schedulerTime, 0u);
    EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context),
              TALOS_RT_REGION_POSTPONED);
    ASSERT_EQ(talos_rt_v1_scheduler_complete_eval_nba(context, region, 1),
              TALOS_RT_OK);
    EXPECT_EQ(context->events.count(3), 0u);
    region = 99;
    ASSERT_EQ(talos_rt_v1_scheduler_prepare_eval_nba_local(
                  context, UINT32_MAX, &region),
              TALOS_RT_OK);
    EXPECT_EQ(region, TALOS_RT_REGION_POSTPONED);
    EXPECT_EQ(context->events.count(3), 0u);
    EXPECT_EQ(context->schedulerTime, 0u);
    EXPECT_EQ(talos_rt_v1_scheduler_prepare_eval_nba_local(
                  context, TALOS_RT_REGION_RE_NBA, &region),
              TALOS_RT_INVALID_ARGUMENT);
    context->schedulerDrainingReactive = true;
    EXPECT_EQ(talos_rt_v1_scheduler_prepare_eval_nba_local(
                  context, TALOS_RT_REGION_NBA, &region),
              TALOS_RT_TIER_UNAVAILABLE);
    context->schedulerDrainingReactive = false;
  }
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, EvalNBACutRetainsCanonicalNBAs) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_native_schedule_plan plan{};
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_RUNTIME_CALENDAR_EVAL;
  context->nativeSchedulePlan = &plan;
  ScheduledDesignEvent event;
  event.stableID = 1;
  event.execRegion = TALOS_RT_REGION_NBA;
  context->scheduledDesignEvents.push_back(event);
  {
    ContextMutexLock lock(context);
    NativeAOTContextScope aot(context);
    NativeAOTMutexScope mutex(context);
    uint32_t region = UINT32_MAX;
    ASSERT_EQ(talos_rt_v1_scheduler_prepare_eval_nba(context, &region),
              TALOS_RT_OK);
    EXPECT_EQ(region, TALOS_RT_REGION_NBA);
    EXPECT_EQ(context->events.count(1), 0u);
    ASSERT_EQ(talos_rt_v1_scheduler_complete_eval_nba(context, region, 0),
              TALOS_RT_OK);
    EXPECT_EQ(context->events.count(1), 1u);
  }
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, IdleCurrentSlotEntriesKeepLoopEffects) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_native_schedule_plan plan{};
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_RUNTIME_CALENDAR_EVAL;
  context->nativeSchedulePlan = &plan;
  {
    ContextMutexLock lock(context);
    NativeAOTContextScope aot(context);
    NativeAOTMutexScope mutex(context);
    uint32_t region = 99;
    // The first entry also enters the time slot; every region is empty after.
    ASSERT_EQ(talos_rt_v1_scheduler_prepare_eval_nba(context, &region),
              TALOS_RT_OK);
    EXPECT_EQ(region, UINT32_MAX);
    ASSERT_EQ(context->schedulerPreponedTime, context->schedulerTime);
    // An empty barrier still accounts for the generated prefix's update.
    uint64_t epoch = context->schedulerEpoch;
    uint64_t progress = context->schedulerSlotProgress;
    ASSERT_EQ(talos_rt_v1_scheduler_complete_eval_nba(
                  context, TALOS_RT_REGION_NBA, 1),
              TALOS_RT_OK);
    EXPECT_EQ(context->schedulerEpoch, epoch + 1);
    EXPECT_EQ(context->schedulerSlotProgress, progress + 1);
    ASSERT_EQ(talos_rt_v1_scheduler_complete_eval_nba(
                  context, TALOS_RT_REGION_NBA, 0),
              TALOS_RT_OK);
    EXPECT_EQ(context->schedulerEpoch, epoch + 1);
    EXPECT_EQ(context->schedulerSlotProgress, progress + 2);
    // Entering the loop invalidates an enclosing publication batch, and an
    // empty compaction request is retired.
    NativeReadyPublicationBatch outer;
    outer.valid = true;
    context->nativeReadyPublicationBatch = &outer;
    context->schedulerCompactionPending = true;
    ASSERT_EQ(talos_rt_v1_scheduler_prepare_eval_nba(context, &region),
              TALOS_RT_OK);
    EXPECT_FALSE(outer.valid);
    EXPECT_EQ(context->nativeReadyPublicationBatch, &outer);
    EXPECT_FALSE(context->schedulerCompactionPending);
    context->nativeReadyPublicationBatch = nullptr;
    // Generated ingress stays for the continuation, which consumes it at the
    // NBA cut.
    context->nativeScheduleClockIngressPending = true;
    ASSERT_EQ(talos_rt_v1_scheduler_prepare_eval_nba(context, &region),
              TALOS_RT_OK);
    EXPECT_EQ(region, UINT32_MAX);
    EXPECT_FALSE(context->nativeScheduleClockIngressPending);
    // Exhausted slot progress still fails in the full loop.
    context->schedulerSlotProgress = UINT64_MAX;
    EXPECT_EQ(talos_rt_v1_scheduler_complete_eval_nba(
                  context, TALOS_RT_REGION_NBA, 0),
              TALOS_RT_OUT_OF_RESOURCES);
    context->schedulerStatus = TALOS_RT_OK;
    context->schedulerSlotProgress = 0;
    // Pending work in any region still runs (LRM 4.5).
    ScheduledDesignEvent event;
    event.stableID = 1;
    event.execRegion = TALOS_RT_REGION_INACTIVE;
    event.sequence = context->nextSchedulerSequence++;
    context->scheduledDesignEvents.push_back(event);
    ASSERT_EQ(talos_rt_v1_scheduler_prepare_eval_nba(context, &region),
              TALOS_RT_OK);
    EXPECT_EQ(context->events.count(1), 1u);
    EXPECT_TRUE(context->scheduledDesignEvents.empty());
    // A periodic drain of an idle slot leaves the coordinator alone; pending
    // generated ingress still runs it.
    plan.flags |= TALOS_RT_NATIVE_SCHEDULE_SHARED_PERIODIC;
    uint64_t ingress = 0;
    clockCoordinatorIngress = &ingress;
    clockCoordinatorWords = 1;
    clockCoordinatorCalls = 0;
    plan.timeslot_coordinator = clockCoordinator;
    talos_rt_native_periodic_control_v1 control{};
    EXPECT_EQ(talos_rt_v1_scheduler_drain_periodic_aot(context, &control),
              TALOS_RT_OK);
    EXPECT_EQ(clockCoordinatorCalls, 0u);
    context->nativeScheduleClockIngressPending = true;
    EXPECT_EQ(talos_rt_v1_scheduler_drain_periodic_aot(context, &control),
              TALOS_RT_OK);
    EXPECT_EQ(clockCoordinatorCalls, 1u);
    EXPECT_FALSE(context->nativeScheduleClockIngressPending);
    clockCoordinatorIngress = nullptr;
  }
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, EvalNBACutReturnsBeforeTimedForeignCallback) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_native_schedule_plan plan{};
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_RUNTIME_CALENDAR_EVAL;
  context->nativeSchedulePlan = &plan;
  context->nextVPITimeCallback = 0;
  ScheduledDesignEvent event;
  event.stableID = 1;
  event.execRegion = TALOS_RT_REGION_ACTIVE;
  context->scheduledDesignEvents.push_back(event);
  {
    ContextMutexLock lock(context);
    NativeAOTContextScope aot(context);
    NativeAOTMutexScope mutex(context);
    uint32_t region = 99;
    // LRM 38.36.2/4.9.4: return while the local capture and callback deadline
    // are intact, before running either the callback or subsequent Active work.
    EXPECT_EQ(talos_rt_v1_scheduler_prepare_eval_nba_local(
                  context, TALOS_RT_REGION_NBA, &region),
              TALOS_RT_TIER_UNAVAILABLE);
    EXPECT_EQ(region, 99u);
    EXPECT_EQ(context->nextVPITimeCallback, 0u);
    EXPECT_EQ(context->events.count(1), 0u);
    EXPECT_EQ(context->schedulerTime, 0u);
    context->nextVPITimeCallback.reset();
    ASSERT_EQ(talos_rt_v1_scheduler_prepare_eval_nba_local(
                  context, TALOS_RT_REGION_NBA, &region),
              TALOS_RT_OK);
    EXPECT_EQ(context->events.count(1), 1u);
    EXPECT_EQ(region, TALOS_RT_REGION_NBA);
  }
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, WritableEvalNBACutReturnsBeforeUnscopedActor) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_native_schedule_plan plan{};
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_RUNTIME_CALENDAR_EVAL;
  context->nativeSchedulePlan = &plan;
  SchedulerFixture actor(101);
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(actor), 0),
      TALOS_RT_OK);
  {
    ContextMutexLock lock(context);
    NativeAOTContextScope aot(context);
    NativeAOTMutexScope mutex(context);
    uint32_t region = 99;
    // LRM 38.34/4.9.4: the unmapped actor could call VPI. Its exact original
    // continuation must remain runnable after the private capture is exported.
    EXPECT_EQ(talos_rt_v1_scheduler_prepare_eval_nba_local_write(
                  context, TALOS_RT_REGION_NBA, &region),
              TALOS_RT_TIER_UNAVAILABLE);
    EXPECT_EQ(schedulerResumeCount, 0u);
    EXPECT_EQ(region, 99u);
    ASSERT_EQ(context->scheduledProcesses.size(), 1u);
    EXPECT_FALSE(context->scheduledProcesses[0].started);
    // Off/read use the ordinary service and do not make this mutation query.
    ASSERT_EQ(talos_rt_v1_scheduler_prepare_eval_nba_local(
                  context, TALOS_RT_REGION_NBA, &region),
              TALOS_RT_OK);
    EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{101}));
    EXPECT_EQ(region, TALOS_RT_REGION_NBA);
    EXPECT_EQ(context->schedulerTime, 0u);
  }
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, SharedNBABarrierSelectionMatchesScalarInventory) {
  // Cross both the leaf and summary boundaries; include stale generated
  // payloads, runtime stages, empty pages, and all iterative region choices.
  constexpr uint32_t rootCount = 4103;
  constexpr uint32_t regions[] = {TALOS_RT_REGION_ACTIVE,
                                 TALOS_RT_REGION_NBA,
                                 TALOS_RT_REGION_REACTIVE,
                                 TALOS_RT_REGION_RE_NBA};
  std::vector<talos_rt_generated_nba_accumulator_256> generated(rootCount);
  std::vector<talos_rt_static_nba_root> roots(rootCount);
  std::vector<uint64_t> dirty((rootCount + 63) / 64);
  std::vector<uint64_t> summary((dirty.size() + 63) / 64);
  talos_rt_native_schedule_plan plan{};
  plan.nba_roots = roots.data();
  plan.nba_root_count = rootCount;
  plan.nba_dirty_roots = dirty.data();
  plan.nba_dirty_word_count = dirty.size();
  plan.nba_dirty_summary = summary.data();
  plan.nba_dirty_summary_word_count = summary.size();
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->nativeSchedulePlan = &plan;
  context->nativeScheduleNBARoots = roots.data();
  context->nativeScheduleNBARootCount = rootCount;
  context->nativeScheduleHasGeneratedNBAAccumulators = true;
  context->staticNBAAccumulators.resize(rootCount);
  std::mt19937 random(0x18002023);
  for (unsigned trial = 0; trial != 100; ++trial) {
    context->schedulerDrainingReactive = trial % 2 != 0;
    // IEEE 1800-2023 4.4.2.4, 4.4.2.8: compiler-certified static NBA
    // batches use only NBA/Re-NBA. Compare their early barrier selection
    // with the same exhaustive oracle, including reactive-set isolation.
    bool certified = trial % 4 >= 2;
    plan.flags =
        certified ? TALOS_RT_NATIVE_SCHEDULE_RUNTIME_CALENDAR_EVAL : 0;
    auto nextRegion = [&] {
      return regions[certified ? 1 + 2 * (random() % 2)
                               : random() % std::size(regions)];
    };
    std::fill(dirty.begin(), dirty.end(), 0);
    std::fill(summary.begin(), summary.end(), 0);
    context->staticNBAAccumulatorsPending = false;
    context->staticNBAPendingAccumulatorCount = 0;
    uint32_t expectedGenerated = UINT32_MAX;
    uint32_t expectedRuntime = UINT32_MAX;
    for (uint32_t root = 0; root != rootCount; ++root) {
      roots[root].generated_accumulator = &generated[root];
      generated[root].valid = random() % 2;
      generated[root].exec_region = nextRegion();
      auto &runtime = context->staticNBAAccumulators[root];
      runtime.valid = false;
      runtime.execRegion = nextRegion();
      if (trial == 0 || (root != rootCount - 1 && random() % 1024 != 0))
        continue;
      dirty[root / 64] |= uint64_t{1} << (root % 64);
      summary[root / 4096] |= uint64_t{1} << ((root / 64) % 64);
      if (random() % 2) {
        markStaticNBAAccumulatorPending(context, root, runtime);
        if (!context->schedulerDrainingReactive ||
            runtime.execRegion >= TALOS_RT_REGION_REACTIVE)
          expectedRuntime = std::min(expectedRuntime, runtime.execRegion);
      }
      if (generated[root].valid &&
          (!context->schedulerDrainingReactive ||
           generated[root].exec_region >= TALOS_RT_REGION_REACTIVE))
        expectedGenerated =
            std::min(expectedGenerated, generated[root].exec_region);
    }
    SCOPED_TRACE(trial);
    EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context, false), expectedRuntime);
    EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context),
              std::min(expectedGenerated, expectedRuntime));
    // The unindexed policy sees only live payloads. Removing stale payloads
    // must yield the same answer without altering the runtime stages.
    for (uint32_t root = 0; root != rootCount; ++root)
      if ((dirty[root / 64] & (uint64_t{1} << (root % 64))) == 0)
        generated[root].valid = 0;
    plan.nba_dirty_roots = nullptr;
    plan.nba_dirty_summary = nullptr;
    EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context, false), expectedRuntime);
    EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context),
              std::min(expectedGenerated, expectedRuntime));
    plan.nba_dirty_roots = dirty.data();
    plan.nba_dirty_summary = summary.data();
  }
  context->nativeSchedulePlan = nullptr;
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTControlSelectsNBABarrierFromDirtyHierarchy) {
  constexpr uint32_t rootCount = 65;
  AOTTestState state;
  std::array<talos_rt_generated_nba_accumulator_256, rootCount> generated{};
  std::array<talos_rt_static_nba_root, rootCount> roots{};
  for (uint32_t root = 0; root != rootCount; ++root)
    roots[root] = {root, root + 1, 1, &generated[root], nullptr, 0};
  std::array<uint8_t, 9> valuePlane{};
  std::array<uint8_t, 9> unknownPlane{};
  std::array<uint64_t, 2> dirtyRoots{0, 1};
  std::array<uint64_t, 1> dirtySummary{uint64_t{1} << 1};
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_NBA;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = rootCount;
  plan.nba_roots = roots.data();
  plan.nba_root_count = rootCount;
  plan.nba_dirty_roots = dirtyRoots.data();
  plan.nba_dirty_word_count = dirtyRoots.size();
  plan.nba_dirty_summary = dirtySummary.data();
  plan.nba_dirty_summary_word_count = dirtySummary.size();
  plan.nba_commit = aotCommitAllNBARoots;
  plan.run = aotRunWaitNodes;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = rootCount;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  for (uint32_t root = 0; root != rootCount; ++root)
    ASSERT_EQ(
        talos_rt_v1_native_state_register_static(context, root + 1, root, 1),
        TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  generated[64].value[0] = 1;
  generated[64].write_mask[0] = 1;
  generated[64].valid = 1;
  generated[64].exec_region = TALOS_RT_REGION_NBA;
  SchedulerFixture fixture(1);
  fixture.descriptor.execution = &execution;
  schedulerWaitKind = TALOS_RT_SUSPEND_DELAY;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ASSERT_EQ(
      talos_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(valuePlane[8] & 1, 1u);
  EXPECT_EQ(dirtyRoots[0], 0u);
  EXPECT_EQ(dirtyRoots[1], 0u);
  EXPECT_EQ(dirtySummary[0], 0u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GuardedNBAClaimsPreserveGeneratedLastWriteOrder) {
  AOTTestState state;
  talos_rt_generated_nba_accumulator_256 generated{};
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated, nullptr, 0},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  uint32_t specializationFast = 0;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_NBA |
               TALOS_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_commit = aotCommitOneNBARoot;
  plan.specialization_fast = &specializationFast;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 256;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 256),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  state.runHook = runGuardedNBAOrdering;
  state.generatedNBA = &generated;
  state.generatedNBAFirst = true;
  state.generatedNBAValue = 0x11111111;
  state.claimedNBAValue = 0x22222222;
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  uint32_t changed = 0;
  ASSERT_EQ(talos_rt_v1_static_nba_commit_root(
                context, 0, TALOS_RT_REGION_NBA, &changed),
            TALOS_RT_OK);
  EXPECT_EQ(changed, 1u);
  uint32_t observed = 0;
  std::memcpy(&observed, valuePlane.data(), sizeof(observed));
  EXPECT_EQ(observed, 0x22222222u);

  state.generatedNBAFirst = false;
  state.generatedNBAValue = 0x44444444;
  state.claimedNBAValue = 0x33333333;
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  changed = 0;
  ASSERT_EQ(talos_rt_v1_static_nba_commit_root(
                context, 0, TALOS_RT_REGION_NBA, &changed),
            TALOS_RT_OK);
  EXPECT_EQ(changed, 1u);
  std::memcpy(&observed, valuePlane.data(), sizeof(observed));
  EXPECT_EQ(observed, 0x44444444u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBA256SnapshotPreservesReactiveCommitRegion) {
  AOTTestState state;
  talos_rt_generated_nba_accumulator_256 generated{};
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated, nullptr, 0},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 256;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 256),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  generated.value[0] = 0x55;
  generated.write_mask[0] = UINT32_MAX;
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_RE_NBA;
  talos_rt_aot_deopt_snapshot snapshot{};
  ASSERT_EQ(talos_rt_v1_scheduler_snapshot_aot(context, &snapshot),
            TALOS_RT_OK);
  ASSERT_EQ(snapshot.nba_count, 1u);
  EXPECT_EQ(snapshot.nbas[0].exec_region, TALOS_RT_REGION_RE_NBA);
  EXPECT_EQ(context->staticNBAAccumulators[0].execRegion,
            TALOS_RT_REGION_RE_NBA);
  EXPECT_TRUE(std::all_of(std::begin(generated.write_mask),
                          std::end(generated.write_mask),
                          [](uint64_t mask) { return mask == 0; }));
  EXPECT_EQ(generated.valid, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBA256CommitsDirectlyWithoutFanout) {
  AOTTestState state;
  talos_rt_generated_nba_accumulator_256 generated{};
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated, nullptr, 0},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  std::array<uint8_t, 32> nativeValue{};
  std::array<uint8_t, 32> nativeUnknown{};
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 256;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 256),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_sync(
                context, nativeValue.data(), nativeUnknown.data(), 256),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  generated.value[0] = 0x55;
  generated.value[3] = UINT64_C(0xaa00000000000000);
  generated.write_mask[0] = UINT32_MAX;
  generated.write_mask[3] = UINT64_C(0xffffffff00000000);
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_NBA;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(valuePlane[0], 0x55);
  EXPECT_EQ(valuePlane[31], 0xaa);
  EXPECT_EQ(context->stateValue[0] & 0xff, 0x55);
  EXPECT_EQ(context->stateValue[3] >> 56, 0xaa);
  EXPECT_EQ(nativeValue, valuePlane);
  EXPECT_EQ(nativeUnknown, unknownPlane);
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  EXPECT_FALSE(context->staticNBAAccumulators[0].valid);
  EXPECT_TRUE(std::all_of(std::begin(generated.write_mask),
                          std::end(generated.write_mask),
                          [](uint64_t mask) { return mask == 0; }));
  EXPECT_EQ(context->signalDiagnostics.publications, 0u);

  // The active-AOT batch path intentionally leaves the canonical context
  // plane lazy, but every separately bound native tier must still observe the
  // committed plan state.
  generated.value[0] = 0xaa;
  generated.value[3] = UINT64_C(0x5500000000000000);
  generated.unknown[0] = 0x33;
  generated.write_mask[0] = UINT32_MAX;
  generated.write_mask[3] = UINT64_C(0xffffffff00000000);
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_NBA;
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(valuePlane[0], 0xaa);
  EXPECT_EQ(valuePlane[31], 0x55);
  EXPECT_EQ(unknownPlane[0], 0x33);
  EXPECT_EQ(nativeValue, valuePlane);
  EXPECT_EQ(nativeUnknown, unknownPlane);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBA256BatchRejectsRootsThatNeedTransitions) {
  AOTTestState state;
  talos_rt_generated_nba_accumulator_256 generated{};
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated, nullptr, 0},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 256;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 256),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  ASSERT_EQ(context->staticNBARootHasFanout.size(), 1u);
  context->staticNBARootHasFanout[0] = 1;

  generated.value[0] = 0x55;
  generated.write_mask[0] = UINT32_MAX;
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_NBA;
  uint32_t changed = 0;
  ASSERT_EQ(talos_rt_v1_static_nba_commit_roots(
                context, 1, TALOS_RT_REGION_NBA, &changed),
            TALOS_RT_OK);
  EXPECT_EQ(changed, 1u);
  EXPECT_EQ(valuePlane[0], 0x55);
  // Transition-tracking commits keep canonical runtime state synchronized.
  // The no-fanout AVX batch intentionally defers that duplicate write.
  EXPECT_EQ(context->stateValue[0] & 0xff, 0x55);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBA256TracksTransitionsWithoutExactFanout) {
  constexpr uint64_t rootOffset = 5;
  constexpr uint64_t planeBits = 272;
  AOTTestState state;
  talos_rt_generated_nba_accumulator_256 generated{};
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated, nullptr, 0},
  };
  std::array<uint8_t, 34> valuePlane{};
  std::array<uint8_t, 34> unknownPlane{};
  valuePlane[0] = 0x1f;
  valuePlane[32] = 0xe0;
  valuePlane[33] = 0xa5;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = planeBits;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = planeBits;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->signalDiagnosticsEnabled = true;
  context->stateUnknown.assign(5, 0);
  ASSERT_EQ(
      talos_rt_v1_native_state_register_static(context, 1, rootOffset, 256),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  generated.value[0] = 0x55;
  generated.value[3] = UINT64_C(0xaa00000000000000);
  generated.write_mask[0] = UINT32_MAX;
  generated.write_mask[3] = UINT64_C(0xffffffff00000000);
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_NBA;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  auto readRootByte = [=](const uint8_t *plane, uint64_t byte) {
    uint64_t bit = rootOffset + byte * 8;
    uint64_t offset = bit / 8;
    unsigned shift = static_cast<unsigned>(bit % 8);
    uint16_t window = static_cast<uint16_t>(plane[offset]) |
                      (static_cast<uint16_t>(plane[offset + 1]) << 8);
    return static_cast<uint8_t>(window >> shift);
  };
  EXPECT_EQ(readRootByte(valuePlane.data(), 0), 0x55);
  EXPECT_EQ(
      readRootByte(
          reinterpret_cast<const uint8_t *>(context->stateValue.data()), 0),
      0x55);
  EXPECT_EQ(readRootByte(valuePlane.data(), 31), 0xaa);
  EXPECT_EQ(
      readRootByte(
          reinterpret_cast<const uint8_t *>(context->stateValue.data()), 31),
      0xaa);
  EXPECT_EQ(valuePlane[0] & 0x1f, 0x1f);
  EXPECT_EQ(valuePlane[32] & 0xe0, 0xe0);
  EXPECT_EQ(valuePlane[33], 0xa5);
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  EXPECT_EQ(context->signalDiagnostics.publications, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBA256CommitHonorsOverrideMask) {
  AOTTestState state;
  talos_rt_generated_nba_accumulator_256 generated{};
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated, nullptr, 0},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  valuePlane[0] = 0x33;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 256;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 256),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  context->forceMask.assign(4, 0);
  context->forceMask[0] = 0xff;
  ASSERT_FALSE(context->nativeScheduleDirtyRootsPresent);

  generated.value[0] = 0x55;
  generated.write_mask[0] = UINT32_MAX;
  generated.valid = 1;
  generated.exec_region = TALOS_RT_REGION_NBA;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(valuePlane[0], 0x33);
  EXPECT_EQ(context->stateValue[0] & 0xff, 0x33);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, TransientDirtyRootReconcilesWithoutRunnableActor) {
  AOTTestState state;
  uint8_t valuePlane = 0;
  uint8_t unknownPlane = 0;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = &valuePlane;
  plan.state_unknown = &unknownPlane;
  plan.state_bit_count = 8;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  context->stateValue[0] = 0x5a;
  talos_rt_aot_external_write_range_unlocked(context, 0, 8, false);
  ASSERT_TRUE(context->nativeScheduleExternalWritePending);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(valuePlane, 0x5a);
  EXPECT_TRUE(context->nativeScheduleTransientDirtyRoots.empty());
  EXPECT_FALSE(context->nativeScheduleExternalWritePending);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, PersistentDirtyRootSurvivesPartialAndOverlappingRelease) {
  AOTTestState state;
  std::array<uint8_t, 16> valuePlane{};
  std::array<uint8_t, 16> unknownPlane{};
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 128;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 128;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(2, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 128),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  constexpr uint64_t firstForce = uint64_t{1} << 1;
  constexpr uint64_t secondForce = uint64_t{1} << 6;
  constexpr uint64_t assigned = uint64_t{1} << 26;
  context->forceMask.assign(2, 0);
  context->assignMask.assign(2, 0);
  context->forceMask[0] = firstForce;
  context->forceMask[1] = secondForce | assigned;
  context->assignMask[1] = assigned;
  context->stateValue[0] |= firstForce;
  context->stateValue[1] |= secondForce;
  talos_rt_aot_external_write_range_unlocked(context, 0, 128, true);

  context->forceMask[0] &= ~firstForce;
  talos_rt_aot_release_range_unlocked(context, 1, 1);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 1u);

  context->forceMask[1] &= ~secondForce;
  talos_rt_aot_release_range_unlocked(context, 70, 1);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 1u);

  // Releasing force reveals the still-active procedural assignment.
  context->forceMask[1] &= ~assigned;
  context->stateValue[1] |= assigned;
  talos_rt_aot_release_range_unlocked(context, 90, 1);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 1u);

  context->assignMask[1] &= ~assigned;
  talos_rt_aot_release_range_unlocked(context, 90, 1);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 0u);
  EXPECT_EQ(valuePlane[0] & firstForce, firstForce);
  EXPECT_EQ(valuePlane[8] & secondForce, secondForce);
  EXPECT_EQ(valuePlane[11] & uint8_t{1} << 2, uint8_t{1} << 2);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, PersistentReleaseReconcilesRetainedValueToGeneratedPlane) {
  AOTTestState state;
  uint8_t valuePlane = 0x11;
  uint8_t unknownPlane = 0;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL;
  plan.state_value = &valuePlane;
  plan.state_unknown = &unknownPlane;
  plan.state_bit_count = 8;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  context->forceMask.assign(1, 0xff);
  context->stateValue[0] = 0xa5;
  talos_rt_aot_external_write_range_unlocked(context, 0, 8, true);
  ASSERT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 1u);
  EXPECT_EQ(valuePlane, 0x11);

  context->forceMask[0] = 0;
  talos_rt_aot_release_range_unlocked(context, 0, 8);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 0u);
  EXPECT_EQ(valuePlane, 0xa5);
  EXPECT_EQ(talos_rt_v1_static_specialization_guard(
                context, UINT32_MAX, 1, TALOS_RT_STATIC_ROOT_READ),
            1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticNBASitesClipToStaticRootAndPreserveFourState) {
  AOTTestState state;
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 8, nullptr, nullptr, 0},
  };
  const talos_rt_static_nba_site sites[] = {
      {7, 0, TALOS_RT_STATIC_NBA_ROOT_ACCUMULATOR},
      {8, 0, TALOS_RT_STATIC_NBA_FIXED_SLOT},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_sites = sites;
  plan.nba_site_count = std::size(sites);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 16;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 4, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  uint16_t valuePlane = 0;
  uint16_t unknownPlane = 0;
  uint8_t lowValue = 0xc;
  uint8_t lowUnknown = 0x4;
  uint8_t highValue = 0xf;
  uint8_t highUnknown = 0x3;
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  ASSERT_EQ(talos_rt_v1_scheduler_static_nba(
                context, 7, reinterpret_cast<uint8_t *>(&valuePlane),
                reinterpret_cast<uint8_t *>(&unknownPlane), 16,
                talos_rt_v1_native_handle_offset(root, -2), 4, &lowValue,
                &lowUnknown),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_static_nba(
                context, 8, reinterpret_cast<uint8_t *>(&valuePlane),
                reinterpret_cast<uint8_t *>(&unknownPlane), 16,
                talos_rt_v1_native_handle_offset(root, 6), 4, &highValue,
                &highUnknown),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(valuePlane, 0x0c30);
  EXPECT_EQ(unknownPlane, 0x0c10);
  EXPECT_EQ(context->stateValue[0], 0x0c30);
  EXPECT_EQ(context->stateUnknown[0], 0x0c10);
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticNBASitesPreserveMixedGenericExecutionOrder) {
  AOTTestState state;
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 8, nullptr, nullptr, 0},
  };
  const talos_rt_static_nba_site sites[] = {
      {7, 0, TALOS_RT_STATIC_NBA_ROOT_ACCUMULATOR},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_sites = sites;
  plan.nba_site_count = std::size(sites);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  uint8_t plane = 0;
  uint8_t older = 0x55;
  uint8_t newer = 0xaa;
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  uint32_t stateFast = 1, nbaFast = 1;
  context->nativeStateSpecializationFast = &stateFast;
  plan.specialization_fast = &nbaFast;
  ASSERT_EQ(talos_rt_v1_scheduler_nba(context, &plane, nullptr, 8, root, 8, 0,
                                        &older, nullptr),
            TALOS_RT_OK);
  EXPECT_EQ(stateFast, 1u);
  EXPECT_EQ(nbaFast, 0u);
  ASSERT_EQ(talos_rt_v1_scheduler_static_nba(context, 7, &plane, nullptr, 8,
                                               root, 8, &newer, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(plane, 0xaa);
  EXPECT_EQ(context->stateValue[0], 0xaa);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticNBAExternalWriteSlowsOnlyIntersectingRoot) {
  AOTTestState state;
  const talos_rt_static_nba_root roots[] = {
      {17, 1, 8, nullptr, nullptr, 0},
      {18, 2, 8, nullptr, nullptr, 0},
  };
  const talos_rt_static_nba_site sites[] = {
      {7, 0, TALOS_RT_STATIC_NBA_ROOT_ACCUMULATOR},
      {8, 1, TALOS_RT_STATIC_NBA_ROOT_ACCUMULATOR},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_sites = sites;
  plan.nba_site_count = std::size(sites);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 16;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 8, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  uint16_t plane = 0;
  uint8_t value = 0x5a;
  uint64_t cleanRoot = talos_rt_v1_native_state_static_handle(2);
  uint64_t dirtyRoot = talos_rt_v1_native_state_static_handle(1);
  talos_rt_aot_external_write_range_unlocked(context, 0, 8, false);
  ASSERT_EQ(talos_rt_v1_scheduler_static_nba(
                context, 8, reinterpret_cast<uint8_t *>(&plane), nullptr, 16,
                cleanRoot, 8, &value, nullptr),
            TALOS_RT_OK);
  EXPECT_TRUE(context->staticNBAAccumulators[1].valid);
  EXPECT_TRUE(context->scheduledNBAs.empty());

  ASSERT_EQ(talos_rt_v1_scheduler_static_nba(
                context, 7, reinterpret_cast<uint8_t *>(&plane), nullptr, 16,
                dirtyRoot, 8, &value, nullptr),
            TALOS_RT_OK);
  EXPECT_FALSE(context->staticNBAAccumulators[0].valid);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().bitOffset, dirtyRoot);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticNBATablesRejectDuplicateAndMismatchedRoots) {
  AOTTestState state;
  const talos_rt_static_nba_root duplicateRoots[] = {
      {17, 1, 8, nullptr, nullptr, 0},
      {18, 1, 8, nullptr, nullptr, 0},
  };
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.nba_roots = duplicateRoots;
  plan.nba_root_count = std::size(duplicateRoots);

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_INVALID_ARGUMENT);

  const talos_rt_static_nba_root mismatchedRoot[] = {
      {17, 2, 8, nullptr, nullptr, 0},
  };
  plan.nba_roots = mismatchedRoot;
  plan.nba_root_count = std::size(mismatchedRoot);
  EXPECT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan),
            TALOS_RT_LAYOUT_MISMATCH);

  const talos_rt_static_nba_root validRoot[] = {
      {17, 1, 8, nullptr, nullptr, 0},
  };
  const talos_rt_static_nba_site validSite[] = {
      {7, 0, TALOS_RT_STATIC_NBA_ROOT_ACCUMULATOR},
  };
  plan.nba_roots = validRoot;
  plan.nba_root_count = std::size(validRoot);
  plan.nba_sites = validSite;
  plan.nba_site_count = std::size(validSite);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  uint8_t plane = 0;
  uint8_t value = 1;
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  EXPECT_EQ(talos_rt_v1_scheduler_static_nba(context, 8, &plane, nullptr, 8,
                                               root, 8, &value, nullptr),
            TALOS_RT_INVALID_DESIGN);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, DelayedNBAsAdvanceTimeAndPreserveQueueOrder) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  uint8_t plane = 0;
  uint8_t first = 0x35;
  uint8_t second = 0xa6;
  ASSERT_EQ(talos_rt_v1_scheduler_nba(context, &plane, nullptr, 8, 0, 8, 5,
                                        &first, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_nba(context, &plane, nullptr, 8, 0, 8, 5,
                                        &second, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(plane, second);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, CanonicalNBAKeepsBoundNativeStateMirrorCoherent) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);

  uint8_t nativeValue = 0;
  uint8_t nativeUnknown = 0;
  ASSERT_EQ(talos_rt_v1_native_state_sync(context, &nativeValue,
                                            &nativeUnknown, 8),
            TALOS_RT_OK);
  uint8_t value = 0xa6;
  uint8_t unknown = 0x18;
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  ASSERT_EQ(talos_rt_v1_scheduler_nba(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                root, 8, 0, &value, &unknown),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  EXPECT_EQ(context->stateValue[0] & 0xff, value);
  EXPECT_EQ(context->stateUnknown[0] & 0xff, unknown);
  EXPECT_EQ(nativeValue, value);
  EXPECT_EQ(nativeUnknown, unknown);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, UnknownSummaryClearsCleanRootsAndReportsRuntimeWrites) {
  // A clear summary bit promises that the root's unknown plane matches its
  // pattern: X/Z exactly in the pattern's bits. Refresh clears bits only
  // after scanning; runtime writes that break the match set them before
  // generated code runs again.
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 192;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 64),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 64, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 3, 128, 64),
            TALOS_RT_OK);
  std::array<uint64_t, 4> value{}, unknown{};
  unknown[2] = uint64_t{0xff} << 32; // root 3 bits [160, 168) hold X
  auto *v = reinterpret_cast<uint8_t *>(value.data());
  auto *u = reinterpret_cast<uint8_t *>(unknown.data());
  ASSERT_EQ(talos_rt_v1_native_state_sync(context, v, u, 192), TALOS_RT_OK);
  // Summary bit 0 is root 3, whose pattern holds X/Z in bits [160, 168);
  // bit 1 is root 1 and bit 2 is root 2.
  const talos_rt_native_unknown_root_v1 roots[] = {
      {128, 64, 0, 1}, {0, 64, 0, 0}, {64, 8, 0, 0}};
  const uint64_t patterns[] = {160, 8};
  const talos_rt_native_unknown_root_v1 overlapping[] = {{0, 64, 0, 0},
                                                           {32, 8, 0, 0}};
  uint64_t summary = 0;
  EXPECT_EQ(talos_rt_v1_native_state_bind_unknown_summary(
                context, u, 192, &summary, overlapping, 2, nullptr, 0),
            TALOS_RT_INVALID_ARGUMENT);
  ASSERT_EQ(talos_rt_v1_native_state_bind_unknown_summary(
                context, u, 192, &summary, roots, 3, patterns, 1),
            TALOS_RT_OK);
  EXPECT_EQ(summary, 0b111u);
  EXPECT_EQ(talos_rt_v1_native_state_bind_unknown_summary(
                context, u, 192, &summary, roots, 3, patterns, 1),
            TALOS_RT_INVALID_LIFECYCLE);
  const uint64_t all[] = {0, 0b111};
  EXPECT_EQ(talos_rt_v1_native_state_refresh_unknown_summary(context, all, 1,
                                                               nullptr),
            1u);
  EXPECT_EQ(summary, 0u);

  // X/Z outside the pattern keeps root 3 dirty. Each failed refresh doubles
  // the gate's delay, the number of tests generated code fails before it
  // calls again, up to a bound; a successful one clears it.
  talos_rt_v1_native_state_mark_unknown(context, 130, 1);
  EXPECT_EQ(summary, 0b001u);
  unknown[2] |= 4;
  const uint64_t third[] = {0, 0b001};
  const auto &state = *context->nativeUnknownSummary;
  uint64_t backoff = 0;
  auto refresh = [&] {
    return talos_rt_v1_native_state_refresh_unknown_summary(context, third, 1,
                                                              &backoff);
  };
  EXPECT_EQ(refresh(), 0u);
  EXPECT_EQ(state.rescans, 4u);
  EXPECT_EQ(backoff, uint64_t{1} << 32 | 1);
  EXPECT_EQ(refresh(), 0u);
  EXPECT_EQ(state.rescans, 5u);
  EXPECT_EQ(backoff, uint64_t{3} << 32 | 3);
  unknown[2] &= ~uint64_t{4};
  EXPECT_EQ(refresh(), 1u);
  EXPECT_EQ(backoff, 0u);
  EXPECT_EQ(summary, 0u);
  // A known bit inside the pattern breaks the match as well.
  unknown[2] &= ~(uint64_t{1} << 33);
  talos_rt_v1_native_state_mark_unknown(context, 161, 1);
  for (unsigned attempt = 0; attempt != 12; ++attempt)
    EXPECT_EQ(refresh(), 0u);
  EXPECT_EQ(backoff, uint64_t{1024} << 32 | 1024);
  unknown[2] |= uint64_t{1} << 33;
  EXPECT_EQ(refresh(), 1u);

  // A runtime NBA of X/Z marks its root; a known one does not.
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  uint8_t nextValue = 0x5a, nextUnknown = 0;
  ASSERT_EQ(talos_rt_v1_scheduler_nba(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 192,
                root, 8, 0, &nextValue, &nextUnknown),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(value[0] & 0xff, 0x5au);
  EXPECT_EQ(summary, 0u);
  nextUnknown = 0x10;
  ASSERT_EQ(talos_rt_v1_scheduler_nba(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 192,
                root, 8, 0, &nextValue, &nextUnknown),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(unknown[0] & 0xff, 0x10u);
  EXPECT_EQ(summary, 0b010u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AllKnownSummaryRetainsUnknownsAcrossPartialKnownWrites) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  std::array<uint64_t, 2> unknown{};
  uint64_t summary = 0;
  const talos_rt_native_unknown_root_v1 roots[] = {{0, 64, 0, 0},
                                                   {64, 64, 0, 0}};
  ASSERT_EQ(talos_rt_v1_native_state_bind_unknown_summary(
                context, reinterpret_cast<uint8_t *>(unknown.data()), 128,
                &summary, roots, 2, nullptr, 0),
            TALOS_RT_OK);
  const uint64_t all[] = {0, 3};
  auto refresh = [&] {
    return talos_rt_v1_native_state_refresh_unknown_summary(context, all, 1,
                                                              nullptr);
  };
  ASSERT_EQ(refresh(), 1u);
  EXPECT_EQ(summary, 0u);
  // Bits outside the written mask, or beyond the write width, do not taint
  // a root. A known write must not clear an earlier, still-live X/Z.
  markNativeUnknownBitsUnlocked(context, 0, 32, UINT64_MAX << 32);
  markNativeUnknownBitsUnlocked(context, 0, 64, UINT64_MAX, 0);
  EXPECT_EQ(summary, 0u);
  unknown[0] = 1;
  markNativeUnknownBitsUnlocked(context, 0, 64, 1);
  EXPECT_EQ(summary, 1u);
  markNativeUnknownBitsUnlocked(context, 1, 64, 0);
  EXPECT_EQ(summary, 1u);
  EXPECT_EQ(refresh(), 0u);
  unknown[0] = 0;
  markNativeUnknownBitsUnlocked(context, 0, 1, 0);
  EXPECT_EQ(summary, 1u);
  EXPECT_EQ(refresh(), 1u);
  EXPECT_EQ(summary, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, UnknownSummaryRefreshRereadsLastDifference) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  std::array<uint64_t, 4> unknown{};
  uint64_t summary = 0;
  // One 256-bit root whose pattern holds X/Z in bits [64, 72).
  const talos_rt_native_unknown_root_v1 roots[] = {{0, 256, 0, 1}};
  const uint64_t patterns[] = {64, 8};
  unknown[1] = 0xff;
  ASSERT_EQ(talos_rt_v1_native_state_bind_unknown_summary(
                context, reinterpret_cast<uint8_t *>(unknown.data()), 256,
                &summary, roots, 1, patterns, 1),
            TALOS_RT_OK);
  const uint64_t gate[] = {0, 1};
  auto refresh = [&] {
    return talos_rt_v1_native_state_refresh_unknown_summary(context, gate, 1,
                                                              nullptr);
  };
  const auto &mismatch = context->nativeUnknownSummary->mismatches[0];
  ASSERT_EQ(refresh(), 1u);
  EXPECT_EQ(mismatch.bit, UINT64_MAX);
  // A difference in the last chunk is remembered there.
  unknown[3] = 1;
  talos_rt_v1_native_state_mark_unknown(context, 192, 1);
  EXPECT_EQ(refresh(), 0u);
  EXPECT_EQ(mismatch.bit, 192u);
  EXPECT_EQ(refresh(), 0u);
  EXPECT_EQ(mismatch.bit, 192u);
  // Once that chunk matches again, the full scan finds a difference that
  // appeared in an earlier chunk, including a known bit inside the pattern.
  unknown[3] = 0;
  unknown[1] = 0x7f;
  EXPECT_EQ(refresh(), 0u);
  EXPECT_EQ(mismatch.bit, 64u);
  unknown[1] = 0xff;
  EXPECT_EQ(refresh(), 1u);
  EXPECT_EQ(mismatch.bit, UINT64_MAX);
  EXPECT_EQ(summary, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(SchedulerDeathTest, UnknownSummaryVerificationRejectsUnreportedWrites) {
  // With verification, a root whose summary bit is clear while its plane
  // holds X/Z (a writer that skipped its report) stops the simulation.
  ASSERT_EQ(setTestEnvironment("TALOS_RT_VERIFY_UNKNOWN_SUMMARY", "1"), 0);
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 64;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 64),
            TALOS_RT_OK);
  uint64_t value = 0, unknown = 0, summary = 0;
  auto *u = reinterpret_cast<uint8_t *>(&unknown);
  ASSERT_EQ(talos_rt_v1_native_state_sync(
                context, reinterpret_cast<uint8_t *>(&value), u, 64),
            TALOS_RT_OK);
  const talos_rt_native_unknown_root_v1 roots[] = {{0, 64, 0, 0}};
  ASSERT_EQ(talos_rt_v1_native_state_bind_unknown_summary(
                context, u, 64, &summary, roots, 1, nullptr, 0),
            TALOS_RT_OK);
  ASSERT_EQ(unsetTestEnvironment("TALOS_RT_VERIFY_UNKNOWN_SUMMARY"), 0);
  const uint64_t gate[] = {0, 1};
  auto refresh = [&] {
    return talos_rt_v1_native_state_refresh_unknown_summary(context, gate, 1,
                                                              nullptr);
  };
  EXPECT_EQ(refresh(), 1u);
  unknown = 2;
  EXPECT_DEATH(refresh(), "unknown summary reports root 0 .* matching its "
                          "pattern while it differs");
  talos_rt_v1_native_state_mark_unknown(context, 1, 1);
  EXPECT_EQ(refresh(), 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, ProceduralPathsUseNativeDestinationBeforeCanonicalBinding) {
  for (auto [oldValue, oldUnknown, nextValue] :
       {std::tuple<uint8_t, uint8_t, uint8_t>{1, 0, 0}, {0, 1, 0}, {1, 1, 1}}) {
    talos_rt_execution_descriptor_v1 execution{};
    execution.version = TALOS_RT_VERSION;
    execution.state_bit_count = 1;
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
              TALOS_RT_OK);
    // Canonical storage exists, but only the coroutine's plane is current.
    // Deliberately make the stale image equal to the requested target.
    context->stateValue[0] = nextValue;
    context->stateUnknown[0] = 0;
    context->signalDiagnosticsEnabled = true;
    uint8_t nativeValue = oldValue, nativeUnknown = oldUnknown;
    uint8_t known = 0, write = 1, inactive = 0;
    uint64_t handle = talos_rt_v1_native_state_static_handle(1);
    ASSERT_EQ(talos_rt_v1_scheduler_inertial_path_storage(
                  context, &nativeValue, &nativeUnknown, 1, handle, 1, 17, 0, 0,
                  1, 0, 4, 4, 4, &nextValue, &known, &write, &inactive,
                  &inactive, &inactive, &inactive),
              TALOS_RT_OK);
    EXPECT_EQ(nativeValue, nextValue);
    EXPECT_EQ(nativeUnknown, 0);
    EXPECT_EQ(context->signalDiagnostics.publications, 1u);
    EXPECT_TRUE(context->scheduledInertialPathNBAs.empty());
    talos_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, InertialPathsCancelReturnToPublishedFourStateValue) {
  // Compare all distinct leading targets and returns to 0, 1, X and Z. The
  // same shared calendar handles drivers, blocking paths and NBA paths.
  for (unsigned kind = 0; kind != 3; ++kind)
    for (uint8_t original = 0; original != 4; ++original)
      for (uint8_t leading = 0; leading != 4; ++leading) {
        if (leading == original)
          continue;
        for (uint8_t returnMask : {uint8_t{0}, uint8_t{1}}) {
          SCOPED_TRACE(::testing::Message()
                       << "kind=" << kind << " original=" << unsigned(original)
                       << " leading=" << unsigned(leading)
                       << " returnMask=" << unsigned(returnMask));
          talos_rt_execution_descriptor_v1 execution{};
          execution.version = TALOS_RT_VERSION;
          execution.state_bit_count = 1;
          talos_rt_context *context = nullptr;
          ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
                    TALOS_RT_OK);
          ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
                    TALOS_RT_OK);
          uint8_t oldValue = original & 1, oldUnknown = original >> 1;
          uint8_t nextValue = leading & 1, nextUnknown = leading >> 1;
          uint8_t nativeValue = oldValue, nativeUnknown = oldUnknown;
          context->stateValue[0] = oldValue;
          context->stateUnknown[0] = oldUnknown;
          context->signalDiagnosticsEnabled = true;
          uint64_t handle = talos_rt_v1_native_state_static_handle(1);
          uint8_t active = 1;
          auto schedule = [&](uint8_t value, uint8_t unknown, uint8_t mask,
                              uint32_t group, uint32_t groups) {
            if (kind == 0)
              return talos_rt_v1_scheduler_inertial_path_driver(
                  context, &nativeValue, &nativeUnknown, 1, handle, 1, 17, 0,
                  group, groups, TALOS_RT_INERTIAL_DRIVER_DEFER_RESOLUTION,
                  5, 5, 5, &value, &unknown, &active, &mask, &mask, &mask);
            return talos_rt_v1_scheduler_inertial_path_storage(
                context, &nativeValue, &nativeUnknown, 1, handle, 1, 17, 0,
                group, groups, kind == 2, 5, 5, 5, &value, &unknown, &active,
                &active, &mask, &mask, &mask);
          };
          ASSERT_EQ(schedule(nextValue, nextUnknown, 1, 0, 1), TALOS_RT_OK);
          ASSERT_EQ(context->scheduledInertialPathNBAs.size(), 1u);
          EXPECT_EQ(context->scheduledInertialPathNBAs.begin()->first.first, 5u);
          context->schedulerTime = 1;
          ASSERT_EQ(schedule(oldValue, oldUnknown, returnMask, 0, 2),
                    TALOS_RT_OK);
          ASSERT_EQ(schedule(oldValue, oldUnknown, returnMask, 1, 2),
                    TALOS_RT_OK);
          EXPECT_TRUE(context->scheduledInertialPathNBAs.empty());
          EXPECT_EQ(nativeValue, oldValue);
          EXPECT_EQ(nativeUnknown, oldUnknown);
          EXPECT_EQ(context->stateValue[0], oldValue);
          EXPECT_EQ(context->stateUnknown[0], oldUnknown);
          EXPECT_EQ(context->signalDiagnostics.publications, 0u);
          ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
          EXPECT_EQ(context->schedulerTime, 1u);
          talos_rt_v1_context_destroy(context);
        }
      }
}

TEST(Scheduler, InertialGateDriversUsePerBitTransitionDelays) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 4;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 4),
            TALOS_RT_OK);

  // Exercise 0->1, 1->0, 0->z, and 0->x in bit order. IEEE 1800-2017
  // Table 28-9 selects rise, fall, turn-off, and the least delay.
  context->stateValue[0] = 0b0010;
  context->stateUnknown[0] = 0;
  uint8_t value = 0b0101;
  uint8_t unknown = 0b1100;
  uint64_t handle = talos_rt_v1_native_state_static_handle(1);
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 17, 3, TALOS_RT_INERTIAL_DRIVER_DEFER_RESOLUTION,
                7, 11, 13, &value, &unknown),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 4u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 7u);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 11u);
  EXPECT_EQ(context->scheduledNBAs[2].dueTime, 13u);
  EXPECT_EQ(context->scheduledNBAs[3].dueTime, 7u);
  ASSERT_EQ(context->inertialDriverPending.size(), 1u);
  EXPECT_EQ(context->inertialDriverPending.begin()->second.remaining, 4u);

  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->schedulerTime, 13u);
  EXPECT_EQ(context->stateValue[0] & 0xf, value);
  EXPECT_EQ(context->stateUnknown[0] & 0xf, unknown);
  EXPECT_TRUE(context->inertialDriverPending.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialStrengthPairsMatureAtomicallyAndRejectPulses) {
  // One wire plus the complementary (strong0, highz1) and
  // (highz0, strong1) driver banks used for a conditional primitive.
  constexpr uint64_t descriptorsOffset = 0;
  std::vector<uint8_t> bytes(3 * 32, 0);
  auto write32 = [&](uint64_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto write64 = [&](uint64_t offset, uint64_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto writeDescriptor = [&](uint64_t index, uint32_t function,
                             uint32_t argument, uint64_t valueOffset,
                             uint64_t unknownOffset) {
    uint64_t record = descriptorsOffset + index * 32;
    write32(record, function);
    write32(record + 4, argument);
    write64(record + 8, valueOffset);
    write64(record + 16, unknownOffset);
    write64(record + 24, 1);
  };
  writeDescriptor(0, talos::designbytecode::kNetStateDescriptor,
                  /*four-state wire=*/1, /*net=*/0, UINT64_MAX);
  writeDescriptor(1, talos::designbytecode::kDriverStateDescriptor,
                  /*strong0, highz1=*/185, /*driver=*/8, /*net=*/0);
  writeDescriptor(2, talos::designbytecode::kDriverStateDescriptor,
                  /*highz0, strong1, high bank=*/2953, /*driver=*/16,
                  /*net=*/0);
  talos::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 3;
  image.stateBitCount = 17;
  ASSERT_TRUE(talos::designbytecode::validateImage(image));

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 17;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 8, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 16, 1),
            TALOS_RT_OK);
  context->designBytecodeImage = image;
  // Net and both driver banks start at z.
  context->stateValue[0] =
      (uint64_t{1} << 0) | (uint64_t{1} << 8) | (uint64_t{1} << 16);
  context->stateUnknown[0] = context->stateValue[0];
  std::array<uint8_t, 3> nativeValue{1, 1, 1};
  std::array<uint8_t, 3> nativeUnknown{1, 1, 1};
  ASSERT_EQ(talos_rt_v1_native_state_sync(context, nativeValue.data(),
                                            nativeUnknown.data(), 17),
            TALOS_RT_OK);
  uint64_t low = talos_rt_v1_native_state_static_handle(1);
  uint64_t high = talos_rt_v1_native_state_static_handle(2);
  uint8_t zero = 0;
  uint8_t one = 1;
  auto schedule = [&](uint8_t lowValue, uint8_t lowUnknown, uint8_t highValue,
                      uint8_t highUnknown, uint8_t transitionValue,
                      uint8_t transitionUnknown) {
    return talos_rt_v1_scheduler_inertial_driver_strength_pair(
        context, nativeValue.data(), nativeUnknown.data(), 17, low, high, 1, 41,
        3, /*rise=*/7, /*fall=*/11, /*turnoff=*/13, &lowValue, &lowUnknown,
        &highValue, &highUnknown, &transitionValue, &transitionUnknown);
  };

  // The runtime trust boundary rejects handles that do not identify the
  // adjacent complementary banks of one logical driver range.
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver_strength_pair(
                context, nativeValue.data(), nativeUnknown.data(), 17, low, low,
                1, 42, 3, 7, 11, 13, &zero, &zero, &one, &one, &zero, &zero),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_TRUE(context->scheduledNBAs.empty());

  // A logical 0 changes only the low bank, but the unchanged high-bank marker
  // still resolves the completed pair at the falling deadline.
  ASSERT_EQ(schedule(zero, zero, one, one, zero, zero), TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 11u);
  EXPECT_TRUE(context->scheduledNBAs[0].deferDriverResolution);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 11u);
  EXPECT_TRUE(context->scheduledNBAs[1].forceDriverResolution);
  context->schedulerTime = 3;
  ASSERT_EQ(schedule(zero, zero, one, one, zero, zero), TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 11u);

  // Returning to z before maturity rejects both halves of the pulse.
  context->schedulerTime = 4;
  ASSERT_EQ(schedule(one, one, one, one, one, one), TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());

  // A known 1 uses the rise delay and publishes only after both strength
  // banks have matured.
  ASSERT_EQ(schedule(one, one, one, zero, one, zero), TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 11u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->schedulerTime, 11u);
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 0u);
  EXPECT_TRUE(context->inertialDriverPending.empty());

  // L/H ranges use the x delay, the minimum of all three values (§28.6),
  // rather than the z-looking representation of either strength bank.
  ASSERT_EQ(schedule(one, one, zero, one, zero, one), TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 18u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->stateUnknown[0] & 1, 1u);

  // A real high-impedance output uses the third (turn-off) delay.
  ASSERT_EQ(schedule(one, one, one, one, one, one), TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 31u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 1u);
  EXPECT_EQ(nativeValue[0] & 1, 1u);
  EXPECT_EQ(nativeUnknown[0] & 1, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialStrengthPairsClipOnlyMatchingDynamicViews) {
  std::vector<uint8_t> bytes(3 * 32, 0);
  auto write32 = [&](uint64_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto write64 = [&](uint64_t offset, uint64_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto writeDescriptor = [&](uint64_t index, uint32_t function,
                             uint32_t argument, uint64_t valueOffset,
                             uint64_t unknownOffset) {
    uint64_t record = index * 32;
    write32(record, function);
    write32(record + 4, argument);
    write64(record + 8, valueOffset);
    write64(record + 16, unknownOffset);
    write64(record + 24, 4);
  };
  writeDescriptor(0, talos::designbytecode::kNetStateDescriptor, 1, 0,
                  UINT64_MAX);
  writeDescriptor(1, talos::designbytecode::kDriverStateDescriptor, 185, 4,
                  0);
  writeDescriptor(2, talos::designbytecode::kDriverStateDescriptor, 2953, 8,
                  0);
  talos::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.stateDescriptorCount = 3;
  image.stateBitCount = 12;
  ASSERT_TRUE(talos::designbytecode::validateImage(image));

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 12;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 4, 4),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 8, 4),
            TALOS_RT_OK);
  context->designBytecodeImage = image;
  context->stateValue[0] = 0xfff;
  context->stateUnknown[0] = 0xfff;

  uint64_t low = talos::designbytecode::encodeStaticHandle(1, -2);
  uint64_t matchingHigh = talos::designbytecode::encodeStaticHandle(2, -2);
  uint64_t mismatchedHigh = talos::designbytecode::encodeStaticHandle(2, -1);
  uint8_t zero = 0;
  uint8_t z = 0xf;
  auto schedule = [&](uint64_t high) {
    return talos_rt_v1_scheduler_inertial_driver_strength_pair(
        context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
        reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 12, low,
        high, 4, 71, 4, 7, 11, 13, &zero, &zero, &z, &z, &zero, &zero);
  };
  EXPECT_EQ(schedule(mismatchedHigh), TALOS_RT_INVALID_HANDLE);
  EXPECT_TRUE(context->scheduledNBAs.empty());

  ASSERT_EQ(schedule(matchingHigh), TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 4u);
  EXPECT_EQ(context->scheduledNBAs[0].bitOffset,
            talos_rt_v1_native_state_static_handle(1));
  EXPECT_EQ(context->scheduledNBAs[1].bitOffset,
            talos_rt_v1_native_state_static_handle(2));
  EXPECT_EQ(context->scheduledNBAs[2].bitOffset,
            talos::designbytecode::encodeStaticHandle(1, 1));
  EXPECT_EQ(context->scheduledNBAs[3].bitOffset,
            talos::designbytecode::encodeStaticHandle(2, 1));
  for (const auto &update : context->scheduledNBAs)
    EXPECT_EQ(update.dueTime, 11u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, NetDeclarationDelaysApplyAfterDriverResolution) {
  // A net descriptor carries its expanded rise/fall/turn-off delays in the
  // constant table. Its driver points back to the net's canonical bit range.
  constexpr uint64_t constantsOffset = 0;
  constexpr uint64_t descriptorsOffset = 24;
  std::vector<uint8_t> bytes(descriptorsOffset + 2 * 32, 0);
  auto write32 = [&](uint64_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto write64 = [&](uint64_t offset, uint64_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  write64(constantsOffset, 7);
  write64(constantsOffset + 8, 11);
  write64(constantsOffset + 16, 13);
  auto writeDescriptor = [&](uint64_t index, uint32_t function,
                             uint32_t argument, uint64_t valueOffset,
                             uint64_t unknownOffset, uint64_t width) {
    uint64_t record = descriptorsOffset + index * 32;
    write32(record, function);
    write32(record + 4, argument);
    write64(record + 8, valueOffset);
    write64(record + 16, unknownOffset);
    write64(record + 24, width);
  };
  writeDescriptor(0, talos::designbytecode::kNetStateDescriptor,
                  /*four-state | delayed=*/9, /*net=*/0,
                  /*delay constant offset=*/0, /*width=*/4);
  writeDescriptor(1, talos::designbytecode::kDriverStateDescriptor,
                  /*four-state, default strong strengths=*/1, /*driver=*/8,
                  /*target net=*/0, /*width=*/4);

  talos::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.constants = constantsOffset;
  image.constantSize = 24;
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 2;
  image.stateBitCount = 12;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 12;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 4),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 8, 4),
            TALOS_RT_OK);

  // Begin at Z and resolve one driver to 1, 0, X, Z. Table 28-9 selects
  // rise, fall, the least delay, and no event for the unchanged Z bit.
  context->stateValue[0] = 0b1001'0000'1111;
  context->stateUnknown[0] = 0b1100'0000'1111;
  bool changed = false;
  ASSERT_TRUE(talos::designbytecode::resolveDrivenNets(image, context, 8, 12,
                                                         changed));
  EXPECT_FALSE(changed);
  EXPECT_EQ(context->stateValue[0] & 0xf, 0xfu);
  ASSERT_EQ(context->scheduledNBAs.size(), 3u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 7u);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 11u);
  EXPECT_EQ(context->scheduledNBAs[2].dueTime, 7u);

  context->designBytecodeImage = image;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->schedulerTime, 11u);
  EXPECT_EQ(context->stateValue[0] & 0xf, 0x9u);
  EXPECT_EQ(context->stateUnknown[0] & 0xf, 0xcu);
  EXPECT_TRUE(context->inertialNetPending.empty());

  // Re-resolving the same target retains its original deadline. Returning to
  // the visible value before that deadline rejects the pulse completely.
  context->stateValue[0] &= ~uint64_t{1};
  context->stateUnknown[0] &= ~uint64_t{1};
  context->stateValue[0] |= uint64_t{1} << 8;
  changed = false;
  ASSERT_TRUE(talos::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 18u);
  context->schedulerTime = 14;
  ASSERT_TRUE(talos::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 18u);
  context->schedulerTime = 15;
  context->stateValue[0] &= ~(uint64_t{1} << 8);
  ASSERT_TRUE(talos::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialNetPending.empty());
  EXPECT_EQ(context->stateValue[0] & 1, 0u);

  // A propagation-delayed driver can mature while the scheduler is draining
  // this same queue. Its resolved net transition must be safely appended as a
  // second inertial stage rather than invalidating the active driver event.
  uint8_t one = 1;
  uint8_t known = 0;
  uint64_t driverHandle = talos_rt_v1_native_state_static_handle(2);
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 12,
                driverHandle, 1, 99, 0, TALOS_RT_INERTIAL_DRIVER_VECTOR_DELAY,
                3, 3, 3, &one, &known),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 18u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->schedulerTime, 25u);
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 0u);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());
  EXPECT_TRUE(context->inertialNetPending.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, TriregChargeDecayStartsCancelsAndUsesItsOwnDelay) {
  // IEEE 1800-2017 28.16.2 defines a trireg triple as rise, fall, and
  // charge-decay time.  The driver descriptor targets the scalar net at bit
  // zero; its own contribution is stored at bit eight.
  constexpr uint64_t descriptorsOffset = 24;
  std::vector<uint8_t> bytes(descriptorsOffset + 2 * 32, 0);
  auto write32 = [&](uint64_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto write64 = [&](uint64_t offset, uint64_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  write64(0, 7);
  write64(8, 11);
  write64(16, 13);
  auto writeDescriptor = [&](uint64_t index, uint32_t function,
                             uint32_t argument, uint64_t valueOffset,
                             uint64_t unknownOffset) {
    uint64_t record = descriptorsOffset + index * 32;
    write32(record, function);
    write32(record + 4, argument);
    write64(record + 8, valueOffset);
    write64(record + 16, unknownOffset);
    write64(record + 24, 1);
  };
  // Net resolution 9 encodes as bits 1 and 6. The delayed and four-state
  // flags occupy bits 3 and 0. Driver resolution 9 uses bits 1 and 13.
  writeDescriptor(0, talos::designbytecode::kNetStateDescriptor,
                  1u | 8u | 2u | 64u, 0, 0);
  writeDescriptor(1, talos::designbytecode::kDriverStateDescriptor,
                  1u | 2u | 8192u, 8, 0);

  talos::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.constants = 0;
  image.constantSize = 24;
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 2;
  image.stateBitCount = 9;
  ASSERT_TRUE(talos::designbytecode::validateImage(image));

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 9;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 8, 1),
            TALOS_RT_OK);
  context->designBytecodeImage = image;
  auto set = [&](uint64_t offset, bool value, bool unknown) {
    uint64_t mask = uint64_t{1} << offset;
    context->stateValue[0] =
        value ? context->stateValue[0] | mask : context->stateValue[0] & ~mask;
    context->stateUnknown[0] = unknown ? context->stateUnknown[0] | mask
                                       : context->stateUnknown[0] & ~mask;
  };
  auto state = [&] {
    return std::pair((context->stateValue[0] & 1) != 0,
                     (context->stateUnknown[0] & 1) != 0);
  };
  auto resolve = [&] {
    bool changed = false;
    ASSERT_TRUE(talos::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                           changed));
    EXPECT_FALSE(changed);
  };

  // A driven 1 observes the rise delay. Turning the driver off starts decay
  // from the turn-off time without changing the retained value.
  set(0, false, true);
  set(8, true, false);
  resolve();
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 7u);
  EXPECT_FALSE(context->scheduledNBAs.front().inertialNetChargeDecay);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(state(), std::pair(true, false));
  EXPECT_EQ(context->schedulerTime, 7u);

  set(8, true, true);
  resolve();
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 20u);
  EXPECT_TRUE(context->scheduledNBAs.front().inertialNetChargeDecay);
  EXPECT_EQ(state(), std::pair(true, false));

  // Re-resolving all-z does not restart decay. A driver returning with the
  // retained value ends decay immediately and needs no replacement event.
  context->schedulerTime = 10;
  resolve();
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 20u);
  context->schedulerTime = 12;
  set(8, true, false);
  resolve();
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialNetPending.empty());
  EXPECT_EQ(state(), std::pair(true, false));

  // With no returning driver, a stored known value decays to x.
  context->schedulerTime = 14;
  set(8, true, true);
  resolve();
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 27u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->schedulerTime, 27u);
  EXPECT_EQ(state(), std::pair(false, true));
  EXPECT_TRUE(context->inertialNetPending.empty());

  // A driven x ends pending decay and uses min(rise, fall), never the third
  // charge-decay value, as its ordinary propagation delay.
  context->schedulerTime = 30;
  set(8, true, false);
  resolve();
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->schedulerTime, 37u);
  EXPECT_EQ(state(), std::pair(true, false));
  set(8, true, true);
  resolve();
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 50u);
  context->schedulerTime = 40;
  set(8, false, true);
  resolve();
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 47u);
  EXPECT_FALSE(context->scheduledNBAs.front().inertialNetChargeDecay);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->schedulerTime, 47u);
  EXPECT_EQ(state(), std::pair(false, true));

  // The fall delay applies when a driver supplies 0, and stored 0 decays by
  // the same third value as stored 1.
  context->schedulerTime = 50;
  set(8, false, false);
  resolve();
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 61u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(state(), std::pair(false, false));
  set(8, true, true);
  resolve();
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 74u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(state(), std::pair(false, true));

  // Turning the only driver off before its rise reaches the trireg rejects
  // the pulse. Since the visible retained state is already x, decay does not
  // create a redundant x event.
  context->schedulerTime = 80;
  set(8, true, false);
  resolve();
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 87u);
  context->schedulerTime = 83;
  set(8, true, true);
  resolve();
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialNetPending.empty());
  EXPECT_EQ(state(), std::pair(false, true));
  talos_rt_v1_context_destroy(context);

  // Omitted decay is encoded separately from zero-time decay and is legal
  // only for an effectively trireg component. It retains known charge forever.
  write64(16, UINT64_MAX);
  ASSERT_TRUE(talos::designbytecode::validateImage(image));
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  set(0, true, false);
  set(8, true, true);
  bool changed = false;
  ASSERT_TRUE(talos::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 0u);
  talos_rt_v1_context_destroy(context);

  // Zero is a real decay time, not the omitted-decay sentinel. It schedules
  // the stored known value to become x in the current time slot.
  write64(16, 0);
  ASSERT_TRUE(talos::designbytecode::validateImage(image));
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 8, 1),
            TALOS_RT_OK);
  context->designBytecodeImage = image;
  set(0, true, false);
  set(8, true, true);
  changed = false;
  ASSERT_TRUE(talos::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 0u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(state(), std::pair(false, true));
  talos_rt_v1_context_destroy(context);

  // The image trust boundary rejects the same omitted third slot for an
  // ordinary wire, and rejects malformed partial/all-absent uniform triples.
  write64(16, UINT64_MAX);
  write32(descriptorsOffset + 4, 1u | 8u);
  write32(descriptorsOffset + 32 + 4, 1u);
  EXPECT_FALSE(talos::designbytecode::validateImage(image));
  write32(descriptorsOffset + 4, 1u | 8u | 2u | 64u);
  write32(descriptorsOffset + 32 + 4, 1u | 2u | 8192u);
  write64(0, UINT64_MAX);
  EXPECT_FALSE(talos::designbytecode::validateImage(image));
  write64(8, UINT64_MAX);
  EXPECT_FALSE(talos::designbytecode::validateImage(image));
}

TEST(Scheduler, DelayedTriregSharesChargeBeforeDecay) {
  constexpr uint64_t descriptorsOffset = 48;
  constexpr uint64_t connectivityOffset = descriptorsOffset + 3 * 32;
  std::vector<uint8_t> bytes(connectivityOffset + 32, 0);
  auto write32 = [&](uint64_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto write64 = [&](uint64_t offset, uint64_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  for (uint64_t offset : {uint64_t{0}, uint64_t{24}}) {
    write64(offset, 0);
    write64(offset + 8, 0);
    write64(offset + 16, 13);
  }
  auto writeDescriptor = [&](uint64_t index, uint32_t function,
                             uint32_t argument, uint64_t valueOffset,
                             uint64_t unknownOffset) {
    uint64_t record = descriptorsOffset + index * 32;
    write32(record, function);
    write32(record + 4, argument);
    write64(record + 8, valueOffset);
    write64(record + 16, unknownOffset);
    write64(record + 24, 1);
  };
  // The two delayed triregs hold small 0 and large 1 respectively. Their
  // active driver is z, so IEEE 1800-2017 28.16.2 requires the shared large 1
  // to become visible immediately, before both charges decay to x at time 13.
  writeDescriptor(0, talos::designbytecode::kNetStateDescriptor,
                  1u | 8u | 2u | 64u | 128u, 0, 0);
  writeDescriptor(1, talos::designbytecode::kNetStateDescriptor,
                  1u | 8u | 2u | 64u | 384u, 1, 24);
  writeDescriptor(2, talos::designbytecode::kDriverStateDescriptor,
                  1u | 2u | 8192u, 8, 0);
  write64(connectivityOffset, 0);
  write64(connectivityOffset + 8, 1);
  write64(connectivityOffset + 16, 1);
  bytes[connectivityOffset + 24] = 9;
  bytes[connectivityOffset + 25] = 9;

  talos::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.constants = 0;
  image.constantSize = 48;
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 3;
  image.connectivity = connectivityOffset;
  image.connectivityCount = 1;
  image.stateBitCount = 9;
  ASSERT_TRUE(talos::designbytecode::validateImage(image));

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 9;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 2),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 8, 1),
            TALOS_RT_OK);
  context->designBytecodeImage = image;
  context->stateValue[0] = (uint64_t{1} << 1) | (uint64_t{1} << 8);
  context->stateUnknown[0] = uint64_t{1} << 8;
  bool changed = false;
  ASSERT_TRUE(talos::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  EXPECT_TRUE(changed);
  EXPECT_EQ(context->stateValue[0] & 3, 3u);
  EXPECT_EQ(context->stateUnknown[0] & 3, 0u);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 13u);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 13u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->stateValue[0] & 3, 0u);
  EXPECT_EQ(context->stateUnknown[0] & 3, 3u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, DominatingNetDelayPublishesEveryCollapsedAlias) {
  constexpr uint64_t constantsOffset = 0;
  constexpr uint64_t descriptorsOffset = 48;
  constexpr uint64_t connectivityOffset = descriptorsOffset + 3 * 32;
  std::vector<uint8_t> bytes(connectivityOffset + 32, 0);
  auto write32 = [&](uint64_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto write64 = [&](uint64_t offset, uint64_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  write64(constantsOffset, 7);
  write64(constantsOffset + 8, 11);
  write64(constantsOffset + 16, 13);
  write64(constantsOffset + 24, 7);
  write64(constantsOffset + 32, 11);
  write64(constantsOffset + 40, 13);
  auto writeDescriptor = [&](uint64_t index, uint32_t function,
                             uint32_t argument, uint64_t valueOffset,
                             uint64_t unknownOffset) {
    uint64_t record = descriptorsOffset + index * 32;
    write32(record, function);
    write32(record + 4, argument);
    write64(record + 8, valueOffset);
    write64(record + 16, unknownOffset);
    write64(record + 24, 1);
  };
  writeDescriptor(0, talos::designbytecode::kNetStateDescriptor, 9, 0, 0);
  writeDescriptor(1, talos::designbytecode::kNetStateDescriptor, 9, 1, 24);
  writeDescriptor(2, talos::designbytecode::kDriverStateDescriptor, 1, 8, 0);
  write64(connectivityOffset, 0);
  write64(connectivityOffset + 8, 1);
  write64(connectivityOffset + 16, 1);

  talos::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.constants = constantsOffset;
  image.constantSize = 48;
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 3;
  image.connectivity = connectivityOffset;
  image.connectivityCount = 1;
  image.stateBitCount = 9;
  ASSERT_TRUE(talos::designbytecode::validateImage(image));

  // The bytecode trust boundary rejects aliases that do not carry one
  // normalized dominating delay.
  write64(constantsOffset + 24, 17);
  EXPECT_FALSE(talos::designbytecode::validateImage(image));
  write64(constantsOffset + 24, 7);

  // Bitwise delay metadata requires the delayed flag and uses only complete
  // all-UINT64_MAX triples as the absent-bit sentinel.
  write32(descriptorsOffset + 4, 17);
  write64(descriptorsOffset + 16, UINT64_MAX);
  EXPECT_FALSE(talos::designbytecode::validateImage(image));
  write32(descriptorsOffset + 4, 25);
  write64(descriptorsOffset + 16, 0);
  write64(constantsOffset, UINT64_MAX);
  EXPECT_FALSE(talos::designbytecode::validateImage(image));
  write32(descriptorsOffset + 4, 9);
  write64(constantsOffset, 7);

  constexpr uint64_t observerID = 77;
  talos_rt_observer_descriptor_v1 observer{observerID,
                                             nullptr,
                                             0,
                                             1,
                                             0,
                                             TALOS_RT_OBSERVER_NO_BYTECODE,
                                             collapsedAliasObserverEvaluator,
                                             0};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 9;
  execution.observers = &observer;
  execution.observer_count = 1;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 1, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 3, 8, 1),
            TALOS_RT_OK);

  struct AliasObserverWait {
    talos_rt_computed_wait_record_v1 wait{};
    talos_rt_computed_observer_v1 observer{};
    talos_rt_computed_dependency_v1 dependency{};
    talos_rt_computed_clause_v1 clause{};
    uint64_t previousValue = 0;
    uint64_t previousUnknown = 0;
  } record;
  record.wait = {TALOS_RT_VERSION,
                 TALOS_RT_SUSPEND_OBSERVER,
                 TALOS_RT_COMPUTED_WAIT_INTERLEAVED,
                 1,
                 1,
                 0,
                 1,
                 1,
                 offsetof(AliasObserverWait, observer),
                 offsetof(AliasObserverWait, dependency),
                 offsetof(AliasObserverWait, dependency),
                 offsetof(AliasObserverWait, clause),
                 offsetof(AliasObserverWait, previousValue),
                 0,
                 sizeof(AliasObserverWait),
                 0};
  record.observer = {
      observerID,
      0,
      0,
      0,
      1,
      static_cast<uint32_t>(offsetof(AliasObserverWait, previousValue)),
      0};
  uint64_t aliasHandle =
      talos_rt_canonical_state_handle_unlocked(context, 0, 1);
  ASSERT_NE(aliasHandle, UINT64_MAX);
  record.dependency = {aliasHandle, TALOS_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
  record.clause = {0, TALOS_RT_OBSERVER_CONDITION_NONE,
                   TALOS_RT_WAIT_EDGE_NEGEDGE, 0};
  ASSERT_TRUE(talos_rt_validate_computed_wait_record(&execution, &record.wait,
                                                       sizeof(record)));
  talos_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  talos_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.frame = &record;
  instance.frame_size = sizeof(record);
  instance.context = context;
  context->scheduledProcesses.emplace_back();
  ScheduledProcess &scheduled = context->scheduledProcesses.back();
  scheduled.instance = &instance;
  scheduled.token = 1;
  scheduled.waitSize = sizeof(record);
  scheduled.suspendKind = TALOS_RT_SUSPEND_OBSERVER;
  scheduled.started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  ASSERT_TRUE(talos_rt_register_computed_signal_wait_unlocked(
      context, &record.wait, scheduled.token, false,
      scheduled.signalSubscriptions, scheduled.signalLatch));

  collapsedAliasObserverSamples.clear();
  context->stateValue[0] = (uint64_t{1} << 8) | 3;
  context->stateUnknown[0] = 3;
  bool changed = false;
  ASSERT_TRUE(talos::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  EXPECT_FALSE(changed);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 7u);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 7u);
  context->designBytecodeImage = image;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->stateValue[0] & 3, 3u);
  EXPECT_EQ(context->stateUnknown[0] & 3, 0u);
  EXPECT_EQ(collapsedAliasObserverSamples, std::vector<uint32_t>({3}));
  EXPECT_TRUE(context->inertialNetPending.empty());
  talos_rt_unregister_signal_wait_unlocked(
      context, scheduled.signalSubscriptions, scheduled.token, false);
  scheduled.instance = nullptr;
  context->scheduledProcessIndices.clear();
  context->scheduledProcesses.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, BitwiseDominatingNetDelaysPreserveImmediateVectorBits) {
  constexpr uint64_t descriptorsOffset = 48;
  std::vector<uint8_t> bytes(descriptorsOffset + 2 * 32, 0);
  auto write32 = [&](uint64_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto write64 = [&](uint64_t offset, uint64_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  write64(0, 7);
  write64(8, 11);
  write64(16, 13);
  write64(24, UINT64_MAX);
  write64(32, UINT64_MAX);
  write64(40, UINT64_MAX);
  auto writeDescriptor = [&](uint64_t index, uint32_t function,
                             uint32_t argument, uint64_t valueOffset,
                             uint64_t unknownOffset) {
    uint64_t record = descriptorsOffset + index * 32;
    write32(record, function);
    write32(record + 4, argument);
    write64(record + 8, valueOffset);
    write64(record + 16, unknownOffset);
    write64(record + 24, 2);
  };
  writeDescriptor(0, talos::designbytecode::kNetStateDescriptor,
                  /*four-state | delayed | bitwise=*/25, 0, 0);
  writeDescriptor(1, talos::designbytecode::kDriverStateDescriptor, 1, 8, 0);

  talos::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.constants = 0;
  image.constantSize = 48;
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 2;
  image.stateBitCount = 10;
  ASSERT_TRUE(talos::designbytecode::validateImage(image));
  image.constantSize = 47;
  EXPECT_FALSE(talos::designbytecode::validateImage(image));
  image.constantSize = 48;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 10;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 2),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 8, 2),
            TALOS_RT_OK);
  context->stateValue[0] = (uint64_t{3} << 8) | 3;
  context->stateUnknown[0] = 3;
  bool changed = false;
  ASSERT_TRUE(talos::designbytecode::resolveDrivenNets(image, context, 8, 10,
                                                         changed));
  EXPECT_TRUE(changed);
  EXPECT_EQ(context->stateValue[0] & 3, 3u);
  EXPECT_EQ(context->stateUnknown[0] & 3, 1u);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 7u);
  context->designBytecodeImage = image;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->stateUnknown[0] & 3, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialVectorDriversRejectPulsesAndKeepStableDeadlines) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 4;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 4),
            TALOS_RT_OK);
  context->stateValue[0] = 0;
  context->stateUnknown[0] = 0;
  uint64_t handle = talos_rt_v1_native_state_static_handle(1);
  uint32_t flags = TALOS_RT_INERTIAL_DRIVER_VECTOR_DELAY |
                   TALOS_RT_INERTIAL_DRIVER_DEFER_RESOLUTION;
  uint8_t one = 1;
  uint8_t zero = 0;

  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 23, 9, flags, 10, 20, 30, &one, &zero),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);

  // Re-evaluating to the same target does not restart the propagation delay.
  context->schedulerTime = 3;
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 23, 9, flags, 10, 20, 30, &one, &zero),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);

  // Returning to the current driver value before the deadline suppresses the
  // pulse and leaves no replacement event (10.3.3 b-d).
  context->schedulerTime = 4;
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 23, 9, flags, 10, 20, 30, &zero, &zero),
            TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());
  EXPECT_EQ(context->stateValue[0] & 0xf, 0u);

  // A known nonzero-to-zero vector transition uses the falling delay.
  context->stateValue[0] = 0b0100;
  context->schedulerTime = 8;
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 23, 9, flags, 10, 20, 30, &zero, &zero),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 28u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialVectorInitialHighZToZeroUsesFallDelay) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 4;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 4),
            TALOS_RT_OK);

  // IEEE 1800-2017 10.3.3: the initial Z-to-zero vector transition uses the
  // falling delay, not the rising delay.
  context->stateValue[0] = 0xf;
  context->stateUnknown[0] = 0xf;
  uint8_t zero = 0;
  uint64_t handle = talos_rt_v1_native_state_static_handle(1);
  uint32_t flags = TALOS_RT_INERTIAL_DRIVER_VECTOR_DELAY |
                   TALOS_RT_INERTIAL_DRIVER_DEFER_RESOLUTION;
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 24, 10, flags, 5, 3, 5, &zero, &zero),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 3u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialDriversAcceptGeneratedSchedulePlanes) {
  // IEEE 1800-2017 10.3.3: a delayed continuous assignment drives its net
  // through the inertial driver. A generated schedule owns its state planes
  // and passes those, not the canonical ones, so refusing them would fail the
  // delay at run time in the native tier alone.
  AOTTestState state;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state);
  std::array<uint8_t, 1> planValue{};
  std::array<uint8_t, 1> planUnknown{};
  plan.state_value = planValue.data();
  plan.state_unknown = planUnknown.data();
  plan.state_bit_count = 4;
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 4;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 4),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);
  uint64_t handle = talos_rt_v1_native_state_static_handle(1);
  uint8_t one = 1;
  uint8_t zero = 0;
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context, plan.state_value, plan.state_unknown, 4, handle, 1, 31,
                0, 0, 10, 20, 30, &one, &zero),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);
  EXPECT_EQ(context->scheduledNBAs.front().valuePlane, plan.state_value);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialPendingTargetsIncludeTheirDestination) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  uint64_t handle = talos_rt_v1_native_state_static_handle(1);
  uint8_t value = 1;
  uint8_t unknown = 0;
  uint32_t flags = TALOS_RT_INERTIAL_DRIVER_VECTOR_DELAY |
                   TALOS_RT_INERTIAL_DRIVER_DEFER_RESOLUTION;
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                handle, 4, 29, 2, flags, 10, 20, 30, &value, &unknown),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);

  // A dynamic lvalue selection can move while retaining the same value. The
  // destination is therefore part of the pending propagation target.
  context->schedulerTime = 3;
  uint64_t upper = talos_rt_v1_native_handle_offset(handle, 4);
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                upper, 4, 29, 2, flags, 10, 20, 30, &value, &unknown),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().bitOffset, upper);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 13u);

  // Unknown dynamic selections suppress the drive and still reject the old
  // pending propagation event from this statement.
  context->schedulerTime = 4;
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                UINT64_MAX, 4, 29, 2, flags, 10, 20, 30, nullptr, nullptr),
            TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());

  // A partial overlap clips both the destination and the corresponding low
  // source bits, matching ordinary driver-store view semantics.
  context->schedulerTime = 5;
  value = 0b1101;
  uint64_t partial = talos_rt_v1_native_handle_offset(handle, -2);
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                partial, 4, 29, 2, flags, 10, 20, 30, &value, &unknown),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().bitOffset, handle);
  EXPECT_EQ(context->scheduledNBAs.front().bitWidth, 2u);
  ASSERT_EQ(context->scheduledNBAs.front().value.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().value.front() & 3, 3);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 15u);

  // A fully out-of-range selection likewise suppresses the drive and cancels
  // the partially overlapping event.
  context->schedulerTime = 6;
  uint64_t outside = talos_rt_v1_native_handle_offset(handle, 8);
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                outside, 4, 29, 2, flags, 10, 20, 30, &value, &unknown),
            TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialKnownTargetsArePlaneRepresentationIndependent) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 4;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 4),
            TALOS_RT_OK);
  uint64_t handle = talos_rt_v1_native_state_static_handle(1);
  uint8_t value = 1;
  uint8_t unknown = 0;
  uint32_t flags = TALOS_RT_INERTIAL_DRIVER_VECTOR_DELAY |
                   TALOS_RT_INERTIAL_DRIVER_DEFER_RESOLUTION;

  // Native two-state lowering omits the unknown plane.
  ASSERT_EQ(
      talos_rt_v1_scheduler_inertial_driver(
          context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
          nullptr, 4, handle, 4, 31, 7, flags, 10, 20, 30, &value, nullptr),
      TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);

  // Bytecode and four-state lowering provide an explicit all-zero unknown
  // plane. The known target is identical and must retain its first deadline.
  context->schedulerTime = 3;
  ASSERT_EQ(talos_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 31, 7, flags, 10, 20, 30, &value, &unknown),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, BytecodeInertialDriversClipAndSuppressDynamicViews) {
  // Build the small validated-image fragment needed to invoke the bytecode
  // intrinsic directly. This executes descriptor decoding rather than merely
  // checking the compiler's bytecode encoding.
  constexpr uint64_t layoutOffset = 0;
  constexpr uint64_t intrinsicOffset = 8 * 40;
  constexpr uint64_t siteOffset = intrinsicOffset + 16;
  constexpr uint64_t operandOffset = siteOffset + 16;
  std::vector<uint8_t> bytes(operandOffset + 8 * 8, 0);
  auto write32 = [&](uint64_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto write64 = [&](uint64_t offset, uint64_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto writeLayout = [&](uint32_t index, uint8_t kind, uint32_t width,
                         uint64_t offset, uint64_t size) {
    uint64_t record = layoutOffset + uint64_t{index} * 40;
    bytes[record] = kind;
    write32(record + 4, width);
    write64(record + 8, offset);
    write64(record + 16, size);
  };
  writeLayout(0, TALOS_RT_DBREG_LOGIC, 4, 0, 16);
  writeLayout(1, TALOS_RT_DBREG_HANDLE, 0, 16, 32);
  for (uint32_t index = 2; index != 8; ++index)
    writeLayout(index, TALOS_RT_DBREG_BITS, 64, 48 + uint64_t{index - 2} * 8,
                8);
  write32(intrinsicOffset, TALOS_RT_INTRINSIC_V1_INERTIAL_DRIVER);
  write32(intrinsicOffset + 4, 8);
  write32(siteOffset + 8, 8);
  for (uint32_t index = 0; index != 8; ++index)
    write32(operandOffset + uint64_t{index} * 8 + 4, index);

  talos::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.layouts = layoutOffset;
  image.layoutCount = 8;
  image.intrinsics = intrinsicOffset;
  image.intrinsicCount = 1;
  image.sites = siteOffset;
  image.siteCount = 1;
  image.operands = operandOffset;
  image.operandCount = 8;

  std::array<uint8_t, 96> frameData{};
  talos::designbytecode::Frame frame{};
  frame.function.layoutCount = 8;
  frame.function.scratchSize = frameData.size();
  frame.data = frameData.data();
  ASSERT_TRUE(
      talos::designbytecode::validIntrinsic(image, frame.function, 0));
  uint64_t value = 0b1101;
  std::memcpy(frameData.data(), &value, sizeof(value));
  uint32_t kind = TALOS_RT_DESCRIPTOR_DRIVER;
  std::memcpy(frameData.data() + 16, &kind, sizeof(kind));
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  std::memcpy(frameData.data() + 24, &root, sizeof(root));
  auto setView = [&](int64_t start, int64_t end) {
    std::memcpy(frameData.data() + 32, &start, sizeof(start));
    std::memcpy(frameData.data() + 40, &end, sizeof(end));
  };
  const std::array<uint64_t, 6> arguments{
      {10, 20, 30, 37, 5,
       TALOS_RT_INERTIAL_DRIVER_VECTOR_DELAY |
           TALOS_RT_INERTIAL_DRIVER_DEFER_RESOLUTION}};
  for (size_t index = 0; index != arguments.size(); ++index)
    std::memcpy(frameData.data() + 48 + index * 8, &arguments[index], 8);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);

  setView(0, 8);
  ASSERT_EQ(talos::designbytecode::invokeIntrinsic(image, frame, context, 0),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().bitOffset, root);
  EXPECT_EQ(context->scheduledNBAs.front().bitWidth, 4u);

  // An unknown selection cancels the pending event at this bytecode site.
  setView(talos::designbytecode::kInvalidHandleStart, 8);
  ASSERT_EQ(talos::designbytecode::invokeIntrinsic(image, frame, context, 0),
            TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());

  // A low-side overlap drops the two out-of-range source bits and schedules
  // only source bits 2..3 into destination bits 0..1.
  setView(-2, 8);
  ASSERT_EQ(talos::designbytecode::invokeIntrinsic(image, frame, context, 0),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().bitOffset, root);
  EXPECT_EQ(context->scheduledNBAs.front().bitWidth, 2u);
  ASSERT_EQ(context->scheduledNBAs.front().value.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().value.front() & 3, 3);

  // A fully out-of-range selection is suppressed and cancels that event.
  setView(8, 8);
  ASSERT_EQ(talos::designbytecode::invokeIntrinsic(image, frame, context, 0),
            TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, BytecodeInertialStrengthPairsUseLogicalTransitionDelay) {
  constexpr uint64_t layoutOffset = 0;
  constexpr uint64_t intrinsicOffset = 10 * 40;
  constexpr uint64_t siteOffset = intrinsicOffset + 16;
  constexpr uint64_t operandOffset = siteOffset + 16;
  constexpr uint64_t descriptorOffset = operandOffset + 10 * 8;
  std::vector<uint8_t> bytes(descriptorOffset + 3 * 32, 0);
  auto write32 = [&](uint64_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto write64 = [&](uint64_t offset, uint64_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  auto writeLayout = [&](uint32_t index, uint8_t kind, uint32_t width,
                         uint64_t offset, uint64_t size) {
    uint64_t record = layoutOffset + uint64_t{index} * 40;
    bytes[record] = kind;
    write32(record + 4, width);
    write64(record + 8, offset);
    write64(record + 16, size);
  };
  writeLayout(0, TALOS_RT_DBREG_LOGIC, 1, 0, 16);
  writeLayout(1, TALOS_RT_DBREG_HANDLE, 0, 16, 32);
  writeLayout(2, TALOS_RT_DBREG_LOGIC, 1, 48, 16);
  writeLayout(3, TALOS_RT_DBREG_HANDLE, 0, 64, 32);
  writeLayout(4, TALOS_RT_DBREG_LOGIC, 1, 96, 16);
  for (uint32_t index = 5; index != 10; ++index)
    writeLayout(index, TALOS_RT_DBREG_BITS, 64, 112 + uint64_t{index - 5} * 8,
                8);
  write32(intrinsicOffset,
          TALOS_RT_INTRINSIC_V1_INERTIAL_DRIVER_STRENGTH_PAIR);
  write32(intrinsicOffset + 4, 10);
  write32(siteOffset + 8, 10);
  for (uint32_t index = 0; index != 10; ++index)
    write32(operandOffset + uint64_t{index} * 8 + 4, index);
  auto writeDescriptor = [&](uint64_t index, uint32_t function,
                             uint32_t argument, uint64_t valueOffset,
                             uint64_t unknownOffset) {
    uint64_t record = descriptorOffset + index * 32;
    write32(record, function);
    write32(record + 4, argument);
    write64(record + 8, valueOffset);
    write64(record + 16, unknownOffset);
    write64(record + 24, 1);
  };
  writeDescriptor(0, talos::designbytecode::kNetStateDescriptor, 1, 0,
                  UINT64_MAX);
  writeDescriptor(1, talos::designbytecode::kDriverStateDescriptor, 185, 1,
                  0);
  writeDescriptor(2, talos::designbytecode::kDriverStateDescriptor, 2953, 2,
                  0);

  talos::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.layouts = layoutOffset;
  image.layoutCount = 10;
  image.intrinsics = intrinsicOffset;
  image.intrinsicCount = 1;
  image.sites = siteOffset;
  image.siteCount = 1;
  image.operands = operandOffset;
  image.operandCount = 10;
  image.stateDescriptors = descriptorOffset;
  image.stateDescriptorCount = 3;
  image.stateBitCount = 3;
  ASSERT_TRUE(talos::designbytecode::validateImage(image));
  std::array<uint8_t, 160> frameData{};
  talos::designbytecode::Frame frame{};
  frame.function.layoutCount = 10;
  frame.function.scratchSize = frameData.size();
  frame.data = frameData.data();
  ASSERT_TRUE(
      talos::designbytecode::validIntrinsic(image, frame.function, 0));
  // Low=z, high=x is an H strength range. Its logical transition is x, so
  // §28.6 requires min(rise, fall, turn-off), despite the z-looking low bank.
  uint64_t one = 1;
  uint64_t zero = 0;
  std::memcpy(frameData.data(), &one, 8);
  std::memcpy(frameData.data() + 8, &one, 8);
  std::memcpy(frameData.data() + 48, &zero, 8);
  std::memcpy(frameData.data() + 56, &one, 8);
  std::memcpy(frameData.data() + 96, &zero, 8);
  std::memcpy(frameData.data() + 104, &one, 8);
  uint32_t kind = TALOS_RT_DESCRIPTOR_DRIVER;
  auto writeHandle = [&](uint64_t offset, uint64_t root) {
    std::memcpy(frameData.data() + offset, &kind, 4);
    std::memcpy(frameData.data() + offset + 8, &root, 8);
    int64_t start = 0;
    int64_t end = 1;
    std::memcpy(frameData.data() + offset + 16, &start, 8);
    std::memcpy(frameData.data() + offset + 24, &end, 8);
  };
  uint64_t lowRoot = talos_rt_v1_native_state_static_handle(1);
  uint64_t highRoot = talos_rt_v1_native_state_static_handle(2);
  writeHandle(16, lowRoot);
  writeHandle(64, highRoot);
  const std::array<uint64_t, 5> arguments{{7, 11, 13, 43, 2}};
  for (size_t index = 0; index != arguments.size(); ++index)
    std::memcpy(frameData.data() + 112 + index * 8, &arguments[index], 8);

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 3;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 1, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 2, 1),
            TALOS_RT_OK);
  context->designBytecodeImage = image;
  context->stateValue[0] = 0b110;
  context->stateUnknown[0] = 0b110;
  ASSERT_EQ(talos::designbytecode::invokeIntrinsic(image, frame, context, 0),
            TALOS_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 7u);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 7u);

  int64_t invalid = talos::designbytecode::kInvalidHandleStart;
  std::memcpy(frameData.data() + 32, &invalid, 8);
  std::memcpy(frameData.data() + 80, &invalid, 8);
  ASSERT_EQ(talos::designbytecode::invokeIntrinsic(image, frame, context, 0),
            TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, DelayedStringNBAsRootValuesAndCompareByteContents) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);

  talos_rt_string_v1 first = 0;
  talos_rt_string_v1 second = 0;
  ASSERT_EQ(talos_rt_v1_string_create(lane, "delayed value", 13, &first),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_string_create(lane, "delayed value", 13, &second),
            TALOS_RT_OK);
  ASSERT_NE(first, second);
  talos_rt_string_v1 plane = 0;
  ASSERT_EQ(talos_rt_v1_scheduler_string_nba(
                context, reinterpret_cast<uint8_t *>(&plane), 64, 0, 1, first),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_string_nba(
                context, reinterpret_cast<uint8_t *>(&plane), 64, 0, 2, second),
            TALOS_RT_OK);
  first = 0;
  second = 0;
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);

  uint64_t epoch = context->schedulerEpoch;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(context->schedulerEpoch, epoch + 1);
  char scratch[8]{};
  const char *bytes = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(talos_rt_v1_string_view(plane, scratch, &bytes, &size),
            TALOS_RT_OK);
  EXPECT_EQ(std::string_view(bytes, size), "delayed value");

  plane = 0;
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, MaximumRepresentableDueTimeIsNotDropped) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  uint8_t plane = 0;
  uint8_t value = 0xa5;
  ASSERT_EQ(talos_rt_v1_scheduler_nba(context, &plane, nullptr, 8, 0, 8,
                                        UINT64_MAX, &value, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(plane, value);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTObserverPlaneAuthorityIsExplicitNotDepthDerived) {
  AOTTestState state;
  state.runHook = runObserverPlaneAuthority;
  talos_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL;

  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_install_aot(context, &plan), TALOS_RT_OK);

  uint8_t generatedPlane = UINT8_C(0xa5);
  context->stateValue[0] = UINT64_C(0x3c);
  state.authorityPlane = &generatedPlane;
  state.authorityHandle = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(state.authorityHandle, UINT64_MAX);
  ASSERT_EQ(talos_rt_v1_scheduler_run_aot(context), TALOS_RT_OK);
  EXPECT_EQ(state.ordinaryAOTLoad, generatedPlane);
  EXPECT_EQ(state.nestedObserverLoad, generatedPlane);
  EXPECT_EQ(state.canonicalObserverLoad, UINT8_C(0x3c));
  EXPECT_EQ(context->observerDepth, 0u);
  EXPECT_FALSE(context->observerForcesCanonicalPlane);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     AOTObserverReadsOwnedGeneratedPlanesWithPublicationOverlay) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 128;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 32, 32),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 3, 64, 64),
            TALOS_RT_OK);
  std::array<uint8_t, 16> value{}, unknown{}, unrelated{};
  const float shortReal = 0.5f;
  const double real = 1.5;
  std::memcpy(value.data() + 4, &shortReal, sizeof(shortReal));
  std::memcpy(value.data() + 8, &real, sizeof(real));
  context->stateValue.assign(2, 0);
  context->stateUnknown.assign(2, 0);
  talos_rt_native_schedule_plan plan{};
  plan.flags = TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL;
  plan.state_bit_count = 128;
  plan.state_value = value.data();
  plan.state_unknown = unknown.data();
  context->nativeSchedulePlan = &plan;
  context->observerForcesCanonicalPlane = true;
  const uint64_t clock = talos_rt_v1_native_state_static_handle(1);
  uint8_t published = 1, publishedUnknown = 0;
  ClockConditionPublicationView publication{clock, 1, 0, &published,
                                            &publishedUnknown};
  context->conditionPublication = &publication;
  {
    NativeAOTContextScope scope(context);
    float loadedShortReal = 0;
    double loadedReal = 0;
    const uint64_t shortRef = talos_rt_v1_native_state_static_handle(2);
    ASSERT_EQ(talos_rt_v1_native_state_load_plane(
                  context, value.data(), 128, shortRef, 32, 0, 0,
                  reinterpret_cast<uint8_t *>(&loadedShortReal)),
              TALOS_RT_OK);
    EXPECT_FLOAT_EQ(loadedShortReal, shortReal);
    // Ordinary native schedules own their planes too, without static control.
    plan.flags = 0;
    loadedShortReal = 0;
    EXPECT_EQ(talos_rt_v1_native_state_load_plane(
                  context, value.data(), 128, shortRef, 32, 0, 0,
                  reinterpret_cast<uint8_t *>(&loadedShortReal)),
              TALOS_RT_OK);
    EXPECT_FLOAT_EQ(loadedShortReal, shortReal);
    ASSERT_EQ(talos_rt_v1_native_state_load_plane(
                  context, value.data(), 128,
                  talos_rt_v1_native_state_static_handle(3), 64, 0, 0,
                  reinterpret_cast<uint8_t *>(&loadedReal)),
              TALOS_RT_OK);
    EXPECT_DOUBLE_EQ(loadedReal, real);
    uint8_t loadedClock = 0;
    ASSERT_EQ(talos_rt_v1_native_state_load_plane(
                  context, value.data(), 128, clock, 1, 0, 0, &loadedClock),
              TALOS_RT_OK);
    EXPECT_EQ(loadedClock, 1);
    EXPECT_EQ(value[0], 0);
    // A foreign buffer cannot replace the canonical observer view.
    unrelated[4] = 0xff;
    EXPECT_EQ(talos_rt_v1_native_state_load_plane(
                  context, unrelated.data(), 128, shortRef, 32, 0, 0,
                  reinterpret_cast<uint8_t *>(&loadedShortReal)),
              TALOS_RT_OK);
    EXPECT_FLOAT_EQ(loadedShortReal, 0);
    // A native-only generic activation also reads its generated globals;
    // there is no canonical import until planes or bytecode are installed.
    talos_rt_process_instance_v1 process{};
    process.tier = TALOS_RT_TIER_NATIVE;
    context->nativeSchedulePlan = nullptr;
    context->activeNativeProcess = &process;
    EXPECT_EQ(talos_rt_v1_native_state_load_plane(
                  context, value.data(), 128, shortRef, 32, 0, 0,
                  reinterpret_cast<uint8_t *>(&loadedShortReal)),
              TALOS_RT_OK);
    EXPECT_FLOAT_EQ(loadedShortReal, shortReal);
    loadedClock = 0;
    EXPECT_EQ(talos_rt_v1_native_state_load_plane(
                  context, value.data(), 128, clock, 1, 0, 0, &loadedClock),
              TALOS_RT_OK);
    EXPECT_EQ(loadedClock, 1);
    context->activeNativeProcess = nullptr;
  }
  context->nativeSchedulePlan = nullptr;
  context->conditionPublication = nullptr;
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ObserverBookkeepingWritesUpdateCanonicalAndNativePlanes) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 8),
            TALOS_RT_OK);
  context->observerForcesCanonicalPlane = true;
  uint64_t handle = talos_rt_v1_native_state_static_handle(1);
  uint8_t nativeValue = 0, nativeUnknown = 0, changed = 0;
  uint8_t value = 0xa5, unknown = 0x18;
  ASSERT_EQ(talos_rt_v1_native_state_store_plane(
                context, &nativeValue, 8, handle, 8, 0, &value, &changed),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_store_plane(
                context, &nativeUnknown, 8, handle, 8, 1, &unknown, &changed),
            TALOS_RT_OK);
  EXPECT_EQ(nativeValue, value);
  EXPECT_EQ(nativeUnknown, unknown);
  EXPECT_EQ(context->stateValue[0], value);
  EXPECT_EQ(context->stateUnknown[0], unknown);
  uint8_t read = 0;
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(context, &nativeValue, 8,
                                                  handle, 8, 0, 0, &read),
            TALOS_RT_OK);
  EXPECT_EQ(read, value);
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(context, &nativeUnknown, 8,
                                                  handle, 8, 1, 0, &read),
            TALOS_RT_OK);
  EXPECT_EQ(read, unknown);
  context->observerForcesCanonicalPlane = false;
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, PackedCanonicalReadMergesOnlyOverriddenBits) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 256;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 53, 130),
            TALOS_RT_OK);
  context->observerForcesCanonicalPlane = true;
  context->stateValue.assign(4, UINT64_C(0x96e1a5c387f0b42d));
  context->stateUnknown.assign(4, UINT64_C(0x1042088102044080));
  std::array<uint8_t, 32> global;
  global.fill(0x5a);
  uint64_t root = talos_rt_v1_native_state_static_handle(1);
  for (uint32_t masks = 0; masks != 4; ++masks) {
    context->forceMask.clear();
    context->assignMask.clear();
    if (masks & 1)
      context->forceMask.assign(4, UINT64_C(0x1111111111111111));
    if (masks & 2)
      context->assignMask.assign(4, UINT64_C(0x4848484848484848));
    for (uint32_t plane = 0; plane != 2; ++plane) {
      const auto &canonical =
          plane ? context->stateUnknown : context->stateValue;
      for (uint32_t width = 1; width <= 130; ++width) {
        for (int64_t offset : {-3, 0, 1, 63, 129, 130}) {
          for (uint32_t fallback = 0; fallback != 2; ++fallback) {
            SCOPED_TRACE(testing::Message()
                         << masks << "," << plane << "," << width << ","
                         << offset << "," << fallback);
            std::array<uint8_t, 17> actual{}, expected{};
            for (uint32_t bit = 0; bit != width; ++bit) {
              int64_t coordinate = offset + bit;
              bool v = fallback;
              if (coordinate >= 0 && coordinate < 130) {
                uint64_t source = 53 + coordinate;
                uint64_t mask = uint64_t{1} << (source % 64);
                bool overridden = (!context->forceMask.empty() &&
                                   (context->forceMask[source / 64] & mask)) ||
                                  (!context->assignMask.empty() &&
                                   (context->assignMask[source / 64] & mask));
                v = overridden ? ((global[source / 8] >> (source % 8)) & 1)
                               : (canonical[source / 64] & mask) != 0;
              }
              expected[bit / 8] |= static_cast<uint8_t>(v) << (bit % 8);
            }
            ASSERT_EQ(talos_rt_v1_native_state_load_plane(
                          context, global.data(), 256,
                          talos_rt_v1_native_handle_offset(root, offset),
                          width, plane, fallback, actual.data()),
                      TALOS_RT_OK);
            EXPECT_EQ(actual, expected);
          }
        }
      }
    }
  }
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, WidePlaneStoresPreserveBoundsMasksAndContinuousValues) {
  // Include unaligned roots, a short final word, clipped writes, and both
  // planes. Compare with bit semantics independently of the packed helpers.
  for (bool canonical : {false, true})
    for (bool continuous : {false, true})
      for (bool forced : {false, true})
        for (int64_t offset : {-3, 0, 1, 63, 129}) {
          SCOPED_TRACE(testing::Message()
                       << canonical << continuous << forced << ":" << offset);
          talos_rt_execution_descriptor_v1 execution{};
          execution.version = TALOS_RT_VERSION;
          execution.state_bit_count = 183;
          talos_rt_context *context = nullptr;
          ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
                    TALOS_RT_OK);
          ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 53,
                                                               130),
                    TALOS_RT_OK);
          context->observerForcesCanonicalPlane = canonical;
          if (forced)
            context->forceMask.assign(3, UINT64_C(0x2222222222222222));
          for (uint32_t plane = 0; plane != 2; ++plane) {
            std::array<uint8_t, 23> global;
            global.fill(0x96);
            auto expected = global;
            // IEEE 1800-2023 6.11.2/9.4.2: generic handle writes must update
            // the domain proofs used by native consumers, including clipped
            // and force-masked writes. Check their exact footprints against
            // the independent per-bit store model below.
            static std::array<bool, 183> lost, recovered;
            lost.fill(false);
            recovered.fill(false);
            talos_rt_native_schedule_plan plan{};
            plan.state_unknown = global.data();
            plan.state_bit_count = 183;
            plan.promotion_invalidate_range = [](uint64_t low, uint64_t width) {
              for (uint64_t bit = low; bit < low + width; ++bit)
                lost.at(bit) = true;
            };
            plan.promotion_recheck_range = [](uint64_t low, uint64_t width) {
              for (uint64_t bit = low; bit < low + width; ++bit)
                recovered.at(bit) = true;
            };
            context->nativeSchedulePlan = &plan;
            auto &storage = plane ? context->stateUnknown : context->stateValue;
            storage.assign(3, UINT64_C(0x9696969696969696));
            auto expectedCanonical = storage;
            std::array<uint8_t, 17> payload;
            payload.fill(0x5a);
            for (uint64_t bit = 0; bit < 130; ++bit) {
              int64_t local = offset + bit;
              if (local < 0 || local >= 130)
                continue;
              uint64_t absolute = 53 + local;
              uint64_t mask = uint64_t{1} << (absolute % 64);
              if (canonical && forced &&
                  (context->forceMask[absolute / 64] & mask))
                continue;
              bool next = (payload[bit / 8] >> (bit % 8)) & 1;
              uint8_t byteMask = uint8_t{1} << (absolute % 8);
              expected[absolute / 8] = next
                  ? expected[absolute / 8] | byteMask
                  : expected[absolute / 8] & ~byteMask;
              if (canonical) {
                auto &&limb = expectedCanonical[absolute / 64];
                limb = next ? limb | mask : limb & ~mask;
              }
            }
            uint64_t handle = talos_rt_v1_native_handle_offset(
                talos_rt_v1_native_state_static_handle(1), offset);
            auto store = continuous
                ? talos_rt_v1_native_state_store_continuous_plane
                : talos_rt_v1_native_state_store_plane;
            bool expectChange = global != expected;
            uint8_t changed = 0;
            ASSERT_EQ(store(context, global.data(), 183, handle, 130, plane,
                            payload.data(), &changed), TALOS_RT_OK);
            EXPECT_EQ(global, expected);
            EXPECT_EQ(storage, expectedCanonical);
            EXPECT_EQ(changed, expectChange);
            for (unsigned bit = 0; bit != 183; ++bit) {
              bool old = (0x96 >> (bit % 8)) & 1;
              bool next = (expected[bit / 8] >> (bit % 8)) & 1;
              EXPECT_EQ(lost[bit], plane && !old && next);
              EXPECT_EQ(recovered[bit], plane && old && !next);
            }
            lost.fill(false);
            recovered.fill(false);
            ASSERT_EQ(store(context, global.data(), 183, handle, 130, plane,
                            payload.data(), &changed), TALOS_RT_OK);
            EXPECT_EQ(changed, 0);
            EXPECT_EQ(lost, (std::array<bool, 183>{}));
            EXPECT_EQ(recovered, (std::array<bool, 183>{}));
            context->nativeSchedulePlan = nullptr;
            if (canonical && continuous) {
              const auto &retained = plane ? context->continuousUnknown
                                           : context->continuousValue;
              for (uint64_t bit = 0; bit < 130; ++bit) {
                int64_t local = offset + bit;
                if (local < 0 || local >= 130)
                  continue;
                uint64_t absolute = 53 + local;
                EXPECT_EQ((retained[absolute / 64] >> (absolute % 64)) & 1,
                          (payload[bit / 8] >> (bit % 8)) & 1);
                EXPECT_NE(context->continuousMask[absolute / 64] &
                              (uint64_t{1} << (absolute % 64)), 0u);
              }
            }
          }
          talos_rt_v1_context_destroy(context);
        }
}

TEST(Scheduler, WideNativeTransitionsFollowFourStateEdgeTable) {
  // IEEE 1800-2023 9.4.2, Table 9-2. In our planes the state encodings are
  // 0, 1, X, Z. Test subscribers in both full words and the partial tail.
  constexpr bool posedge[4][4] = {
      {false, true, true, true}, {false, false, false, false},
      {false, true, false, false}, {false, true, false, false}};
  constexpr bool negedge[4][4] = {
      {false, false, false, false}, {true, false, true, true},
      {true, false, false, false}, {true, false, false, false}};
  for (unsigned oldState = 0; oldState < 4; ++oldState)
    for (unsigned newState = 0; newState < 4; ++newState)
      for (uint32_t edge : {TALOS_RT_WAIT_EDGE_CHANGE,
                            TALOS_RT_WAIT_EDGE_POSEDGE,
                            TALOS_RT_WAIT_EDGE_NEGEDGE})
        for (unsigned watchedBit : {0, 64, 129}) {
          SCOPED_TRACE(testing::Message()
                       << oldState << "->" << newState << ":" << edge
                       << ":" << watchedBit);
          talos_rt_execution_descriptor_v1 execution{};
          execution.version = TALOS_RT_VERSION;
          execution.state_bit_count = 183;
          talos_rt_context *context = nullptr;
          ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
                    TALOS_RT_OK);
          ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 53,
                                                               130),
                    TALOS_RT_OK);
          SchedulerFixture fixture(77);
          fixture.descriptor.execution = &execution;
          uint64_t root = talos_rt_v1_native_state_static_handle(1);
          schedulerWaitKind = TALOS_RT_SUSPEND_EDGE;
          schedulerWaitEdge = edge;
          schedulerWaitHandle =
              talos_rt_v1_native_handle_offset(root, watchedBit);
          schedulerWaitWidth = 1;
          schedulerResumeCount = 0;
          ASSERT_EQ(talos_rt_v1_scheduler_add(
                        context, makeSchedulerInstance(fixture), 0),
                    TALOS_RT_OK);
          ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
          std::array<uint8_t, 17> oldV{}, oldU{}, newV{}, newU{};
          oldV.fill(oldState & 1 ? 0xff : 0);
          oldU.fill(oldState & 2 ? 0xff : 0);
          newV.fill(newState & 1 ? 0xff : 0);
          newU.fill(newState & 2 ? 0xff : 0);
          talos_rt_v1_scheduler_signal_transition(
              context, root, 130, oldV.data(), oldU.data(), newV.data(),
              newU.data());
          ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
          bool expected = edge == TALOS_RT_WAIT_EDGE_CHANGE
                              ? oldState != newState
                          : edge == TALOS_RT_WAIT_EDGE_POSEDGE
                              ? posedge[oldState][newState]
                              : negedge[oldState][newState];
          EXPECT_EQ(schedulerResumeCount, expected ? 1u : 0u);
          talos_rt_v1_context_destroy(context);
        }
}

TEST(Scheduler, AutomaticStateAllocationsAreIsolatedAndBoundsChecked) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  uint8_t global = 0x5a;
  uint8_t firstInitial = 0x11;
  uint8_t secondInitial = 0x22;
  uint64_t first = UINT64_MAX;
  uint64_t second = UINT64_MAX;
  ASSERT_EQ(talos_rt_v1_native_state_alloc(context, 8, &firstInitial, nullptr,
                                             &first),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_alloc(context, 8, &secondInitial,
                                             nullptr, &second),
            TALOS_RT_OK);
  ASSERT_NE(first, second);
  uint8_t replacement = 0xfe;
  uint8_t changed = 0;
  ASSERT_EQ(talos_rt_v1_native_state_store_plane(
                context, &global, 8, first, 8, 0, &replacement, &changed),
            TALOS_RT_OK);
  EXPECT_EQ(changed, 1u);
  uint8_t loaded = 0;
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(context, &global, 8, second,
                                                  8, 0, 0, &loaded),
            TALOS_RT_OK);
  EXPECT_EQ(loaded, secondInitial);
  loaded = 0;
  uint64_t tail = talos_rt_v1_native_handle_offset(first, 6);
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(context, &global, 8, tail, 4,
                                                  0, 1, &loaded),
            TALOS_RT_OK);
  EXPECT_EQ(loaded & 0xfu, 0xfu);
  EXPECT_EQ(global, 0x5a);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AutomaticEventBookkeepingFollowsObjectLifetime) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  uint8_t initial = 0;

  uint64_t immediateObject = UINT64_MAX;
  ASSERT_EQ(talos_rt_v1_native_state_alloc(context, 8, &initial, nullptr,
                                             &immediateObject),
            TALOS_RT_OK);
  uint64_t immediateEvent =
      talos_rt_v1_native_handle_offset(immediateObject, 3);
  ASSERT_NE(immediateEvent, UINT64_MAX);
  talos_rt_v1_scheduler_event(context, immediateEvent, 0);
  EXPECT_EQ(context->events.count(immediateEvent), 1u);
  ASSERT_EQ(talos_rt_v1_native_state_release(context, immediateObject, 0),
            TALOS_RT_OK);
  EXPECT_TRUE(context->events.empty());

  uint64_t delayedObject = UINT64_MAX;
  ASSERT_EQ(talos_rt_v1_native_state_alloc(context, 8, &initial, nullptr,
                                             &delayedObject),
            TALOS_RT_OK);
  uint64_t delayedEvent = talos_rt_v1_native_handle_offset(delayedObject, 5);
  uint64_t secondDelayedEvent =
      talos_rt_v1_native_handle_offset(delayedObject, 6);
  ASSERT_NE(delayedEvent, UINT64_MAX);
  ASSERT_NE(secondDelayedEvent, UINT64_MAX);
  talos_rt_v1_scheduler_event_after(context, delayedEvent, 1, 7);
  talos_rt_v1_scheduler_event_after(context, secondDelayedEvent, 1, 9);
  ASSERT_EQ(context->scheduledDesignEvents.size(), 2u);
  ASSERT_EQ(talos_rt_v1_native_state_release(context, delayedObject, 0),
            TALOS_RT_OK);
  EXPECT_EQ(context->nativeAutomaticStates.size(), 1u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_TRUE(context->scheduledDesignEvents.empty());
  EXPECT_TRUE(context->nativeAutomaticStates.empty());
  EXPECT_TRUE(context->events.empty());

  uint64_t waitedObject = UINT64_MAX;
  ASSERT_EQ(talos_rt_v1_native_state_alloc(context, 8, &initial, nullptr,
                                             &waitedObject),
            TALOS_RT_OK);
  uint64_t waitedEvent = talos_rt_v1_native_handle_offset(waitedObject, 7);
  ASSERT_NE(waitedEvent, UINT64_MAX);
  ASSERT_EQ(talos_rt_v1_native_state_retain(context, waitedObject),
            TALOS_RT_OK);
  SchedulerFixture waiter(14);
  schedulerWaitKind = TALOS_RT_SUSPEND_EVENT;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_NONE;
  schedulerWaitHandle = waitedEvent;
  schedulerWaitWidth = 0;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(waiter), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_TRUE(context->events.empty());
  talos_rt_v1_scheduler_event_after(context, waitedEvent, 1, 1);
  ASSERT_EQ(talos_rt_v1_native_state_release(context, waitedObject, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->events.count(waitedEvent), 1u);
  ASSERT_EQ(talos_rt_v1_native_state_release(context, waitedObject, 0),
            TALOS_RT_OK);
  EXPECT_TRUE(context->nativeAutomaticStates.empty());
  EXPECT_TRUE(context->events.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, AutomaticStateIsReleasedWithItsOwningProcess) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(8);
  fixture.descriptor.native_execute = automaticStateExecute;
  processAutomaticHandle = UINT64_MAX;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_NE(processAutomaticHandle, UINT64_MAX);
  uint8_t global = 0;
  uint8_t loaded = 0;
  EXPECT_EQ(talos_rt_v1_native_state_load_plane(
                context, &global, 8, processAutomaticHandle, 8, 0, 0, &loaded),
            TALOS_RT_INVALID_HANDLE);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, DirectExecutionOwnsAndReleasesAutomaticState) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(9);
  fixture.descriptor.native_execute = automaticStateExecute;
  processAutomaticHandle = UINT64_MAX;
  talos_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, context, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  ASSERT_NE(processAutomaticHandle, UINT64_MAX);
  uint8_t global = 0;
  uint8_t loaded = 0;
  EXPECT_EQ(talos_rt_v1_native_state_load_plane(
                context, &global, 8, processAutomaticHandle, 8, 0, 0, &loaded),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, RetainedAutomaticStateSurvivesItsOwningProcess) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(10);
  fixture.descriptor.native_execute = retainedAutomaticStateExecute;
  retainedAutomaticHandle = UINT64_MAX;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_NE(retainedAutomaticHandle, UINT64_MAX);

  uint8_t global = 0;
  uint8_t loaded = 0;
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(
                context, &global, 8, retainedAutomaticHandle, 8, 0, 0, &loaded),
            TALOS_RT_OK);
  EXPECT_EQ(loaded, 0xa5);

  ASSERT_EQ(
      talos_rt_v1_native_state_release(context, retainedAutomaticHandle, 0),
      TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_native_state_load_plane(
                context, &global, 8, retainedAutomaticHandle, 8, 0, 0, &loaded),
            TALOS_RT_INVALID_HANDLE);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, DelayedNBARetainsItsAutomaticDestination) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  SchedulerFixture fixture(11);
  fixture.descriptor.native_execute = automaticNBAExecute;
  nbaAutomaticHandle = UINT64_MAX;
  nbaDummyPlane = 0;
  talos_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, context, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  ASSERT_NE(nbaAutomaticHandle, UINT64_MAX);

  uint8_t loaded = 0;
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(context, &nbaDummyPlane, 8,
                                                  nbaAutomaticHandle, 8, 0, 0,
                                                  &loaded),
            TALOS_RT_OK);
  EXPECT_EQ(loaded, 0u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_native_state_load_plane(context, &nbaDummyPlane, 8,
                                                  nbaAutomaticHandle, 8, 0, 0,
                                                  &loaded),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, PartialOutOfBoundsHandlesPreserveInRangeBits) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 2, 4),
            TALOS_RT_OK);
  uint64_t base = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(base, UINT64_MAX);
  uint64_t lower = talos_rt_v1_native_handle_offset(base, -1);
  uint64_t upper = talos_rt_v1_native_handle_offset(base, 3);
  ASSERT_NE(lower, UINT64_MAX);
  ASSERT_NE(upper, UINT64_MAX);
  uint8_t plane = 0xc3;
  uint8_t lowerReplacement = 0x2;
  uint8_t upperReplacement = 0x1;
  uint8_t changed = 0;
  ASSERT_EQ(talos_rt_v1_native_state_store_plane(
                context, &plane, 8, lower, 2, 0, &lowerReplacement, &changed),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_store_plane(
                context, &plane, 8, upper, 2, 0, &upperReplacement, &changed),
            TALOS_RT_OK);
  EXPECT_EQ(plane, 0xe7);
  EXPECT_EQ(changed, 1u);
  uint8_t loaded = 0;
  ASSERT_EQ(talos_rt_v1_native_state_load_plane(context, &plane, 8, lower, 2,
                                                  0, 0, &loaded),
            TALOS_RT_OK);
  EXPECT_EQ(loaded & 0x3, 0x2);

  plane = 0xc3;
  ASSERT_EQ(talos_rt_v1_scheduler_nba(context, &plane, nullptr, 8, lower, 2,
                                        0, &lowerReplacement, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_nba(context, &plane, nullptr, 8, upper, 2,
                                        0, &upperReplacement, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(plane, 0xe7);
  talos_rt_v1_context_destroy(context);
}

TEST(Scheduler, OutOfBoundsTransitionsDoNotWakeAdjacentStaticObjects) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 4),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 4, 4),
            TALOS_RT_OK);
  SchedulerFixture fixture(9);
  schedulerWaitKind = TALOS_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = TALOS_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = talos_rt_v1_native_state_static_handle(2);
  schedulerWaitWidth = 4;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  uint64_t upper = talos_rt_v1_native_handle_offset(
      talos_rt_v1_native_state_static_handle(1), 3);
  uint8_t oldValue = 0;
  uint8_t newValue = 2;
  talos_rt_v1_scheduler_signal_transition(context, upper, 2, &oldValue,
                                            nullptr, &newValue, nullptr);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);

  talos_rt_v1_scheduler_signal(context,
                                 talos_rt_v1_native_state_static_handle(2), 1,
                                 TALOS_RT_SIGNAL_CHANGE);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  talos_rt_v1_context_destroy(context);
}

void initializeWait(void *frame) {
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(
      static_cast<uint8_t *>(frame) + 8);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_DELAY, 0, 0, 17, 0};
}

TEST(ProcessSpawn, BatchPreservesCapturesOrderRanksAndFailureOwnership) {
  Fixture fixture;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  uint32_t continuation = 1, rank = 17;
  talos_rt_process_spawn_plan_v1 plan{&fixture.descriptor,
                                        8,
                                        &continuation,
                                        &rank,
                                        nullptr,
                                        0,
                                        TALOS_RT_SCHEDULE_STARTUP,
                                        UINT32_MAX,
                                        9,
                                        1,
                                        0,
                                        0};
  uint64_t first = 0x123456789abcdef0, second = 0xfedcba9876543210;
  auto invalid = plan;
  invalid.schedule_flags = UINT32_MAX;
  const uint64_t captures[] = {first, second};
  const talos_rt_process_spawn_entry_v1 entries[] = {
      {&plan, 0, 0}, {&invalid, 0, 0}, {&plan, 8, 0}};
  EXPECT_EQ(talos_rt_v1_process_spawn_batch(context, entries, captures, 3),
            TALOS_RT_OK);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_INVALID_ARGUMENT);
  ASSERT_EQ(context->scheduledProcesses.size(), 2u);
  for (unsigned index = 0; index != 2; ++index) {
    const auto &process = context->scheduledProcesses[index];
    EXPECT_EQ(process.token, index + 1);
    EXPECT_EQ(process.insertionSequence, index + 1);
    EXPECT_EQ(process.scheduleRank, 9u);
    EXPECT_TRUE(process.startupProcess);
    ASSERT_EQ(process.continuationRanks.size(), 1u);
    EXPECT_EQ(process.continuationRanks[0], std::make_pair(1u, 17u));
    uint64_t capture = 0;
    std::memcpy(&capture, process.instance->frame, sizeof(capture));
    EXPECT_EQ(capture, index == 0 ? first : second);
    const auto *frame = static_cast<const uint8_t *>(process.instance->frame);
    EXPECT_TRUE(std::all_of(frame + 8, frame + 40,
                            [](uint8_t byte) { return byte == 0; }));
    EXPECT_EQ(process.instance->native_handle, nullptr);
    EXPECT_EQ(process.instance->continuation, 0u);
  }
  talos_rt_v1_context_destroy(context);
}

TEST(ProcessSpawn, RejectsInvalidCaptureAndDescriptorBeforeInsertion) {
  Fixture fixture;
  for (unsigned failure = 0; failure != 4; ++failure) {
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    talos_rt_process_spawn_plan_v1 plan{
        &fixture.descriptor, 8, nullptr, nullptr, nullptr, 0, 0,
        UINT32_MAX,          0, 0,       0,       0};
    uint64_t capture = 42;
    if (failure == 0)
      plan.capture_size = fixture.layout.frame_size + 1;
    if (failure == 1)
      plan.descriptor = nullptr;
    if (failure == 2)
      plan.options = UINT32_MAX;
    EXPECT_EQ(talos_rt_v1_process_spawn(context, &plan,
                                          failure == 3 ? nullptr : &capture),
              0u);
    EXPECT_EQ(context->schedulerStatus, TALOS_RT_INVALID_ARGUMENT);
    EXPECT_TRUE(context->scheduledProcesses.empty());
    talos_rt_v1_context_destroy(context);
  }
}

TEST(ProcessSpawn, CreationAndAOTInsertionFailuresDoNotTransferOwnership) {
  for (bool invalidDescriptor : {true, false}) {
    Fixture fixture;
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    if (invalidDescriptor)
      fixture.layout.checksum ^= 1;
    talos_rt_process_spawn_plan_v1 plan{&fixture.descriptor,
                                          0,
                                          nullptr,
                                          nullptr,
                                          nullptr,
                                          0,
                                          0,
                                          invalidDescriptor ? UINT32_MAX : 0,
                                          0,
                                          0,
                                          0,
                                          0};
    EXPECT_EQ(talos_rt_v1_process_spawn(context, &plan, nullptr), 0u);
    EXPECT_NE(context->schedulerStatus, TALOS_RT_OK);
    EXPECT_TRUE(context->scheduledProcesses.empty());
    talos_rt_v1_context_destroy(context);
  }
}

TEST(ProcessSpawn, PrimingFailureLeavesInstanceOwnedByScheduler) {
  Fixture fixture;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  nativeExecuteStatus = TALOS_RT_OUT_OF_RESOURCES;
  talos_rt_process_spawn_plan_v1 plan{&fixture.descriptor,
                                        0,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        0,
                                        TALOS_RT_SCHEDULE_DETACHED_CONTROLS,
                                        UINT32_MAX,
                                        0,
                                        0,
                                        0,
                                        TALOS_RT_SPAWN_PRIME};
  EXPECT_EQ(talos_rt_v1_process_spawn(context, &plan, nullptr), 0u);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_OUT_OF_RESOURCES);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  ASSERT_NE(context->scheduledProcesses.front().instance, nullptr);
  EXPECT_EQ(context->scheduledProcesses.front().instance->lifecycle,
            TALOS_RT_PROCESS_SUSPENDED);
  talos_rt_v1_context_destroy(context);
}

TEST(ProcessSpawn, ReturnsTaggedLogicalProcessIdentity) {
  Fixture fixture;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_process_spawn_plan_v1 plan{
      &fixture.descriptor, 0, nullptr, nullptr, nullptr, 0, 0,
      UINT32_MAX,          0, 0,       0,       0};
  uint64_t token = talos_rt_v1_process_spawn(context, &plan, nullptr);
  EXPECT_EQ(token, TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG | 1);
  uint32_t state = UINT32_MAX;
  EXPECT_EQ(talos_rt_v1_process_status(context, token, &state),
            TALOS_RT_OK);
  EXPECT_EQ(state, TALOS_RT_PROCESS_RUNNING);
  talos_rt_v1_context_destroy(context);
}

TEST(ProcessInstance, InitializesOutputRecordsOnFailure) {
  void *frame = reinterpret_cast<void *>(uintptr_t{1});
  uint64_t frameSize = 7;
  EXPECT_EQ(talos_rt_v1_process_instance_frame(nullptr, &frame, &frameSize),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(frame, nullptr);
  EXPECT_EQ(frameSize, 0u);

  talos_rt_fragment_action_v1 action{99, 99, 99, 99, 99, 99};
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                nullptr, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(action.suspend_kind, TALOS_RT_SUSPEND_NONE);
  EXPECT_EQ(action.continuation, 0u);
  EXPECT_EQ(action.flags, 0u);
  EXPECT_EQ(action.payload, 0u);
  EXPECT_EQ(action.auxiliary, 0u);
}

TEST(ProcessInstance, DefersDestroyWhileObserverPinsActivation) {
  Fixture fixture;
  nativeDestroyCount = 0;
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  instance->native_handle = instance;
  instance->observer_pin_count = 1;

  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  EXPECT_EQ(instance->observer_destroy_pending, 1u);
  EXPECT_EQ(nativeDestroyCount, 0);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  EXPECT_EQ(talos_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            TALOS_RT_OK);
  EXPECT_NE(frame, nullptr);

  instance->observer_pin_count = 0;
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 1);
}

TEST(ProcessInstance, NativeBytecodeNativeUsesStableCanonicalFrame) {
  Fixture fixture;
  nativeDestroyCount = 0;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;
  frameDuringExecute = TALOS_RT_OK;
  destroyDuringExecute = TALOS_RT_OK;
  observedContext = nullptr;
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  ASSERT_NE(instance, nullptr);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(talos_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            TALOS_RT_OK);
  EXPECT_EQ(frameSize, 40u);

  talos_rt_fragment_action_v1 action{};
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, context, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(frameDuringExecute, TALOS_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(destroyDuringExecute, TALOS_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(observedContext, context);
  EXPECT_EQ(instance->context, nullptr);
  EXPECT_EQ(instance->action, nullptr);
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_BYTECODE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(nativeDestroyCount, 1);
  void *sameFrame = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_frame(instance, &sameFrame, &frameSize),
      TALOS_RT_OK);
  EXPECT_EQ(sameFrame, frame);
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(instance->native_handle, instance);
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 2);
  talos_rt_v1_context_destroy(context);
}

TEST(ProcessInstance, BytecodeFirstCanReconstructNativeAtContinuation) {
  Fixture fixture;
  nativeDestroyCount = 0;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(talos_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            TALOS_RT_OK);
  initializeWait(frame);
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_BYTECODE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(action.flags, TALOS_RT_ACTION_FRAME_WAIT_RECORD);
  EXPECT_EQ(action.auxiliary, sizeof(talos_rt_wait_record_v1));
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(instance->native_handle, instance);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 1);
}

TEST(ProcessInstance, RejectsLayoutsTiersContinuationsAndFrameRecords) {
  Fixture fixture;
  nativeDestroyCount = 0;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;
  talos_rt_process_instance_v1 *instance = nullptr;
  uint64_t savedChecksum = fixture.layout.checksum;
  fixture.layout.checksum ^= 1;
  EXPECT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_LAYOUT_MISMATCH);
  fixture.layout.checksum = savedChecksum;
  fixture.layout.frame_size = 41;
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_LAYOUT_MISMATCH);
  fixture.layout.frame_size = 48;
  fixture.fields[1].size = 33;
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_LAYOUT_MISMATCH);
  fixture.layout.frame_size = 40;
  fixture.fields[1].size = 40;
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_LAYOUT_MISMATCH);
  fixture.fields[1].size = sizeof(talos_rt_wait_record_v1);
  fixture.fields[0].flags = TALOS_RT_FRAME_FOUR_STATE_UNKNOWN;
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_LAYOUT_MISMATCH);
  fixture.fields[0].flags = TALOS_RT_FRAME_FIELD_FLAGS_NONE;
  fixture.layout.checksum = checksum(fixture.layout);
  uint8_t savedOpcode = fixture.code[0];
  fixture.code[0] = UINT8_MAX;
  EXPECT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_INVALID_BYTECODE);
  fixture.code[0] = savedOpcode;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  talos_rt_fragment_action_v1 action{};
  instance->continuation = 99;
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_INVALID_CONTINUATION);
  instance->continuation = 0;
  fixture.descriptor.available_tiers = TALOS_RT_TIER_MASK_NATIVE;
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_BYTECODE, &action),
            TALOS_RT_TIER_UNAVAILABLE);
  fixture.descriptor.available_tiers =
      TALOS_RT_TIER_MASK_NATIVE | TALOS_RT_TIER_MASK_BYTECODE;
  emitInvalidNativeWait = true;
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_INVALID_FRAME);
  EXPECT_EQ(instance->lifecycle, TALOS_RT_PROCESS_SUSPENDED);
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = true;
  instance->continuation = 1;
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 2);
}

TEST(ProcessInstance, NativeFailureDestroysHandleBeforeRetry) {
  Fixture fixture;
  nativeDestroyCount = 0;
  nativeExecuteStatus = TALOS_RT_IO_ERROR;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;

  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  talos_rt_fragment_action_v1 action{};
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_IO_ERROR);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(instance->continuation, 0u);
  EXPECT_EQ(instance->lifecycle, TALOS_RT_PROCESS_SUSPENDED);
  EXPECT_EQ(nativeDestroyCount, 1);

  nativeExecuteStatus = TALOS_RT_OK;
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(instance->native_handle, instance);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 2);
}

TEST(ProcessInstance, MissingBytecodeContinuationPreservesNativeHandle) {
  Fixture fixture;
  nativeDestroyCount = 0;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;

  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  ASSERT_EQ(instance->continuation, 1u);
  void *nativeHandle = instance->native_handle;

  fixture.entries[1].continuation = 2;
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_BYTECODE, &action),
            TALOS_RT_TIER_UNAVAILABLE);
  EXPECT_EQ(instance->native_handle, nativeHandle);
  EXPECT_EQ(instance->tier, TALOS_RT_TIER_NATIVE);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(nativeDestroyCount, 0);

  fixture.entries[1].continuation = 1;
  uint8_t savedOpcode = fixture.code[0];
  fixture.code[0] = UINT8_MAX;
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_BYTECODE, &action),
            TALOS_RT_INVALID_BYTECODE);
  EXPECT_EQ(instance->native_handle, nativeHandle);
  EXPECT_EQ(instance->tier, TALOS_RT_TIER_NATIVE);
  EXPECT_EQ(nativeDestroyCount, 0);
  fixture.code[0] = savedOpcode;

  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 1);
}

TEST(ProcessInstance,
     NativeBytecodeNativeRoundTripPreservesCanonicalContinuationFrame) {
  Fixture fixture;
  nativeDestroyCount = 0;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;

  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  *static_cast<uint64_t *>(instance->frame) = UINT64_C(0x123456789abcdef0);
  talos_rt_fragment_action_v1 action{};

  // A generated/native activation suspends at continuation 1. Tier 3 resumes
  // that exact continuation in the shared canonical frame, then the supported
  // return continuation routes directly back to native execution.
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  ASSERT_EQ(action.kind, TALOS_RT_FRAGMENT_SUSPEND);
  ASSERT_EQ(instance->continuation, 1u);
  void *nativeHandle = instance->native_handle;
  ASSERT_NE(nativeHandle, nullptr);

  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_BYTECODE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(instance->tier, TALOS_RT_TIER_BYTECODE);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(*static_cast<uint64_t *>(instance->frame),
            UINT64_C(0x123456789abcdef0));

  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(instance->tier, TALOS_RT_TIER_NATIVE);
  EXPECT_NE(instance->native_handle, nullptr);
  EXPECT_EQ(*static_cast<uint64_t *>(instance->frame),
            UINT64_C(0x123456789abcdef0));

  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 2);
}

TEST(ProcessInstance, RejectsOverlappingCanonicalFrameFields) {
  Fixture fixture;
  talos_rt_process_instance_v1 *instance = nullptr;

  fixture.fields[1] = {
      TALOS_RT_FRAME_CAPTURE, TALOS_RT_FRAME_FIELD_FLAGS_NONE, 4, 8, 4, 0};
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_LAYOUT_MISMATCH);

  fixture.fields[1] = {
      TALOS_RT_FRAME_WAIT, TALOS_RT_FRAME_FIELD_FLAGS_NONE, 0, 32, 8, 0};
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_LAYOUT_MISMATCH);
}

TEST(ProcessInstance, RejectsMalformedWaitSemantics) {
  Fixture fixture;
  nativeDestroyCount = 0;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = true;
  emitInvalidResumeRegion = false;
  fixture.fields[1].size = 64;
  fixture.layout.frame_size = 72;
  fixture.layout.checksum = checksum(fixture.layout);
  fixture.descriptor.available_tiers = TALOS_RT_TIER_MASK_NATIVE;
  fixture.descriptor.bytecode = nullptr;

  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      TALOS_RT_OK);
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(
      static_cast<uint8_t *>(instance->frame) + 8);
  auto *entries = reinterpret_cast<talos_rt_wait_entry_v1 *>(wait + 1);
  talos_rt_fragment_action_v1 action{};
  auto expectInvalid = [&](talos_rt_suspend_kind kind, uint32_t flags,
                           uint32_t count, talos_rt_wait_edge_kind firstEdge,
                           uint32_t firstReserved = 0) {
    std::memset(wait, 0, 64);
    *wait = {TALOS_RT_VERSION, kind, flags, count, 0, 0};
    entries[0] = {17, firstEdge, firstReserved};
    entries[1] = {18, TALOS_RT_WAIT_EDGE_POSEDGE, 0};
    EXPECT_EQ(talos_rt_v1_process_instance_execute(
                  instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
              TALOS_RT_INVALID_FRAME);
  };

  expectInvalid(TALOS_RT_SUSPEND_DELAY, 1, 0, TALOS_RT_WAIT_EDGE_NONE);
  expectInvalid(TALOS_RT_SUSPEND_CHANGE, 0, 1, TALOS_RT_WAIT_EDGE_POSEDGE);
  expectInvalid(TALOS_RT_SUSPEND_EDGE, 0, 1, 4);
  expectInvalid(TALOS_RT_SUSPEND_EDGE, 0, 2, TALOS_RT_WAIT_EDGE_CHANGE, 1);
  expectInvalid(TALOS_RT_SUSPEND_EVENT, 0, 1, TALOS_RT_WAIT_EDGE_CHANGE);
  expectInvalid(TALOS_RT_SUSPEND_AWAIT, 0, 0, TALOS_RT_WAIT_EDGE_NONE);
  expectInvalid(TALOS_RT_SUSPEND_JOIN, 2, 1, TALOS_RT_WAIT_EDGE_NONE);
  expectInvalid(TALOS_RT_SUSPEND_FRONTIER, 0, 1, TALOS_RT_WAIT_EDGE_CHANGE);

  auto expectInvalidOccurrence =
      [&](uint32_t count, uint64_t payload, uint64_t conditionMask,
          talos_rt_wait_edge_kind conditionEdge, uint32_t conditionWidth) {
        std::memset(wait, 0, 64);
        *wait = {TALOS_RT_VERSION,
                 TALOS_RT_SUSPEND_EDGE,
                 TALOS_RT_WAIT_CLOCK_OCCURRENCE,
                 count,
                 payload,
                 conditionMask};
        entries[0] = {17, TALOS_RT_WAIT_EDGE_POSEDGE, 1};
        entries[1] = {18, conditionEdge, conditionWidth};
        EXPECT_EQ(talos_rt_v1_process_instance_execute(
                      instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
                  TALOS_RT_INVALID_FRAME);
      };
  expectInvalidOccurrence(1, 0, 0, TALOS_RT_WAIT_EDGE_NONE, 1);
  expectInvalidOccurrence(0, 1, 0, TALOS_RT_WAIT_EDGE_NONE, 1);
  expectInvalidOccurrence(2, 1, 2, TALOS_RT_WAIT_EDGE_NONE, 1);
  expectInvalidOccurrence(2, 1, 1, TALOS_RT_WAIT_EDGE_POSEDGE, 1);
  expectInvalidOccurrence(2, 1, 1, TALOS_RT_WAIT_EDGE_NONE, 0);

  auto expectInvalidOccurrenceEncoding = [&](uint32_t primaryEdge,
                                             uint32_t conditionEdge) {
    std::memset(wait, 0, 64);
    *wait = {TALOS_RT_VERSION,
             TALOS_RT_SUSPEND_EDGE,
             TALOS_RT_WAIT_CLOCK_OCCURRENCE,
             2,
             1,
             1};
    entries[0] = {17, primaryEdge, 1};
    entries[1] = {18, conditionEdge, 1};
    EXPECT_EQ(talos_rt_v1_process_instance_execute(
                  instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
              TALOS_RT_INVALID_FRAME);
  };
  expectInvalidOccurrenceEncoding(TALOS_RT_WAIT_EDGE_TRANSITION_MASK,
                                  TALOS_RT_WAIT_EDGE_NONE);
  expectInvalidOccurrenceEncoding(TALOS_RT_WAIT_EDGE_TRANSITION_MASK |
                                      UINT32_C(0x41),
                                  TALOS_RT_WAIT_EDGE_NONE);
  expectInvalidOccurrenceEncoding(TALOS_RT_WAIT_EDGE_TRANSITION_MASK | 1,
                                  TALOS_RT_WAIT_CONDITION_PREDICATE - 1);
  expectInvalidOccurrenceEncoding(TALOS_RT_WAIT_EDGE_TRANSITION_MASK | 1,
                                  TALOS_RT_WAIT_CONDITION_CASE_NE_ONE + 1);

  std::memset(wait, 0, 64);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  entries[0] = {TALOS_RT_STABLE_HANDLE_AUTOMATIC_TAG,
                TALOS_RT_WAIT_EDGE_CHANGE, 1};
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_INVALID_FRAME);

  std::memset(wait, 0, 64);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  entries[0] = {17, TALOS_RT_WAIT_EDGE_CHANGE, 0};
  emitInvalidResumeRegion = true;
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_INVALID_FRAME);
  emitInvalidResumeRegion = false;

  std::memset(wait, 0, 64);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE, 0, 2, 0, 0};
  entries[0] = {UINT64_MAX, TALOS_RT_WAIT_EDGE_CHANGE, 1};
  entries[1] = {18, TALOS_RT_WAIT_EDGE_POSEDGE, 1};
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);

  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 20);
}

TEST(SampledValues, CapturesCanonicalPreponedPlane) {
  talos_rt_sampled_range_v1 sampledRange{3, 0, 10};
  struct {
    talos_rt_execution_descriptor_v1 execution{};
    talos_rt_execution_extension_v1 extension{};
  } storage;
  storage.extension = {TALOS_RT_EXECUTION_EXTENSION_VERSION,
                       sizeof(talos_rt_execution_extension_v1),
                       &sampledRange,
                       1,
                       nullptr,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       0};
  auto &execution = storage.execution;
  execution.version = TALOS_RT_VERSION;
  execution.flags = TALOS_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.state_bit_count = 4096;
  execution.reserved = offsetof(decltype(storage), extension);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateValue[0] = UINT64_C(0x35a7);
  context->stateUnknown[0] = UINT64_C(0x0104);
  ASSERT_EQ(talos_rt_capture_preponed_unlocked(context), TALOS_RT_OK);
  EXPECT_EQ(context->preponedValue.size(), 1u);
  EXPECT_EQ(context->preponedUnknown.size(), 1u);

  // A later Active-region update must not affect the sampled result.
  context->stateValue[0] = 0;
  context->stateUnknown[0] = 0;
  uint64_t handle =
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_GLOBAL, 0, 3);
  uint8_t value[2] = {};
  uint8_t unknown[2] = {};
  EXPECT_EQ(talos_rt_v1_sampled_read(context, handle, 10, value, unknown),
            TALOS_RT_OK);
  EXPECT_EQ(value[0], UINT8_C(0xb4));
  EXPECT_EQ(value[1], UINT8_C(0x02));
  EXPECT_EQ(unknown[0], UINT8_C(0x20));
  EXPECT_EQ(unknown[1], UINT8_C(0x00));
  // A dynamically selected subrange reads the same frozen plane after the
  // live array has changed. An address outside the declared snapshot still
  // fails; only the explicit invalid-selection sentinel returns X.
  EXPECT_EQ(talos_rt_v1_sampled_read(context, 6, 4, value, unknown),
            TALOS_RT_OK);
  EXPECT_EQ(value[0], UINT8_C(6));
  EXPECT_EQ(unknown[0], UINT8_C(4));
  EXPECT_EQ(talos_rt_v1_sampled_read(context, 14, 4, value, unknown),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_sampled_read(context, UINT64_MAX, 10, value, unknown),
            TALOS_RT_OK);
  EXPECT_EQ(value[0], 0);
  EXPECT_EQ(value[1], 0);
  EXPECT_EQ(unknown[0], UINT8_MAX);
  EXPECT_EQ(unknown[1], UINT8_C(3));
  talos_rt_v1_context_destroy(context);
}

talos_rt_status
rootInitializerExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  instance->context->stateValue[0] = 1;
  instance->context->stateUnknown[0] = 0;
  *instance->action = {
      TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return TALOS_RT_OK;
}

TEST(SampledValues, TimeZeroSamplesDeclarationInitializers) {
  // Declaration initializers precede every time-zero event (IEEE 1800-2023
  // 6.8), so time zero samples the values the root initializer establishes,
  // not the uninitialized plane captured on entry to the time slot.
  talos_rt_sampled_range_v1 sampledRange{0, 0, 1};
  struct {
    talos_rt_execution_descriptor_v1 execution{};
    talos_rt_execution_extension_v1 extension{};
  } storage;
  storage.extension = {TALOS_RT_EXECUTION_EXTENSION_VERSION,
                       sizeof(talos_rt_execution_extension_v1),
                       &sampledRange,
                       1,
                       nullptr,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       0};
  auto &execution = storage.execution;
  execution.version = TALOS_RT_VERSION;
  execution.flags = TALOS_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.state_bit_count = 1;
  execution.reserved = offsetof(decltype(storage), extension);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->stateValue[0] = 0;
  context->stateUnknown[0] = 1;

  SchedulerFixture fixture(100);
  fixture.descriptor.native_execute = rootInitializerExecute;
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture),
                                        TALOS_RT_SCHEDULE_ROOT),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_EQ(context->schedulerTime, 0u);

  // A later time-zero update is not sampled.
  context->stateValue[0] = 0;
  uint64_t handle =
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_GLOBAL, 0, 0);
  uint8_t value = 0;
  uint8_t unknown = 0;
  ASSERT_EQ(talos_rt_v1_sampled_read(context, handle, 1, &value, &unknown),
            TALOS_RT_OK);
  EXPECT_EQ(value, 1);
  EXPECT_EQ(unknown, 0);
  talos_rt_v1_context_destroy(context);
}

std::vector<uint32_t> preponedObserverSamples;

std::vector<uint32_t> covergroupClockEventSamples;
uint8_t covergroupClockEventGlobalPlane = 0;
uint64_t covergroupClockEventSentinel = 1;
uint64_t covergroupClockEventCompletionTarget = 0;
bool covergroupClockEventReentrantTransition = false;
uint32_t covergroupClockEventSamplerReentrantKind = 0;
uint32_t covergroupClockEventRegion = UINT32_MAX;

constexpr uint64_t covergroupClockEventPrimaryObserverID = 979;
constexpr uint64_t covergroupClockEventOrObserverID = 980;

talos_rt_status
covergroupClockEventPrimaryEvaluator(talos_rt_context *context,
                                     const uint64_t *captures,
                                     uint32_t captureCount, uint64_t *value,
                                     uint64_t *unknown, uint32_t limbCount) {
  if (!context || !captures || captureCount != 1 || !value || !unknown ||
      limbCount != 1)
    return TALOS_RT_INVALID_ARGUMENT;
  bool bitValue = false;
  bool bitUnknown = false;
  if (!talos_rt_read_clock_condition_publication_bit_unlocked(
          context, captures[0], 0, bitValue, bitUnknown) &&
      !talos_rt_read_signal_bit_unlocked(context, captures[0], 0, bitValue,
                                           bitUnknown,
                                           /*useSnapshot=*/false))
    return TALOS_RT_INVALID_HANDLE;
  value[0] = bitValue;
  unknown[0] = bitUnknown;
  return TALOS_RT_OK;
}

talos_rt_status covergroupClockEventOrEvaluator(talos_rt_context *context,
                                                  const uint64_t *captures,
                                                  uint32_t captureCount,
                                                  uint64_t *value,
                                                  uint64_t *unknown,
                                                  uint32_t limbCount) {
  if (!context || !captures || captureCount != 1 || !value || !unknown ||
      limbCount != 1)
    return TALOS_RT_INVALID_ARGUMENT;
  auto read = [&](uint64_t stableID, bool &bitValue, bool &bitUnknown) {
    return talos_rt_read_clock_condition_publication_bit_unlocked(
               context, stableID, 0, bitValue, bitUnknown) ||
           talos_rt_read_signal_bit_unlocked(context, stableID, 0, bitValue,
                                               bitUnknown,
                                               /*useSnapshot=*/false);
  };
  bool lhs = false;
  bool lhsUnknown = false;
  bool rhs = false;
  bool rhsUnknown = false;
  if (!read(0, lhs, lhsUnknown) || !read(1, rhs, rhsUnknown))
    return TALOS_RT_INVALID_HANDLE;
  value[0] = lhs || rhs;
  unknown[0] = !value[0] && (lhsUnknown || rhsUnknown);
  return TALOS_RT_OK;
}

std::vector<uint64_t>
makeCovergroupClockEventPlan(const std::vector<uint64_t> &primaries,
                             const std::vector<uint64_t> &conditions,
                             const std::vector<uint32_t> &edges) {
  uint32_t clauseCount = primaries.size();
  uint32_t conditionCount = 0;
  for (uint64_t condition : conditions)
    conditionCount += condition != UINT64_MAX;
  uint32_t observerCount = clauseCount + conditionCount;
  uint32_t captureCount = observerCount;
  uint32_t dependencyCount = observerCount;
  uint64_t observersOffset = sizeof(talos_rt_computed_wait_record_v1);
  uint64_t capturesOffset =
      observersOffset +
      uint64_t{observerCount} * sizeof(talos_rt_computed_observer_v1);
  uint64_t dependenciesOffset =
      capturesOffset +
      uint64_t{captureCount} * sizeof(talos_rt_computed_capture_v1);
  uint64_t clausesOffset =
      dependenciesOffset +
      uint64_t{dependencyCount} * sizeof(talos_rt_computed_dependency_v1);
  uint64_t previousOffset =
      clausesOffset +
      uint64_t{clauseCount} * sizeof(talos_rt_computed_clause_v1);
  uint64_t totalSize = previousOffset + uint64_t{clauseCount} * 16;
  EXPECT_EQ(totalSize % sizeof(uint64_t), 0u);
  std::vector<uint64_t> bytes(totalSize / sizeof(uint64_t), 0);
  auto write = [&](uint64_t offset, const auto &record) {
    auto *storage = reinterpret_cast<uint8_t *>(bytes.data());
    std::memcpy(storage + offset, &record, sizeof(record));
  };
  talos_rt_computed_wait_record_v1 header{
      TALOS_RT_VERSION,
      TALOS_RT_SUSPEND_OBSERVER,
      TALOS_RT_COMPUTED_WAIT_INTERLEAVED,
      clauseCount,
      observerCount,
      captureCount,
      dependencyCount,
      clauseCount,
      observersOffset,
      capturesOffset,
      dependenciesOffset,
      clausesOffset,
      previousOffset,
      0,
      totalSize,
      0};
  write(0, header);
  std::vector<uint64_t> handles(primaries);
  for (uint64_t condition : conditions)
    if (condition != UINT64_MAX)
      handles.push_back(condition);
  for (uint32_t index = 0; index != observerCount; ++index) {
    talos_rt_computed_observer_v1 observer{
        covergroupClockEventPrimaryObserverID,
        index,
        1,
        index,
        1,
        index < clauseCount
            ? static_cast<uint32_t>(previousOffset + uint64_t{index} * 16)
            : UINT32_MAX,
        0};
    write(observersOffset +
              uint64_t{index} * sizeof(talos_rt_computed_observer_v1),
          observer);
    talos_rt_computed_capture_v1 capture{handles[index], 0, 0, 0};
    write(capturesOffset +
              uint64_t{index} * sizeof(talos_rt_computed_capture_v1),
          capture);
    talos_rt_computed_dependency_v1 dependency{
        handles[index], TALOS_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
    write(dependenciesOffset +
              uint64_t{index} * sizeof(talos_rt_computed_dependency_v1),
          dependency);
  }
  uint32_t conditionIndex = 0;
  for (uint32_t index = 0; index != clauseCount; ++index) {
    talos_rt_computed_clause_v1 clause{
        index,
        conditions[index] == UINT64_MAX ? TALOS_RT_OBSERVER_CONDITION_NONE
                                        : clauseCount + conditionIndex++,
        edges[index], 0};
    write(clausesOffset +
              uint64_t{index} * sizeof(talos_rt_computed_clause_v1),
          clause);
  }
  return bytes;
}

talos_rt_status covergroupClockEventEvaluator(talos_rt_context *context,
                                                const uint64_t *captures,
                                                uint32_t captureCount,
                                                uint64_t *value,
                                                uint64_t *unknown,
                                                uint32_t limbCount) {
  if (!context || !captures || captureCount != 1 || captures[0] != 42 ||
      !value || !unknown || limbCount != 1)
    return TALOS_RT_INVALID_ARGUMENT;
  uint8_t sampled = 0;
  talos_rt_status status = talos_rt_v1_native_state_load_plane(
      context, &covergroupClockEventGlobalPlane, 4, 0, 1, 0, 0, &sampled);
  if (status != TALOS_RT_OK)
    return status;
  covergroupClockEventRegion = context->activeExecRegion;
  covergroupClockEventSamples.push_back(sampled & 1);
  if (covergroupClockEventSamplerReentrantKind != 0) {
    uint32_t kind = covergroupClockEventSamplerReentrantKind;
    covergroupClockEventSamplerReentrantKind = 0;
    if (kind == 1) {
      const uint8_t oldValue = 0;
      const uint8_t newValue = 1;
      talos_rt_v1_scheduler_signal_transition(context, 1, 1, &oldValue,
                                                nullptr, &newValue, nullptr);
      context->stateValue[0] |= uint64_t{1} << 2;
      context->stateUnknown[0] &= ~(uint64_t{1} << 2);
    }
  }
  if (covergroupClockEventReentrantTransition) {
    covergroupClockEventReentrantTransition = false;
    const uint8_t oldValue = 0;
    const uint8_t newValue = 1;
    talos_rt_v1_scheduler_signal_transition(context, 0, 1, &oldValue, nullptr,
                                              &newValue, nullptr);
  }
  if (covergroupClockEventCompletionTarget != 0) {
    uint64_t target = covergroupClockEventCompletionTarget;
    covergroupClockEventCompletionTarget = 0;
    talos_rt_program_complete_unlocked(context, target, 0);
  }
  value[0] = covergroupClockEventSentinel & 1;
  unknown[0] = (covergroupClockEventSentinel >> 1) & 1;
  return TALOS_RT_OK;
}

talos_rt_status preponedObserverEvaluator(talos_rt_context *context,
                                            const uint64_t *,
                                            uint32_t captureCount,
                                            uint64_t *value, uint64_t *unknown,
                                            uint32_t limbCount) {
  if (!context || captureCount != 0 || !value || !unknown || limbCount != 1)
    return TALOS_RT_INVALID_ARGUMENT;
  uint8_t sampledValue = 0;
  uint8_t sampledUnknown = 0;
  uint64_t handle =
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_GLOBAL, 0, 0);
  talos_rt_status status = talos_rt_v1_sampled_read(
      context, handle, 1, &sampledValue, &sampledUnknown);
  if (status != TALOS_RT_OK)
    return status;
  preponedObserverSamples.push_back(
      static_cast<uint32_t>(sampledValue & 1) |
      (static_cast<uint32_t>(sampledUnknown & 1) << 1));
  value[0] = sampledValue & 1;
  unknown[0] = sampledUnknown & 1;
  return TALOS_RT_OK;
}

TEST(RuntimeInternals, ComputedWaitAcceptsDynamicEventCapture) {
  constexpr uint64_t observerID = 98;
  const talos_rt_observer_capture_abi_v1 captureABI{
      TALOS_RT_OBSERVER_CAPTURE_EVENT, 1};
  talos_rt_observer_descriptor_v1 observer{
      observerID, &captureABI,
      1,          1,
      0,          TALOS_RT_OBSERVER_NO_BYTECODE,
      nullptr,    0};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.observers = &observer;
  execution.observer_count = 1;

  struct ObserverWait {
    talos_rt_computed_wait_record_v1 wait{};
    talos_rt_computed_observer_v1 observer{};
    talos_rt_computed_capture_v1 capture{};
    talos_rt_computed_clause_v1 clause{};
    uint64_t previousValue = 0;
    uint64_t previousUnknown = 0;
  } record;
  record.wait = {TALOS_RT_VERSION,
                 TALOS_RT_SUSPEND_OBSERVER,
                 TALOS_RT_COMPUTED_WAIT_INTERLEAVED,
                 1,
                 1,
                 1,
                 0,
                 1,
                 offsetof(ObserverWait, observer),
                 offsetof(ObserverWait, capture),
                 offsetof(ObserverWait, clause),
                 offsetof(ObserverWait, clause),
                 offsetof(ObserverWait, previousValue),
                 0,
                 sizeof(ObserverWait),
                 0};
  record.observer = {
      observerID, 0,
      1,          0,
      0,          static_cast<uint32_t>(offsetof(ObserverWait, previousValue)),
      0};
  record.capture.stable_id =
      TALOS_RT_STABLE_HANDLE_DYNAMIC_EVENT_TAG | UINT64_C(17);
  record.clause = {0, TALOS_RT_OBSERVER_CONDITION_NONE,
                   TALOS_RT_WAIT_EDGE_CHANGE,
                   TALOS_RT_COMPUTED_CLAUSE_EVENT_PRIMARY};
  EXPECT_TRUE(talos_rt_validate_computed_wait_record(&execution, &record.wait,
                                                       sizeof(record)));

  // The all-ones null sentinel is not a schedulable event capture.
  record.capture.stable_id = UINT64_MAX;
  EXPECT_FALSE(talos_rt_validate_computed_wait_record(
      &execution, &record.wait, sizeof(record)));
}

TEST(RuntimeInternals,
     CovergroupClockEventsSampleEachAtomicPublicationAndShareOneCohort) {
  constexpr uint64_t observerID = 981;
  const std::array<talos_rt_observer_capture_abi_v1, 2> misplacedABI{{
      {TALOS_RT_OBSERVER_CAPTURE_STORAGE, 1},
      {TALOS_RT_OBSERVER_CAPTURE_COVERGROUP, 64},
  }};
  const talos_rt_observer_capture_abi_v1 captureABI{
      TALOS_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const talos_rt_observer_capture_abi_v1 primaryABI{
      TALOS_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const std::array<talos_rt_observer_descriptor_v1, 3> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       TALOS_RT_OBSERVER_FOUR_STATE, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID - 1, misplacedABI.data(), 2, 1, 0,
       TALOS_RT_OBSERVER_NO_BYTECODE, covergroupClockEventEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 4;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});

  talos_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  talos_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.context = context;
  instance.tier = TALOS_RT_TIER_NATIVE;
  context->scheduledProcesses.emplace_back();
  ScheduledProcess &scheduled = context->scheduledProcesses.back();
  scheduled.instance = &instance;
  scheduled.token = 1;
  scheduled.started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  context->activeNativeProcess = &instance;
  context->activeLogicalProcessToken =
      TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1};

  const std::array<uint64_t, 2> primaries{{0, 1}};
  // The first clause is gated by bit 2; the second has no iff condition.
  const std::array<uint64_t, 2> conditions{{2, UINT64_MAX}};
  const std::array<uint32_t, 2> edges{
      {TALOS_RT_WAIT_EDGE_BOTH, TALOS_RT_WAIT_EDGE_BOTH}};
  std::vector<uint64_t> eventPlan = makeCovergroupClockEventPlan(
      std::vector<uint64_t>(primaries.begin(), primaries.end()),
      std::vector<uint64_t>(conditions.begin(), conditions.end()),
      std::vector<uint32_t>(edges.begin(), edges.end()));
  auto *eventRecord =
      reinterpret_cast<talos_rt_computed_wait_record_v1 *>(eventPlan.data());
  auto *eventObservers = reinterpret_cast<talos_rt_computed_observer_v1 *>(
      reinterpret_cast<uint8_t *>(eventPlan.data()) +
      eventRecord->observers_offset);
  // Both primaries depend on two bits in the same indexed page. Registration
  // must keep only one forward bucket entry per clause, and teardown must not
  // leave a same-clause swap entry dangling.
  eventObservers[0].dependency_count = 2;
  eventObservers[1].dependency_begin = 0;
  eventObservers[1].dependency_count = 2;
  const talos_rt_computed_capture_v1 capture{42, 0, 0, 0};
  const std::array<talos_rt_computed_capture_v1, 2> misplacedCaptures{{
      {0, 0, 0, 0},
      capture,
  }};
  EXPECT_EQ(talos_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                observerID - 1, misplacedCaptures.data(),
                misplacedCaptures.size()),
            TALOS_RT_INVALID_DESIGN);
  ASSERT_EQ(talos_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                observerID, &capture, 1),
            TALOS_RT_OK);
  ASSERT_NE(context->covergroupClockEvents, nullptr);
  ASSERT_EQ(context->covergroupClockEvents->subscriptionBuckets.size(), 1u);
  EXPECT_EQ(context->covergroupClockEvents->subscriptionBuckets.begin()
                ->second.size(),
            2u);
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;

  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 0;
  covergroupClockEventSentinel = 1;
  const uint8_t bothOld = 0;
  const uint8_t bothNew = 3;
  talos_rt_v1_scheduler_signal_transition(context, 0, 2, &bothOld, nullptr,
                                            &bothNew, nullptr);
  // Both clauses matched one atomic publication, and the evaluator's direct
  // load observed that publication's new bit before canonical commit.
  ASSERT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));

  const uint8_t oneOld = 1;
  const uint8_t oneNew = 0;
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &oneOld, nullptr,
                                            &oneNew, nullptr);
  talos_rt_v1_scheduler_signal_transition(context, 1, 1, &oneOld, nullptr,
                                            &oneNew, nullptr);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1, 0}));

  context->stateValue[0] |= uint64_t{1} << 2;
  context->stateUnknown[0] &= ~(uint64_t{1} << 2);
  bool conditionValue = false;
  bool conditionUnknown = false;
  ASSERT_TRUE(talos_rt_read_signal_bit_unlocked(context, 2, 0, conditionValue,
                                                  conditionUnknown));
  ASSERT_TRUE(conditionValue);
  ASSERT_FALSE(conditionUnknown);
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &oneNew, nullptr,
                                            &oneOld, nullptr);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1, 0, 1}));

  context->coverage->instances[42].enabled = false;
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &oneOld, nullptr,
                                            &oneNew, nullptr);
  EXPECT_EQ(covergroupClockEventSamples.size(), 3u);

  talos_rt_program_complete_unlocked(
      context, TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1}, 0);
  EXPECT_EQ(context->covergroupClockEvents, nullptr);
  context->schedulerStatus = TALOS_RT_INVALID_DESIGN;
  EXPECT_EQ(talos_rt_v1_scheduler_handoff_pending(context), 1u);
  context->schedulerStatus = TALOS_RT_OK;
  talos_rt_v1_scheduler_signal_transition(context, 0, 2, &bothOld, nullptr,
                                            &bothNew, nullptr);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_OK);
  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     CovergroupClockEventUpdatesEveryOrClauseBeforeCoalescingSample) {
  constexpr uint64_t observerID = 984;
  const talos_rt_observer_capture_abi_v1 primaryABI{
      TALOS_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const talos_rt_observer_capture_abi_v1 captureABI{
      TALOS_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const std::array<talos_rt_observer_descriptor_v1, 3> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       TALOS_RT_OBSERVER_FOUR_STATE, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {covergroupClockEventOrObserverID, &primaryABI, 1, 1,
       TALOS_RT_OBSERVER_FOUR_STATE, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventOrEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 2;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});

  talos_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  talos_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.context = context;
  instance.tier = TALOS_RT_TIER_NATIVE;
  context->scheduledProcesses.emplace_back();
  context->scheduledProcesses.back().instance = &instance;
  context->scheduledProcesses.back().token = 1;
  context->scheduledProcesses.back().started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  context->activeNativeProcess = &instance;
  context->activeLogicalProcessToken =
      TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1};

  std::vector<uint64_t> eventPlan = makeCovergroupClockEventPlan(
      {0, 0}, {UINT64_MAX, UINT64_MAX},
      {TALOS_RT_WAIT_EDGE_POSEDGE, TALOS_RT_WAIT_EDGE_CHANGE});
  auto *eventRecord =
      reinterpret_cast<talos_rt_computed_wait_record_v1 *>(eventPlan.data());
  auto *eventObservers = reinterpret_cast<talos_rt_computed_observer_v1 *>(
      reinterpret_cast<uint8_t *>(eventPlan.data()) +
      eventRecord->observers_offset);
  auto *eventDependencies =
      reinterpret_cast<talos_rt_computed_dependency_v1 *>(
          reinterpret_cast<uint8_t *>(eventPlan.data()) +
          eventRecord->dependencies_offset);
  eventObservers[1].code_unit_id = covergroupClockEventOrObserverID;
  eventObservers[1].dependency_begin = 0;
  eventObservers[1].dependency_count = 2;
  eventDependencies[1].stable_id = 1;
  const talos_rt_computed_capture_v1 capture{42, 0, 0, 0};
  ASSERT_EQ(talos_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                observerID, &capture, 1),
            TALOS_RT_OK);
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;

  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 1;
  covergroupClockEventSentinel = 1;
  const uint8_t zero = 0;
  const uint8_t one = 1;
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  ASSERT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));

  // The first publication also changes (a | b), even though posedge a wins
  // sample coalescing. Updating b from zero to one leaves (a | b) stable and
  // therefore must not synthesize a second event from stale clause history.
  talos_rt_v1_scheduler_signal_transition(context, 1, 1, &zero, nullptr, &one,
                                            nullptr);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));

  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     CovergroupClockEventEvaluatesReentrantIffAtEventInstant) {
  constexpr uint64_t observerID = 985;
  const talos_rt_observer_capture_abi_v1 primaryABI{
      TALOS_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const talos_rt_observer_capture_abi_v1 captureABI{
      TALOS_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const std::array<talos_rt_observer_descriptor_v1, 2> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       TALOS_RT_OBSERVER_FOUR_STATE, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 3;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});

  talos_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  std::array<talos_rt_process_instance_v1, 2> instances{};
  for (uint64_t index = 0; index != instances.size(); ++index) {
    instances[index].descriptor = &descriptor;
    instances[index].context = context;
    instances[index].tier = TALOS_RT_TIER_NATIVE;
    context->scheduledProcesses.emplace_back();
    context->scheduledProcesses.back().instance = &instances[index];
    context->scheduledProcesses.back().token = index + 1;
    context->scheduledProcesses.back().started = true;
    context->scheduledProcessIndices.emplace(index + 1, index);
  }
  const talos_rt_computed_capture_v1 capture{42, 0, 0, 0};
  auto registerEvent = [&](uint64_t token, std::vector<uint64_t> &eventPlan) {
    context->activeNativeProcess = &instances[token - 1];
    context->activeLogicalProcessToken =
        TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | token;
    auto *record = reinterpret_cast<talos_rt_computed_wait_record_v1 *>(
        eventPlan.data());
    return talos_rt_v1_covergroup_clock_event_register(
        context, record, eventPlan.size() * sizeof(uint64_t), 0, observerID,
        &capture, 1);
  };
  std::vector<uint64_t> outerPlan = makeCovergroupClockEventPlan(
      {0}, {UINT64_MAX}, {TALOS_RT_WAIT_EDGE_POSEDGE});
  std::vector<uint64_t> nestedPlan =
      makeCovergroupClockEventPlan({1}, {2}, {TALOS_RT_WAIT_EDGE_POSEDGE});
  ASSERT_EQ(registerEvent(1, outerPlan), TALOS_RT_OK);
  ASSERT_EQ(registerEvent(2, nestedPlan), TALOS_RT_OK);
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;

  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 1;
  covergroupClockEventSentinel = 1;
  covergroupClockEventSamplerReentrantKind = 1;
  const uint8_t zero = 0;
  const uint8_t one = 1;
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_OK);
  // The outer sampler publishes signal 1 while gate bit 2 is zero, then sets
  // the gate before returning. The nested iff is evaluated synchronously at
  // publication and must not observe that later gate write.
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));
  EXPECT_NE(context->stateValue[0] & (uint64_t{1} << 2), 0u);

  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, CovergroupClockEventComposesNestedPublicationSnapshots) {
  constexpr uint64_t observerID = 986;
  const talos_rt_observer_capture_abi_v1 primaryABI{
      TALOS_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const talos_rt_observer_capture_abi_v1 captureABI{
      TALOS_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const std::array<talos_rt_observer_descriptor_v1, 2> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       TALOS_RT_OBSERVER_FOUR_STATE, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 2;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});

  talos_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  std::array<talos_rt_process_instance_v1, 2> instances{};
  for (uint64_t index = 0; index != instances.size(); ++index) {
    instances[index].descriptor = &descriptor;
    instances[index].context = context;
    instances[index].tier = TALOS_RT_TIER_NATIVE;
    context->scheduledProcesses.emplace_back();
    context->scheduledProcesses.back().instance = &instances[index];
    context->scheduledProcesses.back().token = index + 1;
    context->scheduledProcesses.back().started = true;
    context->scheduledProcessIndices.emplace(index + 1, index);
  }
  const talos_rt_computed_capture_v1 capture{42, 0, 0, 0};
  auto registerEvent = [&](uint64_t token, std::vector<uint64_t> &eventPlan) {
    context->activeNativeProcess = &instances[token - 1];
    context->activeLogicalProcessToken =
        TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | token;
    auto *record = reinterpret_cast<talos_rt_computed_wait_record_v1 *>(
        eventPlan.data());
    return talos_rt_v1_covergroup_clock_event_register(
        context, record, eventPlan.size() * sizeof(uint64_t), 0, observerID,
        &capture, 1);
  };
  std::vector<uint64_t> outerPlan = makeCovergroupClockEventPlan(
      {0}, {UINT64_MAX}, {TALOS_RT_WAIT_EDGE_POSEDGE});
  std::vector<uint64_t> nestedPlan = makeCovergroupClockEventPlan(
      {1}, {UINT64_MAX}, {TALOS_RT_WAIT_EDGE_POSEDGE});
  ASSERT_EQ(registerEvent(1, outerPlan), TALOS_RT_OK);
  ASSERT_EQ(registerEvent(2, nestedPlan), TALOS_RT_OK);
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;

  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 1;
  covergroupClockEventSentinel = 1;
  covergroupClockEventSamplerReentrantKind = 1;
  const uint8_t zero = 0;
  const uint8_t one = 1;
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_OK);
  // The outer A publication has not committed canonical storage when its
  // sampler publishes B. B's synchronous sampler must nevertheless retain
  // the enclosing A snapshot and observe A's post-transition value.
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1, 1}));

  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, CovergroupClockEventRejectsFalseSamplerSentinel) {
  constexpr uint64_t observerID = 982;
  const talos_rt_observer_capture_abi_v1 captureABI{
      TALOS_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const talos_rt_observer_capture_abi_v1 primaryABI{
      TALOS_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const std::array<talos_rt_observer_descriptor_v1, 2> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       TALOS_RT_OBSERVER_FOUR_STATE, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 4;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});
  talos_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  talos_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.context = context;
  instance.tier = TALOS_RT_TIER_NATIVE;
  context->scheduledProcesses.emplace_back();
  context->scheduledProcesses.back().instance = &instance;
  context->scheduledProcesses.back().token = 1;
  context->scheduledProcesses.back().started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  context->activeNativeProcess = &instance;
  context->activeLogicalProcessToken =
      TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1};
  const uint64_t primary = 0;
  const uint64_t condition = UINT64_MAX;
  const uint32_t edge = TALOS_RT_WAIT_EDGE_CHANGE;
  std::vector<uint64_t> eventPlan =
      makeCovergroupClockEventPlan({primary}, {condition}, {edge});
  auto *eventRecord =
      reinterpret_cast<talos_rt_computed_wait_record_v1 *>(eventPlan.data());
  const talos_rt_computed_capture_v1 capture{42, 0, 0, 0};
  ASSERT_EQ(talos_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                observerID, &capture, 1),
            TALOS_RT_OK);
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;
  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 0;
  covergroupClockEventSentinel = 0;
  const uint8_t oldValue = 0;
  const uint8_t newValue = 1;
  context->covergroupClockEventEvaluationDepth = 1;
  talos_rt_v1_scheduler_signal_transition(context, 3, 1, &oldValue, nullptr,
                                            &newValue, nullptr);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_INVALID_DESIGN);
  context->covergroupClockEventEvaluationDepth = 0;
  context->schedulerStatus = TALOS_RT_OK;
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &oldValue, nullptr,
                                            &newValue, nullptr);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_INVALID_DESIGN);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));
  covergroupClockEventSentinel = 1;
  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     CovergroupClockEventReentrantCompletionInvalidatesNoCandidates) {
  constexpr uint64_t observerID = 983;
  const talos_rt_observer_capture_abi_v1 captureABI{
      TALOS_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const talos_rt_observer_capture_abi_v1 primaryABI{
      TALOS_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const std::array<talos_rt_observer_descriptor_v1, 2> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       TALOS_RT_OBSERVER_FOUR_STATE, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 1;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});

  talos_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  std::array<talos_rt_process_instance_v1, 2> instances{};
  for (uint64_t index = 0; index != instances.size(); ++index) {
    instances[index].descriptor = &descriptor;
    instances[index].context = context;
    instances[index].tier = TALOS_RT_TIER_NATIVE;
    context->scheduledProcesses.emplace_back();
    ScheduledProcess &scheduled = context->scheduledProcesses.back();
    scheduled.instance = &instances[index];
    scheduled.token = index + 1;
    scheduled.started = true;
    context->scheduledProcessIndices.emplace(index + 1, index);
  }

  const uint64_t primary = 0;
  const uint64_t condition = UINT64_MAX;
  const uint32_t edge = TALOS_RT_WAIT_EDGE_CHANGE;
  std::vector<uint64_t> eventPlan =
      makeCovergroupClockEventPlan({primary}, {condition}, {edge});
  auto *eventRecord =
      reinterpret_cast<talos_rt_computed_wait_record_v1 *>(eventPlan.data());
  const talos_rt_computed_capture_v1 capture{42, 0, 0, 0};
  for (uint64_t index = 0; index != instances.size(); ++index) {
    context->activeNativeProcess = &instances[index];
    context->activeLogicalProcessToken =
        TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | (index + 1);
    ASSERT_EQ(talos_rt_v1_covergroup_clock_event_register(
                  context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                  observerID, &capture, 1),
              TALOS_RT_OK);
  }
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;

  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 0;
  covergroupClockEventSentinel = 1;
  covergroupClockEventCompletionTarget =
      TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{2};
  const uint8_t oldValue = 0;
  const uint8_t newValue = 1;
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &oldValue, nullptr,
                                            &newValue, nullptr);
  EXPECT_EQ(context->schedulerStatus, TALOS_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));
  ASSERT_NE(context->covergroupClockEvents, nullptr);
  EXPECT_EQ(context->covergroupClockEvents->registrations.size(), 1u);

  talos_rt_program_complete_unlocked(
      context, TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1}, 0);
  EXPECT_EQ(context->covergroupClockEvents, nullptr);
  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, CovergroupStrobeDefersAndCoalescesUntilPostponedDrain) {
  constexpr uint64_t observerID = 984;
  const talos_rt_observer_capture_abi_v1 captureABI{
      TALOS_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const talos_rt_observer_capture_abi_v1 primaryABI{
      TALOS_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const std::array<talos_rt_observer_descriptor_v1, 2> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       TALOS_RT_OBSERVER_FOUR_STATE, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, TALOS_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 2;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  FunctionalCoverageInstanceState coverageInstance;
  coverageInstance.strobe = true;
  context->coverage->instances.emplace(42, std::move(coverageInstance));

  talos_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  talos_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.context = context;
  instance.tier = TALOS_RT_TIER_NATIVE;
  context->scheduledProcesses.emplace_back();
  ScheduledProcess &scheduled = context->scheduledProcesses.back();
  scheduled.instance = &instance;
  scheduled.token = 1;
  scheduled.started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  context->activeNativeProcess = &instance;
  context->activeLogicalProcessToken =
      TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1};

  const uint64_t primary = 0;
  const uint64_t condition = UINT64_MAX;
  const uint32_t edge = TALOS_RT_WAIT_EDGE_BOTH;
  std::vector<uint64_t> eventPlan =
      makeCovergroupClockEventPlan({primary}, {condition}, {edge});
  auto *eventRecord =
      reinterpret_cast<talos_rt_computed_wait_record_v1 *>(eventPlan.data());
  const talos_rt_computed_capture_v1 capture{42, 0, 0, 0};
  EXPECT_EQ(talos_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                observerID, &capture, 1),
            TALOS_RT_INVALID_DESIGN);
  ASSERT_EQ(talos_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 1,
                observerID, &capture, 1),
            TALOS_RT_OK);
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;

  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 0;
  covergroupClockEventSentinel = 1;
  const uint8_t zero = 0;
  const uint8_t one = 1;
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &one, nullptr, &zero,
                                            nullptr);
  EXPECT_TRUE(talos_rt_covergroup_strobes_pending_unlocked(context));
  EXPECT_TRUE(covergroupClockEventSamples.empty());
  EXPECT_EQ(context->nativePeriodicTerminationRequested, 0u);
  EXPECT_EQ(talos_rt_v1_scheduler_handoff_pending(context), 1u);

  // The sample expression is evaluated only when Postponed work drains, and
  // the two qualifying publications above collapse to one sample. A signal
  // publication reentered from the callback is still in this numeric time and
  // must not queue a second automatic sample.
  covergroupClockEventGlobalPlane = 1;
  covergroupClockEventReentrantTransition = true;
  covergroupClockEventRegion = UINT32_MAX;
  context->activeHomeRegion = TALOS_RT_REGION_REACTIVE;
  context->activeExecRegion = TALOS_RT_REGION_REACTIVE;
  ASSERT_EQ(talos_rt_drain_covergroup_strobes_unlocked(context),
            TALOS_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));
  EXPECT_EQ(covergroupClockEventRegion, TALOS_RT_REGION_POSTPONED);
  EXPECT_EQ(context->activeHomeRegion, TALOS_RT_REGION_REACTIVE);
  EXPECT_EQ(context->activeExecRegion, TALOS_RT_REGION_REACTIVE);
  EXPECT_FALSE(talos_rt_covergroup_strobes_pending_unlocked(context));
  EXPECT_EQ(talos_rt_v1_scheduler_handoff_pending(context), 0u);
  ASSERT_EQ(talos_rt_drain_covergroup_strobes_unlocked(context),
            TALOS_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples.size(), 1u);

  // start()/stop() controls collection at the actual Postponed sample time.
  ++context->schedulerTime;
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  context->coverage->instances[42].enabled = false;
  ASSERT_EQ(talos_rt_drain_covergroup_strobes_unlocked(context),
            TALOS_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples.size(), 1u);

  ++context->schedulerTime;
  context->coverage->instances[42].enabled = false;
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &one, nullptr, &zero,
                                            nullptr);
  context->coverage->instances[42].enabled = true;
  covergroupClockEventGlobalPlane = 0;
  ASSERT_EQ(talos_rt_drain_covergroup_strobes_unlocked(context),
            TALOS_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1, 0}));

  // Removing an owner with a queued sample removes its pending contribution;
  // no stale Postponed work survives process teardown.
  ++context->schedulerTime;
  talos_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  EXPECT_TRUE(talos_rt_covergroup_strobes_pending_unlocked(context));
  talos_rt_program_complete_unlocked(
      context, TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1}, 0);
  EXPECT_EQ(context->covergroupClockEvents, nullptr);
  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(SampledValues, PreponedObserverRunsOncePerTimeSlot) {
  constexpr uint64_t observerID = 99;
  talos_rt_sampled_range_v1 sampledRange{0, 0, 1};
  struct {
    talos_rt_execution_descriptor_v1 execution{};
    talos_rt_execution_extension_v1 extension{};
  } storage;
  storage.extension = {TALOS_RT_EXECUTION_EXTENSION_VERSION,
                       sizeof(talos_rt_execution_extension_v1),
                       &sampledRange,
                       1,
                       nullptr,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       0};
  talos_rt_observer_descriptor_v1 observer{observerID,
                                             nullptr,
                                             0,
                                             1,
                                             0,
                                             TALOS_RT_OBSERVER_NO_BYTECODE,
                                             preponedObserverEvaluator,
                                             0};
  auto &execution = storage.execution;
  execution.version = TALOS_RT_VERSION;
  execution.flags = TALOS_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.state_bit_count = 1;
  execution.observers = &observer;
  execution.observer_count = 1;
  execution.reserved = offsetof(decltype(storage), extension);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);

  struct ObserverWait {
    talos_rt_computed_wait_record_v1 wait{};
    talos_rt_computed_observer_v1 observer{};
    talos_rt_computed_dependency_v1 dependency{};
    talos_rt_computed_clause_v1 clause{};
    uint64_t previousValue = 0;
    uint64_t previousUnknown = 0;
  } record;
  record.wait = {TALOS_RT_VERSION,
                 TALOS_RT_SUSPEND_OBSERVER,
                 TALOS_RT_COMPUTED_WAIT_INTERLEAVED,
                 1,
                 1,
                 0,
                 1,
                 1,
                 offsetof(ObserverWait, observer),
                 offsetof(ObserverWait, dependency),
                 offsetof(ObserverWait, dependency),
                 offsetof(ObserverWait, clause),
                 offsetof(ObserverWait, previousValue),
                 0,
                 sizeof(ObserverWait),
                 0};
  record.observer = {
      observerID, 0,
      0,          0,
      1,          static_cast<uint32_t>(offsetof(ObserverWait, previousValue)),
      0};
  record.dependency = {TALOS_RT_STABLE_HANDLE_PREPONED_EVENT,
                       TALOS_RT_OBSERVER_DEPENDENCY_EVENT, 1};
  record.clause = {0, TALOS_RT_OBSERVER_CONDITION_NONE,
                   TALOS_RT_WAIT_EDGE_NEGEDGE, 0};
  ASSERT_TRUE(talos_rt_validate_computed_wait_record(&execution, &record.wait,
                                                       sizeof(record)));

  talos_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  talos_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.frame = &record;
  instance.frame_size = sizeof(record);
  instance.context = context;
  context->scheduledProcesses.emplace_back();
  ScheduledProcess &scheduled = context->scheduledProcesses.back();
  scheduled.instance = &instance;
  scheduled.token = 1;
  scheduled.waitSize = sizeof(record);
  scheduled.suspendKind = TALOS_RT_SUSPEND_OBSERVER;
  scheduled.started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  ASSERT_TRUE(talos_rt_register_computed_signal_wait_unlocked(
      context, &record.wait, scheduled.token, false,
      scheduled.signalSubscriptions, scheduled.signalLatch));
  ASSERT_TRUE(context->preponedObserverPresent);

  preponedObserverSamples.clear();
  context->stateValue[0] = 0;
  context->stateUnknown[0] = 0;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_EQ(preponedObserverSamples, std::vector<uint32_t>({0}));
  ASSERT_EQ(context->events[TALOS_RT_STABLE_HANDLE_PREPONED_EVENT].generation,
            1u);

  // A late mutation in the same time slot neither republishes the private
  // event nor changes the already captured sampled plane.
  context->stateValue[0] = 1;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(preponedObserverSamples, std::vector<uint32_t>({0}));
  uint8_t sampledValue = 0;
  uint8_t sampledUnknown = 0;
  uint64_t handle =
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_GLOBAL, 0, 0);
  ASSERT_EQ(talos_rt_v1_sampled_read(context, handle, 1, &sampledValue,
                                       &sampledUnknown),
            TALOS_RT_OK);
  EXPECT_EQ(sampledValue, 0);
  EXPECT_EQ(sampledUnknown, 0);

  // The next time slot captures the mutation and notifies exactly once.
  context->schedulerTime = 1;
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(preponedObserverSamples, std::vector<uint32_t>({0, 1}));
  EXPECT_EQ(context->events[TALOS_RT_STABLE_HANDLE_PREPONED_EVENT].generation,
            2u);
  EXPECT_EQ(context->events[TALOS_RT_STABLE_HANDLE_PREPONED_EVENT]
                .lastTriggeredTime,
            1u);

  talos_rt_unregister_signal_wait_unlocked(
      context, scheduled.signalSubscriptions, scheduled.token, false);
  scheduled.instance = nullptr;
  context->scheduledProcessIndices.clear();
  context->scheduledProcesses.clear();
  talos_rt_v1_context_destroy(context);
}

TEST(SampledValues, ValidatesExecutionExtension) {
  talos_rt_sampled_range_v1 sampledRange{0, 0, 1};
  struct {
    talos_rt_execution_descriptor_v1 execution{};
    talos_rt_execution_extension_v1 extension{};
  } storage;
  storage.extension = {TALOS_RT_EXECUTION_EXTENSION_VERSION,
                       sizeof(talos_rt_execution_extension_v1),
                       &sampledRange,
                       1,
                       nullptr,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       0};
  auto &execution = storage.execution;
  execution.version = TALOS_RT_VERSION;
  execution.flags = TALOS_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.state_bit_count = 8;
  talos_rt_context *context = nullptr;
  EXPECT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_INVALID_DESIGN);

  execution.reserved = offsetof(decltype(storage), extension);
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
  context = nullptr;

  execution.flags = 0;
  EXPECT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_INVALID_DESIGN);
  execution.flags = TALOS_RT_EXECUTION_PREPONED_SNAPSHOT;
  ++storage.extension.version;
  EXPECT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_INVALID_DESIGN);
}

TEST(SampledValues, CapturesBoundNativePlanesWithoutWholeStateCopies) {
  talos_rt_sampled_range_v1 sampledRange{8, 0, 4};
  struct {
    talos_rt_execution_descriptor_v1 execution{};
    talos_rt_execution_extension_v1 extension{};
  } storage;
  storage.extension = {TALOS_RT_EXECUTION_EXTENSION_VERSION,
                       sizeof(talos_rt_execution_extension_v1),
                       &sampledRange,
                       1,
                       nullptr,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       0};
  auto &execution = storage.execution;
  execution.version = TALOS_RT_VERSION;
  execution.flags = TALOS_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.reserved = offsetof(decltype(storage), extension);
  execution.state_bit_count = 32;

  talos_rt_context *first = nullptr;
  talos_rt_context *second = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &first),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &second),
            TALOS_RT_OK);
  uint8_t firstValue[4] = {};
  uint8_t firstUnknown[4] = {};
  uint8_t secondValue[4] = {};
  uint8_t secondUnknown[4] = {};
  ASSERT_EQ(
      talos_rt_v1_native_state_sync(first, firstValue, firstUnknown, 32),
      TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_v1_native_state_sync(second, secondValue, secondUnknown, 32),
      TALOS_RT_OK);

  // Publication after binding is observed directly at the next Preponed
  // capture; sync does not need to copy the complete planes again.
  firstValue[1] = UINT8_C(0x0a);
  firstUnknown[1] = UINT8_C(0x04);
  secondValue[1] = UINT8_C(0x03);
  ASSERT_EQ(talos_rt_capture_preponed_unlocked(first), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_capture_preponed_unlocked(second), TALOS_RT_OK);

  uint64_t handle =
      talos_rt_stable_handle_encode(TALOS_RT_STABLE_HANDLE_GLOBAL, 0, 8);
  uint8_t value = 0, unknown = 0;
  EXPECT_EQ(talos_rt_v1_sampled_read(first, handle, 4, &value, &unknown),
            TALOS_RT_OK);
  EXPECT_EQ(value, UINT8_C(0x0a));
  EXPECT_EQ(unknown, UINT8_C(0x04));
  EXPECT_EQ(talos_rt_v1_sampled_read(second, handle, 4, &value, &unknown),
            TALOS_RT_OK);
  EXPECT_EQ(value, UINT8_C(0x03));
  EXPECT_EQ(unknown, UINT8_C(0x00));

  talos_rt_v1_context_destroy(first);
  talos_rt_v1_context_destroy(second);
}

TEST(SampledValues, SharesCompilerPlannedAlternateClockHistory) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  constexpr uint64_t site = UINT64_C(0x12345678);
  uint8_t value = 0, unknown = 0, resultValue = 0, resultUnknown = 0;

  // Missing ages have the sampled-value default. A disabled clock tick does
  // not advance the shared ring.
  ASSERT_EQ(talos_rt_v1_clocked_sample_read(context, site, 4, 2, 0, 1,
                                              &resultValue, &resultUnknown),
            TALOS_RT_OK);
  EXPECT_EQ(resultValue, UINT8_C(0x00));
  EXPECT_EQ(resultUnknown, UINT8_C(0x0f));
  value = UINT8_C(0x03);
  ASSERT_EQ(talos_rt_v1_clocked_sample_update(context, site, 4, 2, 1, 0,
                                                &value, &unknown),
            TALOS_RT_OK);

  const uint8_t samples[] = {UINT8_C(0x0a), UINT8_C(0x04), UINT8_C(0x0c)};
  const uint8_t unknowns[] = {UINT8_C(0x00), UINT8_C(0x01), UINT8_C(0x00)};
  for (size_t index = 0; index != 3; ++index)
    ASSERT_EQ(talos_rt_v1_clocked_sample_update(
                  context, site, 4, 2, 1, 1, &samples[index], &unknowns[index]),
              TALOS_RT_OK);

  const uint8_t expectedValues[] = {UINT8_C(0x0c), UINT8_C(0x04),
                                    UINT8_C(0x0a)};
  const uint8_t expectedUnknowns[] = {UINT8_C(0x00), UINT8_C(0x01),
                                      UINT8_C(0x00)};
  for (uint64_t age = 0; age != 3; ++age) {
    ASSERT_EQ(talos_rt_v1_clocked_sample_read(context, site, 4, 2, age, 1,
                                                &resultValue, &resultUnknown),
              TALOS_RT_OK);
    EXPECT_EQ(resultValue, expectedValues[age]);
    EXPECT_EQ(resultUnknown, expectedUnknowns[age]);
  }
  EXPECT_EQ(talos_rt_v1_clocked_sample_read(context, site, 4, 2, 3, 1,
                                              &resultValue, &resultUnknown),
            TALOS_RT_INVALID_ARGUMENT);
  talos_rt_v1_context_destroy(context);
}

TEST(SampledValues, SkipsSnapshotAllocationWithoutConsumers) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 4096;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_TRUE(context->preponedValue.empty());
  ASSERT_TRUE(context->preponedUnknown.empty());
  ASSERT_EQ(context->preponedValue.capacity(), 0u);
  ASSERT_EQ(context->preponedUnknown.capacity(), 0u);

  context->stateValue.front() = UINT64_MAX;
  context->stateUnknown.front() = UINT64_MAX;
  EXPECT_EQ(talos_rt_capture_preponed_unlocked(context), TALOS_RT_OK);
  EXPECT_TRUE(context->preponedValue.empty());
  EXPECT_TRUE(context->preponedUnknown.empty());
  EXPECT_EQ(context->preponedValue.capacity(), 0u);
  EXPECT_EQ(context->preponedUnknown.capacity(), 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(SampledValues, UsesBoundedGatedPerProcessHistory) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->activeLogicalProcessToken = 17;
  uint8_t currentValue = 0;
  uint8_t currentUnknown = 0;
  uint8_t previousValue = 0;
  uint8_t previousUnknown = 0;
  auto sample = [&](uint8_t value, uint32_t gate) {
    currentValue = value;
    previousValue = previousUnknown = 0;
    return talos_rt_v1_sampled_history(context, 91, 4, 2, 1, gate,
                                         &currentValue, &currentUnknown,
                                         &previousValue, &previousUnknown);
  };

  ASSERT_EQ(sample(1, 1), TALOS_RT_OK);
  EXPECT_EQ(previousUnknown, UINT8_C(0x0f));
  ASSERT_EQ(sample(2, 1), TALOS_RT_OK);
  EXPECT_EQ(previousUnknown, UINT8_C(0x0f));
  ASSERT_EQ(sample(3, 0), TALOS_RT_OK);
  EXPECT_EQ(previousValue, 1);
  EXPECT_EQ(previousUnknown, 0);
  ASSERT_EQ(sample(3, 1), TALOS_RT_OK);
  EXPECT_EQ(previousValue, 1);
  ASSERT_EQ(sample(4, 1), TALOS_RT_OK);
  EXPECT_EQ(previousValue, 2);

  // The same compiler site in another logical process owns another ring.
  context->activeLogicalProcessToken = 18;
  ASSERT_EQ(sample(9, 1), TALOS_RT_OK);
  EXPECT_EQ(previousUnknown, UINT8_C(0x0f));
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, BuiltinClassWaitersSelectExactNativeAndDesignObjects) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  talos_rt_object_v1 *mailbox = nullptr, *other = nullptr,
                       *semaphore = nullptr;
  auto createMailbox = [&](talos_rt_object_v1 **result) {
    return talos_rt_v1_mailbox_create_typed(
        lane, 1, TALOS_RT_ELEMENT_BITS, 0, sizeof(uint32_t),
        alignof(uint32_t), 32, nullptr, 0, 1, result);
  };
  talos_rt_gc_root_v1 mailboxRoot{}, otherRoot{}, semaphoreRoot{};
  ASSERT_EQ(createMailbox(&mailbox), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &mailboxRoot, &mailbox),
            TALOS_RT_OK);
  ASSERT_EQ(createMailbox(&other), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &otherRoot, &other),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_semaphore_create(lane, 0, &semaphore), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &semaphoreRoot, &semaphore),
            TALOS_RT_OK);
  struct WaitRecord {
    talos_rt_wait_record_v1 header;
    talos_rt_wait_entry_v1 entry;
  };
  auto makeWait = [](talos_rt_object_v1 *object, uint32_t kind,
                     uint32_t flags = 0) {
    return WaitRecord{{TALOS_RT_VERSION, kind, flags, 1,
                       kind == TALOS_RT_SUSPEND_SEMAPHORE ? UINT64_C(1) : 0,
                       0},
                      {talos_rt_managed_word_from_object(object),
                       TALOS_RT_WAIT_EDGE_NONE, 0}};
  };
  auto addDesign = [&](uint64_t id, const WaitRecord &wait,
                       bool terminated = false) {
    ScheduledDesignTask task;
    task.id = id;
    task.started = true;
    task.terminated = terminated;
    task.suspendKind = wait.header.kind;
    task.waitSize = sizeof(wait);
    task.scratchOffset = sizeof(wait);
    task.frame.resize(sizeof(wait));
    std::memcpy(task.frame.data(), &wait, sizeof(wait));
    context->scheduledDesignTasks.push_back(std::move(task));
  };
  addDesign(1, makeWait(mailbox, TALOS_RT_SUSPEND_MAILBOX,
                        TALOS_RT_WAIT_MAILBOX_NOT_EMPTY));
  addDesign(2, makeWait(mailbox, TALOS_RT_SUSPEND_MAILBOX,
                        TALOS_RT_WAIT_MAILBOX_NOT_FULL));
  addDesign(3, makeWait(other, TALOS_RT_SUSPEND_MAILBOX));
  addDesign(4, makeWait(mailbox, TALOS_RT_SUSPEND_MAILBOX), true);
  addDesign(5, makeWait(semaphore, TALOS_RT_SUSPEND_SEMAPHORE));
  addDesign(6, makeWait(mailbox, TALOS_RT_SUSPEND_MAILBOX));
  context->activeLogicalProcessToken = 6;

  const talos_rt_frame_field_v1 field{TALOS_RT_FRAME_WAIT,
                                        TALOS_RT_FRAME_FIELD_FLAGS_NONE,
                                        0,
                                        sizeof(WaitRecord),
                                        alignof(WaitRecord),
                                        0};
  const talos_rt_frame_layout_v1 layout{TALOS_RT_VERSION,
                                          0,
                                          sizeof(WaitRecord),
                                          alignof(WaitRecord),
                                          &field,
                                          1,
                                          0,
                                          nullptr,
                                          0};
  talos_rt_process_descriptor_v1 descriptor{};
  descriptor.frame_layout = &layout;
  std::array<WaitRecord, 3> waits{
      makeWait(mailbox, TALOS_RT_SUSPEND_MAILBOX),
      makeWait(semaphore, TALOS_RT_SUSPEND_SEMAPHORE),
      makeWait(mailbox, TALOS_RT_SUSPEND_MAILBOX)};
  std::array<talos_rt_process_instance_v1, 3> instances{};
  for (size_t index = 0; index != instances.size(); ++index) {
    auto &instance = instances[index];
    instance.descriptor = &descriptor;
    instance.frame = &waits[index];
    instance.frame_size = sizeof(WaitRecord);
    ScheduledProcess process;
    process.token = 11 + index;
    process.started = true;
    process.instance = &instance;
    process.suspendKind = waits[index].header.kind;
    process.waitSize = sizeof(WaitRecord);
    context->scheduledProcesses.push_back(std::move(process));
  }
  context->activeNativeProcess = &instances[2];
  const uint64_t selectionGeneration = context->schedulerSelectionGeneration;
  const uint64_t schedulerEpoch = context->schedulerEpoch;
  const talos_rt_status schedulerStatus = context->schedulerStatus;
  std::vector<uint64_t> waiters;
  ASSERT_EQ(talos_rt_builtin_class_waiters(lane, mailbox, waiters),
            TALOS_RT_OK);
  std::sort(waiters.begin(), waiters.end());
  EXPECT_EQ(waiters, (std::vector<uint64_t>{
                         1, 2, TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | 11}));
  ASSERT_EQ(talos_rt_builtin_class_waiters(lane, semaphore, waiters),
            TALOS_RT_OK);
  std::sort(waiters.begin(), waiters.end());
  EXPECT_EQ(waiters, (std::vector<uint64_t>{
                         5, TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | 12}));
  ASSERT_EQ(talos_rt_builtin_class_waiters(lane, other, waiters),
            TALOS_RT_OK);
  EXPECT_EQ(waiters, (std::vector<uint64_t>{3}));
  EXPECT_EQ(context->schedulerSelectionGeneration, selectionGeneration);
  EXPECT_EQ(context->schedulerEpoch, schedulerEpoch);
  EXPECT_EQ(context->schedulerStatus, schedulerStatus);
  // Native AOT execution can identify the running process by token without
  // publishing an active heap-backed process frame.
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | 13;
  ASSERT_EQ(talos_rt_builtin_class_waiters(lane, mailbox, waiters),
            TALOS_RT_OK);
  std::sort(waiters.begin(), waiters.end());
  EXPECT_EQ(waiters, (std::vector<uint64_t>{
                        1, 2, 6, TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG | 11}));
  waits[0].header.count = 2;
  EXPECT_EQ(talos_rt_builtin_class_waiters(lane, mailbox, waiters),
            TALOS_RT_INVALID_FRAME);
  EXPECT_TRUE(waiters.empty());
  context->activeLogicalProcessToken = 0;
  context->activeNativeProcess = nullptr;
  context->scheduledProcesses.clear();
  context->scheduledDesignTasks.clear();
  ASSERT_EQ(talos_rt_builtin_class_waiters(lane, mailbox, waiters),
            TALOS_RT_OK);
  EXPECT_TRUE(waiters.empty());
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &semaphoreRoot), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &otherRoot), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &mailboxRoot), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

} // namespace
