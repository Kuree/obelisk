//===- ProcessTest.cpp - Shared process instance ABI tests ---------------===//

#include "obelisk/Runtime/ClockKernelReadySet.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "../lib/DesignBytecodeExecution.h"
#include "../lib/DesignBytecodeImage.h"
#include "../lib/DesignBytecodeNets.h"
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

uint64_t checksum(const obelisk_rt_frame_layout_v1 &layout) {
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
  code.resize(offset + OBELISK_RT_BYTECODE_INSTRUCTION_SIZE);
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
obelisk_rt_status nativeExecuteStatus;
bool emitInvalidNativeWait;
bool emitInvalidNativeTerminate;
bool emitExistingNativeWait;
bool emitInvalidResumeRegion;

std::vector<uint32_t> collapsedAliasObserverSamples;

obelisk_rt_status
collapsedAliasObserverEvaluator(obelisk_rt_context *context, const uint64_t *,
                                uint32_t captureCount, uint64_t *value,
                                uint64_t *unknown, uint32_t limbCount) {
  if (!context || captureCount != 0 || !value || !unknown || limbCount != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  collapsedAliasObserverSamples.push_back(
      static_cast<uint32_t>(context->stateValue[0] & 3) |
      (static_cast<uint32_t>(context->stateUnknown[0] & 3) << 2));
  value[0] = context->stateValue[0] & 1;
  unknown[0] = context->stateUnknown[0] & 1;
  return OBELISK_RT_OK;
}
obelisk_rt_status frameDuringExecute;
obelisk_rt_status destroyDuringExecute;
obelisk_rt_context *observedContext;

obelisk_rt_status nativeRequirements(uint64_t *size, uint64_t *alignment) {
  if (!size || !alignment)
    return OBELISK_RT_INVALID_ARGUMENT;
  *size = 64;
  *alignment = 16;
  return OBELISK_RT_OK;
}

obelisk_rt_status nativeExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  void *frame = nullptr;
  uint64_t frameSize = 0;
  frameDuringExecute =
      obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize);
  destroyDuringExecute = obelisk_rt_v1_process_instance_destroy(instance);
  observedContext = instance->context;
  instance->native_handle = instance;
  if (nativeExecuteStatus != OBELISK_RT_OK)
    return nativeExecuteStatus;
  if (instance->continuation == 0) {
    auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(
        static_cast<uint8_t *>(instance->frame) + 8);
    if (!emitExistingNativeWait)
      *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_DELAY, 0, 0, 17, 0};
    uint32_t actionFlags = OBELISK_RT_ACTION_FRAME_WAIT_RECORD;
    if (emitInvalidResumeRegion)
      actionFlags |=
          OBELISK_RT_ACTION_RESUME_REGION_VALID |
          (OBELISK_RT_REGION_NBA << OBELISK_RT_ACTION_RESUME_REGION_SHIFT);
    *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                         wait->kind,
                         1,
                         actionFlags,
                         emitInvalidNativeWait ? 9u : 8u,
                         instance->frame_size - 8};
  } else {
    *instance->action = {OBELISK_RT_FRAGMENT_TERMINATE,
                         OBELISK_RT_SUSPEND_NONE,
                         0,
                         0,
                         emitInvalidNativeTerminate ? 1u : 0u,
                         0};
  }
  return OBELISK_RT_OK;
}

void nativeDestroy(obelisk_rt_process_instance_v1 *instance) {
  ++nativeDestroyCount;
  instance->native_handle = nullptr;
}

struct Fixture {
  std::array<obelisk_rt_frame_field_v1, 2> fields{{
      {OBELISK_RT_FRAME_CAPTURE, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 0, 8, 8, 0},
      {OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 8, 32, 8, 0},
  }};
  std::array<uint32_t, 2> continuations{{0, 1}};
  obelisk_rt_frame_layout_v1 layout{};
  std::vector<uint8_t> code;
  std::array<obelisk_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  obelisk_rt_bytecode_v1 bytecode{};
  obelisk_rt_process_descriptor_v1 descriptor{};

  Fixture() {
    nativeExecuteStatus = OBELISK_RT_OK;
    layout = {OBELISK_RT_VERSION,
              0,
              40,
              8,
              fields.data(),
              static_cast<uint32_t>(fields.size()),
              static_cast<uint32_t>(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = checksum(layout);
    appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0,
                      0, 8);
    appendInstruction(code, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE, 0,
                      OBELISK_RT_SUSPEND_DELAY, 0, 1);
    appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0,
                      0, 8);
    appendInstruction(code, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE, 0,
                      OBELISK_RT_SUSPEND_DELAY, 0, 1);
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
    descriptor = {{OBELISK_RT_DESCRIPTOR_PROCESS, 0, 9},
                  OBELISK_RT_VERSION,
                  0,
                  OBELISK_RT_TIER_MASK_NATIVE | OBELISK_RT_TIER_MASK_BYTECODE,
                  0,
                  &layout,
                  nativeRequirements,
                  nativeExecute,
                  nativeDestroy,
                  &bytecode};
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
const obelisk_rt_process_descriptor_v1 *cachedCohortUrgentChild;
const obelisk_rt_process_descriptor_v1 *cachedCohortOrdinaryChild;
const obelisk_rt_process_descriptor_v1 *cachedCohortTaskCallee;
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
obelisk_rt_status invalidGeneratedCheckpointStatus;
obelisk_rt_status validGeneratedCheckpointStatus;
unsigned schedulerPromotionInvalidationCount;
unsigned schedulerPromotionReadyCount;
bool schedulerPromotionReadyValue;

void schedulerInvalidatePromotion() { ++schedulerPromotionInvalidationCount; }

uint32_t schedulerPromotionReady() {
  ++schedulerPromotionReadyCount;
  return schedulerPromotionReadyValue ? 1u : 0u;
}

struct AOTTestState {
  using RunHook = obelisk_rt_status (*)(AOTTestState *, obelisk_rt_context *);

  std::array<obelisk_rt_process_instance_v1 *, 2> actors{};
  std::vector<obelisk_rt_native_schedule_node> testNodes;
  bool requestFallback = false;
  bool corruptSnapshot = false;
  uint32_t observedSpecializationFast = UINT32_MAX;
  uint32_t observedSpecializationAfterSlot = UINT32_MAX;
  RunHook runHook = nullptr;
  obelisk_rt_generated_nba_accumulator_256 *generatedNBA = nullptr;
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

obelisk_rt_status aotBind(void *opaque, obelisk_rt_context *, uint32_t slot,
                          obelisk_rt_process_instance_v1 *instance) {
  auto *state = static_cast<AOTTestState *>(opaque);
  if (!state || slot >= state->actors.size())
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!instance) {
    state->actors[slot] = nullptr;
    return OBELISK_RT_OK;
  }
  state->actors[slot] = instance;
  return OBELISK_RT_OK;
}

obelisk_rt_status aotRun(void *opaque, obelisk_rt_context *context) {
  auto *state = static_cast<AOTTestState *>(opaque);
  if (!state)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (context->nativeSchedulePlan &&
      context->nativeSchedulePlan->specialization_fast)
    state->observedSpecializationFast =
        *context->nativeSchedulePlan->specialization_fast;
  if (state->requestFallback)
    return OBELISK_RT_TIER_UNAVAILABLE;
  if (state->runHook)
    return state->runHook(state, context);
  return obelisk_rt_v1_scheduler_run(context);
}

obelisk_rt_status aotCommitOneNBARoot(void *, obelisk_rt_context *context,
                                      uint32_t barrierRegion,
                                      uint32_t *outChanged) {
  return obelisk_rt_v1_static_nba_commit_roots(context, 1, barrierRegion,
                                               outChanged);
}

obelisk_rt_status aotCommitAllNBARoots(void *, obelisk_rt_context *context,
                                       uint32_t barrierRegion,
                                       uint32_t *outChanged) {
  return obelisk_rt_v1_static_nba_commit_roots(
      context, context->nativeScheduleNBARootCount, barrierRegion, outChanged);
}

obelisk_rt_status runGuardedNBAOrdering(AOTTestState *state,
                                        obelisk_rt_context *context) {
  if (!state || !state->generatedNBA || !context ||
      !context->nativeSchedulePlan)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto stageGenerated = [&] {
    state->generatedNBA->value[0] = state->generatedNBAValue;
    state->generatedNBA->write_mask[0] = UINT32_MAX;
    state->generatedNBA->valid = 1;
    state->generatedNBA->exec_region = OBELISK_RT_REGION_NBA;
  };
  if (state->generatedNBAFirst)
    stageGenerated();
  obelisk_rt_status status = obelisk_rt_v1_static_nba_claim(
      context, 0, context->nativeSchedulePlan->state_value, nullptr,
      context->nativeSchedulePlan->state_bit_count, 0, 32,
      state->claimedNBAValue, 0);
  if (status == OBELISK_RT_OK && !state->generatedNBAFirst)
    stageGenerated();
  return status;
}

obelisk_rt_status runSpecializationFastRearm(AOTTestState *state,
                                             obelisk_rt_context *context) {
  if (!state || !context || !context->nativeSchedulePlan ||
      !context->nativeSchedulePlan->specialization_fast ||
      context->staticNBASlowRoots.empty())
    return OBELISK_RT_INVALID_ARGUMENT;
  context->staticNBASlowRoots[0] = 1;
  context->staticNBASlowRootsPresent = true;
  *context->nativeSchedulePlan->specialization_fast = 0;
  obelisk_rt_status status = obelisk_rt_v1_scheduler_run(context);
  state->observedSpecializationAfterSlot =
      *context->nativeSchedulePlan->specialization_fast;
  return status;
}

obelisk_rt_status runCheckpointThenReenter(AOTTestState *state,
                                           obelisk_rt_context *) {
  if (!state)
    return OBELISK_RT_INVALID_ARGUMENT;
  ++state->runCalls;
  return state->runCalls == 1 ? OBELISK_RT_AOT_CHECKPOINT : OBELISK_RT_OK;
}

obelisk_rt_status runTimedCheckpointThenReenter(AOTTestState *state,
                                                obelisk_rt_context *context) {
  if (!state || !context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ++state->runCalls;
  if (state->runCalls != 1)
    return OBELISK_RT_OK;
  context->nativePeriodicRuntimeDeadline = 5;
  return OBELISK_RT_AOT_TIMED_CHECKPOINT;
}

obelisk_rt_status runCountOK(AOTTestState *state, obelisk_rt_context *) {
  if (!state)
    return OBELISK_RT_INVALID_ARGUMENT;
  ++state->runCalls;
  return OBELISK_RT_OK;
}

obelisk_rt_status runObserverPlaneAuthority(AOTTestState *state,
                                            obelisk_rt_context *context) {
  if (!state || !context || !state->authorityPlane)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto load = [&](uint8_t &value) {
    return obelisk_rt_v1_native_state_load_plane(context, state->authorityPlane,
                                                 8, state->authorityHandle, 8,
                                                 0, 0, &value);
  };
  if (obelisk_rt_status status = load(state->ordinaryAOTLoad);
      status != OBELISK_RT_OK)
    return status;
  ++context->observerDepth;
  obelisk_rt_status status = load(state->nestedObserverLoad);
  --context->observerDepth;
  if (status != OBELISK_RT_OK)
    return status;
  bool previous = context->observerForcesCanonicalPlane;
  context->observerForcesCanonicalPlane = true;
  status = load(state->canonicalObserverLoad);
  context->observerForcesCanonicalPlane = previous;
  return status;
}

obelisk_rt_status runDirectSelfTransition(AOTTestState *state,
                                          obelisk_rt_context *context) {
  if (!state || !context)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_process_instance_v1 *instance = nullptr;
  obelisk_rt_status status =
      obelisk_rt_v1_scheduler_direct_fragment_enter(context, 0, 1, &instance);
  if (status != OBELISK_RT_OK || instance != state->actors[0])
    return status != OBELISK_RT_OK ? status : OBELISK_RT_INVALID_LIFECYCLE;
  obelisk_rt_v1_scheduler_static_transition(context, 1, 0, 1, 0, 0, 1, 0);
  return obelisk_rt_v1_scheduler_direct_fragment_leave(context, 0);
}

obelisk_rt_status aotRunNodes(void *opaque, obelisk_rt_context *context) {
  if (!opaque)
    return OBELISK_RT_INVALID_ARGUMENT;
  constexpr obelisk_rt_native_schedule_node nodes[] = {{0, 0}, {0, 1}};
  return obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes,
                                               std::size(nodes));
}

obelisk_rt_status generatedCheckpointCallback(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ++generatedCheckpointCallbackCount;
  if (generatedCheckpointCallbackCount == 1) {
    obelisk_rt_status status = obelisk_rt_v1_scheduler_queue_aot_checkpoint(
        context, 0, 1, generatedCheckpointCallback);
    return status == OBELISK_RT_OK ? OBELISK_RT_AOT_GENERATED_CHECKPOINT
                                   : status;
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status runGeneratedCheckpoint(AOTTestState *state,
                                         obelisk_rt_context *context) {
  if (!state || !context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ++state->runCalls;
  if (state->runCalls != 1)
    return OBELISK_RT_OK;
  // Model run_until reaching a new slot without runtime slot entry. Callback
  // progress must survive the subsequent current-slot drain.
  context->schedulerTime = 17;
  context->schedulerSlotProgress = 0;
  invalidGeneratedCheckpointStatus =
      obelisk_rt_v1_scheduler_queue_aot_checkpoint(context, 0, 2,
                                                   generatedCheckpointCallback);
  validGeneratedCheckpointStatus = obelisk_rt_v1_scheduler_queue_aot_checkpoint(
      context, 0, 1, generatedCheckpointCallback);
  return validGeneratedCheckpointStatus == OBELISK_RT_OK
             ? OBELISK_RT_AOT_CHECKPOINT
             : validGeneratedCheckpointStatus;
}

obelisk_rt_status aotRunWaitNodes(void *opaque, obelisk_rt_context *context) {
  if (!opaque)
    return OBELISK_RT_INVALID_ARGUMENT;
  constexpr obelisk_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
  };
  return obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes,
                                               std::size(nodes));
}

obelisk_rt_status aotRunOneNode(void *opaque, obelisk_rt_context *context) {
  if (!opaque)
    return OBELISK_RT_INVALID_ARGUMENT;
  constexpr obelisk_rt_native_schedule_node nodes[] = {{0, 0}};
  return obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes,
                                               std::size(nodes));
}

obelisk_rt_status runAOTNodes(AOTTestState *state,
                              obelisk_rt_context *context) {
  if (!state)
    return OBELISK_RT_INVALID_ARGUMENT;
  ++state->runCalls;
  return aotRunNodes(state, context);
}

obelisk_rt_status aotRunOneNodeThenFallback(void *opaque,
                                            obelisk_rt_context *context) {
  obelisk_rt_status status = aotRunOneNode(opaque, context);
  return status == OBELISK_RT_OK ? OBELISK_RT_TIER_UNAVAILABLE : status;
}

obelisk_rt_status runGroupedStaticActivationNodes(AOTTestState *state,
                                                  obelisk_rt_context *context) {
  if (!state->testNodes.empty())
    return obelisk_rt_v1_scheduler_run_aot_nodes(
        context, state->testNodes.data(), state->testNodes.size());
  constexpr obelisk_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {1, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
      {1, 1, UINT32_MAX},
  };
  return obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes,
                                               std::size(nodes));
}

obelisk_rt_status aotSnapshot(void *opaque, obelisk_rt_context *context,
                              obelisk_rt_aot_deopt_snapshot *snapshot) {
  auto *state = static_cast<AOTTestState *>(opaque);
  if (!state || !context || !snapshot)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_status status =
      obelisk_rt_v1_scheduler_snapshot_aot(context, snapshot);
  if (status == OBELISK_RT_OK && state->corruptSnapshot)
    snapshot->size = 0;
  return status;
}

uint64_t *clockCoordinatorIngress = nullptr;
uint32_t clockCoordinatorWords = 0;
uint32_t clockCoordinatorCalls = 0;

obelisk_rt_status clockCoordinator(void *, obelisk_rt_context *context) {
  if (!context || !clockCoordinatorIngress)
    return OBELISK_RT_INVALID_ARGUMENT;
  ++clockCoordinatorCalls;
  std::fill_n(clockCoordinatorIngress, clockCoordinatorWords, uint64_t{0});
  return OBELISK_RT_OK;
}

obelisk_rt_status clockWaitCoordinator(void *opaque,
                                       obelisk_rt_context *context) {
  if (!context || !clockCoordinatorIngress || clockCoordinatorWords != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t ready = (clockCoordinatorIngress[0] & 1) != 0 ? uint64_t{2} : 0;
  clockCoordinatorIngress[0] = 0;
  ++clockCoordinatorCalls;
  obelisk_rt_v1_scheduler_activate_static_nodes(context, &ready, 1);
  if (context->schedulerStatus != OBELISK_RT_OK)
    return context->schedulerStatus;
  return aotRunWaitNodes(opaque, context);
}

obelisk_rt_native_schedule_plan makeAOTPlan(AOTTestState &state,
                                            uint32_t actors = 2) {
  return {sizeof(obelisk_rt_native_schedule_plan),
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
          0};
}

obelisk_rt_status schedulerRequirements(uint64_t *size, uint64_t *alignment) {
  if (!size || !alignment)
    return OBELISK_RT_INVALID_ARGUMENT;
  *size = 0;
  *alignment = 1;
  return OBELISK_RT_OK;
}

obelisk_rt_status schedulerExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  if (instance->descriptor->handle.id >= 100) {
    schedulerOrder.push_back(instance->descriptor->handle.id);
    *instance->action = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return OBELISK_RT_OK;
  }
  if (instance->continuation != 0) {
    schedulerOrder.push_back(instance->descriptor->handle.id);
    ++schedulerResumeCount;
    *instance->action = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return OBELISK_RT_OK;
  }
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(
      static_cast<uint8_t *>(instance->frame) + schedulerWaitOffset);
  auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  if (schedulerWaitKind == OBELISK_RT_SUSPEND_DELAY) {
    *wait = {
        OBELISK_RT_VERSION, schedulerWaitKind, 0, 0, schedulerWaitDelay, 0};
  } else {
    *wait = {OBELISK_RT_VERSION, schedulerWaitKind, 0, 1, 0, 0};
    *entry = {schedulerWaitHandle, schedulerWaitEdge,
              schedulerWaitKind == OBELISK_RT_SUSPEND_CHANGE ||
                      schedulerWaitKind == OBELISK_RT_SUSPEND_EDGE
                  ? schedulerWaitWidth
                  : 0};
  }
  *instance->action = {
      OBELISK_RT_FRAGMENT_SUSPEND,         schedulerWaitKind,   1,
      OBELISK_RT_ACTION_FRAME_WAIT_RECORD, schedulerWaitOffset, 48};
  return OBELISK_RT_OK;
}

obelisk_rt_status
cachedSignalCohortExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
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
        obelisk_rt_v1_scheduler_signal(instance->context,
                                       cachedCohortSecondarySignal, 1,
                                       OBELISK_RT_SIGNAL_CHANGE);
      *instance->action = {
          OBELISK_RT_FRAGMENT_CONTINUE, OBELISK_RT_SUSPEND_NONE, 1, 0, 0, 0};
      return OBELISK_RT_OK;
    }
    if (id == cachedCohortFrontierID && !cachedCohortFrontierSuspended) {
      cachedCohortFrontierSuspended = true;
      auto *wait =
          reinterpret_cast<obelisk_rt_wait_record_v1 *>(instance->frame);
      auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
      *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_FRONTIER, 0, 1, 0, 0};
      *entry = {0, OBELISK_RT_WAIT_EDGE_NONE, 0};
      *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                           OBELISK_RT_SUSPEND_FRONTIER,
                           1,
                           OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                           0,
                           48};
      return OBELISK_RT_OK;
    }
    if (id == cachedCohortTaskCallID && !cachedCohortTaskCalled) {
      if (!cachedCohortTaskCallee)
        return OBELISK_RT_INVALID_LIFECYCLE;
      obelisk_rt_process_instance_v1 *callee = nullptr;
      obelisk_rt_status status = obelisk_rt_v1_process_instance_create(
          cachedCohortTaskCallee, &callee);
      if (status != OBELISK_RT_OK)
        return status;
      cachedCohortTaskCalled = true;
      *instance->action = {
          OBELISK_RT_FRAGMENT_TASK_CALL,
          OBELISK_RT_SUSPEND_NONE,
          1,
          0,
          static_cast<uint64_t>(reinterpret_cast<uintptr_t>(callee)),
          0};
      return OBELISK_RT_OK;
    }
    if (id == cachedCohortSpawnID) {
      if (!cachedCohortUrgentChild || !cachedCohortOrdinaryChild)
        return OBELISK_RT_INVALID_LIFECYCLE;
      obelisk_rt_process_instance_v1 *ordinary = nullptr;
      obelisk_rt_status status = obelisk_rt_v1_process_instance_create(
          cachedCohortOrdinaryChild, &ordinary);
      if (status != OBELISK_RT_OK)
        return status;
      status = obelisk_rt_v1_scheduler_add(instance->context, ordinary, 0);
      if (status != OBELISK_RT_OK) {
        obelisk_rt_v1_process_instance_destroy(ordinary);
        return status;
      }
      obelisk_rt_process_instance_v1 *urgent = nullptr;
      status = obelisk_rt_v1_process_instance_create(cachedCohortUrgentChild,
                                                     &urgent);
      if (status != OBELISK_RT_OK)
        return status;
      status = obelisk_rt_v1_scheduler_add(instance->context, urgent,
                                           OBELISK_RT_SCHEDULE_STARTUP);
      if (status != OBELISK_RT_OK) {
        obelisk_rt_v1_process_instance_destroy(urgent);
        return status;
      }
    }
    if (id == cachedCohortPublishID)
      obelisk_rt_v1_scheduler_signal(instance->context,
                                     cachedCohortSecondarySignal, 1,
                                     OBELISK_RT_SIGNAL_CHANGE);
    *instance->action = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return OBELISK_RT_OK;
  }

  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  *entry = {id == cachedCohortSecondaryID ? cachedCohortSecondarySignal
                                          : cachedCohortPrimarySignal,
            OBELISK_RT_WAIT_EDGE_CHANGE, 1};
  *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                       OBELISK_RT_SUSPEND_CHANGE,
                       1,
                       OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       48};
  return OBELISK_RT_OK;
}

obelisk_rt_status
schedulerSelfTriggerExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !instance->context)
    return OBELISK_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  if (instance->continuation != 0) {
    ++schedulerSelfTriggerCount;
    if (schedulerSelfTriggerCount == 3) {
      *instance->action = {
          OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
      return OBELISK_RT_OK;
    }
    if (schedulerSelfTriggerStaticState != 0) {
      uint64_t oldValue = schedulerSelfTriggerCount & 1;
      obelisk_rt_v1_scheduler_static_transition(
          instance->context, schedulerSelfTriggerStaticState, 0, 1, oldValue, 0,
          oldValue ^ 1, 0);
    } else {
      obelisk_rt_v1_scheduler_signal(instance->context, schedulerWaitHandle,
                                     schedulerWaitWidth,
                                     OBELISK_RT_SIGNAL_CHANGE);
    }
  }
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  *entry = {schedulerWaitHandle, OBELISK_RT_WAIT_EDGE_CHANGE,
            schedulerWaitWidth};
  *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                       OBELISK_RT_SUSPEND_CHANGE,
                       1,
                       OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       48};
  return OBELISK_RT_OK;
}

obelisk_rt_status
schedulerCheckpointThenNativeExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (instance->continuation == 0) {
    instance->native_handle = instance;
    ++schedulerCheckpointCount;
    return OBELISK_RT_AOT_CHECKPOINT;
  }
  return schedulerExecute(instance);
}

obelisk_rt_status
groupedStaticActivationExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
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
      obelisk_rt_v1_scheduler_activate_static_nodes(instance->context,
                                                    nodes.data(), nodes.size());
      obelisk_rt_v1_scheduler_activate_static_nodes(instance->context,
                                                    nodes.data(), nodes.size());
    } else {
      ++schedulerResumeCount;
    }
    *instance->action = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return OBELISK_RT_OK;
  }

  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(instance->frame);
  if (id == 90) {
    *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_DELAY, 0, 0, 1, 0};
  } else {
    auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
    *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
    *entry = {1, OBELISK_RT_WAIT_EDGE_CHANGE, 1};
  }
  *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                       id == 90 ? OBELISK_RT_SUSPEND_DELAY
                                : OBELISK_RT_SUSPEND_CHANGE,
                       1,
                       OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       48};
  return OBELISK_RT_OK;
}

void schedulerDestroy(obelisk_rt_process_instance_v1 *instance) {
  ++schedulerDestroyCount;
  instance->native_handle = nullptr;
}

unsigned finishDestroyCount;
const obelisk_rt_process_descriptor_v1 *taskCalleeDescriptor;
unsigned taskCallerExecutions;
unsigned taskCalleeExecutions;

obelisk_rt_status finishExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  obelisk_rt_status status =
      obelisk_rt_v1_scheduler_finish(instance->context, 0);
  if (status != OBELISK_RT_OK)
    return status;
  *instance->action = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return OBELISK_RT_OK;
}

void finishDestroy(obelisk_rt_process_instance_v1 *instance) {
  ++finishDestroyCount;
  instance->native_handle = nullptr;
}

obelisk_rt_status taskCallerExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !taskCalleeDescriptor)
    return OBELISK_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  ++taskCallerExecutions;
  if (instance->continuation != 0) {
    *instance->action = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return OBELISK_RT_OK;
  }
  obelisk_rt_process_instance_v1 *callee = nullptr;
  obelisk_rt_status status =
      obelisk_rt_v1_process_instance_create(taskCalleeDescriptor, &callee);
  if (status != OBELISK_RT_OK)
    return status;
  *instance->action = {
      OBELISK_RT_FRAGMENT_TASK_CALL,
      OBELISK_RT_SUSPEND_NONE,
      1,
      0,
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(callee)),
      0};
  return OBELISK_RT_OK;
}

obelisk_rt_status taskCalleeExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  ++taskCalleeExecutions;
  *instance->action = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return OBELISK_RT_OK;
}

uint64_t processAutomaticHandle;
uint64_t retainedAutomaticHandle;
uint64_t nbaAutomaticHandle;
uint8_t nbaDummyPlane;

obelisk_rt_status
automaticStateExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  uint8_t initial = 0x5a;
  obelisk_rt_status status = obelisk_rt_v1_native_state_alloc(
      instance->context, 8, &initial, nullptr, &processAutomaticHandle);
  if (status != OBELISK_RT_OK)
    return status;
  *instance->action = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return OBELISK_RT_OK;
}

obelisk_rt_status
retainedAutomaticStateExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  uint8_t initial = 0xa5;
  obelisk_rt_status status = obelisk_rt_v1_native_state_alloc(
      instance->context, 8, &initial, nullptr, &retainedAutomaticHandle);
  if (status != OBELISK_RT_OK)
    return status;
  status = obelisk_rt_v1_native_state_retain(instance->context,
                                             retainedAutomaticHandle);
  if (status != OBELISK_RT_OK)
    return status;
  status = obelisk_rt_v1_native_state_release(instance->context,
                                              retainedAutomaticHandle, 1);
  if (status != OBELISK_RT_OK)
    return status;
  *instance->action = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return OBELISK_RT_OK;
}

obelisk_rt_status
automaticNBAExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  uint8_t initial = 0;
  obelisk_rt_status status = obelisk_rt_v1_native_state_alloc(
      instance->context, 8, &initial, nullptr, &nbaAutomaticHandle);
  if (status != OBELISK_RT_OK)
    return status;
  uint8_t replacement = 0xa5;
  status = obelisk_rt_v1_scheduler_nba(instance->context, &nbaDummyPlane,
                                       nullptr, 8, nbaAutomaticHandle, 8, 3,
                                       &replacement, nullptr);
  if (status != OBELISK_RT_OK)
    return status;
  status = obelisk_rt_v1_native_state_release(instance->context,
                                              nbaAutomaticHandle, 1);
  if (status != OBELISK_RT_OK)
    return status;
  *instance->action = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return OBELISK_RT_OK;
}

struct SchedulerFixture {
  std::array<obelisk_rt_frame_field_v1, 2> fields{{
      {OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 0, 48, 8, 0},
      {OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 48, 48, 8, 0},
  }};
  std::array<uint32_t, 2> continuations{{0, 1}};
  obelisk_rt_frame_layout_v1 layout{};
  obelisk_rt_process_descriptor_v1 descriptor{};

  explicit SchedulerFixture(uint64_t id) {
    schedulerWaitOffset = 0;
    schedulerWaitDelay = 17;
    layout = {OBELISK_RT_VERSION,
              0,
              96,
              8,
              fields.data(),
              static_cast<uint32_t>(fields.size()),
              static_cast<uint32_t>(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = checksum(layout);
    descriptor = {{OBELISK_RT_DESCRIPTOR_PROCESS, 0, id},
                  OBELISK_RT_VERSION,
                  0,
                  OBELISK_RT_TIER_MASK_NATIVE,
                  0,
                  &layout,
                  schedulerRequirements,
                  schedulerExecute,
                  schedulerDestroy,
                  nullptr};
  }
};

bool clockOccurrenceLifecycleChangesWait;
unsigned clockOccurrenceLifecycleResumes;

obelisk_rt_status
clockOccurrenceLifecycleExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !instance->frame)
    return OBELISK_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(instance->frame);
  auto *entries = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  if (instance->continuation == 0 ||
      (instance->continuation == 1 && !clockOccurrenceLifecycleChangesWait)) {
    if (instance->continuation != 0)
      ++clockOccurrenceLifecycleResumes;
    *wait = {OBELISK_RT_VERSION,
             OBELISK_RT_SUSPEND_EDGE,
             OBELISK_RT_WAIT_CLOCK_OCCURRENCE,
             2,
             91,
             0};
    entries[0] = {16, OBELISK_RT_WAIT_EDGE_POSEDGE, 1};
    entries[1] = {32, OBELISK_RT_WAIT_EDGE_POSEDGE, 1};
    *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                         OBELISK_RT_SUSPEND_EDGE,
                         instance->continuation + 1,
                         OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                         0,
                         sizeof(obelisk_rt_wait_record_v1) +
                             2 * sizeof(obelisk_rt_wait_entry_v1)};
    return OBELISK_RT_OK;
  }
  ++clockOccurrenceLifecycleResumes;
  if (instance->continuation == 1 && clockOccurrenceLifecycleChangesWait) {
    *wait = {OBELISK_RT_VERSION,
             OBELISK_RT_SUSPEND_EDGE,
             OBELISK_RT_WAIT_FLAGS_NONE,
             1,
             0,
             0};
    entries[0] = {48, OBELISK_RT_WAIT_EDGE_POSEDGE, 1};
    *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                         OBELISK_RT_SUSPEND_EDGE,
                         2,
                         OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                         0,
                         64};
    return OBELISK_RT_OK;
  }
  *instance->action = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return OBELISK_RT_OK;
}

struct ClockOccurrenceLifecycleFixture {
  std::array<obelisk_rt_frame_field_v1, 1> fields{{
      {OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 0, 64, 8, 0},
  }};
  std::array<uint32_t, 3> continuations{{0, 1, 2}};
  obelisk_rt_frame_layout_v1 layout{};
  obelisk_rt_process_descriptor_v1 descriptor{};

  explicit ClockOccurrenceLifecycleFixture(uint64_t id) {
    layout = {OBELISK_RT_VERSION,
              0,
              64,
              8,
              fields.data(),
              static_cast<uint32_t>(fields.size()),
              static_cast<uint32_t>(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = checksum(layout);
    descriptor = {{OBELISK_RT_DESCRIPTOR_PROCESS, 0, id},
                  OBELISK_RT_VERSION,
                  0,
                  OBELISK_RT_TIER_MASK_NATIVE,
                  0,
                  &layout,
                  schedulerRequirements,
                  clockOccurrenceLifecycleExecute,
                  schedulerDestroy,
                  nullptr};
  }
};

constexpr uint64_t eventOnlyComputedObserverID = 0x7e01;
constexpr uint64_t eventOnlyComputedDependencyID = 0x7e02;
unsigned eventOnlyComputedResumeCount;

struct EventOnlyComputedWait {
  obelisk_rt_computed_wait_record_v1 wait{};
  obelisk_rt_computed_observer_v1 observer{};
  obelisk_rt_computed_dependency_v1 dependency{};
  obelisk_rt_computed_clause_v1 clause{};
  uint64_t previousValue = 0;
  uint64_t previousUnknown = 0;
};

obelisk_rt_status eventOnlyComputedEvaluator(obelisk_rt_context *context,
                                             const uint64_t *,
                                             uint32_t captureCount,
                                             uint64_t *value, uint64_t *unknown,
                                             uint32_t limbCount) {
  if (!context || captureCount != 0 || !value || !unknown || limbCount != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  value[0] = 1;
  unknown[0] = 0;
  return OBELISK_RT_OK;
}

obelisk_rt_status
eventOnlyComputedExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !instance->frame ||
      instance->frame_size < sizeof(EventOnlyComputedWait))
    return OBELISK_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  if (instance->continuation != 0) {
    ++eventOnlyComputedResumeCount;
    *instance->action = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return OBELISK_RT_OK;
  }
  auto &record = *static_cast<EventOnlyComputedWait *>(instance->frame);
  record = {};
  record.wait = {OBELISK_RT_VERSION,
                 OBELISK_RT_SUSPEND_OBSERVER,
                 OBELISK_RT_COMPUTED_WAIT_INTERLEAVED,
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
                       OBELISK_RT_OBSERVER_DEPENDENCY_EVENT, 1};
  record.clause = {0, OBELISK_RT_OBSERVER_CONDITION_NONE,
                   OBELISK_RT_WAIT_EDGE_POSEDGE, 0};
  *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                       OBELISK_RT_SUSPEND_OBSERVER,
                       1,
                       OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       sizeof(EventOnlyComputedWait)};
  return OBELISK_RT_OK;
}

struct EventOnlyComputedFixture {
  std::array<obelisk_rt_frame_field_v1, 1> fields{{
      {OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 0,
       sizeof(EventOnlyComputedWait), alignof(EventOnlyComputedWait), 0},
  }};
  std::array<uint32_t, 2> continuations{{0, 1}};
  obelisk_rt_frame_layout_v1 layout{};
  obelisk_rt_process_descriptor_v1 descriptor{};

  explicit EventOnlyComputedFixture(uint64_t id) {
    layout = {OBELISK_RT_VERSION,
              0,
              sizeof(EventOnlyComputedWait),
              alignof(EventOnlyComputedWait),
              fields.data(),
              static_cast<uint32_t>(fields.size()),
              static_cast<uint32_t>(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = checksum(layout);
    descriptor = {{OBELISK_RT_DESCRIPTOR_PROCESS, 0, id},
                  OBELISK_RT_VERSION,
                  0,
                  OBELISK_RT_TIER_MASK_NATIVE,
                  0,
                  &layout,
                  schedulerRequirements,
                  eventOnlyComputedExecute,
                  schedulerDestroy,
                  nullptr};
  }
};

obelisk_rt_process_instance_v1 *
makeClockOccurrenceLifecycleInstance(ClockOccurrenceLifecycleFixture &fixture) {
  obelisk_rt_process_instance_v1 *instance = nullptr;
  EXPECT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  return instance;
}

obelisk_rt_process_instance_v1 *
makeSchedulerInstance(SchedulerFixture &fixture) {
  obelisk_rt_process_instance_v1 *instance = nullptr;
  EXPECT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
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
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 256;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 53, 128),
            OBELISK_RT_OK);
  uint64_t handle = obelisk_rt_v1_native_state_static_handle(1);
  for (uint64_t width : {1, 7, 8, 31, 32, 63, 64})
    for (bool forced : {false, true})
      for (bool establishesOverride : {false, true}) {
        context->stateValue.assign(4, UINT64_C(0x9696969696969696));
        context->stateUnknown.assign(4, UINT64_C(0x6969696969696969));
        context->forceMask.assign(4, forced ? UINT64_C(0x2222222222222222) : 0);
        auto expectedValue = context->stateValue;
        auto expectedUnknown = context->stateUnknown;
        std::array<uint8_t, 8> changed{}, value{}, unknown{};
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
            auto &word = plane[absolute / 64];
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
  obelisk_rt_v1_context_destroy(context);
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
  uint32_t postponed =
      OBELISK_RT_ACTION_RESUME_REGION_VALID |
      (OBELISK_RT_REGION_POSTPONED << OBELISK_RT_ACTION_RESUME_REGION_SHIFT);
  EXPECT_TRUE(obelisk_rt_next_queued_region(OBELISK_RT_REGION_ACTIVE,
                                            OBELISK_RT_SUSPEND_CHANGE, 1,
                                            postponed, queuedRegion));
  EXPECT_EQ(queuedRegion, OBELISK_RT_REGION_POSTPONED);

  uint32_t nba =
      OBELISK_RT_ACTION_RESUME_REGION_VALID |
      (OBELISK_RT_REGION_NBA << OBELISK_RT_ACTION_RESUME_REGION_SHIFT);
  EXPECT_FALSE(obelisk_rt_next_queued_region(OBELISK_RT_REGION_ACTIVE,
                                             OBELISK_RT_SUSPEND_CHANGE, 1, nba,
                                             queuedRegion));
}

TEST(RuntimeInternals, MonitorRepeatsOnlyWhenItsDisplayedValuesChange) {
  // IEEE 1800-2017 21.2.3: a monitor reports when one of its arguments
  // changes value. Waking it without a change -- an unrelated bit of a
  // watched variable, say -- must not repeat the report.
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  std::string path =
      (std::filesystem::temp_directory_path() / "obelisk-monitor.bin").string();
  std::string mode = "w+b";
  uint32_t descriptor = 0;
  ASSERT_EQ(obelisk_rt_v1_file_open(context, path.data(), path.size(),
                                    mode.data(), mode.size(), &descriptor),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_monitor_register(context, 21, 1), OBELISK_RT_OK);
  context->activeLogicalProcessToken = 21;

  std::string formatString = "bit=%b";
  uint64_t zero = 0;
  uint64_t one = 1;
  auto report = [&](const uint64_t &value) {
    obelisk_rt_arg_v1 items[2] = {
        {OBELISK_RT_ARG_STRING, OBELISK_RT_ARG_FORMAT_STRING,
         formatString.size(), formatString.data(), nullptr},
        {OBELISK_RT_ARG_LOGIC, 0, 1, &value, nullptr}};
    return obelisk_rt_v1_display(context, descriptor, 1,
                                 OBELISK_RT_RADIX_DECIMAL, items, 2, nullptr);
  };
  ASSERT_EQ(report(zero), OBELISK_RT_OK);
  ASSERT_EQ(report(zero), OBELISK_RT_OK);
  ASSERT_EQ(report(one), OBELISK_RT_OK);
  ASSERT_EQ(report(one), OBELISK_RT_OK);

  // A simulation time argument advances on its own and never triggers a
  // report, so a list carrying one still reports only on a value change.
  std::string timedFormat = "%0t bit=%b";
  uint64_t early = 10;
  uint64_t late = 20;
  auto timedReport = [&](const uint64_t &time, const uint64_t &value) {
    obelisk_rt_arg_v1 items[3] = {
        {OBELISK_RT_ARG_STRING, OBELISK_RT_ARG_FORMAT_STRING,
         timedFormat.size(), timedFormat.data(), nullptr},
        {OBELISK_RT_ARG_TIME, 0, 64, &time, nullptr},
        {OBELISK_RT_ARG_LOGIC, 0, 1, &value, nullptr}};
    return obelisk_rt_v1_display(context, descriptor, 1,
                                 OBELISK_RT_RADIX_DECIMAL, items, 3, nullptr);
  };
  ASSERT_EQ(timedReport(early, one), OBELISK_RT_OK);
  ASSERT_EQ(timedReport(late, one), OBELISK_RT_OK);
  ASSERT_EQ(timedReport(late, zero), OBELISK_RT_OK);
  context->activeLogicalProcessToken = 0;

  ASSERT_EQ(obelisk_rt_v1_file_flush(context, descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);
  char bytes[64]{};
  uint64_t read = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)),
            "bit=0\nbit=1\n10 bit=1\n20 bit=0\n");
  obelisk_rt_v1_context_destroy(context);
  std::filesystem::remove(path);
}

