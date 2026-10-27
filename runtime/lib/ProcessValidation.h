//===- ProcessValidation.h - Process ABI validation helpers -----*- C++ -*-===//

#ifndef TALOS_RUNTIME_LIB_PROCESSVALIDATION_H
#define TALOS_RUNTIME_LIB_PROCESSVALIDATION_H

#include "talos/Runtime/Runtime.h"

#include <cstdint>

struct ScheduledProcess;

namespace talos::process {

talos_rt_status
validateTableProcess(const talos_rt_process_descriptor_v1 &descriptor);

constexpr uint64_t kWaitHeaderSize = sizeof(talos_rt_wait_record_v1);
constexpr uint64_t kWaitEntrySize = sizeof(talos_rt_wait_entry_v1);

bool addOverflow(uint64_t lhs, uint64_t rhs, uint64_t &result);
bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result);
uint64_t layoutChecksum(const talos_rt_frame_layout_v1 &layout);
bool validContinuation(const talos_rt_frame_layout_v1 &layout,
                       uint32_t continuation);
/// The wait field at `offset` of a layout accepted by validateLayout.
const talos_rt_frame_field_v1 *
findWaitField(const talos_rt_frame_layout_v1 &layout, uint64_t offset);
talos_rt_status
validateDescriptor(const talos_rt_process_descriptor_v1 &descriptor,
                   talos_rt_context *context, uint64_t &nativeSize,
                   uint64_t &nativeAlignment, uint64_t &scratchOffset,
                   uint64_t &scratchSize);
talos_rt_status validateAction(talos_rt_process_instance_v1 &instance,
                                 talos_rt_fragment_action_v1 &action,
                                 bool bytecode);
const talos_rt_wait_record_v1 *currentWait(const ScheduledProcess &process);
const talos_rt_wait_entry_v1 *
waitEntries(const talos_rt_wait_record_v1 *wait);
const talos_rt_observer_descriptor_v1 *
findObserverDescriptor(const talos_rt_execution_descriptor_v1 *execution,
                       uint64_t codeUnitID);

template <typename T>
const T *computedWaitSpan(const talos_rt_computed_wait_record_v1 *wait,
                          uint64_t offset, uint64_t count) {
  if (!wait || offset > wait->total_size ||
      count > (wait->total_size - offset) / sizeof(T))
    return nullptr;
  return reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(wait) +
                                     offset);
}

template <typename T>
T *computedWaitSpan(talos_rt_computed_wait_record_v1 *wait, uint64_t offset,
                    uint64_t count) {
  return const_cast<T *>(computedWaitSpan<T>(
      static_cast<const talos_rt_computed_wait_record_v1 *>(wait), offset,
      count));
}

} // namespace talos::process

#endif // TALOS_RUNTIME_LIB_PROCESSVALIDATION_H
