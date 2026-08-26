//===- SimulationProcessRuntimeABI.h - Native process ABI layout ------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSRUNTIMEABI_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSRUNTIMEABI_H

#include <cstdint>

namespace obelisk::detail {

// Field indices, not host byte offsets. Loads and stores turn these into typed
// LLVM GEPs, leaving the selected target DataLayout to place each field.
enum class ProcessInstanceField : uint32_t {
  Descriptor,
  Allocation,
  Frame,
  FrameSize,
  ScratchOffset,
  ScratchSize,
  NativeHandle,
  Continuation,
  Tier,
  Lifecycle,
  Status,
  Context,
  Action,
  OwnershipContext,
  ObserverPinCount,
  ObserverDestroyPending,
};

enum class FragmentActionField : uint32_t {
  Kind,
  SuspendKind,
  Continuation,
  Flags,
  Payload,
  Auxiliary,
};

inline constexpr auto kInstanceAllocationField =
    ProcessInstanceField::Allocation;
inline constexpr auto kInstanceFrameField = ProcessInstanceField::Frame;
inline constexpr auto kInstanceScratchField =
    ProcessInstanceField::ScratchOffset;
inline constexpr auto kInstanceNativeHandleField =
    ProcessInstanceField::NativeHandle;
inline constexpr auto kInstanceContinuationField =
    ProcessInstanceField::Continuation;
inline constexpr auto kInstanceStatusField = ProcessInstanceField::Status;
inline constexpr auto kInstanceContextField = ProcessInstanceField::Context;
inline constexpr auto kInstanceActionField = ProcessInstanceField::Action;
inline constexpr auto kActionKindField = FragmentActionField::Kind;
inline constexpr auto kActionSuspendKindField =
    FragmentActionField::SuspendKind;
inline constexpr auto kActionContinuationField =
    FragmentActionField::Continuation;
inline constexpr auto kActionFlagsField = FragmentActionField::Flags;
inline constexpr auto kActionPayloadField = FragmentActionField::Payload;
inline constexpr auto kActionAuxiliaryField = FragmentActionField::Auxiliary;

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSRUNTIMEABI_H
