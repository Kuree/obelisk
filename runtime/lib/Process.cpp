//===- Process.cpp - Shared native/bytecode process instances ------------===//

#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHandle.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <tuple>

namespace {

constexpr uint64_t kNativeLogicalProcessTag = UINT64_C(1) << 63;

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
      descriptor.version != OBELISK_RT_VERSION ||
      descriptor.flags != 0 || descriptor.reserved != 0 ||
      !descriptor.frame_layout)
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
      status = obelisk_rt_validate_design_bytecode(
          *descriptor.design_bytecode, &bytecodeSize, &bytecodeAlignment);
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

const obelisk_rt_observer_descriptor_v1 *findObserverDescriptor(
    const obelisk_rt_execution_descriptor_v1 *execution, uint64_t codeUnitID) {
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

template <typename T>
const T *computedSpan(const obelisk_rt_computed_wait_record_v1 *wait,
                      uint64_t offset, uint64_t count) {
  if (!wait || offset > wait->total_size ||
      count > (wait->total_size - offset) / sizeof(T))
    return nullptr;
  return reinterpret_cast<const T *>(
      reinterpret_cast<const uint8_t *>(wait) + offset);
}

obelisk_rt_status
validateComputedWait(obelisk_rt_process_instance_v1 &instance,
                     const obelisk_rt_fragment_action_v1 &action) {
  if (action.auxiliary < sizeof(obelisk_rt_computed_wait_record_v1))
    return OBELISK_RT_INVALID_FRAME;
  const auto *wait =
      reinterpret_cast<const obelisk_rt_computed_wait_record_v1 *>(
          static_cast<const uint8_t *>(instance.frame) + action.payload);
  if (wait->version != OBELISK_RT_VERSION ||
      wait->kind != OBELISK_RT_SUSPEND_OBSERVER ||
      wait->flags != OBELISK_RT_COMPUTED_WAIT_INTERLEAVED ||
      wait->clause_count == 0 ||
      wait->observer_count < wait->clause_count ||
      wait->reserved != 0 || wait->total_size > action.auxiliary)
    return OBELISK_RT_INVALID_FRAME;
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
      wait->total_size != previousValuesEnd ||
      wait->total_size > action.auxiliary)
    return OBELISK_RT_INVALID_FRAME;
  const auto *observers = computedSpan<obelisk_rt_computed_observer_v1>(
      wait, wait->observers_offset, wait->observer_count);
  const auto *captures = computedSpan<obelisk_rt_computed_capture_v1>(
      wait, wait->captures_offset, wait->capture_count);
  const auto *dependencies = computedSpan<obelisk_rt_computed_dependency_v1>(
      wait, wait->dependencies_offset, wait->dependency_count);
  const auto *clauses = computedSpan<obelisk_rt_computed_clause_v1>(
      wait, wait->clauses_offset, wait->clause_count);
  if (!observers || !captures || !dependencies || !clauses)
    return OBELISK_RT_INVALID_FRAME;

  const obelisk_rt_execution_descriptor_v1 *execution =
      instance.descriptor->execution;
  std::vector<bool> usedConditions(wait->observer_count, false);
  uint64_t expectedPrevious = wait->previous_value_offset;
  for (uint32_t index = 0; index != wait->observer_count; ++index) {
    const obelisk_rt_computed_observer_v1 &observer = observers[index];
    const obelisk_rt_observer_descriptor_v1 *descriptor =
        findObserverDescriptor(execution, observer.code_unit_id);
    if (!descriptor ||
        observer.capture_begin > wait->capture_count ||
        observer.capture_count >
            wait->capture_count - observer.capture_begin ||
        observer.dependency_begin > wait->dependency_count ||
        observer.dependency_count >
            wait->dependency_count - observer.dependency_begin ||
        observer.capture_count != descriptor->capture_count ||
        observer.reserved != 0)
      return OBELISK_RT_INVALID_FRAME;
    if (index < wait->clause_count) {
      uint64_t limbs = (uint64_t{descriptor->result_width} + 63) / 64;
      if (observer.previous_offset != expectedPrevious ||
          limbs > (wait->total_size - expectedPrevious) / 16)
        return OBELISK_RT_INVALID_FRAME;
      expectedPrevious += limbs * 16;
    } else if (observer.previous_offset != UINT32_MAX ||
               descriptor->result_width != 1) {
      return OBELISK_RT_INVALID_FRAME;
    }
    for (uint32_t capture = 0; capture != observer.capture_count; ++capture) {
      obelisk_rt_stable_handle_v1 decoded;
      if (!obelisk_rt_stable_handle_decode(
              captures[observer.capture_begin + capture].stable_id,
              &decoded))
        return OBELISK_RT_INVALID_FRAME;
    }
    for (uint32_t dependency = 0;
         dependency != observer.dependency_count; ++dependency) {
      const obelisk_rt_computed_dependency_v1 &entry =
          dependencies[observer.dependency_begin + dependency];
      if ((entry.kind != OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL &&
           entry.kind != OBELISK_RT_OBSERVER_DEPENDENCY_EVENT) ||
          entry.width == 0 ||
          (entry.kind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT &&
           entry.width != 1))
        return OBELISK_RT_INVALID_FRAME;
      if (entry.kind == OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL) {
        obelisk_rt_stable_handle_v1 decoded;
        if (!obelisk_rt_stable_handle_decode(entry.stable_id, &decoded))
          return OBELISK_RT_INVALID_FRAME;
      }
    }
  }
  if (expectedPrevious != wait->total_size)
    return OBELISK_RT_INVALID_FRAME;
  for (uint32_t index = 0; index != wait->clause_count; ++index) {
    const obelisk_rt_computed_clause_v1 &clause = clauses[index];
    if (clause.primary_observer != index ||
        clause.edge > OBELISK_RT_WAIT_EDGE_BOTH ||
        (clause.flags & ~OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY) != 0)
      return OBELISK_RT_INVALID_FRAME;
    if (clause.condition_observer != OBELISK_RT_OBSERVER_CONDITION_NONE) {
      if (clause.condition_observer < wait->clause_count ||
          clause.condition_observer >= wait->observer_count ||
          usedConditions[clause.condition_observer])
        return OBELISK_RT_INVALID_FRAME;
      usedConditions[clause.condition_observer] = true;
    }
  }
  for (uint32_t index = wait->clause_count; index != wait->observer_count;
       ++index)
    if (!usedConditions[index])
      return OBELISK_RT_INVALID_FRAME;
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
  auto entriesMatch = [&](bool requireEdge,
                          obelisk_rt_wait_edge_kind exactEdge) {
    for (uint32_t index = 0; index != wait->count; ++index) {
      const obelisk_rt_wait_entry_v1 &entry = entries[index];
      if (requireEdge ? entry.reserved == 0 : entry.reserved != 0)
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
    valid = (wait->flags == 0 ||
             wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE) &&
            wait->count == 1 && wait->payload == 0 && wait->auxiliary == 0 &&
            entriesMatch(true, OBELISK_RT_WAIT_EDGE_CHANGE);
    break;
  case OBELISK_RT_SUSPEND_EDGE:
    if (wait->flags == OBELISK_RT_WAIT_EDGE_IFF)
      valid = wait->count == 2 && wait->payload == 0 &&
              wait->auxiliary == 0 &&
              validEdge(entries[0].edge) && entries[0].reserved != 0 &&
              entries[1].edge == OBELISK_RT_WAIT_EDGE_NONE &&
              entries[1].reserved != 0;
    else
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
  case OBELISK_RT_SUSPEND_FOREVER:
    valid = wait->flags == 0 && wait->count == 0 && wait->payload == 0 &&
            wait->auxiliary == 0;
    break;
  case OBELISK_RT_SUSPEND_CHILDREN:
    valid = wait->flags == 0 && wait->count == 0 && wait->payload == 0 &&
            wait->auxiliary == 0;
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
  if (wait->kind == OBELISK_RT_SUSPEND_EDGE && wait->count > 1 &&
      wait->flags == 0)
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
        action.suspend_kind > OBELISK_RT_SUSPEND_OBSERVER)
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
    if (action.flags != 0 ||
        action.suspend_kind != OBELISK_RT_SUSPEND_NONE ||
        action.continuation == 0 || action.payload == 0 ||
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

const obelisk_rt_wait_record_v1 *
currentWait(const ScheduledProcess &process) {
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

bool signalEdgeMatches(uint32_t requested, uint32_t observed) {
  switch (requested) {
  case OBELISK_RT_WAIT_EDGE_CHANGE:
    return (observed & OBELISK_RT_SIGNAL_CHANGE) != 0;
  case OBELISK_RT_WAIT_EDGE_POSEDGE:
    return (observed & OBELISK_RT_SIGNAL_POSEDGE) != 0;
  case OBELISK_RT_WAIT_EDGE_NEGEDGE:
    return (observed & OBELISK_RT_SIGNAL_NEGEDGE) != 0;
  case OBELISK_RT_WAIT_EDGE_BOTH:
    return (observed &
            (OBELISK_RT_SIGNAL_POSEDGE | OBELISK_RT_SIGNAL_NEGEDGE)) != 0;
  default:
    return false;
  }
}

uint32_t transitionEdges(bool oldValue, bool oldUnknown, bool newValue,
                         bool newUnknown) {
  if (oldValue == newValue && oldUnknown == newUnknown)
    return 0;
  uint32_t result = OBELISK_RT_SIGNAL_CHANGE;
  bool oldZero = !oldUnknown && !oldValue;
  bool oldOne = !oldUnknown && oldValue;
  bool newZero = !newUnknown && !newValue;
  bool newOne = !newUnknown && newValue;
  if ((oldZero && !newZero) || (oldUnknown && newOne))
    result |= OBELISK_RT_SIGNAL_POSEDGE;
  if ((oldOne && !newOne) || (oldUnknown && newZero))
    result |= OBELISK_RT_SIGNAL_NEGEDGE;
  return result;
}

bool decodeNativeAutomatic(uint64_t handle, uint32_t &id, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_AUTOMATIC)
    return false;
  id = decoded.id;
  offset = decoded.offset;
  return true;
}

bool decodeNativeGlobal(uint64_t handle, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL)
    return false;
  offset = decoded.offset;
  return true;
}

bool decodeNativeStatic(uint64_t handle, uint32_t &id, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC)
    return false;
  id = decoded.id;
  offset = decoded.offset;
  return true;
}

bool addHandleOffset(int64_t base, uint64_t offset, int64_t &result) {
  if (offset > static_cast<uint64_t>(INT64_MAX) ||
      base > INT64_MAX - static_cast<int64_t>(offset))
    return false;
  result = base + static_cast<int64_t>(offset);
  return true;
}

uint64_t nativeHandleOffset(uint64_t handle, int64_t amount) {
  return obelisk_rt_stable_handle_offset(handle, amount);
}

bool nativeRangesOverlap(uint64_t lhsHandle, uint64_t lhsWidth,
                         uint64_t rhsHandle, uint64_t rhsWidth) {
  uint32_t lhsID = 0, rhsID = 0;
  int64_t lhs = 0, rhs = 0;
  bool lhsAutomatic = decodeNativeAutomatic(lhsHandle, lhsID, lhs);
  bool rhsAutomatic = decodeNativeAutomatic(rhsHandle, rhsID, rhs);
  if (lhsAutomatic != rhsAutomatic || (lhsAutomatic && lhsID != rhsID))
    return false;
  if (!lhsAutomatic) {
    bool lhsStatic = decodeNativeStatic(lhsHandle, lhsID, lhs);
    bool rhsStatic = decodeNativeStatic(rhsHandle, rhsID, rhs);
    if (lhsStatic != rhsStatic || (lhsStatic && lhsID != rhsID))
      return false;
    if (!lhsStatic &&
        (!decodeNativeGlobal(lhsHandle, lhs) ||
         !decodeNativeGlobal(rhsHandle, rhs)))
      return false;
  }
  __int128 lhsEnd = static_cast<__int128>(lhs) + lhsWidth;
  __int128 rhsEnd = static_cast<__int128>(rhs) + rhsWidth;
  return lhsWidth != 0 && rhsWidth != 0 &&
         static_cast<__int128>(lhs) < rhsEnd &&
         static_cast<__int128>(rhs) < lhsEnd;
}

void releaseOwnedNativeStates(obelisk_rt_context *context,
                              obelisk_rt_process_instance_v1 *instance) {
  if (!context || !instance)
    return;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  for (auto state = context->nativeAutomaticStates.begin();
       state != context->nativeAutomaticStates.end();) {
    if (state->second.owner != instance) {
      ++state;
      continue;
    }
    state->second.owner = nullptr;
    if (state->second.referenceCount <= 1) {
      obelisk_rt_erase_automatic_signal_snapshots_unlocked(context,
                                                           state->first);
      state = context->nativeAutomaticStates.erase(state);
    } else {
      --state->second.referenceCount;
      ++state;
    }
  }
  instance->ownership_context = nullptr;
}

bool byteBit(const uint8_t *bytes, uint64_t bit) {
  return (bytes[bit / 8] & static_cast<uint8_t>(1u << (bit % 8))) != 0;
}

void setByteBit(uint8_t *bytes, uint64_t bit, bool value) {
  uint8_t mask = static_cast<uint8_t>(1u << (bit % 8));
  if (value)
    bytes[bit / 8] |= mask;
  else
    bytes[bit / 8] &= static_cast<uint8_t>(~mask);
}

bool nativeWaitReady(const obelisk_rt_context &context,
                     const ScheduledProcess &process) {
  const obelisk_rt_wait_record_v1 *wait = currentWait(process);
  if (!wait)
    return false;
  const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
  switch (process.suspendKind) {
  case OBELISK_RT_SUSPEND_OBSERVER:
    return process.signalTriggered;
  case OBELISK_RT_SUSPEND_CHANGE:
  case OBELISK_RT_SUSPEND_EDGE:
    if (wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE ||
        wait->flags == OBELISK_RT_WAIT_EDGE_IFF)
      return process.signalTriggered;
    for (uint32_t index = 0; index != wait->count; ++index) {
      uint64_t width = entries[index].reserved;
      for (const ScheduledSignalEvent &event : context.scheduledSignalEvents)
        if (event.sequence >= process.observedSignalSequence &&
            nativeRangesOverlap(entries[index].stable_id, width,
                                event.bitOffset, event.bitWidth) &&
            signalEdgeMatches(entries[index].edge, event.edges))
          return true;
    }
    return false;
  case OBELISK_RT_SUSPEND_EVENT:
    if (process.waitGenerations.size() != wait->count)
      return false;
    for (uint32_t index = 0; index != wait->count; ++index) {
      auto found = context.eventGenerations.find(entries[index].stable_id);
      uint64_t generation =
          found == context.eventGenerations.end() ? 0 : found->second;
      if (generation != process.waitGenerations[index])
        return true;
    }
    return false;
  case OBELISK_RT_SUSPEND_AWAIT:
    return wait->count == 1 &&
           context.terminatedNativeProcesses.count(entries[0].stable_id) != 0;
  case OBELISK_RT_SUSPEND_JOIN: {
    bool ready = wait->flags == 0;
    for (uint32_t index = 0; index != wait->count; ++index) {
      bool terminated =
          context.terminatedNativeProcesses.count(entries[index].stable_id) != 0;
      if (wait->flags == 0)
        ready &= terminated;
      else
        ready |= terminated;
    }
    return ready;
  }
  case OBELISK_RT_SUSPEND_FRONTIER:
    return process.observedEpoch != context.schedulerEpoch;
  case OBELISK_RT_SUSPEND_CHILDREN: {
    uint64_t parent = kNativeLogicalProcessTag | process.token;
    for (const ScheduledProcess &child : context.scheduledProcesses)
      if (child.instance && child.parent == parent)
        return false;
    for (const ScheduledDesignTask &child : context.scheduledDesignTasks)
      if (!child.terminated && child.parent == parent)
        return false;
    return true;
  }
  case OBELISK_RT_SUSPEND_FOREVER:
    return false;
  default:
    return false;
  }
}

} // namespace

