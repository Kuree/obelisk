//===- GeneratedProcessTest.cpp - Execute compiler coroutine output -------===//

#include "talos/Runtime/Runtime.h"

#include "../lib/ProcessAllocation.h"
#include "../lib/ProcessShared.h"
#include "../lib/RuntimeInternal.h"

#include "GeneratedSymbol.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>
#if defined(_WIN32)
#include <malloc.h>
#endif

extern "C" const talos_rt_process_descriptor_v1 generatedDescriptor asm(
    TALOS_TEST_SYMBOL("execution_process.__talos_process_descriptor"));
extern "C" void generatedRamp(talos_rt_process_instance_v1 *, uint32_t,
                              uint64_t *, uint64_t *)
    asm(TALOS_TEST_SYMBOL("execution_process.__talos_coro_ramp"));
extern "C" const talos_rt_process_descriptor_v1 failingDescriptor asm(
    TALOS_TEST_SYMBOL("failing_process.__talos_process_descriptor"));
extern "C" const talos_rt_process_descriptor_v1 orchestrationDescriptor asm(
    TALOS_TEST_SYMBOL("orchestration_process.__talos_process_descriptor"));
extern "C" const talos_rt_process_descriptor_v1 automaticDescriptor asm(
    TALOS_TEST_SYMBOL("automatic_process.__talos_process_descriptor"));
extern "C" const talos_rt_process_descriptor_v1 automaticLoopDescriptor asm(
    TALOS_TEST_SYMBOL("automatic_loop_process.__talos_process_descriptor"));
extern "C" const talos_rt_process_descriptor_v1 groupDescriptor asm(
    TALOS_TEST_SYMBOL("group_process.__talos_process_descriptor"));
extern "C" const talos_rt_process_descriptor_v1 copyDescriptor asm(
    TALOS_TEST_SYMBOL("copy_process.__talos_process_descriptor"));
extern "C" const talos_rt_process_descriptor_v1 secondCopyDescriptor asm(
    TALOS_TEST_SYMBOL("copy_process_second.__talos_process_descriptor"));

extern "C" const talos_rt_table_process_descriptor_v1 tableDescriptor asm(
    TALOS_TEST_SYMBOL("table_process.__talos_process_descriptor"));

namespace {

std::atomic<size_t> alignedAllocationCount;
uint64_t requestedNativeSize;
uint64_t requestedNativeAlignment;

talos_rt_status requestedNativeRequirements(uint64_t *size,
                                              uint64_t *alignment) {
  if (!size || !alignment)
    return TALOS_RT_INVALID_ARGUMENT;
  *size = requestedNativeSize;
  *alignment = requestedNativeAlignment;
  return TALOS_RT_OK;
}

void appendInstruction(std::array<uint8_t, 64> &code, size_t instruction,
                       uint8_t opcode, uint8_t type, uint16_t destination,
                       uint16_t source0, uint16_t source1, uint64_t immediate) {
  size_t offset = instruction * TALOS_RT_BYTECODE_INSTRUCTION_SIZE;
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

const talos_rt_frame_field_v1 *findWaitField() {
  const talos_rt_frame_layout_v1 &layout = *generatedDescriptor.frame_layout;
  for (uint32_t index = 0; index != layout.field_count; ++index)
    if (layout.fields[index].kind == TALOS_RT_FRAME_WAIT)
      return &layout.fields[index];
  return nullptr;
}

void initializeDelayWait(void *frame) {
  const talos_rt_frame_field_v1 *field = findWaitField();
  ASSERT_NE(field, nullptr);
  auto *wait = reinterpret_cast<talos_rt_wait_record_v1 *>(
      static_cast<uint8_t *>(frame) + field->offset);
  *wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_DELAY, 0, 0, 3, 0};
}

struct DualTierDescriptor {
  std::array<uint8_t, 64> code{};
  std::array<talos_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  talos_rt_bytecode_v1 bytecode{};
  talos_rt_process_descriptor_v1 descriptor{};

