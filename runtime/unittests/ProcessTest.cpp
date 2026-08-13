//===- ProcessTest.cpp - Shared process instance ABI tests ---------------===//

#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "../lib/RuntimeInternal.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
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
unsigned schedulerCheckpointCount;
unsigned generatedCheckpointCallbackCount;
obelisk_rt_status invalidGeneratedCheckpointStatus;
obelisk_rt_status validGeneratedCheckpointStatus;
unsigned schedulerPromotionInvalidationCount;
unsigned schedulerPromotionReadyCount;
bool schedulerPromotionReadyValue;

void schedulerInvalidatePromotion() {
  ++schedulerPromotionInvalidationCount;
}

uint32_t schedulerPromotionReady() {
  ++schedulerPromotionReadyCount;
  return schedulerPromotionReadyValue ? 1u : 0u;
}

struct AOTTestState {
  using RunHook = obelisk_rt_status (*)(AOTTestState *, obelisk_rt_context *);

  std::array<obelisk_rt_process_instance_v1 *, 2> actors{};
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
    return obelisk_rt_v1_native_state_load_plane(
        context, state->authorityPlane, 8, state->authorityHandle, 8, 0, 0,
        &value);
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
  invalidGeneratedCheckpointStatus =
      obelisk_rt_v1_scheduler_queue_aot_checkpoint(
          context, 0, 2, generatedCheckpointCallback);
  validGeneratedCheckpointStatus =
      obelisk_rt_v1_scheduler_queue_aot_checkpoint(
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

obelisk_rt_status runGroupedStaticActivationNodes(AOTTestState *,
                                                  obelisk_rt_context *context) {
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
schedulerSelfTriggerExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action || !instance->context)
    return OBELISK_RT_INVALID_ARGUMENT;
  instance->native_handle = instance;
  if (instance->continuation != 0) {
    ++schedulerSelfTriggerCount;
    if (schedulerSelfTriggerCount == 3) {
      *instance->action = {OBELISK_RT_FRAGMENT_TERMINATE,
                           OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
      return OBELISK_RT_OK;
    }
    if (schedulerSelfTriggerStaticState != 0) {
      uint64_t oldValue = schedulerSelfTriggerCount & 1;
      obelisk_rt_v1_scheduler_static_transition(
          instance->context, schedulerSelfTriggerStaticState, 0, 1, oldValue,
          0, oldValue ^ 1, 0);
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
      const uint64_t nodes[] = {uint64_t{1} << 3};
      // The second publication must leave one ready bit and one resume.
      obelisk_rt_v1_scheduler_activate_static_nodes(instance->context, nodes,
                                                    std::size(nodes));
      obelisk_rt_v1_scheduler_activate_static_nodes(instance->context, nodes,
                                                    std::size(nodes));
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
  *instance->action = {
      OBELISK_RT_FRAGMENT_SUSPEND,
      id == 90 ? OBELISK_RT_SUSPEND_DELAY : OBELISK_RT_SUSPEND_CHANGE,
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

obelisk_rt_process_instance_v1 *
makeSchedulerInstance(SchedulerFixture &fixture) {
  obelisk_rt_process_instance_v1 *instance = nullptr;
  EXPECT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  return instance;
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
  examinedBefore = context->signalDiagnostics.subscribersExamined;
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE));
  ASSERT_TRUE(obelisk_rt_publish_signal_occurrence_unlocked(
      context, 16, 1, OBELISK_RT_SIGNAL_CHANGE));
  EXPECT_EQ(context->pendingNativeComputedWaiters, std::vector<uint64_t>({42}));
  EXPECT_TRUE(context->pendingDesignComputedWaiters.empty());
  EXPECT_EQ(context->signalDiagnostics.subscribersExamined - examinedBefore,
            2u);
  obelisk_rt_unregister_signal_wait_unlocked(context, computedSubscriptions);
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
                                 schedulerWaitWidth,
                                 OBELISK_RT_SIGNAL_CHANGE);
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context,
                                        makeSchedulerInstance(active), 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_TRUE(schedulerOrder.empty());

  // An actor that has never run does take its first activation ahead of
  // signal resumptions, but only within its own region: a postponed actor
  // that preempted this active resume would observe the values from before
  // the active region ran, and would leave the Postponed region reachable a
  // second time in the same time slot (IEEE 1800-2017 4.4.2.9).
  obelisk_rt_v1_scheduler_signal(context, schedulerWaitHandle,
                                 schedulerWaitWidth,
                                 OBELISK_RT_SIGNAL_CHANGE);
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

TEST(Scheduler, AOTCheckpointRunsOneRuntimeActionAndReentersNatively) {
  AOTTestState state;
  state.runHook = runCheckpointThenReenter;
  obelisk_rt_native_schedule_plan plan = makeAOTPlan(state, 1);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_OK);

  SchedulerFixture fixture(321);
  schedulerOrder.clear();
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context,
                                        makeSchedulerInstance(fixture), 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(state.runCalls, 2u);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{321}));
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 0u);
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_OK);

  SchedulerFixture fixture(22);
  schedulerWaitKind = OBELISK_RT_SUSPEND_CHANGE;
  schedulerWaitEdge = OBELISK_RT_WAIT_EDGE_CHANGE;
  schedulerWaitHandle = 17;
  schedulerWaitWidth = 1;
  schedulerWaitOffset = 0;
  generatedCheckpointCallbackCount = 0;
  invalidGeneratedCheckpointStatus = OBELISK_RT_OK;
  validGeneratedCheckpointStatus = OBELISK_RT_INVALID_CONTINUATION;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(
                context, makeSchedulerInstance(fixture), 0, 0, 0, nullptr,
                nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);
  // Establish the source wait before the generated transaction takes direct
  // ownership of continuation 1.
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(invalidGeneratedCheckpointStatus,
            OBELISK_RT_INVALID_CONTINUATION);
  EXPECT_EQ(validGeneratedCheckpointStatus, OBELISK_RT_OK);
  EXPECT_EQ(generatedCheckpointCallbackCount, 2u);
  EXPECT_EQ(state.runCalls, 2u);
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_OK);

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
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_OK);

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
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_INVALID_ARGUMENT);

  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
               OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
               OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
               OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT;
  EXPECT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_OK);
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
  EXPECT_EQ(state.actors[0], nullptr);
  EXPECT_EQ(state.actors[1], nullptr);
  obelisk_rt_v1_context_destroy(context);
}

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
                context, makeSchedulerInstance(fixture), 0, 0, 0,
                &continuation, &continuationRank, 1, nullptr, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(context->signalDiagnostics.aotFallbacks, 1u);
  ASSERT_EQ(context->scheduledProcesses.size(), 1u);
  EXPECT_EQ(context->scheduledProcesses.front().scheduleRank,
            continuationRank);
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
  EXPECT_EQ(context->nativeSchedulePersistentDirtySummary[0],
            uint64_t{1} << 1);
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
      context, root, 1, &changed, &posedge, &negedge, &newValue, &newUnknown,
      true));
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
  obelisk_rt_v1_context_destroy(context);
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_OK);

  SchedulerFixture fixture(35);
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0,
                    0, 0);
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_aot(context, instance, 0, 0, 0,
                                            nullptr, nullptr, 0, nullptr, 0),
            OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run_aot(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerCheckpointCount, 1u);
  EXPECT_EQ(schedulerPromotionInvalidationCount, 1u);
  EXPECT_EQ(instance->tier, OBELISK_RT_TIER_BYTECODE);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  ASSERT_EQ(context->scheduledProcesses.front().signalSubscriptions.size(),
            1u);
  EXPECT_EQ(context->scheduledProcesses.front().signalSubscriptions.front()
                ->stableID,
            16u);
  // Exercise the real signal subscription and AOT ready-bit routing on the
  // Tier-3 return continuation instead of mutating scheduler internals.
  obelisk_rt_v1_scheduler_signal(
      context, 16, 8,
      OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_NEGEDGE);
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
  obelisk_rt_process_instance_v1 *firstInstance =
      makeSchedulerInstance(first);
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
    obelisk_rt_assoc_key_v1 key{OBELISK_RT_ASSOC_KEY_PROCESS, 0, 0,
                                tokens[index], 0, 0};
    uint64_t value = index + 20;
    ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              OBELISK_RT_OK);
  }

  obelisk_rt_assoc_key_v1 invalid{
      OBELISK_RT_ASSOC_KEY_PROCESS, 0, 0,
      OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG | UINT64_C(0x123456), 0, 0};
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

  obelisk_rt_reparent_process_children_unlocked(context, tag | 4, tag | 2);

  EXPECT_EQ(context->scheduledProcesses[0].parent, tag | 1);
  EXPECT_EQ(context->scheduledProcesses[1].parent, tag | 2);
  EXPECT_EQ(context->scheduledDesignTasks[0].parent, tag | 2);
  context->scheduledProcesses.clear();
  context->scheduledDesignTasks.clear();
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