void obelisk_rt_erase_automatic_signal_snapshots_unlocked(
    obelisk_rt_context *context, uint32_t automaticID) {
  if (!context)
    return;
  for (auto snapshot = context->signalValueSnapshots.begin();
       snapshot != context->signalValueSnapshots.end();) {
    uint32_t id = 0;
    int64_t offset = 0;
    if (decodeNativeAutomatic(snapshot->first, id, offset) &&
        id == automaticID)
      snapshot = context->signalValueSnapshots.erase(snapshot);
    else
      ++snapshot;
  }
}

void obelisk_rt_invalidate_signal_snapshots_unlocked(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth) {
  if (!context || bitWidth == 0)
    return;
  for (auto snapshot = context->signalValueSnapshots.begin();
       snapshot != context->signalValueSnapshots.end();)
    if (nativeRangesOverlap(snapshot->first, 1, bitOffset, bitWidth))
      snapshot = context->signalValueSnapshots.erase(snapshot);
    else
      ++snapshot;
}

ScheduledProcess *findScheduledProcess(obelisk_rt_context *context,
                                       uint64_t token) {
  for (ScheduledProcess &process : context->scheduledProcesses)
    if (process.token == token)
      return &process;
  return nullptr;
}

obelisk_rt_computed_wait_record_v1 *
computedWait(ScheduledProcess &process) {
  if (process.suspendKind != OBELISK_RT_SUSPEND_OBSERVER ||
      !process.instance || !process.instance->frame ||
      process.waitSize < sizeof(obelisk_rt_computed_wait_record_v1) ||
      process.waitOffset > process.instance->frame_size ||
      process.waitSize > process.instance->frame_size - process.waitOffset)
    return nullptr;
  auto *wait = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
      static_cast<uint8_t *>(process.instance->frame) + process.waitOffset);
  return wait->version == OBELISK_RT_VERSION &&
                 wait->kind == OBELISK_RT_SUSPEND_OBSERVER &&
                 wait->total_size <= process.waitSize
             ? wait
             : nullptr;
}

bool evaluateNativeObserver(
    obelisk_rt_context *context, uint64_t processToken,
    obelisk_rt_computed_wait_record_v1 *wait, uint32_t observerIndex,
    std::vector<uint64_t> &value, std::vector<uint64_t> &unknown) {
  if (!context || !wait || observerIndex >= wait->observer_count)
    return false;
  ScheduledProcess *process = findScheduledProcess(context, processToken);
  if (!process || !process->instance || !process->instance->descriptor)
    return false;
  auto *observers = reinterpret_cast<obelisk_rt_computed_observer_v1 *>(
      reinterpret_cast<uint8_t *>(wait) + wait->observers_offset);
  auto *captures = reinterpret_cast<obelisk_rt_computed_capture_v1 *>(
      reinterpret_cast<uint8_t *>(wait) + wait->captures_offset);
  const obelisk_rt_computed_observer_v1 &binding =
      observers[observerIndex];
  const obelisk_rt_observer_descriptor_v1 *descriptor =
      findObserverDescriptor(process->instance->descriptor->execution,
                             binding.code_unit_id);
  if (!descriptor || descriptor->capture_count != binding.capture_count)
    return false;
  if (context->observerDepth >= 256) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint32_t limbs = static_cast<uint32_t>(
      (uint64_t{descriptor->result_width} + 63) / 64);
  value.assign(limbs, 0);
  unknown.assign(limbs, 0);
  std::vector<uint64_t> nativeCaptures(binding.capture_count);
  for (uint32_t index = 0; index != binding.capture_count; ++index)
    nativeCaptures[index] =
        captures[binding.capture_begin + index].stable_id;

  std::vector<uint64_t> retainedCaptures;
  retainedCaptures.reserve(binding.capture_count);
  obelisk_rt_process_instance_v1 *waiter = process->instance;
  if (waiter->observer_pin_count == UINT32_MAX) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  ++waiter->observer_pin_count;
  obelisk_rt_status status = OBELISK_RT_OK;
  for (uint32_t index = 0; index != binding.capture_count; ++index) {
    if (descriptor->capture_abi[index].kind !=
        OBELISK_RT_OBSERVER_CAPTURE_STORAGE)
      continue;
    status =
        obelisk_rt_v1_native_state_retain(context, nativeCaptures[index]);
    if (status != OBELISK_RT_OK)
      break;
    retainedCaptures.push_back(nativeCaptures[index]);
  }
  if (status != OBELISK_RT_OK) {
    for (auto capture = retainedCaptures.rbegin();
         capture != retainedCaptures.rend(); ++capture)
      (void)obelisk_rt_v1_native_state_release(context, *capture, 0);
    --waiter->observer_pin_count;
    context->schedulerStatus = status;
    return false;
  }

  obelisk_rt_process_instance_v1 *producer = context->activeNativeProcess;
  uint64_t producerToken = context->activeLogicalProcessToken;
  uint64_t producerDesignTask = context->activeDesignTaskID;
  bool producerDesignExecuting = context->designTaskExecuting;
  std::vector<uint64_t> producerControls =
      std::move(context->activeControls);
  std::vector<uint64_t> waiterControls = process->controls;
  context->activeNativeProcess = waiter;
  context->activeLogicalProcessToken =
      kNativeLogicalProcessTag | processToken;
  context->activeDesignTaskID = 0;
  context->designTaskExecuting = false;
  context->activeControls = std::move(waiterControls);
  ++context->observerDepth;
  {
    ContextCallbackUnlock unlock(context);
    try {
      if ((waiter->tier == OBELISK_RT_TIER_BYTECODE ||
           !descriptor->native_evaluator) &&
          descriptor->bytecode_function != OBELISK_RT_OBSERVER_NO_BYTECODE) {
        status = obelisk_rt_execute_design_observer(
            *waiter->descriptor->execution, context,
            descriptor->bytecode_function, captures + binding.capture_begin,
            binding.capture_count, value.data(), unknown.data(), limbs);
      } else if (descriptor->native_evaluator) {
        status = descriptor->native_evaluator(
            context, nativeCaptures.data(), binding.capture_count,
            value.data(), unknown.data(), limbs);
      } else {
        status = OBELISK_RT_TIER_UNAVAILABLE;
      }
    } catch (const std::bad_alloc &) {
      status = OBELISK_RT_OUT_OF_MEMORY;
    } catch (...) {
      status = OBELISK_RT_INVALID_ARGUMENT;
    }
  }
  --context->observerDepth;
  waiterControls = std::move(context->activeControls);
  if (ScheduledProcess *updated =
          findScheduledProcess(context, processToken);
      updated && updated->instance == waiter)
    updated->controls = std::move(waiterControls);
  context->activeControls = std::move(producerControls);
  context->activeNativeProcess = producer;
  context->activeLogicalProcessToken = producerToken;
  context->activeDesignTaskID = producerDesignTask;
  context->designTaskExecuting = producerDesignExecuting;
  for (auto capture = retainedCaptures.rbegin();
       capture != retainedCaptures.rend(); ++capture) {
    obelisk_rt_status releaseStatus =
        obelisk_rt_v1_native_state_release(context, *capture, 0);
    if (status == OBELISK_RT_OK && releaseStatus != OBELISK_RT_OK)
      status = releaseStatus;
  }
  --waiter->observer_pin_count;
  if (waiter->observer_pin_count == 0 &&
      waiter->observer_destroy_pending != 0) {
    obelisk_rt_status destroyStatus =
        obelisk_rt_v1_process_instance_destroy(waiter);
    if (status == OBELISK_RT_OK && destroyStatus != OBELISK_RT_OK)
      status = destroyStatus;
  }
  if (status != OBELISK_RT_OK) {
    context->schedulerStatus = status;
    return false;
  }
  if (descriptor->result_width % 64 != 0) {
    uint64_t mask =
        (uint64_t{1} << (descriptor->result_width % 64)) - 1;
    value.back() &= mask;
    unknown.back() &= mask;
  }
  if ((descriptor->flags & OBELISK_RT_OBSERVER_FOUR_STATE) == 0)
    std::fill(unknown.begin(), unknown.end(), 0);
  return true;
}