  DualTierDescriptor() {
    const talos_rt_frame_field_v1 *wait = findWaitField();
    EXPECT_NE(wait, nullptr);
    appendInstruction(code, 0, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0,
                      0, 0, wait->offset);
    appendInstruction(code, 1, TALOS_RT_BC_SUSPEND, TALOS_RT_BC_TYPE_NONE,
                      0, TALOS_RT_SUSPEND_DELAY, 0, 1);
    appendInstruction(code, 2, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0,
                      0, 0, wait->offset);
    appendInstruction(code, 3, TALOS_RT_BC_SUSPEND, TALOS_RT_BC_TYPE_NONE,
                      0, TALOS_RT_SUSPEND_DELAY, 0, 1);

    uint64_t nativeSize = 0;
    uint64_t nativeAlignment = 0;
    EXPECT_EQ(
        generatedDescriptor.native_requirements(&nativeSize, &nativeAlignment),
        TALOS_RT_OK);
    uint64_t tailAlignment = std::max<uint64_t>(nativeAlignment, 16);
    uint64_t registerOffset =
        (generatedDescriptor.frame_layout->frame_size + tailAlignment - 1) &
        ~(tailAlignment - 1);
    bytecode = {code.data(),
                code.size(),
                entries.data(),
                static_cast<uint32_t>(entries.size()),
                1,
                registerOffset,
                nullptr,
                nullptr,
                0,
                nullptr,
                0,
                0,
                nullptr,
                0};
    descriptor = generatedDescriptor;
    descriptor.available_tiers =
        TALOS_RT_TIER_MASK_NATIVE | TALOS_RT_TIER_MASK_BYTECODE;
    descriptor.bytecode = &bytecode;
    descriptor.design_bytecode = nullptr;
  }
};

TEST(GeneratedProcess, SharedNativeCallbacksPreserveScratchRequirements) {
  EXPECT_EQ(generatedDescriptor.native_destroy,
            failingDescriptor.native_destroy);
  EXPECT_EQ(groupDescriptor.native_destroy, automaticDescriptor.native_destroy);
  EXPECT_EQ(groupDescriptor.native_requirements,
            automaticDescriptor.native_requirements);
  EXPECT_NE(generatedDescriptor.native_destroy, groupDescriptor.native_destroy);
  uint64_t size = UINT64_MAX;
  uint64_t alignment = UINT64_MAX;
  ASSERT_EQ(groupDescriptor.native_requirements(&size, &alignment),
            TALOS_RT_OK);
  EXPECT_EQ(size, 0u);
  EXPECT_EQ(alignment, 1u);
  ASSERT_EQ(generatedDescriptor.native_requirements(&size, &alignment),
            TALOS_RT_OK);
  EXPECT_GT(size, 0u);
  EXPECT_GT(alignment, 0u);
}

TEST(GeneratedProcess, UnstartedNativeFrameDoesNotPublishAndCanBeDestroyed) {
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&generatedDescriptor, &instance),
      TALOS_RT_OK);
  auto *frame = static_cast<uint8_t *>(instance->frame);
  std::vector<uint8_t> savedFrame(
      frame, frame + generatedDescriptor.frame_layout->frame_size);
  auto lifecycle = instance->lifecycle;
  talos_rt_fragment_action_v1 action{};
  instance->action = &action;
  auto savedAction = action;

  // The ordinary wrapper immediately resumes this implementation suspend.
  // Exercise destruction before that resume without running any source code,
  // loading captures, or publishing a semantic wait/continuation.
  generatedRamp(instance, 1, nullptr, nullptr);
  EXPECT_NE(instance->native_handle, nullptr);
  EXPECT_EQ(instance->continuation, 0u);
  EXPECT_EQ(instance->lifecycle, lifecycle);
  EXPECT_EQ(std::memcmp(&action, &savedAction, sizeof(action)), 0);
  EXPECT_TRUE(std::equal(savedFrame.begin(), savedFrame.end(), frame));

  generatedDescriptor.native_destroy(instance);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(instance->continuation, 0u);
  EXPECT_TRUE(std::equal(savedFrame.begin(), savedFrame.end(), frame));
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
}

TEST(GeneratedProcess, NativeResumeTerminateAndSuspendedDestroy) {
  size_t before = alignedAllocationCount.load();
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&generatedDescriptor, &instance),
      TALOS_RT_OK);
  ASSERT_LE(alignedAllocationCount.load(), before + 1);
  size_t afterCreate = alignedAllocationCount.load();
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(talos_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            TALOS_RT_OK);

  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(action.continuation, 1u);
  EXPECT_EQ(action.suspend_kind, TALOS_RT_SUSPEND_DELAY);
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(action.continuation, 2u);
  EXPECT_EQ(alignedAllocationCount.load(), afterCreate);

  void *sameFrame = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_frame(instance, &sameFrame, &frameSize),
      TALOS_RT_OK);
  EXPECT_EQ(sameFrame, frame);
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(instance->lifecycle, TALOS_RT_PROCESS_TERMINATED);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);

  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&generatedDescriptor, &instance),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(instance->lifecycle, TALOS_RT_PROCESS_SUSPENDED);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
}