TEST(RuntimeInternals, ClosingAChannelDisablesTheMonitorWritingToIt) {
  // IEEE 1800-2017 21.3.5: closing a channel disables any $fmonitor or
  // $fstrobe associated with it. The report that would follow the close is
  // dropped, and the monitor stays unregistered rather than failing every
  // time one of its arguments changes.
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  std::string path =
      (std::filesystem::temp_directory_path() / "obelisk-monitor-closed.bin")
          .string();
  std::string mode = "w+b";
  uint32_t descriptor = 0;
  ASSERT_EQ(obelisk_rt_v1_file_open(context, path.data(), path.size(),
                                    mode.data(), mode.size(), &descriptor),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_monitor_register(context, 21, 1), OBELISK_RT_OK);
  context->activeLogicalProcessToken = 21;

  std::string formatString = "bit=%b";
  uint64_t zero = 0;
  uint64_t one = 1;
  auto report = [&](const uint64_t &value) {
    obelisk_rt_arg_v1 items[2] = {
        {OBELISK_RT_ARG_STRING, OBELISK_RT_ARG_FORMAT_STRING,
         formatString.size(), formatString.data(), nullptr},
        {OBELISK_RT_ARG_LOGIC, 0, 1, &value, nullptr}};
    return obelisk_rt_v1_display(context, descriptor, 1,
                                 OBELISK_RT_RADIX_DECIMAL, items, 2, nullptr);
  };
  ASSERT_EQ(report(zero), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
  EXPECT_EQ(report(one), OBELISK_RT_OK);
  EXPECT_EQ(context->monitorLogicalProcessToken, 0u);
  context->activeLogicalProcessToken = 0;

  std::ifstream written(path, std::ios::binary);
  std::string contents((std::istreambuf_iterator<char>(written)),
                       std::istreambuf_iterator<char>());
  EXPECT_EQ(contents, "bit=0\n");
  obelisk_rt_v1_context_destroy(context);
  std::filesystem::remove(path);
}

TEST(RuntimeInternals, ReplacedAndReenabledMonitorsAreWokenInPostponed) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  uint64_t selectionGeneration = context->schedulerSelectionGeneration;
  ScheduledDesignTask oldMonitor;
  oldMonitor.id = 17;
  oldMonitor.suspendKind = OBELISK_RT_SUSPEND_FOREVER;
  oldMonitor.queuedRegion = OBELISK_RT_REGION_ACTIVE;
  context->scheduledDesignTasks.push_back(std::move(oldMonitor));

  ASSERT_EQ(obelisk_rt_v1_monitor_register(context, 17, 1), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_monitor_register(context, 18, 1), OBELISK_RT_OK);
  EXPECT_EQ(context->scheduledDesignTasks.front().suspendKind,
            OBELISK_RT_SUSPEND_NONE);
  EXPECT_EQ(context->scheduledDesignTasks.front().queuedRegion,
            OBELISK_RT_REGION_POSTPONED);
  EXPECT_NE(context->schedulerSelectionGeneration, selectionGeneration);

  context->scheduledDesignTasks.front().suspendKind =
      OBELISK_RT_SUSPEND_FOREVER;
  selectionGeneration = context->schedulerSelectionGeneration;
  ASSERT_EQ(obelisk_rt_v1_monitor_register(context, 17, 1), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_monitor_control(context, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_monitor_control(context, 1), OBELISK_RT_OK);
  EXPECT_EQ(context->scheduledDesignTasks.front().suspendKind,
            OBELISK_RT_SUSPEND_NONE);
  EXPECT_EQ(context->scheduledDesignTasks.front().queuedRegion,
            OBELISK_RT_REGION_POSTPONED);
  EXPECT_NE(context->schedulerSelectionGeneration, selectionGeneration);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ManagedObjectWaitsRemainRootsAcrossCollection) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);

  for (uint32_t suspendKind :
       {OBELISK_RT_SUSPEND_SEMAPHORE, OBELISK_RT_SUSPEND_MAILBOX}) {
    obelisk_rt_string_v1 string = 0;
    ASSERT_EQ(obelisk_rt_v1_string_create(lane, "waiting", 7, &string),
              OBELISK_RT_OK);
    const obelisk_rt_string_v1 stableID = string;

    struct {
      obelisk_rt_wait_record_v1 wait;
      obelisk_rt_wait_entry_v1 entry;
    } record{{OBELISK_RT_VERSION, suspendKind, 0, 1, 0, 0},
             {stableID, OBELISK_RT_WAIT_EDGE_NONE, 0}};
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
    ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
    char inlineBytes[8]{};
    const char *bytes = nullptr;
    uint64_t size = 0;
    ASSERT_EQ(obelisk_rt_v1_string_view(stableID, inlineBytes, &bytes, &size),
              OBELISK_RT_OK);
    EXPECT_EQ(std::string_view(bytes, size), "waiting");

    context->scheduledDesignTasks.clear();
    ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  }

  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, SignalSubscriptionsAreRangeIndexedStableAndBounded) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->signalDiagnosticsEnabled = true;

  struct {
    obelisk_rt_wait_record_v1 wait;
    std::array<obelisk_rt_wait_entry_v1, 2> entries;
  } record{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 2, 0, 0},
           {{{16, OBELISK_RT_WAIT_EDGE_POSEDGE, 8},
             {80, OBELISK_RT_WAIT_EDGE_NEGEDGE, 4}}}};

  ScheduledProcess process;
  process.token = 7;
  process.instance = reinterpret_cast<obelisk_rt_process_instance_v1 *>(1);
  process.started = true;
  process.suspendKind = OBELISK_RT_SUSPEND_EDGE;
  context->scheduledProcesses.push_back(std::move(process));
  ScheduledDesignTask task;
  task.id = 9;
  task.started = true;
  task.suspendKind = OBELISK_RT_SUSPEND_EDGE;
  context->scheduledDesignTasks.push_back(std::move(task));

  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &record.wait,
      context->scheduledProcesses.back().signalSubscriptions,
      context->scheduledProcesses.back().signalLatch, 7, false));
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &record.wait,
      context->scheduledDesignTasks.back().signalSubscriptions,
      context->scheduledDesignTasks.back().signalLatch, 9, true));
  EXPECT_EQ(context->scheduledProcesses.back().signalSubscriptions.size(), 2u);
  EXPECT_EQ(context->scheduledDesignTasks.back().signalSubscriptions.size(),
            2u);
  EXPECT_EQ(context->signalDiagnostics.subscriptionsCurrent, 4u);
  EXPECT_EQ(context->signalDiagnostics.subscriptionsHighWater, 4u);

  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 18, 1, OBELISK_RT_SIGNAL_CHANGE));
  EXPECT_FALSE(context->scheduledProcesses.back().signalLatch->triggered);
  EXPECT_FALSE(context->scheduledDesignTasks.back().signalLatch->triggered);
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 18, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  EXPECT_TRUE(context->scheduledProcesses.back().signalLatch->triggered);
  EXPECT_TRUE(context->scheduledDesignTasks.back().signalLatch->triggered);
  EXPECT_EQ(context->nativePollCandidates.count(7), 1u);
  EXPECT_EQ(context->designPollCandidates.count(9), 1u);

  obelisk_rt_unregister_signal_wait_unlocked(
      context, context->scheduledProcesses.back().signalSubscriptions);
  obelisk_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks.back().signalSubscriptions);
  EXPECT_TRUE(context->signalSubscriptionBuckets.empty());
  EXPECT_EQ(context->signalDiagnostics.subscriptionsCurrent, 0u);
  EXPECT_EQ(context->signalDiagnostics.publications, 2u);
  EXPECT_EQ(context->signalDiagnostics.subscribersExamined, 8u);

  struct {
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entry;
  } positiveRecord{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0},
                   {17, OBELISK_RT_WAIT_EDGE_POSEDGE, 1}},
      negativeRecord{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0},
                     {17, OBELISK_RT_WAIT_EDGE_NEGEDGE, 1}};
  std::vector<std::unique_ptr<SignalSubscription>> positiveSubscriptions;
  std::vector<std::unique_ptr<SignalSubscription>> negativeSubscriptions;
  std::unique_ptr<SignalWaitLatch> positiveLatch;
  std::unique_ptr<SignalWaitLatch> negativeLatch;
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &positiveRecord.wait, positiveSubscriptions, positiveLatch));
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &negativeRecord.wait, negativeSubscriptions, negativeLatch));
  const uint8_t changedTransitions = 0b11;
  const uint8_t posedgeTransitions = 0b01;
  const uint8_t negedgeTransitions = 0b10;
  ASSERT_TRUE(obelisk_rt_publish_signal_transition_batch_unlocked(
      context, 16, 2, &changedTransitions, &posedgeTransitions,
      &negedgeTransitions));
  EXPECT_FALSE(positiveLatch->triggered);
  EXPECT_TRUE(negativeLatch->triggered);
  obelisk_rt_unregister_signal_wait_unlocked(context, positiveSubscriptions);
  obelisk_rt_unregister_signal_wait_unlocked(context, negativeSubscriptions);

  struct PageWait {
    struct {
      obelisk_rt_wait_record_v1 wait;
      obelisk_rt_wait_entry_v1 entry;
    } record{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_CHANGE, 0, 1, 0, 0},
             {0, OBELISK_RT_WAIT_EDGE_CHANGE, 1}};
    std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
    std::unique_ptr<SignalWaitLatch> latch;
  };
  std::vector<PageWait> pageWaits(65);
  for (size_t index = 0; index != pageWaits.size(); ++index) {
    pageWaits[index].record.entry.stable_id =
        index == 0 ? 16 : index * 256 + 16;
    ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
        context, &pageWaits[index].record.wait, pageWaits[index].subscriptions,
        pageWaits[index].latch));
  }
  uint64_t examinedBefore = context->signalDiagnostics.subscribersExamined;
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(pageWaits[0].latch->triggered);
  for (size_t index = 1; index != pageWaits.size(); ++index)
    EXPECT_FALSE(pageWaits[index].latch->triggered);
  EXPECT_EQ(context->signalDiagnostics.subscribersExamined - examinedBefore,
            1u);
  for (PageWait &waiter : pageWaits)
    obelisk_rt_unregister_signal_wait_unlocked(context, waiter.subscriptions);

  PageWait wideWait;
  wideWait.record.entry.reserved = UINT32_MAX;
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &wideWait.record.wait, wideWait.subscriptions, wideWait.latch));
  ASSERT_EQ(wideWait.subscriptions.size(), 1u);
  EXPECT_EQ(wideWait.subscriptions.front()->bucketSlots.size(), 1u);
  examinedBefore = context->signalDiagnostics.subscribersExamined;
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 8192, 1, OBELISK_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(wideWait.latch->triggered);
  EXPECT_EQ(context->signalDiagnostics.subscribersExamined - examinedBefore,
            1u);
  obelisk_rt_unregister_signal_wait_unlocked(context, wideWait.subscriptions);

  struct {
    obelisk_rt_computed_wait_record_v1 wait{};
    std::array<obelisk_rt_computed_dependency_v1, 2> dependencies{};
  } computed;
  computed.wait.dependency_count = computed.dependencies.size();
  computed.wait.dependencies_offset = sizeof(computed.wait);
  computed.dependencies[0] = {16, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
  computed.dependencies[1] = {4096, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
  std::vector<std::unique_ptr<SignalSubscription>> computedSubscriptions;
  std::unique_ptr<SignalWaitLatch> computedLatch;
  ASSERT_TRUE(obelisk_rt_register_computed_signal_wait_unlocked(
      context, &computed.wait, 42, false, computedSubscriptions,
      computedLatch));
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 0u);
  examinedBefore = context->signalDiagnostics.subscribersExamined;
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE));
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE));
  EXPECT_EQ(context->pendingNativeComputedWaiters, std::vector<uint64_t>({42}));
  EXPECT_TRUE(context->pendingDesignComputedWaiters.empty());
  EXPECT_EQ(context->signalDiagnostics.subscribersExamined - examinedBefore,
            2u);
  obelisk_rt_unregister_signal_wait_unlocked(context, computedSubscriptions, 42,
                                             false);
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 0u);
  context->pendingNativeComputedWaiters.clear();

  struct {
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entry;
  } objectRecord{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_CHANGE, 0, 1, 0, 0},
                 {0, OBELISK_RT_WAIT_EDGE_CHANGE, 4}};
  std::vector<std::unique_ptr<SignalSubscription>> objectSubscriptions;
  std::unique_ptr<SignalWaitLatch> objectLatch;
  objectRecord.entry.stable_id =
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, 3, 4);
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &objectRecord.wait, objectSubscriptions, objectLatch));
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context,
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, 4, 5), 1,
      OBELISK_RT_SIGNAL_CHANGE));
  EXPECT_FALSE(objectLatch->triggered);
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context,
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, 3, 5), 1,
      OBELISK_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(objectLatch->triggered);
  obelisk_rt_unregister_signal_wait_unlocked(context, objectSubscriptions);

  objectRecord.entry.stable_id =
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_AUTOMATIC, 7, 4);
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &objectRecord.wait, objectSubscriptions, objectLatch));
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context,
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_AUTOMATIC, 8, 5),
      1, OBELISK_RT_SIGNAL_CHANGE));
  EXPECT_FALSE(objectLatch->triggered);
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context,
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_AUTOMATIC, 7, 5),
      1, OBELISK_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(objectLatch->triggered);
  obelisk_rt_unregister_signal_wait_unlocked(context, objectSubscriptions);

  context->scheduledProcesses.back().instance = nullptr;
  context->nativeConditionalSignalWaiters.insert(UINT64_C(0xdeadbeef));
  ASSERT_TRUE(obelisk_rt_append_signal_event_unlocked(context, 16, false, false,
                                                      true, false, false));
  ASSERT_TRUE(obelisk_rt_append_signal_event_unlocked(
      context, 4096, false, false, true, false, false));
  ASSERT_EQ(context->signalValueSnapshots.size(), 2u);
  obelisk_rt_invalidate_signal_snapshots_unlocked(context, 16, 1);
  EXPECT_EQ(context->signalValueSnapshots.count(16), 0u);
  EXPECT_EQ(context->signalValueSnapshots.count(4096), 1u);
  obelisk_rt_invalidate_signal_snapshots_unlocked(context, 0, 8192);
  EXPECT_TRUE(context->signalValueSnapshots.empty());

  for (unsigned iteration = 0; iteration != 1000; ++iteration) {
    ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
        context, &record.wait,
        context->scheduledDesignTasks.back().signalSubscriptions,
        context->scheduledDesignTasks.back().signalLatch));
    obelisk_rt_unregister_signal_wait_unlocked(
        context, context->scheduledDesignTasks.back().signalSubscriptions);
  }
  EXPECT_TRUE(context->signalSubscriptionBuckets.empty());
  EXPECT_EQ(context->signalDiagnostics.subscriptionsCurrent, 0u);
  EXPECT_EQ(context->signalDiagnostics.subscriptionsHighWater, 65u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceCohortsFinalizeByProducerWaveAndRemainTokenLocal) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);

  struct ClockWait {
    obelisk_rt_wait_record_v1 wait;
    std::array<obelisk_rt_wait_entry_v1, 2> entries;
  } first{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
           OBELISK_RT_WAIT_CLOCK_OCCURRENCE, 2, 71, 0},
          {{{16, OBELISK_RT_WAIT_EDGE_POSEDGE, 1},
            {32, OBELISK_RT_WAIT_EDGE_POSEDGE, 1}}}},
      second{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
              OBELISK_RT_WAIT_CLOCK_OCCURRENCE, 2, 72, 0},
             {{{48, OBELISK_RT_WAIT_EDGE_POSEDGE, 1},
               {64, OBELISK_RT_WAIT_EDGE_POSEDGE, 1}}}};

  auto addTask = [&](uint64_t token, ClockWait &record) {
    ScheduledDesignTask task;
    task.id = token;
    task.started = true;
    task.suspendKind = OBELISK_RT_SUSPEND_EDGE;
    context->scheduledDesignTaskIndices[token] =
        context->scheduledDesignTasks.size();
    context->scheduledDesignTasks.push_back(std::move(task));
    ScheduledDesignTask &scheduled = context->scheduledDesignTasks.back();
    ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
        context, &record.wait, scheduled.signalSubscriptions,
        scheduled.signalLatch, token, true));
  };
  addTask(11, first);
  addTask(12, second);
  ASSERT_TRUE(context->clockOccurrences);
  ASSERT_EQ(context->clockOccurrences->waits.size(), 2u);

  context->schedulerTime = 9;
  context->schedulerSlotProgress = 20;
  context->activeExecRegion = OBELISK_RT_REGION_ACTIVE;
  context->activeLogicalProcessToken = 11;
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  // The coordinator can run before another producer boundary, but a partial
  // cohort from the still-open publication wave is deliberately invisible.
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 71), 0u);
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 71), 0u);

  ++context->schedulerSlotProgress;
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 71), 3u);
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 72), 0u);

  // Two occurrences of clock 0 in one producer wave form ordinal cohorts;
  // they are not coalesced merely because simulation time and region match.
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 71), 3u);
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 71), 1u);

  // Same-time publications separated by a producer boundary are distinct,
  // even when no simulation time advances.
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 71), 1u);
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 71), 2u);

  // Region changes are part of the exact occurrence key as well. Active and
  // Reactive publications cannot fuse merely because time/progress match in a
  // synthetic or nested scheduler handoff.
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  context->activeExecRegion = OBELISK_RT_REGION_REACTIVE;
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 71), 1u);
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 71), 2u);

  // An exact token/site pair owns each queue; one coordinator cannot consume
  // another coordinator's coincident mask.
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 48, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 64, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 72), 0u);
  context->activeLogicalProcessToken = 12;
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 72), 3u);
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 71), 0u);

  obelisk_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks[0].signalSubscriptions, 11, true);
  obelisk_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks[1].signalSubscriptions, 12, true);
  EXPECT_FALSE(context->clockOccurrences);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, NoChangeUsesPayForPlaySignedOpenWindows) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  EXPECT_FALSE(context->clockOccurrences);
  EXPECT_FALSE(context->noChangeChecks);
  context->clockOccurrences = std::make_unique<ClockOccurrenceFeatureState>();
  context->activeLogicalProcessToken = 41;

  auto update = [&](uint64_t site, uint64_t time, uint64_t mask, int64_t start,
                    int64_t end) {
    context->schedulerTime = time;
    return obelisk_rt_v1_nochange_update(context, site, mask, start, end);
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
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions, 41, true);
  ASSERT_TRUE(context->noChangeChecks);
  EXPECT_EQ(context->noChangeChecks->checks.size(), 1u);
  EXPECT_FALSE(context->clockOccurrences);
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions, 42, true);
  EXPECT_FALSE(context->noChangeChecks);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ClockOccurrenceStateIsLazyAndOrdinaryWaitLayoutStable) {
  static_assert(sizeof(void *) != 8 || sizeof(SignalSubscription) == 72);
  static_assert(sizeof(obelisk_rt_wait_record_v1) == 32);
  static_assert(sizeof(void *) != 8 ||
                sizeof(ClockOccurrenceFeatureState) == 240);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
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
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entry;
  } ordinary{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
              OBELISK_RT_WAIT_FLAGS_NONE, 1, 0, 0},
             {16, OBELISK_RT_WAIT_EDGE_POSEDGE, 1}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &ordinary.wait, subscriptions, latch, 9, false));
  ASSERT_EQ(subscriptions.size(), 1u);
  EXPECT_FALSE(context->clockOccurrences);
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  EXPECT_FALSE(context->clockOccurrences);
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions, 9, false);
  EXPECT_FALSE(context->clockOccurrences);

  struct {
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entry;
  } slotFinal{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
               OBELISK_RT_WAIT_CLOCK_OCCURRENCE |
                   OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL,
               1, 91, 0},
              {32, OBELISK_RT_WAIT_EDGE_POSEDGE, 1}};
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &slotFinal.wait, subscriptions, latch, 10, false));
  EXPECT_TRUE(context->clockOccurrences);
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions, 10, false);
  EXPECT_FALSE(context->clockOccurrences);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ReplaceableTimingEventIsLazyBoundedSaturatingAndPermanentlyGuarded) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);

  // This service is owned by a slot-final timing coordinator.  Its feature
  // state consequently shares the exact lifecycle of that coordinator's
  // clock-occurrence wait and is absent from ordinary designs.
  context->clockOccurrences = std::make_unique<ClockOccurrenceFeatureState>();
  context->clockOccurrences->waits.try_emplace(21);
  context->activeLogicalProcessToken = 21;
  ASSERT_FALSE(context->clockOccurrences->replaceableEvents);

  context->schedulerTime = 7;
  for (uint64_t restart = 0; restart != 10000; ++restart) {
    obelisk_rt_v1_scheduler_event_replace_after(context, 91, 1, 100 + restart);
    ASSERT_EQ(context->schedulerStatus, OBELISK_RT_OK);
    ASSERT_TRUE(context->clockOccurrences->replaceableEvents);
    EXPECT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(),
              1u);
    EXPECT_EQ(context->clockOccurrences->replaceableEvents->pending.size(), 1u);
    // Restart replacement is indexed and never enters the ordinary delayed
    // vector, so its live population cannot grow or induce a linear scan.
    EXPECT_TRUE(context->scheduledDesignEvents.empty());
  }
  obelisk_rt_v1_scheduler_event_replace_after(context, 91, 0, 0);
  EXPECT_TRUE(context->clockOccurrences->replaceableEvents->calendar.empty());
  EXPECT_EQ(context->clockOccurrences->replaceableEvents->pending.size(), 1u);

  context->schedulerTime = UINT64_MAX - 2;
  obelisk_rt_v1_scheduler_event_replace_after(context, 91, 1, 10);
  ASSERT_EQ(context->schedulerStatus, OBELISK_RT_OK);
  ASSERT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(), 1u);
  EXPECT_EQ(context->clockOccurrences->replaceableEvents->calendar.begin()
                ->first.first,
            UINT64_MAX);

  auto &pending = context->clockOccurrences->replaceableEvents->pending.at(91);
  pending.generation = UINT64_MAX;
  obelisk_rt_v1_scheduler_event_replace_after(context, 91, 0, 0);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(pending.generation, UINT64_MAX);
  EXPECT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(), 1u);

  // Exhaustion is permanent: clearing the status cannot permit generation
  // wrap to make an ancient maturity current again.
  context->schedulerStatus = OBELISK_RT_OK;
  obelisk_rt_v1_scheduler_event_replace_after(context, 91, 1, 1);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(pending.generation, UINT64_MAX);
  EXPECT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(), 1u);

  context->schedulerStatus = OBELISK_RT_OK;
  context->nextSchedulerSequence = 0;
  obelisk_rt_v1_scheduler_event_replace_after(context, 92, 1, 1);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(
      context->clockOccurrences->replaceableEvents->pending.at(92).generation,
      0u);
  EXPECT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(), 1u);

  context->activeLogicalProcessToken = 0;
  context->clockOccurrences.reset();
  EXPECT_FALSE(context->clockOccurrences);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ReplaceableTimingEventTeardownIsTokenLocalAndRejectsInvalidOwners) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->clockOccurrences = std::make_unique<ClockOccurrenceFeatureState>();
  context->clockOccurrences->waits.try_emplace(31);
  context->clockOccurrences->waits.try_emplace(32);

  // Coordinator A owns a permanently saturated deadline while coordinator B
  // owns an ordinary live deadline. Both records share the cold calendar but
  // retain independent clock-wait lifetimes.
  context->activeLogicalProcessToken = 31;
  obelisk_rt_v1_scheduler_event_replace_after(context, 301, 1, UINT64_MAX);
  ASSERT_EQ(context->schedulerStatus, OBELISK_RT_OK);
  context->activeLogicalProcessToken = 32;
  obelisk_rt_v1_scheduler_event_replace_after(context, 302, 1, 5);
  ASSERT_EQ(context->schedulerStatus, OBELISK_RT_OK);
  auto &feature = *context->clockOccurrences->replaceableEvents;
  ASSERT_EQ(feature.calendar.size(), 2u);
  ASSERT_EQ(feature.pending.size(), 2u);
  ASSERT_EQ(feature.ownedTimers.size(), 2u);

  // The compiler-private service is valid only in the active coordinator
  // that owns a registered clock-occurrence wait. A second coordinator may
  // not replace another coordinator's static timer event.
  context->activeLogicalProcessToken = 0;
  obelisk_rt_v1_scheduler_event_replace_after(context, 303, 1, 1);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_INVALID_LIFECYCLE);
  context->schedulerStatus = OBELISK_RT_OK;
  context->activeLogicalProcessToken = 33;
  obelisk_rt_v1_scheduler_event_replace_after(context, 303, 1, 1);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_INVALID_LIFECYCLE);
  context->schedulerStatus = OBELISK_RT_OK;
  context->activeLogicalProcessToken = 32;
  obelisk_rt_v1_scheduler_event_replace_after(context, 301, 1, 1);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_INVALID_LIFECYCLE);
  context->schedulerStatus = OBELISK_RT_OK;
  EXPECT_EQ(feature.calendar.size(), 2u);
  EXPECT_EQ(feature.pending.size(), 2u);

  std::vector<std::unique_ptr<SignalSubscription>> noSubscriptions;
  obelisk_rt_unregister_signal_wait_unlocked(context, noSubscriptions, 31,
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->schedulerTime, 5u);
  EXPECT_EQ(context->events.count(301), 0u);
  ASSERT_EQ(context->events.count(302), 1u);
  EXPECT_NE(context->events.at(302).generation, 0u);
  EXPECT_TRUE(remaining.calendar.empty());
  EXPECT_EQ(remaining.pending.size(), 1u);

  obelisk_rt_unregister_signal_wait_unlocked(context, noSubscriptions, 32,
                                             true);
  EXPECT_FALSE(context->clockOccurrences);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceWaitReuseComparesCompleteNativeAndDesignRecords) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  struct ClockWait {
    obelisk_rt_wait_record_v1 wait;
    std::array<obelisk_rt_wait_entry_v1, 4> entries;
  } record{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
            OBELISK_RT_WAIT_CLOCK_OCCURRENCE, 4, 81, 3},
           {{{16, OBELISK_RT_WAIT_EDGE_POSEDGE, 1},
             {32, OBELISK_RT_WAIT_EDGE_NEGEDGE, 1},
             {48, OBELISK_RT_WAIT_EDGE_NONE, 1},
             {64, OBELISK_RT_WAIT_EDGE_NONE, 1}}}};

  auto exercise = [&](uint64_t token, bool design) {
    std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
    std::unique_ptr<SignalWaitLatch> latch;
    ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
        context, &record.wait, subscriptions, latch, token, design));
    ASSERT_TRUE(obelisk_rt_same_clock_occurrence_wait_unlocked(
        context, &record.wait, token, design));

    ClockWait changed = record;
    ++changed.wait.payload;
    EXPECT_FALSE(obelisk_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));
    changed = record;
    changed.wait.auxiliary = 1;
    EXPECT_FALSE(obelisk_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));
    changed = record;
    changed.entries[0].reserved = 2;
    EXPECT_FALSE(obelisk_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));
    changed = record;
    changed.entries[1].edge = OBELISK_RT_WAIT_EDGE_POSEDGE;
    EXPECT_FALSE(obelisk_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));
    changed = record;
    changed.entries[3].stable_id = 80;
    EXPECT_FALSE(obelisk_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));
    changed = record;
    changed.entries[2].reserved = 2;
    EXPECT_FALSE(obelisk_rt_same_clock_occurrence_wait_unlocked(
        context, &changed.wait, token, design));

    obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions, token,
                                               design);
    EXPECT_FALSE(obelisk_rt_same_clock_occurrence_wait_unlocked(
        context, &record.wait, token, design));
    EXPECT_FALSE(context->clockOccurrences);
    ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
        context, 16, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
    EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OK);
    EXPECT_FALSE(context->clockOccurrences);
    ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
        context, &record.wait, subscriptions, latch, token, design));
    EXPECT_TRUE(obelisk_rt_same_clock_occurrence_wait_unlocked(
        context, &record.wait, token, design));
    obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions, token,
                                               design);
    EXPECT_FALSE(context->clockOccurrences);
  };
  exercise(21, false);
  exercise(22, true);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ClockOccurrenceWaitAccepts64AndRejects65Clocks) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  struct ClockWait {
    obelisk_rt_wait_record_v1 wait;
    std::array<obelisk_rt_wait_entry_v1, 65> entries;
  } record{};
  record.wait = {OBELISK_RT_VERSION,
                 OBELISK_RT_SUSPEND_EDGE,
                 OBELISK_RT_WAIT_CLOCK_OCCURRENCE,
                 64,
                 82,
                 0};
  for (auto &entry : record.entries)
    entry = {16, OBELISK_RT_WAIT_EDGE_POSEDGE, 1};

  auto exercise = [&](uint64_t token, bool design) {
    std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
    std::unique_ptr<SignalWaitLatch> latch;
    ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
        context, &record.wait, subscriptions, latch, token, design));
    ASSERT_TRUE(context->clockOccurrences);
    EXPECT_EQ(
        context->clockOccurrences->subscriptions
            .at(design ? token : OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | token)
            .size(),
        64u);
    obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions, token,
                                               design);
    EXPECT_FALSE(context->clockOccurrences);

    record.wait.count = 65;
    EXPECT_FALSE(obelisk_rt_register_signal_wait_unlocked(
        context, &record.wait, subscriptions, latch, token, design));
    EXPECT_FALSE(context->clockOccurrences);
    record.wait.count = 64;
  };
  exercise(31, false);
  exercise(32, true);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ClockOccurrenceIffSamplesCommittedPublicationValue) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->stateValue.assign(2, 0);
  context->stateUnknown.assign(2, 0);

  struct ClockWait {
    obelisk_rt_wait_record_v1 wait;
    std::array<obelisk_rt_wait_entry_v1, 3> entries;
  } record{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
            OBELISK_RT_WAIT_CLOCK_OCCURRENCE, 3, 73, 1},
           {{{16, OBELISK_RT_WAIT_EDGE_POSEDGE, 1},
             {32, OBELISK_RT_WAIT_EDGE_POSEDGE, 1},
             {80, OBELISK_RT_WAIT_EDGE_NONE, 1}}}};

  ScheduledDesignTask task;
  task.id = 13;
  task.started = true;
  task.suspendKind = OBELISK_RT_SUSPEND_EDGE;
  context->scheduledDesignTaskIndices[task.id] = 0;
  context->scheduledDesignTasks.push_back(std::move(task));
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &record.wait,
      context->scheduledDesignTasks.front().signalSubscriptions,
      context->scheduledDesignTasks.front().signalLatch, 13, true));
  ASSERT_TRUE(context->clockOccurrences);
  EXPECT_EQ(context->clockOccurrences->conditionalWaitCount, 1u);

  context->activeLogicalProcessToken = 13;
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 73), 2u);

  context->stateValue[1] |= uint64_t{1} << 16;
  ASSERT_TRUE(obelisk_rt_append_signal_event_unlocked(context, 80, false, false,
                                                      true, false));
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 32, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 73), 3u);

  obelisk_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks.front().signalSubscriptions, 13,
      true);
  EXPECT_FALSE(context->clockOccurrences);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceObserverConditionIsActorLocalAndCaptureExact) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);

  struct ObserverClockWait {
    obelisk_rt_wait_record_v1 wait;
    std::array<obelisk_rt_wait_entry_v1, 2> entries;
    obelisk_rt_computed_capture_v1 capture;
  } record{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
            OBELISK_RT_WAIT_CLOCK_OCCURRENCE |
                OBELISK_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS,
            2, 97, 1},
           {{{16, OBELISK_RT_WAIT_EDGE_POSEDGE, 1},
             {71, OBELISK_RT_WAIT_CONDITION_OBSERVER, 1}}},
           {32, 3, 5, 7}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &record.wait, subscriptions, latch, 19, false));
  ASSERT_TRUE(context->clockOccurrences);
  EXPECT_TRUE(context->signalSubscriptionBuckets.empty());
  EXPECT_EQ(context->nativeDynamicSignalSubscriptions, 0u);
  EXPECT_TRUE(context->pendingNativeComputedWaiters.empty());
  EXPECT_TRUE(context->nativeConditionalSignalWaiters.empty());
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 0u);
  EXPECT_TRUE(obelisk_rt_same_clock_occurrence_wait_unlocked(
      context, &record.wait, 19, false));

  // IEEE 1800-2017 31.7 samples the compiled condition only after the
  // controlled event matches. Its captures therefore belong to this exact
  // actor-local clock wait: they are neither subscriptions nor a reusable
  // wait when any ABI word differs.
  const obelisk_rt_computed_capture_v1 original = record.capture;
  auto differs = [&](uint64_t &word) {
    ++word;
    EXPECT_FALSE(obelisk_rt_same_clock_occurrence_wait_unlocked(
        context, &record.wait, 19, false));
    record.capture = original;
  };
  differs(record.capture.stable_id);
  differs(record.capture.payload0);
  differs(record.capture.payload1);
  differs(record.capture.payload2);

  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions, 19, false);
  EXPECT_FALSE(context->clockOccurrences);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     StaticEvalIslandRejectsOnlyGeneratedClockOccurrencePrimaries) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->clockOccurrences = std::make_unique<ClockOccurrenceFeatureState>();

  constexpr uint64_t token = 29;
  constexpr uint32_t generatedState = 7;
  constexpr uint32_t dormantState = 8;
  auto &wait = context->clockOccurrences->waits[token];
  wait.conditions.resize(1);
  wait.conditions[0].stableID = obelisk_rt_stable_handle_encode(
      OBELISK_RT_STABLE_HANDLE_STATIC, generatedState, 0);
  auto primary = std::make_unique<ClockOccurrenceSubscription>();
  primary->stableID = obelisk_rt_stable_handle_encode(
      OBELISK_RT_STABLE_HANDLE_STATIC, dormantState, 0);
  primary->bitWidth = 1;
  context->clockOccurrences->subscriptions[token].push_back(std::move(primary));

  std::unordered_set<uint32_t> generatedWritableStates{generatedState};
  // Clause 31.7 condition reads do not wake the coordinator. A condition-only
  // overlap with generated state therefore leaves the dormant primary safe.
  EXPECT_FALSE(nativeClockOccurrencePrimaryReadsGeneratedState(
      context, generatedWritableStates));

  context->clockOccurrences->subscriptions[token].front()->stableID =
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC,
                                      generatedState, 0);
  EXPECT_TRUE(nativeClockOccurrencePrimaryReadsGeneratedState(
      context, generatedWritableStates));

  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ClockConditionPublicationViewMergesOnlyCapturedOverlap) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 64;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
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
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, globalPlane, 64, 16,
                                                  1, 0, 0, &value),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 0);

  // IEEE 1800-2017 31.7 instead exposes the post-transition value and X plane
  // when the condition capture aliases the controlled publication.
  publication = {16, 8, 0, &publishedValue, &publishedUnknown};
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, globalPlane, 64, 16,
                                                  8, 0, 0, &value),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, globalPlane, 64, 16,
                                                  8, 1, 0, &unknown),
            OBELISK_RT_OK);
  EXPECT_EQ(value, publishedValue);
  EXPECT_EQ(unknown, publishedUnknown);

  // A negative view base must not hide its in-bounds publication overlap.
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 64),
            OBELISK_RT_OK);
  context->stateUnknown[0] = 0;
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  publication.stableID = obelisk_rt_v1_native_handle_offset(root, 16);
  std::array<uint8_t, 4> partialValue{}, partialUnknown{};
  uint64_t partial = obelisk_rt_v1_native_handle_offset(root, -3);
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, globalPlane, 64, partial, 32, 0, 0,
                partialValue.data()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, globalPlane, 64, partial, 32, 1, 1,
                partialUnknown.data()),
            OBELISK_RT_OK);
  EXPECT_EQ(loadPackedBytes(partialValue.data(), 0, 32),
            uint64_t{publishedValue} << 19);
  EXPECT_EQ(loadPackedBytes(partialUnknown.data(), 0, 32),
            (uint64_t{publishedUnknown} << 19) | 7);

  context->conditionPublication = nullptr;
  context->observerForcesCanonicalPlane = false;
  context->clockOccurrences.reset();
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceCustomDescriptorUsesOnlyLazyFeatureStateAndCoalesces) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->stateValue.assign(1, 0);
  context->stateUnknown.assign(1, 0);

  struct {
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entry;
  } record{
      {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
       OBELISK_RT_WAIT_CLOCK_OCCURRENCE, 1, 74, 0},
      {16, OBELISK_RT_WAIT_EDGE_TRANSITION_MASK | (1u << 0) | (1u << 3), 4}};
  ScheduledDesignTask task;
  task.id = 14;
  task.started = true;
  task.suspendKind = OBELISK_RT_SUSPEND_EDGE;
  context->scheduledDesignTaskIndices[task.id] = 0;
  context->scheduledDesignTasks.push_back(std::move(task));
  context->activeLogicalProcessToken = 14;
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
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
  ASSERT_TRUE(obelisk_rt_publish_signal_transition_batch_unlocked(
      context, 16, 4, &changed, &posedge, &negedge, 0, nullptr));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 74), 1u);

  // 1x is the other selected Clause 31.5 class.
  context->stateUnknown[0] |= uint64_t{0xf} << 16;
  posedge = 0;
  ASSERT_TRUE(obelisk_rt_publish_signal_transition_batch_unlocked(
      context, 16, 4, &changed, &posedge, &negedge, 0, nullptr));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 74), 1u);

  obelisk_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks.front().signalSubscriptions, 14,
      true);
  EXPECT_FALSE(context->clockOccurrences);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceCustomDescriptorRollsBackPartialRegistration) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->stateValue.assign(1, 0);
  context->stateUnknown.assign(1, 0);
  struct {
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entries[2];
  } record{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
            OBELISK_RT_WAIT_CLOCK_OCCURRENCE, 2, 75, 0},
           {{16, OBELISK_RT_WAIT_EDGE_TRANSITION_MASK | 1u, 4},
            {UINT64_MAX, OBELISK_RT_WAIT_EDGE_TRANSITION_MASK | 1u, 4}}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;

  EXPECT_FALSE(obelisk_rt_register_signal_wait_unlocked(
      context, &record.wait, subscriptions, latch, 15, false));
  EXPECT_TRUE(subscriptions.empty());
  EXPECT_EQ(context->nativeDynamicSignalSubscriptions, 0u);
  EXPECT_FALSE(context->clockOccurrences);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ClockOccurrenceWideCustomDescriptorCoalescesPartialPageOverlapAndCleans) {
  constexpr uint64_t width = 65 * kSignalSubscriptionPageBits;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->stateValue.assign((width + 63) / 64, 0);
  context->stateUnknown.assign((width + 63) / 64, 0);
  struct {
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entry;
  } record{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
            OBELISK_RT_WAIT_CLOCK_OCCURRENCE, 1, 76, 0},
           {0, OBELISK_RT_WAIT_EDGE_TRANSITION_MASK | 1u,
            static_cast<uint32_t>(width)}};
  ScheduledDesignTask task;
  task.id = 16;
  task.started = true;
  task.suspendKind = OBELISK_RT_SUSPEND_EDGE;
  context->scheduledDesignTaskIndices[task.id] = 0;
  context->scheduledDesignTasks.push_back(std::move(task));
  context->activeLogicalProcessToken = 16;
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
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
  ASSERT_TRUE(obelisk_rt_publish_signal_transition_batch_unlocked(
      context, 255, 3, &changed, &posedge, &negedge, 0, nullptr));
  ++context->schedulerSlotProgress;
  EXPECT_EQ(obelisk_rt_v1_clock_occurrence_consume(context, 76), 1u);

  obelisk_rt_unregister_signal_wait_unlocked(
      context, context->scheduledDesignTasks.front().signalSubscriptions, 16,
      true);
  EXPECT_FALSE(context->clockOccurrences);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, DirectSignalWaitCanSuppressActiveSelfPublication) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);

  struct {
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entry;
  } record{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_CHANGE,
            OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF, 1, 0, 0},
           {16, OBELISK_RT_WAIT_EDGE_CHANGE, 1}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;
  constexpr uint64_t token = 7;
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &record.wait, subscriptions, latch, token, false));

  context->activeLogicalProcessToken =
      OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | token;
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE));
  EXPECT_FALSE(latch->triggered);
  EXPECT_EQ(context->nativePollCandidates.count(token), 0u);

  context->activeLogicalProcessToken = 0;
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE));
  EXPECT_TRUE(latch->triggered);
  EXPECT_EQ(context->nativePollCandidates.count(token), 1u);

  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, ConditionalWakeUpdatesSchedulerSelectionGeneration) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);

  constexpr uint32_t automaticID = 3;
  uint64_t watched = obelisk_rt_stable_handle_encode(
      OBELISK_RT_STABLE_HANDLE_AUTOMATIC, automaticID, 0);
  uint64_t condition = obelisk_rt_stable_handle_encode(
      OBELISK_RT_STABLE_HANDLE_AUTOMATIC, automaticID, 1);
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
  task.suspendKind = OBELISK_RT_SUSPEND_EDGE;
  task.waitOffset = 0;
  task.waitSize =
      sizeof(obelisk_rt_wait_record_v1) + 2 * sizeof(obelisk_rt_wait_entry_v1);
  task.frame.resize(task.waitSize);
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(task.frame.data());
  *wait = {OBELISK_RT_VERSION,
           OBELISK_RT_SUSPEND_EDGE,
           OBELISK_RT_WAIT_EDGE_IFF,
           2,
           0,
           0};
  auto *entries = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  entries[0] = {watched, OBELISK_RT_WAIT_EDGE_POSEDGE, 1};
  entries[1] = {condition, OBELISK_RT_WAIT_EDGE_NONE, 1};
  context->scheduledDesignTasks.push_back(std::move(task));
  context->scheduledDesignTaskIndices[11] = 0;
  context->designConditionalSignalWaiters.insert(11);

  uint64_t selectionGeneration = context->schedulerSelectionGeneration;
  ASSERT_TRUE(obelisk_rt_latch_conditional_signal_waiters_unlocked(
      context, watched, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
  EXPECT_TRUE(context->scheduledDesignTasks.front().signalTriggered);
  EXPECT_EQ(context->designPollCandidates.count(11), 1u);
  EXPECT_NE(context->schedulerSelectionGeneration, selectionGeneration);

  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, SignalWaitsAreSelectiveAndEdgeAware) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture fixture(1);
  schedulerWaitKind = OBELISK_RT_SUSPEND_EDGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_POSEDGE;
  schedulerWaitHandle = 16;
  schedulerWaitWidth = 8;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  obelisk_rt_v1_scheduler_signal(context, 80, 1, OBELISK_RT_SIGNAL_POSEDGE);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);
  obelisk_rt_v1_scheduler_signal(context, 18, 1, OBELISK_RT_SIGNAL_CHANGE);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);
  obelisk_rt_v1_scheduler_signal(
      context, 18, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, ClockOccurrenceWaitExitReclaimsFeatureState) {
  for (bool changesWait : {false, true}) {
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
    ClockOccurrenceLifecycleFixture fixture(changesWait ? 81 : 80);
    clockOccurrenceLifecycleChangesWait = changesWait;
    clockOccurrenceLifecycleResumes = 0;
    ASSERT_EQ(obelisk_rt_v1_scheduler_add(
                  context, makeClockOccurrenceLifecycleInstance(fixture), 0),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
    ASSERT_TRUE(context->clockOccurrences);
    ASSERT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(),
              1u);
    ASSERT_FALSE(
        context->scheduledProcesses.front().signalSubscriptions.front());

    context->activeLogicalProcessToken =
        OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG |
        context->scheduledProcesses.front().token;
    obelisk_rt_v1_scheduler_event_replace_after(context, 191, 1, 100);
    context->activeLogicalProcessToken = 0;
    ASSERT_TRUE(context->clockOccurrences->replaceableEvents);
    ASSERT_EQ(context->clockOccurrences->replaceableEvents->calendar.size(),
              1u);

    ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
        context, 16, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
    ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
        context, 32, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
    ++context->schedulerSlotProgress;
    ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
    EXPECT_EQ(clockOccurrenceLifecycleResumes, 1u);
    if (!changesWait) {
      // Re-entering the identical wait reuses the exact token/site record.
      ASSERT_TRUE(context->clockOccurrences);
      ASSERT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(),
                1u);
      ASSERT_FALSE(
          context->scheduledProcesses.front().signalSubscriptions.front());
      ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
          context, 16, 1,
          OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
      ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
          context, 32, 1,
          OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
      ++context->schedulerSlotProgress;
      ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
      EXPECT_EQ(clockOccurrenceLifecycleResumes, 2u);
    }
    EXPECT_FALSE(context->clockOccurrences);

    // The old sources are no longer subscribed after either termination or a
    // change to an ordinary direct wait. Publishing them again is inert and
    // cannot resurrect a stale feature record.
    ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
        context, 16, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
    ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
        context, 32, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
    ++context->schedulerSlotProgress;
    ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
    EXPECT_FALSE(context->clockOccurrences);
    if (changesWait) {
      ASSERT_EQ(context->scheduledProcesses.size(), 1u);
      EXPECT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(),
                1u);
      ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
          context, 48, 1,
          OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE));
      ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
      EXPECT_EQ(clockOccurrenceLifecycleResumes, 2u);
    }
    EXPECT_TRUE(context->scheduledProcesses.empty());
    EXPECT_FALSE(context->clockOccurrences);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, PrimeFindsTailAndNonTailActors) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture first(1);
  SchedulerFixture second(2);
  schedulerWaitKind = OBELISK_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = 700;
  schedulerWaitWidth = 1;
  obelisk_rt_process_instance_v1 *firstInstance = makeSchedulerInstance(first);
  obelisk_rt_process_instance_v1 *secondInstance =
      makeSchedulerInstance(second);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, firstInstance, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, secondInstance, 0),
            OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_scheduler_prime(context, firstInstance),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_prime(context, secondInstance),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledProcesses[0].started);
  EXPECT_TRUE(context->scheduledProcesses[1].started);
  EXPECT_EQ(context->scheduledProcesses[0].suspendKind,
            OBELISK_RT_SUSPEND_CHANGE);
  EXPECT_EQ(context->scheduledProcesses[1].suspendKind,
            OBELISK_RT_SUSPEND_CHANGE);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSignalCohortPreservesOrderingChildrenAndCompaction) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
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
    ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  id == 1 ? 0 : static_cast<uint32_t>(id)),
              OBELISK_RT_OK);
  }
  for (uint64_t id : {uint64_t{30}, uint64_t{31}}) {
    fixtures.push_back(std::make_unique<SchedulerFixture>(id));
    fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
    ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()),
                  OBELISK_RT_SCHEDULE_PRIORITY_SIGNAL, 100),
              OBELISK_RT_OK);
  }
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_TRUE(schedulerOrder.empty());

  obelisk_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 OBELISK_RT_SIGNAL_CHANGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  std::vector<uint64_t> expected{30, 31, 1, 100, 101};
  for (uint64_t id = 2; id <= 20; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  EXPECT_EQ(context->schedulerDeadProcessCount, 0u);
  cachedCohortOrdinaryChild = nullptr;
  cachedCohortUrgentChild = nullptr;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedEqualRankSignalCohortPreservesInsertionOrder) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
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
    ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0, 0),
              OBELISK_RT_OK);
  }
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  obelisk_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 OBELISK_RT_SIGNAL_CHANGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  std::vector<uint64_t> expected;
  for (uint64_t id = 1; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSignalCohortInvalidatesForNewPriorityPublication) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
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
    ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  static_cast<uint32_t>(id)),
              OBELISK_RT_OK);
  }
  fixtures.push_back(std::make_unique<SchedulerFixture>(40));
  fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(*fixtures.back()),
                OBELISK_RT_SCHEDULE_PRIORITY_SIGNAL, 100),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  obelisk_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 OBELISK_RT_SIGNAL_CHANGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  std::vector<uint64_t> expected{1, 40};
  for (uint64_t id = 2; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedReactiveSignalCohortYieldsToSameSlotNBA) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
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
    ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()),
                  OBELISK_RT_SCHEDULE_HOME(OBELISK_RT_REGION_REACTIVE),
                  static_cast<uint32_t>(id)),
              OBELISK_RT_OK);
  }
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(context, &plane, nullptr, 8, 0, 8, 0,
                                        &replacement, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 OBELISK_RT_SIGNAL_CHANGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(cachedCohortNBAVisible);
  EXPECT_EQ(plane, replacement);
  EXPECT_EQ(schedulerOrder.size(), 17u);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  cachedCohortNBAPlane = nullptr;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSignalCohortRequeuesUrgentTaskCallToken) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
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
    ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  static_cast<uint32_t>(id)),
              OBELISK_RT_OK);
  }
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  obelisk_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 OBELISK_RT_SIGNAL_CHANGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  std::vector<uint64_t> expected{1, 150, 1};
  for (uint64_t id = 2; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  cachedCohortTaskCallee = nullptr;
  cachedCohortTaskCallID = 0;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedUrgentRequeueRebuildsOldCursorDistances) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
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
    obelisk_rt_process_instance_v1 *instance =
        makeSchedulerInstance(*fixtures.back());
    ASSERT_NE(instance, nullptr);
    instance->continuation = 1;
    ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance,
                                          OBELISK_RT_SCHEDULE_STARTUP),
              OBELISK_RT_OK);
  }
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

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
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSignalCohortReadmitsSameTokenContinue) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
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
    ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  static_cast<uint32_t>(id)),
              OBELISK_RT_OK);
  }
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  obelisk_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 OBELISK_RT_SIGNAL_CHANGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  std::vector<uint64_t> expected{1, 1};
  for (uint64_t id = 2; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  cachedCohortContinueID = 0;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSameTokenPublicationInvalidatesBeforeContinue) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
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
    ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  static_cast<uint32_t>(id)),
              OBELISK_RT_OK);
  }
  fixtures.push_back(std::make_unique<SchedulerFixture>(40));
  fixtures.back()->descriptor.native_execute = cachedSignalCohortExecute;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(*fixtures.back()),
                OBELISK_RT_SCHEDULE_PRIORITY_SIGNAL, 100),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  obelisk_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 OBELISK_RT_SIGNAL_CHANGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  std::vector<uint64_t> expected{1, 40, 1};
  for (uint64_t id = 2; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  cachedCohortContinueID = 0;
  cachedCohortContinuePublishes = false;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CachedSameTokenWaitMovesToSlowUntilEpochReady) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
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
    ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                  context, makeSchedulerInstance(*fixtures.back()), 0,
                  static_cast<uint32_t>(id)),
              OBELISK_RT_OK);
  }
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  obelisk_rt_v1_scheduler_signal(context, cachedCohortPrimarySignal, 1,
                                 OBELISK_RT_SIGNAL_CHANGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  std::vector<uint64_t> expected{1, 2, 1};
  for (uint64_t id = 3; id <= 17; ++id)
    expected.push_back(id);
  EXPECT_EQ(schedulerOrder, expected);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  cachedCohortFrontierID = 0;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, SignalChangedWhileExecutingRetriggersTheSameWait) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture fixture(42);
  fixture.descriptor.native_execute = schedulerSelfTriggerExecute;
  schedulerWaitHandle = 16;
  schedulerWaitWidth = 1;
  schedulerSelfTriggerCount = 0;
  schedulerSelfTriggerStaticState = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  obelisk_rt_v1_scheduler_signal(context, schedulerWaitHandle,
                                 schedulerWaitWidth, OBELISK_RT_SIGNAL_CHANGE);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerSelfTriggerCount, 3u);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticTransitionRetriggersTheExecutingWait) {
  AOTTestState state;
  const obelisk_rt_static_fanout_entry fanout[] = {
      {1, 0, 1, OBELISK_RT_WAIT_EDGE_CHANGE, 1, 0, 0, 1},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.fanout_entries = fanout;
  plan.fanout_entry_count = std::size(fanout);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(43);
  fixture.descriptor.execution = &execution;
  fixture.descriptor.native_execute = schedulerSelfTriggerExecute;
  schedulerWaitHandle = obelisk_rt_v1_native_state_static_handle(1);
  schedulerWaitWidth = 1;
  schedulerSelfTriggerCount = 0;
  schedulerSelfTriggerStaticState = 1;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  constexpr obelisk_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
  };
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);

  uint8_t oldValue = 0;
  uint8_t newValue = 1;
  obelisk_rt_v1_scheduler_signal_transition(
      context, schedulerWaitHandle, 1, &oldValue, nullptr, &newValue, nullptr);
  EXPECT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);
  EXPECT_EQ(schedulerSelfTriggerCount, 3u);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  schedulerSelfTriggerStaticState = 0;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticTransitionCanSuppressTheExecutingWait) {
  AOTTestState state;
  const obelisk_rt_static_fanout_entry fanout[] = {
      {1, 0, 1, OBELISK_RT_WAIT_EDGE_CHANGE, 1,
       OBELISK_RT_FANOUT_SUPPRESS_ACTIVE_SELF, 0, 1},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.fanout_entries = fanout;
  plan.fanout_entry_count = std::size(fanout);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(44);
  fixture.descriptor.execution = &execution;
  fixture.descriptor.native_execute = schedulerSelfTriggerExecute;
  schedulerWaitHandle = obelisk_rt_v1_native_state_static_handle(1);
  schedulerWaitWidth = 1;
  schedulerSelfTriggerCount = 0;
  schedulerSelfTriggerStaticState = 1;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  constexpr obelisk_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
  };
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);

  uint8_t oldValue = 0;
  uint8_t newValue = 1;
  obelisk_rt_v1_scheduler_signal_transition(
      context, schedulerWaitHandle, 1, &oldValue, nullptr, &newValue, nullptr);
  EXPECT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);
  EXPECT_EQ(schedulerSelfTriggerCount, 1u);
  EXPECT_EQ(context->nativeScheduleReadyNodes.findFirst(), UINT32_MAX);
  schedulerSelfTriggerStaticState = 0;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTDirectFragmentRetainsSelfRetrigger) {
  AOTTestState state;
  state.runHook = runDirectSelfTransition;
  const obelisk_rt_static_fanout_entry fanout[] = {
      {1, 0, 1, OBELISK_RT_WAIT_EDGE_CHANGE, 1, 0, 0, 1},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.fanout_entries = fanout;
  plan.fanout_entry_count = std::size(fanout);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(45);
  fixture.descriptor.execution = &execution;
  fixture.descriptor.native_execute = schedulerSelfTriggerExecute;
  schedulerWaitHandle = obelisk_rt_v1_native_state_static_handle(1);
  schedulerWaitWidth = 1;
  schedulerSelfTriggerCount = 0;
  schedulerSelfTriggerStaticState = 1;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  constexpr obelisk_rt_native_schedule_node initial[] = {
      {0, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
  };
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot_nodes(context, initial,
                                                  std::size(initial)),
            OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_TRUE(context->scheduledProcesses.front().signalTriggered);
  EXPECT_TRUE(context->nativeScheduleReadyNodes.test(1));
  schedulerSelfTriggerStaticState = 0;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CompactBytecodeUsesDirectSignalSubscriptions) {
  SchedulerFixture fixture(31);
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE, 0,
                    OBELISK_RT_SUSPEND_EDGE, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<obelisk_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  obelisk_rt_bytecode_v1 bytecode{code.data(),
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
  fixture.descriptor.available_tiers = OBELISK_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.native_requirements = nullptr;
  fixture.descriptor.native_execute = nullptr;
  fixture.descriptor.native_destroy = nullptr;
  fixture.descriptor.bytecode = &bytecode;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->signalDiagnosticsEnabled = true;
  obelisk_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0};
  *entry = {16, OBELISK_RT_WAIT_EDGE_NEGEDGE, 8};
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(), 1u);

  obelisk_rt_v1_scheduler_signal(
      context, 18, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(), 1u);
  obelisk_rt_v1_scheduler_signal(
      context, 18, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledProcesses.empty() ||
              context->scheduledProcesses.front().signalSubscriptions.empty());
  EXPECT_TRUE(context->signalSubscriptionBuckets.empty());
  EXPECT_EQ(context->signalDiagnostics.subscriptionsHighWater, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, ImmediateAndDeferredEventsWakeOnlyTheirWaiters) {
  for (uint32_t nonblocking : {0u, 1u}) {
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
    SchedulerFixture fixture(2 + nonblocking);
    schedulerWaitKind = OBELISK_RT_SUSPEND_EVENT;
    schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_NONE;
    schedulerWaitHandle = 42;
    schedulerWaitWidth = 0;
    schedulerResumeCount = 0;
    ASSERT_EQ(
        obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
    obelisk_rt_v1_scheduler_event(context, 41, nonblocking);
    EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
    EXPECT_EQ(schedulerResumeCount, 0u);
    obelisk_rt_v1_scheduler_event(context, 42, nonblocking);
    EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
    EXPECT_EQ(schedulerResumeCount, 1u);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, EventTriggeredSpansExactlyOneTimeSlot) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_event_triggered(context, 91), 0u);

  obelisk_rt_v1_scheduler_event(context, 91, 0);
  EXPECT_EQ(obelisk_rt_v1_scheduler_event_triggered(context, 91), 1u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_event_triggered(context, 92), 0u);

  SchedulerFixture fixture(91);
  schedulerWaitKind = OBELISK_RT_SUSPEND_DELAY;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_event_triggered(context, 91), 0u);

  obelisk_rt_v1_scheduler_event(context, 91, 1);
  EXPECT_EQ(obelisk_rt_v1_scheduler_event_triggered(context, 91), 0u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_event_triggered(context, 91), 1u);

  obelisk_rt_v1_scheduler_event_after(context, 93, 1, 4);
  EXPECT_EQ(obelisk_rt_v1_scheduler_event_triggered(context, 93), 0u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_event_triggered(context, 91), 0u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_event_triggered(context, 93), 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, TriggeringNullEventHasNoEffect) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);

  EXPECT_TRUE(context->events.empty());
  obelisk_rt_v1_scheduler_event(context, UINT64_MAX, 0);
  EXPECT_TRUE(context->events.empty());
  EXPECT_EQ(obelisk_rt_v1_scheduler_event_triggered(context, UINT64_MAX), 0u);

  obelisk_rt_v1_scheduler_event_after(context, UINT64_MAX, 1, 7);
  EXPECT_TRUE(context->scheduledDesignEvents.empty());
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->events.empty());
  EXPECT_EQ(obelisk_rt_v1_scheduler_event_triggered(context, UINT64_MAX), 0u);

  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, WaitActionPayloadSelectsTheExactFrameField) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture fixture(5);
  schedulerWaitKind = OBELISK_RT_SUSPEND_EVENT;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_NONE;
  schedulerWaitHandle = 73;
  schedulerWaitWidth = 0;
  schedulerWaitOffset = 48;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  obelisk_rt_v1_scheduler_event(context, 73, 0);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, ComputeGraphRanksOrderRunnableProcesses) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture later(100);
  SchedulerFixture earlier(200);
  schedulerOrder.clear();
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(later), 0, 9),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(earlier), 0, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{200, 100}));
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, DynamicNativeChildrenInheritPhaseThroughIndexedParent) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture fixture(250);

  // Keep the active parent behind a large existing cohort. Child creation
  // must use the token index rather than rescanning that cohort per child.
  constexpr uint32_t cohortSize = 4096;
  for (uint32_t index = 0; index != cohortSize; ++index)
    ASSERT_EQ(
        obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
        OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *parent = makeSchedulerInstance(fixture);
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, parent, OBELISK_RT_SCHEDULE_FINAL),
      OBELISK_RT_OK);
  uint64_t parentToken = context->scheduledProcesses.back().token;
  context->activeNativeProcess = parent;
  context->activeLogicalProcessToken =
      OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | parentToken;

  size_t firstChild = context->scheduledProcesses.size();
  for (uint32_t index = 0; index != cohortSize; ++index)
    ASSERT_EQ(
        obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
        OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledProcessIndices.size(),
            context->scheduledProcesses.size());
  for (size_t index = firstChild; index != context->scheduledProcesses.size();
       ++index)
    EXPECT_EQ(context->scheduledProcesses[index].phase, 1u);

  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, UnstartedPostponedActorsDoNotPreemptActiveResumes) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture active(10);
  SchedulerFixture monitor(200);
  schedulerWaitKind = OBELISK_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = 16;
  schedulerWaitWidth = 1;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(active), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_TRUE(schedulerOrder.empty());

  // An actor that has never run does take its first activation ahead of
  // signal resumptions, but only within its own region: a postponed actor
  // that preempted this active resume would observe the values from before
  // the active region ran, and would leave the Postponed region reachable a
  // second time in the same time slot (IEEE 1800-2017 4.4.2.9).
  obelisk_rt_v1_scheduler_signal(context, schedulerWaitHandle,
                                 schedulerWaitWidth, OBELISK_RT_SIGNAL_CHANGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(
                context, makeSchedulerInstance(monitor),
                OBELISK_RT_SCHEDULE_HOME(OBELISK_RT_REGION_POSTPONED)),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{10, 200}));
  EXPECT_EQ(schedulerResumeCount, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, PlannedContinuationRanksApplyAfterResume) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture later(10);
  SchedulerFixture earlier(20);
  schedulerWaitKind = OBELISK_RT_SUSPEND_DELAY;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_NONE;
  schedulerWaitHandle = 0;
  schedulerWaitWidth = 0;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  const uint32_t continuation = 1;
  const uint32_t laterRank = 9;
  const uint32_t earlierRank = 1;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_planned(context, makeSchedulerInstance(later),
                                          0, 0, &continuation, &laterRank, 1),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_planned(
                context, makeSchedulerInstance(earlier), 0, 0, &continuation,
                &earlierRank, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{20, 10}));
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTPlanInstallBindRunAndExclusiveMutableState) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  plan.promotion_ready = schedulerPromotionReady;
  schedulerPromotionInvalidationCount = 0;
  schedulerPromotionReadyCount = 0;
  obelisk_rt_context *first = nullptr;
  obelisk_rt_context *second = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&first), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_context_create(&second), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(first, &plan), OBELISK_RT_OK);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 1u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(first, &plan),
            OBELISK_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(second, &plan),
            OBELISK_RT_INVALID_LIFECYCLE);
  obelisk_rt_native_schedule_plan undersized = plan;
  undersized.mutable_state_size = sizeof(void *);
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(second, &undersized),
            OBELISK_RT_INVALID_ARGUMENT);
  undersized = plan;
  --undersized.size;
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(second, &undersized),
            OBELISK_RT_INVALID_ARGUMENT);

  SchedulerFixture fixture(100);
  obelisk_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(first, instance, 0, 0, 7, nullptr,
                                            nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  EXPECT_EQ(state.actors[0], instance);
  EXPECT_NE(obelisk_rt_v1_scheduler_process_token(first, instance), 0u);

  obelisk_rt_process_instance_v1 *duplicate = makeSchedulerInstance(fixture);
  ASSERT_NE(duplicate, nullptr);
  EXPECT_EQ(obelisk_rt_v1_scheduler_add_aot(first, duplicate, 0, 0, 7, nullptr,
                                            nullptr, 0, nullptr, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(duplicate), OBELISK_RT_OK);

  schedulerOrder.clear();
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(first), OBELISK_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{100}));
  EXPECT_EQ(state.actors[0], nullptr);
  obelisk_rt_v1_context_destroy(first);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 2u);

  state.actors = {};
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(second, &plan), OBELISK_RT_OK);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 3u);
  EXPECT_EQ(second->nativeScheduleNBARootCount, 0u);
  EXPECT_EQ(second->nativeScheduleNBASiteCount, 0u);
  obelisk_rt_v1_context_destroy(second);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 4u);
  // Installation and release reset promotion state but never scan it; only a
  // reached quiescent transient handback may query readiness.
  EXPECT_EQ(schedulerPromotionReadyCount, 0u);
}