bool evaluateNativeComputedWaiters(obelisk_rt_context *context,
                                   uint32_t dependencyKind,
                                   uint64_t publishedHandle,
                                   uint64_t publishedWidth) {
  if (!context)
    return false;
  if (context->schedulerStatus != OBELISK_RT_OK)
    return true;
  std::vector<uint64_t> tokens;
  tokens.reserve(context->scheduledProcesses.size());
  for (const ScheduledProcess &process : context->scheduledProcesses)
    if (process.instance && process.started &&
        process.suspendKind == OBELISK_RT_SUSPEND_OBSERVER &&
        !process.signalTriggered)
      tokens.push_back(process.token);

  for (uint64_t token : tokens) {
    ScheduledProcess *process = findScheduledProcess(context, token);
    if (!process || process->signalTriggered)
      continue;
    obelisk_rt_computed_wait_record_v1 *wait = computedWait(*process);
    if (!wait)
      continue;
    auto *observers = reinterpret_cast<obelisk_rt_computed_observer_v1 *>(
        reinterpret_cast<uint8_t *>(wait) + wait->observers_offset);
    auto *dependencies = reinterpret_cast<obelisk_rt_computed_dependency_v1 *>(
        reinterpret_cast<uint8_t *>(wait) + wait->dependencies_offset);
    auto *clauses = reinterpret_cast<obelisk_rt_computed_clause_v1 *>(
        reinterpret_cast<uint8_t *>(wait) + wait->clauses_offset);
    for (uint32_t clauseIndex = 0;
         clauseIndex != wait->clause_count; ++clauseIndex) {
      obelisk_rt_computed_clause_v1 clause = clauses[clauseIndex];
      obelisk_rt_computed_observer_v1 primary =
          observers[clause.primary_observer];
      bool affected = false;
      for (uint32_t dependencyIndex = 0;
           dependencyIndex != primary.dependency_count; ++dependencyIndex) {
        const obelisk_rt_computed_dependency_v1 &dependency =
            dependencies[primary.dependency_begin + dependencyIndex];
        if (dependency.kind != dependencyKind)
          continue;
        if (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT
                ? dependency.stable_id == publishedHandle
                : nativeRangesOverlap(dependency.stable_id, dependency.width,
                                      publishedHandle, publishedWidth)) {
          affected = true;
          break;
        }
      }
      if (!affected)
        continue;
      std::vector<uint64_t> value;
      std::vector<uint64_t> unknown;
      if (!evaluateNativeObserver(context, token, wait,
                                  clause.primary_observer, value, unknown))
        return false;
      process = findScheduledProcess(context, token);
      wait = process ? computedWait(*process) : nullptr;
      if (!process || !process->instance || process->signalTriggered || !wait)
        break;
      const obelisk_rt_observer_descriptor_v1 *descriptor =
          findObserverDescriptor(process->instance->descriptor->execution,
                                 primary.code_unit_id);
      if (!descriptor)
        return false;
      uint32_t limbs = static_cast<uint32_t>(
          (uint64_t{descriptor->result_width} + 63) / 64);
      auto *previousValue = reinterpret_cast<uint64_t *>(
          reinterpret_cast<uint8_t *>(wait) + primary.previous_offset);
      auto *previousUnknown = previousValue + limbs;
      bool changed = false;
      for (uint32_t limb = 0; limb != limbs; ++limb)
        changed |= previousValue[limb] != value[limb] ||
                   previousUnknown[limb] != unknown[limb];
      uint32_t observedEdges = transitionEdges(
          (previousValue[0] & 1) != 0,
          (previousUnknown[0] & 1) != 0,
          (value[0] & 1) != 0, (unknown[0] & 1) != 0);
      for (uint32_t limb = 0; limb != limbs; ++limb) {
        previousValue[limb] = value[limb];
        previousUnknown[limb] = unknown[limb];
      }
      bool occurrence =
          (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT &&
           (clause.flags &
            OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY) != 0) ||
          (clause.edge == OBELISK_RT_WAIT_EDGE_CHANGE
               ? changed
               : signalEdgeMatches(clause.edge, observedEdges));
      if (!occurrence)
        continue;
      if (clause.condition_observer !=
          OBELISK_RT_OBSERVER_CONDITION_NONE) {
        if (!evaluateNativeObserver(context, token, wait,
                                    clause.condition_observer, value,
                                    unknown))
          return false;
        process = findScheduledProcess(context, token);
        wait = process ? computedWait(*process) : nullptr;
        if (!process || !process->instance || process->signalTriggered ||
            !wait)
          break;
        occurrence =
            !value.empty() && (value[0] & 1) != 0 &&
            (unknown.empty() || (unknown[0] & 1) == 0);
      }
      if (occurrence) {
        if (ScheduledProcess *updated =
                findScheduledProcess(context, token);
            updated && updated->instance)
          updated->signalTriggered = true;
        break;
      }
    }
  }
  return context->schedulerStatus == OBELISK_RT_OK;
}

bool obelisk_rt_notify_observer_event_unlocked(obelisk_rt_context *context,
                                               uint64_t stableID) {
  return evaluateNativeComputedWaiters(
             context, OBELISK_RT_OBSERVER_DEPENDENCY_EVENT, stableID, 1) &&
         obelisk_rt_evaluate_design_observers_unlocked(
             context, OBELISK_RT_OBSERVER_DEPENDENCY_EVENT, stableID, 1);
}

bool obelisk_rt_notify_observer_signal_unlocked(obelisk_rt_context *context,
                                                uint64_t stableID,
                                                uint64_t width) {
  return evaluateNativeComputedWaiters(
             context, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, stableID,
             width) &&
         obelisk_rt_evaluate_design_observers_unlocked(
             context, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, stableID,
             width);
}

bool obelisk_rt_append_signal_event_unlocked(
    obelisk_rt_context *context, uint64_t bitOffset, bool oldValue,
    bool oldUnknown, bool newValue, bool newUnknown) {
  return obelisk_rt_append_signal_event_unlocked(
      context, bitOffset, oldValue, oldUnknown, newValue, newUnknown, true);
}