TEST(GeneratedProcess, EmittedDesignBytecodeMatchesNativeLifecycle) {
  ASSERT_EQ(generatedDescriptor.available_tiers,
            TALOS_RT_TIER_MASK_NATIVE | TALOS_RT_TIER_MASK_BYTECODE);
  ASSERT_NE(generatedDescriptor.execution, nullptr);
  ASSERT_NE(generatedDescriptor.design_bytecode, nullptr);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(
                generatedDescriptor.execution, &context),
            TALOS_RT_OK);
  talos_rt_process_instance_v1 *native = nullptr;
  talos_rt_process_instance_v1 *bytecode = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&generatedDescriptor, &native),
      TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&generatedDescriptor, &bytecode),
      TALOS_RT_OK);

  for (unsigned step = 0; step != 3; ++step) {
    talos_rt_fragment_action_v1 nativeAction{}, bytecodeAction{};
    ASSERT_EQ(talos_rt_v1_process_instance_execute(
                  native, context, TALOS_RT_TIER_NATIVE, &nativeAction),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_process_instance_execute(
                  bytecode, context, TALOS_RT_TIER_BYTECODE, &bytecodeAction),
              TALOS_RT_OK);
    EXPECT_EQ(bytecodeAction.kind, nativeAction.kind);
    EXPECT_EQ(bytecodeAction.suspend_kind, nativeAction.suspend_kind);
    EXPECT_EQ(bytecodeAction.continuation, nativeAction.continuation);
    EXPECT_EQ(bytecodeAction.flags, nativeAction.flags);
    EXPECT_EQ(bytecodeAction.payload, nativeAction.payload);
    EXPECT_EQ(bytecodeAction.auxiliary, nativeAction.auxiliary);
  }
  EXPECT_EQ(native->lifecycle, TALOS_RT_PROCESS_TERMINATED);
  EXPECT_EQ(bytecode->lifecycle, TALOS_RT_PROCESS_TERMINATED);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(native), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(bytecode), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(GeneratedProcess, DirectGroupResumesCanonicalStateAcrossBytecode) {
  uint64_t size = UINT64_MAX, alignment = 0;
  ASSERT_EQ(groupDescriptor.native_requirements(&size, &alignment),
            TALOS_RT_OK);
  ASSERT_EQ(size, 0u);
  ASSERT_EQ(alignment, 1u);

  for (bool startNative : {false, true}) {
    talos_rt_context *context = nullptr;
    std::array<uint64_t, 2> expected{};
    ASSERT_EQ(talos_rt_v1_context_create_for_design(groupDescriptor.execution,
                                                    &context),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 64),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 2, 64, 64),
              TALOS_RT_OK);
    talos_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        talos_rt_v1_process_instance_create(&groupDescriptor, &instance),
        TALOS_RT_OK);
    unsigned captures = 0;
    uint64_t captureOffset = 0;
    const auto &layout = *groupDescriptor.frame_layout;
    for (uint32_t index = 0; index != layout.field_count; ++index) {
      const auto &field = layout.fields[index];
      if (field.kind != TALOS_RT_FRAME_CAPTURE)
        continue;
      ASSERT_EQ(field.size, sizeof(uint64_t));
      captureOffset = field.offset;
      ++captures;
    }
    ASSERT_EQ(captures, 1u);
    void *frame = instance->frame;
    uint32_t continuation = 0;
    for (uint64_t step = 1; step <= 12; ++step) {
      bool native = (step % 2 != 0) == startNative;
      // Tier changes must observe the new capture binding and simulation
      // time without restarting the carried counter or caching signal state.
      size_t lane = (step / 3) % 2;
      context->schedulerTime = step * 100;
      uint64_t handle = talos_rt_v1_native_state_static_handle(lane + 1);
      std::memcpy(static_cast<uint8_t *>(instance->frame) + captureOffset,
                  &handle, sizeof(handle));
      talos_rt_fragment_action_v1 action{};
      ASSERT_EQ(talos_rt_v1_process_instance_execute(
                    instance, context,
                    native ? TALOS_RT_TIER_NATIVE : TALOS_RT_TIER_BYTECODE,
                    &action),
                TALOS_RT_OK);
      EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_SUSPEND);
      EXPECT_EQ(action.suspend_kind, TALOS_RT_SUSPEND_CHANGE);
      if (step == 1)
        continuation = action.continuation;
      EXPECT_NE(continuation, 0u);
      EXPECT_EQ(action.continuation, continuation);
      expected[lane] = step + context->schedulerTime;
      ASSERT_GE(context->stateValue.size(), 2u);
      for (size_t word = 0; word != 2; ++word)
        EXPECT_EQ(context->stateValue[word], expected[word]);
      EXPECT_EQ(instance->frame, frame);
      EXPECT_EQ(instance->native_handle, nullptr);
    }
    EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
    talos_rt_v1_context_destroy(context);
  }
}