TEST(Scheduler, GeneratedNBAScalarCommitsValueUnknownAndPartMaskDirectly) {
  AOTTestState state;
  obelisk_rt_generated_nba_accumulator_256 generated{};
  const obelisk_rt_static_nba_root roots[] = {
      {17, 1, 8, &generated},
  };
  uint8_t valuePlane = 0xc0;
  uint8_t unknownPlane = 0xf0;
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  generated.value[0] = 0x0a;
  generated.unknown[0] = 0x05;
  generated.write_mask[0] = 0x0f;
  generated.valid = 1;
  generated.exec_region = OBELISK_RT_REGION_NBA;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(valuePlane, 0xca);
  EXPECT_EQ(unknownPlane, 0xf5);
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
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 2u);
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
    ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, root + 1,
                                                         root, 1),
              OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  generated[64].value[0] = 1;
  generated[64].write_mask[0] = 1;
  generated[64].valid = 1;
  generated[64].exec_region = OBELISK_RT_REGION_NBA;
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
    ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, root + 1,
                                                         root, 1),
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
  EXPECT_EQ(context->signalDiagnostics.aotNBAStages, 2u);
  EXPECT_EQ(context->signalDiagnostics.aotNBACommits, 1u);
  EXPECT_FALSE(context->staticNBAAccumulators[0].valid);
  EXPECT_TRUE(std::all_of(std::begin(generated.write_mask),
                          std::end(generated.write_mask),
                          [](uint64_t mask) { return mask == 0; }));
  EXPECT_EQ(context->signalDiagnostics.publications, 0u);
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan),
            OBELISK_RT_OK);

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

  std::memset(wait, 0, 64);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  entries[0] = {UINT64_MAX, OBELISK_RT_WAIT_EDGE_CHANGE, 1};
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
  entries[0] = {17, OBELISK_RT_WAIT_EDGE_CHANGE, 1};
  entries[1] = {18, OBELISK_RT_WAIT_EDGE_POSEDGE, 1};
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);

  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 11);
}