bool obelisk_rt_append_signal_event_unlocked(
    obelisk_rt_context *context, uint64_t bitOffset, bool oldValue,
    bool oldUnknown, bool newValue, bool newUnknown,
    bool evaluateComputedObservers) {
  if (!context)
    return false;
  uint32_t edges =
      transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
  if (edges == 0)
    return true;
  if (context->nextSchedulerSequence == 0) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint64_t sequence = context->nextSchedulerSequence++;
  context->scheduledSignalEvents.push_back({sequence, bitOffset, 1, edges});
  context->signalValueSnapshots[bitOffset] =
      {sequence, newValue, newUnknown};
  if (evaluateComputedObservers &&
      !obelisk_rt_notify_observer_signal_unlocked(context, bitOffset, 1))
    return false;

  auto readBit = [&](uint64_t handle, uint64_t bitIndex, bool &value,
                     bool &unknown) {
    uint32_t id = 0;
    int64_t offset = 0;
    if (decodeNativeAutomatic(handle, id, offset)) {
      auto found = context->nativeAutomaticStates.find(id);
      if (found == context->nativeAutomaticStates.end() || offset < 0 ||
          bitIndex > uint64_t{INT64_MAX} ||
          static_cast<uint64_t>(offset) + bitIndex <
              static_cast<uint64_t>(offset) ||
          static_cast<uint64_t>(offset) + bitIndex >=
              found->second.bitWidth)
        return false;
      uint64_t absolute = static_cast<uint64_t>(offset) + bitIndex;
      value = !found->second.value.empty() &&
              byteBit(found->second.value.data(), absolute);
      unknown = !found->second.unknown.empty() &&
                byteBit(found->second.unknown.data(), absolute);
      return true;
    }
    uint64_t absolute = 0;
    if (decodeNativeStatic(handle, id, offset)) {
      auto found = context->nativeStaticStates.find(id);
      if (found == context->nativeStaticStates.end() || offset < 0 ||
          bitIndex > uint64_t{INT64_MAX} ||
          static_cast<uint64_t>(offset) + bitIndex <
              static_cast<uint64_t>(offset) ||
          static_cast<uint64_t>(offset) + bitIndex >= found->second.bitWidth)
        return false;
      absolute =
          found->second.bitOffset + static_cast<uint64_t>(offset) + bitIndex;
    } else {
      if (!decodeNativeGlobal(handle, offset) || offset < 0 ||
          bitIndex > uint64_t{INT64_MAX} ||
          static_cast<uint64_t>(offset) + bitIndex <
              static_cast<uint64_t>(offset))
        return false;
      absolute = static_cast<uint64_t>(offset) + bitIndex;
    }
    if (absolute >= context->stateValue.size() * uint64_t{64} ||
        absolute >= context->stateUnknown.size() * uint64_t{64})
      return false;
    uint64_t mask = uint64_t{1} << (absolute % 64);
    value = (context->stateValue[absolute / 64] & mask) != 0;
    unknown = (context->stateUnknown[absolute / 64] & mask) != 0;
    return true;
  };
  auto levelTrue = [&](const obelisk_rt_wait_entry_v1 &entry) {
    for (uint64_t index = 0; index != entry.reserved; ++index) {
      bool value = false;
      bool unknown = false;
      uint64_t indexed =
          index <= uint64_t{INT64_MAX}
              ? nativeHandleOffset(entry.stable_id,
                                   static_cast<int64_t>(index))
              : UINT64_MAX;
      const SignalValueSnapshot *snapshot = nullptr;
      for (const auto &[handle, candidate] : context->signalValueSnapshots)
        if (indexed != UINT64_MAX &&
            nativeRangesOverlap(indexed, 1, handle, 1) &&
            (!snapshot || candidate.sequence > snapshot->sequence))
          snapshot = &candidate;
      if (snapshot) {
        value = snapshot->value;
        unknown = snapshot->unknown;
      } else if (!readBit(entry.stable_id, index, value, unknown))
        return false;
      if (value && !unknown)
        return true;
    }
    return false;
  };
  auto consider = [&](const obelisk_rt_wait_record_v1 *wait,
                      uint32_t suspendKind, bool &latched) {
    if (!wait || latched || wait->version != OBELISK_RT_VERSION ||
        wait->kind != suspendKind)
      return;
    const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
    if (wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE && wait->count == 1) {
      if (nativeRangesOverlap(entries[0].stable_id, entries[0].reserved,
                              bitOffset, 1))
        latched = levelTrue(entries[0]);
      return;
    }
    if (wait->flags != OBELISK_RT_WAIT_EDGE_IFF || wait->count != 2 ||
        !nativeRangesOverlap(entries[0].stable_id, entries[0].reserved,
                             bitOffset, 1) ||
        !signalEdgeMatches(entries[0].edge, edges))
      return;
    latched = levelTrue(entries[1]);
  };

  for (ScheduledProcess &process : context->scheduledProcesses)
    if (process.instance && process.started)
      consider(currentWait(process), process.suspendKind,
               process.signalTriggered);
  for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
    const obelisk_rt_wait_record_v1 *wait = nullptr;
    if (task.started && !task.terminated &&
        task.waitSize >= sizeof(obelisk_rt_wait_record_v1) &&
        task.waitOffset <= task.frame.size() &&
        task.waitSize <= task.frame.size() - task.waitOffset)
      wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
          task.frame.data() + task.waitOffset);
    consider(wait, task.suspendKind, task.signalTriggered);
  }
  return true;
}

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
               nullptr,
               nullptr,
               0,
               0};
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
  ContextTransaction transaction(context);
  if (instance->lifecycle == OBELISK_RT_PROCESS_EXECUTING ||
      instance->lifecycle == OBELISK_RT_PROCESS_TERMINATED)
    return OBELISK_RT_INVALID_LIFECYCLE;
  uint32_t mask = requestedTier == OBELISK_RT_TIER_NATIVE
                      ? OBELISK_RT_TIER_MASK_NATIVE
                      : OBELISK_RT_TIER_MASK_BYTECODE;
  if ((instance->descriptor->available_tiers & mask) == 0)
    return OBELISK_RT_TIER_UNAVAILABLE;
  if (context && instance->descriptor->execution &&
      context->execution != instance->descriptor->execution)
    return OBELISK_RT_INVALID_DESIGN;
  if (requestedTier == OBELISK_RT_TIER_BYTECODE &&
      instance->descriptor->design_bytecode && !context)
    return OBELISK_RT_INVALID_DESIGN;
  if (!validContinuation(*instance->descriptor->frame_layout,
                         instance->continuation))
    return OBELISK_RT_INVALID_CONTINUATION;
  if (requestedTier == OBELISK_RT_TIER_BYTECODE) {
    if (!instance->descriptor->bytecode &&
        !instance->descriptor->design_bytecode)
      return OBELISK_RT_TIER_UNAVAILABLE;
    obelisk_rt_status status = instance->descriptor->bytecode
                                   ? obelisk_rt_validate_bytecode_program(
                                         *instance->descriptor->bytecode,
                                         instance->continuation)
                                   : obelisk_rt_validate_design_bytecode(
                                         *instance->descriptor->design_bytecode,
                                         nullptr, nullptr);
    if (status != OBELISK_RT_OK)
      return status;
  }

  if (instance->tier != 0 && instance->tier != requestedTier &&
      instance->tier == OBELISK_RT_TIER_NATIVE && instance->native_handle) {
    instance->descriptor->native_destroy(instance);
    if (instance->native_handle)
      return OBELISK_RT_INVALID_LIFECYCLE;
  }
  bool installedActiveProcess = false;
  if (context) {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->activeNativeProcess &&
        context->activeNativeProcess != instance)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (instance->ownership_context &&
        instance->ownership_context != context)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (!context->activeNativeProcess) {
      context->activeNativeProcess = instance;
      installedActiveProcess = true;
    }
    instance->ownership_context = context;
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
    if (instance->descriptor->design_bytecode) {
      status = obelisk_rt_execute_design_bytecode(
          *instance->descriptor->design_bytecode, context, instance->frame,
          instance->scratch_offset + instance->scratch_size,
          instance->scratch_offset, instance->scratch_size,
          instance->continuation, 0, outAction);
    } else {
      obelisk_rt_fragment_descriptor_v1 descriptor{};
      descriptor.handle = {OBELISK_RT_DESCRIPTOR_FRAGMENT, 0,
                           instance->descriptor->handle.id};
      descriptor.code_kind = OBELISK_RT_FRAGMENT_BYTECODE;
      descriptor.code.bytecode = *instance->descriptor->bytecode;
      status = obelisk_rt_v1_fragment_execute(
          &descriptor, context, instance->frame,
          instance->scratch_offset + instance->scratch_size,
          instance->continuation, outAction);
    }
  }

  if (installedActiveProcess) {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->activeNativeProcess == instance)
      context->activeNativeProcess = nullptr;
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
  if (instance->lifecycle == OBELISK_RT_PROCESS_TERMINATED &&
      instance->ownership_context)
    releaseOwnedNativeStates(instance->ownership_context, instance);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_process_instance_destroy(
    obelisk_rt_process_instance_v1 *instance) {
  if (!instance)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (instance->lifecycle == OBELISK_RT_PROCESS_EXECUTING)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (instance->observer_pin_count != 0) {
    instance->observer_destroy_pending = 1;
    return OBELISK_RT_OK;
  }
  if (instance->ownership_context)
    releaseOwnedNativeStates(instance->ownership_context, instance);
  if (instance->native_handle) {
    instance->descriptor->native_destroy(instance);
    if (instance->native_handle)
      return OBELISK_RT_INVALID_LIFECYCLE;
  }
  std::free(instance);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_add(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t phase) {
  return obelisk_rt_v1_scheduler_add_ranked(context, instance, phase,
                                             UINT32_MAX);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_add_ranked(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t phase, uint32_t scheduleRank) {
  return obelisk_rt_v1_scheduler_add_planned(
      context, instance, phase, scheduleRank, nullptr, nullptr, 0);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_add_planned(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t phase, uint32_t initialRank, const uint32_t *continuations,
    const uint32_t *ranks, uint32_t continuationCount) {
  if (!context || !instance || phase > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (continuationCount != 0 && (!continuations || !ranks))
    return OBELISK_RT_INVALID_ARGUMENT;
  for (uint32_t index = 0; index != continuationCount; ++index)
    if (continuations[index] == 0 ||
        (index != 0 && continuations[index - 1] >= continuations[index]))
      return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    ScheduledProcess process;
    process.instance = instance;
    if (context->nextNativeProcessToken == 0 ||
        context->nextProcessInsertionSequence == 0 ||
        context->nextProcessInsertionSequence == UINT64_MAX) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    if (context->nextNativeProcessToken >= kNativeLogicalProcessTag) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    process.token = context->nextNativeProcessToken++;
    process.parent = context->activeLogicalProcessToken;
    process.controls = context->activeControls;
    process.insertionSequence = context->nextProcessInsertionSequence++;
    process.observedEpoch = context->schedulerEpoch;
    process.observedSignalSequence = context->nextSchedulerSequence;
    process.phase = phase;
    process.scheduleRank = initialRank;
    if (context->execution &&
        (context->execution->flags &
         OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0) {
      if ((instance->descriptor->available_tiers &
           OBELISK_RT_TIER_MASK_BYTECODE) == 0) {
        context->schedulerStatus = OBELISK_RT_TIER_UNAVAILABLE;
        return OBELISK_RT_TIER_UNAVAILABLE;
      }
      instance->tier = OBELISK_RT_TIER_BYTECODE;
    }
    process.continuationRanks.reserve(continuationCount);
    for (uint32_t index = 0; index != continuationCount; ++index)
      process.continuationRanks.emplace_back(continuations[index],
                                             ranks[index]);
    context->scheduledProcesses.push_back(std::move(process));
    obelisk_rt_retain_controls_unlocked(
        context, context->scheduledProcesses.back().controls);
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" uint64_t obelisk_rt_v1_scheduler_process_token(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance) {
  if (!context || !instance)
    return 0;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    for (const ScheduledProcess &process : context->scheduledProcesses)
      if (process.instance == instance)
        return process.token;
  } catch (...) {
  }
  return 0;
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_nba(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto fail = [&](obelisk_rt_status status) {
    obelisk_rt_v1_scheduler_fail(context, status);
    return status;
  };
  if (!valuePlane || bitWidth == 0 || (bitWidth + 7) < bitWidth)
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  if (bitOffset == UINT64_MAX)
    return OBELISK_RT_OK;
  ContextTransaction transaction(context);
  uint32_t automaticID = 0;
  uint32_t staticID = 0;
  int64_t offset = 0;
  bool automatic = decodeNativeAutomatic(bitOffset, automaticID, offset);
  bool boundedStatic = !automatic && decodeNativeStatic(bitOffset, staticID,
                                                         offset);
  if (!automatic && !boundedStatic && !decodeNativeGlobal(bitOffset, offset))
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  if (!automatic && !boundedStatic &&
      (offset >= static_cast<__int128>(planeBitCount) ||
       static_cast<__int128>(offset) + bitWidth <= 0))
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (!value || (unknownPlane && !unknown) ||
      byteCount > std::numeric_limits<size_t>::max())
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  try {
    ScheduledNBA update;
    update.valuePlane = valuePlane;
    update.unknownPlane = unknownPlane;
    update.planeBitCount = planeBitCount;
    update.bitOffset = bitOffset;
    update.bitWidth = bitWidth;
    update.value.assign(value, value + static_cast<size_t>(byteCount));
    if (unknownPlane)
      update.unknown.assign(unknown, unknown + static_cast<size_t>(byteCount));
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (automatic) {
      auto found = context->nativeAutomaticStates.find(automaticID);
      if (found == context->nativeAutomaticStates.end() ||
          offset >= static_cast<__int128>(found->second.bitWidth) ||
          static_cast<__int128>(offset) + bitWidth <= 0) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
      if (found->second.referenceCount == UINT64_MAX) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return OBELISK_RT_OUT_OF_RESOURCES;
      }
      update.retainedAutomaticID = automaticID;
    } else if (boundedStatic) {
      auto found = context->nativeStaticStates.find(staticID);
      if (found == context->nativeStaticStates.end() ||
          found->second.bitOffset > planeBitCount ||
          found->second.bitWidth > planeBitCount - found->second.bitOffset ||
          offset >= static_cast<__int128>(found->second.bitWidth) ||
          static_cast<__int128>(offset) + bitWidth <= 0) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
    }
    if (context->nextSchedulerSequence == 0) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    update.sequence = context->nextSchedulerSequence++;
    update.dueTime = delay > UINT64_MAX - context->schedulerTime
                         ? UINT64_MAX
                         : context->schedulerTime + delay;
    context->scheduledNBAs.push_back(std::move(update));
    if (automatic)
      ++context->nativeAutomaticStates.find(automaticID)->second.referenceCount;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" void obelisk_rt_v1_scheduler_signal(obelisk_rt_context *context,
                                                  uint64_t bitOffset,
                                                  uint64_t bitWidth,
                                                  uint32_t edges) {
  if (!context || bitOffset == UINT64_MAX || bitWidth == 0 || edges == 0 ||
      (edges & ~(OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE |
                 OBELISK_RT_SIGNAL_NEGEDGE)) != 0)
    return;
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    uint32_t objectID = 0;
    int64_t offset = 0;
    uint64_t objectWidth = 0;
    if (decodeNativeAutomatic(bitOffset, objectID, offset)) {
      auto found = context->nativeAutomaticStates.find(objectID);
      if (found == context->nativeAutomaticStates.end())
        return;
      objectWidth = found->second.bitWidth;
    } else if (decodeNativeStatic(bitOffset, objectID, offset)) {
      auto found = context->nativeStaticStates.find(objectID);
      if (found == context->nativeStaticStates.end())
        return;
      objectWidth = found->second.bitWidth;
    } else if (!decodeNativeGlobal(bitOffset, offset)) {
      return;
    }
    if (objectWidth != 0 &&
        (offset >= static_cast<__int128>(objectWidth) ||
         static_cast<__int128>(offset) + bitWidth <= 0))
      return;
    if (context->nextSchedulerSequence == 0) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return;
    }
    context->scheduledSignalEvents.push_back(
        {context->nextSchedulerSequence++, bitOffset, bitWidth, edges});
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  } catch (const std::bad_alloc &) {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" void obelisk_rt_v1_scheduler_signal_transition(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth,
    const uint8_t *oldValue, const uint8_t *oldUnknown,
    const uint8_t *newValue, const uint8_t *newUnknown) {
  if (!context || bitOffset == UINT64_MAX || bitWidth == 0 || !oldValue ||
      !newValue)
    return;
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    obelisk_rt_invalidate_signal_snapshots_unlocked(context, bitOffset,
                                                    bitWidth);
    bool changed = false;
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      bool oldValueBit = byteBit(oldValue, bit);
      bool oldUnknownBit = oldUnknown && byteBit(oldUnknown, bit);
      bool newValueBit = byteBit(newValue, bit);
      bool newUnknownBit = newUnknown && byteBit(newUnknown, bit);
      uint32_t edges = transitionEdges(oldValueBit, oldUnknownBit, newValueBit,
                                       newUnknownBit);
      if (edges == 0)
        continue;
      if (bit > static_cast<uint64_t>(INT64_MAX))
        continue;
      uint64_t eventHandle = nativeHandleOffset(bitOffset,
                                                static_cast<int64_t>(bit));
      uint32_t automaticID = 0;
      uint32_t staticID = 0;
      int64_t eventOffset = 0;
      bool automatic =
          decodeNativeAutomatic(eventHandle, automaticID, eventOffset);
      bool boundedStatic =
          !automatic && decodeNativeStatic(eventHandle, staticID, eventOffset);
      bool inRange = eventOffset >= 0;
      if (automatic) {
        auto found = context->nativeAutomaticStates.find(automaticID);
        inRange &= found != context->nativeAutomaticStates.end() &&
                   static_cast<uint64_t>(eventOffset) < found->second.bitWidth;
      } else if (boundedStatic) {
        auto found = context->nativeStaticStates.find(staticID);
        inRange &= found != context->nativeStaticStates.end() &&
                   static_cast<uint64_t>(eventOffset) < found->second.bitWidth;
      } else {
        inRange &= decodeNativeGlobal(eventHandle, eventOffset);
      }
      if (!inRange)
        continue;
      if (!obelisk_rt_append_signal_event_unlocked(
              context, eventHandle, oldValueBit, oldUnknownBit, newValueBit,
              newUnknownBit, false))
        return;
      changed = true;
    }
    if (changed &&
        !obelisk_rt_notify_observer_signal_unlocked(context, bitOffset,
                                                    bitWidth))
      return;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
  }
}

extern "C" void obelisk_rt_v1_scheduler_event(obelisk_rt_context *context,
                                                uint64_t stableID,
                                                uint32_t nonblocking) {
  obelisk_rt_v1_scheduler_event_after(context, stableID, nonblocking, 0);
}

extern "C" void obelisk_rt_v1_scheduler_event_after(
    obelisk_rt_context *context, uint64_t stableID, uint32_t nonblocking,
    uint64_t delay) {
  if (!context || nonblocking > 1 || (!nonblocking && delay != 0)) {
    if (context)
      obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return;
  }
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    if (nonblocking) {
      if (context->nextSchedulerSequence == 0) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return;
      }
      uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                             ? UINT64_MAX
                             : context->schedulerTime + delay;
      context->scheduledDesignEvents.push_back(
          {context->nextSchedulerSequence++, dueTime, stableID});
      return;
    }
    uint64_t &generation = context->eventGenerations[stableID];
    if (++generation == 0)
      generation = 1;
    context->eventLastTriggeredTimes[stableID] = context->schedulerTime;
    if (!obelisk_rt_notify_observer_event_unlocked(context, stableID))
      return;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  } catch (const std::bad_alloc &) {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" uint32_t obelisk_rt_v1_scheduler_event_triggered(
    obelisk_rt_context *context, uint64_t stableID) {
  if (!context)
    return 0;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto found = context->eventLastTriggeredTimes.find(stableID);
    return found != context->eventLastTriggeredTimes.end() &&
           found->second == context->schedulerTime;
  } catch (...) {
    return 0;
  }
}