TEST(GeneratedProcess, CopyActivationPreservesFourStateTierHandoff) {
  uint64_t size = UINT64_MAX, alignment = 0;
  ASSERT_EQ(copyDescriptor.native_requirements(&size, &alignment),
            TALOS_RT_OK);
  ASSERT_EQ(size, 0u);
  ASSERT_EQ(alignment, 1u);
  for (const auto *descriptor : {&copyDescriptor, &secondCopyDescriptor}) {
    for (bool nativeFirst : {false, true}) {
      std::array<talos_rt_context *, 2> contexts{};
      std::array<talos_rt_process_instance_v1 *, 2> instances{};
      for (unsigned lane = 0; lane != 2; ++lane) {
        ASSERT_EQ(talos_rt_v1_context_create_for_design(descriptor->execution,
                                                          &contexts[lane]),
                  TALOS_RT_OK);
        ASSERT_EQ(talos_rt_v1_native_state_register_static(contexts[lane], 3,
                                                             128, 65),
                  TALOS_RT_OK);
        ASSERT_EQ(talos_rt_v1_native_state_register_static(contexts[lane], 4,
                                                             193, 65),
                  TALOS_RT_OK);
        ASSERT_EQ(
            talos_rt_v1_process_instance_create(descriptor, &instances[lane]),
            TALOS_RT_OK);
        unsigned capture = 0;
        const auto &layout = *descriptor->frame_layout;
        for (uint32_t index = 0; index != layout.field_count; ++index) {
          const auto &field = layout.fields[index];
          if (field.kind != TALOS_RT_FRAME_CAPTURE)
            continue;
          ASSERT_LT(capture, 2u);
          uint64_t handle =
              talos_rt_v1_native_state_static_handle(3 + capture++);
          ASSERT_EQ(field.size, sizeof(handle));
          std::memcpy(static_cast<uint8_t *>(instances[lane]->frame) +
                          field.offset,
                      &handle, sizeof(handle));
        }
        ASSERT_EQ(capture, 2u);
      }
      for (unsigned step = 0; step != 8; ++step) {
        std::array<talos_rt_fragment_action_v1, 2> actions{};
        for (unsigned lane = 0; lane != 2; ++lane) {
          auto *context = contexts[lane];
          ASSERT_GE(context->stateValue.size(), 5u);
          for (unsigned bit = 0; bit != 65; ++bit) {
            uint64_t mask = uint64_t{1} << ((128 + bit) % 64);
            bool value = (bit + step) % 3 == 0;
            bool unknown = step % 4 != 0 && (bit + step) % 5 == 0;
            auto v = context->stateValue[(128 + bit) / 64];
            auto x = context->stateUnknown[(128 + bit) / 64];
            v = (v & ~mask) | (value ? mask : 0);
            x = (x & ~mask) | (unknown ? mask : 0);
          }
          bool native = lane == 0 && ((step % 2 == 0) == nativeFirst);
          ASSERT_EQ(
              talos_rt_v1_process_instance_execute(
                  instances[lane], context,
                  native ? TALOS_RT_TIER_NATIVE : TALOS_RT_TIER_BYTECODE,
                  &actions[lane]),
              TALOS_RT_OK);
          EXPECT_EQ(instances[lane]->native_handle, nullptr);
          EXPECT_EQ(actions[lane].kind, TALOS_RT_FRAGMENT_SUSPEND);
          EXPECT_EQ(actions[lane].suspend_kind, TALOS_RT_SUSPEND_CHANGE);
          EXPECT_EQ(actions[lane].continuation,
                    descriptor == &copyDescriptor ? 1u : 7u);
          for (unsigned bit = 0; bit != 65; ++bit) {
            unsigned target = 193 + bit;
            EXPECT_EQ((context->stateValue[target / 64] >> (target % 64)) & 1,
                      (bit + step) % 3 == 0);
            EXPECT_EQ((context->stateUnknown[target / 64] >> (target % 64)) & 1,
                      step % 4 != 0 && (bit + step) % 5 == 0);
          }
        }
        EXPECT_EQ(actions[0].continuation, actions[1].continuation);
        EXPECT_EQ(actions[0].flags, actions[1].flags);
        EXPECT_EQ(actions[0].payload, actions[1].payload);
        EXPECT_EQ(actions[0].auxiliary, actions[1].auxiliary);
      }
      for (unsigned lane = 0; lane != 2; ++lane) {
        EXPECT_EQ(talos_rt_v1_process_instance_destroy(instances[lane]),
                  TALOS_RT_OK);
        talos_rt_v1_context_destroy(contexts[lane]);
      }
    }
  }
}

