//===- ProcessTest.cpp - Shared process instance ABI tests ---------------===//

#include "obelisk/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <cstring>
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
      *wait = {OBELISK_RT_WAIT_RECORD_VERSION,
               OBELISK_RT_SUSPEND_DELAY,
               0,
               0,
               17,
               0};
    *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                         wait->kind,
                         1,
                         OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
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
    layout = {OBELISK_RT_FRAME_LAYOUT_VERSION,
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
                  OBELISK_RT_ABI_GENERATION,
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
unsigned schedulerResumeCount;
std::vector<uint64_t> schedulerOrder;

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
    *instance->action = {OBELISK_RT_FRAGMENT_TERMINATE,
                         OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return OBELISK_RT_OK;
  }
  if (instance->continuation != 0) {
    schedulerOrder.push_back(instance->descriptor->handle.id);
    ++schedulerResumeCount;
    *instance->action = {OBELISK_RT_FRAGMENT_TERMINATE,
                         OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return OBELISK_RT_OK;
  }
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(
      static_cast<uint8_t *>(instance->frame) + schedulerWaitOffset);
  auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  uint32_t version = schedulerWaitKind == OBELISK_RT_SUSPEND_CHANGE ||
                             schedulerWaitKind == OBELISK_RT_SUSPEND_EDGE
                         ? OBELISK_RT_WAIT_RECORD_SIGNAL_WIDTH_VERSION
                         : OBELISK_RT_WAIT_RECORD_VERSION;
  if (schedulerWaitKind == OBELISK_RT_SUSPEND_DELAY) {
    *wait = {version, schedulerWaitKind, 0, 0, 17, 0};
  } else {
    *wait = {version, schedulerWaitKind, 0, 1, 0, 0};
    *entry = {schedulerWaitHandle, schedulerWaitEdge,
              version == OBELISK_RT_WAIT_RECORD_SIGNAL_WIDTH_VERSION
                  ? schedulerWaitWidth
                  : 0};
  }
  *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                       schedulerWaitKind,
                       1,
                       OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                       schedulerWaitOffset,
                       48};
  return OBELISK_RT_OK;
}

void schedulerDestroy(obelisk_rt_process_instance_v1 *instance) {
  instance->native_handle = nullptr;
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
  *instance->action = {OBELISK_RT_FRAGMENT_TERMINATE,
                       OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
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
  *instance->action = {OBELISK_RT_FRAGMENT_TERMINATE,
                       OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
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
  status = obelisk_rt_v1_scheduler_nba(
      instance->context, &nbaDummyPlane, nullptr, 8, nbaAutomaticHandle, 8,
      3, &replacement, nullptr);
  if (status != OBELISK_RT_OK)
    return status;
  status = obelisk_rt_v1_native_state_release(instance->context,
                                               nbaAutomaticHandle, 1);
  if (status != OBELISK_RT_OK)
    return status;
  *instance->action = {OBELISK_RT_FRAGMENT_TERMINATE,
                       OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return OBELISK_RT_OK;
}

struct SchedulerFixture {
  std::array<obelisk_rt_frame_field_v1, 2> fields{{
      {OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 0, 48, 8, 0},
      {OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 48, 48, 8,
       0},
  }};
  std::array<uint32_t, 2> continuations{{0, 1}};
  obelisk_rt_frame_layout_v1 layout{};
  obelisk_rt_process_descriptor_v1 descriptor{};

  explicit SchedulerFixture(uint64_t id) {
    schedulerWaitOffset = 0;
    layout = {OBELISK_RT_FRAME_LAYOUT_VERSION,
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
                  OBELISK_RT_ABI_GENERATION,
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

obelisk_rt_process_instance_v1 *makeSchedulerInstance(SchedulerFixture &fixture) {
  obelisk_rt_process_instance_v1 *instance = nullptr;
  EXPECT_EQ(obelisk_rt_v1_process_instance_create(&fixture.descriptor,
                                                   &instance),
            OBELISK_RT_OK);
  return instance;
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(
                context, makeSchedulerInstance(fixture), 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  obelisk_rt_v1_scheduler_signal(context, 80, 1, OBELISK_RT_SIGNAL_POSEDGE);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);
  obelisk_rt_v1_scheduler_signal(context, 18, 1, OBELISK_RT_SIGNAL_CHANGE);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);
  obelisk_rt_v1_scheduler_signal(context, 18, 1,
                                 OBELISK_RT_SIGNAL_CHANGE |
                                     OBELISK_RT_SIGNAL_POSEDGE);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
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
    ASSERT_EQ(obelisk_rt_v1_scheduler_add(
                  context, makeSchedulerInstance(fixture), 0),
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(
                context, makeSchedulerInstance(fixture), 0),
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_planned(
                context, makeSchedulerInstance(later), 0, 0, &continuation,
                &laterRank, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_planned(
                context, makeSchedulerInstance(earlier), 0, 0, &continuation,
                &earlierRank, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerOrder, (std::vector<uint64_t>{20, 10}));
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

TEST(Scheduler, AutomaticStateAllocationsAreIsolatedAndBoundsChecked) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  uint8_t global = 0x5a;
  uint8_t firstInitial = 0x11;
  uint8_t secondInitial = 0x22;
  uint64_t first = UINT64_MAX;
  uint64_t second = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 8, &firstInitial, nullptr, &first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 8, &secondInitial, nullptr, &second),
            OBELISK_RT_OK);
  ASSERT_NE(first, second);
  uint8_t replacement = 0xfe;
  uint8_t changed = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context, &global, 8, first, 8, 0, &replacement, &changed),
            OBELISK_RT_OK);
  EXPECT_EQ(changed, 1u);
  uint8_t loaded = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &global, 8, second, 8, 0, 0, &loaded),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded, secondInitial);
  loaded = 0;
  uint64_t tail = obelisk_rt_v1_native_handle_offset(first, 6);
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &global, 8, tail, 4, 0, 1, &loaded),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded & 0xfu, 0xfu);
  EXPECT_EQ(global, 0x5a);
  obelisk_rt_v1_context_destroy(context);
}

