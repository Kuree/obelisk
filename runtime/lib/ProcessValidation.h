//===- ProcessValidation.h - Process ABI validation helpers -----*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_PROCESSVALIDATION_H
#define OBELISK_RUNTIME_LIB_PROCESSVALIDATION_H

#include "obelisk/Runtime/Runtime.h"

#include <cstdint>

namespace obelisk::process {

constexpr uint64_t kWaitHeaderSize = sizeof(obelisk_rt_wait_record_v1);
constexpr uint64_t kWaitEntrySize = sizeof(obelisk_rt_wait_entry_v1);

bool addOverflow(uint64_t lhs, uint64_t rhs, uint64_t &result);
bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result);
bool validContinuation(const obelisk_rt_frame_layout_v1 &layout,
                       uint32_t continuation);
const obelisk_rt_frame_field_v1 *
findWaitField(const obelisk_rt_frame_layout_v1 &layout, uint64_t offset);
obelisk_rt_status
validateDescriptor(const obelisk_rt_process_descriptor_v1 &descriptor,
                   uint64_t &nativeSize, uint64_t &nativeAlignment,
                   uint64_t &scratchOffset, uint64_t &scratchSize);
const obelisk_rt_observer_descriptor_v1 *
findObserverDescriptor(const obelisk_rt_execution_descriptor_v1 *execution,
                       uint64_t codeUnitID);

template <typename T>
const T *computedWaitSpan(const obelisk_rt_computed_wait_record_v1 *wait,
                          uint64_t offset, uint64_t count) {
  if (!wait || offset > wait->total_size ||
      count > (wait->total_size - offset) / sizeof(T))
    return nullptr;
  return reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(wait) +
                                     offset);
}

template <typename T>
T *computedWaitSpan(obelisk_rt_computed_wait_record_v1 *wait, uint64_t offset,
                    uint64_t count) {
  return const_cast<T *>(computedWaitSpan<T>(
      static_cast<const obelisk_rt_computed_wait_record_v1 *>(wait), offset,
      count));
}

} // namespace obelisk::process

#endif // OBELISK_RUNTIME_LIB_PROCESSVALIDATION_H
