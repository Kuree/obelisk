//===- GeneratedProcessTest.cpp - Execute compiler coroutine output -------===//

#include "obelisk/Runtime/Runtime.h"

#include "../lib/RuntimeInternal.h"

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

extern "C" const obelisk_rt_process_descriptor_v1
    generatedDescriptor asm("execution_process.__obelisk_process_descriptor");
extern "C" const obelisk_rt_process_descriptor_v1
    failingDescriptor asm("failing_process.__obelisk_process_descriptor");
extern "C" const obelisk_rt_process_descriptor_v1 orchestrationDescriptor
    asm("orchestration_process.__obelisk_process_descriptor");
extern "C" const obelisk_rt_process_descriptor_v1 automaticDescriptor
    asm("automatic_process.__obelisk_process_descriptor");
extern "C" const obelisk_rt_process_descriptor_v1 automaticLoopDescriptor
    asm("automatic_loop_process.__obelisk_process_descriptor");

namespace {

std::atomic<size_t> alignedAllocationCount;
uint64_t requestedNativeSize;
uint64_t requestedNativeAlignment;

obelisk_rt_status requestedNativeRequirements(uint64_t *size,
                                              uint64_t *alignment) {
  if (!size || !alignment)
    return OBELISK_RT_INVALID_ARGUMENT;
  *size = requestedNativeSize;
  *alignment = requestedNativeAlignment;
  return OBELISK_RT_OK;
}

void appendInstruction(std::array<uint8_t, 64> &code, size_t instruction,
                       uint8_t opcode, uint8_t type, uint16_t destination,
                       uint16_t source0, uint16_t source1, uint64_t immediate) {
  size_t offset = instruction * OBELISK_RT_BYTECODE_INSTRUCTION_SIZE;
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

const obelisk_rt_frame_field_v1 *findWaitField() {
  const obelisk_rt_frame_layout_v1 &layout = *generatedDescriptor.frame_layout;
  for (uint32_t index = 0; index != layout.field_count; ++index)
    if (layout.fields[index].kind == OBELISK_RT_FRAME_WAIT)
      return &layout.fields[index];
  return nullptr;
}

void initializeDelayWait(void *frame) {
  const obelisk_rt_frame_field_v1 *field = findWaitField();
  ASSERT_NE(field, nullptr);
  auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(
      static_cast<uint8_t *>(frame) + field->offset);
  *wait = {
      OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_DELAY, 0, 0, 3, 0};
}

struct DualTierDescriptor {
  std::array<uint8_t, 64> code{};
  std::array<obelisk_rt_bytecode_entry_v1, 2> entries{{{0, 0}, {1, 2}}};
  obelisk_rt_bytecode_v1 bytecode{};
  obelisk_rt_process_descriptor_v1 descriptor{};

  DualTierDescriptor() {
    const obelisk_rt_frame_field_v1 *wait = findWaitField();
    EXPECT_NE(wait, nullptr);
    appendInstruction(code, 0, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0,
                      0, 0, wait->offset);
    appendInstruction(code, 1, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE,
                      0, OBELISK_RT_SUSPEND_DELAY, 0, 1);
    appendInstruction(code, 2, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0,
                      0, 0, wait->offset);
    appendInstruction(code, 3, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE,
                      0, OBELISK_RT_SUSPEND_DELAY, 0, 1);

    uint64_t nativeSize = 0;
    uint64_t nativeAlignment = 0;
    EXPECT_EQ(
        generatedDescriptor.native_requirements(&nativeSize, &nativeAlignment),
        OBELISK_RT_OK);
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
        OBELISK_RT_TIER_MASK_NATIVE | OBELISK_RT_TIER_MASK_BYTECODE;
    descriptor.bytecode = &bytecode;
    descriptor.design_bytecode = nullptr;
  }
};

TEST(GeneratedProcess, NativeResumeTerminateAndSuspendedDestroy) {
  size_t before = alignedAllocationCount.load();
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&generatedDescriptor, &instance),
      OBELISK_RT_OK);
  ASSERT_LE(alignedAllocationCount.load(), before + 1);
  size_t afterCreate = alignedAllocationCount.load();
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);

  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(action.continuation, 1u);
  EXPECT_EQ(action.suspend_kind, OBELISK_RT_SUSPEND_DELAY);
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(action.continuation, 2u);
  EXPECT_EQ(alignedAllocationCount.load(), afterCreate);

  void *sameFrame = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_frame(instance, &sameFrame, &frameSize),
      OBELISK_RT_OK);
  EXPECT_EQ(sameFrame, frame);
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(instance->lifecycle, OBELISK_RT_PROCESS_TERMINATED);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);

  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&generatedDescriptor, &instance),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(instance->lifecycle, OBELISK_RT_PROCESS_SUSPENDED);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
}