TEST(Scheduler, PeriodicWriteFootprintTracksInstalledPlanOwnership) {
  AOTTestState state;
  uint64_t ingress = 0;
  uint64_t active = 3;
  uint8_t value = 0, unknown = 0;
  constexpr uint32_t sparseNBAState = UINT32_C(0x10000000);
  const obelisk_rt_native_clock_kernel clocks[] = {
      {1, OBELISK_RT_WAIT_EDGE_POSEDGE, 0, 1, &ingress, 1, 0, &active},
  };
  obelisk_rt_native_merged_fragment merged[] = {
      {0, 1, 0, 0, 0, 0, generatedCheckpointCallback},
      {1, 1, 0, 1, 1, 0, nullptr},
  };
  const obelisk_rt_static_actor_root roots[] = {
      {0, 1, OBELISK_RT_STATIC_ROOT_READ, 0},
      {0, 2, OBELISK_RT_STATIC_ROOT_WRITE, 0},
      {1, 3, OBELISK_RT_STATIC_ROOT_WRITE, 0},
  };
  const obelisk_rt_static_nba_root nba[] = {{2, sparseNBAState, 1, nullptr}};
  auto plan = makeAOTPlan(state);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
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
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 4;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  for (uint32_t id = 1; id <= 3; ++id)
    ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, id, id - 1, 1),
              OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, sparseNBAState,
                                                       3, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  // Include direct-owner writes and all NBA roots, not read-only dependencies
  // or runtime-only writers. Sparse IDs must not require a huge dense bitmap.
  EXPECT_EQ(context->nativePeriodicGeneratedWritableStates,
            (std::unordered_set<uint32_t>{2, sparseNBAState}));
  obelisk_rt_release_native_schedule_plan(context);
  EXPECT_TRUE(context->nativePeriodicGeneratedWritableStates.empty());
  // The image may be reused with new ownership only after release. Its cache
  // belongs to this installation, never to the plan pointer or process-global
  // mutable state address.
  merged[0].execute = nullptr;
  merged[1].execute = generatedCheckpointCallback;
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  EXPECT_EQ(context->nativePeriodicGeneratedWritableStates,
            (std::unordered_set<uint32_t>{3, sparseNBAState}));
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTCheckpointRunsOneRuntimeActionAndReentersNatively) {
  AOTTestState state;
  state.runHook = runCheckpointThenReenter;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(321);
  schedulerOrder.clear();
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.runCalls, 2u);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{321}));
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, SharedDriverArbitratesPlanAndDescriptorContinuations) {
  AOTTestState state;
  auto plan = makeAOTPlan(state);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  SchedulerFixture native(1), interpreted(2), descriptor(3);
  schedulerWaitKind = OBELISK_RT_SUSPEND_DELAY;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE, 0,
                    OBELISK_RT_SUSPEND_DELAY, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<obelisk_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  obelisk_rt_bytecode_v1 bytecode{};
  bytecode.code = code.data();
  bytecode.code_size = code.size();
  bytecode.entries = entries.data();
  bytecode.entry_count = entries.size();
  bytecode.register_count = 1;
  bytecode.register_offset = interpreted.layout.frame_size;
  interpreted.descriptor.available_tiers |= OBELISK_RT_TIER_MASK_BYTECODE;
  interpreted.descriptor.bytecode = &bytecode;
  auto *nativeInstance = makeSchedulerInstance(native);
  auto *bytecodeInstance = makeSchedulerInstance(interpreted);
  auto *wait =
      reinterpret_cast<obelisk_rt_wait_record_v1 *>(bytecodeInstance->frame);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_DELAY, 0, 0, 17, 0};
  uint32_t bytecodeEntry = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context, nativeInstance, 0, 0, 3,
                                            nullptr, nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context, bytecodeInstance, 0, 1, 2,
                                            nullptr, nullptr, 0, &bytecodeEntry,
                                            1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(descriptor), 0, 1),
            OBELISK_RT_OK);
  // Deliberately scramble node layout: scheduler rank governs execution.
  const obelisk_rt_native_schedule_node nodes[] = {{1, 1, UINT32_MAX},
                                                   {0, 0, UINT32_MAX},
                                                   {0, 1, UINT32_MAX},
                                                   {1, 0, UINT32_MAX}};
  ASSERT_EQ(initializeNativeAOTNodesUnlocked(context, nodes, std::size(nodes)),
            OBELISK_RT_OK);
  uint8_t futurePlane = 0, replacement = 9;
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(context, &futurePlane, nullptr, 8, 0, 8,
                                        99, &replacement, nullptr),
            OBELISK_RT_OK);
  {
    NativeAOTContextScope active(context);
    NativeAOTMutexScope locked(context);
    ASSERT_EQ(runScheduler(context, {true, true}), OBELISK_RT_OK);
    EXPECT_EQ(context->schedulerTime, 0u);
    EXPECT_EQ(nativeInstance->continuation, 1u);
    EXPECT_EQ(bytecodeInstance->continuation, 1u);
    EXPECT_EQ(bytecodeInstance->tier, OBELISK_RT_TIER_BYTECODE);
    EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 2u);
    EXPECT_EQ(futurePlane, 0u);
    EXPECT_EQ(context->scheduledNBAs.size(), 1u);
    EXPECT_TRUE(schedulerOrder.empty());
    ASSERT_EQ(runScheduler(context, {true, false}), OBELISK_RT_OK);
  }
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{3, 2, 1}));
  EXPECT_EQ(schedulerResumeCount, 3u);
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 4u);
  EXPECT_EQ(context->schedulerTime, 99u);
  EXPECT_EQ(futurePlane, 9u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, SharedDriverReactivatesPlanAfterEachSameSlotNBA) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  AOTTestState state;
  auto plan = makeAOTPlan(state, 1);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  SchedulerFixture fixture(5);
  fixture.descriptor.execution = &execution;
  fixture.descriptor.native_execute =
      [](obelisk_rt_process_instance_v1 *instance) {
        if (instance->continuation == 0)
          return schedulerExecute(instance);
        auto *context = instance->context;
        uint8_t value = context->stateValue[0];
        schedulerOrder.push_back(value);
        EXPECT_EQ(context->stateUnknown[0] & 255, 0u);
        if (value == 3)
          return schedulerExecute(instance);
        uint8_t next = value + 1;
        auto status = obelisk_rt_v1_scheduler_nba(
            context, &nbaDummyPlane, nullptr, 8, schedulerWaitHandle, 8, 0,
            &next, nullptr);
        *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                             OBELISK_RT_SUSPEND_CHANGE,
                             1,
                             OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                             0,
                             48};
        return status;
      };
  schedulerWaitKind = OBELISK_RT_SUSPEND_CHANGE;
  schedulerWaitHandle = obelisk_rt_v1_native_state_static_handle(1);
  schedulerWaitWidth = 8;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_CHANGE;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  nbaDummyPlane = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  const obelisk_rt_native_schedule_node nodes[] = {{0, 0, UINT32_MAX},
                                                   {0, 1, UINT32_MAX}};
  ASSERT_EQ(initializeNativeAOTNodesUnlocked(context, nodes, std::size(nodes)),
            OBELISK_RT_OK);
  uint8_t first = 1;
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(context, &nbaDummyPlane, nullptr, 8,
                                        schedulerWaitHandle, 8, 0, &first,
                                        nullptr),
            OBELISK_RT_OK);
  {
    NativeAOTContextScope active(context);
    NativeAOTMutexScope locked(context);
    ASSERT_EQ(runScheduler(context, {true, true}), OBELISK_RT_OK);
  }
  // Three separate NBA transitions, each followed by Active reactivation;
  // the final entry records source-process termination exactly once.
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{1, 2, 3, 5}));
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->schedulerTime, 0u);
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 4u);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedCheckpointValidatesAndConsumesExactContinuation) {
  AOTTestState state;
  state.runHook = runGeneratedCheckpoint;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(22);
  schedulerWaitKind = OBELISK_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = 17;
  schedulerWaitWidth = 1;
  schedulerWaitOffset = 0;
  generatedCheckpointCallbackCount = 0;
  invalidGeneratedCheckpointStatus = OBELISK_RT_OK;
  validGeneratedCheckpointStatus = OBELISK_RT_INVALID_CONTINUATION;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  // Establish the source wait before the generated transaction takes direct
  // ownership of continuation 1.
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(invalidGeneratedCheckpointStatus, OBELISK_RT_INVALID_CONTINUATION);
  EXPECT_EQ(validGeneratedCheckpointStatus, OBELISK_RT_OK);
  EXPECT_EQ(generatedCheckpointCallbackCount, 2u);
  EXPECT_EQ(state.runCalls, 2u);
  EXPECT_EQ(context->schedulerPreponedTime, 17u);
  EXPECT_EQ(context->schedulerSlotProgress, 2u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->nativeScheduleCheckpointActorSlot, UINT32_MAX);
  EXPECT_EQ(context->nativeScheduleCheckpointContinuation, 0u);
  EXPECT_EQ(context->nativeScheduleCheckpointCallback, nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTTimedCheckpointCommitsSameSlotNBAAndReentersNatively) {
  AOTTestState state;
  state.runHook = runTimedCheckpointThenReenter;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  uint8_t plane = 0;
  uint8_t replacement = 0xa5;
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(context, &plane, nullptr, 8, 0, 8, 5,
                                        &replacement, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.runCalls, 2u);
  EXPECT_EQ(context->schedulerTime, 5u);
  EXPECT_EQ(plane, replacement);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBAQueueGrowsWithoutLosingOrderOrUnknowns) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  obelisk::runtime::EvalNBAQueue queue;
  for (uint32_t i = 0; i != 513; ++i) {
    ASSERT_EQ(obelisk_rt_v1_eval_nba_reserve(context, &queue), OBELISK_RT_OK);
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
  ASSERT_EQ(obelisk_rt_v1_eval_nba_reserve(context, &queue), OBELISK_RT_OK);
  EXPECT_EQ(queue.data, storage);
  queue.data[queue.size++] = {99, 0, 0, UINT64_MAX};
  EXPECT_EQ(queue.data[0].site, 99u);
  queue.error = OBELISK_RT_OUT_OF_MEMORY;
  obelisk_rt_v1_context_destroy(context);
  EXPECT_EQ(queue.data, nullptr);
  EXPECT_EQ(queue.capacity, 0u);
  EXPECT_EQ(queue.size, 0u);
  EXPECT_EQ(queue.error, OBELISK_RT_OK);
}

TEST(Scheduler, GeneratedNBAQueueRejectsMalformedOrExhaustedDescriptors) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  obelisk::runtime::EvalNBAQueue queue;
  EXPECT_EQ(obelisk_rt_v1_eval_nba_reserve(nullptr, &queue),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_eval_nba_reserve(context, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  queue.size = 1;
  EXPECT_EQ(obelisk_rt_v1_eval_nba_reserve(context, &queue),
            OBELISK_RT_INVALID_ARGUMENT);
  obelisk::runtime::EvalNBARecord sentinel{};
  queue = {&sentinel, UINT32_MAX, UINT32_MAX};
  EXPECT_EQ(obelisk_rt_v1_eval_nba_reserve(context, &queue),
            OBELISK_RT_OUT_OF_RESOURCES);
  EXPECT_TRUE(context->nativeEvalNBAQueues.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, IndexedClockKernelIngressMaintainsReadySetAcrossReentry) {
  for (uint32_t capacity : {65u, 129u, 2049u, 4097u}) {
    SCOPED_TRACE(capacity);
    uint32_t words = (capacity + 63) / 64;
    const auto layout = obelisk::runtime::clockKernelReadySetLayout(words);
    std::vector<uint64_t> ingress(layout.storageWords);
    obelisk::runtime::ReadySetView ready(ingress.data(), layout);
    ready.clear();
    AOTTestState state;
    obelisk_rt_native_clock_kernel clock{
        1,
        OBELISK_RT_WAIT_EDGE_POSEDGE,
        0,
        1,
        ingress.data(),
        words,
        obelisk::runtime::indexedClockKernelReadySet};
    obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
    plan.clock_kernels = &clock;
    plan.clock_kernel_count = 1;
    obelisk_rt_native_merged_fragment merged{0, 0, 0, capacity - 1, 0, 0};
    plan.merged_fragments = &merged;
    plan.merged_fragment_count = 1;
    plan.timeslot_coordinator = clockCoordinator;
    obelisk_rt_execution_descriptor_v1 execution{};
    execution.version = OBELISK_RT_VERSION;
    execution.state_bit_count = 1;
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
              OBELISK_RT_OK);
    clock.reserved =
        2; // Unknown index layout must be rejected, not read as leaves.
    EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
              OBELISK_RT_INVALID_ARGUMENT);
    clock.reserved = obelisk::runtime::indexedClockKernelReadySet;
    ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
              OBELISK_RT_OK);
    for (unsigned iteration = 0; iteration != 3; ++iteration) {
      ASSERT_EQ(obelisk_rt_v1_scheduler_activate_clock_kernel(context, 0,
                                                              capacity - 1),
                OBELISK_RT_OK);
      EXPECT_EQ(ready.findFirst(), capacity - 1);
      ASSERT_EQ(obelisk_rt_v1_scheduler_activate_clock_kernel(context, 0, 0),
                OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_scheduler_activate_clock_kernel(context, 0, 0),
                OBELISK_RT_OK);
      EXPECT_EQ(ready.popFirst(), 0u);
      EXPECT_EQ(ready.popFirst(), capacity - 1);
      EXPECT_EQ(ready.findFirst(), UINT32_MAX);
    }
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, AOTClockKernelIngressSuppressesDuplicateBits) {
  AOTTestState state;
  uint64_t ingress[2] = {};
  const obelisk_rt_native_clock_kernel clocks[] = {
      {1, OBELISK_RT_WAIT_EDGE_POSEDGE, 0, 1, ingress, 2, 0},
  };
  const obelisk_rt_native_merged_fragment merged[] = {
      {0, 0, 0, 65, 0, 0},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.clock_kernels = clocks;
  plan.clock_kernel_count = std::size(clocks);
  plan.merged_fragments = merged;
  plan.merged_fragment_count = std::size(merged);
  plan.timeslot_coordinator = clockCoordinator;

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_scheduler_activate_clock_kernel(context, 0, 65),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_activate_clock_kernel(context, 0, 65),
            OBELISK_RT_OK);
  EXPECT_EQ(ingress[0], 0u);
  EXPECT_EQ(ingress[1], 2u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_activate_clock_kernel(context, 0, 128),
            OBELISK_RT_INVALID_ARGUMENT);

  clockCoordinatorIngress = ingress;
  clockCoordinatorWords = std::size(ingress);
  clockCoordinatorCalls = 0;
  // A VPI deposit inside an active scheduler/checkpoint publishes ingress but
  // must not recursively run the clock loop. The outer drain consumes it once
  // the current actor has returned.
  context->nativeScheduleRunning = true;
  EXPECT_EQ(obelisk_rt_v1_scheduler_run_clock_coordinator(context),
            OBELISK_RT_OK);
  EXPECT_EQ(clockCoordinatorCalls, 0u);
  EXPECT_EQ(ingress[1], 2u);
  context->nativeScheduleRunning = false;
  obelisk_rt_process_instance_v1 active{};
  context->activeNativeProcess = &active;
  EXPECT_EQ(obelisk_rt_v1_scheduler_run_clock_coordinator(context),
            OBELISK_RT_OK);
  EXPECT_EQ(clockCoordinatorCalls, 0u);
  EXPECT_EQ(ingress[1], 2u);
  context->activeNativeProcess = nullptr;
  EXPECT_EQ(obelisk_rt_v1_scheduler_run_clock_coordinator(context),
            OBELISK_RT_OK);
  EXPECT_EQ(clockCoordinatorCalls, 1u);
  EXPECT_EQ(ingress[0], 0u);
  EXPECT_EQ(ingress[1], 0u);
  clockCoordinatorIngress = nullptr;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTCleanSuperstepRequiresACompleteStaticPlan) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP;
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_INVALID_ARGUMENT);

  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  EXPECT_TRUE(canUseStaticAOTFanout(context));
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticEvalIslandIsDistinctFromFullyStatic) {
  AOTTestState state;
  uint64_t ingress = 0;
  uint64_t active = 1;
  const obelisk_rt_native_clock_kernel clocks[] = {
      {1, OBELISK_RT_WAIT_EDGE_POSEDGE, 0, 1, &ingress, 1, 0, &active},
  };
  const obelisk_rt_native_merged_fragment merged[] = {
      {0, 1, 0, 0, 0, 0, generatedCheckpointCallback},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.clock_kernels = clocks;
  plan.clock_kernel_count = std::size(clocks);
  plan.merged_fragments = merged;
  plan.merged_fragment_count = std::size(merged);
  plan.timeslot_coordinator = clockCoordinator;
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  plan.promotion_ready = schedulerPromotionReady;
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_EVAL |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_EVAL_ISLAND |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS;

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_INVALID_ARGUMENT);

  plan.flags |= OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  // The bootstrap may enter scheduler_run_aot_nodes from inside periodic
  // preparation. It must retain publication until the primary-overlap audit
  // has certified this island; a full static plan above remains unaffected.
  EXPECT_FALSE(context->nativeStaticEvalIslandCertified);
  EXPECT_FALSE(canUseStaticAOTFanout(context));
  context->nativeStaticEvalIslandCertified = true;
  EXPECT_TRUE(canUseStaticAOTFanout(context));
  obelisk_rt_v1_context_destroy(context);

  // The island certifies only its residual eval closure. Claiming the
  // whole-design FULLY_STATIC invariant at the same time is rejected.
  plan.flags |= OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_INVALID_ARGUMENT);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTCleanSuperstepExecutesCertifiedNativeActor) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.run = aotRunOneNode;
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(100);
  fixture.descriptor.execution = &execution;
  schedulerOrder.clear();
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{100}));
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 1u);
  EXPECT_EQ(state.actors[0], nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTCleanSuperstepDirectNodeCallUsesLockedGenericPath) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(100);
  fixture.descriptor.execution = &execution;
  schedulerOrder.clear();
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  constexpr obelisk_rt_native_schedule_node nodes[] = {{0, 0}};
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{100}));
  EXPECT_EQ(state.actors[0], nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTGroupedStaticActivationSuppressesDuplicateWake) {
  AOTTestState state;
  state.runHook = runGroupedStaticActivationNodes;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 2);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture source(90);
  SchedulerFixture target(91);
  source.descriptor.execution = &execution;
  target.descriptor.execution = &execution;
  source.descriptor.native_execute = groupedStaticActivationExecute;
  target.descriptor.native_execute = groupedStaticActivationExecute;
  schedulerOrder.clear();
  schedulerResumeCount = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(source), 0, 0,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(target), 0, 1,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{90, 91}));
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->signalDiagnostics.aotFanoutEntries, 1u);
  EXPECT_EQ(state.actors[0], nullptr);
  EXPECT_EQ(state.actors[1], nullptr);
  obelisk_rt_v1_context_destroy(context);
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
    plan.flags = OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
                 OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
                 OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
                 OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
                 OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
    obelisk_rt_execution_descriptor_v1 execution{};
    execution.version = OBELISK_RT_VERSION;
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
              OBELISK_RT_OK);
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
    ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(
                  context, makeSchedulerInstance(source), 0, 0, 0, nullptr,
                  nullptr, 0, nullptr, 0),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(
                  context, makeSchedulerInstance(target), 0, 1, 0, nullptr,
                  nullptr, 0, nullptr, 0),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
    EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{90, 91}));
    EXPECT_EQ(schedulerResumeCount, 1u);
    EXPECT_EQ(context->signalDiagnostics.aotFanoutEntries, 1u);
    EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 4u);
    EXPECT_EQ(context->nativeScheduleReadyNodes.findFirst(), UINT32_MAX);
    EXPECT_EQ(context->nativeScheduleReadyNodes.capacity(), count);
    obelisk_rt_v1_context_destroy(context);
  }
}