TEST(GeneratedProcess, TableEntriesChooseWaitsAcrossTierHandoffs) {
  const auto &descriptor = tableDescriptor.base;
  ASSERT_EQ(descriptor.native_execute, talos_rt_v1_table_process_execute);
  ASSERT_EQ(descriptor.flags & TALOS_RT_PROCESS_TABLE_NATIVE,
            TALOS_RT_PROCESS_TABLE_NATIVE);
  ASSERT_NE(tableDescriptor.plan, nullptr);
  ASSERT_EQ(tableDescriptor.plan->wait_count, 3u);
  uint64_t size = UINT64_MAX, alignment = 0;
  ASSERT_EQ(descriptor.native_requirements(&size, &alignment), TALOS_RT_OK);
  ASSERT_EQ(size, 0u);
  ASSERT_EQ(alignment, 1u);
  for (bool nativeFirst : {false, true}) {
    std::array<talos_rt_context *, 2> contexts{};
    std::array<talos_rt_process_instance_v1 *, 2> instances{};
    for (unsigned lane = 0; lane != 2; ++lane) {
      ASSERT_EQ(talos_rt_v1_context_create_for_design(descriptor.execution,
                                                        &contexts[lane]),
                TALOS_RT_OK);
      for (unsigned capture = 0; capture != 2; ++capture)
        ASSERT_EQ(talos_rt_v1_native_state_register_static(
                      contexts[lane], capture + 1, capture * 64, 64),
                  TALOS_RT_OK);
      ASSERT_EQ(
          talos_rt_v1_process_instance_create(&descriptor, &instances[lane]),
          TALOS_RT_OK);
      unsigned capture = 0;
      const auto &layout = *descriptor.frame_layout;
      for (uint32_t i = 0; i != layout.field_count; ++i) {
        const auto &field = layout.fields[i];
        if (field.kind != TALOS_RT_FRAME_CAPTURE)
          continue;
        uint64_t handle = talos_rt_v1_native_state_static_handle(++capture);
        std::memcpy(static_cast<uint8_t *>(instances[lane]->frame) +
                        field.offset,
                    &handle, sizeof(handle));
      }
      ASSERT_EQ(capture, 2u);
    }
    for (unsigned step = 0; step != 4; ++step) {
      std::array<talos_rt_fragment_action_v1, 2> actions{};
      for (unsigned lane = 0; lane != 2; ++lane) {
        SCOPED_TRACE(::testing::Message()
                     << "native first " << nativeFirst << " step " << step
                     << " lane " << lane);
        contexts[lane]->stateValue[0] = step >= 2;
        contexts[lane]->stateUnknown[0] = 0;
        ASSERT_TRUE(storeNativeScheduleStateUnlocked(contexts[lane], 0, 64,
                                                     step >= 2, 0));
        bool native = lane == 0 && ((step % 2 == 0) == nativeFirst);
        ASSERT_EQ(
            talos_rt_v1_process_instance_execute(
                instances[lane], contexts[lane],
                native ? TALOS_RT_TIER_NATIVE : TALOS_RT_TIER_BYTECODE,
                &actions[lane]),
            TALOS_RT_OK);
        EXPECT_EQ(instances[lane]->native_handle, nullptr);
        EXPECT_EQ(actions[lane].continuation,
                  (std::array<uint32_t, 4>{3, 7, 13, 0})[step]);
        EXPECT_EQ(actions[lane].kind, step == 3 ? TALOS_RT_FRAGMENT_TERMINATE
                                                : TALOS_RT_FRAGMENT_SUSPEND);
        if (step == 1 || step == 2) {
          EXPECT_EQ(contexts[lane]->stateValue[1], step == 1 ? 11u : 22u);
        }
      }
      EXPECT_EQ(actions[0].suspend_kind, actions[1].suspend_kind);
      EXPECT_EQ(actions[0].flags, actions[1].flags);
      EXPECT_EQ(actions[0].payload, actions[1].payload);
      EXPECT_EQ(actions[0].auxiliary, actions[1].auxiliary);
      if (step != 3) {
        EXPECT_EQ(std::memcmp(static_cast<uint8_t *>(instances[0]->frame) +
                                  actions[0].payload,
                              static_cast<uint8_t *>(instances[1]->frame) +
                                  actions[1].payload,
                              sizeof(talos_rt_wait_record_v1) +
                                  (step == 2 ? 2 : 1) *
                                      sizeof(talos_rt_wait_entry_v1)),
                  0);
      }
    }
    for (unsigned lane = 0; lane != 2; ++lane) {
      EXPECT_EQ(talos_rt_v1_process_instance_destroy(instances[lane]),
                TALOS_RT_OK);
      talos_rt_v1_context_destroy(contexts[lane]);
    }
  }
}