extern "C" void obelisk_rt_v1_scheduler_fail(obelisk_rt_context *context,
                                                obelisk_rt_status status) {
  if (!context || status == OBELISK_RT_OK)
    return;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->schedulerStatus == OBELISK_RT_OK)
      context->schedulerStatus = status;
  } catch (...) {
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_register_static(
    obelisk_rt_context *context, uint32_t id, uint64_t bitOffset,
    uint64_t bitWidth) {
  if (!context || id == 0 ||
      id > OBELISK_RT_STABLE_HANDLE_MAX_STATIC_ID || bitWidth == 0 ||
      bitOffset > UINT64_MAX - bitWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    NativeStaticState state{bitOffset, bitWidth};
    auto [entry, inserted] = context->nativeStaticStates.emplace(id, state);
    if (!inserted && (entry->second.bitOffset != bitOffset ||
                      entry->second.bitWidth != bitWidth)) {
      context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
      return OBELISK_RT_INVALID_ARGUMENT;
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" uint64_t obelisk_rt_v1_native_state_static_handle(uint32_t id) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, id,
                                         0);
}

extern "C" uint64_t obelisk_rt_v1_native_handle_offset(uint64_t handle,
                                                          int64_t offset) {
  return nativeHandleOffset(handle, offset);
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_alloc(
    obelisk_rt_context *context, uint64_t bitWidth, const uint8_t *value,
    const uint8_t *unknown, uint64_t *outHandle) {
  if (!outHandle)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outHandle = UINT64_MAX;
  if (!context || !value || bitWidth == 0 || bitWidth > INT32_MAX ||
      bitWidth > UINT64_MAX - 7)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (byteCount > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    NativeAutomaticState state;
    state.bitWidth = bitWidth;
    state.value.assign(value, value + static_cast<size_t>(byteCount));
    if (unknown)
      state.unknown.assign(unknown, unknown + static_cast<size_t>(byteCount));
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    state.owner = context->activeNativeProcess;
    if (!state.owner)
      state.designOwner = context->activeDesignTaskID;
    uint32_t id = context->nextNativeAutomaticID;
    if (id == 0 || id > OBELISK_RT_STABLE_HANDLE_MAX_AUTOMATIC_ID) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    context->nativeAutomaticStates.emplace(id, std::move(state));
    ++context->nextNativeAutomaticID;
    *outHandle = obelisk_rt_stable_handle_encode(
        OBELISK_RT_STABLE_HANDLE_AUTOMATIC, id, 0);
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_retain(
    obelisk_rt_context *context, uint64_t handle) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (handle == UINT64_MAX)
    return OBELISK_RT_OK;
  uint32_t id = 0;
  int64_t offset = 0;
  if (!decodeNativeAutomatic(handle, id, offset)) {
    uint32_t staticID = 0;
    return decodeNativeStatic(handle, staticID, offset) ||
                   decodeNativeGlobal(handle, offset)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_HANDLE;
  }
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto found = context->nativeAutomaticStates.find(id);
    if (found == context->nativeAutomaticStates.end())
      return OBELISK_RT_INVALID_HANDLE;
    if (found->second.referenceCount == UINT64_MAX) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    ++found->second.referenceCount;
    return OBELISK_RT_OK;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_release(
    obelisk_rt_context *context, uint64_t handle, uint32_t ownerReference) {
  if (!context || ownerReference > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (handle == UINT64_MAX)
    return OBELISK_RT_OK;
  uint32_t id = 0;
  int64_t offset = 0;
  if (!decodeNativeAutomatic(handle, id, offset)) {
    uint32_t staticID = 0;
    return decodeNativeStatic(handle, staticID, offset) ||
                   decodeNativeGlobal(handle, offset)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_HANDLE;
  }
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto found = context->nativeAutomaticStates.find(id);
    if (found == context->nativeAutomaticStates.end() ||
        found->second.referenceCount == 0)
      return OBELISK_RT_INVALID_HANDLE;
    if (ownerReference) {
      bool nativeOwner = found->second.owner &&
                         found->second.owner == context->activeNativeProcess;
      bool designOwner = found->second.designOwner != 0 &&
                         found->second.designOwner ==
                             context->activeDesignTaskID;
      if (!nativeOwner && !designOwner)
        return OBELISK_RT_INVALID_LIFECYCLE;
      found->second.owner = nullptr;
      found->second.designOwner = 0;
    }
    if (--found->second.referenceCount == 0) {
      obelisk_rt_erase_automatic_signal_snapshots_unlocked(context, id);
      context->nativeAutomaticStates.erase(found);
    }
    return OBELISK_RT_OK;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_load_plane(
    obelisk_rt_context *context, const uint8_t *globalPlane,
    uint64_t globalBitCount, uint64_t handle, uint64_t bitWidth,
    uint32_t unknownPlane, uint32_t fallback, uint8_t *outValue) {
  if (!context || !globalPlane || !outValue || bitWidth == 0 ||
      bitWidth > UINT64_MAX - 7 || unknownPlane > 1 || fallback > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (byteCount > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memset(outValue, fallback ? UINT8_MAX : 0,
              static_cast<size_t>(byteCount));
  auto maskPadding = [&] {
    if (uint32_t remainder = static_cast<uint32_t>(bitWidth % 8))
      outValue[byteCount - 1] &= static_cast<uint8_t>((1u << remainder) - 1);
  };
  if (handle == UINT64_MAX) {
    maskPadding();
    return OBELISK_RT_OK;
  }
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    uint32_t id = 0;
    int64_t offset = 0;
    if (decodeNativeAutomatic(handle, id, offset)) {
      auto found = context->nativeAutomaticStates.find(id);
      if (found == context->nativeAutomaticStates.end()) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
      const NativeAutomaticState &state = found->second;
      const std::vector<uint8_t> &plane = unknownPlane ? state.unknown : state.value;
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        int64_t source = 0;
        if (addHandleOffset(offset, bit, source) && source >= 0 &&
            static_cast<uint64_t>(source) < state.bitWidth && !plane.empty())
          setByteBit(outValue, bit,
                     byteBit(plane.data(), static_cast<uint64_t>(source)));
      }
      maskPadding();
      return OBELISK_RT_OK;
    }
    uint64_t rootOffset = 0;
    uint64_t rootWidth = globalBitCount;
    int64_t globalOffset = 0;
    uint32_t staticID = 0;
    if (decodeNativeStatic(handle, staticID, globalOffset)) {
      auto found = context->nativeStaticStates.find(staticID);
      if (found == context->nativeStaticStates.end() ||
          found->second.bitOffset > globalBitCount ||
          found->second.bitWidth > globalBitCount - found->second.bitOffset) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
      rootOffset = found->second.bitOffset;
      rootWidth = found->second.bitWidth;
    } else if (!decodeNativeGlobal(handle, globalOffset)) {
      context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
      return OBELISK_RT_INVALID_HANDLE;
    }
    bool canonical = context->execution &&
                     context->execution->state_bit_count == globalBitCount;
    const std::vector<uint64_t> *canonicalPlane = nullptr;
    if (canonical) {
      canonicalPlane = unknownPlane ? &context->stateUnknown
                                    : &context->stateValue;
      if (canonicalPlane->size() != (globalBitCount + 63) / 64)
        return OBELISK_RT_INVALID_DESIGN;
    }
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      int64_t coordinate = 0;
      if (addHandleOffset(globalOffset, bit, coordinate) && coordinate >= 0 &&
          static_cast<uint64_t>(coordinate) < rootWidth) {
        uint64_t source = rootOffset + static_cast<uint64_t>(coordinate);
        setByteBit(outValue, bit,
                   canonical
                       ? (((*canonicalPlane)[source / 64] >> (source % 64)) &
                          1) != 0
                       : byteBit(globalPlane, source));
      }
    }
    maskPadding();
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_store_plane(
    obelisk_rt_context *context, uint8_t *globalPlane,
    uint64_t globalBitCount, uint64_t handle, uint64_t bitWidth,
    uint32_t unknownPlane, const uint8_t *value, uint8_t *outChanged) {
  if (!context || !globalPlane || !value || !outChanged || bitWidth == 0 ||
      bitWidth > UINT64_MAX - 7 || unknownPlane > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (byteCount > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  *outChanged = 0;
  if (handle == UINT64_MAX)
    return OBELISK_RT_OK;
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    uint32_t id = 0;
    int64_t offset = 0;
    if (decodeNativeAutomatic(handle, id, offset)) {
      auto found = context->nativeAutomaticStates.find(id);
      if (found == context->nativeAutomaticStates.end()) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
      NativeAutomaticState &state = found->second;
      std::vector<uint8_t> &plane = unknownPlane ? state.unknown : state.value;
      if (plane.empty())
        return OBELISK_RT_INVALID_HANDLE;
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        int64_t coordinate = 0;
        if (!addHandleOffset(offset, bit, coordinate) || coordinate < 0 ||
            static_cast<uint64_t>(coordinate) >= state.bitWidth)
          continue;
        uint64_t destination = static_cast<uint64_t>(coordinate);
        bool old = byteBit(plane.data(), destination);
        bool next = byteBit(value, bit);
        *outChanged |= old != next;
        setByteBit(plane.data(), destination, next);
      }
      return OBELISK_RT_OK;
    }
    uint64_t rootOffset = 0;
    uint64_t rootWidth = globalBitCount;
    int64_t globalOffset = 0;
    uint32_t staticID = 0;
    if (decodeNativeStatic(handle, staticID, globalOffset)) {
      auto found = context->nativeStaticStates.find(staticID);
      if (found == context->nativeStaticStates.end() ||
          found->second.bitOffset > globalBitCount ||
          found->second.bitWidth > globalBitCount - found->second.bitOffset) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
      rootOffset = found->second.bitOffset;
      rootWidth = found->second.bitWidth;
    } else if (!decodeNativeGlobal(handle, globalOffset)) {
      context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
      return OBELISK_RT_INVALID_HANDLE;
    }
    bool canonical = context->execution &&
                     context->execution->state_bit_count == globalBitCount;
    std::vector<uint64_t> *canonicalPlane = nullptr;
    if (canonical) {
      canonicalPlane = unknownPlane ? &context->stateUnknown
                                    : &context->stateValue;
      if (canonicalPlane->size() != (globalBitCount + 63) / 64)
        return OBELISK_RT_INVALID_DESIGN;
    }
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      int64_t coordinate = 0;
      if (!addHandleOffset(globalOffset, bit, coordinate) || coordinate < 0 ||
          static_cast<uint64_t>(coordinate) >= rootWidth)
        continue;
      uint64_t destination = rootOffset + static_cast<uint64_t>(coordinate);
      bool old = canonical
                     ? (((*canonicalPlane)[destination / 64] >>
                         (destination % 64)) & 1) != 0
                     : byteBit(globalPlane, destination);
      bool next = byteBit(value, bit);
      *outChanged |= old != next;
      setByteBit(globalPlane, destination, next);
      if (canonical) {
        uint64_t mask = uint64_t{1} << (destination % 64);
        uint64_t &limb = (*canonicalPlane)[destination / 64];
        limb = next ? limb | mask : limb & ~mask;
      }
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" void obelisk_rt_v1_scheduler_notify(obelisk_rt_context *context) {
  if (!context)
    return;
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  } catch (...) {
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_finish(obelisk_rt_context *context,
                               uint32_t verbosity) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    context->schedulerFinishRequested = true;
    context->schedulerFinishVerbosity = verbosity;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_fatal(obelisk_rt_context *context,
                              uint32_t verbosity) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    context->schedulerFinishRequested = true;
    context->schedulerFinishVerbosity = verbosity;
    context->schedulerFinishStatus = OBELISK_RT_FATAL;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" uint32_t
obelisk_rt_v1_scheduler_termination_requested(obelisk_rt_context *context) {
  if (!context)
    return 0;
  ContextTransaction transaction(context);
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    return context->schedulerFinishRequested ? 1u : 0u;
  } catch (...) {
    return 0;
  }
}

obelisk_rt_status runScheduler(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  for (;;) {
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->destroyPending)
        return OBELISK_RT_OK;
      if (context->schedulerStatus != OBELISK_RT_OK)
        return context->schedulerStatus;
      if (context->schedulerFinishRequested)
        context->schedulerRunningFinals = true;
    }
    uint32_t nativeRegion = UINT32_MAX;
    uint32_t nativeRank = UINT32_MAX;
    uint64_t nativeInsertionSequence = UINT64_MAX;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      for (const ScheduledProcess &candidate : context->scheduledProcesses) {
        if (!candidate.instance ||
            candidate.phase != (context->schedulerRunningFinals ? 1u : 0u))
          continue;
        bool runnable =
            !candidate.started ||
            candidate.suspendKind == OBELISK_RT_SUSPEND_NONE ||
            (candidate.suspendKind == OBELISK_RT_SUSPEND_DELAY
                 ? candidate.wakeTime <= context->schedulerTime
                 : nativeWaitReady(*context, candidate));
        if (runnable && candidate.urgent) {
          nativeRegion = 0;
          nativeRank = 0;
          nativeInsertionSequence = 0;
          break;
        }
        auto key =
            std::tuple{candidate.queuedRegion, candidate.scheduleRank,
                       candidate.insertionSequence};
        if (runnable &&
            key < std::tuple{nativeRegion, nativeRank,
                             nativeInsertionSequence}) {
          nativeRegion = candidate.queuedRegion;
          nativeRank = candidate.scheduleRank;
          nativeInsertionSequence = candidate.insertionSequence;
        }
      }
    }
    bool designProgress = false;
    obelisk_rt_status designStatus =
        obelisk_rt_run_one_design_task(context, nativeRegion, nativeRank,
                                       nativeInsertionSequence,
                                       &designProgress);
    if (designStatus != OBELISK_RT_OK)
      return designStatus;
    if (designProgress)
      continue;
    obelisk_rt_process_instance_v1 *selected = nullptr;
    size_t selectedIndex = 0;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      const size_t processCount = context->scheduledProcesses.size();
      uint32_t selectedRank = UINT32_MAX;
      uint32_t selectedRegion = UINT32_MAX;
      uint64_t selectedInsertionSequence = UINT64_MAX;
      for (size_t step = 0; step < processCount; ++step) {
        size_t index = (context->schedulerCursor + step) % processCount;
        ScheduledProcess &candidate = context->scheduledProcesses[index];
        if (!candidate.instance ||
            candidate.phase != (context->schedulerRunningFinals ? 1u : 0u))
          continue;
        bool runnable = !candidate.started ||
                        candidate.suspendKind == OBELISK_RT_SUSPEND_NONE ||
                        (candidate.suspendKind == OBELISK_RT_SUSPEND_DELAY
                             ? candidate.wakeTime <= context->schedulerTime
                             : nativeWaitReady(*context, candidate));
        if (!runnable)
          continue;
        if (candidate.urgent) {
          selected = candidate.instance;
          selectedIndex = index;
          selectedRank = 0;
          selectedRegion = 0;
          selectedInsertionSequence = 0;
          break;
        }
        auto key =
            std::tuple{candidate.queuedRegion, candidate.scheduleRank,
                       candidate.insertionSequence};
        if (selected &&
            !(key < std::tuple{selectedRegion, selectedRank,
                               selectedInsertionSequence}))
          continue;
        selected = candidate.instance;
        selectedIndex = index;
        selectedRank = candidate.scheduleRank;
        selectedRegion = candidate.queuedRegion;
        selectedInsertionSequence = candidate.insertionSequence;
      }
      if (selected) {
        context->schedulerCursor = (selectedIndex + 1) % processCount;
        ScheduledProcess &candidate =
            context->scheduledProcesses[selectedIndex];
        candidate.started = true;
        candidate.observedEpoch = context->schedulerEpoch;
      }
      uint64_t oldestSignal = context->nextSchedulerSequence;
      for (const ScheduledProcess &candidate : context->scheduledProcesses)
        if (candidate.instance && candidate.started &&
            (candidate.suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
             candidate.suspendKind == OBELISK_RT_SUSPEND_EDGE))
          oldestSignal =
              std::min(oldestSignal, candidate.observedSignalSequence);
      context->scheduledSignalEvents.erase(
          std::remove_if(context->scheduledSignalEvents.begin(),
                         context->scheduledSignalEvents.end(),
                         [&](const ScheduledSignalEvent &event) {
                           return event.sequence < oldestSignal;
                         }),
          context->scheduledSignalEvents.end());
      bool hasDueNativeNBA = false;
      for (const ScheduledNBA &update : context->scheduledNBAs)
        hasDueNativeNBA |= update.dueTime <= context->schedulerTime;
      bool hasDueDesignNBA = false;
      for (const ScheduledDesignNBA &update : context->scheduledDesignNBAs)
        hasDueDesignNBA |= update.dueTime <= context->schedulerTime;
      bool hasDueDesignEvent = false;
      for (const ScheduledDesignEvent &event :
           context->scheduledDesignEvents)
        hasDueDesignEvent |= event.dueTime <= context->schedulerTime;
      if (!selected && !context->schedulerRunningFinals &&
          (hasDueNativeNBA || hasDueDesignNBA || hasDueDesignEvent)) {
        bool changed = false;
        bool eventTriggered = false;
        auto applyNative = [&](const ScheduledNBA &update) {
          bool publicationChanged = false;
          uint32_t automaticID = 0;
          uint32_t staticID = 0;
          int64_t baseOffset = 0;
          bool automatic = decodeNativeAutomatic(
              update.bitOffset, automaticID, baseOffset);
          NativeAutomaticState *automaticState = nullptr;
          const NativeStaticState *staticState = nullptr;
          if (automatic) {
            auto found = context->nativeAutomaticStates.find(automaticID);
            if (found == context->nativeAutomaticStates.end()) {
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
              return;
            }
            automaticState = &found->second;
          } else if (decodeNativeStatic(update.bitOffset, staticID,
                                        baseOffset)) {
            auto found = context->nativeStaticStates.find(staticID);
            if (found == context->nativeStaticStates.end() ||
                found->second.bitOffset > update.planeBitCount ||
                found->second.bitWidth >
                    update.planeBitCount - found->second.bitOffset) {
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
              return;
            }
            staticState = &found->second;
          } else if (!decodeNativeGlobal(update.bitOffset, baseOffset)) {
            context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
            return;
          }
          bool canonical = !automatic && context->execution &&
                           context->execution->state_bit_count ==
                               update.planeBitCount;
          if (canonical &&
              (context->stateValue.size() !=
                   (update.planeBitCount + 63) / 64 ||
               context->stateUnknown.size() != context->stateValue.size())) {
            context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
            return;
          }
          for (uint64_t bit = 0; bit < update.bitWidth; ++bit) {
            uint64_t sourceByte = bit / 8;
            uint8_t sourceMask = static_cast<uint8_t>(1u << (bit % 8));
            int64_t coordinate = 0;
            if (!addHandleOffset(baseOffset, bit, coordinate) ||
                coordinate < 0)
              continue;
            uint64_t localBit = static_cast<uint64_t>(coordinate);
            if (automatic) {
              if (localBit >= automaticState->bitWidth)
                continue;
            } else if (staticState) {
              if (localBit >= staticState->bitWidth)
                continue;
            } else if (localBit >= update.planeBitCount) {
              continue;
            }
            uint64_t planeBit = staticState ? staticState->bitOffset + localBit
                                            : localBit;
            uint64_t storageBit = automatic ? localBit : planeBit;
            uint64_t destinationByte = storageBit / 8;
            uint8_t destinationMask =
                static_cast<uint8_t>(1u << (storageBit % 8));
            auto read = [&](uint8_t *globalPlane, bool unknown) {
              if (automatic) {
                const std::vector<uint8_t> &plane =
                    unknown ? automaticState->unknown : automaticState->value;
                return !plane.empty() &&
                       (plane[destinationByte] & destinationMask) != 0;
              }
              if (canonical) {
                const std::vector<uint64_t> &plane =
                    unknown ? context->stateUnknown : context->stateValue;
                return ((plane[planeBit / 64] >> (planeBit % 64)) & 1) != 0;
              }
              return globalPlane &&
                     (globalPlane[destinationByte] & destinationMask) != 0;
            };
            bool oldValue = read(update.valuePlane, false);
            bool oldUnknown = read(update.unknownPlane, true);
            bool newValue = (update.value[sourceByte] & sourceMask) != 0;
            bool newUnknown = !update.unknown.empty() &&
                              (update.unknown[sourceByte] & sourceMask) != 0;
            auto apply = [&](uint8_t *globalPlane, bool unknown, bool value) {
              uint8_t *plane = globalPlane;
              if (automatic) {
                std::vector<uint8_t> &storage =
                    unknown ? automaticState->unknown : automaticState->value;
                if (storage.empty())
                  return;
                plane = storage.data();
              }
              if (plane) {
                uint8_t old = plane[destinationByte];
                uint8_t next =
                    value ? old | destinationMask
                          : old & static_cast<uint8_t>(~destinationMask);
                plane[destinationByte] = next;
              }
              if (canonical) {
                std::vector<uint64_t> &storage =
                    unknown ? context->stateUnknown : context->stateValue;
                uint64_t mask = uint64_t{1} << (planeBit % 64);
                uint64_t &limb = storage[planeBit / 64];
                limb = value ? limb | mask : limb & ~mask;
              }
            };
            apply(update.valuePlane, false, newValue);
            apply(update.unknownPlane, true, newUnknown);
            uint32_t edges = transitionEdges(oldValue, oldUnknown, newValue,
                                             newUnknown);
            if (edges != 0) {
              changed = true;
              publicationChanged = true;
              if (context->nextSchedulerSequence == 0) {
                context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
                return;
              }
              uint64_t destinationHandle =
                  bit <= static_cast<uint64_t>(INT64_MAX)
                      ? nativeHandleOffset(update.bitOffset,
                                           static_cast<int64_t>(bit))
                      : UINT64_MAX;
              if (destinationHandle == UINT64_MAX)
                continue;
              context->scheduledSignalEvents.push_back(
                  {context->nextSchedulerSequence++, destinationHandle, 1,
                   edges});
            }
          }
          if (publicationChanged &&
              !obelisk_rt_notify_observer_signal_unlocked(
                  context, update.bitOffset, update.bitWidth))
            return;
        };
        auto planeBit = [](const std::vector<uint64_t> &plane,
                           uint64_t bit) {
          return bit / 64 < plane.size() &&
                 ((plane[bit / 64] >> (bit % 64)) & 1) != 0;
        };
        auto applyDesign = [&](const ScheduledDesignNBA &update) {
          if (update.handleKind != OBELISK_RT_DESCRIPTOR_STORAGE)
            return false;
          bool publicationChanged = false;
          uint64_t publicationBegin = UINT64_MAX;
          uint64_t publicationEnd = 0;
          uint64_t available = context->execution
                                   ? context->execution->state_bit_count
                                   : 0;
          for (uint64_t bit = 0; bit != update.bitWidth; ++bit) {
            if (bit > uint64_t{INT64_MAX} ||
                update.start > INT64_MAX - static_cast<int64_t>(bit))
              continue;
            int64_t coordinate = update.start + static_cast<int64_t>(bit);
            if (coordinate < update.begin || coordinate >= update.end ||
                coordinate < 0)
              continue;
            uint64_t destination = static_cast<uint64_t>(coordinate);
            if (destination >= available)
              return false;
            uint64_t limb = destination / 64;
            uint64_t mask = uint64_t{1} << (destination % 64);
            bool oldValue = (context->stateValue[limb] & mask) != 0;
            bool oldUnknown = (context->stateUnknown[limb] & mask) != 0;
            bool newValue = planeBit(update.value, bit);
            bool newUnknown = planeBit(update.unknown, bit);
            publicationChanged |= oldValue != newValue ||
                                  oldUnknown != newUnknown;
            publicationBegin = std::min(publicationBegin, destination);
            publicationEnd = std::max(publicationEnd, destination + 1);
            auto apply = [&](std::vector<uint64_t> &plane, bool value) {
              uint64_t old = plane[limb];
              uint64_t next = value ? old | mask : old & ~mask;
              changed |= old != next;
              plane[limb] = next;
            };
            apply(context->stateValue, newValue);
            apply(context->stateUnknown, newUnknown);
            uint32_t edges = transitionEdges(oldValue, oldUnknown, newValue,
                                             newUnknown);
            if (edges != 0) {
              if (context->nextSchedulerSequence == 0) {
                context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
                return false;
              }
              context->scheduledSignalEvents.push_back(
                  {context->nextSchedulerSequence++, destination, 1, edges});
            }
          }
          if (publicationChanged &&
              !obelisk_rt_notify_observer_signal_unlocked(
                  context, publicationBegin,
                  publicationEnd - publicationBegin))
            return false;
          return true;
        };
        for (;;) {
          uint64_t nativeSequence = UINT64_MAX;
          size_t nativeIndex = 0;
          for (size_t index = 0; index != context->scheduledNBAs.size(); ++index) {
            const ScheduledNBA &update = context->scheduledNBAs[index];
            if (update.dueTime <= context->schedulerTime &&
                update.sequence < nativeSequence) {
              nativeSequence = update.sequence;
              nativeIndex = index;
            }
          }
          uint64_t eventSequence = UINT64_MAX;
          size_t eventIndex = 0;
          for (size_t index = 0;
               index != context->scheduledDesignEvents.size(); ++index) {
            const ScheduledDesignEvent &event =
                context->scheduledDesignEvents[index];
            if (event.dueTime <= context->schedulerTime &&
                event.sequence < eventSequence) {
              eventSequence = event.sequence;
              eventIndex = index;
            }
          }
          uint64_t designSequence = UINT64_MAX;
          size_t designIndex = 0;
          for (size_t index = 0;
               index != context->scheduledDesignNBAs.size(); ++index) {
            const ScheduledDesignNBA &update =
                context->scheduledDesignNBAs[index];
            if (update.dueTime <= context->schedulerTime &&
                update.sequence < designSequence) {
              designSequence = update.sequence;
              designIndex = index;
            }
          }
          uint64_t sequence =
              std::min(nativeSequence, std::min(eventSequence, designSequence));
          if (sequence == UINT64_MAX)
            break;
          if (sequence == nativeSequence) {
            uint32_t retainedAutomaticID =
                context->scheduledNBAs[nativeIndex].retainedAutomaticID;
            applyNative(context->scheduledNBAs[nativeIndex]);
            if (retainedAutomaticID != 0) {
              auto found =
                  context->nativeAutomaticStates.find(retainedAutomaticID);
              if (found == context->nativeAutomaticStates.end() ||
                  found->second.referenceCount == 0)
                return OBELISK_RT_INVALID_HANDLE;
              if (--found->second.referenceCount == 0) {
                obelisk_rt_erase_automatic_signal_snapshots_unlocked(
                    context, retainedAutomaticID);
                context->nativeAutomaticStates.erase(found);
              }
            }
            context->scheduledNBAs.erase(context->scheduledNBAs.begin() +
                                         nativeIndex);
          } else if (sequence == eventSequence) {
            uint64_t stableID =
                context->scheduledDesignEvents[eventIndex].stableID;
            context->scheduledDesignEvents.erase(
                context->scheduledDesignEvents.begin() + eventIndex);
            uint64_t &generation = context->eventGenerations[stableID];
            if (++generation == 0)
              generation = 1;
            context->eventLastTriggeredTimes[stableID] =
                context->schedulerTime;
            if (!obelisk_rt_notify_observer_event_unlocked(context,
                                                           stableID))
              return context->schedulerStatus;
            eventTriggered = true;
          } else {
            if (!applyDesign(context->scheduledDesignNBAs[designIndex]))
              return OBELISK_RT_INVALID_HANDLE;
            context->scheduledDesignNBAs.erase(
                context->scheduledDesignNBAs.begin() + designIndex);
          }
        }
        if ((changed || eventTriggered) && ++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
        continue;
      }
      if (!selected && !context->schedulerRunningFinals) {
        std::optional<uint64_t> nextTime;
        auto considerTime = [&](uint64_t candidate) {
          if (candidate > context->schedulerTime &&
              (!nextTime || candidate < *nextTime))
            nextTime = candidate;
        };
        for (const ScheduledProcess &candidate : context->scheduledProcesses)
          if (candidate.instance && candidate.phase == 0 &&
              candidate.started &&
              candidate.suspendKind == OBELISK_RT_SUSPEND_DELAY)
            considerTime(candidate.wakeTime);
        for (const ScheduledDesignTask &candidate :
             context->scheduledDesignTasks)
          if (!candidate.terminated && candidate.phase == 0 &&
              candidate.started &&
              candidate.suspendKind == OBELISK_RT_SUSPEND_DELAY)
            considerTime(candidate.wakeTime);
        for (const ScheduledDesignNBA &update : context->scheduledDesignNBAs)
          if (update.dueTime > context->schedulerTime)
            considerTime(update.dueTime);
        for (const ScheduledNBA &update : context->scheduledNBAs)
          if (update.dueTime > context->schedulerTime)
            considerTime(update.dueTime);
        for (const ScheduledDesignEvent &event :
             context->scheduledDesignEvents)
          if (event.dueTime > context->schedulerTime)
            considerTime(event.dueTime);
        if (nextTime) {
          context->schedulerTime = *nextTime;
          continue;
        }
        bool hasFinal = false;
        for (const ScheduledProcess &candidate : context->scheduledProcesses)
          hasFinal |= candidate.instance && candidate.phase == 1;
        for (const ScheduledDesignTask &candidate :
             context->scheduledDesignTasks)
          hasFinal |= !candidate.terminated && candidate.phase == 1;
        if (hasFinal) {
          context->schedulerRunningFinals = true;
          continue;
        }
      }
    }
    if (!selected) {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      return context->schedulerFinishStatus;
    }

    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->activeNativeProcess)
        return OBELISK_RT_INVALID_LIFECYCLE;
      context->activeNativeProcess = selected;
      context->activeLogicalProcessToken =
          kNativeLogicalProcessTag |
          context->scheduledProcesses[selectedIndex].token;
      context->activeControls =
          std::move(context->scheduledProcesses[selectedIndex].controls);
    }
    obelisk_rt_fragment_action_v1 action{};
    obelisk_rt_execution_tier tier = selected->tier;
    if (tier != OBELISK_RT_TIER_NATIVE && tier != OBELISK_RT_TIER_BYTECODE)
      tier = (selected->descriptor->available_tiers &
              OBELISK_RT_TIER_MASK_NATIVE)
                 ? OBELISK_RT_TIER_NATIVE
                 : OBELISK_RT_TIER_BYTECODE;
    obelisk_rt_status status = obelisk_rt_v1_process_instance_execute(
        selected, context, tier, &action);
    bool terminationRequested = false;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (selectedIndex < context->scheduledProcesses.size() &&
          context->scheduledProcesses[selectedIndex].instance == selected)
        context->scheduledProcesses[selectedIndex].controls =
            std::move(context->activeControls);
      context->activeControls.clear();
      context->activeNativeProcess = nullptr;
      context->activeLogicalProcessToken = 0;
      terminationRequested = context->schedulerFinishRequested;
    }
    if (!terminationRequested && status != OBELISK_RT_OK)
      return status;
    auto destroyPendingCallee =
        [](obelisk_rt_process_instance_v1 *instance) noexcept {
          if (instance)
            (void)obelisk_rt_v1_process_instance_destroy(instance);
        };
    std::unique_ptr<obelisk_rt_process_instance_v1,
                    decltype(destroyPendingCallee)>
        pendingCallee(nullptr, destroyPendingCallee);
    if (status == OBELISK_RT_OK &&
        action.kind == OBELISK_RT_FRAGMENT_TASK_CALL) {
      auto *callee = reinterpret_cast<obelisk_rt_process_instance_v1 *>(
          static_cast<uintptr_t>(action.payload));
      if (!callee || callee == selected)
        return OBELISK_RT_INVALID_LIFECYCLE;
      pendingCallee.reset(callee);
      if (callee->lifecycle != OBELISK_RT_PROCESS_READY ||
          !callee->descriptor ||
          callee->descriptor->execution != selected->descriptor->execution ||
          (callee->ownership_context &&
           callee->ownership_context != context))
        return OBELISK_RT_INVALID_LIFECYCLE;
    }
    if (terminationRequested)
      action = {OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE,
                0, 0, 0, 0};
    bool destroy = false;
    std::vector<obelisk_rt_process_instance_v1 *> terminatedCallers;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (selectedIndex >= context->scheduledProcesses.size() ||
          context->scheduledProcesses[selectedIndex].instance != selected)
        return OBELISK_RT_INVALID_LIFECYCLE;
      ScheduledProcess &scheduled = context->scheduledProcesses[selectedIndex];
      if (action.kind == OBELISK_RT_FRAGMENT_TERMINATE) {
        if (!scheduled.callers.empty() &&
            !context->schedulerFinishRequested) {
          scheduled.instance = scheduled.callers.back();
          scheduled.callers.pop_back();
          scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
          scheduled.waitOffset = 0;
          scheduled.waitSize = 0;
          scheduled.waitGenerations.clear();
          scheduled.signalTriggered = false;
          scheduled.urgent = true;
          destroy = true;
        } else {
          uint64_t token = scheduled.token;
          scheduled.instance = nullptr;
          if (terminationRequested)
            terminatedCallers.swap(scheduled.callers);
          obelisk_rt_release_controls_unlocked(context, scheduled.controls);
          scheduled.controls.clear();
          destroy = true;
          context->terminatedNativeProcesses.insert(token);
          scheduled.signalTriggered = false;
          scheduled.urgent = false;
          if (++context->schedulerEpoch == 0)
            context->schedulerEpoch = 1;
        }
      } else if (action.kind == OBELISK_RT_FRAGMENT_SUSPEND) {
        scheduled.suspendKind = action.suspend_kind;
        scheduled.waitOffset = action.payload;
        scheduled.waitSize = action.auxiliary;
        if (!scheduled.continuationRanks.empty()) {
          auto rank = std::lower_bound(
              scheduled.continuationRanks.begin(),
              scheduled.continuationRanks.end(), action.continuation,
              [](const std::pair<uint32_t, uint32_t> &entry,
                 uint32_t continuation) { return entry.first < continuation; });
          if (rank != scheduled.continuationRanks.end() &&
              rank->first == action.continuation)
            scheduled.scheduleRank = rank->second;
        }
        scheduled.observedEpoch = context->schedulerEpoch;
        scheduled.observedSignalSequence = context->nextSchedulerSequence;
        scheduled.waitGenerations.clear();
        scheduled.signalTriggered = false;
        scheduled.urgent = false;
        const auto *wait =
            reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
                static_cast<const uint8_t *>(selected->frame) +
                action.payload);
        scheduled.queuedRegion =
            action.suspend_kind == OBELISK_RT_SUSPEND_DELAY &&
                    wait->payload == 0
                ? 1
                : 0;
        if (action.suspend_kind == OBELISK_RT_SUSPEND_EVENT) {
          wait = currentWait(scheduled);
          if (!wait)
            return OBELISK_RT_INVALID_FRAME;
          const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
          scheduled.waitGenerations.reserve(wait->count);
          for (uint32_t index = 0; index != wait->count; ++index)
            scheduled.waitGenerations.push_back(
                context->eventGenerations[entries[index].stable_id]);
        }
        if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY) {
          scheduled.wakeTime =
              wait->payload > UINT64_MAX - context->schedulerTime
                  ? UINT64_MAX
                  : context->schedulerTime + wait->payload;
        }
      } else if (action.kind == OBELISK_RT_FRAGMENT_TASK_CALL) {
        auto *callee = pendingCallee.get();
        if (!callee)
          return OBELISK_RT_INVALID_LIFECYCLE;
        if (scheduled.callers.size() ==
            std::numeric_limits<size_t>::max())
          return OBELISK_RT_OUT_OF_RESOURCES;
        scheduled.callers.reserve(scheduled.callers.size() + 1);
        scheduled.callers.push_back(selected);
        scheduled.instance = callee;
        scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
        scheduled.waitOffset = 0;
        scheduled.waitSize = 0;
        scheduled.waitGenerations.clear();
        scheduled.signalTriggered = false;
        scheduled.urgent = true;
        pendingCallee.release();
      } else {
        scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
        scheduled.waitOffset = 0;
        scheduled.waitSize = 0;
        scheduled.waitGenerations.clear();
        scheduled.signalTriggered = false;
        scheduled.urgent = false;
      }
    }
    if (destroy) {
      status = obelisk_rt_v1_process_instance_destroy(selected);
      if (status != OBELISK_RT_OK)
        return status;
    }
    for (auto caller = terminatedCallers.rbegin();
         caller != terminatedCallers.rend(); ++caller) {
      status = obelisk_rt_v1_process_instance_destroy(*caller);
      if (status != OBELISK_RT_OK)
        return status;
    }
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_run(obelisk_rt_context *context) {
  ContextTransaction transaction(context);
  try {
    return runScheduler(context);
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}