INSTANTIATE_TEST_SUITE_P(Boundaries, SchedulerReadySet,
                         testing::Values(4u, 64u, 65u, 128u, 129u, 2048u, 2049u,
                                         4097u));

TEST(Scheduler, AOTCleanSuperstepSnapshotsContinuationRankForHandover) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.run = aotRunOneNodeThenFallback;
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(4);
  fixture.descriptor.execution = &execution;
  schedulerWaitKind = OBELISK_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = 1;
  schedulerWaitWidth = 1;
  constexpr uint32_t continuation = 1;
  constexpr uint32_t continuationRank = 42;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(
                context, makeSchedulerInstance(fixture), 0, 0, 0, &continuation,
                &continuationRank, 1, nullptr, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 1u);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_EQ(context->scheduledProcesses.front().scheduleRank, continuationRank);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, StaticSpecializationGuardsIntersectOnlyDirtyRoots) {
  AOTTestState state;
  constexpr obelisk_rt_static_actor_root dependencies[] = {
      {0, 1, OBELISK_RT_STATIC_ROOT_READ, 0},
      {1, 2, OBELISK_RT_STATIC_ROOT_WRITE, 0},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  uint8_t stateValue[2] = {};
  uint8_t stateUnknown[2] = {};
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = stateValue;
  plan.state_unknown = stateUnknown;
  plan.state_bit_count = 16;
  plan.actor_roots = dependencies;
  plan.actor_root_count = std::size(dependencies);
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 16;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 8, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture cleanFixture(202);
  cleanFixture.descriptor.execution = &execution;
  obelisk_rt_process_instance_v1 *cleanActor =
      makeSchedulerInstance(cleanFixture);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context, cleanActor, 1, 0, 0,
                                            nullptr, nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  // Tier construction was already validated. Advertising bytecode here is
  // enough to exercise root-local handover selection without building an
  // unrelated interpreter program for this guard test.
  cleanFixture.descriptor.available_tiers |= OBELISK_RT_TIER_MASK_BYTECODE;

  EXPECT_EQ(obelisk_rt_v1_static_specialization_guard(
                context, 0, 1, OBELISK_RT_STATIC_ROOT_READ),
            1u);
  EXPECT_EQ(obelisk_rt_v1_static_specialization_guard(
                context, 1, 2, OBELISK_RT_STATIC_ROOT_WRITE),
            1u);
  EXPECT_EQ(cleanActor->tier, 0u);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_external_write_range_unlocked(context, 0, 8, false);
  }
  EXPECT_EQ(obelisk_rt_v1_static_specialization_guard(
                context, 0, 1, OBELISK_RT_STATIC_ROOT_READ),
            0u);
  EXPECT_EQ(obelisk_rt_v1_static_specialization_guard(
                context, 1, 2, OBELISK_RT_STATIC_ROOT_WRITE),
            1u);

  context->nativeScheduleTransientDirtyRoots.clear();
  std::fill(context->nativeScheduleTransientDirtyMask.begin(),
            context->nativeScheduleTransientDirtyMask.end(), uint64_t{0});
  std::fill(context->nativeScheduleTransientDirtySummary.begin(),
            context->nativeScheduleTransientDirtySummary.end(), uint64_t{0});
  context->nativeScheduleDirtyRootsPresent = false;
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_external_write_range_unlocked(context, 8, 8, true);
  }
  EXPECT_EQ(obelisk_rt_v1_static_specialization_guard(
                context, 1, 2, OBELISK_RT_STATIC_ROOT_WRITE),
            0u);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_release_range_unlocked(context, 8, 8);
  }
  EXPECT_EQ(obelisk_rt_v1_static_specialization_guard(
                context, 1, 2, OBELISK_RT_STATIC_ROOT_WRITE),
            1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, StaticStateRangeIndexPreservesCanonicalHandleSemantics) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 512;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 9, 400, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 7, 100, 300),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 3, 120, 64),
            OBELISK_RT_OK);

  obelisk_rt_stable_handle_v1 decoded{};
  uint64_t handle = obelisk_rt_canonical_state_handle_unlocked(context, 130, 4);
  ASSERT_TRUE(obelisk_rt_stable_handle_decode(handle, &decoded));
  EXPECT_EQ(decoded.kind, OBELISK_RT_STABLE_HANDLE_STATIC);
  EXPECT_EQ(decoded.id, 3u);
  EXPECT_EQ(decoded.offset, 10);
  EXPECT_TRUE(context->nativeStaticStateRangesValid);

  // Registering another range invalidates the lazy index. The lowest static
  // ID remains the canonical choice when multiple roots contain a range.
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 128, 8),
            OBELISK_RT_OK);
  EXPECT_FALSE(context->nativeStaticStateRangesValid);
  handle = obelisk_rt_canonical_state_handle_unlocked(context, 130, 4);
  ASSERT_TRUE(obelisk_rt_stable_handle_decode(handle, &decoded));
  EXPECT_EQ(decoded.kind, OBELISK_RT_STABLE_HANDLE_STATIC);
  EXPECT_EQ(decoded.id, 2u);
  EXPECT_EQ(decoded.offset, 2);

  handle = obelisk_rt_canonical_state_handle_unlocked(context, 450, 4);
  ASSERT_TRUE(obelisk_rt_stable_handle_decode(handle, &decoded));
  EXPECT_EQ(decoded.kind, OBELISK_RT_STABLE_HANDLE_GLOBAL);
  EXPECT_EQ(decoded.offset, 450);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, ExternalDirtyRootIndexSummarizesLeafPages) {
  AOTTestState state;
  uint8_t valuePlane = 0;
  uint8_t unknownPlane = 0;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = &valuePlane;
  plan.state_unknown = &unknownPlane;
  plan.state_bit_count = 2;
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 2;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  for (uint32_t id = 1; id != 65; ++id)
    ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, id, 0, 1),
              OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 65, 1, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  uint64_t root = obelisk_rt_canonical_state_handle_unlocked(context, 1, 1);
  ASSERT_NE(root, UINT64_MAX);

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_external_write_handle_unlocked(context, root, 1, 1, true);
  }
  ASSERT_EQ(context->nativeSchedulePersistentDirtyMask.size(), 2u);
  ASSERT_EQ(context->nativeSchedulePersistentDirtySummary.size(), 1u);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyMask[0], 0u);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyMask[1], uint64_t{1} << 1);
  EXPECT_EQ(context->nativeSchedulePersistentDirtySummary[0], uint64_t{1} << 1);
  EXPECT_EQ(obelisk_rt_v1_static_specialization_guard(
                context, UINT32_MAX, 65, OBELISK_RT_STATIC_ROOT_READ),
            0u);

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_release_range_unlocked(context, 1, 1);
  }
  EXPECT_EQ(context->nativeSchedulePersistentDirtyMask[1], 0u);
  EXPECT_EQ(context->nativeSchedulePersistentDirtySummary[0], 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, ExternalDepositTouchesOnlyIntersectingStaticRoots) {
  AOTTestState state;
  std::array<uint8_t, 2> valuePlane{};
  std::array<uint8_t, 2> unknownPlane{};
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 16;

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 16;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 8, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 3, 4, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  uint64_t handle = obelisk_rt_canonical_state_handle_unlocked(context, 8, 4);
  context->stateValue[0] = UINT64_C(0x0ab0);
  ASSERT_TRUE(obelisk_rt_aot_external_deposit_unlocked(context, handle, 8, 4));
  EXPECT_EQ(valuePlane[0], 0xb0);
  EXPECT_EQ(valuePlane[1] & 0x0f, 0x0a);
  EXPECT_FALSE(context->nativeScheduleDirtyRootsPresent);

  obelisk_rt_aot_external_write_handle_unlocked(context, handle, 8, 4, false);
  EXPECT_EQ(context->nativeScheduleTransientDirtyRoots.count(1), 0u);
  EXPECT_EQ(context->nativeScheduleTransientDirtyRoots.count(2), 1u);
  EXPECT_EQ(context->nativeScheduleTransientDirtyRoots.count(3), 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTSpecializationFastFlagIsScopedAndInvalidated) {
  AOTTestState state;
  uint32_t specializationFast = UINT32_MAX;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE |
               OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION;
  plan.specialization_fast = &specializationFast;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  EXPECT_EQ(specializationFast, 0u);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.observedSpecializationFast, 1u);
  EXPECT_EQ(specializationFast, 0u);

  specializationFast = 1;
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_external_write_unlocked(context);
  }
  EXPECT_EQ(specializationFast, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, VPIObservationDemandIsAColdReversibleAOTHandoff) {
  AOTTestState state;
  uint32_t specializationFast = 1;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE |
               OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION;
  plan.specialization_fast = &specializationFast;
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  schedulerPromotionInvalidationCount = 0;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  schedulerPromotionInvalidationCount = 0;
  specializationFast = 1;

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_observation_demand_changed_unlocked(context, true);
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
    obelisk_rt_aot_observation_demand_changed_unlocked(context, true);
    obelisk_rt_aot_observation_demand_changed_unlocked(context, false);
    refreshNativeStaticSpecializationFastUnlocked(context);
  }
  EXPECT_FALSE(context->vpiObservationDemand);
  EXPECT_TRUE(nativeStaticSpecializationEnvironmentClean(context));
  EXPECT_TRUE(nativeAOTTransientBoundaryClean(context));
  EXPECT_EQ(specializationFast, 1u);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 0u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  context->nativeScheduleRunning = false;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, VPIObservationDemandRoutesAroundTier1UntilReleased) {
  AOTTestState state;
  state.runHook = [](AOTTestState *state, obelisk_rt_context *context) {
    ++state->runCalls;
    return obelisk_rt_v1_scheduler_run(context);
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.runCalls, 1u);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_observation_demand_changed_unlocked(context, true);
  }
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.runCalls, 1u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_observation_demand_changed_unlocked(context, false);
  }
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.runCalls, 2u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTObserverInventoryTracksLiveComputedWaitsOnly) {
  constexpr uint64_t observerID = 101;
  obelisk_rt_observer_descriptor_v1 observer{observerID,
                                             nullptr,
                                             0,
                                             1,
                                             0,
                                             OBELISK_RT_OBSERVER_NO_BYTECODE,
                                             collapsedAliasObserverEvaluator,
                                             0};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.observers = &observer;
  execution.observer_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);

  // A descriptor is immutable evaluator inventory. Clause 31.7 invokes its
  // timing evaluator synchronously from an already-matched clock occurrence,
  // so the mere descriptor must not poison unrelated static AOT closure.
  EXPECT_TRUE(nativeStaticSpecializationEnvironmentClean(context));

  struct ComputedWait {
    obelisk_rt_computed_wait_record_v1 wait{};
    obelisk_rt_computed_dependency_v1 dependency{};
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
  computed.dependency = {37, OBELISK_RT_OBSERVER_DEPENDENCY_EVENT, 1};
  ASSERT_TRUE(obelisk_rt_register_computed_signal_wait_unlocked(
      context, &computed.wait, 43, false, subscriptions, latch));
  EXPECT_FALSE(nativeStaticSpecializationEnvironmentClean(context));
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions, 43, false);
  EXPECT_TRUE(nativeStaticSpecializationEnvironmentClean(context));

  computed.dependency = {41, OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED, 64};
  ASSERT_TRUE(obelisk_rt_register_computed_signal_wait_unlocked(
      context, &computed.wait, 43, false, subscriptions, latch));
  EXPECT_FALSE(nativeStaticSpecializationEnvironmentClean(context));
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions, 43, false);
  EXPECT_TRUE(nativeStaticSpecializationEnvironmentClean(context));

  context->scheduledProcessIndices.clear();
  context->scheduledProcesses.clear();
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, EventOnlyComputedWaitUnregistersWhenNativeProcessTerminates) {
  obelisk_rt_observer_descriptor_v1 observer{eventOnlyComputedObserverID,
                                             nullptr,
                                             0,
                                             1,
                                             0,
                                             OBELISK_RT_OBSERVER_NO_BYTECODE,
                                             eventOnlyComputedEvaluator,
                                             0};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.observers = &observer;
  execution.observer_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);

  EventOnlyComputedFixture fixture(0x7e03);
  fixture.descriptor.execution = &execution;
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  eventOnlyComputedResumeCount = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_TRUE(context->scheduledProcesses.front().signalSubscriptions.empty());
  EXPECT_TRUE(
      context->scheduledProcesses.front().computedObserverWaitRegistered);
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 1u);
  EXPECT_FALSE(nativeStaticSpecializationEnvironmentClean(context));

  obelisk_rt_v1_scheduler_event(context, eventOnlyComputedDependencyID, 0);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(eventOnlyComputedResumeCount, 1u);
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 0u);
  EXPECT_TRUE(nativeStaticSpecializationEnvironmentClean(context));
  EXPECT_TRUE(context->scheduledProcesses.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTSpecializationFastFlagRearmsAfterSlowSlot) {
  AOTTestState state;
  uint32_t specializationFast = 0;
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 256, nullptr},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE |
               OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.specialization_fast = &specializationFast;

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 256;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 256),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  state.runHook = runSpecializationFastRearm;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.observedSpecializationFast, 1u);
  EXPECT_EQ(state.observedSpecializationAfterSlot, 1u);
  EXPECT_EQ(specializationFast, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTNodesValidateInventoryAndExecuteExactActor) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(100);
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 7, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  constexpr obelisk_rt_native_schedule_node duplicateNodes[] = {{0, 0}, {0, 0}};
  EXPECT_EQ(obelisk_rt_v1_scheduler_run_aot_nodes(context, duplicateNodes,
                                                  std::size(duplicateNodes)),
            OBELISK_RT_INVALID_ARGUMENT);
  constexpr obelisk_rt_native_schedule_node missingEntry[] = {{0, 1}};
  EXPECT_EQ(obelisk_rt_v1_scheduler_run_aot_nodes(context, missingEntry,
                                                  std::size(missingEntry)),
            OBELISK_RT_INVALID_CONTINUATION);
  EXPECT_FALSE(context->nativeScheduleSingleStep);
  EXPECT_EQ(context->nativeScheduleForcedSlot, UINT32_MAX);
  EXPECT_FALSE(context->nativeScheduleControlOnly);

  constexpr obelisk_rt_native_schedule_node nodes[] = {{0, 0}};
  schedulerOrder.clear();
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{100}));
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 1u);
  EXPECT_EQ(context->signalDiagnostics.candidateScans, 0u);
  EXPECT_EQ(state.actors[0], nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTFusedReadyNodesRetainStaticNodeOrder) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture first(100);
  SchedulerFixture second(200);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(first), 0, 0,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(second), 0, 1,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  constexpr obelisk_rt_native_schedule_node nodes[] = {
      {0, 0, 7},
      {1, 0, 7},
  };
  schedulerOrder.clear();
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{100, 200}));
  EXPECT_EQ(context->signalDiagnostics.aotNodeExecutions, 2u);
  EXPECT_EQ(context->signalDiagnostics.candidateScans, 0u);
  obelisk_rt_v1_context_destroy(context);
}

// IEEE 1800-2017 10.6.2: a force overrides every driver of its target until a
// release, so a driver that changes behind the override is not a value change.
// Generated code reports the transition it computed from its own plane, which
// does not model the override; the overridden bit must not reach the canonical
// plane, because release resolves the target from its drivers and compares the
// result against that plane to decide whether the value changed.
TEST(Scheduler, NativeTransitionKeepsOverriddenBits) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);

  uint8_t globalValue = 0;
  uint8_t globalUnknown = 0;
  uint8_t forced = 1;
  uint8_t forcedUnknown = 0;
  ASSERT_EQ(obelisk_rt_v1_native_override(
                context, &globalValue, &globalUnknown, 8,
                obelisk_rt_v1_native_handle_offset(root, 1), 1,
                OBELISK_RT_DESCRIPTOR_STORAGE, 0, &forced, &forcedUnknown),
            OBELISK_RT_OK);
  ASSERT_EQ((context->stateValue[0] >> 1) & 1, 1u);

  // A driver behind the override reports bit 1 going to z and bit 2 to one.
  uint8_t oldValue = 1u << 1;
  uint8_t oldUnknown = 0;
  uint8_t newValue = (1u << 1) | (1u << 2);
  uint8_t newUnknown = 1u << 1;
  obelisk_rt_v1_scheduler_signal_transition(
      context, root, 8, &oldValue, &oldUnknown, &newValue, &newUnknown);

  // The forced bit keeps its overridden value; the unforced bit still lands.
  EXPECT_EQ((context->stateValue[0] >> 1) & 1, 1u);
  EXPECT_EQ((context->stateUnknown[0] >> 1) & 1, 0u);
  EXPECT_EQ((context->stateValue[0] >> 2) & 1, 1u);

  // After release the driver value becomes visible again.
  ASSERT_EQ(obelisk_rt_v1_native_release_override(
                context, &globalValue, &globalUnknown, 8,
                obelisk_rt_v1_native_handle_offset(root, 1), 1,
                OBELISK_RT_DESCRIPTOR_STORAGE, 0),
            OBELISK_RT_OK);
  obelisk_rt_v1_scheduler_signal_transition(
      context, root, 8, &oldValue, &oldUnknown, &newValue, &newUnknown);
  EXPECT_EQ((context->stateUnknown[0] >> 1) & 1, 1u);

  obelisk_rt_v1_context_destroy(context);
}

