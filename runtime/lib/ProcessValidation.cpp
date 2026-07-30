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
    } else if (field.flags == OBELISK_RT_FRAME_MANAGED_ROOT) {
      if (field.size != sizeof(obelisk_rt_object_v1 *) ||
          field.alignment < alignof(obelisk_rt_object_v1 *))
        return OBELISK_RT_LAYOUT_MISMATCH;
      previousEnd = end;
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

template <typename T>
const T *computedSpan(const obelisk_rt_computed_wait_record_v1 *wait,
                      uint64_t offset, uint64_t count) {
  if (!wait || offset > wait->total_size ||
      count > (wait->total_size - offset) / sizeof(T))
    return nullptr;
  return reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(wait) +
                                     offset);
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
  const auto *observers = computedSpan<obelisk_rt_computed_observer_v1>(
      wait, wait->observers_offset, wait->observer_count);
  const auto *captures = computedSpan<obelisk_rt_computed_capture_v1>(
      wait, wait->captures_offset, wait->capture_count);
  const auto *dependencies = computedSpan<obelisk_rt_computed_dependency_v1>(
      wait, wait->dependencies_offset, wait->dependency_count);
  const auto *clauses = computedSpan<obelisk_rt_computed_clause_v1>(
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
      obelisk_rt_stable_handle_v1 decoded;
      if (!obelisk_rt_stable_handle_decode(
              captures[observer.capture_begin + capture].stable_id, &decoded))
        return false;
    }
    for (uint32_t dependency = 0; dependency != observer.dependency_count;
         ++dependency) {
      const obelisk_rt_computed_dependency_v1 &entry =
          dependencies[observer.dependency_begin + dependency];
      if ((entry.kind != OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL &&
           entry.kind != OBELISK_RT_OBSERVER_DEPENDENCY_EVENT) ||
          entry.width == 0 ||
          (entry.kind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT &&
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
        (clause.flags & ~OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY) != 0)
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