TEST(GeneratedProcess, SchedulerRunsEventSpawnJoinAndAwait) {
  ASSERT_NE(orchestrationDescriptor.execution, nullptr);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(
                orchestrationDescriptor.execution, &context),
            TALOS_RT_OK);
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(talos_rt_v1_process_instance_create(&orchestrationDescriptor,
                                                  &instance),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  testing::internal::CaptureStdout();
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(testing::internal::GetCapturedStdout(),
            "join-short\njoin-long\njoined\nawait-child\nawaited\n");
  talos_rt_v1_context_destroy(context);
}

TEST(GeneratedProcess, DynamicAutomaticSurvivesCallSpawnAndDelayedNBA) {
  ASSERT_NE(automaticDescriptor.execution, nullptr);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(
                automaticDescriptor.execution, &context),
            TALOS_RT_OK);
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&automaticDescriptor, &instance),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(GeneratedProcess, CrossBlockAutomaticLoopReleasesEveryIteration) {
  ASSERT_NE(automaticLoopDescriptor.execution, nullptr);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(
                automaticLoopDescriptor.execution, &context),
            TALOS_RT_OK);
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(talos_rt_v1_process_instance_create(&automaticLoopDescriptor,
                                                  &instance),
            TALOS_RT_OK);
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, context, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  ASSERT_EQ(action.kind, TALOS_RT_FRAGMENT_SUSPEND);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    EXPECT_TRUE(context->nativeAutomaticStates.empty());
  }
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, context, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(GeneratedProcess, NativeBytecodeNativeReusesFrameAndScratch) {
  DualTierDescriptor dual;
  size_t before = alignedAllocationCount.load();
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(talos_rt_v1_process_instance_create(&dual.descriptor, &instance),
            TALOS_RT_OK);
  ASSERT_LE(alignedAllocationCount.load(), before + 1);
  size_t afterCreate = alignedAllocationCount.load();
  void *frame = instance->frame;
  void *scratch =
      static_cast<uint8_t *>(instance->allocation) + instance->scratch_offset;
  talos_rt_fragment_action_v1 action{};

  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.continuation, 1u);
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_BYTECODE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.continuation, 1u);
  EXPECT_EQ(instance->native_handle, nullptr);
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.continuation, 2u);
  EXPECT_EQ(instance->frame, frame);
  EXPECT_EQ(static_cast<uint8_t *>(instance->allocation) +
                instance->scratch_offset,
            scratch);
  EXPECT_EQ(alignedAllocationCount.load(), afterCreate);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
}