// IEEE 1800-2017 9.4.2: an implicit event is detected on any change in the
// value of its expression, and 10.6.2 makes a force one such change — it
// overrides every driver of the target until a release. The publication that
// installs the override therefore has to reach waiters like any other write.
// It is the one transition the override mask must not filter: the mask exists
// to drop driver transitions arriving behind an established override.
TEST(Scheduler, NativeOverrideWakesWaitersOnTheForcedValue) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);

  SchedulerFixture fixture(5);
  fixture.descriptor.execution = &execution;
  schedulerWaitKind = OBELISK_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = root;
  schedulerWaitWidth = 8;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(schedulerResumeCount, 0u);

  uint8_t globalValue = 0;
  uint8_t globalUnknown = 0;
  uint8_t forced = 1;
  uint8_t forcedUnknown = 0;
  ASSERT_EQ(obelisk_rt_v1_native_override(
                context, &globalValue, &globalUnknown, 8, root, 8,
                OBELISK_RT_DESCRIPTOR_STORAGE, 0, &forced, &forcedUnknown),
            OBELISK_RT_OK);
  EXPECT_EQ(context->stateValue[0] & 0xffu, 1u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);

  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, NativeForceReleaseRestoresAssignedValueOnce) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);

  uint8_t globalValue = 0;
  uint8_t globalUnknown = 0;
  constexpr uint8_t zero = 0;
  constexpr uint8_t one = 1;
  ASSERT_EQ(obelisk_rt_v1_native_override(
                context, &globalValue, &globalUnknown, 1, root, 1,
                OBELISK_RT_DESCRIPTOR_STORAGE, 1, &one, &zero),
            OBELISK_RT_OK);
  ASSERT_EQ(globalValue, 1u);

  // Rebase toggle coverage to the already-active assign. The force contributes
  // one 1->0 transition; release must publish the restored assign as exactly
  // one 0->1 transition even though assignMask remains active.
  ASSERT_EQ(obelisk_rt_v1_coverage_finalize(
                context, 0, 1, &one, &zero,
                OBELISK_RT_COVERAGE_PERSIST_ALL),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 0, 1, root),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_seal(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_override(
                context, &globalValue, &globalUnknown, 1, root, 1,
                OBELISK_RT_DESCRIPTOR_STORAGE, 0, &zero, &zero),
            OBELISK_RT_OK);
  ASSERT_EQ(globalValue, 0u);

  SchedulerFixture fixture(6);
  fixture.descriptor.execution = &execution;
  schedulerWaitKind = OBELISK_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = root;
  schedulerWaitWidth = 1;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(schedulerResumeCount, 0u);

  ASSERT_EQ(obelisk_rt_v1_native_release_override(
                context, &globalValue, &globalUnknown, 1, root, 1,
                OBELISK_RT_DESCRIPTOR_STORAGE, 0),
            OBELISK_RT_OK);
  EXPECT_EQ(globalValue, 1u);
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);

  ASSERT_NE(context->coverage, nullptr);
  ASSERT_EQ(context->coverage->toggleCounters.size(), 4u);
  EXPECT_EQ(context->coverage->toggleCounters[0], 1u);
  EXPECT_EQ(context->coverage->toggleCounters[1], 1u);
  EXPECT_EQ(context->coverage->toggleCounters[2], 0u);
  EXPECT_EQ(context->coverage->toggleCounters[3], 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticFanoutMatchesRangeAndFourStateEdgeExactly) {
  AOTTestState state;
  const obelisk_rt_static_fanout_entry fanout[] = {
      {1, 0, 1, OBELISK_RT_WAIT_EDGE_POSEDGE, 1, 0, 2, 2},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.fanout_entries = fanout;
  plan.fanout_entry_count = std::size(fanout);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->signalDiagnosticsEnabled = true;
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(4);
  fixture.descriptor.execution = &execution;
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  schedulerWaitKind = OBELISK_RT_SUSPEND_EDGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_POSEDGE;
  schedulerWaitHandle = obelisk_rt_v1_native_handle_offset(root, 2);
  schedulerWaitWidth = 2;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  constexpr obelisk_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
  };
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);

  uint8_t oldValue = 0;
  uint8_t newValue = 1;
  obelisk_rt_v1_scheduler_signal_transition(context, root, 8, &oldValue,
                                            nullptr, &newValue, nullptr);
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);

  oldValue = 1u << 2;
  newValue = 0;
  obelisk_rt_v1_scheduler_signal_transition(context, root, 8, &oldValue,
                                            nullptr, &newValue, nullptr);
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);

  oldValue = 0;
  newValue = 0;
  uint8_t oldUnknown = 0;
  uint8_t newUnknown = 1u << 2;
  obelisk_rt_v1_scheduler_signal_transition(
      context, root, 8, &oldValue, &oldUnknown, &newValue, &newUnknown);
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->signalDiagnostics.aotFanoutEntries, 3u);
  // Exact fanout does not publish the out-of-range change or the unmatched
  // negedge. Only the four-state transition that satisfies the posedge wait
  // receives a scheduler sequence.
  EXPECT_EQ(context->signalDiagnostics.publications, 1u);

  uint64_t unobserved = obelisk_rt_v1_native_handle_offset(root, 6);
  oldValue = 0;
  newValue = 1;
  obelisk_rt_v1_scheduler_signal_transition(context, unobserved, 1, &oldValue,
                                            nullptr, &newValue, nullptr);
  EXPECT_EQ(context->signalDiagnostics.publications, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, IndexedExternalDepositResumesFourStateAOTWithoutBytecode) {
  AOTTestState state;
  const obelisk_rt_static_fanout_entry fanout[] = {
      {1, 0, 1, OBELISK_RT_WAIT_EDGE_CHANGE, 1, 0, 0, 1},
  };
  uint8_t valuePlane = 0;
  uint8_t unknownPlane = 0;
  uint64_t ingress = 0;
  const obelisk_rt_native_clock_kernel clocks[] = {
      {1, OBELISK_RT_WAIT_EDGE_CHANGE, 0, 1, &ingress, 1, 0},
  };
  const obelisk_rt_native_merged_fragment merged[] = {
      {0, 1, 0, 0, 1, 0},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT |
               OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP;
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

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  constexpr uint8_t zero = 0;
  ASSERT_EQ(obelisk_rt_v1_coverage_finalize(
                context, 0, 1, &zero, &zero,
                OBELISK_RT_COVERAGE_PERSIST_ALL),
            OBELISK_RT_OK);
  uint64_t coveredState = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(coveredState, UINT64_MAX);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 0, 1, coveredState),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_seal(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(5);
  fixture.descriptor.execution = &execution;
  uint64_t root = obelisk_rt_canonical_state_handle_unlocked(context, 0, 1);
  ASSERT_NE(root, UINT64_MAX);
  schedulerWaitKind = OBELISK_RT_SUSPEND_CHANGE;
  schedulerWaitHandle = root;
  schedulerWaitWidth = 1;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);

  // Model a design which has a bytecode deoptimization tier without having to
  // embed an otherwise unused bytecode image in this scheduler unit test. The
  // exact indexed deposit must not interpret this capability bit as an active
  // request to stabilize through bytecode.
  execution.flags = OBELISK_RT_EXECUTION_REQUIRE_BYTECODE;
  context->stateValue[0] = 1;
  ASSERT_TRUE(obelisk_rt_aot_external_deposit_unlocked(context, root, 0, 1));
  uint8_t changed = 1;
  uint8_t posedge = 1;
  uint8_t negedge = 0;
  uint8_t newValue = 1;
  uint8_t newUnknown = 0;
  ASSERT_TRUE(obelisk_rt_publish_native_signal_transition_unlocked(
      context, root, 1, &changed, &posedge, &negedge, &zero, &zero, &newValue,
      &newUnknown, true));
  EXPECT_EQ(ingress, 1u);
  EXPECT_FALSE(context->nativeScheduleExternalWritePending);
  EXPECT_FALSE(context->nativeScheduleDirtyRootsPresent);
  clockCoordinatorIngress = &ingress;
  clockCoordinatorWords = 1;
  clockCoordinatorCalls = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_clock_coordinator(context),
            OBELISK_RT_OK);
  EXPECT_EQ(clockCoordinatorCalls, 1u);
  EXPECT_EQ(ingress, 0u);
  clockCoordinatorIngress = nullptr;
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  uint64_t covered = 0, total = 0;
  double percentage = 0;
  ASSERT_EQ(obelisk_rt_v1_coverage_query(context, OBELISK_RT_COVERAGE_TOGGLE,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 1u);
  EXPECT_EQ(total, 2u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Coverage, GenericScalarNetPublicationRecordsToggleTransition) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  constexpr uint8_t zero = 0;
  ASSERT_EQ(obelisk_rt_v1_coverage_finalize(
                context, 0, 1, &zero, &zero,
                OBELISK_RT_COVERAGE_PERSIST_ALL),
            OBELISK_RT_OK);
  uint64_t state = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(state, UINT64_MAX);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 0, 1, state),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_seal(context), OBELISK_RT_OK);

  ASSERT_TRUE(obelisk_rt_append_signal_event_unlocked(context, state, false,
                                                      false, true, false));
  uint64_t covered = 0, total = 0;
  double percentage = 0;
  ASSERT_EQ(obelisk_rt_v1_coverage_query(context, OBELISK_RT_COVERAGE_TOGGLE,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 1u);
  EXPECT_EQ(total, 2u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Coverage, TransitionFastPathsObserveLateToggleBinding) {
  for (bool staticAOT : {false, true}) {
    obelisk_rt_execution_descriptor_v1 execution{};
    execution.version = OBELISK_RT_VERSION;
    execution.state_bit_count = 1;
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
              OBELISK_RT_OK);
    context->stateValue[0] = context->stateUnknown[0] = 0;
    AOTTestState state;
    auto plan = makeAOTPlan(state, 1);
    plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
                 OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
    ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
              OBELISK_RT_OK);
    uint64_t handle = obelisk_rt_v1_native_state_static_handle(1);
    auto publish = [&](uint8_t oldValue, uint8_t newValue) {
      if (staticAOT) {
        NativeAOTContextScope active(context);
        NativeAOTMutexScope locked(context);
        obelisk_rt_v1_scheduler_static_transition(context, 1, 0, 1, oldValue, 0,
                                                  newValue, 0);
      } else {
        obelisk_rt_v1_scheduler_signal_transition(context, handle, 1, &oldValue,
                                                  nullptr, &newValue, nullptr);
      }
    };
    publish(0, 1);
    publish(1, 0);
    EXPECT_EQ(context->coverage, nullptr);
    constexpr uint8_t zero = 0;
    ASSERT_EQ(obelisk_rt_v1_coverage_finalize(context, 0, 1, &zero, &zero,
                                              OBELISK_RT_COVERAGE_PERSIST_ALL),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 0, 1, handle),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_coverage_toggle_seal(context), OBELISK_RT_OK);
    publish(0, 1);
    publish(1, 0);
    uint64_t covered = 0, total = 0;
    double percentage = 0;
    ASSERT_EQ(obelisk_rt_v1_coverage_query(context, OBELISK_RT_COVERAGE_TOGGLE,
                                           &covered, &total, &percentage),
              OBELISK_RT_OK);
    EXPECT_EQ(covered, 2u);
    EXPECT_EQ(total, 2u);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, AOTTiedDeadlinesUseFixedHeapAndDeterministicNodeOrder) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture first(1);
  SchedulerFixture second(2);
  schedulerWaitKind = OBELISK_RT_SUSPEND_DELAY;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(first), 0, 0,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(second), 0, 1,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  constexpr obelisk_rt_native_schedule_node nodes[] = {
      {0, 0, UINT32_MAX},
      {1, 0, UINT32_MAX},
      {0, 1, UINT32_MAX},
      {1, 1, UINT32_MAX},
  };
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_run_aot_nodes(context, nodes, std::size(nodes)),
      OBELISK_RT_OK);
  EXPECT_EQ(context->schedulerTime, 17u);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{1, 2}));
  EXPECT_EQ(context->signalDiagnostics.aotDeadlineHighWater, 2u);
  EXPECT_TRUE(context->nativeScheduleDeadlineHeap.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTDelayIndexStaysBoundedWithoutGenericCalendarQueries) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 instances[3]{};
  context->scheduledProcesses.resize(3);
  for (unsigned index = 0; index != 3; ++index) {
    auto &process = context->scheduledProcesses[index];
    process.instance = &instances[index];
    process.token = index + 1;
    process.started = true;
    process.suspendKind = OBELISK_RT_SUSPEND_DELAY;
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
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTZeroDelayTransactionallyFallsBackBeforeRegionBoundary) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL;
  plan.run = aotRunNodes;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(3);
  schedulerWaitKind = OBELISK_RT_SUSPEND_DELAY;
  schedulerWaitDelay = 0;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 1u);
  EXPECT_EQ(context->schedulerTime, 0u);
  EXPECT_EQ(schedulerResumeCount, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTFallbackSnapshotIsValidatedBeforeGenericResume) {
  AOTTestState state;
  state.requestFallback = true;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  SchedulerFixture fixture(200);
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  schedulerOrder.clear();
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 1u);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{200}));
  EXPECT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 1u);
  obelisk_rt_v1_context_destroy(context);

  AOTTestState invalid;
  invalid.requestFallback = true;
  invalid.corruptSnapshot = true;
  plan = makeAOTPlan(invalid);
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run_aot(context),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTFallbackPreservesUnsupportedSuspendContinuation) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.run = aotRunNodes;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(42);
  schedulerWaitKind = OBELISK_RT_SUSPEND_EVENT;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_NONE;
  schedulerWaitHandle = 91;
  schedulerWaitWidth = 0;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  ASSERT_TRUE(context->nativeScheduleDeoptimized);
  ASSERT_EQ(context->signalDiagnostics.aotFallbacks, 1u);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_EQ(context->scheduledProcesses.front().instance->continuation, 1u);
  EXPECT_EQ(context->scheduledProcesses.front().suspendKind,
            OBELISK_RT_SUSPEND_EVENT);
  EXPECT_EQ(schedulerResumeCount, 0u);

  obelisk_rt_v1_scheduler_event(context, 91, 0);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTFinishDestroysTheCompleteTaskCallerStack) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.run = aotRunNodes;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(43);
  fixture.descriptor.native_execute = finishExecute;
  fixture.descriptor.native_destroy = finishDestroy;
  obelisk_rt_process_instance_v1 *callee = makeSchedulerInstance(fixture);
  obelisk_rt_process_instance_v1 *caller = makeSchedulerInstance(fixture);
  ASSERT_NE(callee, nullptr);
  ASSERT_NE(caller, nullptr);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context, callee, 0, 0, 0, nullptr,
                                            nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  // A real suspended caller retains its native coroutine handle while the
  // callee occupies the actor slot.
  caller->native_handle = caller;
  context->scheduledProcesses.front().callers.push_back(caller);
  finishDestroyCount = 0;

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(finishDestroyCount, 2u);
  EXPECT_EQ(state.actors[0], nullptr);
  EXPECT_TRUE(context->scheduledProcesses.empty() ||
              (!context->scheduledProcesses.front().instance &&
               context->scheduledProcesses.front().callers.empty()));
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTTaskCallFallbackRebindsAndResumesTheCaller) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.run = aotRunNodes;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture caller(44);
  SchedulerFixture callee(45);
  caller.descriptor.native_execute = taskCallerExecute;
  callee.descriptor.native_execute = taskCalleeExecute;
  taskCalleeDescriptor = &callee.descriptor;
  taskCallerExecutions = 0;
  taskCalleeExecutions = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context,
                                            makeSchedulerInstance(caller), 0, 0,
                                            0, nullptr, nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 1u);
  EXPECT_EQ(taskCallerExecutions, 2u);
  EXPECT_EQ(taskCalleeExecutions, 1u);
  EXPECT_EQ(state.actors[0], nullptr);
  taskCalleeDescriptor = nullptr;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTExternalWriteUsesNativeFineSchedulerUntilCleanBoundary) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  SchedulerFixture fixture(33);
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE, 0,
                    OBELISK_RT_SUSPEND_EDGE, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<obelisk_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  obelisk_rt_bytecode_v1 bytecode{code.data(),
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
      OBELISK_RT_TIER_MASK_NATIVE | OBELISK_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.bytecode = &bytecode;
  schedulerWaitKind = OBELISK_RT_SUSPEND_EDGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_NEGEDGE;
  schedulerWaitHandle = 17;
  schedulerWaitWidth = 8;
  obelisk_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0};
  *entry = {17, OBELISK_RT_WAIT_EDGE_NEGEDGE, 8};
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context, instance, 0, 0, 0, nullptr,
                                            nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  EXPECT_EQ(instance->tier, 0u);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_external_write_unlocked(context);
  }
  EXPECT_TRUE(context->nativeScheduleExternalWritePending);
  EXPECT_EQ(instance->tier, 0u);
  schedulerResumeCount = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_FALSE(context->nativeScheduleExternalWritePending);
  EXPECT_EQ(instance->tier, OBELISK_RT_TIER_NATIVE);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  EXPECT_EQ(schedulerResumeCount, 0u);
  obelisk_rt_v1_scheduler_signal(
      context, 17, 8, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTExternalWriteReturnsToFourStateAtCleanBoundary) {
  AOTTestState state;
  state.runHook = runCountOK;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  plan.promotion_ready = schedulerPromotionReady;
  schedulerPromotionInvalidationCount = 0;
  schedulerPromotionReadyCount = 0;
  schedulerPromotionReadyValue = false;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_external_write_unlocked(context);
  }

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.runCalls, 1u);
  // The callback scans exactly once after canonical export at the quiescent
  // Tier-2 handback. A false result selects four-state Tier 1 without delaying
  // that handback.
  EXPECT_EQ(schedulerPromotionReadyCount, 1u);

  // An ordinary Tier-1 invocation does not rescan an unchanged unknown
  // plane. Only a new transient write invalidates the promotion boundary.
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.runCalls, 2u);
  EXPECT_EQ(schedulerPromotionReadyCount, 1u);

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_external_write_unlocked(context);
  }
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.runCalls, 3u);
  EXPECT_EQ(schedulerPromotionReadyCount, 2u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTExternalWriteExecutesNativeActorAtCleanBoundary) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(34);
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE, 0,
                    OBELISK_RT_SUSPEND_EDGE, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<obelisk_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  obelisk_rt_bytecode_v1 bytecode{code.data(),
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
      OBELISK_RT_TIER_MASK_NATIVE | OBELISK_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.bytecode = &bytecode;
  schedulerWaitKind = OBELISK_RT_SUSPEND_EDGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = 17;
  schedulerWaitWidth = 8;
  schedulerDestroyCount = 0;
  obelisk_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  ASSERT_NE(instance->native_handle, nullptr);
  ASSERT_EQ(instance->tier, OBELISK_RT_TIER_NATIVE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context, instance, 0, 0, 0, nullptr,
                                            nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_aot_external_write_unlocked(context);
  }
  EXPECT_EQ(instance->tier, OBELISK_RT_TIER_NATIVE);
  EXPECT_NE(instance->native_handle, nullptr);
  EXPECT_EQ(schedulerDestroyCount, 0u);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(instance->tier, OBELISK_RT_TIER_NATIVE);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(schedulerDestroyCount, 1u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTBytecodeFragmentReturnsToNativeAtContinuationBoundary) {
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  SchedulerFixture fixture(32);
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE, 0,
                    OBELISK_RT_SUSPEND_EDGE, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<obelisk_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  obelisk_rt_bytecode_v1 bytecode{code.data(),
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
      OBELISK_RT_TIER_MASK_NATIVE | OBELISK_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.bytecode = &bytecode;
  obelisk_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0};
  *entry = {16, OBELISK_RT_WAIT_EDGE_NEGEDGE, 8};
  const uint32_t bytecodeContinuation = 0;
  schedulerResumeCount = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context, instance, 0, 0, 0, nullptr,
                                            nullptr, 0, &bytecodeContinuation,
                                            1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);
  obelisk_rt_v1_scheduler_signal(
      context, 16, 8, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  // Continuation zero suspended through bytecode. Continuation one then
  // resumed through the native wrapper without deoptimizing the AOT plan.
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTNativeCheckpointRunsBytecodeIslandAndReturnsToNative) {
  AOTTestState state;
  state.runHook = runAOTNodes;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.promotion_invalidate = schedulerInvalidatePromotion;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  SchedulerFixture fixture(35);
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0, 0,
                    0);
  appendInstruction(code, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE, 0,
                    OBELISK_RT_SUSPEND_EDGE, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 0);
  std::array<obelisk_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  obelisk_rt_bytecode_v1 bytecode{code.data(),
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
      OBELISK_RT_TIER_MASK_NATIVE | OBELISK_RT_TIER_MASK_BYTECODE;
  fixture.descriptor.native_execute = schedulerCheckpointThenNativeExecute;
  fixture.descriptor.bytecode = &bytecode;
  schedulerCheckpointCount = 0;
  schedulerPromotionInvalidationCount = 0;
  schedulerResumeCount = 0;
  obelisk_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(instance->frame);
  auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0};
  *entry = {16, OBELISK_RT_WAIT_EDGE_NEGEDGE, 8};
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context, instance, 0, 0, 0, nullptr,
                                            nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerCheckpointCount, 1u);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 1u);
  EXPECT_EQ(instance->tier, OBELISK_RT_TIER_BYTECODE);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  ASSERT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(), 1u);
  EXPECT_EQ(
      context->scheduledProcesses.front().signalSubscriptions.front()->stableID,
      16u);
  // Exercise the real signal subscription and AOT ready-bit routing on the
  // Tier-3 return continuation instead of mutating scheduler internals.
  obelisk_rt_v1_scheduler_signal(
      context, 16, 8, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 1u);
  EXPECT_EQ(instance->tier, OBELISK_RT_TIER_NATIVE);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AwaitUsesStableNonAddressProcessTokens) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture completed(100);
  obelisk_rt_process_instance_v1 *oldInstance =
      makeSchedulerInstance(completed);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, oldInstance, 0),
            OBELISK_RT_OK);
  uint64_t oldToken =
      obelisk_rt_v1_scheduler_process_token(context, oldInstance);
  ASSERT_NE(oldToken, 0u);
  EXPECT_NE(oldToken,
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(oldInstance)));
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  obelisk_rt_process_instance_v1 *child = makeSchedulerInstance(completed);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(context, child, 0, 1),
            OBELISK_RT_OK);
  uint64_t childToken = obelisk_rt_v1_scheduler_process_token(context, child);
  ASSERT_NE(childToken, 0u);
  EXPECT_NE(childToken, oldToken);
  EXPECT_NE(childToken,
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(child)));

  SchedulerFixture waiter(4);
  schedulerWaitKind = OBELISK_RT_SUSPEND_AWAIT;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_NONE;
  schedulerWaitHandle = childToken;
  schedulerWaitWidth = 0;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(
                context, makeSchedulerInstance(waiter), 0, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, ProcessAssociativeKeysUseStableTombstoneIdentity) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture first(100);
  SchedulerFixture second(200);
  obelisk_rt_process_instance_v1 *firstInstance = makeSchedulerInstance(first);
  obelisk_rt_process_instance_v1 *secondInstance =
      makeSchedulerInstance(second);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(context, firstInstance, 0, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(context, secondInstance, 0, 1),
            OBELISK_RT_OK);
  uint64_t firstToken =
      obelisk_rt_v1_scheduler_process_token(context, firstInstance);
  uint64_t secondToken =
      obelisk_rt_v1_scheduler_process_token(context, secondInstance);
  ASSERT_NE(firstToken, 0u);
  ASSERT_GT(secondToken, firstToken);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  const obelisk_rt_element_type_v1 wordElement{
      OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_BITS, 92, 0,      0,
      sizeof(uint64_t),   alignof(uint64_t),       64, nullptr};
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(lane, &wordElement,
                                       OBELISK_RT_ASSOC_KEY_PROCESS, 0, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);

  const uint64_t tokens[] = {0, firstToken, secondToken};
  for (uint64_t index = 0; index != std::size(tokens); ++index) {
    obelisk_rt_assoc_key_v1 key{
        OBELISK_RT_ASSOC_KEY_PROCESS, 0, 0, tokens[index], 0, 0};
    uint64_t value = index + 20;
    ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              OBELISK_RT_OK);
  }

  obelisk_rt_assoc_key_v1 invalid{OBELISK_RT_ASSOC_KEY_PROCESS,
                                  0,
                                  0,
                                  OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG |
                                      UINT64_C(0x123456),
                                  0,
                                  0};
  uint64_t value = 99;
  EXPECT_EQ(obelisk_rt_v1_assoc_write(lane, array, &invalid, &value, nullptr),
            OBELISK_RT_INVALID_HANDLE);

  obelisk_rt_assoc_key_v1 cursor{};
  uint32_t success = 0;
  for (uint64_t expected : tokens) {
    ASSERT_EQ(obelisk_rt_v1_assoc_first(lane, array, &cursor, &success),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    EXPECT_EQ(cursor.kind, OBELISK_RT_ASSOC_KEY_PROCESS);
    EXPECT_EQ(cursor.value, expected);
    ASSERT_EQ(obelisk_rt_v1_assoc_delete(array, &cursor), OBELISK_RT_OK);
  }
  EXPECT_EQ(obelisk_rt_v1_container_size(array), 0u);

  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CompletedProcessStorageStaysCompactAcrossChurn) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture completed(100);
  constexpr uint64_t processCount = 1000;
  for (uint64_t iteration = 0; iteration != processCount; ++iteration) {
    ASSERT_EQ(obelisk_rt_v1_scheduler_add(context,
                                          makeSchedulerInstance(completed), 0),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  }

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    EXPECT_TRUE(context->scheduledProcesses.empty());
    EXPECT_EQ(context->terminatedNativeProcesses.rangeCount(), 1u);
    EXPECT_EQ(context->terminatedNativeProcesses.count(1), 1u);
    EXPECT_EQ(context->terminatedNativeProcesses.count(processCount), 1u);
  }
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CompactionPreservesCursorAcrossInterleavedDeadSlots) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture first(101);
  SchedulerFixture second(102);
  SchedulerFixture third(103);
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(first), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(second), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(third), 0),
      OBELISK_RT_OK);
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

  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{103, 101, 102}));
  EXPECT_TRUE(context->scheduledProcesses.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, TerminatedJoinNoneParentsPreserveDescendantAncestry) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);

  constexpr uint64_t tag = OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG;
  auto live = reinterpret_cast<obelisk_rt_process_instance_v1 *>(
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

  obelisk_rt_reparent_process_children_unlocked(context, tag | 4, tag | 2);

  EXPECT_EQ(context->scheduledProcesses[0].parent, tag | 1);
  EXPECT_EQ(context->scheduledProcesses[1].parent, tag | 2);
  EXPECT_EQ(context->scheduledDesignTasks[0].parent, tag | 2);
  EXPECT_EQ(context->logicalProcessParentsWithChildren.count(tag | 4), 0u);
  EXPECT_EQ(context->logicalProcessParentsWithChildren.count(tag | 2), 1u);
  obelisk_rt_reparent_process_children_unlocked(context, tag | 2, 0);
  EXPECT_EQ(context->scheduledProcesses[1].parent, 0u);
  EXPECT_EQ(context->scheduledDesignTasks[0].parent, 0u);
  EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  context->scheduledProcesses.clear();
  context->scheduledDesignTasks.clear();
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, NativeChildAttachIndexesAndReparentsActualProcess) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  constexpr uint64_t tag = OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG;
  context->activeLogicalProcessToken = tag | 41;
  SchedulerFixture fixture(151);
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_EQ(context->scheduledProcesses.front().parent, tag | 41);
  EXPECT_EQ(context->logicalProcessParentsWithChildren.count(tag | 41), 1u);

  obelisk_rt_reparent_process_children_unlocked(context, tag | 41, tag | 40);
  EXPECT_EQ(context->scheduledProcesses.front().parent, tag | 40);
  EXPECT_EQ(context->logicalProcessParentsWithChildren.count(tag | 41), 0u);
  EXPECT_EQ(context->logicalProcessParentsWithChildren.count(tag | 40), 1u);
  context->activeLogicalProcessToken = 0;
  obelisk_rt_reparent_process_children_unlocked(context, tag | 40, 0);
  EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, DetachedChildlessTerminationDoesNotCreateParentIndex) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  constexpr uint64_t tag = OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG;
  context->activeLogicalProcessToken = tag | 99;
  SchedulerFixture fixture(150);
  schedulerDestroyCount = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture),
                                  OBELISK_RT_SCHEDULE_STARTUP |
                                      OBELISK_RT_SCHEDULE_DETACHED_CONTROLS),
      OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_EQ(context->scheduledProcesses.front().parent, 0u);
  EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  context->activeLogicalProcessToken = 0;

  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  EXPECT_EQ(schedulerDestroyCount, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, ParentIndexStaysBoundedAcrossNaturalAndKilledChurn) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
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
    obelisk_rt_process_instance_v1 *instance = makeSchedulerInstance(parent);
    ASSERT_NE(instance, nullptr);
    instance->continuation = 1;
    ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance,
                                          OBELISK_RT_SCHEDULE_STARTUP),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
    EXPECT_TRUE(context->scheduledProcesses.empty());
    EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  }

  constexpr uint64_t tag = OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG;
  for (unsigned iteration = 0; iteration != 16; ++iteration) {
    context->activeLogicalProcessToken = tag | 900;
    uint64_t activation = 0;
    ASSERT_EQ(obelisk_rt_v1_control_enter(context, 901, &activation),
              OBELISK_RT_OK);
    context->activeLogicalProcessToken = 0;
    SchedulerFixture parent(170);
    SchedulerFixture child(171);
    ASSERT_EQ(
        obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(parent), 0),
        OBELISK_RT_OK);
    ASSERT_FALSE(context->scheduledProcesses.empty());
    uint64_t parentToken = tag | context->scheduledProcesses.back().token;
    context->activeLogicalProcessToken = parentToken;
    ASSERT_EQ(
        obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(child), 0),
        OBELISK_RT_OK);
    EXPECT_EQ(context->logicalProcessParentsWithChildren.count(parentToken),
              1u);

    context->activeLogicalProcessToken = tag | 900;
    ASSERT_EQ(obelisk_rt_v1_control_disable(context, 901, activation, 0),
              OBELISK_RT_OK);
    context->activeLogicalProcessToken = 0;
    ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
    EXPECT_TRUE(context->scheduledProcesses.empty());
    EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  }
  cachedCohortUrgentChild = nullptr;
  cachedCohortOrdinaryChild = nullptr;
  cachedCohortSpawnID = 0;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticNBASitesMergeAndCommitEachRootOnce) {
  AOTTestState state;
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 8, nullptr},
  };
  const obelisk_rt_static_nba_site sites[] = {
      {7, 0, OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR},
      {8, 0, OBELISK_RT_STATIC_NBA_FIXED_SLOT},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_sites = sites;
  plan.nba_site_count = std::size(sites);

  obelisk_rt_context *context = nullptr;
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  uint8_t plane = 0;
  uint8_t first = 0xa;
  uint8_t second = 0x3;
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  ASSERT_EQ(obelisk_rt_v1_scheduler_static_nba(context, 7, &plane, nullptr, 8,
                                               root, 4, &first, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_static_nba(
                context, 8, &plane, nullptr, 8,
                obelisk_rt_v1_native_handle_offset(root, 2), 4, &second,
                nullptr),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(plane, 0x0e);
  EXPECT_EQ(context->stateValue[0], 0x0e);
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBA256SnapshotsAndPreservesGenericLastWrite) {
  AOTTestState state;
  obelisk_rt_generated_nba_accumulator_256 generated{};
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated},
  };
  const obelisk_rt_static_nba_site sites[] = {
      {7, 0, OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_sites = sites;
  plan.nba_site_count = std::size(sites);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 256;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 256),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  generated.value[0] = 0x55;
  generated.write_mask[0] = UINT32_MAX;
  generated.valid = 1;
  generated.exec_region = OBELISK_RT_REGION_NBA;
  obelisk_rt_aot_deopt_snapshot snapshot{};
  ASSERT_EQ(obelisk_rt_v1_scheduler_snapshot_aot(context, &snapshot),
            OBELISK_RT_OK);
  ASSERT_EQ(snapshot.nba_count, 1u);
  EXPECT_TRUE(std::all_of(std::begin(generated.write_mask),
                          std::end(generated.write_mask),
                          [](uint64_t mask) { return mask == 0; }));
  EXPECT_EQ(generated.valid, 0u);
  EXPECT_TRUE(context->staticNBAAccumulators[0].valid);

  uint8_t newer = 0xaa;
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_EQ(obelisk_rt_v1_scheduler_static_nba(context, 7, valuePlane.data(),
                                               nullptr, 256, root, 8, &newer,
                                               nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(valuePlane[0], 0xaa);
  EXPECT_EQ(context->stateValue[0] & 0xff, 0xaa);
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, WritableNBADirectCommitRequiresCleanLockedBoundary) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags =
      OBELISK_RT_EXECUTION_VPI_READ | OBELISK_RT_EXECUTION_VPI_WRITE;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  uint32_t fast = 1;
  obelisk_rt_native_schedule_plan plan{};
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP;
  plan.specialization_fast = &fast;
  context->nativeSchedulePlan = &plan;
  context->nativeScheduleGuardedFanoutActive = true;
  EXPECT_EQ(obelisk_rt_v1_static_nba_direct_commit_guard(context), 0u);
  {
    NativeAOTContextScope active(context);
    EXPECT_EQ(obelisk_rt_v1_static_nba_direct_commit_guard(context), 0u);
    NativeAOTMutexScope locked(context);
    EXPECT_EQ(obelisk_rt_v1_static_nba_direct_commit_guard(context), 1u);
    for (bool *condition : {&context->nativeScheduleExternalWritePending,
                            &context->nativeScheduleDirtyRootsPresent,
                            &context->nativeScheduleDeoptimized}) {
      *condition = true;
      EXPECT_EQ(obelisk_rt_v1_static_nba_direct_commit_guard(context), 0u);
      *condition = false;
    }
    context->nativeDynamicSignalSubscriptions = 1;
    EXPECT_EQ(obelisk_rt_v1_static_nba_direct_commit_guard(context), 0u);
    context->nativeDynamicSignalSubscriptions = 0;
    context->activeComputedObserverWaiterCount = 1;
    EXPECT_EQ(obelisk_rt_v1_static_nba_direct_commit_guard(context), 0u);
    context->activeComputedObserverWaiterCount = 0;
    fast = 0;
    EXPECT_EQ(obelisk_rt_v1_static_nba_direct_commit_guard(context), 0u);
    fast = 1;
    EXPECT_EQ(obelisk_rt_v1_static_nba_direct_commit_guard(context), 1u);
  }
  context->nativeSchedulePlan = nullptr;
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBAScalarCommitsValueUnknownAndPartMaskDirectly) {
  AOTTestState state;
  obelisk_rt_generated_nba_accumulator_256 generated{};
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 8, &generated},
  };
  uint8_t valuePlane = 0xc0;
  uint8_t unknownPlane = 0xf0;
  uint8_t nativeValue = valuePlane;
  uint8_t nativeUnknown = unknownPlane;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.state_value = &valuePlane;
  plan.state_unknown = &unknownPlane;
  plan.state_bit_count = 8;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_sync(context, &nativeValue,
                                            &nativeUnknown, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  generated.value[0] = 0x0a;
  generated.unknown[0] = 0x05;
  generated.write_mask[0] = 0x0f;
  generated.valid = 1;
  generated.exec_region = OBELISK_RT_REGION_NBA;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
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
  generated.exec_region = OBELISK_RT_REGION_NBA;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(valuePlane, 0x2a);
  EXPECT_EQ(unknownPlane, 0x45);
  EXPECT_EQ(nativeValue, valuePlane);
  EXPECT_EQ(nativeUnknown, unknownPlane);
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 2u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBAScalarRecordsCoveredTransitionsExactlyOnce) {
  AOTTestState state;
  obelisk_rt_generated_nba_accumulator_256 generated{};
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 1, &generated},
  };
  uint8_t valuePlane = 0;
  uint8_t unknownPlane = 0;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.state_value = &valuePlane;
  plan.state_unknown = &unknownPlane;
  plan.state_bit_count = 1;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  constexpr std::array<uint8_t, 1> zero{0};
  ASSERT_EQ(
      obelisk_rt_v1_coverage_finalize(
          context, 0, 1, zero.data(), zero.data(),
          OBELISK_RT_COVERAGE_PERSIST_ALL),
      OBELISK_RT_OK);
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 0, 1, root),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_seal(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  generated.value[0] = 1;
  generated.write_mask[0] = 1;
  generated.valid = 1;
  generated.exec_region = OBELISK_RT_REGION_NBA;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);

  generated.value[0] = 0;
  generated.write_mask[0] = 1;
  generated.valid = 1;
  generated.exec_region = OBELISK_RT_REGION_NBA;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);

  ASSERT_NE(context->coverage, nullptr);
  ASSERT_EQ(context->coverage->toggleCounters.size(), 4u);
  EXPECT_EQ(context->coverage->toggleCounters[0], 1u);
  EXPECT_EQ(context->coverage->toggleCounters[1], 1u);
  EXPECT_EQ(context->coverage->toggleCounters[2], 0u);
  EXPECT_EQ(context->coverage->toggleCounters[3], 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, RebuiltCalendarRetainsDetachedPeriodicDeadline) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture fixture(42);
  schedulerWaitKind = OBELISK_RT_SUSPEND_DELAY;
  schedulerWaitDelay = 5;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  auto *instance = makeSchedulerInstance(fixture);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_prime(context, instance), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  ASSERT_EQ(context->scheduledProcesses.front().wakeTime, 5u);
  // The generated loop advances its detached source without maintaining the
  // generic calendar. Whole-plan fallback restores the actor's next edge,
  // then rebuilds the scheduler indices. A stale heap entry for time 5 must
  // not make the scheduler exit at time 20 with a live deadline at time 25.
  context->schedulerTime = 20;
  context->scheduledProcesses.front().wakeTime = 25;
  rebuildNativeSchedulerIndexUnlocked(context);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->schedulerTime, 25u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, DeoptimizedNBACommitDoesNotReenterGeneratedBarrier) {
  AOTTestState state;
  obelisk_rt_generated_nba_accumulator_256 generated{};
  const obelisk_rt_static_nba_root roots[] = {{17, 1, 4, &generated}};
  std::array<uint8_t, 8> valuePlane{};
  std::array<uint8_t, 8> unknownPlane{};
  uint64_t dirtyRoots = 1;
  uint64_t dirtySummary = 1;
  auto plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA;
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
  plan.nba_commit = [](void *, obelisk_rt_context *, uint32_t, uint32_t *) {
    return OBELISK_RT_TIER_UNAVAILABLE;
  };

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 4;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 4),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  generated.value[0] = 5;
  generated.unknown[0] = 2;
  generated.write_mask[0] = 15;
  generated.valid = 1;
  generated.exec_region = OBELISK_RT_REGION_NBA;

  bool changed = false;
  EXPECT_EQ(commitStaticNBAAccumulatorsUnlocked(
                context, OBELISK_RT_REGION_NBA, changed),
            OBELISK_RT_TIER_UNAVAILABLE);
  EXPECT_EQ(generated.valid, 1u);
  context->nativeScheduleDeoptimized = true;
  ASSERT_EQ(commitStaticNBAAccumulatorsUnlocked(
                context, OBELISK_RT_REGION_NBA, changed),
            OBELISK_RT_OK);
  EXPECT_TRUE(changed);
  EXPECT_EQ(valuePlane[0], 5u);
  EXPECT_EQ(unknownPlane[0], 2u);
  EXPECT_EQ(context->stateValue[0] & 15, 5u);
  EXPECT_EQ(context->stateUnknown[0] & 15, 2u);
  EXPECT_EQ(generated.valid, 0u);
  EXPECT_EQ(dirtyRoots, 0u);
  EXPECT_EQ(dirtySummary, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBADirtyHierarchySkipsEmptyLeafPages) {
  constexpr uint32_t rootCount = 65;
  AOTTestState state;
  std::array<obelisk_rt_generated_nba_accumulator_256, rootCount> generated{};
  std::array<obelisk_rt_static_nba_root, rootCount> roots{};
  for (uint32_t root = 0; root != rootCount; ++root)
    roots[root] = {root, root + 1, 1, &generated[root]};
  std::array<uint8_t, 9> valuePlane{};
  std::array<uint8_t, 9> unknownPlane{};
  std::array<uint64_t, 2> dirtyRoots{0, 1};
  std::array<uint64_t, 1> dirtySummary{uint64_t{1} << 1};
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = rootCount;
  plan.nba_roots = roots.data();
  plan.nba_root_count = rootCount;
  plan.nba_dirty_roots = dirtyRoots.data();
  plan.nba_dirty_word_count = dirtyRoots.size();
  plan.nba_dirty_summary = dirtySummary.data();
  plan.nba_dirty_summary_word_count = dirtySummary.size();

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = rootCount;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  for (uint32_t root = 0; root != rootCount; ++root)
    ASSERT_EQ(
        obelisk_rt_v1_native_state_register_static(context, root + 1, root, 1),
        OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  generated[64].value[0] = 1;
  generated[64].write_mask[0] = 1;
  generated[64].valid = 1;
  generated[64].exec_region = OBELISK_RT_REGION_NBA;
  // Fixed-site Tier-1 stages retain constant valid/mask fields after their
  // dirty bit is consumed. They must not create a phantom runtime barrier.
  generated[0].valid = 1;
  generated[0].write_mask[0] = 1;
  generated[0].exec_region = OBELISK_RT_REGION_ACTIVE;
  EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context), OBELISK_RT_REGION_NBA);
  uint32_t changed = 0;
  ASSERT_EQ(obelisk_rt_v1_static_nba_commit_roots(
                context, rootCount, OBELISK_RT_REGION_NBA, &changed),
            OBELISK_RT_OK);
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
  EXPECT_EQ(nextDueNBABarrierRegionUnlocked(context), OBELISK_RT_REGION_ACTIVE);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, SlotEntryPreservesProgressAndSamplesAcrossTierBoundaries) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->preponedObserverPresent = true;
  context->schedulerTime = 37;
  context->schedulerSlotProgress = 9;
  context->staticNBASlowRoots = {1};
  context->staticNBASlowRootsPresent = true;
  ASSERT_EQ(enterSchedulerTimeSlotUnlocked(context), OBELISK_RT_OK);
  EXPECT_EQ(context->schedulerPreponedTime, 37u);
  EXPECT_EQ(context->schedulerSlotProgress, 0u);
  EXPECT_FALSE(context->staticNBASlowRootsPresent);
  const auto eventID = OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT;
  ASSERT_EQ(context->events.count(eventID), 1u);
  uint64_t generation = context->events.at(eventID).generation;
  context->schedulerSlotProgress = 4;
  context->staticNBASlowRoots[0] = 1;
  context->staticNBASlowRootsPresent = true;
  ASSERT_EQ(enterSchedulerTimeSlotUnlocked(context), OBELISK_RT_OK);
  EXPECT_EQ(context->events.at(eventID).generation, generation);
  EXPECT_EQ(context->schedulerSlotProgress, 4u);
  EXPECT_TRUE(context->staticNBASlowRootsPresent);
  ++context->schedulerTime;
  ASSERT_EQ(enterSchedulerTimeSlotUnlocked(context), OBELISK_RT_OK);
  EXPECT_EQ(context->events.at(eventID).generation, generation + 1);
  EXPECT_EQ(context->schedulerSlotProgress, 0u);
  EXPECT_FALSE(context->staticNBASlowRootsPresent);
  EXPECT_EQ(context->staticNBASlowRoots[0], 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, ReentrantExecutorSelectionRestoresSuspendedCaller) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->schedulerTime = 37;
  context->nextSchedulerSequence = 81;
  context->activeExecRegion = OBELISK_RT_REGION_REACTIVE;
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
  EXPECT_EQ(context->activeExecRegion, OBELISK_RT_REGION_REACTIVE);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, DPIExportTemporarilyReleasesOuterExecutorSelection) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture exported(7);
  schedulerWaitKind = OBELISK_RT_SUSPEND_DELAY;
  schedulerWaitDelay = 3;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ScheduledDesignEvent later;
  later.stableID = 1;
  later.dueTime = 99;
  later.execRegion = OBELISK_RT_REGION_NBA;
  context->scheduledDesignEvents.push_back(later);
  {
    NativeScheduleStepScope caller(context, 5, true, true, 19);
    context->nativeScheduleForcedExecuted = true;
    EXPECT_EQ(obelisk_rt_v1_dpi_export_task_run(
                  context, makeSchedulerInstance(exported)),
              OBELISK_RT_OK);
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
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, SharedNBABarrierSelectionMatchesScalarInventory) {
  // Cross both the leaf and summary boundaries; include stale generated
  // payloads, runtime stages, empty pages, and all iterative region choices.
  constexpr uint32_t rootCount = 4103;
  constexpr uint32_t regions[] = {OBELISK_RT_REGION_ACTIVE,
                                 OBELISK_RT_REGION_NBA,
                                 OBELISK_RT_REGION_REACTIVE,
                                 OBELISK_RT_REGION_RE_NBA};
  std::vector<obelisk_rt_generated_nba_accumulator_256> generated(rootCount);
  std::vector<obelisk_rt_static_nba_root> roots(rootCount);
  std::vector<uint64_t> dirty((rootCount + 63) / 64);
  std::vector<uint64_t> summary((dirty.size() + 63) / 64);
  obelisk_rt_native_schedule_plan plan{};
  plan.nba_roots = roots.data();
  plan.nba_root_count = rootCount;
  plan.nba_dirty_roots = dirty.data();
  plan.nba_dirty_word_count = dirty.size();
  plan.nba_dirty_summary = summary.data();
  plan.nba_dirty_summary_word_count = summary.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->nativeSchedulePlan = &plan;
  context->nativeScheduleNBARoots = roots.data();
  context->nativeScheduleNBARootCount = rootCount;
  context->nativeScheduleHasGeneratedNBAAccumulators = true;
  context->staticNBAAccumulators.resize(rootCount);
  std::mt19937 random(0x18002023);
  for (unsigned trial = 0; trial != 100; ++trial) {
    std::fill(dirty.begin(), dirty.end(), 0);
    std::fill(summary.begin(), summary.end(), 0);
    context->staticNBAAccumulatorsPending = false;
    uint32_t expectedGenerated = UINT32_MAX;
    uint32_t expectedRuntime = UINT32_MAX;
    for (uint32_t root = 0; root != rootCount; ++root) {
      roots[root].generated_accumulator = &generated[root];
      generated[root].valid = random() % 2;
      generated[root].exec_region = regions[random() % std::size(regions)];
      auto &runtime = context->staticNBAAccumulators[root];
      runtime.valid = false;
      runtime.execRegion = regions[random() % std::size(regions)];
      if (trial == 0 || (root != rootCount - 1 && random() % 1024 != 0))
        continue;
      dirty[root / 64] |= uint64_t{1} << (root % 64);
      summary[root / 4096] |= uint64_t{1} << ((root / 64) % 64);
      if (random() % 2) {
        markStaticNBAAccumulatorPending(context, root, runtime);
        expectedRuntime = std::min(expectedRuntime, runtime.execRegion);
      }
      if (generated[root].valid)
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
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTControlSelectsNBABarrierFromDirtyHierarchy) {
  constexpr uint32_t rootCount = 65;
  AOTTestState state;
  std::array<obelisk_rt_generated_nba_accumulator_256, rootCount> generated{};
  std::array<obelisk_rt_static_nba_root, rootCount> roots{};
  for (uint32_t root = 0; root != rootCount; ++root)
    roots[root] = {root, root + 1, 1, &generated[root]};
  std::array<uint8_t, 9> valuePlane{};
  std::array<uint8_t, 9> unknownPlane{};
  std::array<uint64_t, 2> dirtyRoots{0, 1};
  std::array<uint64_t, 1> dirtySummary{uint64_t{1} << 1};
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA;
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

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = rootCount;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  for (uint32_t root = 0; root != rootCount; ++root)
    ASSERT_EQ(
        obelisk_rt_v1_native_state_register_static(context, root + 1, root, 1),
        OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  generated[64].value[0] = 1;
  generated[64].write_mask[0] = 1;
  generated[64].valid = 1;
  generated[64].exec_region = OBELISK_RT_REGION_NBA;
  SchedulerFixture fixture(1);
  fixture.descriptor.execution = &execution;
  schedulerWaitKind = OBELISK_RT_SUSPEND_DELAY;
  schedulerResumeCount = 0;
  schedulerOrder.clear();
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add_aot(context, makeSchedulerInstance(fixture),
                                      0, 0, 0, nullptr, nullptr, 0, nullptr, 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(valuePlane[8] & 1, 1u);
  EXPECT_EQ(dirtyRoots[0], 0u);
  EXPECT_EQ(dirtyRoots[1], 0u);
  EXPECT_EQ(dirtySummary[0], 0u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GuardedNBAClaimsPreserveGeneratedLastWriteOrder) {
  AOTTestState state;
  obelisk_rt_generated_nba_accumulator_256 generated{};
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  uint32_t specializationFast = 0;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA |
               OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_commit = aotCommitOneNBARoot;
  plan.specialization_fast = &specializationFast;

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 256;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 256),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  state.runHook = runGuardedNBAOrdering;
  state.generatedNBA = &generated;
  state.generatedNBAFirst = true;
  state.generatedNBAValue = 0x11111111;
  state.claimedNBAValue = 0x22222222;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  uint32_t changed = 0;
  ASSERT_EQ(obelisk_rt_v1_static_nba_commit_root(
                context, 0, OBELISK_RT_REGION_NBA, &changed),
            OBELISK_RT_OK);
  EXPECT_EQ(changed, 1u);
  uint32_t observed = 0;
  std::memcpy(&observed, valuePlane.data(), sizeof(observed));
  EXPECT_EQ(observed, 0x22222222u);

  state.generatedNBAFirst = false;
  state.generatedNBAValue = 0x44444444;
  state.claimedNBAValue = 0x33333333;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  changed = 0;
  ASSERT_EQ(obelisk_rt_v1_static_nba_commit_root(
                context, 0, OBELISK_RT_REGION_NBA, &changed),
            OBELISK_RT_OK);
  EXPECT_EQ(changed, 1u);
  std::memcpy(&observed, valuePlane.data(), sizeof(observed));
  EXPECT_EQ(observed, 0x44444444u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBA256SnapshotPreservesReactiveCommitRegion) {
  AOTTestState state;
  obelisk_rt_generated_nba_accumulator_256 generated{};
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 256;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 256),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  generated.value[0] = 0x55;
  generated.write_mask[0] = UINT32_MAX;
  generated.valid = 1;
  generated.exec_region = OBELISK_RT_REGION_RE_NBA;
  obelisk_rt_aot_deopt_snapshot snapshot{};
  ASSERT_EQ(obelisk_rt_v1_scheduler_snapshot_aot(context, &snapshot),
            OBELISK_RT_OK);
  ASSERT_EQ(snapshot.nba_count, 1u);
  EXPECT_EQ(snapshot.nbas[0].exec_region, OBELISK_RT_REGION_RE_NBA);
  EXPECT_EQ(context->staticNBAAccumulators[0].execRegion,
            OBELISK_RT_REGION_RE_NBA);
  EXPECT_TRUE(std::all_of(std::begin(generated.write_mask),
                          std::end(generated.write_mask),
                          [](uint64_t mask) { return mask == 0; }));
  EXPECT_EQ(generated.valid, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBA256CommitsDirectlyWithoutFanout) {
  AOTTestState state;
  obelisk_rt_generated_nba_accumulator_256 generated{};
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  std::array<uint8_t, 32> nativeValue{};
  std::array<uint8_t, 32> nativeUnknown{};
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 256;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 256),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_sync(
                context, nativeValue.data(), nativeUnknown.data(), 256),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  generated.value[0] = 0x55;
  generated.value[3] = UINT64_C(0xaa00000000000000);
  generated.write_mask[0] = UINT32_MAX;
  generated.write_mask[3] = UINT64_C(0xffffffff00000000);
  generated.valid = 1;
  generated.exec_region = OBELISK_RT_REGION_NBA;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
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
  generated.exec_region = OBELISK_RT_REGION_NBA;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(valuePlane[0], 0xaa);
  EXPECT_EQ(valuePlane[31], 0x55);
  EXPECT_EQ(unknownPlane[0], 0x33);
  EXPECT_EQ(nativeValue, valuePlane);
  EXPECT_EQ(nativeUnknown, unknownPlane);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBA256BatchRejectsRootsThatNeedTransitions) {
  AOTTestState state;
  obelisk_rt_generated_nba_accumulator_256 generated{};
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 256;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 256),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  ASSERT_EQ(context->staticNBARootHasFanout.size(), 1u);
  context->staticNBARootHasFanout[0] = 1;

  generated.value[0] = 0x55;
  generated.write_mask[0] = UINT32_MAX;
  generated.valid = 1;
  generated.exec_region = OBELISK_RT_REGION_NBA;
  uint32_t changed = 0;
  ASSERT_EQ(obelisk_rt_v1_static_nba_commit_roots(
                context, 1, OBELISK_RT_REGION_NBA, &changed),
            OBELISK_RT_OK);
  EXPECT_EQ(changed, 1u);
  EXPECT_EQ(valuePlane[0], 0x55);
  // Transition-tracking commits keep canonical runtime state synchronized.
  // The no-fanout AVX batch intentionally defers that duplicate write.
  EXPECT_EQ(context->stateValue[0] & 0xff, 0x55);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBA256TracksTransitionsWithoutExactFanout) {
  constexpr uint64_t rootOffset = 5;
  constexpr uint64_t planeBits = 272;
  AOTTestState state;
  obelisk_rt_generated_nba_accumulator_256 generated{};
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated},
  };
  std::array<uint8_t, 34> valuePlane{};
  std::array<uint8_t, 34> unknownPlane{};
  valuePlane[0] = 0x1f;
  valuePlane[32] = 0xe0;
  valuePlane[33] = 0xa5;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = planeBits;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = planeBits;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->signalDiagnosticsEnabled = true;
  context->stateUnknown.assign(5, 0);
  ASSERT_EQ(
      obelisk_rt_v1_native_state_register_static(context, 1, rootOffset, 256),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  generated.value[0] = 0x55;
  generated.value[3] = UINT64_C(0xaa00000000000000);
  generated.write_mask[0] = UINT32_MAX;
  generated.write_mask[3] = UINT64_C(0xffffffff00000000);
  generated.valid = 1;
  generated.exec_region = OBELISK_RT_REGION_NBA;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
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
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, GeneratedNBA256CommitHonorsOverrideMask) {
  AOTTestState state;
  obelisk_rt_generated_nba_accumulator_256 generated{};
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 256, &generated},
  };
  std::array<uint8_t, 32> valuePlane{};
  std::array<uint8_t, 32> unknownPlane{};
  valuePlane[0] = 0x33;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 256;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 256;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(4, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 256),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  context->forceMask.assign(4, 0);
  context->forceMask[0] = 0xff;
  ASSERT_FALSE(context->nativeScheduleDirtyRootsPresent);

  generated.value[0] = 0x55;
  generated.write_mask[0] = UINT32_MAX;
  generated.valid = 1;
  generated.exec_region = OBELISK_RT_REGION_NBA;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(valuePlane[0], 0x33);
  EXPECT_EQ(context->stateValue[0] & 0xff, 0x33);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, TransientDirtyRootReconcilesWithoutRunnableActor) {
  AOTTestState state;
  uint8_t valuePlane = 0;
  uint8_t unknownPlane = 0;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = &valuePlane;
  plan.state_unknown = &unknownPlane;
  plan.state_bit_count = 8;

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  context->stateValue[0] = 0x5a;
  obelisk_rt_aot_external_write_range_unlocked(context, 0, 8, false);
  ASSERT_TRUE(context->nativeScheduleExternalWritePending);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(valuePlane, 0x5a);
  EXPECT_TRUE(context->nativeScheduleTransientDirtyRoots.empty());
  EXPECT_FALSE(context->nativeScheduleExternalWritePending);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, PersistentDirtyRootSurvivesPartialAndOverlappingRelease) {
  AOTTestState state;
  std::array<uint8_t, 16> valuePlane{};
  std::array<uint8_t, 16> unknownPlane{};
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = valuePlane.data();
  plan.state_unknown = unknownPlane.data();
  plan.state_bit_count = 128;

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 128;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(2, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 128),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

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
  obelisk_rt_aot_external_write_range_unlocked(context, 0, 128, true);

  context->forceMask[0] &= ~firstForce;
  obelisk_rt_aot_release_range_unlocked(context, 1, 1);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 1u);

  context->forceMask[1] &= ~secondForce;
  obelisk_rt_aot_release_range_unlocked(context, 70, 1);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 1u);

  // Releasing force reveals the still-active procedural assignment.
  context->forceMask[1] &= ~assigned;
  context->stateValue[1] |= assigned;
  obelisk_rt_aot_release_range_unlocked(context, 90, 1);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 1u);

  context->assignMask[1] &= ~assigned;
  obelisk_rt_aot_release_range_unlocked(context, 90, 1);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 0u);
  EXPECT_EQ(valuePlane[0] & firstForce, firstForce);
  EXPECT_EQ(valuePlane[8] & secondForce, secondForce);
  EXPECT_EQ(valuePlane[11] & uint8_t{1} << 2, uint8_t{1} << 2);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, PersistentReleaseReconcilesRetainedValueToGeneratedPlane) {
  AOTTestState state;
  uint8_t valuePlane = 0x11;
  uint8_t unknownPlane = 0;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL;
  plan.state_value = &valuePlane;
  plan.state_unknown = &unknownPlane;
  plan.state_bit_count = 8;

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  context->forceMask.assign(1, 0xff);
  context->stateValue[0] = 0xa5;
  obelisk_rt_aot_external_write_range_unlocked(context, 0, 8, true);
  ASSERT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 1u);
  EXPECT_EQ(valuePlane, 0x11);

  context->forceMask[0] = 0;
  obelisk_rt_aot_release_range_unlocked(context, 0, 8);
  EXPECT_EQ(context->nativeSchedulePersistentDirtyRoots.count(1), 0u);
  EXPECT_EQ(valuePlane, 0xa5);
  EXPECT_EQ(obelisk_rt_v1_static_specialization_guard(
                context, UINT32_MAX, 1, OBELISK_RT_STATIC_ROOT_READ),
            1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticNBASitesClipToStaticRootAndPreserveFourState) {
  AOTTestState state;
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 8, nullptr},
  };
  const obelisk_rt_static_nba_site sites[] = {
      {7, 0, OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR},
      {8, 0, OBELISK_RT_STATIC_NBA_FIXED_SLOT},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_sites = sites;
  plan.nba_site_count = std::size(sites);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 16;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 4, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  uint16_t valuePlane = 0;
  uint16_t unknownPlane = 0;
  uint8_t lowValue = 0xc;
  uint8_t lowUnknown = 0x4;
  uint8_t highValue = 0xf;
  uint8_t highUnknown = 0x3;
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  ASSERT_EQ(obelisk_rt_v1_scheduler_static_nba(
                context, 7, reinterpret_cast<uint8_t *>(&valuePlane),
                reinterpret_cast<uint8_t *>(&unknownPlane), 16,
                obelisk_rt_v1_native_handle_offset(root, -2), 4, &lowValue,
                &lowUnknown),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_static_nba(
                context, 8, reinterpret_cast<uint8_t *>(&valuePlane),
                reinterpret_cast<uint8_t *>(&unknownPlane), 16,
                obelisk_rt_v1_native_handle_offset(root, 6), 4, &highValue,
                &highUnknown),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(valuePlane, 0x0c30);
  EXPECT_EQ(unknownPlane, 0x0c10);
  EXPECT_EQ(context->stateValue[0], 0x0c30);
  EXPECT_EQ(context->stateUnknown[0], 0x0c10);
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticNBASitesPreserveMixedGenericExecutionOrder) {
  AOTTestState state;
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 8, nullptr},
  };
  const obelisk_rt_static_nba_site sites[] = {
      {7, 0, OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_sites = sites;
  plan.nba_site_count = std::size(sites);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  uint8_t plane = 0;
  uint8_t older = 0x55;
  uint8_t newer = 0xaa;
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(context, &plane, nullptr, 8, root, 8, 0,
                                        &older, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_static_nba(context, 7, &plane, nullptr, 8,
                                               root, 8, &newer, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(plane, 0xaa);
  EXPECT_EQ(context->stateValue[0], 0xaa);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticNBAExternalWriteSlowsOnlyIntersectingRoot) {
  AOTTestState state;
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 8, nullptr},
      {18, 2, 8, nullptr},
  };
  const obelisk_rt_static_nba_site sites[] = {
      {7, 0, OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR},
      {8, 1, OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.nba_roots = roots;
  plan.nba_root_count = std::size(roots);
  plan.nba_sites = sites;
  plan.nba_site_count = std::size(sites);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 16;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateUnknown.assign(1, 0);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 8, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  uint16_t plane = 0;
  uint8_t value = 0x5a;
  uint64_t cleanRoot = obelisk_rt_v1_native_state_static_handle(2);
  uint64_t dirtyRoot = obelisk_rt_v1_native_state_static_handle(1);
  obelisk_rt_aot_external_write_range_unlocked(context, 0, 8, false);
  ASSERT_EQ(obelisk_rt_v1_scheduler_static_nba(
                context, 8, reinterpret_cast<uint8_t *>(&plane), nullptr, 16,
                cleanRoot, 8, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->staticNBAAccumulators[1].valid);
  EXPECT_TRUE(context->scheduledNBAs.empty());

  ASSERT_EQ(obelisk_rt_v1_scheduler_static_nba(
                context, 7, reinterpret_cast<uint8_t *>(&plane), nullptr, 16,
                dirtyRoot, 8, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_FALSE(context->staticNBAAccumulators[0].valid);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().bitOffset, dirtyRoot);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTStaticNBATablesRejectDuplicateAndMismatchedRoots) {
  AOTTestState state;
  const obelisk_rt_static_nba_root duplicateRoots[] = {
      {17, 1, 8, nullptr},
      {18, 1, 8, nullptr},
  };
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC;
  plan.nba_roots = duplicateRoots;
  plan.nba_root_count = std::size(duplicateRoots);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_INVALID_ARGUMENT);

  const obelisk_rt_static_nba_root mismatchedRoot[] = {
      {17, 2, 8, nullptr},
  };
  plan.nba_roots = mismatchedRoot;
  plan.nba_root_count = std::size(mismatchedRoot);
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_LAYOUT_MISMATCH);

  const obelisk_rt_static_nba_root validRoot[] = {
      {17, 1, 8, nullptr},
  };
  const obelisk_rt_static_nba_site validSite[] = {
      {7, 0, OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR},
  };
  plan.nba_roots = validRoot;
  plan.nba_root_count = std::size(validRoot);
  plan.nba_sites = validSite;
  plan.nba_site_count = std::size(validSite);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  uint8_t plane = 0;
  uint8_t value = 1;
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  EXPECT_EQ(obelisk_rt_v1_scheduler_static_nba(context, 8, &plane, nullptr, 8,
                                               root, 8, &value, nullptr),
            OBELISK_RT_INVALID_DESIGN);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, DelayedNBAsAdvanceTimeAndPreserveQueueOrder) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  uint8_t plane = 0;
  uint8_t first = 0x35;
  uint8_t second = 0xa6;
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(context, &plane, nullptr, 8, 0, 8, 5,
                                        &first, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(context, &plane, nullptr, 8, 0, 8, 5,
                                        &second, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(plane, second);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, CanonicalNBAKeepsBoundNativeStateMirrorCoherent) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);

  uint8_t nativeValue = 0;
  uint8_t nativeUnknown = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_sync(context, &nativeValue,
                                            &nativeUnknown, 8),
            OBELISK_RT_OK);
  uint8_t value = 0xa6;
  uint8_t unknown = 0x18;
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                root, 8, 0, &value, &unknown),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  EXPECT_EQ(context->stateValue[0] & 0xff, value);
  EXPECT_EQ(context->stateUnknown[0] & 0xff, unknown);
  EXPECT_EQ(nativeValue, value);
  EXPECT_EQ(nativeUnknown, unknown);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, ProceduralPathsUseNativeDestinationBeforeCanonicalBinding) {
  for (auto [oldValue, oldUnknown, nextValue] :
       {std::tuple<uint8_t, uint8_t, uint8_t>{1, 0, 0}, {0, 1, 0}, {1, 1, 1}}) {
    obelisk_rt_execution_descriptor_v1 execution{};
    execution.version = OBELISK_RT_VERSION;
    execution.state_bit_count = 1;
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
              OBELISK_RT_OK);
    // Canonical storage exists, but only the coroutine's plane is current.
    // Deliberately make the stale image equal to the requested target.
    context->stateValue[0] = nextValue;
    context->stateUnknown[0] = 0;
    context->signalDiagnosticsEnabled = true;
    uint8_t nativeValue = oldValue, nativeUnknown = oldUnknown;
    uint8_t known = 0, write = 1, inactive = 0;
    uint64_t handle = obelisk_rt_v1_native_state_static_handle(1);
    ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_path_storage(
                  context, &nativeValue, &nativeUnknown, 1, handle, 1, 17, 0, 0,
                  1, 0, 4, 4, 4, &nextValue, &known, &write, &inactive,
                  &inactive, &inactive, &inactive),
              OBELISK_RT_OK);
    EXPECT_EQ(nativeValue, nextValue);
    EXPECT_EQ(nativeUnknown, 0);
    EXPECT_EQ(context->signalDiagnostics.publications, 1u);
    EXPECT_TRUE(context->scheduledInertialPathNBAs.empty());
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(Scheduler, InertialGateDriversUsePerBitTransitionDelays) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 4;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 4),
            OBELISK_RT_OK);

  // Exercise 0->1, 1->0, 0->z, and 0->x in bit order. IEEE 1800-2017
  // Table 28-9 selects rise, fall, turn-off, and the least delay.
  context->stateValue[0] = 0b0010;
  context->stateUnknown[0] = 0;
  uint8_t value = 0b0101;
  uint8_t unknown = 0b1100;
  uint64_t handle = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 17, 3, OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION,
                7, 11, 13, &value, &unknown),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 4u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 7u);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 11u);
  EXPECT_EQ(context->scheduledNBAs[2].dueTime, 13u);
  EXPECT_EQ(context->scheduledNBAs[3].dueTime, 7u);
  ASSERT_EQ(context->inertialDriverPending.size(), 1u);
  EXPECT_EQ(context->inertialDriverPending.begin()->second.remaining, 4u);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->schedulerTime, 13u);
  EXPECT_EQ(context->stateValue[0] & 0xf, value);
  EXPECT_EQ(context->stateUnknown[0] & 0xf, unknown);
  EXPECT_TRUE(context->inertialDriverPending.empty());
  obelisk_rt_v1_context_destroy(context);
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
  writeDescriptor(0, obelisk::designbytecode::kNetStateDescriptor,
                  /*four-state wire=*/1, /*net=*/0, UINT64_MAX);
  writeDescriptor(1, obelisk::designbytecode::kDriverStateDescriptor,
                  /*strong0, highz1=*/185, /*driver=*/8, /*net=*/0);
  writeDescriptor(2, obelisk::designbytecode::kDriverStateDescriptor,
                  /*highz0, strong1, high bank=*/2953, /*driver=*/16,
                  /*net=*/0);
  obelisk::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 3;
  image.stateBitCount = 17;
  ASSERT_TRUE(obelisk::designbytecode::validateImage(image));

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 17;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 8, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 16, 1),
            OBELISK_RT_OK);
  context->designBytecodeImage = image;
  // Net and both driver banks start at z.
  context->stateValue[0] =
      (uint64_t{1} << 0) | (uint64_t{1} << 8) | (uint64_t{1} << 16);
  context->stateUnknown[0] = context->stateValue[0];
  std::array<uint8_t, 3> nativeValue{1, 1, 1};
  std::array<uint8_t, 3> nativeUnknown{1, 1, 1};
  ASSERT_EQ(obelisk_rt_v1_native_state_sync(context, nativeValue.data(),
                                            nativeUnknown.data(), 17),
            OBELISK_RT_OK);
  uint64_t low = obelisk_rt_v1_native_state_static_handle(1);
  uint64_t high = obelisk_rt_v1_native_state_static_handle(2);
  uint8_t zero = 0;
  uint8_t one = 1;
  auto schedule = [&](uint8_t lowValue, uint8_t lowUnknown, uint8_t highValue,
                      uint8_t highUnknown, uint8_t transitionValue,
                      uint8_t transitionUnknown) {
    return obelisk_rt_v1_scheduler_inertial_driver_strength_pair(
        context, nativeValue.data(), nativeUnknown.data(), 17, low, high, 1, 41,
        3, /*rise=*/7, /*fall=*/11, /*turnoff=*/13, &lowValue, &lowUnknown,
        &highValue, &highUnknown, &transitionValue, &transitionUnknown);
  };

  // The runtime trust boundary rejects handles that do not identify the
  // adjacent complementary banks of one logical driver range.
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver_strength_pair(
                context, nativeValue.data(), nativeUnknown.data(), 17, low, low,
                1, 42, 3, 7, 11, 13, &zero, &zero, &one, &one, &zero, &zero),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_TRUE(context->scheduledNBAs.empty());

  // A logical 0 changes only the low bank, but the unchanged high-bank marker
  // still resolves the completed pair at the falling deadline.
  ASSERT_EQ(schedule(zero, zero, one, one, zero, zero), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 11u);
  EXPECT_TRUE(context->scheduledNBAs[0].deferDriverResolution);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 11u);
  EXPECT_TRUE(context->scheduledNBAs[1].forceDriverResolution);
  context->schedulerTime = 3;
  ASSERT_EQ(schedule(zero, zero, one, one, zero, zero), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 11u);

  // Returning to z before maturity rejects both halves of the pulse.
  context->schedulerTime = 4;
  ASSERT_EQ(schedule(one, one, one, one, one, one), OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());

  // A known 1 uses the rise delay and publishes only after both strength
  // banks have matured.
  ASSERT_EQ(schedule(one, one, one, zero, one, zero), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 11u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->schedulerTime, 11u);
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 0u);
  EXPECT_TRUE(context->inertialDriverPending.empty());

  // L/H ranges use the x delay, the minimum of all three values (§28.6),
  // rather than the z-looking representation of either strength bank.
  ASSERT_EQ(schedule(one, one, zero, one, zero, one), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 18u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->stateUnknown[0] & 1, 1u);

  // A real high-impedance output uses the third (turn-off) delay.
  ASSERT_EQ(schedule(one, one, one, one, one, one), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 31u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 1u);
  EXPECT_EQ(nativeValue[0] & 1, 1u);
  EXPECT_EQ(nativeUnknown[0] & 1, 1u);
  obelisk_rt_v1_context_destroy(context);
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
  writeDescriptor(0, obelisk::designbytecode::kNetStateDescriptor, 1, 0,
                  UINT64_MAX);
  writeDescriptor(1, obelisk::designbytecode::kDriverStateDescriptor, 185, 4,
                  0);
  writeDescriptor(2, obelisk::designbytecode::kDriverStateDescriptor, 2953, 8,
                  0);
  obelisk::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.stateDescriptorCount = 3;
  image.stateBitCount = 12;
  ASSERT_TRUE(obelisk::designbytecode::validateImage(image));

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 12;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 4, 4),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 8, 4),
            OBELISK_RT_OK);
  context->designBytecodeImage = image;
  context->stateValue[0] = 0xfff;
  context->stateUnknown[0] = 0xfff;

  uint64_t low = obelisk::designbytecode::encodeStaticHandle(1, -2);
  uint64_t matchingHigh = obelisk::designbytecode::encodeStaticHandle(2, -2);
  uint64_t mismatchedHigh = obelisk::designbytecode::encodeStaticHandle(2, -1);
  uint8_t zero = 0;
  uint8_t z = 0xf;
  auto schedule = [&](uint64_t high) {
    return obelisk_rt_v1_scheduler_inertial_driver_strength_pair(
        context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
        reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 12, low,
        high, 4, 71, 4, 7, 11, 13, &zero, &zero, &z, &z, &zero, &zero);
  };
  EXPECT_EQ(schedule(mismatchedHigh), OBELISK_RT_INVALID_HANDLE);
  EXPECT_TRUE(context->scheduledNBAs.empty());

  ASSERT_EQ(schedule(matchingHigh), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 4u);
  EXPECT_EQ(context->scheduledNBAs[0].bitOffset,
            obelisk_rt_v1_native_state_static_handle(1));
  EXPECT_EQ(context->scheduledNBAs[1].bitOffset,
            obelisk_rt_v1_native_state_static_handle(2));
  EXPECT_EQ(context->scheduledNBAs[2].bitOffset,
            obelisk::designbytecode::encodeStaticHandle(1, 1));
  EXPECT_EQ(context->scheduledNBAs[3].bitOffset,
            obelisk::designbytecode::encodeStaticHandle(2, 1));
  for (const auto &update : context->scheduledNBAs)
    EXPECT_EQ(update.dueTime, 11u);
  obelisk_rt_v1_context_destroy(context);
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
  writeDescriptor(0, obelisk::designbytecode::kNetStateDescriptor,
                  /*four-state | delayed=*/9, /*net=*/0,
                  /*delay constant offset=*/0, /*width=*/4);
  writeDescriptor(1, obelisk::designbytecode::kDriverStateDescriptor,
                  /*four-state, default strong strengths=*/1, /*driver=*/8,
                  /*target net=*/0, /*width=*/4);

  obelisk::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.constants = constantsOffset;
  image.constantSize = 24;
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 2;
  image.stateBitCount = 12;

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 12;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 4),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 8, 4),
            OBELISK_RT_OK);

  // Begin at Z and resolve one driver to 1, 0, X, Z. Table 28-9 selects
  // rise, fall, the least delay, and no event for the unchanged Z bit.
  context->stateValue[0] = 0b1001'0000'1111;
  context->stateUnknown[0] = 0b1100'0000'1111;
  bool changed = false;
  ASSERT_TRUE(obelisk::designbytecode::resolveDrivenNets(image, context, 8, 12,
                                                         changed));
  EXPECT_FALSE(changed);
  EXPECT_EQ(context->stateValue[0] & 0xf, 0xfu);
  ASSERT_EQ(context->scheduledNBAs.size(), 3u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 7u);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 11u);
  EXPECT_EQ(context->scheduledNBAs[2].dueTime, 7u);

  context->designBytecodeImage = image;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
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
  ASSERT_TRUE(obelisk::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 18u);
  context->schedulerTime = 14;
  ASSERT_TRUE(obelisk::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 18u);
  context->schedulerTime = 15;
  context->stateValue[0] &= ~(uint64_t{1} << 8);
  ASSERT_TRUE(obelisk::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialNetPending.empty());
  EXPECT_EQ(context->stateValue[0] & 1, 0u);

  // A propagation-delayed driver can mature while the scheduler is draining
  // this same queue. Its resolved net transition must be safely appended as a
  // second inertial stage rather than invalidating the active driver event.
  uint8_t one = 1;
  uint8_t known = 0;
  uint64_t driverHandle = obelisk_rt_v1_native_state_static_handle(2);
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 12,
                driverHandle, 1, 99, 0, OBELISK_RT_INERTIAL_DRIVER_VECTOR_DELAY,
                3, 3, 3, &one, &known),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 18u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->schedulerTime, 25u);
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 0u);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());
  EXPECT_TRUE(context->inertialNetPending.empty());
  obelisk_rt_v1_context_destroy(context);
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
  writeDescriptor(0, obelisk::designbytecode::kNetStateDescriptor,
                  1u | 8u | 2u | 64u, 0, 0);
  writeDescriptor(1, obelisk::designbytecode::kDriverStateDescriptor,
                  1u | 2u | 8192u, 8, 0);

  obelisk::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.constants = 0;
  image.constantSize = 24;
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 2;
  image.stateBitCount = 9;
  ASSERT_TRUE(obelisk::designbytecode::validateImage(image));

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 9;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 8, 1),
            OBELISK_RT_OK);
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
    ASSERT_TRUE(obelisk::designbytecode::resolveDrivenNets(image, context, 8, 9,
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->schedulerTime, 27u);
  EXPECT_EQ(state(), std::pair(false, true));
  EXPECT_TRUE(context->inertialNetPending.empty());

  // A driven x ends pending decay and uses min(rise, fall), never the third
  // charge-decay value, as its ordinary propagation delay.
  context->schedulerTime = 30;
  set(8, true, false);
  resolve();
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->schedulerTime, 47u);
  EXPECT_EQ(state(), std::pair(false, true));

  // The fall delay applies when a driver supplies 0, and stored 0 decays by
  // the same third value as stored 1.
  context->schedulerTime = 50;
  set(8, false, false);
  resolve();
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 61u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(state(), std::pair(false, false));
  set(8, true, true);
  resolve();
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 74u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
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
  obelisk_rt_v1_context_destroy(context);

  // Omitted decay is encoded separately from zero-time decay and is legal
  // only for an effectively trireg component. It retains known charge forever.
  write64(16, UINT64_MAX);
  ASSERT_TRUE(obelisk::designbytecode::validateImage(image));
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  set(0, true, false);
  set(8, true, true);
  bool changed = false;
  ASSERT_TRUE(obelisk::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 0u);
  obelisk_rt_v1_context_destroy(context);

  // Zero is a real decay time, not the omitted-decay sentinel. It schedules
  // the stored known value to become x in the current time slot.
  write64(16, 0);
  ASSERT_TRUE(obelisk::designbytecode::validateImage(image));
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 8, 1),
            OBELISK_RT_OK);
  context->designBytecodeImage = image;
  set(0, true, false);
  set(8, true, true);
  changed = false;
  ASSERT_TRUE(obelisk::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 0u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(state(), std::pair(false, true));
  obelisk_rt_v1_context_destroy(context);

  // The image trust boundary rejects the same omitted third slot for an
  // ordinary wire, and rejects malformed partial/all-absent uniform triples.
  write64(16, UINT64_MAX);
  write32(descriptorsOffset + 4, 1u | 8u);
  write32(descriptorsOffset + 32 + 4, 1u);
  EXPECT_FALSE(obelisk::designbytecode::validateImage(image));
  write32(descriptorsOffset + 4, 1u | 8u | 2u | 64u);
  write32(descriptorsOffset + 32 + 4, 1u | 2u | 8192u);
  write64(0, UINT64_MAX);
  EXPECT_FALSE(obelisk::designbytecode::validateImage(image));
  write64(8, UINT64_MAX);
  EXPECT_FALSE(obelisk::designbytecode::validateImage(image));
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
  writeDescriptor(0, obelisk::designbytecode::kNetStateDescriptor,
                  1u | 8u | 2u | 64u | 128u, 0, 0);
  writeDescriptor(1, obelisk::designbytecode::kNetStateDescriptor,
                  1u | 8u | 2u | 64u | 384u, 1, 24);
  writeDescriptor(2, obelisk::designbytecode::kDriverStateDescriptor,
                  1u | 2u | 8192u, 8, 0);
  write64(connectivityOffset, 0);
  write64(connectivityOffset + 8, 1);
  write64(connectivityOffset + 16, 1);
  bytes[connectivityOffset + 24] = 9;
  bytes[connectivityOffset + 25] = 9;

  obelisk::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.constants = 0;
  image.constantSize = 48;
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 3;
  image.connectivity = connectivityOffset;
  image.connectivityCount = 1;
  image.stateBitCount = 9;
  ASSERT_TRUE(obelisk::designbytecode::validateImage(image));

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 9;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 2),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 8, 1),
            OBELISK_RT_OK);
  context->designBytecodeImage = image;
  context->stateValue[0] = (uint64_t{1} << 1) | (uint64_t{1} << 8);
  context->stateUnknown[0] = uint64_t{1} << 8;
  bool changed = false;
  ASSERT_TRUE(obelisk::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  EXPECT_TRUE(changed);
  EXPECT_EQ(context->stateValue[0] & 3, 3u);
  EXPECT_EQ(context->stateUnknown[0] & 3, 0u);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 13u);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 13u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->stateValue[0] & 3, 0u);
  EXPECT_EQ(context->stateUnknown[0] & 3, 3u);
  obelisk_rt_v1_context_destroy(context);
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
  writeDescriptor(0, obelisk::designbytecode::kNetStateDescriptor, 9, 0, 0);
  writeDescriptor(1, obelisk::designbytecode::kNetStateDescriptor, 9, 1, 24);
  writeDescriptor(2, obelisk::designbytecode::kDriverStateDescriptor, 1, 8, 0);
  write64(connectivityOffset, 0);
  write64(connectivityOffset + 8, 1);
  write64(connectivityOffset + 16, 1);

  obelisk::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.constants = constantsOffset;
  image.constantSize = 48;
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 3;
  image.connectivity = connectivityOffset;
  image.connectivityCount = 1;
  image.stateBitCount = 9;
  ASSERT_TRUE(obelisk::designbytecode::validateImage(image));

  // The bytecode trust boundary rejects aliases that do not carry one
  // normalized dominating delay.
  write64(constantsOffset + 24, 17);
  EXPECT_FALSE(obelisk::designbytecode::validateImage(image));
  write64(constantsOffset + 24, 7);

  // Bitwise delay metadata requires the delayed flag and uses only complete
  // all-UINT64_MAX triples as the absent-bit sentinel.
  write32(descriptorsOffset + 4, 17);
  write64(descriptorsOffset + 16, UINT64_MAX);
  EXPECT_FALSE(obelisk::designbytecode::validateImage(image));
  write32(descriptorsOffset + 4, 25);
  write64(descriptorsOffset + 16, 0);
  write64(constantsOffset, UINT64_MAX);
  EXPECT_FALSE(obelisk::designbytecode::validateImage(image));
  write32(descriptorsOffset + 4, 9);
  write64(constantsOffset, 7);

  constexpr uint64_t observerID = 77;
  obelisk_rt_observer_descriptor_v1 observer{observerID,
                                             nullptr,
                                             0,
                                             1,
                                             0,
                                             OBELISK_RT_OBSERVER_NO_BYTECODE,
                                             collapsedAliasObserverEvaluator,
                                             0};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 9;
  execution.observers = &observer;
  execution.observer_count = 1;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 1, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 3, 8, 1),
            OBELISK_RT_OK);

  struct AliasObserverWait {
    obelisk_rt_computed_wait_record_v1 wait{};
    obelisk_rt_computed_observer_v1 observer{};
    obelisk_rt_computed_dependency_v1 dependency{};
    obelisk_rt_computed_clause_v1 clause{};
    uint64_t previousValue = 0;
    uint64_t previousUnknown = 0;
  } record;
  record.wait = {OBELISK_RT_VERSION,
                 OBELISK_RT_SUSPEND_OBSERVER,
                 OBELISK_RT_COMPUTED_WAIT_INTERLEAVED,
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
      obelisk_rt_canonical_state_handle_unlocked(context, 0, 1);
  ASSERT_NE(aliasHandle, UINT64_MAX);
  record.dependency = {aliasHandle, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
  record.clause = {0, OBELISK_RT_OBSERVER_CONDITION_NONE,
                   OBELISK_RT_WAIT_EDGE_NEGEDGE, 0};
  ASSERT_TRUE(obelisk_rt_validate_computed_wait_record(&execution, &record.wait,
                                                       sizeof(record)));
  obelisk_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  obelisk_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.frame = &record;
  instance.frame_size = sizeof(record);
  instance.context = context;
  context->scheduledProcesses.emplace_back();
  ScheduledProcess &scheduled = context->scheduledProcesses.back();
  scheduled.instance = &instance;
  scheduled.token = 1;
  scheduled.waitSize = sizeof(record);
  scheduled.suspendKind = OBELISK_RT_SUSPEND_OBSERVER;
  scheduled.started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  ASSERT_TRUE(obelisk_rt_register_computed_signal_wait_unlocked(
      context, &record.wait, scheduled.token, false,
      scheduled.signalSubscriptions, scheduled.signalLatch));

  collapsedAliasObserverSamples.clear();
  context->stateValue[0] = (uint64_t{1} << 8) | 3;
  context->stateUnknown[0] = 3;
  bool changed = false;
  ASSERT_TRUE(obelisk::designbytecode::resolveDrivenNets(image, context, 8, 9,
                                                         changed));
  EXPECT_FALSE(changed);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 7u);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 7u);
  context->designBytecodeImage = image;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->stateValue[0] & 3, 3u);
  EXPECT_EQ(context->stateUnknown[0] & 3, 0u);
  EXPECT_EQ(collapsedAliasObserverSamples, std::vector<uint32_t>({3}));
  EXPECT_TRUE(context->inertialNetPending.empty());
  obelisk_rt_unregister_signal_wait_unlocked(
      context, scheduled.signalSubscriptions, scheduled.token, false);
  scheduled.instance = nullptr;
  context->scheduledProcessIndices.clear();
  context->scheduledProcesses.clear();
  obelisk_rt_v1_context_destroy(context);
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
  writeDescriptor(0, obelisk::designbytecode::kNetStateDescriptor,
                  /*four-state | delayed | bitwise=*/25, 0, 0);
  writeDescriptor(1, obelisk::designbytecode::kDriverStateDescriptor, 1, 8, 0);

  obelisk::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.constants = 0;
  image.constantSize = 48;
  image.stateDescriptors = descriptorsOffset;
  image.stateDescriptorCount = 2;
  image.stateBitCount = 10;
  ASSERT_TRUE(obelisk::designbytecode::validateImage(image));
  image.constantSize = 47;
  EXPECT_FALSE(obelisk::designbytecode::validateImage(image));
  image.constantSize = 48;
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 10;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 2),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 8, 2),
            OBELISK_RT_OK);
  context->stateValue[0] = (uint64_t{3} << 8) | 3;
  context->stateUnknown[0] = 3;
  bool changed = false;
  ASSERT_TRUE(obelisk::designbytecode::resolveDrivenNets(image, context, 8, 10,
                                                         changed));
  EXPECT_TRUE(changed);
  EXPECT_EQ(context->stateValue[0] & 3, 3u);
  EXPECT_EQ(context->stateUnknown[0] & 3, 1u);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 7u);
  context->designBytecodeImage = image;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->stateUnknown[0] & 3, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialVectorDriversRejectPulsesAndKeepStableDeadlines) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 4;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 4),
            OBELISK_RT_OK);
  context->stateValue[0] = 0;
  context->stateUnknown[0] = 0;
  uint64_t handle = obelisk_rt_v1_native_state_static_handle(1);
  uint32_t flags = OBELISK_RT_INERTIAL_DRIVER_VECTOR_DELAY |
                   OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION;
  uint8_t one = 1;
  uint8_t zero = 0;

  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 23, 9, flags, 10, 20, 30, &one, &zero),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);

  // Re-evaluating to the same target does not restart the propagation delay.
  context->schedulerTime = 3;
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 23, 9, flags, 10, 20, 30, &one, &zero),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);

  // Returning to the current driver value before the deadline suppresses the
  // pulse and leaves no replacement event (10.3.3 b-d).
  context->schedulerTime = 4;
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 23, 9, flags, 10, 20, 30, &zero, &zero),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());
  EXPECT_EQ(context->stateValue[0] & 0xf, 0u);

  // A known nonzero-to-zero vector transition uses the falling delay.
  context->stateValue[0] = 0b0100;
  context->schedulerTime = 8;
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 23, 9, flags, 10, 20, 30, &zero, &zero),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 28u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialVectorInitialHighZToZeroUsesFallDelay) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 4;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 4),
            OBELISK_RT_OK);

  // IEEE 1800-2017 10.3.3: the initial Z-to-zero vector transition uses the
  // falling delay, not the rising delay.
  context->stateValue[0] = 0xf;
  context->stateUnknown[0] = 0xf;
  uint8_t zero = 0;
  uint64_t handle = obelisk_rt_v1_native_state_static_handle(1);
  uint32_t flags = OBELISK_RT_INERTIAL_DRIVER_VECTOR_DELAY |
                   OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION;
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 24, 10, flags, 5, 3, 5, &zero, &zero),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 3u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialDriversAcceptGeneratedSchedulePlanes) {
  // IEEE 1800-2017 10.3.3: a delayed continuous assignment drives its net
  // through the inertial driver. A generated schedule owns its state planes
  // and passes those, not the canonical ones, so refusing them would fail the
  // delay at run time in the native tier alone.
  AOTTestState state;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state);
  std::array<uint8_t, 1> planValue{};
  std::array<uint8_t, 1> planUnknown{};
  plan.state_value = planValue.data();
  plan.state_unknown = planUnknown.data();
  plan.state_bit_count = 4;
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 4;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 4),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  uint64_t handle = obelisk_rt_v1_native_state_static_handle(1);
  uint8_t one = 1;
  uint8_t zero = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context, plan.state_value, plan.state_unknown, 4, handle, 1, 31,
                0, 0, 10, 20, 30, &one, &zero),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);
  EXPECT_EQ(context->scheduledNBAs.front().valuePlane, plan.state_value);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialPendingTargetsIncludeTheirDestination) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  uint64_t handle = obelisk_rt_v1_native_state_static_handle(1);
  uint8_t value = 1;
  uint8_t unknown = 0;
  uint32_t flags = OBELISK_RT_INERTIAL_DRIVER_VECTOR_DELAY |
                   OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION;
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                handle, 4, 29, 2, flags, 10, 20, 30, &value, &unknown),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);

  // A dynamic lvalue selection can move while retaining the same value. The
  // destination is therefore part of the pending propagation target.
  context->schedulerTime = 3;
  uint64_t upper = obelisk_rt_v1_native_handle_offset(handle, 4);
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                upper, 4, 29, 2, flags, 10, 20, 30, &value, &unknown),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().bitOffset, upper);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 13u);

  // Unknown dynamic selections suppress the drive and still reject the old
  // pending propagation event from this statement.
  context->schedulerTime = 4;
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                UINT64_MAX, 4, 29, 2, flags, 10, 20, 30, nullptr, nullptr),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());

  // A partial overlap clips both the destination and the corresponding low
  // source bits, matching ordinary driver-store view semantics.
  context->schedulerTime = 5;
  value = 0b1101;
  uint64_t partial = obelisk_rt_v1_native_handle_offset(handle, -2);
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                partial, 4, 29, 2, flags, 10, 20, 30, &value, &unknown),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().bitOffset, handle);
  EXPECT_EQ(context->scheduledNBAs.front().bitWidth, 2u);
  ASSERT_EQ(context->scheduledNBAs.front().value.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().value.front() & 3, 3);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 15u);

  // A fully out-of-range selection likewise suppresses the drive and cancels
  // the partially overlapping event.
  context->schedulerTime = 6;
  uint64_t outside = obelisk_rt_v1_native_handle_offset(handle, 8);
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 8,
                outside, 4, 29, 2, flags, 10, 20, 30, &value, &unknown),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, InertialKnownTargetsArePlaneRepresentationIndependent) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 4;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 4),
            OBELISK_RT_OK);
  uint64_t handle = obelisk_rt_v1_native_state_static_handle(1);
  uint8_t value = 1;
  uint8_t unknown = 0;
  uint32_t flags = OBELISK_RT_INERTIAL_DRIVER_VECTOR_DELAY |
                   OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION;

  // Native two-state lowering omits the unknown plane.
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_inertial_driver(
          context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
          nullptr, 4, handle, 4, 31, 7, flags, 10, 20, 30, &value, nullptr),
      OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);

  // Bytecode and four-state lowering provide an explicit all-zero unknown
  // plane. The known target is identical and must retain its first deadline.
  context->schedulerTime = 3;
  ASSERT_EQ(obelisk_rt_v1_scheduler_inertial_driver(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                reinterpret_cast<uint8_t *>(context->stateUnknown.data()), 4,
                handle, 4, 31, 7, flags, 10, 20, 30, &value, &unknown),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().dueTime, 10u);
  obelisk_rt_v1_context_destroy(context);
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
  writeLayout(0, OBELISK_RT_DBREG_LOGIC, 4, 0, 16);
  writeLayout(1, OBELISK_RT_DBREG_HANDLE, 0, 16, 32);
  for (uint32_t index = 2; index != 8; ++index)
    writeLayout(index, OBELISK_RT_DBREG_BITS, 64, 48 + uint64_t{index - 2} * 8,
                8);
  write32(intrinsicOffset, OBELISK_RT_INTRINSIC_V1_INERTIAL_DRIVER);
  write32(intrinsicOffset + 4, 8);
  write32(siteOffset + 8, 8);
  for (uint32_t index = 0; index != 8; ++index)
    write32(operandOffset + uint64_t{index} * 8 + 4, index);

  obelisk::designbytecode::Image image{};
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
  obelisk::designbytecode::Frame frame{};
  frame.function.layoutCount = 8;
  frame.function.scratchSize = frameData.size();
  frame.data = frameData.data();
  ASSERT_TRUE(
      obelisk::designbytecode::validIntrinsic(image, frame.function, 0));
  uint64_t value = 0b1101;
  std::memcpy(frameData.data(), &value, sizeof(value));
  uint32_t kind = OBELISK_RT_DESCRIPTOR_DRIVER;
  std::memcpy(frameData.data() + 16, &kind, sizeof(kind));
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  std::memcpy(frameData.data() + 24, &root, sizeof(root));
  auto setView = [&](int64_t start, int64_t end) {
    std::memcpy(frameData.data() + 32, &start, sizeof(start));
    std::memcpy(frameData.data() + 40, &end, sizeof(end));
  };
  const std::array<uint64_t, 6> arguments{
      {10, 20, 30, 37, 5,
       OBELISK_RT_INERTIAL_DRIVER_VECTOR_DELAY |
           OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION}};
  for (size_t index = 0; index != arguments.size(); ++index)
    std::memcpy(frameData.data() + 48 + index * 8, &arguments[index], 8);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);

  setView(0, 8);
  ASSERT_EQ(obelisk::designbytecode::invokeIntrinsic(image, frame, context, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().bitOffset, root);
  EXPECT_EQ(context->scheduledNBAs.front().bitWidth, 4u);

  // An unknown selection cancels the pending event at this bytecode site.
  setView(obelisk::designbytecode::kInvalidHandleStart, 8);
  ASSERT_EQ(obelisk::designbytecode::invokeIntrinsic(image, frame, context, 0),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());

  // A low-side overlap drops the two out-of-range source bits and schedules
  // only source bits 2..3 into destination bits 0..1.
  setView(-2, 8);
  ASSERT_EQ(obelisk::designbytecode::invokeIntrinsic(image, frame, context, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().bitOffset, root);
  EXPECT_EQ(context->scheduledNBAs.front().bitWidth, 2u);
  ASSERT_EQ(context->scheduledNBAs.front().value.size(), 1u);
  EXPECT_EQ(context->scheduledNBAs.front().value.front() & 3, 3);

  // A fully out-of-range selection is suppressed and cancels that event.
  setView(8, 8);
  ASSERT_EQ(obelisk::designbytecode::invokeIntrinsic(image, frame, context, 0),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());
  obelisk_rt_v1_context_destroy(context);
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
  writeLayout(0, OBELISK_RT_DBREG_LOGIC, 1, 0, 16);
  writeLayout(1, OBELISK_RT_DBREG_HANDLE, 0, 16, 32);
  writeLayout(2, OBELISK_RT_DBREG_LOGIC, 1, 48, 16);
  writeLayout(3, OBELISK_RT_DBREG_HANDLE, 0, 64, 32);
  writeLayout(4, OBELISK_RT_DBREG_LOGIC, 1, 96, 16);
  for (uint32_t index = 5; index != 10; ++index)
    writeLayout(index, OBELISK_RT_DBREG_BITS, 64, 112 + uint64_t{index - 5} * 8,
                8);
  write32(intrinsicOffset,
          OBELISK_RT_INTRINSIC_V1_INERTIAL_DRIVER_STRENGTH_PAIR);
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
  writeDescriptor(0, obelisk::designbytecode::kNetStateDescriptor, 1, 0,
                  UINT64_MAX);
  writeDescriptor(1, obelisk::designbytecode::kDriverStateDescriptor, 185, 1,
                  0);
  writeDescriptor(2, obelisk::designbytecode::kDriverStateDescriptor, 2953, 2,
                  0);

  obelisk::designbytecode::Image image{};
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
  ASSERT_TRUE(obelisk::designbytecode::validateImage(image));
  std::array<uint8_t, 160> frameData{};
  obelisk::designbytecode::Frame frame{};
  frame.function.layoutCount = 10;
  frame.function.scratchSize = frameData.size();
  frame.data = frameData.data();
  ASSERT_TRUE(
      obelisk::designbytecode::validIntrinsic(image, frame.function, 0));
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
  uint32_t kind = OBELISK_RT_DESCRIPTOR_DRIVER;
  auto writeHandle = [&](uint64_t offset, uint64_t root) {
    std::memcpy(frameData.data() + offset, &kind, 4);
    std::memcpy(frameData.data() + offset + 8, &root, 8);
    int64_t start = 0;
    int64_t end = 1;
    std::memcpy(frameData.data() + offset + 16, &start, 8);
    std::memcpy(frameData.data() + offset + 24, &end, 8);
  };
  uint64_t lowRoot = obelisk_rt_v1_native_state_static_handle(1);
  uint64_t highRoot = obelisk_rt_v1_native_state_static_handle(2);
  writeHandle(16, lowRoot);
  writeHandle(64, highRoot);
  const std::array<uint64_t, 5> arguments{{7, 11, 13, 43, 2}};
  for (size_t index = 0; index != arguments.size(); ++index)
    std::memcpy(frameData.data() + 112 + index * 8, &arguments[index], 8);

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 3;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 1, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 2, 1),
            OBELISK_RT_OK);
  context->designBytecodeImage = image;
  context->stateValue[0] = 0b110;
  context->stateUnknown[0] = 0b110;
  ASSERT_EQ(obelisk::designbytecode::invokeIntrinsic(image, frame, context, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledNBAs.size(), 2u);
  EXPECT_EQ(context->scheduledNBAs[0].dueTime, 7u);
  EXPECT_EQ(context->scheduledNBAs[1].dueTime, 7u);

  int64_t invalid = obelisk::designbytecode::kInvalidHandleStart;
  std::memcpy(frameData.data() + 32, &invalid, 8);
  std::memcpy(frameData.data() + 80, &invalid, 8);
  ASSERT_EQ(obelisk::designbytecode::invokeIntrinsic(image, frame, context, 0),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledNBAs.empty());
  EXPECT_TRUE(context->inertialDriverPending.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, DelayedStringNBAsRootValuesAndCompareByteContents) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);

  obelisk_rt_string_v1 first = 0;
  obelisk_rt_string_v1 second = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "delayed value", 13, &first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "delayed value", 13, &second),
            OBELISK_RT_OK);
  ASSERT_NE(first, second);
  obelisk_rt_string_v1 plane = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_string_nba(
                context, reinterpret_cast<uint8_t *>(&plane), 64, 0, 1, first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_string_nba(
                context, reinterpret_cast<uint8_t *>(&plane), 64, 0, 2, second),
            OBELISK_RT_OK);
  first = 0;
  second = 0;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);

  uint64_t epoch = context->schedulerEpoch;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->schedulerEpoch, epoch + 1);
  char scratch[8]{};
  const char *bytes = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_string_view(plane, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string_view(bytes, size), "delayed value");

  plane = 0;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, MaximumRepresentableDueTimeIsNotDropped) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  uint8_t plane = 0;
  uint8_t value = 0xa5;
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(context, &plane, nullptr, 8, 0, 8,
                                        UINT64_MAX, &value, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(plane, value);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AOTObserverPlaneAuthorityIsExplicitNotDepthDerived) {
  AOTTestState state;
  state.runHook = runObserverPlaneAuthority;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL;

  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  uint8_t generatedPlane = UINT8_C(0xa5);
  context->stateValue[0] = UINT64_C(0x3c);
  state.authorityPlane = &generatedPlane;
  state.authorityHandle = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(state.authorityHandle, UINT64_MAX);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.ordinaryAOTLoad, generatedPlane);
  EXPECT_EQ(state.nestedObserverLoad, generatedPlane);
  EXPECT_EQ(state.canonicalObserverLoad, UINT8_C(0x3c));
  EXPECT_EQ(context->observerDepth, 0u);
  EXPECT_FALSE(context->observerForcesCanonicalPlane);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     ObserverBookkeepingWritesUpdateCanonicalAndNativePlanes) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  context->observerForcesCanonicalPlane = true;
  uint64_t handle = obelisk_rt_v1_native_state_static_handle(1);
  uint8_t nativeValue = 0, nativeUnknown = 0, changed = 0;
  uint8_t value = 0xa5, unknown = 0x18;
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context, &nativeValue, 8, handle, 8, 0, &value, &changed),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context, &nativeUnknown, 8, handle, 8, 1, &unknown, &changed),
            OBELISK_RT_OK);
  EXPECT_EQ(nativeValue, value);
  EXPECT_EQ(nativeUnknown, unknown);
  EXPECT_EQ(context->stateValue[0], value);
  EXPECT_EQ(context->stateUnknown[0], unknown);
  uint8_t read = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, &nativeValue, 8,
                                                  handle, 8, 0, 0, &read),
            OBELISK_RT_OK);
  EXPECT_EQ(read, value);
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, &nativeUnknown, 8,
                                                  handle, 8, 1, 0, &read),
            OBELISK_RT_OK);
  EXPECT_EQ(read, unknown);
  context->observerForcesCanonicalPlane = false;
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, PackedCanonicalReadMergesOnlyOverriddenBits) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 256;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 53, 130),
            OBELISK_RT_OK);
  context->observerForcesCanonicalPlane = true;
  context->stateValue.assign(4, UINT64_C(0x96e1a5c387f0b42d));
  context->stateUnknown.assign(4, UINT64_C(0x1042088102044080));
  std::array<uint8_t, 32> global;
  global.fill(0x5a);
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
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
      for (uint32_t width = 1; width <= 64; ++width) {
        for (int64_t offset : {-3, 0, 1, 63, 129, 130}) {
          for (uint32_t fallback = 0; fallback != 2; ++fallback) {
            SCOPED_TRACE(testing::Message()
                         << masks << "," << plane << "," << width << ","
                         << offset << "," << fallback);
            std::array<uint8_t, 8> actual{}, expected{};
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
            ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                          context, global.data(), 256,
                          obelisk_rt_v1_native_handle_offset(root, offset),
                          width, plane, fallback, actual.data()),
                      OBELISK_RT_OK);
            EXPECT_EQ(actual, expected);
          }
        }
      }
    }
  }
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AutomaticStateAllocationsAreIsolatedAndBoundsChecked) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  uint8_t global = 0x5a;
  uint8_t firstInitial = 0x11;
  uint8_t secondInitial = 0x22;
  uint64_t first = UINT64_MAX;
  uint64_t second = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context, 8, &firstInitial, nullptr,
                                             &first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context, 8, &secondInitial,
                                             nullptr, &second),
            OBELISK_RT_OK);
  ASSERT_NE(first, second);
  uint8_t replacement = 0xfe;
  uint8_t changed = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context, &global, 8, first, 8, 0, &replacement, &changed),
            OBELISK_RT_OK);
  EXPECT_EQ(changed, 1u);
  uint8_t loaded = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, &global, 8, second,
                                                  8, 0, 0, &loaded),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded, secondInitial);
  loaded = 0;
  uint64_t tail = obelisk_rt_v1_native_handle_offset(first, 6);
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, &global, 8, tail, 4,
                                                  0, 1, &loaded),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded & 0xfu, 0xfu);
  EXPECT_EQ(global, 0x5a);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AutomaticEventBookkeepingFollowsObjectLifetime) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  uint8_t initial = 0;

  uint64_t immediateObject = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context, 8, &initial, nullptr,
                                             &immediateObject),
            OBELISK_RT_OK);
  uint64_t immediateEvent =
      obelisk_rt_v1_native_handle_offset(immediateObject, 3);
  ASSERT_NE(immediateEvent, UINT64_MAX);
  obelisk_rt_v1_scheduler_event(context, immediateEvent, 0);
  EXPECT_EQ(context->events.count(immediateEvent), 1u);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, immediateObject, 0),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->events.empty());

  uint64_t delayedObject = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context, 8, &initial, nullptr,
                                             &delayedObject),
            OBELISK_RT_OK);
  uint64_t delayedEvent = obelisk_rt_v1_native_handle_offset(delayedObject, 5);
  uint64_t secondDelayedEvent =
      obelisk_rt_v1_native_handle_offset(delayedObject, 6);
  ASSERT_NE(delayedEvent, UINT64_MAX);
  ASSERT_NE(secondDelayedEvent, UINT64_MAX);
  obelisk_rt_v1_scheduler_event_after(context, delayedEvent, 1, 7);
  obelisk_rt_v1_scheduler_event_after(context, secondDelayedEvent, 1, 9);
  ASSERT_EQ(context->scheduledDesignEvents.size(), 2u);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, delayedObject, 0),
            OBELISK_RT_OK);
  EXPECT_EQ(context->nativeAutomaticStates.size(), 1u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledDesignEvents.empty());
  EXPECT_TRUE(context->nativeAutomaticStates.empty());
  EXPECT_TRUE(context->events.empty());

  uint64_t waitedObject = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context, 8, &initial, nullptr,
                                             &waitedObject),
            OBELISK_RT_OK);
  uint64_t waitedEvent = obelisk_rt_v1_native_handle_offset(waitedObject, 7);
  ASSERT_NE(waitedEvent, UINT64_MAX);
  ASSERT_EQ(obelisk_rt_v1_native_state_retain(context, waitedObject),
            OBELISK_RT_OK);
  SchedulerFixture waiter(14);
  schedulerWaitKind = OBELISK_RT_SUSPEND_EVENT;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_NONE;
  schedulerWaitHandle = waitedEvent;
  schedulerWaitWidth = 0;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(waiter), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->events.empty());
  obelisk_rt_v1_scheduler_event_after(context, waitedEvent, 1, 1);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, waitedObject, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  EXPECT_EQ(context->events.count(waitedEvent), 1u);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, waitedObject, 0),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->nativeAutomaticStates.empty());
  EXPECT_TRUE(context->events.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AutomaticStateIsReleasedWithItsOwningProcess) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture fixture(8);
  fixture.descriptor.native_execute = automaticStateExecute;
  processAutomaticHandle = UINT64_MAX;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_NE(processAutomaticHandle, UINT64_MAX);
  uint8_t global = 0;
  uint8_t loaded = 0;
  EXPECT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &global, 8, processAutomaticHandle, 8, 0, 0, &loaded),
            OBELISK_RT_INVALID_HANDLE);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, DirectExecutionOwnsAndReleasesAutomaticState) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture fixture(9);
  fixture.descriptor.native_execute = automaticStateExecute;
  processAutomaticHandle = UINT64_MAX;
  obelisk_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  ASSERT_NE(processAutomaticHandle, UINT64_MAX);
  uint8_t global = 0;
  uint8_t loaded = 0;
  EXPECT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &global, 8, processAutomaticHandle, 8, 0, 0, &loaded),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, RetainedAutomaticStateSurvivesItsOwningProcess) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture fixture(10);
  fixture.descriptor.native_execute = retainedAutomaticStateExecute;
  retainedAutomaticHandle = UINT64_MAX;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_NE(retainedAutomaticHandle, UINT64_MAX);

  uint8_t global = 0;
  uint8_t loaded = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &global, 8, retainedAutomaticHandle, 8, 0, 0, &loaded),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded, 0xa5);

  ASSERT_EQ(
      obelisk_rt_v1_native_state_release(context, retainedAutomaticHandle, 0),
      OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &global, 8, retainedAutomaticHandle, 8, 0, 0, &loaded),
            OBELISK_RT_INVALID_HANDLE);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, DelayedNBARetainsItsAutomaticDestination) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture fixture(11);
  fixture.descriptor.native_execute = automaticNBAExecute;
  nbaAutomaticHandle = UINT64_MAX;
  nbaDummyPlane = 0;
  obelisk_rt_process_instance_v1 *instance = makeSchedulerInstance(fixture);
  ASSERT_NE(instance, nullptr);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  ASSERT_NE(nbaAutomaticHandle, UINT64_MAX);

  uint8_t loaded = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, &nbaDummyPlane, 8,
                                                  nbaAutomaticHandle, 8, 0, 0,
                                                  &loaded),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded, 0u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_native_state_load_plane(context, &nbaDummyPlane, 8,
                                                  nbaAutomaticHandle, 8, 0, 0,
                                                  &loaded),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, PartialOutOfBoundsHandlesPreserveInRangeBits) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 2, 4),
            OBELISK_RT_OK);
  uint64_t base = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(base, UINT64_MAX);
  uint64_t lower = obelisk_rt_v1_native_handle_offset(base, -1);
  uint64_t upper = obelisk_rt_v1_native_handle_offset(base, 3);
  ASSERT_NE(lower, UINT64_MAX);
  ASSERT_NE(upper, UINT64_MAX);
  uint8_t plane = 0xc3;
  uint8_t lowerReplacement = 0x2;
  uint8_t upperReplacement = 0x1;
  uint8_t changed = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context, &plane, 8, lower, 2, 0, &lowerReplacement, &changed),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context, &plane, 8, upper, 2, 0, &upperReplacement, &changed),
            OBELISK_RT_OK);
  EXPECT_EQ(plane, 0xe7);
  EXPECT_EQ(changed, 1u);
  uint8_t loaded = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, &plane, 8, lower, 2,
                                                  0, 0, &loaded),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded & 0x3, 0x2);

  plane = 0xc3;
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(context, &plane, nullptr, 8, lower, 2,
                                        0, &lowerReplacement, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(context, &plane, nullptr, 8, upper, 2,
                                        0, &upperReplacement, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(plane, 0xe7);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, OutOfBoundsTransitionsDoNotWakeAdjacentStaticObjects) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 4),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 2, 4, 4),
            OBELISK_RT_OK);
  SchedulerFixture fixture(9);
  schedulerWaitKind = OBELISK_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = obelisk_rt_v1_native_state_static_handle(2);
  schedulerWaitWidth = 4;
  schedulerResumeCount = 0;
  ASSERT_EQ(
      obelisk_rt_v1_scheduler_add(context, makeSchedulerInstance(fixture), 0),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  uint64_t upper = obelisk_rt_v1_native_handle_offset(
      obelisk_rt_v1_native_state_static_handle(1), 3);
  uint8_t oldValue = 0;
  uint8_t newValue = 2;
  obelisk_rt_v1_scheduler_signal_transition(context, upper, 2, &oldValue,
                                            nullptr, &newValue, nullptr);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);

  obelisk_rt_v1_scheduler_signal(context,
                                 obelisk_rt_v1_native_state_static_handle(2), 1,
                                 OBELISK_RT_SIGNAL_CHANGE);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  obelisk_rt_v1_context_destroy(context);
}

