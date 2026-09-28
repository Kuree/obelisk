//===- ProcessTable.cpp - Coroutine-free native process dispatch ----------===//

#include "ProcessValidation.h"
#include "RuntimeInternal.h"
#include <algorithm>
#include <cstring>

namespace obelisk::process {
obelisk_rt_status
validateTableProcess(const obelisk_rt_process_descriptor_v1 &descriptor) {
  if (!(descriptor.flags & OBELISK_RT_PROCESS_UNMANAGED_NATIVE) ||
      !(descriptor.available_tiers & OBELISK_RT_TIER_MASK_NATIVE) ||
      descriptor.native_execute != obelisk_rt_v1_table_process_execute)
    return OBELISK_RT_LAYOUT_MISMATCH;
  const auto *plan =
      reinterpret_cast<const obelisk_rt_table_process_descriptor_v1 *>(
          &descriptor)
          ->plan;
  if (!plan || plan->version != OBELISK_RT_VERSION || !plan->entry ||
      !plan->wait_count || !plan->waits)
    return OBELISK_RT_LAYOUT_MISMATCH;
  const auto &layout = *descriptor.frame_layout;
  for (uint32_t i = 0; i < layout.field_count; ++i)
    if ((layout.fields[i].kind != OBELISK_RT_FRAME_CAPTURE &&
         layout.fields[i].kind != OBELISK_RT_FRAME_WAIT) ||
        (layout.fields[i].flags &
         (OBELISK_RT_FRAME_MANAGED_ROOT | OBELISK_RT_FRAME_CANDIDATE_ROOT)))
      return OBELISK_RT_LAYOUT_MISMATCH;
  for (uint32_t i = 0; i < plan->wait_count; ++i) {
    const auto &wait = plan->waits[i];
    const auto &record = wait.record;
    const auto *field = findWaitField(layout, wait.frame_offset);
    constexpr uint32_t resumeFlags = OBELISK_RT_ACTION_RESUME_REGION_VALID |
                                     OBELISK_RT_ACTION_RESUME_REGION_MASK;
    uint32_t region =
        (wait.action_flags & OBELISK_RT_ACTION_RESUME_REGION_MASK) >>
        OBELISK_RT_ACTION_RESUME_REGION_SHIFT;
    if (!field || field->size != wait.frame_size ||
        !validContinuation(layout, wait.continuation) ||
        (wait.action_flags & ~resumeFlags) ||
        ((wait.action_flags & OBELISK_RT_ACTION_RESUME_REGION_MASK) &&
         !(wait.action_flags & OBELISK_RT_ACTION_RESUME_REGION_VALID)) ||
        ((wait.action_flags & OBELISK_RT_ACTION_RESUME_REGION_VALID) &&
         !obelisk_rt_is_process_home_region(region)) ||
        record.version != OBELISK_RT_VERSION ||
        (record.kind != OBELISK_RT_SUSPEND_CHANGE &&
         record.kind != OBELISK_RT_SUSPEND_EDGE) ||
        (record.kind == OBELISK_RT_SUSPEND_CHANGE && record.count != 1) ||
        (record.flags & ~OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF) ||
        record.payload || record.auxiliary || !record.count || !wait.watches ||
        uint64_t(record.count) * sizeof(obelisk_rt_wait_entry_v1) +
                sizeof(obelisk_rt_wait_record_v1) >
            wait.frame_size)
      return OBELISK_RT_LAYOUT_MISMATCH;
    for (uint32_t j = 0; j < record.count; ++j) {
      const auto &watch = wait.watches[j];
      if (!watch.width || watch.edge > OBELISK_RT_WAIT_EDGE_BOTH ||
          (record.kind == OBELISK_RT_SUSPEND_CHANGE &&
           watch.edge != OBELISK_RT_WAIT_EDGE_CHANGE))
        return OBELISK_RT_LAYOUT_MISMATCH;
      bool found = false;
      for (uint32_t k = 0; k < layout.field_count; ++k) {
        const auto &capture = layout.fields[k];
        found |= capture.kind == OBELISK_RT_FRAME_CAPTURE &&
                 capture.flags == OBELISK_RT_FRAME_FIELD_FLAGS_NONE &&
                 capture.offset == watch.capture_offset &&
                 capture.size == sizeof(uint64_t);
      }
      if (!found)
        return OBELISK_RT_LAYOUT_MISMATCH;
    }
  }
  return OBELISK_RT_OK;
}
} // namespace obelisk::process

extern "C" obelisk_rt_status
obelisk_rt_v1_table_process_execute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->descriptor || !instance->action ||
      !instance->frame ||
      !(instance->descriptor->flags & OBELISK_RT_PROCESS_TABLE_NATIVE))
    return OBELISK_RT_INVALID_ARGUMENT;
  const auto *plan =
      reinterpret_cast<const obelisk_rt_table_process_descriptor_v1 *>(
          instance->descriptor)
          ->plan;
  const auto &layout = *instance->descriptor->frame_layout;
  const uint32_t *begin = layout.continuations;
  const uint32_t *end = begin + layout.continuation_count;
  const uint32_t *entry = std::lower_bound(begin, end, instance->continuation);
  if (entry == end || *entry != instance->continuation)
    return OBELISK_RT_INVALID_CONTINUATION;
  uint32_t result = plan->entry(instance, uint32_t(entry - begin));
  if (instance->status != OBELISK_RT_OK)
    return instance->status;
  if (result == OBELISK_RT_TABLE_TERMINATE) {
    *instance->action = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    return OBELISK_RT_OK;
  }
  if (result >= plan->wait_count)
    return OBELISK_RT_INVALID_CONTINUATION;
  const auto &wait = plan->waits[result];
  auto *frame = static_cast<uint8_t *>(instance->frame);
  auto *record = frame + wait.frame_offset;
  std::memcpy(record, &wait.record, sizeof(wait.record));
  for (uint32_t i = 0; i < wait.record.count; ++i) {
    const auto &watch = wait.watches[i];
    obelisk_rt_wait_entry_v1 value{0, watch.edge, watch.width};
    std::memcpy(&value.stable_id, frame + watch.capture_offset,
                sizeof(value.stable_id));
    std::memcpy(record + sizeof(wait.record) + i * sizeof(value), &value,
                sizeof(value));
  }
  instance->continuation = wait.continuation;
  *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                       wait.record.kind,
                       wait.continuation,
                       OBELISK_RT_ACTION_FRAME_WAIT_RECORD | wait.action_flags,
                       wait.frame_offset,
                       wait.frame_size};
  return OBELISK_RT_OK;
}
