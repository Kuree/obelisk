//===- ProcessValidation.cpp - Process ABI validation helpers -----------===//

#include "ProcessValidation.h"
#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHandle.h"
#include "obelisk/Runtime/StableHash.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace obelisk::process {

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

uint64_t layoutChecksum(const obelisk_rt_frame_layout_v1 &layout) {
  uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
  hash = obelisk_stable_hash_append_uint_le(hash, layout.version, 4);
  hash = obelisk_stable_hash_append_uint_le(hash, layout.flags, 4);
  hash = obelisk_stable_hash_append_uint_le(hash, layout.frame_size, 8);
  hash = obelisk_stable_hash_append_uint_le(hash, layout.frame_alignment, 8);
  hash = obelisk_stable_hash_append_uint_le(hash, layout.field_count, 4);
  hash = obelisk_stable_hash_append_uint_le(hash, layout.continuation_count, 4);
  for (uint32_t index = 0; index != layout.field_count; ++index) {
    const obelisk_rt_frame_field_v1 &field = layout.fields[index];
    hash = obelisk_stable_hash_append_uint_le(hash, field.kind, 4);
    hash = obelisk_stable_hash_append_uint_le(hash, field.flags, 4);
    hash = obelisk_stable_hash_append_uint_le(hash, field.offset, 8);
    hash = obelisk_stable_hash_append_uint_le(hash, field.size, 8);
    hash = obelisk_stable_hash_append_uint_le(hash, field.alignment, 4);
    hash = obelisk_stable_hash_append_uint_le(hash, field.reserved, 4);
  }
  for (uint32_t index = 0; index != layout.continuation_count; ++index)
    hash = obelisk_stable_hash_append_uint_le(hash, layout.continuations[index],
                                              4);
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
      descriptor.version != OBELISK_RT_VERSION || descriptor.flags != 0 ||
      descriptor.reserved != 0 || !descriptor.frame_layout)
    return OBELISK_RT_LAYOUT_MISMATCH;
  const obelisk_rt_frame_layout_v1 &layout = *descriptor.frame_layout;
  if (layout.version != OBELISK_RT_VERSION || layout.flags != 0 ||
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
        field.kind > OBELISK_RT_FRAME_WAIT || field.size == 0 ||
        !isPowerOfTwo(field.alignment) ||
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
          unknown.offset < end || unknown.size != field.size ||
          unknown.alignment != field.alignment || unknown.reserved != 0 ||
          unknown.offset % unknown.alignment != 0 ||
          addOverflow(unknown.offset, unknown.size, unknownEnd) ||
          unknownEnd > layout.frame_size)
        return OBELISK_RT_LAYOUT_MISMATCH;
      previousEnd = unknownEnd;
    } else if (field.flags == OBELISK_RT_FRAME_FOUR_STATE_UNKNOWN) {
      // Managed-root instrumentation may retain only precise word-sized
      // pieces of a value plane while the corresponding unknown plane remains
      // live as a whole. In that case there is deliberately no adjacent
      // FOUR_STATE_VALUE record to pair with this field.
      bool followsRoot =
          index != 0 &&
          (layout.fields[index - 1].flags == OBELISK_RT_FRAME_MANAGED_ROOT ||
           layout.fields[index - 1].flags == OBELISK_RT_FRAME_CANDIDATE_ROOT);
      if (field.reserved != 0 || !followsRoot)
        return OBELISK_RT_LAYOUT_MISMATCH;
      previousEnd = end;
    } else if (field.flags == OBELISK_RT_FRAME_MANAGED_ROOT) {
      if (field.reserved != 0 ||
          field.size != sizeof(obelisk_rt_managed_word_v1) ||
          field.alignment < alignof(obelisk_rt_managed_word_v1))
        return OBELISK_RT_LAYOUT_MISMATCH;
      previousEnd = end;
    } else if (field.flags == OBELISK_RT_FRAME_CANDIDATE_ROOT) {
      if (field.reserved == 0 ||
          (field.reserved & ~OBELISK_RT_MANAGED_ROOT_KIND_ALL) != 0 ||
          field.size != sizeof(obelisk_rt_managed_word_v1) ||
          field.alignment < alignof(obelisk_rt_managed_word_v1))
        return OBELISK_RT_LAYOUT_MISMATCH;
      previousEnd = end;
    } else if (field.flags != OBELISK_RT_FRAME_FIELD_FLAGS_NONE ||
               field.reserved != 0) {
      return OBELISK_RT_LAYOUT_MISMATCH;
    } else {
      previousEnd = end;
    }
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status
validateDescriptor(const obelisk_rt_process_descriptor_v1 &descriptor,
                   obelisk_rt_context *context, uint64_t &nativeSize,
                   uint64_t &nativeAlignment, uint64_t &scratchOffset,
                   uint64_t &scratchSize) {
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
    if ((descriptor.bytecode == nullptr) ==
        (descriptor.design_bytecode == nullptr))
      return OBELISK_RT_TIER_UNAVAILABLE;
    if (descriptor.bytecode) {
      if (descriptor.bytecode->reserved != 0)
        return OBELISK_RT_TIER_UNAVAILABLE;
      status = obelisk_rt_validate_bytecode_program(*descriptor.bytecode, 0);
      if (status != OBELISK_RT_OK)
        return status;
      bytecodeSize = uint64_t{descriptor.bytecode->register_count} *
                     OBELISK_RT_BYTECODE_REGISTER_SIZE;
    } else {
      if (!descriptor.execution ||
          descriptor.design_bytecode->execution != descriptor.execution)
        return OBELISK_RT_TIER_UNAVAILABLE;
      uint64_t bytecodeAlignment = 1;
      status = obelisk_rt_validate_design_bytecode(*descriptor.design_bytecode,
                                                   context, &bytecodeSize,
                                                   &bytecodeAlignment);
      if (status != OBELISK_RT_OK)
        return status;
      tailAlignment = std::max(tailAlignment, bytecodeAlignment);
    }
  } else if (descriptor.bytecode || descriptor.design_bytecode) {
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

const obelisk_rt_observer_descriptor_v1 *
findObserverDescriptor(const obelisk_rt_execution_descriptor_v1 *execution,
                       uint64_t codeUnitID) {
  if (!execution || !execution->observers)
    return nullptr;
  uint64_t first = 0;
  uint64_t count = execution->observer_count;
  while (count != 0) {
    uint64_t step = count / 2;
    uint64_t index = first + step;
    if (execution->observers[index].code_unit_id < codeUnitID) {
      first = index + 1;
      count -= step + 1;
    } else {
      count = step;
    }
  }
  return first < execution->observer_count &&
                 execution->observers[first].code_unit_id == codeUnitID
             ? &execution->observers[first]
             : nullptr;
}

obelisk_rt_status
validateComputedWait(obelisk_rt_process_instance_v1 &instance,
                     const obelisk_rt_fragment_action_v1 &action) {
  if (action.auxiliary < sizeof(obelisk_rt_computed_wait_record_v1))
    return OBELISK_RT_INVALID_FRAME;
  const auto *wait =
      reinterpret_cast<const obelisk_rt_computed_wait_record_v1 *>(
          static_cast<const uint8_t *>(instance.frame) + action.payload);
  return obelisk_rt_validate_computed_wait_record(
             instance.descriptor->execution, wait, action.auxiliary)
             ? OBELISK_RT_OK
             : OBELISK_RT_INVALID_FRAME;
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
  constexpr uint32_t resumeFlags = OBELISK_RT_ACTION_RESUME_REGION_VALID |
                                   OBELISK_RT_ACTION_RESUME_REGION_MASK;
  uint32_t resumeRegion =
      (action.flags & OBELISK_RT_ACTION_RESUME_REGION_MASK) >>
      OBELISK_RT_ACTION_RESUME_REGION_SHIFT;
  uint64_t end;
  if ((action.flags & ~resumeFlags) != OBELISK_RT_ACTION_FRAME_WAIT_RECORD ||
      ((action.flags & OBELISK_RT_ACTION_RESUME_REGION_MASK) != 0 &&
       (action.flags & OBELISK_RT_ACTION_RESUME_REGION_VALID) == 0) ||
      ((action.flags & OBELISK_RT_ACTION_RESUME_REGION_VALID) != 0 &&
       !obelisk_rt_is_process_home_region(resumeRegion)) ||
      action.auxiliary != field->size ||
      addOverflow(action.payload, action.auxiliary, end) ||
      end > instance.frame_size || action.payload % field->alignment != 0)
    return OBELISK_RT_INVALID_FRAME;
  if (action.suspend_kind == OBELISK_RT_SUSPEND_OBSERVER)
    return validateComputedWait(instance, action);
  const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
      static_cast<const uint8_t *>(instance.frame) + action.payload);
  uint64_t entriesSize;
  uint64_t required;
  if (uint64_t{wait->count} >
          (std::numeric_limits<uint64_t>::max() - kWaitHeaderSize) /
              kWaitEntrySize ||
      (entriesSize = uint64_t{wait->count} * kWaitEntrySize,
       addOverflow(kWaitHeaderSize, entriesSize, required)) ||
      wait->version != OBELISK_RT_VERSION ||
      wait->kind != action.suspend_kind || required > action.auxiliary)
    return OBELISK_RT_INVALID_FRAME;

  const auto *entries = reinterpret_cast<const obelisk_rt_wait_entry_v1 *>(
      reinterpret_cast<const uint8_t *>(wait) + kWaitHeaderSize);
  auto validEdge = [](obelisk_rt_wait_edge_kind edge) {
    return edge >= OBELISK_RT_WAIT_EDGE_CHANGE &&
           edge <= OBELISK_RT_WAIT_EDGE_BOTH;
  };
  auto validClockEdge = [&](uint32_t edge) {
    return validEdge(static_cast<obelisk_rt_wait_edge_kind>(edge)) ||
           ((edge & ~OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) ==
                OBELISK_RT_WAIT_EDGE_TRANSITION_MASK &&
            (edge & OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) != 0);
  };
  auto validClockCondition = [](uint32_t predicate) {
    return predicate == OBELISK_RT_WAIT_EDGE_NONE ||
           (predicate >= OBELISK_RT_WAIT_CONDITION_KNOWN_ONE &&
            predicate <= OBELISK_RT_WAIT_CONDITION_CASE_NE_ONE) ||
           (predicate >= OBELISK_RT_WAIT_CONDITION_OBSERVER &&
            predicate <= OBELISK_RT_WAIT_CONDITION_OBSERVER_LAST);
  };
  auto validSignalHandle = [](uint64_t stableID) {
    obelisk_rt_stable_handle_v1 decoded;
    return obelisk_rt_stable_handle_decode(stableID, &decoded);
  };
  auto entriesMatch = [&](bool requireEdge, obelisk_rt_wait_edge_kind exactEdge,
                          bool requireSignalHandle = false) {
    for (uint32_t index = 0; index != wait->count; ++index) {
      const obelisk_rt_wait_entry_v1 &entry = entries[index];
      bool managed = requireEdge && requireSignalHandle &&
                     entry.reserved == OBELISK_RT_WAIT_WIDTH_MANAGED;
      if (requireSignalHandle) {
        if (managed ? entry.edge != OBELISK_RT_WAIT_EDGE_CHANGE
                    : !validSignalHandle(entry.stable_id))
          return false;
      }
      if (requireEdge ? (!managed && entry.reserved == 0) : entry.reserved != 0)
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
  bool slotFinal =
      (wait->flags & OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL) != 0;
  uint32_t behaviorFlags =
      wait->flags & ~(OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF |
                      OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL |
                      OBELISK_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS);
  bool suppressActiveSelf =
      (wait->flags & OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF) != 0;
  switch (wait->kind) {
  case OBELISK_RT_SUSPEND_DELAY:
    valid = wait->flags == 0 && wait->count == 0 && wait->auxiliary == 0;
    break;
  case OBELISK_RT_SUSPEND_CHANGE:
    valid =
        (behaviorFlags == 0 || behaviorFlags == OBELISK_RT_WAIT_LEVEL_TRUE) &&
        (!suppressActiveSelf || behaviorFlags == 0) &&
        (wait->flags & ~(OBELISK_RT_WAIT_LEVEL_TRUE |
                         OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF)) == 0 &&
        wait->count == 1 && wait->payload == 0 && wait->auxiliary == 0 &&
        entriesMatch(true, OBELISK_RT_WAIT_EDGE_CHANGE, true);
    break;
  case OBELISK_RT_SUSPEND_EDGE:
    if (behaviorFlags == OBELISK_RT_WAIT_CLOCK_OCCURRENCE) {
      uint32_t conditions =
          static_cast<uint32_t>(__builtin_popcountll(wait->auxiliary));
      uint32_t primaries =
          conditions < wait->count ? wait->count - conditions : 0;
      valid = obelisk_rt_is_clock_occurrence_wait_flags(wait->flags) &&
              wait->payload != 0 && primaries >= 1 && primaries <= 64 &&
              (primaries == 64 || (wait->auxiliary >> primaries) == 0);
      for (uint32_t index = 0; valid && index != wait->count; ++index) {
        bool condition = index >= primaries;
        bool observer =
            condition &&
            entries[index].edge >= OBELISK_RT_WAIT_CONDITION_OBSERVER &&
            entries[index].edge <= OBELISK_RT_WAIT_CONDITION_OBSERVER_LAST;
        valid = (observer || validSignalHandle(entries[index].stable_id)) &&
                (!condition || observer || entries[index].reserved != 0) &&
                (condition ? validClockCondition(entries[index].edge)
                           : validClockEdge(entries[index].edge));
        if (!valid || !observer)
          continue;
        const obelisk_rt_observer_descriptor_v1 *descriptor =
            findObserverDescriptor(instance.descriptor->execution,
                                   entries[index].stable_id);
        valid = descriptor && descriptor->result_width == 1 &&
                descriptor->capture_count == entries[index].reserved;
        uint64_t bytes = uint64_t{entries[index].reserved} *
                         sizeof(obelisk_rt_computed_capture_v1);
        valid &= !addOverflow(required, bytes, required) &&
                 required <= action.auxiliary;
      }
      bool hasObserver = false;
      for (uint32_t index = primaries; index != wait->count; ++index)
        hasObserver |=
            entries[index].edge >= OBELISK_RT_WAIT_CONDITION_OBSERVER &&
            entries[index].edge <= OBELISK_RT_WAIT_CONDITION_OBSERVER_LAST;
      valid &= ((wait->flags & OBELISK_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS) !=
                0) == hasObserver;
    } else if (behaviorFlags == OBELISK_RT_WAIT_EDGE_IFF)
      valid = wait->count == 2 && wait->payload == 0 && wait->auxiliary == 0 &&
              (wait->flags & ~(OBELISK_RT_WAIT_EDGE_IFF |
                               OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF)) == 0 &&
              validEdge(entries[0].edge) && entries[0].reserved != 0 &&
              entries[1].edge == OBELISK_RT_WAIT_EDGE_NONE &&
              entries[1].reserved != 0 &&
              validSignalHandle(entries[0].stable_id) &&
              validSignalHandle(entries[1].stable_id);
    else
      valid = behaviorFlags == 0 &&
              (wait->flags & ~OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF) == 0 &&
              wait->count != 0 && wait->payload == 0 && wait->auxiliary == 0 &&
              entriesMatch(true, OBELISK_RT_WAIT_EDGE_NONE, true);
    break;
  case OBELISK_RT_SUSPEND_EVENT:
  case OBELISK_RT_SUSPEND_AWAIT:
    valid = wait->flags == 0 && wait->count == 1 && wait->payload == 0 &&
            wait->auxiliary == 0 && entriesMatch(false, 0);
    break;
  case OBELISK_RT_SUSPEND_EVENT_ORDER:
    valid = wait->flags == 0 && wait->count != 0 && wait->payload == 0 &&
            wait->auxiliary == 0 && entriesMatch(false, 0);
    break;
  case OBELISK_RT_SUSPEND_MAILBOX:
    valid = wait->flags <= OBELISK_RT_WAIT_MAILBOX_NOT_FULL &&
            wait->count == 1 && wait->payload == 0 && wait->auxiliary == 0 &&
            entriesMatch(false, 0);
    break;
  case OBELISK_RT_SUSPEND_SEMAPHORE:
    valid = wait->flags == 0 && wait->count == 1 &&
            wait->payload <= UINT32_MAX &&
            static_cast<int32_t>(wait->payload) >= 0 && wait->auxiliary == 0 &&
            entriesMatch(false, 0);
    break;
  case OBELISK_RT_SUSPEND_JOIN:
    valid = wait->flags <= 1 && wait->count != 0 && wait->payload == 0 &&
            wait->auxiliary == 0 && entriesMatch(false, 0);
    break;
  case OBELISK_RT_SUSPEND_FOREVER:
  case OBELISK_RT_SUSPEND_CHILDREN:
    valid = wait->flags == 0 && wait->count == 0 && wait->payload == 0 &&
            wait->auxiliary == 0;
    break;
  case OBELISK_RT_SUSPEND_FRONTIER:
    valid = wait->flags == 0 && wait->count != 0 && wait->payload == 0 &&
            wait->auxiliary == 0 && entriesMatch(false, 0);
    break;
  default:
    break;
  }
  // `suspend.any` shares the EDGE action kind and is distinguished by more
  // than one per-watcher edge entry.
  if (wait->kind == OBELISK_RT_SUSPEND_EDGE && wait->count > 1 &&
      behaviorFlags == 0)
    valid = wait->payload == 0 && wait->auxiliary == 0 &&
            (wait->flags & ~OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF) == 0 &&
            entriesMatch(true, OBELISK_RT_WAIT_EDGE_NONE, true);
  valid &= !slotFinal || behaviorFlags == OBELISK_RT_WAIT_CLOCK_OCCURRENCE;
  return valid ? OBELISK_RT_OK : OBELISK_RT_INVALID_FRAME;
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
        action.suspend_kind > OBELISK_RT_SUSPEND_EVENT_ORDER)
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
  case OBELISK_RT_FRAGMENT_TASK_CALL:
    if (action.flags != 0 || action.suspend_kind != OBELISK_RT_SUSPEND_NONE ||
        action.continuation == 0 || action.payload == 0 ||
        action.payload > std::numeric_limits<uintptr_t>::max() ||
        action.auxiliary != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    break;
  case OBELISK_RT_FRAGMENT_PROCESS_SUSPEND:
    if (action.flags != 0 || action.suspend_kind != OBELISK_RT_SUSPEND_NONE ||
        action.continuation == 0 || action.payload != 0 ||
        action.auxiliary != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    break;
  default:
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  return validContinuation(layout, action.continuation)
             ? OBELISK_RT_OK
             : OBELISK_RT_INVALID_CONTINUATION;
}

const obelisk_rt_wait_record_v1 *currentWait(const ScheduledProcess &process) {
  if (!process.instance || !process.instance->descriptor ||
      !process.instance->descriptor->frame_layout || !process.instance->frame)
    return nullptr;
  const obelisk_rt_frame_layout_v1 &layout =
      *process.instance->descriptor->frame_layout;
  if (process.waitSize != 0) {
    const obelisk_rt_frame_field_v1 *field =
        findWaitField(layout, process.waitOffset);
    if (!field || field->size != process.waitSize ||
        process.waitOffset > process.instance->frame_size ||
        process.waitSize > process.instance->frame_size - process.waitOffset)
      return nullptr;
    return reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
        static_cast<const uint8_t *>(process.instance->frame) +
        process.waitOffset);
  }
  for (uint32_t index = 0; index != layout.field_count; ++index) {
    const obelisk_rt_frame_field_v1 &field = layout.fields[index];
    if (field.kind != OBELISK_RT_FRAME_WAIT ||
        field.size < sizeof(obelisk_rt_wait_record_v1) ||
        field.offset > process.instance->frame_size ||
        field.size > process.instance->frame_size - field.offset)
      continue;
    return reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
        static_cast<const uint8_t *>(process.instance->frame) + field.offset);
  }
  return nullptr;
}

const obelisk_rt_wait_entry_v1 *
waitEntries(const obelisk_rt_wait_record_v1 *wait) {
  return reinterpret_cast<const obelisk_rt_wait_entry_v1 *>(wait + 1);
}

} // namespace obelisk::process