TEST(GeneratedProcess, EmittedDesignBytecodeMatchesNativeLifecycle) {
  ASSERT_EQ(generatedDescriptor.available_tiers,
            OBELISK_RT_TIER_MASK_NATIVE | OBELISK_RT_TIER_MASK_BYTECODE);
  ASSERT_NE(generatedDescriptor.execution, nullptr);
  ASSERT_NE(generatedDescriptor.design_bytecode, nullptr);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(
                generatedDescriptor.execution, &context),
            OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *native = nullptr;
  obelisk_rt_process_instance_v1 *bytecode = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&generatedDescriptor,
                                                   &native),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&generatedDescriptor,
                                                   &bytecode),
            OBELISK_RT_OK);

  for (unsigned step = 0; step != 3; ++step) {
    obelisk_rt_fragment_action_v1 nativeAction{}, bytecodeAction{};
    ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                  native, context, OBELISK_RT_TIER_NATIVE, &nativeAction),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                  bytecode, context, OBELISK_RT_TIER_BYTECODE,
                  &bytecodeAction),
              OBELISK_RT_OK);
    EXPECT_EQ(bytecodeAction.kind, nativeAction.kind);
    EXPECT_EQ(bytecodeAction.suspend_kind, nativeAction.suspend_kind);
    EXPECT_EQ(bytecodeAction.continuation, nativeAction.continuation);
    EXPECT_EQ(bytecodeAction.flags, nativeAction.flags);
    EXPECT_EQ(bytecodeAction.payload, nativeAction.payload);
    EXPECT_EQ(bytecodeAction.auxiliary, nativeAction.auxiliary);
  }
  EXPECT_EQ(native->lifecycle, OBELISK_RT_PROCESS_TERMINATED);
  EXPECT_EQ(bytecode->lifecycle, OBELISK_RT_PROCESS_TERMINATED);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(native), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(bytecode), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(GeneratedProcess, SchedulerRunsEventSpawnJoinAndAwait) {
  ASSERT_NE(orchestrationDescriptor.execution, nullptr);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(
                orchestrationDescriptor.execution, &context),
            OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&orchestrationDescriptor,
                                                   &instance),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  testing::internal::CaptureStdout();
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(testing::internal::GetCapturedStdout(),
            "join-short\njoin-long\njoined\nawait-child\nawaited\n");
  obelisk_rt_v1_context_destroy(context);
}

TEST(GeneratedProcess, DynamicAutomaticSurvivesCallSpawnAndDelayedNBA) {
  ASSERT_NE(automaticDescriptor.execution, nullptr);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(
                automaticDescriptor.execution, &context),
            OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&automaticDescriptor,
                                                   &instance),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(GeneratedProcess, CrossBlockAutomaticLoopReleasesEveryIteration) {
  ASSERT_NE(automaticLoopDescriptor.execution, nullptr);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(
                automaticLoopDescriptor.execution, &context),
            OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&automaticLoopDescriptor,
                                                   &instance),
            OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_SUSPEND);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    EXPECT_TRUE(context->nativeAutomaticStates.empty());
  }
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(GeneratedProcess, NativeBytecodeNativeReusesFrameAndScratch) {
  DualTierDescriptor dual;
  size_t before = alignedAllocationCount.load();
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&dual.descriptor, &instance),
            OBELISK_RT_OK);
  ASSERT_LE(alignedAllocationCount.load(), before + 1);
  size_t afterCreate = alignedAllocationCount.load();
  void *frame = instance->frame;
  void *scratch =
      static_cast<uint8_t *>(instance->allocation) + instance->scratch_offset;
  obelisk_rt_fragment_action_v1 action{};

  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.continuation, 1u);
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.continuation, 1u);
  EXPECT_EQ(instance->native_handle, nullptr);
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.continuation, 2u);
  EXPECT_EQ(instance->frame, frame);
  EXPECT_EQ(static_cast<uint8_t *>(instance->allocation) +
                instance->scratch_offset,
            scratch);
  EXPECT_EQ(alignedAllocationCount.load(), afterCreate);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
}

TEST(GeneratedProcess, BytecodeFirstReconstructsNativeContinuation) {
  DualTierDescriptor dual;
  size_t before = alignedAllocationCount.load();
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&dual.descriptor, &instance),
            OBELISK_RT_OK);
  ASSERT_LE(alignedAllocationCount.load(), before + 1);
  size_t afterCreate = alignedAllocationCount.load();
  initializeDelayWait(instance->frame);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.continuation, 1u);
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.continuation, 2u);
  EXPECT_EQ(alignedAllocationCount.load(), afterCreate);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
}