TEST(GeneratedProcess, BytecodeFirstReconstructsNativeContinuation) {
  DualTierDescriptor dual;
  size_t before = alignedAllocationCount.load();
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(talos_rt_v1_process_instance_create(&dual.descriptor, &instance),
            TALOS_RT_OK);
  ASSERT_LE(alignedAllocationCount.load(), before + 1);
  size_t afterCreate = alignedAllocationCount.load();
  initializeDelayWait(instance->frame);
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_BYTECODE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.continuation, 1u);
  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.continuation, 2u);
  EXPECT_EQ(alignedAllocationCount.load(), afterCreate);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
}

TEST(GeneratedProcess, RuntimeFailureReconstructsInsteadOfResumingDoneHandle) {
  size_t before = alignedAllocationCount.load();
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&failingDescriptor, &instance),
      TALOS_RT_OK);
  ASSERT_LE(alignedAllocationCount.load(), before + 1);
  size_t afterCreate = alignedAllocationCount.load();
  void *frame = instance->frame;
  talos_rt_fragment_action_v1 action{};

  ASSERT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  ASSERT_EQ(instance->continuation, 1u);
  ASSERT_NE(instance->native_handle, nullptr);
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(instance->lifecycle, TALOS_RT_PROCESS_SUSPENDED);
  EXPECT_EQ(instance->frame, frame);
  EXPECT_EQ(alignedAllocationCount.load(), afterCreate);

  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(instance->frame, frame);
  EXPECT_EQ(alignedAllocationCount.load(), afterCreate);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
}

TEST(GeneratedProcess, RecyclesSequentialProcessFrameAllocations) {
  size_t before = alignedAllocationCount.load();
  for (unsigned iteration = 0; iteration != 1000; ++iteration) {
    talos_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        talos_rt_v1_process_instance_create(&generatedDescriptor, &instance),
        TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  }
  EXPECT_LE(alignedAllocationCount.load(), before + 1);
}

TEST(GeneratedProcess, RecyclesAcrossSizeAndAlignmentClasses) {
  constexpr std::array<std::pair<uint64_t, uint64_t>, 6> classes{{
      {0, 1},
      {32, 16},
      {200, 64},
      {4096, 256},
      {60000, 4096},
      {500000, 4096},
  }};
  for (const auto &[size, alignment] : classes) {
    talos_rt_process_descriptor_v1 descriptor = generatedDescriptor;
    descriptor.native_requirements = requestedNativeRequirements;
    requestedNativeSize = size;
    requestedNativeAlignment = alignment;
    size_t before = alignedAllocationCount.load();

    talos_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(talos_rt_v1_process_instance_create(&descriptor, &instance),
              TALOS_RT_OK);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(instance) %
                  std::max<uint64_t>(alignment, 16),
              0u);
    if (instance->scratch_size != 0)
      std::memset(static_cast<uint8_t *>(instance->allocation) +
                      instance->scratch_offset,
                  0xa5, static_cast<size_t>(instance->scratch_size));
    ASSERT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);

    ASSERT_EQ(talos_rt_v1_process_instance_create(&descriptor, &instance),
              TALOS_RT_OK);
    const auto *scratch = static_cast<const uint8_t *>(instance->allocation) +
                          instance->scratch_offset;
    EXPECT_TRUE(std::all_of(
        scratch, scratch + static_cast<size_t>(instance->scratch_size),
        [](uint8_t value) { return value == 0; }));
    ASSERT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
    EXPECT_LE(alignedAllocationCount.load(), before + 1);
  }
}

TEST(GeneratedProcess, OversizeProcessFramesBypassTheCache) {
  talos_rt_process_descriptor_v1 descriptor = generatedDescriptor;
  descriptor.native_requirements = requestedNativeRequirements;
  requestedNativeSize = 2 * 1024 * 1024;
  requestedNativeAlignment = 4096;
  size_t before = alignedAllocationCount.load();
  for (unsigned iteration = 0; iteration != 2; ++iteration) {
    talos_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(talos_rt_v1_process_instance_create(&descriptor, &instance),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  }
  EXPECT_EQ(alignedAllocationCount.load(), before + 2);
}

TEST(GeneratedProcess, DestroyUsesFixedAllocationMetadata) {
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&generatedDescriptor, &instance),
      TALOS_RT_OK);
  instance->frame = nullptr;
  instance->scratch_offset = UINT64_MAX;
  instance->scratch_size = UINT64_MAX;
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
}