void initializeWait(void *frame) {
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(
      static_cast<uint8_t *>(frame) + 8);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_DELAY, 0, 0, 17, 0};
}

TEST(ProcessInstance, InitializesOutputRecordsOnFailure) {
  void *frame = reinterpret_cast<void *>(uintptr_t{1});
  uint64_t frameSize = 7;
  EXPECT_EQ(obelisk_rt_v1_process_instance_frame(nullptr, &frame, &frameSize),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(frame, nullptr);
  EXPECT_EQ(frameSize, 0u);

  obelisk_rt_fragment_action_v1 action{99, 99, 99, 99, 99, 99};
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                nullptr, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(action.suspend_kind, OBELISK_RT_SUSPEND_NONE);
  EXPECT_EQ(action.continuation, 0u);
  EXPECT_EQ(action.flags, 0u);
  EXPECT_EQ(action.payload, 0u);
  EXPECT_EQ(action.auxiliary, 0u);
}

TEST(ProcessInstance, DefersDestroyWhileObserverPinsActivation) {
  Fixture fixture;
  nativeDestroyCount = 0;
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  instance->native_handle = instance;
  instance->observer_pin_count = 1;

  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  EXPECT_EQ(instance->observer_destroy_pending, 1u);
  EXPECT_EQ(nativeDestroyCount, 0);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  EXPECT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  EXPECT_NE(frame, nullptr);

  instance->observer_pin_count = 0;
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 1);
}