TEST(GeneratedProcess, RuntimeFailureReconstructsInsteadOfResumingDoneHandle) {
  size_t before = alignedAllocationCount.load();
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&failingDescriptor, &instance),
      OBELISK_RT_OK);
  ASSERT_LE(alignedAllocationCount.load(), before + 1);
  size_t afterCreate = alignedAllocationCount.load();
  void *frame = instance->frame;
  obelisk_rt_fragment_action_v1 action{};

  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(instance->continuation, 1u);
  ASSERT_NE(instance->native_handle, nullptr);
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(instance->lifecycle, OBELISK_RT_PROCESS_SUSPENDED);
  EXPECT_EQ(instance->frame, frame);
  EXPECT_EQ(alignedAllocationCount.load(), afterCreate);

  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(instance->continuation, 1u);
  EXPECT_EQ(instance->frame, frame);
  EXPECT_EQ(alignedAllocationCount.load(), afterCreate);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
}

TEST(GeneratedProcess, RecyclesSequentialProcessFrameAllocations) {
  size_t before = alignedAllocationCount.load();
  for (unsigned iteration = 0; iteration != 1000; ++iteration) {
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_create(&generatedDescriptor, &instance),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
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
    obelisk_rt_process_descriptor_v1 descriptor = generatedDescriptor;
    descriptor.native_requirements = requestedNativeRequirements;
    requestedNativeSize = size;
    requestedNativeAlignment = alignment;
    size_t before = alignedAllocationCount.load();

    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(obelisk_rt_v1_process_instance_create(&descriptor, &instance),
              OBELISK_RT_OK);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(instance) %
                  std::max<uint64_t>(alignment, 16),
              0u);
    if (instance->scratch_size != 0)
      std::memset(static_cast<uint8_t *>(instance->allocation) +
                      instance->scratch_offset,
                  0xa5, static_cast<size_t>(instance->scratch_size));
    ASSERT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);

    ASSERT_EQ(obelisk_rt_v1_process_instance_create(&descriptor, &instance),
              OBELISK_RT_OK);
    const auto *scratch = static_cast<const uint8_t *>(instance->allocation) +
                          instance->scratch_offset;
    EXPECT_TRUE(std::all_of(
        scratch, scratch + static_cast<size_t>(instance->scratch_size),
        [](uint8_t value) { return value == 0; }));
    ASSERT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
    EXPECT_LE(alignedAllocationCount.load(), before + 1);
  }
}

TEST(GeneratedProcess, OversizeProcessFramesBypassTheCache) {
  obelisk_rt_process_descriptor_v1 descriptor = generatedDescriptor;
  descriptor.native_requirements = requestedNativeRequirements;
  requestedNativeSize = 2 * 1024 * 1024;
  requestedNativeAlignment = 4096;
  size_t before = alignedAllocationCount.load();
  for (unsigned iteration = 0; iteration != 2; ++iteration) {
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(obelisk_rt_v1_process_instance_create(&descriptor, &instance),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  }
  EXPECT_EQ(alignedAllocationCount.load(), before + 2);
}

TEST(GeneratedProcess, DestroyUsesFixedAllocationMetadata) {
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&generatedDescriptor, &instance),
      OBELISK_RT_OK);
  instance->frame = nullptr;
  instance->scratch_offset = UINT64_MAX;
  instance->scratch_size = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
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
        obelisk_rt_process_instance_v1 *instance = nullptr;
        if (obelisk_rt_v1_process_instance_create(&generatedDescriptor,
                                                  &instance) != OBELISK_RT_OK ||
            obelisk_rt_v1_process_instance_destroy(instance) != OBELISK_RT_OK)
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
  std::vector<obelisk_rt_process_instance_v1 *> instances;
  instances.reserve(instanceCount);
  for (unsigned index = 0; index != instanceCount; ++index) {
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_create(&generatedDescriptor, &instance),
        OBELISK_RT_OK);
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
        if (obelisk_rt_v1_process_instance_destroy(instances[index]) !=
            OBELISK_RT_OK)
          ++failures;
      }
    });
  for (std::thread &worker : workers)
    worker.join();
  EXPECT_EQ(failures.load(), 0u);

  size_t beforeReuse = alignedAllocationCount.load();
  instances.clear();
  for (unsigned index = 0; index != 128; ++index) {
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_create(&generatedDescriptor, &instance),
        OBELISK_RT_OK);
    instances.push_back(instance);
  }
  EXPECT_EQ(alignedAllocationCount.load(), beforeReuse);
  for (obelisk_rt_process_instance_v1 *instance : instances)
    ASSERT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
}

} // namespace

extern "C" void *aligned_alloc(size_t alignment, size_t size) {
  void *allocation = nullptr;
  if (posix_memalign(&allocation, alignment, size) != 0)
    return nullptr;
  ++alignedAllocationCount;
  return allocation;
}
