//===- TableProcessTest.cpp - Native activation table ABI tests -----------===//
#include "../lib/ProcessValidation.h"
#include "talos/Runtime/Runtime.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>

namespace {
uint32_t selectedWait, observedEntry, entryCount;
talos_rt_status entryStatus;
uint32_t executeEntry(talos_rt_process_instance_v1 *instance,
                      uint32_t entry) {
  observedEntry = entry;
  ++entryCount;
  instance->status = entryStatus;
  return selectedWait;
}
struct TableFixture {
  std::array<talos_rt_frame_field_v1, 2> fields{
      {{TALOS_RT_FRAME_CAPTURE, 0, 0, 8, 8, 0},
       {TALOS_RT_FRAME_WAIT, 0, 8, 48, 8, 0}}};
  std::array<uint32_t, 3> continuations{{0, 9, 42}};
  talos_rt_frame_layout_v1 layout{
      TALOS_RT_VERSION,   0, 56, 8, fields.data(), 2, 3,
      continuations.data(), 0};
  talos_rt_table_watch_v1 watch{0, TALOS_RT_WAIT_EDGE_POSEDGE, 65};
  talos_rt_table_wait_v1 wait{
      9,
      TALOS_RT_ACTION_RESUME_REGION(TALOS_RT_REGION_REACTIVE),
      8,
      48,
      {TALOS_RT_VERSION, TALOS_RT_SUSPEND_EDGE,
       TALOS_RT_WAIT_SUPPRESS_ACTIVE_SELF, 1, 0, 0},
      &watch};
  talos_rt_table_process_plan_v1 plan{TALOS_RT_VERSION, 1, executeEntry,
                                        &wait};
  talos_rt_table_process_descriptor_v1 descriptor{};
  TableFixture() {
    layout.checksum = talos::process::layoutChecksum(layout);
    descriptor.base.handle = {TALOS_RT_DESCRIPTOR_PROCESS, 0, 1};
    descriptor.base.version = TALOS_RT_VERSION;
    descriptor.base.flags =
        TALOS_RT_PROCESS_UNMANAGED_NATIVE | TALOS_RT_PROCESS_TABLE_NATIVE;
    descriptor.base.available_tiers = TALOS_RT_TIER_MASK_NATIVE;
    descriptor.base.frame_layout = &layout;
    descriptor.base.native_requirements = [](uint64_t *size, uint64_t *align) {
      *size = 0;
      *align = 1;
      return TALOS_RT_OK;
    };
    descriptor.base.native_execute = talos_rt_v1_table_process_execute;
    descriptor.base.native_destroy = [](talos_rt_process_instance_v1 *) {};
    descriptor.plan = &plan;
    selectedWait = 0;
    entryStatus = TALOS_RT_OK;
    entryCount = 0;
  }
};

TEST(TableProcess, DispatchesSparseContinuationsAndPublishesCanonicalWait) {
  TableFixture fixture;
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(talos_rt_v1_process_instance_create(&fixture.descriptor.base,
                                                  &instance),
            TALOS_RT_OK);
  uint64_t handle = talos_rt_v1_native_state_static_handle(1);
  std::memcpy(instance->frame, &handle, sizeof(handle));
  for (auto [continuation, index] :
       std::array<std::pair<uint32_t, uint32_t>, 3>{
           {{0, 0}, {9, 1}, {42, 2}}}) {
    instance->continuation = continuation;
    talos_rt_fragment_action_v1 action{};
    ASSERT_EQ(talos_rt_v1_process_instance_execute(
                  instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
              TALOS_RT_OK);
    EXPECT_EQ(observedEntry, index);
    EXPECT_EQ(instance->native_handle, nullptr);
    EXPECT_EQ(action.continuation, 9u);
    EXPECT_EQ(action.payload, 8u);
    EXPECT_EQ(action.auxiliary, 48u);
    EXPECT_EQ(action.flags,
              TALOS_RT_ACTION_FRAME_WAIT_RECORD | fixture.wait.action_flags);
    const auto *record = reinterpret_cast<const talos_rt_wait_record_v1 *>(
        static_cast<const uint8_t *>(instance->frame) + 8);
    EXPECT_EQ(std::memcmp(record, &fixture.wait.record, sizeof(*record)), 0);
    const auto *watch =
        reinterpret_cast<const talos_rt_wait_entry_v1 *>(record + 1);
    EXPECT_EQ(watch->stable_id, handle);
    EXPECT_EQ(watch->edge, TALOS_RT_WAIT_EDGE_POSEDGE);
    EXPECT_EQ(watch->reserved, 65u);
  }
  selectedWait = TALOS_RT_TABLE_TERMINATE;
  talos_rt_fragment_action_v1 action{};
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
}

TEST(TableProcess, RejectsMalformedPlansBeforeAllocatingOrExecuting) {
  for (unsigned fault = 0; fault != 10; ++fault) {
    TableFixture fixture;
    switch (fault) {
    case 0:
      fixture.descriptor.plan = nullptr;
      break;
    case 1:
      fixture.plan.version = 0;
      break;
    case 2:
      fixture.plan.entry = nullptr;
      break;
    case 3:
      fixture.plan.wait_count = 0;
      break;
    case 4:
      fixture.wait.frame_offset = 16;
      break;
    case 5:
      fixture.wait.continuation = 8;
      break;
    case 6:
      fixture.watch.capture_offset = 8;
      break;
    case 7:
      fixture.watch.width = 0;
      break;
    case 8:
      fixture.wait.record.count = 2;
      break;
    case 9:
      fixture.wait.action_flags =
          TALOS_RT_ACTION_RESUME_REGION(TALOS_RT_REGION_NBA);
      break;
    }
    talos_rt_process_instance_v1 *instance = nullptr;
    EXPECT_EQ(talos_rt_v1_process_instance_create(&fixture.descriptor.base,
                                                    &instance),
              TALOS_RT_LAYOUT_MISMATCH)
        << fault;
    EXPECT_EQ(instance, nullptr);
  }
}

TEST(TableProcess, EntryErrorsAndInvalidExitDoNotPublishWaits) {
  TableFixture fixture;
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(talos_rt_v1_process_instance_create(&fixture.descriptor.base,
                                                  &instance),
            TALOS_RT_OK);
  talos_rt_fragment_action_v1 action{};
  entryStatus = TALOS_RT_INVALID_ARGUMENT;
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(instance->continuation, 0u);
  entryStatus = TALOS_RT_OK;
  selectedWait = 99;
  EXPECT_EQ(talos_rt_v1_process_instance_execute(
                instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
            TALOS_RT_INVALID_CONTINUATION);
  EXPECT_EQ(instance->continuation, 0u);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
}

TEST(TableProcess, ResumeForgetsEventsMissedWhileSuspended) {
  // IEEE 1800-2023 9.7: suspend() desensitizes a WAITING process and resume()
  // resensitizes it, so an edge that occurred while it was suspended does not
  // resume it; the next edge does.
  TableFixture fixture;
  fixture.watch.width = 1;
  fixture.wait.action_flags = 0;
  fixture.wait.record.flags = 0;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(talos_rt_v1_process_instance_create(&fixture.descriptor.base,
                                                  &instance),
            TALOS_RT_OK);
  uint64_t handle = 16;
  std::memcpy(instance->frame, &handle, sizeof(handle));
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  uint64_t token = talos_rt_v1_scheduler_process_token(context, instance);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_EQ(entryCount, 1u);
  talos_rt_process_control_disposition disposition{};
  ASSERT_EQ(talos_rt_v1_process_control(context, token,
                                          TALOS_RT_PROCESS_CONTROL_SUSPEND,
                                          &disposition),
            TALOS_RT_OK);
  talos_rt_v1_scheduler_signal(
      context, handle, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_process_control(context, token,
                                          TALOS_RT_PROCESS_CONTROL_RESUME,
                                          &disposition),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(entryCount, 1u);
  talos_rt_v1_scheduler_signal(
      context, handle, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(entryCount, 2u);
  talos_rt_v1_context_destroy(context);
}

TEST(TableProcess, ExternalSuspendResumeAndKillRetainLogicalIdentity) {
  TableFixture fixture;
  fixture.watch.width = 1;
  fixture.wait.action_flags = 0;
  fixture.wait.record.flags = 0;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(talos_rt_v1_process_instance_create(&fixture.descriptor.base,
                                                  &instance),
            TALOS_RT_OK);
  uint64_t handle = 16;
  std::memcpy(instance->frame, &handle, sizeof(handle));
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  uint64_t token = talos_rt_v1_scheduler_process_token(context, instance);
  ASSERT_NE(token, 0u);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  ASSERT_EQ(entryCount, 1u);
  talos_rt_process_control_disposition disposition{};
  ASSERT_EQ(talos_rt_v1_process_control(context, token,
                                          TALOS_RT_PROCESS_CONTROL_SUSPEND,
                                          &disposition),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(entryCount, 1u);
  ASSERT_EQ(talos_rt_v1_process_control(context, token,
                                          TALOS_RT_PROCESS_CONTROL_RESUME,
                                          &disposition),
            TALOS_RT_OK);
  talos_rt_v1_scheduler_signal(
      context, handle, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(entryCount, 2u);
  EXPECT_EQ(observedEntry, 1u);
  EXPECT_EQ(talos_rt_v1_scheduler_process_token(context, instance), token);
  ASSERT_EQ(talos_rt_v1_process_control(
                context, token, TALOS_RT_PROCESS_CONTROL_KILL, &disposition),
            TALOS_RT_OK);
  talos_rt_v1_scheduler_signal(
      context, handle, 1, TALOS_RT_SIGNAL_CHANGE | TALOS_RT_SIGNAL_POSEDGE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  EXPECT_EQ(entryCount, 2u);
  talos_rt_process_state state{};
  ASSERT_EQ(talos_rt_v1_process_status(context, token, &state),
            TALOS_RT_OK);
  EXPECT_EQ(state, TALOS_RT_PROCESS_KILLED);
  talos_rt_v1_context_destroy(context);
}
} // namespace
