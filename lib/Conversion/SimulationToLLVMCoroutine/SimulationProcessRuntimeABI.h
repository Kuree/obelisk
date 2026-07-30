//===- SimulationProcessRuntimeABI.h - Native process ABI layout ------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSRUNTIMEABI_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSRUNTIMEABI_H

#include "obelisk/Runtime/Runtime.h"

#include <cstddef>
#include <cstdint>

namespace obelisk::detail {

inline constexpr uint64_t kInstanceAllocationOffset =
    offsetof(obelisk_rt_process_instance_v1, allocation);
inline constexpr uint64_t kInstanceFrameOffset =
    offsetof(obelisk_rt_process_instance_v1, frame);
inline constexpr uint64_t kInstanceScratchOffset =
    offsetof(obelisk_rt_process_instance_v1, scratch_offset);
inline constexpr uint64_t kInstanceNativeHandleOffset =
    offsetof(obelisk_rt_process_instance_v1, native_handle);
inline constexpr uint64_t kInstanceContinuationOffset =
    offsetof(obelisk_rt_process_instance_v1, continuation);
inline constexpr uint64_t kInstanceStatusOffset =
    offsetof(obelisk_rt_process_instance_v1, status);
inline constexpr uint64_t kInstanceContextOffset =
    offsetof(obelisk_rt_process_instance_v1, context);
inline constexpr uint64_t kInstanceActionOffset =
    offsetof(obelisk_rt_process_instance_v1, action);
inline constexpr uint64_t kActionKindOffset =
    offsetof(obelisk_rt_fragment_action_v1, kind);
inline constexpr uint64_t kActionSuspendKindOffset =
    offsetof(obelisk_rt_fragment_action_v1, suspend_kind);
inline constexpr uint64_t kActionContinuationOffset =
    offsetof(obelisk_rt_fragment_action_v1, continuation);
inline constexpr uint64_t kActionFlagsOffset =
    offsetof(obelisk_rt_fragment_action_v1, flags);
inline constexpr uint64_t kActionPayloadOffset =
    offsetof(obelisk_rt_fragment_action_v1, payload);
inline constexpr uint64_t kActionAuxiliaryOffset =
    offsetof(obelisk_rt_fragment_action_v1, auxiliary);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSRUNTIMEABI_H