TEST(GeneratedProcess, ConcurrentProcessFrameChurnUsesThreadLocalCaches) {
  constexpr unsigned workerCount = 8;
  constexpr unsigned iterations = 1000;
  size_t before = alignedAllocationCount.load();
  std::atomic<unsigned> failures{0};
  std::vector<std::thread> workers;
  workers.reserve(workerCount);
  for (unsigned worker = 0; worker != workerCount; ++worker)
    workers.emplace_back([&] {
      for (unsigned iteration = 0; iteration != iterations; ++iteration) {
        talos_rt_process_instance_v1 *instance = nullptr;
        if (talos_rt_v1_process_instance_create(&generatedDescriptor,
                                                  &instance) != TALOS_RT_OK ||
            talos_rt_v1_process_instance_destroy(instance) != TALOS_RT_OK)
          ++failures;
      }
    });
  for (std::thread &worker : workers)
    worker.join();

  EXPECT_EQ(failures.load(), 0u);
  EXPECT_LE(alignedAllocationCount.load(), before + workerCount);
}

TEST(GeneratedProcess, ProcessFramesCanBeFreedByDifferentWorkers) {
  constexpr unsigned instanceCount = 256;
  constexpr unsigned workerCount = 8;
  std::vector<talos_rt_process_instance_v1 *> instances;
  instances.reserve(instanceCount);
  for (unsigned index = 0; index != instanceCount; ++index) {
    talos_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        talos_rt_v1_process_instance_create(&generatedDescriptor, &instance),
        TALOS_RT_OK);
    instances.push_back(instance);
  }

  std::atomic<unsigned> next{0};
  std::atomic<unsigned> failures{0};
  std::vector<std::thread> workers;
  workers.reserve(workerCount);
  for (unsigned worker = 0; worker != workerCount; ++worker)
    workers.emplace_back([&] {
      for (;;) {
        unsigned index = next.fetch_add(1);
        if (index >= instances.size())
          break;
        if (talos_rt_v1_process_instance_destroy(instances[index]) !=
            TALOS_RT_OK)
          ++failures;
      }
    });
  for (std::thread &worker : workers)
    worker.join();
  EXPECT_EQ(failures.load(), 0u);

  size_t beforeReuse = alignedAllocationCount.load();
  instances.clear();
  for (unsigned index = 0; index != 128; ++index) {
    talos_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        talos_rt_v1_process_instance_create(&generatedDescriptor, &instance),
        TALOS_RT_OK);
    instances.push_back(instance);
  }
  EXPECT_EQ(alignedAllocationCount.load(), beforeReuse);
  for (talos_rt_process_instance_v1 *instance : instances)
    ASSERT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
}

} // namespace

#if defined(_WIN32)
// Interpose the CRT import used by the runtime, just as aligned_alloc is
// interposed below on POSIX. Offset zero uses the same _aligned_free contract.
extern "C" void *countedAlignedMalloc(size_t size,
                                      size_t alignment) asm("_aligned_malloc");
extern "C" void *countedAlignedMalloc(size_t size, size_t alignment) {
  void *allocation = _aligned_offset_malloc(size, alignment, 0);
  if (allocation)
    ++alignedAllocationCount;
  return allocation;
}
extern "C" {
void *(*__imp__aligned_malloc)(size_t, size_t) = countedAlignedMalloc;
}

namespace {
class ProcessAllocationEnvironment : public ::testing::Environment {
  void SetUp() override {
    // Windows also uses _aligned_malloc for the pool's over-aligned metadata.
    // Initialize that shared infrastructure before measuring per-frame costs,
    // including when lit runs a single test in a fresh process.
    void *block = talos::process::allocateProcessMemory(128, 16);
    ASSERT_NE(block, nullptr);
    talos::process::releaseProcessMemory(block, 128, 16);
  }
};
::testing::Environment *const processAllocationEnvironment =
    ::testing::AddGlobalTestEnvironment(new ProcessAllocationEnvironment);
} // namespace
#else
extern "C" void *aligned_alloc(size_t alignment, size_t size) {
  void *allocation = nullptr;
  if (posix_memalign(&allocation, alignment, size) != 0)
    return nullptr;
  ++alignedAllocationCount;
  return allocation;
}
#endif
