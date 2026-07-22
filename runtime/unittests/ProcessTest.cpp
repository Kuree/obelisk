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
  auto *context = obelisk_rt_v1_context_create(OBELISK_RT_V1_ABI_VERSION);
  ASSERT_NE(context, nullptr);
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