TEST(Scheduler, AutomaticStateIsReleasedWithItsOwningProcess) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  SchedulerFixture fixture(8);
  fixture.descriptor.native_execute = automaticStateExecute;
  processAutomaticHandle = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(
                context, makeSchedulerInstance(fixture), 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_NE(processAutomaticHandle, UINT64_MAX);
  uint8_t global = 0;
  uint8_t loaded = 0;
  EXPECT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &global, 8, processAutomaticHandle, 8, 0, 0,
                &loaded),
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
                context, &global, 8, processAutomaticHandle, 8, 0, 0,
                &loaded),
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(
                context, makeSchedulerInstance(fixture), 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_NE(retainedAutomaticHandle, UINT64_MAX);

  uint8_t global = 0;
  uint8_t loaded = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &global, 8, retainedAutomaticHandle, 8, 0, 0,
                &loaded),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded, 0xa5);

  ASSERT_EQ(obelisk_rt_v1_native_state_release(
                context, retainedAutomaticHandle, 0),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &global, 8, retainedAutomaticHandle, 8, 0, 0,
                &loaded),
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
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &nbaDummyPlane, 8, nbaAutomaticHandle, 8, 0, 0,
                &loaded),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded, 0u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &nbaDummyPlane, 8, nbaAutomaticHandle, 8, 0, 0,
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
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, &plane, 8, lower, 2, 0, 0, &loaded),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded & 0x3, 0x2);

  plane = 0xc3;
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(
                context, &plane, nullptr, 8, lower, 2, 0,
                &lowerReplacement, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(
                context, &plane, nullptr, 8, upper, 2, 0,
                &upperReplacement, nullptr),
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
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(
                context, makeSchedulerInstance(fixture), 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  uint64_t upper = obelisk_rt_v1_native_handle_offset(
      obelisk_rt_v1_native_state_static_handle(1), 3);
  uint8_t oldValue = 0;
  uint8_t newValue = 2;
  obelisk_rt_v1_scheduler_signal_transition(
      context, upper, 2, &oldValue, nullptr, &newValue, nullptr);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 0u);

  obelisk_rt_v1_scheduler_signal(
      context, obelisk_rt_v1_native_state_static_handle(2), 1,
      OBELISK_RT_SIGNAL_CHANGE);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(schedulerResumeCount, 1u);
  obelisk_rt_v1_context_destroy(context);
}

void initializeWait(void *frame) {
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(
      static_cast<uint8_t *>(frame) + 8);
  *wait = {
      OBELISK_RT_WAIT_RECORD_VERSION, OBELISK_RT_SUSPEND_DELAY, 0, 0, 17, 0};
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
    *wait = {OBELISK_RT_WAIT_RECORD_VERSION, kind, flags, count, 0, 0};
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
  *wait = {OBELISK_RT_WAIT_RECORD_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 2, 0, 0};
  entries[0] = {17, OBELISK_RT_WAIT_EDGE_CHANGE, 0};
  entries[1] = {18, OBELISK_RT_WAIT_EDGE_POSEDGE, 0};
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);

  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  EXPECT_EQ(nativeDestroyCount, 9);
}

} // namespace