TEST(ProcessInstance, NativeBytecodeNativeUsesStableCanonicalFrame) {
  Fixture fixture;
  nativeDestroyCount = 0;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;
  frameDuringExecute = OBELISK_RT_OK;
  destroyDuringExecute = OBELISK_RT_OK;
  observedContext = nullptr;
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  ASSERT_NE(instance, nullptr);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  EXPECT_EQ(frameSize, 40u);

  obelisk_rt_fragment_action_v1 action{};
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(frameDuringExecute, OBELISK_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(destroyDuringExecute, OBELISK_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(observedContext, context);
  EXPECT_EQ(instance->context, nullptr);
  EXPECT_EQ(instance->action, nullptr);
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(nativeDestroyCount, 1);
  void *sameFrame = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_frame(instance, &sameFrame, &frameSize),
      OBELISK_RT_OK);
  EXPECT_EQ(sameFrame, frame);
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(instance->native_handle, instance);
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 2);
  obelisk_rt_v1_context_destroy(context);
}

TEST(ProcessInstance, BytecodeFirstCanReconstructNativeAtContinuation) {
  Fixture fixture;
  nativeDestroyCount = 0;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  initializeWait(frame);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(action.flags, OBELISK_RT_ACTION_FRAME_WAIT_RECORD);
  EXPECT_EQ(action.auxiliary, sizeof(obelisk_rt_wait_record_v1));
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(instance->native_handle, instance);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 1);
}

TEST(ProcessInstance, RejectsLayoutsTiersContinuationsAndFrameRecords) {
  Fixture fixture;
  nativeDestroyCount = 0;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;
  obelisk_rt_process_instance_v1 *instance = nullptr;
  uint64_t savedChecksum = fixture.layout.checksum;
  fixture.layout.checksum ^= 1;
  EXPECT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_LAYOUT_MISMATCH);
  fixture.layout.checksum = savedChecksum;
  fixture.layout.frame_size = 41;
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_LAYOUT_MISMATCH);
  fixture.layout.frame_size = 48;
  fixture.fields[1].size = 33;
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_LAYOUT_MISMATCH);
  fixture.layout.frame_size = 40;
  fixture.fields[1].size = 40;
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_LAYOUT_MISMATCH);
  fixture.fields[1].size = sizeof(obelisk_rt_wait_record_v1);
  fixture.fields[0].flags = OBELISK_RT_FRAME_FOUR_STATE_UNKNOWN;
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_LAYOUT_MISMATCH);
  fixture.fields[0].flags = OBELISK_RT_FRAME_FIELD_FLAGS_NONE;
  fixture.layout.checksum = checksum(fixture.layout);
  uint8_t savedOpcode = fixture.code[0];
  fixture.code[0] = UINT8_MAX;
  EXPECT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_INVALID_BYTECODE);
  fixture.code[0] = savedOpcode;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  instance->continuation = 99;
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_INVALID_CONTINUATION);
  instance->continuation = 0;
  fixture.descriptor.available_tiers = OBELISK_RT_TIER_MASK_NATIVE;
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_TIER_UNAVAILABLE);
  fixture.descriptor.available_tiers =
      OBELISK_RT_TIER_MASK_NATIVE | OBELISK_RT_TIER_MASK_BYTECODE;
  emitInvalidNativeWait = true;
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_INVALID_FRAME);
  EXPECT_EQ(instance->lifecycle, OBELISK_RT_PROCESS_SUSPENDED);
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = true;
  instance->continuation = 1;
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 2);
}

TEST(ProcessInstance, NativeFailureDestroysHandleBeforeRetry) {
  Fixture fixture;
  nativeDestroyCount = 0;
  nativeExecuteStatus = OBELISK_RT_IO_ERROR;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;

  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_IO_ERROR);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(instance->continuation, 0u);
  EXPECT_EQ(instance->lifecycle, OBELISK_RT_PROCESS_SUSPENDED);
  EXPECT_EQ(nativeDestroyCount, 1);

  nativeExecuteStatus = OBELISK_RT_OK;
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(instance->native_handle, instance);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 2);
}

TEST(ProcessInstance, MissingBytecodeContinuationPreservesNativeHandle) {
  Fixture fixture;
  nativeDestroyCount = 0;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;

  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(instance->continuation, 1u);
  void *nativeHandle = instance->native_handle;

  fixture.entries[1].continuation = 2;
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_TIER_UNAVAILABLE);
  EXPECT_EQ(instance->native_handle, nativeHandle);
  EXPECT_EQ(instance->tier, OBELISK_RT_TIER_NATIVE);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(nativeDestroyCount, 0);

  fixture.entries[1].continuation = 1;
  uint8_t savedOpcode = fixture.code[0];
  fixture.code[0] = UINT8_MAX;
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_INVALID_BYTECODE);
  EXPECT_EQ(instance->native_handle, nativeHandle);
  EXPECT_EQ(instance->tier, OBELISK_RT_TIER_NATIVE);
  EXPECT_EQ(nativeDestroyCount, 0);
  fixture.code[0] = savedOpcode;

  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 1);
}

TEST(ProcessInstance,
     NativeBytecodeNativeRoundTripPreservesCanonicalContinuationFrame) {
  Fixture fixture;
  nativeDestroyCount = 0;
  emitInvalidNativeWait = false;
  emitInvalidNativeTerminate = false;
  emitExistingNativeWait = false;

  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  *static_cast<uint64_t *>(instance->frame) = UINT64_C(0x123456789abcdef0);
  obelisk_rt_fragment_action_v1 action{};

  // A generated/native activation suspends at continuation 1. Tier 3 resumes
  // that exact continuation in the shared canonical frame, then the supported
  // return continuation routes directly back to native execution.
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_SUSPEND);
  ASSERT_EQ(instance->continuation, 1u);
  void *nativeHandle = instance->native_handle;
  ASSERT_NE(nativeHandle, nullptr);

  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(instance->tier, OBELISK_RT_TIER_BYTECODE);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(*static_cast<uint64_t *>(instance->frame),
            UINT64_C(0x123456789abcdef0));

  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(instance->tier, OBELISK_RT_TIER_NATIVE);
  EXPECT_NE(instance->native_handle, nullptr);
  EXPECT_EQ(*static_cast<uint64_t *>(instance->frame),
            UINT64_C(0x123456789abcdef0));

  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 2);
}

TEST(ProcessInstance, RejectsOverlappingCanonicalFrameFields) {
  Fixture fixture;
  obelisk_rt_process_instance_v1 *instance = nullptr;

  fixture.fields[1] = {
      OBELISK_RT_FRAME_CAPTURE, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 4, 8, 4, 0};
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_LAYOUT_MISMATCH);

  fixture.fields[1] = {
      OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 0, 32, 8, 0};
  fixture.layout.checksum = checksum(fixture.layout);
  EXPECT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_LAYOUT_MISMATCH);
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
  fixture.descriptor.available_tiers = OBELISK_RT_TIER_MASK_NATIVE;
  fixture.descriptor.bytecode = nullptr;

  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(
      static_cast<uint8_t *>(instance->frame) + 8);
  auto *entries = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  obelisk_rt_fragment_action_v1 action{};
  auto expectInvalid = [&](obelisk_rt_suspend_kind kind, uint32_t flags,
                           uint32_t count, obelisk_rt_wait_edge_kind firstEdge,
                           uint32_t firstReserved = 0) {
    std::memset(wait, 0, 64);
    *wait = {OBELISK_RT_VERSION, kind, flags, count, 0, 0};
    entries[0] = {17, firstEdge, firstReserved};
    entries[1] = {18, OBELISK_RT_WAIT_EDGE_POSEDGE, 0};
    EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                  instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
              OBELISK_RT_INVALID_FRAME);
  };

  expectInvalid(OBELISK_RT_SUSPEND_DELAY, 1, 0, OBELISK_RT_WAIT_EDGE_NONE);
  expectInvalid(OBELISK_RT_SUSPEND_CHANGE, 0, 1, OBELISK_RT_WAIT_EDGE_POSEDGE);
  expectInvalid(OBELISK_RT_SUSPEND_EDGE, 0, 1, 4);
  expectInvalid(OBELISK_RT_SUSPEND_EDGE, 0, 2, OBELISK_RT_WAIT_EDGE_CHANGE, 1);
  expectInvalid(OBELISK_RT_SUSPEND_EVENT, 0, 1, OBELISK_RT_WAIT_EDGE_CHANGE);
  expectInvalid(OBELISK_RT_SUSPEND_AWAIT, 0, 0, OBELISK_RT_WAIT_EDGE_NONE);
  expectInvalid(OBELISK_RT_SUSPEND_JOIN, 2, 1, OBELISK_RT_WAIT_EDGE_NONE);
  expectInvalid(OBELISK_RT_SUSPEND_FRONTIER, 0, 1, OBELISK_RT_WAIT_EDGE_CHANGE);

  auto expectInvalidOccurrence =
      [&](uint32_t count, uint64_t payload, uint64_t conditionMask,
          obelisk_rt_wait_edge_kind conditionEdge, uint32_t conditionWidth) {
        std::memset(wait, 0, 64);
        *wait = {OBELISK_RT_VERSION,
                 OBELISK_RT_SUSPEND_EDGE,
                 OBELISK_RT_WAIT_CLOCK_OCCURRENCE,
                 count,
                 payload,
                 conditionMask};
        entries[0] = {17, OBELISK_RT_WAIT_EDGE_POSEDGE, 1};
        entries[1] = {18, conditionEdge, conditionWidth};
        EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                      instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
                  OBELISK_RT_INVALID_FRAME);
      };
  expectInvalidOccurrence(1, 0, 0, OBELISK_RT_WAIT_EDGE_NONE, 1);
  expectInvalidOccurrence(0, 1, 0, OBELISK_RT_WAIT_EDGE_NONE, 1);
  expectInvalidOccurrence(2, 1, 2, OBELISK_RT_WAIT_EDGE_NONE, 1);
  expectInvalidOccurrence(2, 1, 1, OBELISK_RT_WAIT_EDGE_POSEDGE, 1);
  expectInvalidOccurrence(2, 1, 1, OBELISK_RT_WAIT_EDGE_NONE, 0);

  auto expectInvalidOccurrenceEncoding = [&](uint32_t primaryEdge,
                                             uint32_t conditionEdge) {
    std::memset(wait, 0, 64);
    *wait = {OBELISK_RT_VERSION,
             OBELISK_RT_SUSPEND_EDGE,
             OBELISK_RT_WAIT_CLOCK_OCCURRENCE,
             2,
             1,
             1};
    entries[0] = {17, primaryEdge, 1};
    entries[1] = {18, conditionEdge, 1};
    EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                  instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
              OBELISK_RT_INVALID_FRAME);
  };
  expectInvalidOccurrenceEncoding(OBELISK_RT_WAIT_EDGE_TRANSITION_MASK,
                                  OBELISK_RT_WAIT_EDGE_NONE);
  expectInvalidOccurrenceEncoding(OBELISK_RT_WAIT_EDGE_TRANSITION_MASK |
                                      UINT32_C(0x41),
                                  OBELISK_RT_WAIT_EDGE_NONE);
  expectInvalidOccurrenceEncoding(OBELISK_RT_WAIT_EDGE_TRANSITION_MASK | 1,
                                  OBELISK_RT_WAIT_CONDITION_PREDICATE - 1);
  expectInvalidOccurrenceEncoding(OBELISK_RT_WAIT_EDGE_TRANSITION_MASK | 1,
                                  OBELISK_RT_WAIT_CONDITION_CASE_NE_ONE + 1);

  std::memset(wait, 0, 64);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  entries[0] = {OBELISK_RT_STABLE_HANDLE_AUTOMATIC_TAG,
                OBELISK_RT_WAIT_EDGE_CHANGE, 1};
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_INVALID_FRAME);

  std::memset(wait, 0, 64);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  entries[0] = {17, OBELISK_RT_WAIT_EDGE_CHANGE, 0};
  emitInvalidResumeRegion = true;
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_INVALID_FRAME);
  emitInvalidResumeRegion = false;

  std::memset(wait, 0, 64);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 2, 0, 0};
  entries[0] = {UINT64_MAX, OBELISK_RT_WAIT_EDGE_CHANGE, 1};
  entries[1] = {18, OBELISK_RT_WAIT_EDGE_POSEDGE, 1};
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);

  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 20);
}

TEST(SampledValues, CapturesCanonicalPreponedPlane) {
  obelisk_rt_sampled_range_v1 sampledRange{3, 0, 10};
  struct {
    obelisk_rt_execution_descriptor_v1 execution{};
    obelisk_rt_execution_extension_v1 extension{};
  } storage;
  storage.extension = {OBELISK_RT_EXECUTION_EXTENSION_VERSION,
                       sizeof(obelisk_rt_execution_extension_v1), &sampledRange,
                       1};
  auto &execution = storage.execution;
  execution.version = OBELISK_RT_VERSION;
  execution.flags = OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.state_bit_count = 4096;
  execution.reserved = offsetof(decltype(storage), extension);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->stateValue[0] = UINT64_C(0x35a7);
  context->stateUnknown[0] = UINT64_C(0x0104);
  ASSERT_EQ(obelisk_rt_capture_preponed_unlocked(context), OBELISK_RT_OK);
  EXPECT_EQ(context->preponedValue.size(), 1u);
  EXPECT_EQ(context->preponedUnknown.size(), 1u);

  // A later Active-region update must not affect the sampled result.
  context->stateValue[0] = 0;
  context->stateUnknown[0] = 0;
  uint64_t handle =
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_GLOBAL, 0, 3);
  uint8_t value[2] = {};
  uint8_t unknown[2] = {};
  EXPECT_EQ(obelisk_rt_v1_sampled_read(context, handle, 10, value, unknown),
            OBELISK_RT_OK);
  EXPECT_EQ(value[0], UINT8_C(0xb4));
  EXPECT_EQ(value[1], UINT8_C(0x02));
  EXPECT_EQ(unknown[0], UINT8_C(0x20));
  EXPECT_EQ(unknown[1], UINT8_C(0x00));
  obelisk_rt_v1_context_destroy(context);
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