TEST(SampledValues, CapturesCanonicalPreponedPlane) {
  obelisk_rt_sampled_range_v1 sampledRange{3, 0, 10};
  obelisk_rt_execution_extension_v1 extension{
      OBELISK_RT_EXECUTION_EXTENSION_VERSION,
      sizeof(obelisk_rt_execution_extension_v1), &sampledRange, 1};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.state_bit_count = 4096;
  execution.reserved = reinterpret_cast<uintptr_t>(&extension);
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
  uint64_t handle = obelisk_rt_stable_handle_encode(
      OBELISK_RT_STABLE_HANDLE_GLOBAL, 0, 3);
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

TEST(SampledValues, ValidatesExecutionExtension) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.state_bit_count = 8;
  obelisk_rt_context *context = nullptr;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);

  obelisk_rt_sampled_range_v1 sampledRange{0, 0, 1};
  obelisk_rt_execution_extension_v1 extension{
      OBELISK_RT_EXECUTION_EXTENSION_VERSION,
      sizeof(obelisk_rt_execution_extension_v1), &sampledRange, 1};
  execution.reserved = reinterpret_cast<uintptr_t>(&extension);
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
  context = nullptr;

  execution.flags = 0;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);
  execution.flags = OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT;
  ++extension.version;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);
}

TEST(SampledValues, CapturesBoundNativePlanesWithoutWholeStateCopies) {
  obelisk_rt_sampled_range_v1 sampledRange{8, 0, 4};
  obelisk_rt_execution_extension_v1 extension{
      OBELISK_RT_EXECUTION_EXTENSION_VERSION,
      sizeof(obelisk_rt_execution_extension_v1), &sampledRange, 1};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT;
  execution.reserved = reinterpret_cast<uintptr_t>(&extension);
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
  ASSERT_EQ(obelisk_rt_v1_native_state_sync(first, firstValue, firstUnknown, 32),
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

  uint64_t handle = obelisk_rt_stable_handle_encode(
      OBELISK_RT_STABLE_HANDLE_GLOBAL, 0, 8);
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
  ASSERT_EQ(obelisk_rt_v1_clocked_sample_read(
                context, site, 4, 2, 0, 1, &resultValue, &resultUnknown),
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
    ASSERT_EQ(obelisk_rt_v1_clocked_sample_read(
                  context, site, 4, 2, age, 1, &resultValue, &resultUnknown),
              OBELISK_RT_OK);
    EXPECT_EQ(resultValue, expectedValues[age]);
    EXPECT_EQ(resultUnknown, expectedUnknowns[age]);
  }
  EXPECT_EQ(obelisk_rt_v1_clocked_sample_read(
                context, site, 4, 2, 3, 1, &resultValue, &resultUnknown),
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
    return obelisk_rt_v1_sampled_history(
        context, 91, 4, 2, 1, gate, &currentValue, &currentUnknown,
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