using namespace obelisk::process;

bool obelisk_rt_validate_computed_wait_record(
    const obelisk_rt_execution_descriptor_v1 *execution,
    const obelisk_rt_computed_wait_record_v1 *wait, uint64_t available) {
  if (!execution || !wait || available < sizeof(*wait) ||
      wait->version != OBELISK_RT_VERSION ||
      wait->kind != OBELISK_RT_SUSPEND_OBSERVER ||
      wait->flags != OBELISK_RT_COMPUTED_WAIT_INTERLEAVED ||
      wait->clause_count == 0 || wait->observer_count < wait->clause_count ||
      wait->reserved != 0 || wait->total_size > available)
    return false;
  uint64_t observersEnd, capturesEnd, dependenciesEnd, clausesEnd,
      previousValuesEnd;
  if (addOverflow(wait->observers_offset,
                  uint64_t{wait->observer_count} *
                      sizeof(obelisk_rt_computed_observer_v1),
                  observersEnd) ||
      addOverflow(wait->captures_offset,
                  uint64_t{wait->capture_count} *
                      sizeof(obelisk_rt_computed_capture_v1),
                  capturesEnd) ||
      addOverflow(wait->dependencies_offset,
                  uint64_t{wait->dependency_count} *
                      sizeof(obelisk_rt_computed_dependency_v1),
                  dependenciesEnd) ||
      addOverflow(wait->clauses_offset,
                  uint64_t{wait->clause_count} *
                      sizeof(obelisk_rt_computed_clause_v1),
                  clausesEnd) ||
      addOverflow(wait->previous_value_offset,
                  uint64_t{wait->previous_limb_count} * sizeof(uint64_t) * 2,
                  previousValuesEnd) ||
      wait->observers_offset != sizeof(*wait) ||
      wait->captures_offset != observersEnd ||
      wait->dependencies_offset != capturesEnd ||
      wait->clauses_offset != dependenciesEnd ||
      wait->previous_value_offset != clausesEnd ||
      wait->previous_unknown_offset != 0 ||
      wait->total_size != previousValuesEnd || wait->total_size > available)
    return false;
  const auto *observers = computedWaitSpan<obelisk_rt_computed_observer_v1>(
      wait, wait->observers_offset, wait->observer_count);
  const auto *captures = computedWaitSpan<obelisk_rt_computed_capture_v1>(
      wait, wait->captures_offset, wait->capture_count);
  const auto *dependencies =
      computedWaitSpan<obelisk_rt_computed_dependency_v1>(
          wait, wait->dependencies_offset, wait->dependency_count);
  const auto *clauses = computedWaitSpan<obelisk_rt_computed_clause_v1>(
      wait, wait->clauses_offset, wait->clause_count);
  if (!observers || !captures || !dependencies || !clauses)
    return false;

  std::vector<bool> usedConditions(wait->observer_count, false);
  uint64_t expectedPrevious = wait->previous_value_offset;
  for (uint32_t index = 0; index != wait->observer_count; ++index) {
    const obelisk_rt_computed_observer_v1 &observer = observers[index];
    const obelisk_rt_observer_descriptor_v1 *descriptor =
        findObserverDescriptor(execution, observer.code_unit_id);
    if (!descriptor || observer.capture_begin > wait->capture_count ||
        observer.capture_count > wait->capture_count - observer.capture_begin ||
        observer.dependency_begin > wait->dependency_count ||
        observer.dependency_count >
            wait->dependency_count - observer.dependency_begin ||
        observer.capture_count != descriptor->capture_count ||
        observer.reserved != 0)
      return false;
    if (index < wait->clause_count) {
      uint64_t limbs = (uint64_t{descriptor->result_width} + 63) / 64;
      if (observer.previous_offset != expectedPrevious ||
          limbs > (wait->total_size - expectedPrevious) / 16)
        return false;
      expectedPrevious += limbs * 16;
    } else if (observer.previous_offset != UINT32_MAX ||
               descriptor->result_width != 1) {
      return false;
    }
    for (uint32_t capture = 0; capture != observer.capture_count; ++capture) {
      const obelisk_rt_observer_capture_abi_v1 &abi =
          descriptor->capture_abi[capture];
      if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_MANAGED)
        continue;
      uint64_t stable = captures[observer.capture_begin + capture].stable_id;
      // Runtime-created named events use the disjoint dynamic-event
      // namespace, not the state-handle namespace. They are nevertheless a
      // canonical event capture and the observer evaluator reconstructs the
      // event handle from this exact word.
      if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_EVENT &&
          obelisk_rt_stable_handle_is_dynamic_event(stable))
        continue;
      obelisk_rt_stable_handle_v1 decoded;
      if (!obelisk_rt_stable_handle_decode(stable, &decoded))
        return false;
    }
    for (uint32_t dependency = 0; dependency != observer.dependency_count;
         ++dependency) {
      const obelisk_rt_computed_dependency_v1 &entry =
          dependencies[observer.dependency_begin + dependency];
      if ((entry.kind != OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL &&
           entry.kind != OBELISK_RT_OBSERVER_DEPENDENCY_EVENT &&
           entry.kind != OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED) ||
          entry.width == 0 ||
          ((entry.kind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT ||
            entry.kind == OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED) &&
           entry.width != 1))
        return false;
      if (entry.kind == OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL) {
        obelisk_rt_stable_handle_v1 decoded;
        if (!obelisk_rt_stable_handle_decode(entry.stable_id, &decoded))
          return false;
      }
    }
  }
  if (expectedPrevious != wait->total_size)
    return false;
  for (uint32_t index = 0; index != wait->clause_count; ++index) {
    const obelisk_rt_computed_clause_v1 &clause = clauses[index];
    if (clause.primary_observer != index ||
        clause.edge > OBELISK_RT_WAIT_EDGE_BOTH ||
        (clause.flags & ~(OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY |
                          OBELISK_RT_COMPUTED_CLAUSE_LEVEL_TRUE)) != 0 ||
        ((clause.flags & OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY) != 0 &&
         (clause.flags & OBELISK_RT_COMPUTED_CLAUSE_LEVEL_TRUE) != 0))
      return false;
    if (clause.condition_observer != OBELISK_RT_OBSERVER_CONDITION_NONE) {
      if (clause.condition_observer < wait->clause_count ||
          clause.condition_observer >= wait->observer_count ||
          usedConditions[clause.condition_observer])
        return false;
      usedConditions[clause.condition_observer] = true;
    }
  }
  for (uint32_t index = wait->clause_count; index != wait->observer_count;
       ++index)
    if (!usedConditions[index])
      return false;
  return true;
}
