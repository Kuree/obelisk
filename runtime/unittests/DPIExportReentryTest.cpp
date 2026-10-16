//===- DPIExportReentryTest.cpp - Stable scheduler identity across DPI
//-----===//
#include "../lib/ProcessValidation.h"
#include "obelisk/Runtime/Runtime.h"
#include "gtest/gtest.h"
#include <array>
#include <cstddef>
#include <memory>

namespace {
struct State {
  const obelisk_rt_process_descriptor_v1 *exported = nullptr;
  unsigned unrelatedReturns = 0, exportReturns = 0, importReturns = 0;
};
struct Frame {
  obelisk_rt_wait_record_v1 wait;
  State *state;
};
obelisk_rt_status execute(obelisk_rt_process_instance_v1 *instance) {
  auto &frame = *static_cast<Frame *>(instance->frame);
  auto &state = *frame.state;
  instance->native_handle = instance;
  const uint64_t unit = instance->descriptor->handle.id;
  if (unit == 40) {
    uint64_t source = obelisk_rt_v1_process_current(instance->context);
    obelisk_rt_process_instance_v1 *exported = nullptr;
    auto status =
        obelisk_rt_v1_process_instance_create(state.exported, &exported);
    if (status != OBELISK_RT_OK)
      return status;
    static_cast<Frame *>(exported->frame)->state = &state;
    status = obelisk_rt_v1_dpi_export_task_run(instance->context, exported);
    if (status != OBELISK_RT_OK)
      return status;
    EXPECT_EQ(obelisk_rt_v1_process_current(instance->context), source);
    EXPECT_EQ(state.unrelatedReturns, 1u);
    EXPECT_EQ(state.exportReturns, 1u);
    ++state.importReturns;
  } else if (!instance->continuation) {
    frame.wait = {OBELISK_RT_VERSION,
                  OBELISK_RT_SUSPEND_DELAY,
                  0,
                  0,
                  unit == 41 ? 1u : 2u,
                  0};
    *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                         OBELISK_RT_SUSPEND_DELAY,
                         1,
                         OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                         0,
                         sizeof(frame.wait)};
    return OBELISK_RT_OK;
  } else if (unit == 41) {
    ++state.unrelatedReturns;
  } else {
    EXPECT_EQ(unit, 42u);
    ++state.exportReturns;
  }
  *instance->action = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return OBELISK_RT_OK;
}
TEST(Process, DPIExportReentryPreservesCompactedCallerIdentity) {
  const std::array<obelisk_rt_frame_field_v1, 2> fields{
      {{OBELISK_RT_FRAME_WAIT, 0, 0, sizeof(obelisk_rt_wait_record_v1),
        alignof(obelisk_rt_wait_record_v1), 0},
       {OBELISK_RT_FRAME_CAPTURE, 0, offsetof(Frame, state), sizeof(State *),
        alignof(State *), 0}}};
  const uint32_t continuations[] = {0, 1};
  obelisk_rt_frame_layout_v1 layout{OBELISK_RT_VERSION,
                                    0,
                                    sizeof(Frame),
                                    alignof(Frame),
                                    fields.data(),
                                    uint32_t(fields.size()),
                                    2,
                                    continuations,
                                    0};
  layout.checksum = obelisk::process::layoutChecksum(layout);
  std::array<obelisk_rt_process_descriptor_v1, 3> descriptors{};
  for (unsigned i = 0; i != descriptors.size(); ++i)
    descriptors[i] = {
        {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 40 + i},
        OBELISK_RT_VERSION,
        0,
        OBELISK_RT_TIER_MASK_NATIVE,
        0,
        &layout,
        [](uint64_t *size, uint64_t *alignment) {
          *size = 0;
          *alignment = 1;
          return OBELISK_RT_OK;
        },
        execute,
        [](obelisk_rt_process_instance_v1 *p) { p->native_handle = nullptr; },
        nullptr,
        nullptr,
        nullptr};
  obelisk_rt_context *raw = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&raw), OBELISK_RT_OK);
  std::unique_ptr<obelisk_rt_context, decltype(&obelisk_rt_v1_context_destroy)>
      context(raw, obelisk_rt_v1_context_destroy);
  State state{&descriptors[2]};
  // The earlier unrelated process terminates during the nested export and
  // compacts the importing process to a different scheduler-vector index.
  for (unsigned index : {1u, 0u}) {
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_create(&descriptors[index], &instance),
        OBELISK_RT_OK);
    static_cast<Frame *>(instance->frame)->state = &state;
    ASSERT_EQ(obelisk_rt_v1_scheduler_add(raw, instance, 0), OBELISK_RT_OK);
  }
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(raw), OBELISK_RT_OK);
  EXPECT_EQ(state.importReturns, 1u);
  EXPECT_EQ(state.exportReturns, 1u);
  EXPECT_EQ(state.unrelatedReturns, 1u);
}
} // namespace