obelisk_rt_status
covergroupClockEventPrimaryEvaluator(obelisk_rt_context *context,
                                     const uint64_t *captures,
                                     uint32_t captureCount, uint64_t *value,
                                     uint64_t *unknown, uint32_t limbCount) {
  if (!context || !captures || captureCount != 1 || !value || !unknown ||
      limbCount != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  bool bitValue = false;
  bool bitUnknown = false;
  if (!obelisk_rt_read_clock_condition_publication_bit_unlocked(
          context, captures[0], 0, bitValue, bitUnknown) &&
      !obelisk_rt_read_signal_bit_unlocked(context, captures[0], 0, bitValue,
                                           bitUnknown,
                                           /*useSnapshot=*/false))
    return OBELISK_RT_INVALID_HANDLE;
  value[0] = bitValue;
  unknown[0] = bitUnknown;
  return OBELISK_RT_OK;
}

obelisk_rt_status covergroupClockEventOrEvaluator(obelisk_rt_context *context,
                                                  const uint64_t *captures,
                                                  uint32_t captureCount,
                                                  uint64_t *value,
                                                  uint64_t *unknown,
                                                  uint32_t limbCount) {
  if (!context || !captures || captureCount != 1 || !value || !unknown ||
      limbCount != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto read = [&](uint64_t stableID, bool &bitValue, bool &bitUnknown) {
    return obelisk_rt_read_clock_condition_publication_bit_unlocked(
               context, stableID, 0, bitValue, bitUnknown) ||
           obelisk_rt_read_signal_bit_unlocked(context, stableID, 0, bitValue,
                                               bitUnknown,
                                               /*useSnapshot=*/false);
  };
  bool lhs = false;
  bool lhsUnknown = false;
  bool rhs = false;
  bool rhsUnknown = false;
  if (!read(0, lhs, lhsUnknown) || !read(1, rhs, rhsUnknown))
    return OBELISK_RT_INVALID_HANDLE;
  value[0] = lhs || rhs;
  unknown[0] = !value[0] && (lhsUnknown || rhsUnknown);
  return OBELISK_RT_OK;
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
  uint64_t observersOffset = sizeof(obelisk_rt_computed_wait_record_v1);
  uint64_t capturesOffset =
      observersOffset +
      uint64_t{observerCount} * sizeof(obelisk_rt_computed_observer_v1);
  uint64_t dependenciesOffset =
      capturesOffset +
      uint64_t{captureCount} * sizeof(obelisk_rt_computed_capture_v1);
  uint64_t clausesOffset =
      dependenciesOffset +
      uint64_t{dependencyCount} * sizeof(obelisk_rt_computed_dependency_v1);
  uint64_t previousOffset =
      clausesOffset +
      uint64_t{clauseCount} * sizeof(obelisk_rt_computed_clause_v1);
  uint64_t totalSize = previousOffset + uint64_t{clauseCount} * 16;
  EXPECT_EQ(totalSize % sizeof(uint64_t), 0u);
  std::vector<uint64_t> bytes(totalSize / sizeof(uint64_t), 0);
  auto write = [&](uint64_t offset, const auto &record) {
    auto *storage = reinterpret_cast<uint8_t *>(bytes.data());
    std::memcpy(storage + offset, &record, sizeof(record));
  };
  obelisk_rt_computed_wait_record_v1 header{
      OBELISK_RT_VERSION,
      OBELISK_RT_SUSPEND_OBSERVER,
      OBELISK_RT_COMPUTED_WAIT_INTERLEAVED,
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
    obelisk_rt_computed_observer_v1 observer{
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
              uint64_t{index} * sizeof(obelisk_rt_computed_observer_v1),
          observer);
    obelisk_rt_computed_capture_v1 capture{handles[index], 0, 0, 0};
    write(capturesOffset +
              uint64_t{index} * sizeof(obelisk_rt_computed_capture_v1),
          capture);
    obelisk_rt_computed_dependency_v1 dependency{
        handles[index], OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
    write(dependenciesOffset +
              uint64_t{index} * sizeof(obelisk_rt_computed_dependency_v1),
          dependency);
  }
  uint32_t conditionIndex = 0;
  for (uint32_t index = 0; index != clauseCount; ++index) {
    obelisk_rt_computed_clause_v1 clause{
        index,
        conditions[index] == UINT64_MAX ? OBELISK_RT_OBSERVER_CONDITION_NONE
                                        : clauseCount + conditionIndex++,
        edges[index], 0};
    write(clausesOffset +
              uint64_t{index} * sizeof(obelisk_rt_computed_clause_v1),
          clause);
  }
  return bytes;
}

obelisk_rt_status covergroupClockEventEvaluator(obelisk_rt_context *context,
                                                const uint64_t *captures,
                                                uint32_t captureCount,
                                                uint64_t *value,
                                                uint64_t *unknown,
                                                uint32_t limbCount) {
  if (!context || !captures || captureCount != 1 || captures[0] != 42 ||
      !value || !unknown || limbCount != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint8_t sampled = 0;
  obelisk_rt_status status = obelisk_rt_v1_native_state_load_plane(
      context, &covergroupClockEventGlobalPlane, 4, 0, 1, 0, 0, &sampled);
  if (status != OBELISK_RT_OK)
    return status;
  covergroupClockEventRegion = context->activeExecRegion;
  covergroupClockEventSamples.push_back(sampled & 1);
  if (covergroupClockEventSamplerReentrantKind != 0) {
    uint32_t kind = covergroupClockEventSamplerReentrantKind;
    covergroupClockEventSamplerReentrantKind = 0;
    if (kind == 1) {
      const uint8_t oldValue = 0;
      const uint8_t newValue = 1;
      obelisk_rt_v1_scheduler_signal_transition(context, 1, 1, &oldValue,
                                                nullptr, &newValue, nullptr);
      context->stateValue[0] |= uint64_t{1} << 2;
      context->stateUnknown[0] &= ~(uint64_t{1} << 2);
    }
  }
  if (covergroupClockEventReentrantTransition) {
    covergroupClockEventReentrantTransition = false;
    const uint8_t oldValue = 0;
    const uint8_t newValue = 1;
    obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &oldValue, nullptr,
                                              &newValue, nullptr);
  }
  if (covergroupClockEventCompletionTarget != 0) {
    uint64_t target = covergroupClockEventCompletionTarget;
    covergroupClockEventCompletionTarget = 0;
    obelisk_rt_program_complete_unlocked(context, target, 0);
  }
  value[0] = covergroupClockEventSentinel & 1;
  unknown[0] = (covergroupClockEventSentinel >> 1) & 1;
  return OBELISK_RT_OK;
}

obelisk_rt_status preponedObserverEvaluator(obelisk_rt_context *context,
                                            const uint64_t *,
                                            uint32_t captureCount,
                                            uint64_t *value, uint64_t *unknown,
                                            uint32_t limbCount) {
  if (!context || captureCount != 0 || !value || !unknown || limbCount != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint8_t sampledValue = 0;
  uint8_t sampledUnknown = 0;
  uint64_t handle =
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_GLOBAL, 0, 0);
  obelisk_rt_status status = obelisk_rt_v1_sampled_read(
      context, handle, 1, &sampledValue, &sampledUnknown);
  if (status != OBELISK_RT_OK)
    return status;
  preponedObserverSamples.push_back(
      static_cast<uint32_t>(sampledValue & 1) |
      (static_cast<uint32_t>(sampledUnknown & 1) << 1));
  value[0] = sampledValue & 1;
  unknown[0] = sampledUnknown & 1;
  return OBELISK_RT_OK;
}

TEST(RuntimeInternals, ComputedWaitAcceptsDynamicEventCapture) {
  constexpr uint64_t observerID = 98;
  const obelisk_rt_observer_capture_abi_v1 captureABI{
      OBELISK_RT_OBSERVER_CAPTURE_EVENT, 1};
  obelisk_rt_observer_descriptor_v1 observer{
      observerID, &captureABI,
      1,          1,
      0,          OBELISK_RT_OBSERVER_NO_BYTECODE,
      nullptr,    0};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.observers = &observer;
  execution.observer_count = 1;

  struct ObserverWait {
    obelisk_rt_computed_wait_record_v1 wait{};
    obelisk_rt_computed_observer_v1 observer{};
    obelisk_rt_computed_capture_v1 capture{};
    obelisk_rt_computed_clause_v1 clause{};
    uint64_t previousValue = 0;
    uint64_t previousUnknown = 0;
  } record;
  record.wait = {OBELISK_RT_VERSION,
                 OBELISK_RT_SUSPEND_OBSERVER,
                 OBELISK_RT_COMPUTED_WAIT_INTERLEAVED,
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
      OBELISK_RT_STABLE_HANDLE_DYNAMIC_EVENT_TAG | UINT64_C(17);
  record.clause = {0, OBELISK_RT_OBSERVER_CONDITION_NONE,
                   OBELISK_RT_WAIT_EDGE_CHANGE,
                   OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY};
  EXPECT_TRUE(obelisk_rt_validate_computed_wait_record(&execution, &record.wait,
                                                       sizeof(record)));

  // The all-ones null sentinel is not a schedulable event capture.
  record.capture.stable_id = UINT64_MAX;
  EXPECT_FALSE(obelisk_rt_validate_computed_wait_record(
      &execution, &record.wait, sizeof(record)));
}

TEST(RuntimeInternals,
     CovergroupClockEventsSampleEachAtomicPublicationAndShareOneCohort) {
  constexpr uint64_t observerID = 981;
  const std::array<obelisk_rt_observer_capture_abi_v1, 2> misplacedABI{{
      {OBELISK_RT_OBSERVER_CAPTURE_STORAGE, 1},
      {OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP, 64},
  }};
  const obelisk_rt_observer_capture_abi_v1 captureABI{
      OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const obelisk_rt_observer_capture_abi_v1 primaryABI{
      OBELISK_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const std::array<obelisk_rt_observer_descriptor_v1, 3> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       OBELISK_RT_OBSERVER_FOUR_STATE, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID - 1, misplacedABI.data(), 2, 1, 0,
       OBELISK_RT_OBSERVER_NO_BYTECODE, covergroupClockEventEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 4;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});

  obelisk_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  obelisk_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.context = context;
  instance.tier = OBELISK_RT_TIER_NATIVE;
  context->scheduledProcesses.emplace_back();
  ScheduledProcess &scheduled = context->scheduledProcesses.back();
  scheduled.instance = &instance;
  scheduled.token = 1;
  scheduled.started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  context->activeNativeProcess = &instance;
  context->activeLogicalProcessToken =
      OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1};

  const std::array<uint64_t, 2> primaries{{0, 1}};
  // The first clause is gated by bit 2; the second has no iff condition.
  const std::array<uint64_t, 2> conditions{{2, UINT64_MAX}};
  const std::array<uint32_t, 2> edges{
      {OBELISK_RT_WAIT_EDGE_BOTH, OBELISK_RT_WAIT_EDGE_BOTH}};
  std::vector<uint64_t> eventPlan = makeCovergroupClockEventPlan(
      std::vector<uint64_t>(primaries.begin(), primaries.end()),
      std::vector<uint64_t>(conditions.begin(), conditions.end()),
      std::vector<uint32_t>(edges.begin(), edges.end()));
  auto *eventRecord =
      reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(eventPlan.data());
  auto *eventObservers = reinterpret_cast<obelisk_rt_computed_observer_v1 *>(
      reinterpret_cast<uint8_t *>(eventPlan.data()) +
      eventRecord->observers_offset);
  // Both primaries depend on two bits in the same indexed page. Registration
  // must keep only one forward bucket entry per clause, and teardown must not
  // leave a same-clause swap entry dangling.
  eventObservers[0].dependency_count = 2;
  eventObservers[1].dependency_begin = 0;
  eventObservers[1].dependency_count = 2;
  const obelisk_rt_computed_capture_v1 capture{42, 0, 0, 0};
  const std::array<obelisk_rt_computed_capture_v1, 2> misplacedCaptures{{
      {0, 0, 0, 0},
      capture,
  }};
  EXPECT_EQ(obelisk_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                observerID - 1, misplacedCaptures.data(),
                misplacedCaptures.size()),
            OBELISK_RT_INVALID_DESIGN);
  ASSERT_EQ(obelisk_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                observerID, &capture, 1),
            OBELISK_RT_OK);
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
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 2, &bothOld, nullptr,
                                            &bothNew, nullptr);
  // Both clauses matched one atomic publication, and the evaluator's direct
  // load observed that publication's new bit before canonical commit.
  ASSERT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));

  const uint8_t oneOld = 1;
  const uint8_t oneNew = 0;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &oneOld, nullptr,
                                            &oneNew, nullptr);
  obelisk_rt_v1_scheduler_signal_transition(context, 1, 1, &oneOld, nullptr,
                                            &oneNew, nullptr);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1, 0}));

  context->stateValue[0] |= uint64_t{1} << 2;
  context->stateUnknown[0] &= ~(uint64_t{1} << 2);
  bool conditionValue = false;
  bool conditionUnknown = false;
  ASSERT_TRUE(obelisk_rt_read_signal_bit_unlocked(context, 2, 0, conditionValue,
                                                  conditionUnknown));
  ASSERT_TRUE(conditionValue);
  ASSERT_FALSE(conditionUnknown);
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &oneNew, nullptr,
                                            &oneOld, nullptr);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1, 0, 1}));

  context->coverage->instances[42].enabled = false;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &oneOld, nullptr,
                                            &oneNew, nullptr);
  EXPECT_EQ(covergroupClockEventSamples.size(), 3u);

  obelisk_rt_program_complete_unlocked(
      context, OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1}, 0);
  EXPECT_EQ(context->covergroupClockEvents, nullptr);
  context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
  EXPECT_EQ(obelisk_rt_v1_scheduler_handoff_pending(context), 1u);
  context->schedulerStatus = OBELISK_RT_OK;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 2, &bothOld, nullptr,
                                            &bothNew, nullptr);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OK);
  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     CovergroupClockEventUpdatesEveryOrClauseBeforeCoalescingSample) {
  constexpr uint64_t observerID = 984;
  const obelisk_rt_observer_capture_abi_v1 primaryABI{
      OBELISK_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const obelisk_rt_observer_capture_abi_v1 captureABI{
      OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const std::array<obelisk_rt_observer_descriptor_v1, 3> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       OBELISK_RT_OBSERVER_FOUR_STATE, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {covergroupClockEventOrObserverID, &primaryABI, 1, 1,
       OBELISK_RT_OBSERVER_FOUR_STATE, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventOrEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 2;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});

  obelisk_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  obelisk_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.context = context;
  instance.tier = OBELISK_RT_TIER_NATIVE;
  context->scheduledProcesses.emplace_back();
  context->scheduledProcesses.back().instance = &instance;
  context->scheduledProcesses.back().token = 1;
  context->scheduledProcesses.back().started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  context->activeNativeProcess = &instance;
  context->activeLogicalProcessToken =
      OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1};

  std::vector<uint64_t> eventPlan = makeCovergroupClockEventPlan(
      {0, 0}, {UINT64_MAX, UINT64_MAX},
      {OBELISK_RT_WAIT_EDGE_POSEDGE, OBELISK_RT_WAIT_EDGE_CHANGE});
  auto *eventRecord =
      reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(eventPlan.data());
  auto *eventObservers = reinterpret_cast<obelisk_rt_computed_observer_v1 *>(
      reinterpret_cast<uint8_t *>(eventPlan.data()) +
      eventRecord->observers_offset);
  auto *eventDependencies =
      reinterpret_cast<obelisk_rt_computed_dependency_v1 *>(
          reinterpret_cast<uint8_t *>(eventPlan.data()) +
          eventRecord->dependencies_offset);
  eventObservers[1].code_unit_id = covergroupClockEventOrObserverID;
  eventObservers[1].dependency_begin = 0;
  eventObservers[1].dependency_count = 2;
  eventDependencies[1].stable_id = 1;
  const obelisk_rt_computed_capture_v1 capture{42, 0, 0, 0};
  ASSERT_EQ(obelisk_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                observerID, &capture, 1),
            OBELISK_RT_OK);
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;

  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 1;
  covergroupClockEventSentinel = 1;
  const uint8_t zero = 0;
  const uint8_t one = 1;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  ASSERT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));

  // The first publication also changes (a | b), even though posedge a wins
  // sample coalescing. Updating b from zero to one leaves (a | b) stable and
  // therefore must not synthesize a second event from stale clause history.
  obelisk_rt_v1_scheduler_signal_transition(context, 1, 1, &zero, nullptr, &one,
                                            nullptr);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));

  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     CovergroupClockEventEvaluatesReentrantIffAtEventInstant) {
  constexpr uint64_t observerID = 985;
  const obelisk_rt_observer_capture_abi_v1 primaryABI{
      OBELISK_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const obelisk_rt_observer_capture_abi_v1 captureABI{
      OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const std::array<obelisk_rt_observer_descriptor_v1, 2> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       OBELISK_RT_OBSERVER_FOUR_STATE, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 3;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});

  obelisk_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  std::array<obelisk_rt_process_instance_v1, 2> instances{};
  for (uint64_t index = 0; index != instances.size(); ++index) {
    instances[index].descriptor = &descriptor;
    instances[index].context = context;
    instances[index].tier = OBELISK_RT_TIER_NATIVE;
    context->scheduledProcesses.emplace_back();
    context->scheduledProcesses.back().instance = &instances[index];
    context->scheduledProcesses.back().token = index + 1;
    context->scheduledProcesses.back().started = true;
    context->scheduledProcessIndices.emplace(index + 1, index);
  }
  const obelisk_rt_computed_capture_v1 capture{42, 0, 0, 0};
  auto registerEvent = [&](uint64_t token, std::vector<uint64_t> &eventPlan) {
    context->activeNativeProcess = &instances[token - 1];
    context->activeLogicalProcessToken =
        OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | token;
    auto *record = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
        eventPlan.data());
    return obelisk_rt_v1_covergroup_clock_event_register(
        context, record, eventPlan.size() * sizeof(uint64_t), 0, observerID,
        &capture, 1);
  };
  std::vector<uint64_t> outerPlan = makeCovergroupClockEventPlan(
      {0}, {UINT64_MAX}, {OBELISK_RT_WAIT_EDGE_POSEDGE});
  std::vector<uint64_t> nestedPlan =
      makeCovergroupClockEventPlan({1}, {2}, {OBELISK_RT_WAIT_EDGE_POSEDGE});
  ASSERT_EQ(registerEvent(1, outerPlan), OBELISK_RT_OK);
  ASSERT_EQ(registerEvent(2, nestedPlan), OBELISK_RT_OK);
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;

  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 1;
  covergroupClockEventSentinel = 1;
  covergroupClockEventSamplerReentrantKind = 1;
  const uint8_t zero = 0;
  const uint8_t one = 1;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OK);
  // The outer sampler publishes signal 1 while gate bit 2 is zero, then sets
  // the gate before returning. The nested iff is evaluated synchronously at
  // publication and must not observe that later gate write.
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));
  EXPECT_NE(context->stateValue[0] & (uint64_t{1} << 2), 0u);

  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, CovergroupClockEventComposesNestedPublicationSnapshots) {
  constexpr uint64_t observerID = 986;
  const obelisk_rt_observer_capture_abi_v1 primaryABI{
      OBELISK_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const obelisk_rt_observer_capture_abi_v1 captureABI{
      OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const std::array<obelisk_rt_observer_descriptor_v1, 2> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       OBELISK_RT_OBSERVER_FOUR_STATE, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 2;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});

  obelisk_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  std::array<obelisk_rt_process_instance_v1, 2> instances{};
  for (uint64_t index = 0; index != instances.size(); ++index) {
    instances[index].descriptor = &descriptor;
    instances[index].context = context;
    instances[index].tier = OBELISK_RT_TIER_NATIVE;
    context->scheduledProcesses.emplace_back();
    context->scheduledProcesses.back().instance = &instances[index];
    context->scheduledProcesses.back().token = index + 1;
    context->scheduledProcesses.back().started = true;
    context->scheduledProcessIndices.emplace(index + 1, index);
  }
  const obelisk_rt_computed_capture_v1 capture{42, 0, 0, 0};
  auto registerEvent = [&](uint64_t token, std::vector<uint64_t> &eventPlan) {
    context->activeNativeProcess = &instances[token - 1];
    context->activeLogicalProcessToken =
        OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | token;
    auto *record = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
        eventPlan.data());
    return obelisk_rt_v1_covergroup_clock_event_register(
        context, record, eventPlan.size() * sizeof(uint64_t), 0, observerID,
        &capture, 1);
  };
  std::vector<uint64_t> outerPlan = makeCovergroupClockEventPlan(
      {0}, {UINT64_MAX}, {OBELISK_RT_WAIT_EDGE_POSEDGE});
  std::vector<uint64_t> nestedPlan = makeCovergroupClockEventPlan(
      {1}, {UINT64_MAX}, {OBELISK_RT_WAIT_EDGE_POSEDGE});
  ASSERT_EQ(registerEvent(1, outerPlan), OBELISK_RT_OK);
  ASSERT_EQ(registerEvent(2, nestedPlan), OBELISK_RT_OK);
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;

  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 1;
  covergroupClockEventSentinel = 1;
  covergroupClockEventSamplerReentrantKind = 1;
  const uint8_t zero = 0;
  const uint8_t one = 1;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OK);
  // The outer A publication has not committed canonical storage when its
  // sampler publishes B. B's synchronous sampler must nevertheless retain
  // the enclosing A snapshot and observe A's post-transition value.
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1, 1}));

  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, CovergroupClockEventRejectsFalseSamplerSentinel) {
  constexpr uint64_t observerID = 982;
  const obelisk_rt_observer_capture_abi_v1 captureABI{
      OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const obelisk_rt_observer_capture_abi_v1 primaryABI{
      OBELISK_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const std::array<obelisk_rt_observer_descriptor_v1, 2> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       OBELISK_RT_OBSERVER_FOUR_STATE, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 4;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});
  obelisk_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  obelisk_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.context = context;
  instance.tier = OBELISK_RT_TIER_NATIVE;
  context->scheduledProcesses.emplace_back();
  context->scheduledProcesses.back().instance = &instance;
  context->scheduledProcesses.back().token = 1;
  context->scheduledProcesses.back().started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  context->activeNativeProcess = &instance;
  context->activeLogicalProcessToken =
      OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1};
  const uint64_t primary = 0;
  const uint64_t condition = UINT64_MAX;
  const uint32_t edge = OBELISK_RT_WAIT_EDGE_CHANGE;
  std::vector<uint64_t> eventPlan =
      makeCovergroupClockEventPlan({primary}, {condition}, {edge});
  auto *eventRecord =
      reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(eventPlan.data());
  const obelisk_rt_computed_capture_v1 capture{42, 0, 0, 0};
  ASSERT_EQ(obelisk_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                observerID, &capture, 1),
            OBELISK_RT_OK);
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;
  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 0;
  covergroupClockEventSentinel = 0;
  const uint8_t oldValue = 0;
  const uint8_t newValue = 1;
  context->covergroupClockEventEvaluationDepth = 1;
  obelisk_rt_v1_scheduler_signal_transition(context, 3, 1, &oldValue, nullptr,
                                            &newValue, nullptr);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_INVALID_DESIGN);
  context->covergroupClockEventEvaluationDepth = 0;
  context->schedulerStatus = OBELISK_RT_OK;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &oldValue, nullptr,
                                            &newValue, nullptr);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));
  covergroupClockEventSentinel = 1;
  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals,
     CovergroupClockEventReentrantCompletionInvalidatesNoCandidates) {
  constexpr uint64_t observerID = 983;
  const obelisk_rt_observer_capture_abi_v1 captureABI{
      OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const obelisk_rt_observer_capture_abi_v1 primaryABI{
      OBELISK_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const std::array<obelisk_rt_observer_descriptor_v1, 2> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       OBELISK_RT_OBSERVER_FOUR_STATE, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 1;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});

  obelisk_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  std::array<obelisk_rt_process_instance_v1, 2> instances{};
  for (uint64_t index = 0; index != instances.size(); ++index) {
    instances[index].descriptor = &descriptor;
    instances[index].context = context;
    instances[index].tier = OBELISK_RT_TIER_NATIVE;
    context->scheduledProcesses.emplace_back();
    ScheduledProcess &scheduled = context->scheduledProcesses.back();
    scheduled.instance = &instances[index];
    scheduled.token = index + 1;
    scheduled.started = true;
    context->scheduledProcessIndices.emplace(index + 1, index);
  }

  const uint64_t primary = 0;
  const uint64_t condition = UINT64_MAX;
  const uint32_t edge = OBELISK_RT_WAIT_EDGE_CHANGE;
  std::vector<uint64_t> eventPlan =
      makeCovergroupClockEventPlan({primary}, {condition}, {edge});
  auto *eventRecord =
      reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(eventPlan.data());
  const obelisk_rt_computed_capture_v1 capture{42, 0, 0, 0};
  for (uint64_t index = 0; index != instances.size(); ++index) {
    context->activeNativeProcess = &instances[index];
    context->activeLogicalProcessToken =
        OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | (index + 1);
    ASSERT_EQ(obelisk_rt_v1_covergroup_clock_event_register(
                  context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                  observerID, &capture, 1),
              OBELISK_RT_OK);
  }
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;

  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 0;
  covergroupClockEventSentinel = 1;
  covergroupClockEventCompletionTarget =
      OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{2};
  const uint8_t oldValue = 0;
  const uint8_t newValue = 1;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &oldValue, nullptr,
                                            &newValue, nullptr);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));
  ASSERT_NE(context->covergroupClockEvents, nullptr);
  EXPECT_EQ(context->covergroupClockEvents->registrations.size(), 1u);

  obelisk_rt_program_complete_unlocked(
      context, OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1}, 0);
  EXPECT_EQ(context->covergroupClockEvents, nullptr);
  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeInternals, CovergroupStrobeDefersAndCoalescesUntilPostponedDrain) {
  constexpr uint64_t observerID = 984;
  const obelisk_rt_observer_capture_abi_v1 captureABI{
      OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
  const obelisk_rt_observer_capture_abi_v1 primaryABI{
      OBELISK_RT_OBSERVER_CAPTURE_STORAGE, 1};
  const std::array<obelisk_rt_observer_descriptor_v1, 2> observers{{
      {covergroupClockEventPrimaryObserverID, &primaryABI, 1, 1,
       OBELISK_RT_OBSERVER_FOUR_STATE, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventPrimaryEvaluator, 0},
      {observerID, &captureABI, 1, 1, 0, OBELISK_RT_OBSERVER_NO_BYTECODE,
       covergroupClockEventEvaluator, 0},
  }};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 2;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->coverage = std::make_unique<CoverageState>();
  FunctionalCoverageInstanceState coverageInstance;
  coverageInstance.strobe = true;
  context->coverage->instances.emplace(42, std::move(coverageInstance));

  obelisk_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  obelisk_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.context = context;
  instance.tier = OBELISK_RT_TIER_NATIVE;
  context->scheduledProcesses.emplace_back();
  ScheduledProcess &scheduled = context->scheduledProcesses.back();
  scheduled.instance = &instance;
  scheduled.token = 1;
  scheduled.started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  context->activeNativeProcess = &instance;
  context->activeLogicalProcessToken =
      OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1};

  const uint64_t primary = 0;
  const uint64_t condition = UINT64_MAX;
  const uint32_t edge = OBELISK_RT_WAIT_EDGE_BOTH;
  std::vector<uint64_t> eventPlan =
      makeCovergroupClockEventPlan({primary}, {condition}, {edge});
  auto *eventRecord =
      reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(eventPlan.data());
  const obelisk_rt_computed_capture_v1 capture{42, 0, 0, 0};
  EXPECT_EQ(obelisk_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 0,
                observerID, &capture, 1),
            OBELISK_RT_INVALID_DESIGN);
  ASSERT_EQ(obelisk_rt_v1_covergroup_clock_event_register(
                context, eventRecord, eventPlan.size() * sizeof(uint64_t), 1,
                observerID, &capture, 1),
            OBELISK_RT_OK);
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;

  covergroupClockEventSamples.clear();
  covergroupClockEventGlobalPlane = 0;
  covergroupClockEventSentinel = 1;
  const uint8_t zero = 0;
  const uint8_t one = 1;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &one, nullptr, &zero,
                                            nullptr);
  EXPECT_TRUE(obelisk_rt_covergroup_strobes_pending_unlocked(context));
  EXPECT_TRUE(covergroupClockEventSamples.empty());
  EXPECT_EQ(context->nativePeriodicTerminationRequested, 0u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_handoff_pending(context), 1u);

  // The sample expression is evaluated only when Postponed work drains, and
  // the two qualifying publications above collapse to one sample. A signal
  // publication reentered from the callback is still in this numeric time and
  // must not queue a second automatic sample.
  covergroupClockEventGlobalPlane = 1;
  covergroupClockEventReentrantTransition = true;
  covergroupClockEventRegion = UINT32_MAX;
  context->activeHomeRegion = OBELISK_RT_REGION_REACTIVE;
  context->activeExecRegion = OBELISK_RT_REGION_REACTIVE;
  ASSERT_EQ(obelisk_rt_drain_covergroup_strobes_unlocked(context),
            OBELISK_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1}));
  EXPECT_EQ(covergroupClockEventRegion, OBELISK_RT_REGION_POSTPONED);
  EXPECT_EQ(context->activeHomeRegion, OBELISK_RT_REGION_REACTIVE);
  EXPECT_EQ(context->activeExecRegion, OBELISK_RT_REGION_REACTIVE);
  EXPECT_FALSE(obelisk_rt_covergroup_strobes_pending_unlocked(context));
  EXPECT_EQ(obelisk_rt_v1_scheduler_handoff_pending(context), 0u);
  ASSERT_EQ(obelisk_rt_drain_covergroup_strobes_unlocked(context),
            OBELISK_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples.size(), 1u);

  // start()/stop() controls collection at the actual Postponed sample time.
  ++context->schedulerTime;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  context->coverage->instances[42].enabled = false;
  ASSERT_EQ(obelisk_rt_drain_covergroup_strobes_unlocked(context),
            OBELISK_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples.size(), 1u);

  ++context->schedulerTime;
  context->coverage->instances[42].enabled = false;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &one, nullptr, &zero,
                                            nullptr);
  context->coverage->instances[42].enabled = true;
  covergroupClockEventGlobalPlane = 0;
  ASSERT_EQ(obelisk_rt_drain_covergroup_strobes_unlocked(context),
            OBELISK_RT_OK);
  EXPECT_EQ(covergroupClockEventSamples, std::vector<uint32_t>({1, 0}));

  // Removing an owner with a queued sample removes its pending contribution;
  // no stale Postponed work survives process teardown.
  ++context->schedulerTime;
  obelisk_rt_v1_scheduler_signal_transition(context, 0, 1, &zero, nullptr, &one,
                                            nullptr);
  EXPECT_TRUE(obelisk_rt_covergroup_strobes_pending_unlocked(context));
  obelisk_rt_program_complete_unlocked(
      context, OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | uint64_t{1}, 0);
  EXPECT_EQ(context->covergroupClockEvents, nullptr);
  context->scheduledProcesses.clear();
  context->scheduledProcessIndices.clear();
  obelisk_rt_v1_context_destroy(context);
}

TEST(SampledValues, PreponedObserverRunsOncePerTimeSlot) {
  constexpr uint64_t observerID = 99;
  obelisk_rt_sampled_range_v1 sampledRange{0, 0, 1};
  struct {
    obelisk_rt_execution_descriptor_v1 execution{};
    obelisk_rt_execution_extension_v1 extension{};
  } storage;
  storage.extension = {OBELISK_RT_EXECUTION_EXTENSION_VERSION,
                       sizeof(obelisk_rt_execution_extension_v1), &sampledRange,
                       1};
  obelisk_rt_observer_descriptor_v1 observer{observerID,
                                             nullptr,
                                             0,
                                             1,
                                             0,
                                             OBELISK_RT_OBSERVER_NO_BYTECODE,
                                             preponedObserverEvaluator,
                                             0};
  auto &execution = storage.execution;
  execution.version = OBELISK_RT_VERSION;
  execution.flags = OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.state_bit_count = 1;
  execution.observers = &observer;
  execution.observer_count = 1;
  execution.reserved = offsetof(decltype(storage), extension);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);

  struct ObserverWait {
    obelisk_rt_computed_wait_record_v1 wait{};
    obelisk_rt_computed_observer_v1 observer{};
    obelisk_rt_computed_dependency_v1 dependency{};
    obelisk_rt_computed_clause_v1 clause{};
    uint64_t previousValue = 0;
    uint64_t previousUnknown = 0;
  } record;
  record.wait = {OBELISK_RT_VERSION,
                 OBELISK_RT_SUSPEND_OBSERVER,
                 OBELISK_RT_COMPUTED_WAIT_INTERLEAVED,
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
  record.dependency = {OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT,
                       OBELISK_RT_OBSERVER_DEPENDENCY_EVENT, 1};
  record.clause = {0, OBELISK_RT_OBSERVER_CONDITION_NONE,
                   OBELISK_RT_WAIT_EDGE_NEGEDGE, 0};
  ASSERT_TRUE(obelisk_rt_validate_computed_wait_record(&execution, &record.wait,
                                                       sizeof(record)));

  obelisk_rt_process_descriptor_v1 descriptor{};
  descriptor.execution = &execution;
  obelisk_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.frame = &record;
  instance.frame_size = sizeof(record);
  instance.context = context;
  context->scheduledProcesses.emplace_back();
  ScheduledProcess &scheduled = context->scheduledProcesses.back();
  scheduled.instance = &instance;
  scheduled.token = 1;
  scheduled.waitSize = sizeof(record);
  scheduled.suspendKind = OBELISK_RT_SUSPEND_OBSERVER;
  scheduled.started = true;
  context->scheduledProcessIndices.emplace(1, 0);
  ASSERT_TRUE(obelisk_rt_register_computed_signal_wait_unlocked(
      context, &record.wait, scheduled.token, false,
      scheduled.signalSubscriptions, scheduled.signalLatch));
  ASSERT_TRUE(context->preponedObserverPresent);

  preponedObserverSamples.clear();
  context->stateValue[0] = 0;
  context->stateUnknown[0] = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(preponedObserverSamples, std::vector<uint32_t>({0}));
  ASSERT_EQ(context->events[OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT].generation,
            1u);

  // A late mutation in the same time slot neither republishes the private
  // event nor changes the already captured sampled plane.
  context->stateValue[0] = 1;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(preponedObserverSamples, std::vector<uint32_t>({0}));
  uint8_t sampledValue = 0;
  uint8_t sampledUnknown = 0;
  uint64_t handle =
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_GLOBAL, 0, 0);
  ASSERT_EQ(obelisk_rt_v1_sampled_read(context, handle, 1, &sampledValue,
                                       &sampledUnknown),
            OBELISK_RT_OK);
  EXPECT_EQ(sampledValue, 0);
  EXPECT_EQ(sampledUnknown, 0);

  // The next time slot captures the mutation and notifies exactly once.
  context->schedulerTime = 1;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(preponedObserverSamples, std::vector<uint32_t>({0, 1}));
  EXPECT_EQ(context->events[OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT].generation,
            2u);
  EXPECT_EQ(context->events[OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT]
                .lastTriggeredTime,
            1u);

  obelisk_rt_unregister_signal_wait_unlocked(
      context, scheduled.signalSubscriptions, scheduled.token, false);
  scheduled.instance = nullptr;
  context->scheduledProcessIndices.clear();
  context->scheduledProcesses.clear();
  obelisk_rt_v1_context_destroy(context);
}

TEST(SampledValues, ValidatesExecutionExtension) {
  obelisk_rt_sampled_range_v1 sampledRange{0, 0, 1};
  struct {
    obelisk_rt_execution_descriptor_v1 execution{};
    obelisk_rt_execution_extension_v1 extension{};
  } storage;
  storage.extension = {OBELISK_RT_EXECUTION_EXTENSION_VERSION,
                       sizeof(obelisk_rt_execution_extension_v1), &sampledRange,
                       1};
  auto &execution = storage.execution;
  execution.version = OBELISK_RT_VERSION;
  execution.flags = OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);

  execution.reserved = offsetof(decltype(storage), extension);
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
  context = nullptr;

  execution.flags = 0;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);
  execution.flags = OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT;
  ++storage.extension.version;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);
}

TEST(SampledValues, CapturesBoundNativePlanesWithoutWholeStateCopies) {
  obelisk_rt_sampled_range_v1 sampledRange{8, 0, 4};
  struct {
    obelisk_rt_execution_descriptor_v1 execution{};
    obelisk_rt_execution_extension_v1 extension{};
  } storage;
  storage.extension = {OBELISK_RT_EXECUTION_EXTENSION_VERSION,
                       sizeof(obelisk_rt_execution_extension_v1), &sampledRange,
                       1};
  auto &execution = storage.execution;
  execution.version = OBELISK_RT_VERSION;
  execution.flags = OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.reserved = offsetof(decltype(storage), extension);
  execution.state_bit_count = 32;

  obelisk_rt_context *first = nullptr;
  obelisk_rt_context *second = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &second),
            OBELISK_RT_OK);
  uint8_t firstValue[4] = {};
  uint8_t firstUnknown[4] = {};
  uint8_t secondValue[4] = {};
  uint8_t secondUnknown[4] = {};
  ASSERT_EQ(
      obelisk_rt_v1_native_state_sync(first, firstValue, firstUnknown, 32),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_native_state_sync(second, secondValue, secondUnknown, 32),
      OBELISK_RT_OK);

  // Publication after binding is observed directly at the next Preponed
  // capture; sync does not need to copy the complete planes again.
  firstValue[1] = UINT8_C(0x0a);
  firstUnknown[1] = UINT8_C(0x04);
  secondValue[1] = UINT8_C(0x03);
  ASSERT_EQ(obelisk_rt_capture_preponed_unlocked(first), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_capture_preponed_unlocked(second), OBELISK_RT_OK);

  uint64_t handle =
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_GLOBAL, 0, 8);
  uint8_t value = 0, unknown = 0;
  EXPECT_EQ(obelisk_rt_v1_sampled_read(first, handle, 4, &value, &unknown),
            OBELISK_RT_OK);
  EXPECT_EQ(value, UINT8_C(0x0a));
  EXPECT_EQ(unknown, UINT8_C(0x04));
  EXPECT_EQ(obelisk_rt_v1_sampled_read(second, handle, 4, &value, &unknown),
            OBELISK_RT_OK);
  EXPECT_EQ(value, UINT8_C(0x03));
  EXPECT_EQ(unknown, UINT8_C(0x00));

  obelisk_rt_v1_context_destroy(first);
  obelisk_rt_v1_context_destroy(second);
}

TEST(SampledValues, SharesCompilerPlannedAlternateClockHistory) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  constexpr uint64_t site = UINT64_C(0x12345678);
  uint8_t value = 0, unknown = 0, resultValue = 0, resultUnknown = 0;

  // Missing ages have the sampled-value default. A disabled clock tick does
  // not advance the shared ring.
  ASSERT_EQ(obelisk_rt_v1_clocked_sample_read(context, site, 4, 2, 0, 1,
                                              &resultValue, &resultUnknown),
            OBELISK_RT_OK);
  EXPECT_EQ(resultValue, UINT8_C(0x00));
  EXPECT_EQ(resultUnknown, UINT8_C(0x0f));
  value = UINT8_C(0x03);
  ASSERT_EQ(obelisk_rt_v1_clocked_sample_update(context, site, 4, 2, 1, 0,
                                                &value, &unknown),
            OBELISK_RT_OK);

  const uint8_t samples[] = {UINT8_C(0x0a), UINT8_C(0x04), UINT8_C(0x0c)};
  const uint8_t unknowns[] = {UINT8_C(0x00), UINT8_C(0x01), UINT8_C(0x00)};
  for (size_t index = 0; index != 3; ++index)
    ASSERT_EQ(obelisk_rt_v1_clocked_sample_update(
                  context, site, 4, 2, 1, 1, &samples[index], &unknowns[index]),
              OBELISK_RT_OK);

  const uint8_t expectedValues[] = {UINT8_C(0x0c), UINT8_C(0x04),
                                    UINT8_C(0x0a)};
  const uint8_t expectedUnknowns[] = {UINT8_C(0x00), UINT8_C(0x01),
                                      UINT8_C(0x00)};
  for (uint64_t age = 0; age != 3; ++age) {
    ASSERT_EQ(obelisk_rt_v1_clocked_sample_read(context, site, 4, 2, age, 1,
                                                &resultValue, &resultUnknown),
              OBELISK_RT_OK);
    EXPECT_EQ(resultValue, expectedValues[age]);
    EXPECT_EQ(resultUnknown, expectedUnknowns[age]);
  }
  EXPECT_EQ(obelisk_rt_v1_clocked_sample_read(context, site, 4, 2, 3, 1,
                                              &resultValue, &resultUnknown),
            OBELISK_RT_INVALID_ARGUMENT);
  obelisk_rt_v1_context_destroy(context);
}

TEST(SampledValues, SkipsSnapshotAllocationWithoutConsumers) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 4096;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_TRUE(context->preponedValue.empty());
  ASSERT_TRUE(context->preponedUnknown.empty());
  ASSERT_EQ(context->preponedValue.capacity(), 0u);
  ASSERT_EQ(context->preponedUnknown.capacity(), 0u);

  context->stateValue.front() = UINT64_MAX;
  context->stateUnknown.front() = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_capture_preponed_unlocked(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->preponedValue.empty());
  EXPECT_TRUE(context->preponedUnknown.empty());
  EXPECT_EQ(context->preponedValue.capacity(), 0u);
  EXPECT_EQ(context->preponedUnknown.capacity(), 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(SampledValues, UsesBoundedGatedPerProcessHistory) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->activeLogicalProcessToken = 17;
  uint8_t currentValue = 0;
  uint8_t currentUnknown = 0;
  uint8_t previousValue = 0;
  uint8_t previousUnknown = 0;
  auto sample = [&](uint8_t value, uint32_t gate) {
    currentValue = value;
    previousValue = previousUnknown = 0;
    return obelisk_rt_v1_sampled_history(context, 91, 4, 2, 1, gate,
                                         &currentValue, &currentUnknown,
                                         &previousValue, &previousUnknown);
  };

  ASSERT_EQ(sample(1, 1), OBELISK_RT_OK);
  EXPECT_EQ(previousUnknown, UINT8_C(0x0f));
  ASSERT_EQ(sample(2, 1), OBELISK_RT_OK);
  EXPECT_EQ(previousUnknown, UINT8_C(0x0f));
  ASSERT_EQ(sample(3, 0), OBELISK_RT_OK);
  EXPECT_EQ(previousValue, 1);
  EXPECT_EQ(previousUnknown, 0);
  ASSERT_EQ(sample(3, 1), OBELISK_RT_OK);
  EXPECT_EQ(previousValue, 1);
  ASSERT_EQ(sample(4, 1), OBELISK_RT_OK);
  EXPECT_EQ(previousValue, 2);

  // The same compiler site in another logical process owns another ring.
  context->activeLogicalProcessToken = 18;
  ASSERT_EQ(sample(9, 1), OBELISK_RT_OK);
  EXPECT_EQ(previousUnknown, UINT8_C(0x0f));
  obelisk_rt_v1_context_destroy(context);
}

} // namespace
