//===- Process.cpp - Shared native/bytecode process instances ------------===//

#include "RuntimeInternal.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace {

constexpr uint64_t kWaitHeaderSize = sizeof(obelisk_rt_wait_record_v1);
constexpr uint64_t kWaitEntrySize = sizeof(obelisk_rt_wait_entry_v1);

bool isPowerOfTwo(uint64_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

bool addOverflow(uint64_t lhs, uint64_t rhs, uint64_t &result) {
  if (lhs > std::numeric_limits<uint64_t>::max() - rhs)
    return true;
  result = lhs + rhs;
  return false;
}

bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result) {
  uint64_t padded;
  return addOverflow(value, alignment - 1, padded) ||
         (result = padded & ~(alignment - 1), false);
}

uint64_t hashBytes(uint64_t hash, const void *data, size_t size) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  for (size_t index = 0; index != size; ++index) {
    hash ^= bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

uint64_t layoutChecksum(const obelisk_rt_frame_layout_v1 &layout) {
  uint64_t hash = UINT64_C(14695981039346656037);
  hash = hashBytes(hash, &layout.version, sizeof(layout.version));
  hash = hashBytes(hash, &layout.flags, sizeof(layout.flags));
  hash = hashBytes(hash, &layout.frame_size, sizeof(layout.frame_size));
  hash =
      hashBytes(hash, &layout.frame_alignment, sizeof(layout.frame_alignment));
  hash = hashBytes(hash, &layout.field_count, sizeof(layout.field_count));
  hash = hashBytes(hash, &layout.continuation_count,
                   sizeof(layout.continuation_count));
  for (uint32_t index = 0; index != layout.field_count; ++index)
    hash = hashBytes(hash, &layout.fields[index], sizeof(layout.fields[index]));
  for (uint32_t index = 0; index != layout.continuation_count; ++index)
    hash = hashBytes(hash, &layout.continuations[index],
                     sizeof(layout.continuations[index]));
  return hash;
}

bool validContinuation(const obelisk_rt_frame_layout_v1 &layout,
                       uint32_t continuation) {
  const uint32_t *begin = layout.continuations;
  return std::binary_search(begin, begin + layout.continuation_count,
                            continuation);
}

const obelisk_rt_frame_field_v1 *
findWaitField(const obelisk_rt_frame_layout_v1 &layout, uint64_t offset) {
  for (uint32_t index = 0; index != layout.field_count; ++index) {
    const obelisk_rt_frame_field_v1 &field = layout.fields[index];
    if (field.kind == OBELISK_RT_FRAME_WAIT && field.offset == offset)
      return &field;
  }
  return nullptr;
}

obelisk_rt_status
validateLayout(const obelisk_rt_process_descriptor_v1 &descriptor) {
  if (descriptor.handle.kind != OBELISK_RT_DESCRIPTOR_PROCESS ||
      descriptor.abi_generation != OBELISK_RT_ABI_GENERATION ||
      descriptor.flags != 0 || descriptor.reserved != 0 ||
      !descriptor.frame_layout)
    return OBELISK_RT_LAYOUT_MISMATCH;
  const obelisk_rt_frame_layout_v1 &layout = *descriptor.frame_layout;
  if (layout.version != OBELISK_RT_FRAME_LAYOUT_VERSION || layout.flags != 0 ||
      !isPowerOfTwo(layout.frame_alignment) ||
      layout.frame_alignment > UINT64_C(4096) ||
      layout.frame_size % layout.frame_alignment != 0 ||
      (layout.field_count != 0 && !layout.fields) ||
      layout.continuation_count == 0 || !layout.continuations ||
      layout.continuations[0] != 0 || layout.checksum == 0 ||
      layout.checksum != layoutChecksum(layout))
    return OBELISK_RT_LAYOUT_MISMATCH;

  for (uint32_t index = 0; index != layout.continuation_count; ++index)
    if ((index != 0 &&
         layout.continuations[index - 1] >= layout.continuations[index]))
      return OBELISK_RT_LAYOUT_MISMATCH;

  uint64_t previousEnd = 0;
  for (uint32_t index = 0; index != layout.field_count; ++index) {
    const obelisk_rt_frame_field_v1 &field = layout.fields[index];
    uint64_t end;
    if (field.kind < OBELISK_RT_FRAME_CAPTURE ||
        field.kind > OBELISK_RT_FRAME_WAIT || field.reserved != 0 ||
        field.size == 0 || !isPowerOfTwo(field.alignment) ||
        field.alignment > layout.frame_alignment ||
        field.offset % field.alignment != 0 ||
        addOverflow(field.offset, field.size, end) || end > layout.frame_size ||
        field.offset < previousEnd)
      return OBELISK_RT_LAYOUT_MISMATCH;
    if (field.kind == OBELISK_RT_FRAME_WAIT &&
        (field.flags != OBELISK_RT_FRAME_FIELD_FLAGS_NONE ||
         field.alignment < alignof(obelisk_rt_wait_record_v1) ||
         field.size < kWaitHeaderSize ||
         (field.size - kWaitHeaderSize) % kWaitEntrySize != 0))
      return OBELISK_RT_LAYOUT_MISMATCH;
    if (field.flags == OBELISK_RT_FRAME_FOUR_STATE_VALUE) {
      if (++index == layout.field_count)
        return OBELISK_RT_LAYOUT_MISMATCH;
      const obelisk_rt_frame_field_v1 &unknown = layout.fields[index];
      uint64_t unknownEnd;
      if (unknown.kind != field.kind ||
          unknown.flags != OBELISK_RT_FRAME_FOUR_STATE_UNKNOWN ||
          unknown.offset != end || unknown.size != field.size ||
          unknown.alignment != field.alignment || unknown.reserved != 0 ||
          unknown.offset % unknown.alignment != 0 ||
          addOverflow(unknown.offset, unknown.size, unknownEnd) ||
          unknownEnd > layout.frame_size)
        return OBELISK_RT_LAYOUT_MISMATCH;
      previousEnd = unknownEnd;
    } else if (field.flags != OBELISK_RT_FRAME_FIELD_FLAGS_NONE) {
      return OBELISK_RT_LAYOUT_MISMATCH;
    } else {
      previousEnd = end;
    }
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status
validateDescriptor(const obelisk_rt_process_descriptor_v1 &descriptor,
                   uint64_t &nativeSize, uint64_t &nativeAlignment,
                   uint64_t &scratchOffset, uint64_t &scratchSize) {
  obelisk_rt_status status = validateLayout(descriptor);
  if (status != OBELISK_RT_OK)
    return status;
  constexpr uint32_t validTiers =
      OBELISK_RT_TIER_MASK_NATIVE | OBELISK_RT_TIER_MASK_BYTECODE;
  if (descriptor.available_tiers == 0 ||
      (descriptor.available_tiers & ~validTiers) != 0)
    return OBELISK_RT_TIER_UNAVAILABLE;

  nativeSize = 0;
  nativeAlignment = 1;
  if (descriptor.available_tiers & OBELISK_RT_TIER_MASK_NATIVE) {
    if (!descriptor.native_requirements || !descriptor.native_execute ||
        !descriptor.native_destroy)
      return OBELISK_RT_TIER_UNAVAILABLE;
    status = descriptor.native_requirements(&nativeSize, &nativeAlignment);
    if (status != OBELISK_RT_OK)
      return status;
    if (!isPowerOfTwo(nativeAlignment) || nativeAlignment > UINT64_C(4096))
      return OBELISK_RT_INVALID_FRAME;
  } else if (descriptor.native_requirements || descriptor.native_execute ||
             descriptor.native_destroy) {
    return OBELISK_RT_TIER_UNAVAILABLE;
  }

  uint64_t bytecodeSize = 0;
  uint64_t tailAlignment = std::max<uint64_t>(nativeAlignment, 16);
  if (descriptor.available_tiers & OBELISK_RT_TIER_MASK_BYTECODE) {
    if (!descriptor.bytecode || descriptor.bytecode->reserved != 0 ||
        descriptor.bytecode->register_count >
            std::numeric_limits<uint64_t>::max() /
                OBELISK_RT_BYTECODE_REGISTER_SIZE)
      return OBELISK_RT_TIER_UNAVAILABLE;
    status = obelisk_rt_validate_bytecode_program(*descriptor.bytecode, 0);
    if (status != OBELISK_RT_OK)
      return status;
    bytecodeSize = uint64_t{descriptor.bytecode->register_count} *
                   OBELISK_RT_BYTECODE_REGISTER_SIZE;
  } else if (descriptor.bytecode) {
    return OBELISK_RT_TIER_UNAVAILABLE;
  }

  if (alignUp(descriptor.frame_layout->frame_size, tailAlignment,
              scratchOffset))
    return OBELISK_RT_INVALID_FRAME;
  scratchSize = std::max(nativeSize, bytecodeSize);
  if (descriptor.bytecode &&
      descriptor.bytecode->register_offset != scratchOffset)
    return OBELISK_RT_LAYOUT_MISMATCH;
  return OBELISK_RT_OK;
}

obelisk_rt_status validateWait(obelisk_rt_process_instance_v1 &instance,
                               obelisk_rt_fragment_action_v1 &action,
                               bool allowLegacyBytecode) {
  const obelisk_rt_frame_layout_v1 &layout = *instance.descriptor->frame_layout;
  const obelisk_rt_frame_field_v1 *field =
      findWaitField(layout, action.payload);
  if (!field)
    return OBELISK_RT_INVALID_FRAME;
  if (allowLegacyBytecode && action.flags == 0) {
    action.flags = OBELISK_RT_ACTION_FRAME_WAIT_RECORD;
    action.auxiliary = field->size;
  }
  uint64_t end;
  if (action.flags != OBELISK_RT_ACTION_FRAME_WAIT_RECORD ||
      action.auxiliary != field->size ||
      addOverflow(action.payload, action.auxiliary, end) ||
      end > instance.frame_size || action.payload % field->alignment != 0)
    return OBELISK_RT_INVALID_FRAME;
  const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
      static_cast<const uint8_t *>(instance.frame) + action.payload);
  uint64_t entriesSize;
  uint64_t required;
  if (uint64_t{wait->count} >
          (std::numeric_limits<uint64_t>::max() - kWaitHeaderSize) /
              kWaitEntrySize ||
      (entriesSize = uint64_t{wait->count} * kWaitEntrySize,
       addOverflow(kWaitHeaderSize, entriesSize, required)) ||
      wait->version != OBELISK_RT_WAIT_RECORD_VERSION ||
      wait->kind != action.suspend_kind || required > action.auxiliary)
    return OBELISK_RT_INVALID_FRAME;

  const auto *entries = reinterpret_cast<const obelisk_rt_wait_entry_v1 *>(
      reinterpret_cast<const uint8_t *>(wait) + kWaitHeaderSize);
  auto validEdge = [](obelisk_rt_wait_edge_kind edge) {
    return edge >= OBELISK_RT_WAIT_EDGE_CHANGE &&
           edge <= OBELISK_RT_WAIT_EDGE_BOTH;
  };
  auto entriesMatch = [&](bool requireEdge,
                          obelisk_rt_wait_edge_kind exactEdge) {
    for (uint32_t index = 0; index != wait->count; ++index) {
      const obelisk_rt_wait_entry_v1 &entry = entries[index];
      if (entry.reserved != 0)
        return false;
      if (requireEdge ? (exactEdge == OBELISK_RT_WAIT_EDGE_NONE
                             ? !validEdge(entry.edge)
                             : entry.edge != exactEdge)
                      : entry.edge != OBELISK_RT_WAIT_EDGE_NONE)
        return false;
    }
    return true;
  };
  bool valid = false;
  switch (wait->kind) {
  case OBELISK_RT_SUSPEND_DELAY:
    valid = wait->flags == 0 && wait->count == 0 && wait->auxiliary == 0;
    break;
  case OBELISK_RT_SUSPEND_CHANGE:
    valid = wait->flags == 0 && wait->count == 1 && wait->payload == 0 &&
            wait->auxiliary == 0 &&
            entriesMatch(true, OBELISK_RT_WAIT_EDGE_CHANGE);
    break;
  case OBELISK_RT_SUSPEND_EDGE:
    valid = wait->flags == 0 && wait->count == 1 && wait->payload == 0 &&
            wait->auxiliary == 0 &&
            entriesMatch(true, OBELISK_RT_WAIT_EDGE_NONE);
    break;
  case OBELISK_RT_SUSPEND_EVENT:
  case OBELISK_RT_SUSPEND_AWAIT:
    valid = wait->flags == 0 && wait->count == 1 && wait->payload == 0 &&
            wait->auxiliary == 0 && entriesMatch(false, 0);
    break;
  case OBELISK_RT_SUSPEND_JOIN:
    valid = wait->flags <= 1 && wait->count != 0 && wait->payload == 0 &&
            wait->auxiliary == 0 && entriesMatch(false, 0);
    break;
  case OBELISK_RT_SUSPEND_FRONTIER:
    valid = wait->flags == 0 && wait->count != 0 && wait->payload == 0 &&
            wait->auxiliary == 0 && entriesMatch(false, 0);
    break;
  default:
    // `suspend.any` shares the EDGE action kind and is distinguished by more
    // than one per-watcher edge entry.
    break;
  }
  if (wait->kind == OBELISK_RT_SUSPEND_EDGE && wait->count > 1)
    valid = wait->flags == 0 && wait->payload == 0 && wait->auxiliary == 0 &&
            entriesMatch(true, OBELISK_RT_WAIT_EDGE_NONE);
  if (!valid)
    return OBELISK_RT_INVALID_FRAME;
  return OBELISK_RT_OK;
}

obelisk_rt_status validateAction(obelisk_rt_process_instance_v1 &instance,
                                 obelisk_rt_fragment_action_v1 &action,
                                 bool bytecode) {
  const obelisk_rt_frame_layout_v1 &layout = *instance.descriptor->frame_layout;
  switch (action.kind) {
  case OBELISK_RT_FRAGMENT_CONTINUE:
    if (action.flags != 0 || action.suspend_kind != OBELISK_RT_SUSPEND_NONE ||
        action.payload != 0 || action.auxiliary != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    break;
  case OBELISK_RT_FRAGMENT_SUSPEND: {
    if (action.suspend_kind < OBELISK_RT_SUSPEND_DELAY ||
        action.suspend_kind > OBELISK_RT_SUSPEND_FRONTIER)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_status status = validateWait(instance, action, bytecode);
    if (status != OBELISK_RT_OK)
      return status;
    break;
  }
  case OBELISK_RT_FRAGMENT_TERMINATE:
    if (action.flags != 0 || action.suspend_kind != OBELISK_RT_SUSPEND_NONE ||
        action.continuation != 0 || action.payload != 0 ||
        action.auxiliary != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    return OBELISK_RT_OK;
  default:
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  return validContinuation(layout, action.continuation)
             ? OBELISK_RT_OK
             : OBELISK_RT_INVALID_CONTINUATION;
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_process_instance_create(
    const obelisk_rt_process_descriptor_v1 *descriptor,
    obelisk_rt_process_instance_v1 **outInstance) {
  if (!outInstance)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outInstance = nullptr;
  if (!descriptor)
    return OBELISK_RT_INVALID_ARGUMENT;

  uint64_t nativeSize;
  uint64_t nativeAlignment;
  uint64_t scratchOffset;
  uint64_t scratchSize;
  obelisk_rt_status status = validateDescriptor(
      *descriptor, nativeSize, nativeAlignment, scratchOffset, scratchSize);
  if (status != OBELISK_RT_OK)
    return status;

  uint64_t frameOffset;
  uint64_t tailSize;
  uint64_t totalSize;
  uint64_t allocationAlignment =
      std::max<uint64_t>({alignof(obelisk_rt_process_instance_v1),
                          descriptor->frame_layout->frame_alignment,
                          nativeAlignment, uint64_t{16}});
  if (alignUp(sizeof(obelisk_rt_process_instance_v1),
              descriptor->frame_layout->frame_alignment, frameOffset) ||
      addOverflow(scratchOffset, scratchSize, tailSize) ||
      addOverflow(frameOffset, tailSize, totalSize) ||
      alignUp(totalSize, allocationAlignment, totalSize) ||
      totalSize > std::numeric_limits<size_t>::max())
    return OBELISK_RT_OUT_OF_MEMORY;

  void *allocation = std::aligned_alloc(
      static_cast<size_t>(allocationAlignment), static_cast<size_t>(totalSize));
  if (!allocation)
    return OBELISK_RT_OUT_OF_MEMORY;
  std::memset(allocation, 0, static_cast<size_t>(totalSize));
  auto *instance = static_cast<obelisk_rt_process_instance_v1 *>(allocation);
  void *frame = static_cast<uint8_t *>(allocation) + frameOffset;
  *instance = {descriptor,
               frame,
               frame,
               descriptor->frame_layout->frame_size,
               scratchOffset,
               scratchSize,
               nullptr,
               0,
               0,
               OBELISK_RT_PROCESS_READY,
               OBELISK_RT_OK,
               nullptr,
               nullptr};
  *outInstance = instance;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_process_instance_frame(obelisk_rt_process_instance_v1 *instance,
                                     void **outFrame, uint64_t *outSize) {
  if (!outFrame || !outSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outFrame = nullptr;
  *outSize = 0;
  if (!instance)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (instance->lifecycle == OBELISK_RT_PROCESS_EXECUTING)
    return OBELISK_RT_INVALID_LIFECYCLE;
  *outFrame = instance->frame;
  *outSize = instance->frame_size;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_process_instance_execute(
    obelisk_rt_process_instance_v1 *instance, obelisk_rt_context *context,
    obelisk_rt_execution_tier requestedTier,
    obelisk_rt_fragment_action_v1 *outAction) {
  if (!outAction)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outAction = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  if (!instance || (requestedTier != OBELISK_RT_TIER_NATIVE &&
                    requestedTier != OBELISK_RT_TIER_BYTECODE))
    return OBELISK_RT_INVALID_ARGUMENT;
  if (instance->lifecycle == OBELISK_RT_PROCESS_EXECUTING ||
      instance->lifecycle == OBELISK_RT_PROCESS_TERMINATED)
    return OBELISK_RT_INVALID_LIFECYCLE;
  uint32_t mask = requestedTier == OBELISK_RT_TIER_NATIVE
                      ? OBELISK_RT_TIER_MASK_NATIVE
                      : OBELISK_RT_TIER_MASK_BYTECODE;
  if ((instance->descriptor->available_tiers & mask) == 0)
    return OBELISK_RT_TIER_UNAVAILABLE;
  if (!validContinuation(*instance->descriptor->frame_layout,
                         instance->continuation))
    return OBELISK_RT_INVALID_CONTINUATION;
  if (requestedTier == OBELISK_RT_TIER_BYTECODE) {
    if (!instance->descriptor->bytecode)
      return OBELISK_RT_TIER_UNAVAILABLE;
    obelisk_rt_status status = obelisk_rt_validate_bytecode_program(
        *instance->descriptor->bytecode, instance->continuation);
    if (status != OBELISK_RT_OK)
      return status;
  }

  if (instance->tier != 0 && instance->tier != requestedTier &&
      instance->tier == OBELISK_RT_TIER_NATIVE && instance->native_handle) {
    instance->descriptor->native_destroy(instance);
    if (instance->native_handle)
      return OBELISK_RT_INVALID_LIFECYCLE;
  }
  instance->tier = requestedTier;
  instance->context = context;
  instance->action = outAction;
  instance->status = OBELISK_RT_OK;
  instance->lifecycle = OBELISK_RT_PROCESS_EXECUTING;

  obelisk_rt_status status;
  if (requestedTier == OBELISK_RT_TIER_NATIVE) {
    try {
      status = instance->descriptor->native_execute(instance);
    } catch (const std::bad_alloc &) {
      status = OBELISK_RT_OUT_OF_MEMORY;
    } catch (...) {
      status = OBELISK_RT_INVALID_ARGUMENT;
    }
  } else {
    obelisk_rt_fragment_descriptor_v1 descriptor{};
    descriptor.handle = {OBELISK_RT_DESCRIPTOR_FRAGMENT, 0,
                         instance->descriptor->handle.id};
    descriptor.code_kind = OBELISK_RT_FRAGMENT_BYTECODE;
    descriptor.code.bytecode = *instance->descriptor->bytecode;
    status = obelisk_rt_v1_fragment_execute(
        &descriptor, context, instance->allocation,
        instance->scratch_offset + instance->scratch_size,
        instance->continuation, outAction);
  }

  instance->context = nullptr;
  instance->action = nullptr;
  if (status == OBELISK_RT_OK)
    status = validateAction(*instance, *outAction,
                            requestedTier == OBELISK_RT_TIER_BYTECODE);
  instance->status = status;
  if (status != OBELISK_RT_OK) {
    if (requestedTier == OBELISK_RT_TIER_NATIVE && instance->native_handle) {
      instance->descriptor->native_destroy(instance);
      if (instance->native_handle)
        instance->status = status = OBELISK_RT_INVALID_LIFECYCLE;
    }
    instance->lifecycle = OBELISK_RT_PROCESS_SUSPENDED;
    return status;
  }
  instance->continuation = outAction->continuation;
  instance->lifecycle = outAction->kind == OBELISK_RT_FRAGMENT_TERMINATE
                            ? OBELISK_RT_PROCESS_TERMINATED
                        : outAction->kind == OBELISK_RT_FRAGMENT_SUSPEND
                            ? OBELISK_RT_PROCESS_SUSPENDED
                            : OBELISK_RT_PROCESS_READY;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_process_instance_destroy(
    obelisk_rt_process_instance_v1 *instance) {
  if (!instance)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (instance->lifecycle == OBELISK_RT_PROCESS_EXECUTING)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (instance->native_handle) {
    instance->descriptor->native_destroy(instance);
    if (instance->native_handle)
      return OBELISK_RT_INVALID_LIFECYCLE;
  }
  std::free(instance);
  return OBELISK_RT_OK;
}
