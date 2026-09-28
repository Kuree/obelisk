//===- TableProcessTest.cpp - Native activation table ABI tests -----------===//
#include "../lib/ProcessValidation.h"
#include "obelisk/Runtime/Runtime.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>

namespace {
uint32_t selectedWait, observedEntry, entryCount;
obelisk_rt_status entryStatus;
uint32_t executeEntry(obelisk_rt_process_instance_v1 *instance,
                      uint32_t entry) {
  observedEntry = entry;
  ++entryCount;
  instance->status = entryStatus;
  return selectedWait;
}
struct TableFixture {
  std::array<obelisk_rt_frame_field_v1, 2> fields{
      {{OBELISK_RT_FRAME_CAPTURE, 0, 0, 8, 8, 0},
       {OBELISK_RT_FRAME_WAIT, 0, 8, 48, 8, 0}}};
  std::array<uint32_t, 3> continuations{{0, 9, 42}};
  obelisk_rt_frame_layout_v1 layout{
      OBELISK_RT_VERSION,   0, 56, 8, fields.data(), 2, 3,
      continuations.data(), 0};
  obelisk_rt_table_watch_v1 watch{0, OBELISK_RT_WAIT_EDGE_POSEDGE, 65};
  obelisk_rt_table_wait_v1 wait{
      9,
      OBELISK_RT_ACTION_RESUME_REGION(OBELISK_RT_REGION_REACTIVE),
      8,
      48,
      {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
       OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF, 1, 0, 0},
      &watch};
  obelisk_rt_table_process_plan_v1 plan{OBELISK_RT_VERSION, 1, executeEntry,
                                        &wait};
  obelisk_rt_table_process_descriptor_v1 descriptor{};
  TableFixture() {
    layout.checksum = obelisk::process::layoutChecksum(layout);
    descriptor.base.handle = {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 1};
    descriptor.base.version = OBELISK_RT_VERSION;
    descriptor.base.flags =
        OBELISK_RT_PROCESS_UNMANAGED_NATIVE | OBELISK_RT_PROCESS_TABLE_NATIVE;
    descriptor.base.available_tiers = OBELISK_RT_TIER_MASK_NATIVE;
    descriptor.base.frame_layout = &layout;
    descriptor.base.native_requirements = [](uint64_t *size, uint64_t *align) {
      *size = 0;
      *align = 1;
      return OBELISK_RT_OK;
    };
    descriptor.base.native_execute = obelisk_rt_v1_table_process_execute;
    descriptor.base.native_destroy = [](obelisk_rt_process_instance_v1 *) {};
    descriptor.plan = &plan;
    selectedWait = 0;
    entryStatus = OBELISK_RT_OK;
    entryCount = 0;
  }
};

TEST(TableProcess, DispatchesSparseContinuationsAndPublishesCanonicalWait) {
  TableFixture fixture;
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&fixture.descriptor.base,
                                                  &instance),
            OBELISK_RT_OK);
  uint64_t handle = obelisk_rt_v1_native_state_static_handle(1);
  std::memcpy(instance->frame, &handle, sizeof(handle));
  for (auto [continuation, index] :
       std::array<std::pair<uint32_t, uint32_t>, 3>{
           {{0, 0}, {9, 1}, {42, 2}}}) {
    instance->continuation = continuation;
    obelisk_rt_fragment_action_v1 action{};
    ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                  instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
              OBELISK_RT_OK);
    EXPECT_EQ(observedEntry, index);
    EXPECT_EQ(instance->native_handle, nullptr);
    EXPECT_EQ(action.continuation, 9u);
    EXPECT_EQ(action.payload, 8u);
    EXPECT_EQ(action.auxiliary, 48u);
    EXPECT_EQ(action.flags,
              OBELISK_RT_ACTION_FRAME_WAIT_RECORD | fixture.wait.action_flags);
    const auto *record = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
        static_cast<const uint8_t *>(instance->frame) + 8);
    EXPECT_EQ(std::memcmp(record, &fixture.wait.record, sizeof(*record)), 0);
    const auto *watch =
        reinterpret_cast<const obelisk_rt_wait_entry_v1 *>(record + 1);
    EXPECT_EQ(watch->stable_id, handle);
    EXPECT_EQ(watch->edge, OBELISK_RT_WAIT_EDGE_POSEDGE);
    EXPECT_EQ(watch->reserved, 65u);
  }
  selectedWait = OBELISK_RT_TABLE_TERMINATE;
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
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
          OBELISK_RT_ACTION_RESUME_REGION(OBELISK_RT_REGION_NBA);
      break;
    }
    obelisk_rt_process_instance_v1 *instance = nullptr;
    EXPECT_EQ(obelisk_rt_v1_process_instance_create(&fixture.descriptor.base,
                                                    &instance),
              OBELISK_RT_LAYOUT_MISMATCH)
        << fault;
    EXPECT_EQ(instance, nullptr);
  }
}

TEST(TableProcess, EntryErrorsAndInvalidExitDoNotPublishWaits) {
  TableFixture fixture;
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&fixture.descriptor.base,
                                                  &instance),
            OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  entryStatus = OBELISK_RT_INVALID_ARGUMENT;
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(instance->continuation, 0u);
  entryStatus = OBELISK_RT_OK;
  selectedWait = 99;
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
            OBELISK_RT_INVALID_CONTINUATION);
  EXPECT_EQ(instance->continuation, 0u);
  EXPECT_EQ(instance->native_handle, nullptr);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
}

TEST(TableProcess, ExternalSuspendResumeAndKillRetainLogicalIdentity) {
  TableFixture fixture;
  fixture.watch.width = 1;
  fixture.wait.action_flags = 0;
  fixture.wait.record.flags = 0;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&fixture.descriptor.base,
                                                  &instance),
            OBELISK_RT_OK);
  uint64_t handle = 16;
  std::memcpy(instance->frame, &handle, sizeof(handle));
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  uint64_t token = obelisk_rt_v1_scheduler_process_token(context, instance);
  ASSERT_NE(token, 0u);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(entryCount, 1u);
  obelisk_rt_process_control_disposition disposition{};
  ASSERT_EQ(obelisk_rt_v1_process_control(context, token,
                                          OBELISK_RT_PROCESS_CONTROL_SUSPEND,
                                          &disposition),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(entryCount, 1u);
  ASSERT_EQ(obelisk_rt_v1_process_control(context, token,
                                          OBELISK_RT_PROCESS_CONTROL_RESUME,
                                          &disposition),
            OBELISK_RT_OK);
  obelisk_rt_v1_scheduler_signal(
      context, handle, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(entryCount, 2u);
  EXPECT_EQ(observedEntry, 1u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_process_token(context, instance), token);
  ASSERT_EQ(obelisk_rt_v1_process_control(
                context, token, OBELISK_RT_PROCESS_CONTROL_KILL, &disposition),
            OBELISK_RT_OK);
  obelisk_rt_v1_scheduler_signal(
      context, handle, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(entryCount, 2u);
  obelisk_rt_process_state state{};
  ASSERT_EQ(obelisk_rt_v1_process_status(context, token, &state),
            OBELISK_RT_OK);
  EXPECT_EQ(state, OBELISK_RT_PROCESS_KILLED);
  obelisk_rt_v1_context_destroy(context);
}
} // namespace
